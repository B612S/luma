#include "MediaManager.hpp"
#include "AsyncFileWriter.hpp"
#include "IcyMetadataReader.hpp"
#include "PlaylistManager.hpp"
#include "MediaPaths.hpp"
#include "ConfigManager.hpp"
#include "YtDlpRuntime.hpp"
#include "MediaInfoService.hpp"
#include "MediaNotifier.hpp"
#include "TrailingSilenceDetector.hpp"
#include "MediaFileActions.hpp"
#include "Logger.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QUrl>
#include <QStandardPaths>
#include <QTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QFutureWatcher>
#include <QTemporaryDir>
#include <QTextStream>
#include <QVector>
#include <algorithm>
#include <QtConcurrent/QtConcurrentRun>


namespace {

bool isDirectStreamUrl(const QUrl &url) {
    const QString scheme = url.scheme();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return false;

    // An explicit port almost always means Icecast/Shoutcast radio.
    if (url.port(-1) != -1) return true;

    const QString path = url.path().toLower();
    static const QStringList streamExts = {
        ".m3u8", ".mp3", ".aac", ".ogg", ".opus", ".flac", ".m4a",
        ".mp4", ".webm", ".mkv"
    };
    for (const QString &ext : streamExts) {
        if (path.endsWith(ext)) return true;
    }

    return path.isEmpty() || path == QStringLiteral("/");
}

// Only live/radio URLs should lose seeking. A direct .mp4/.mkv/.webm URL is
// still a finite video, even though it bypasses yt-dlp.
bool isLiveStreamUrl(const QUrl &url) {
    const QString scheme = url.scheme();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return false;

    if (url.port(-1) != -1) return true; // Icecast/Shoutcast usually uses a port

    const QString path = url.path().toLower();
    static const QStringList liveExts = {
        ".m3u8", ".mp3", ".aac", ".ogg", ".opus", ".flac", ".m4a"
    };
    for (const QString &ext : liveExts) {
        if (path.endsWith(ext)) return true;
    }
    return path.isEmpty() || path == QStringLiteral("/");
}

// yt-dlp -j output is normally a few hundred KB at most. Cap it so a broken or
// malicious helper cannot grow Luma's memory without bounds.
constexpr qsizetype kMaxYtdlpStdout = 8 * 1024 * 1024;

// Title for the notification: don't show "watch" or bare URLs.
QString urlTitle(const QString &url) {
    const QUrl u(url);
    QString title = QFileInfo(u.path()).completeBaseName();
    const QString host = u.host();
    if (title.isEmpty() || title == QLatin1String("watch")
            || host.contains(QStringLiteral("youtube"), Qt::CaseInsensitive)
            || host.contains(QStringLiteral("youtu.be"), Qt::CaseInsensitive))
        title = host.isEmpty() ? url : host;
    return title;
}

} // namespace

MediaManager::MediaManager(MpvItem *mpv, QObject *parent)
: QObject(parent), m_mpv(mpv), m_playlists(nullptr)
{
    m_notifier = new MediaNotifier(this);

    m_fileActions = new MediaFileActions(this);
    connect(m_fileActions, &MediaFileActions::defaultFileManagerNameChanged,
            this, &MediaManager::defaultFileManagerNameChanged);
    m_fileActions->resolveDefaultFileManagerName();

    m_silenceDetector = new TrailingSilenceDetector(this);
    connect(m_silenceDetector, &TrailingSilenceDetector::detected, this,
            [this](const QString &path, quint64 generation, double end) {
        if (generation != m_silenceGeneration)
            return;
        if (!m_mpv || m_mpv->source() != path)
            return;
        m_silenceEffectiveEnd = end;
        updateCrossfade();
    });

    m_mediaInfo = new MediaInfoService(this);
    connect(m_mediaInfo, &MediaInfoService::fileInfoReady,
            this, &MediaManager::fileInfoReady);
    connect(m_mediaInfo, &MediaInfoService::tagsSaved,
            this, &MediaManager::tagsSaved);
    connect(m_mediaInfo, &MediaInfoService::liveBitrateReady,
            this, &MediaManager::liveBitrateReady);

    m_ytDlpRuntime = new YtDlpRuntime(this);
    connect(m_ytDlpRuntime, &YtDlpRuntime::ytdlpInstallFinished,
            this, &MediaManager::ytdlpInstallFinished);
    connect(m_ytDlpRuntime, &YtDlpRuntime::ytdlpEjsInstallFinished,
            this, &MediaManager::ytdlpEjsInstallFinished);
    connect(m_ytDlpRuntime, &YtDlpRuntime::denoInstallFinished,
            this, &MediaManager::denoInstallFinished);
    connect(m_ytDlpRuntime, &YtDlpRuntime::ytdlpRuntimeReady,
            this, &MediaManager::ytdlpRuntimeReady);

    // Resolving a URL via yt-dlp must not hang forever.
    m_ytdlpTimeout = new QTimer(this);
    m_ytdlpTimeout->setSingleShot(true);
    m_ytdlpTimeout->setInterval(30000);
    m_icyReader = new IcyMetadataReader(this);
    connect(m_icyReader, &IcyMetadataReader::titleChanged,
            this, &MediaManager::onIcyTitleChanged);
    m_icyTitleTimer = new QTimer(this);
    m_icyTitleTimer->setSingleShot(true);
    connect(m_icyTitleTimer, &QTimer::timeout, this, &MediaManager::applyIcyTitle);
    connect(m_ytdlpTimeout, &QTimer::timeout, this, [this]() {
        if (!m_ytdlpProcess || m_ytdlpProcess->state() == QProcess::NotRunning) return;
        m_ytdlpTimedOut = true;
        m_ytdlpProcess->kill();
    });

    // Resume positions live in the cache directory and are bounded in size.
    m_positions.load(positionsFilePath());
    m_positionWriter = new AsyncFileWriter(this);
    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(10000);
    connect(m_positionTimer, &QTimer::timeout, this, [this]() { saveCurrentPosition(); });

    // The QML engine (and with it the MpvItem) is destroyed before this
    // object, so the final position must be saved while playback is alive.
    if (QCoreApplication *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
            saveCurrentPosition();
            if (m_positionWriter) m_positionWriter->waitForIdle();
        });
    }
}

MediaManager::~MediaManager() {
    delete m_chaptersDir;
    m_chaptersDir = nullptr;
    if (m_positionWriter) m_positionWriter->waitForIdle();
    if (m_positionTimer) m_positionTimer->stop();
    if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
    if (m_ytdlpProcess) {
        m_ytdlpProcess->disconnect(this);
        m_ytdlpProcess->kill();
        m_ytdlpProcess->waitForFinished(1000);
    }
}

void MediaManager::setConfig(ConfigManager *config) {
    m_config = config;
    if (m_notifier) m_notifier->setConfig(config);
    if (config) {
        connect(config, &ConfigManager::musicCrossfadeChanged,
                this, &MediaManager::updateCrossfade, Qt::UniqueConnection);
    }
    updateCrossfade();
}

void MediaManager::setCrossfadeFilter(const QString &filter) {
    if (m_crossfadeFilter == filter) return;
    m_crossfadeFilter = filter;
    emit crossfadeFilterChanged();
}

void MediaManager::updateCrossfade() {
    const int fade = m_config ? m_config->musicCrossfade() : 0;
    const QString source = m_mpv ? m_mpv->source() : QString();
    if (!m_mpv || !m_config || !m_playlists || fade <= 0
            || source.isEmpty() || source.contains(QStringLiteral("://"))
            || !isAudioFilePath(source)) {
        m_crossfadeFadeInCurrent = false;
        m_pendingCrossfadeIn = false;
        m_crossfadeOutScheduled = false;
        setCrossfadeFilter(QString());
        return;
    }

    QStringList filters;
    if (m_crossfadeFadeInCurrent)
        filters << QStringLiteral("afade=t=in:st=0:d=%1").arg(fade);

    m_crossfadeOutScheduled = false;
    const double duration = m_mpv->duration();
    PlaylistModel *model = m_playlists->playingModel();
    int next = -1;
    if (model) {
        const int current = model->currentIndex();
        const QString mode = m_playlists->playlistModeFor(model);
        if (mode == QLatin1String("repeat")) {
            next = current;
        } else if (mode != QLatin1String("stop")) {
            next = current + 1;
            if (next >= model->rowCount())
                next = -1;
        }
    }
    const QString nextPath = next >= 0 ? model->fileAt(next) : QString();
    const bool nextIsMusic = !nextPath.isEmpty()
            && !nextPath.contains(QStringLiteral("://"))
            && isAudioFilePath(nextPath)
            && QFileInfo::exists(nextPath);
    if (nextIsMusic) {
        if (m_silenceSource != source) {
            m_silenceSource = source;
            m_silenceEffectiveEnd = -1.0;
            m_silenceTransitionTriggered = false;
            startSilenceScan(source);
        }
        // A detected silent tail shortens the fade and the effective end, so
        // the next track starts at the last audible sample.
        const double effectiveEnd = m_silenceEffectiveEnd > 0.0
            ? qMin(m_silenceEffectiveEnd, duration) : duration;
        if (effectiveEnd > fade + 0.25) {
            const double start = effectiveEnd - fade;
            filters << QStringLiteral("afade=t=out:st=%1:d=%2")
                           .arg(QString::number(start, 'f', 3))
                           .arg(fade);
            m_crossfadeOutScheduled = true;
        }
    }

    setCrossfadeFilter(filters.join(QLatin1Char(',')));
}

void MediaManager::resetSilenceState() {
    ++m_silenceGeneration;
    m_silenceSource.clear();
    m_silenceEffectiveEnd = -1.0;
    m_silenceTransitionTriggered = false;
    m_silenceLoadTimer.invalidate();
}

void MediaManager::startSilenceScan(const QString &path) {
    if (path.isEmpty() || !m_silenceDetector)
        return;
    // Do not advance from the detection callback: right after a source change
    // mpv's position can still be the previous file's position, which would
    // immediately skip the new track. positionChanged() sees the new position.
    m_silenceDetector->request(path, m_silenceGeneration);
}

void MediaManager::maybeAdvancePastSilence(double position) {
    if (m_silenceEffectiveEnd <= 0.0 || m_silenceTransitionTriggered)
        return;
    if (!m_mpv || !m_config || m_config->musicCrossfade() <= 0)
        return;
    if (m_mpv->paused() || m_silenceSource != m_mpv->source())
        return;
    if (!m_silenceLoadTimer.isValid() || m_silenceLoadTimer.elapsed() < 500)
        return;
    if (!m_crossfadeOutScheduled)
        return;
    const double end = qMin(m_silenceEffectiveEnd, m_mpv->duration());
    if (end <= 0.0 || position < end - 0.02)
        return;

    // The trailing silence is what the user wants to skip: advance at the last
    // audible position, carrying the fade-in flag to the next music track.
    m_silenceTransitionTriggered = true;
    m_pendingCrossfadeIn = true;
    playNext();
}

void MediaManager::setMpv(MpvItem *mpv) {
    if (m_mpv == mpv) return;
    if (m_mpv) disconnect(m_mpv, nullptr, this, nullptr);
    m_mpv = mpv;
    if (m_mpv) {
        connect(m_mpv, &MpvItem::mediaTitleChanged, this, &MediaManager::onMediaTitleChanged);
        connect(m_mpv, &MpvItem::sourceChanged, this, [this]() { setLoading(true); });
        connect(m_mpv, &MpvItem::fileLoaded, this, &MediaManager::onMpvFileLoaded);
        connect(m_mpv, &MpvItem::fileEnded, this, [this](const QString &reason) {
            saveCurrentPosition();
            setLoading(false);
            if (reason == QLatin1String("eof") && m_crossfadeOutScheduled)
                m_pendingCrossfadeIn = true;
        });
        connect(m_mpv, &MpvItem::durationChanged, this, [this]() { updateCrossfade(); });
        connect(m_mpv, &MpvItem::positionChanged, this, [this]() {
            if (m_mpv)
                maybeAdvancePastSilence(m_mpv->position());
        });
        connect(m_mpv, &MpvItem::pausedChanged, this, [this]() {
            if (!m_mpv) return;
            emit pluginPausedChanged(m_mpv->paused());
            if (m_mpv->paused()) saveCurrentPosition();
        });
        connect(m_mpv, &MpvItem::volumeChanged, this, [this]() {
            if (m_mpv)
                emit pluginVolumeChanged(m_mpv->volume());
        });
        connect(m_mpv, &MpvItem::positionChanged, this, [this]() {
            if (!m_mpv) return;
            // Do not flood plugins with a signal for every frame.
            if (m_pluginPositionTimer.isValid() && m_pluginPositionTimer.elapsed() < 1000)
                return;
            m_pluginPositionTimer.restart();
            emit pluginPositionChanged(m_mpv->position(), m_mpv->duration());
        });
        connect(m_mpv, &QObject::destroyed, this, [this]() { m_mpv = nullptr; });
    }
}

void MediaManager::refreshYtdlpRuntime()
{
    if (m_ytDlpRuntime)
        m_ytDlpRuntime->refreshYtdlpRuntime();
}

void MediaManager::installYtdlp()
{
    if (m_ytDlpRuntime)
        m_ytDlpRuntime->installYtdlp();
}

void MediaManager::installYtdlpEjs()
{
    if (m_ytDlpRuntime)
        m_ytDlpRuntime->installYtdlpEjs();
}

void MediaManager::installDeno()
{
    if (m_ytDlpRuntime)
        m_ytDlpRuntime->installDeno();
}

void MediaManager::setLiveStream(bool live) {
    if (m_liveStream == live) return;
    m_liveStream = live;
    emit liveStreamChanged();
}

void MediaManager::setLoading(bool loading) {
    if (m_loading == loading) return;
    m_loading = loading;
    emit loadingChanged();
}

void MediaManager::setHasMedia(bool hasMedia) {
    if (m_hasMedia == hasMedia) return;
    m_hasMedia = hasMedia;
    emit hasMediaChanged();
}

void MediaManager::onMediaTitleChanged() {
    if (!m_mpv) return;

    const QString source = m_mpv->source();
    if (!source.startsWith(QStringLiteral("http://")) && !source.startsWith(QStringLiteral("https://")))
        return;

    // For URLs resolved by yt-dlp, the notification has already been sent with
    // the original URL; media-title arrives as a googlevideo fragment and is not needed.
    if (m_streamFromYtdlp)
        return;

    const QString title = m_mpv->mediaTitle().trimmed();
    if (title.isEmpty() || title == m_lastStreamTitle) return;
    // For direct streams mpv often reports the URL (or just host:port) as the
    // title — don't show it in the playlist or notifications.
    const QUrl sourceUrl(source);
    if (title.startsWith(QStringLiteral("http://")) || title.startsWith(QStringLiteral("https://"))
            || title == sourceUrl.toString()
            || title == sourceUrl.authority()
            || title == sourceUrl.host()
            || title == sourceUrl.path())
        return;
    m_lastStreamTitle = title;

    // In the playlist, show the current tag instead of the URL.
    if (m_playlists) {
        PlaylistModel *model = m_playlists->activeModel();
        if (model) model->setStreamTitle(source, title);
    }

    if (m_config && m_config->notifications()) {
        const QUrl url(source);
        m_notifier->notify(title, url.host());
    }
}

void MediaManager::onIcyTitleChanged(const QString &title)
{
    if (title.isEmpty() || title == m_lastStreamTitle || title == m_pendingIcyTitle)
        return;

    // The reader sees the live ICY title, while mpv plays behind its forward
    // cache. Delay the playlist/notification update by that buffer so the title
    // changes when the song actually starts.
    double delay = 0.0;
    if (m_mpv) {
        const double cacheEnd = m_mpv->observedProperty("demuxer-cache-time").toDouble();
        const double position = m_mpv->position();
        if (cacheEnd > position && cacheEnd - position < 120.0)
            delay = cacheEnd - position;
    }
    m_pendingIcyTitle = title;
    if (!m_icyTitleTimer) return;
    m_icyTitleTimer->start(qRound(delay * 1000.0));
}

void MediaManager::applyIcyTitle()
{
    const QString title = m_pendingIcyTitle;
    m_pendingIcyTitle.clear();
    if (title.isEmpty() || title == m_lastStreamTitle) return;
    m_lastStreamTitle = title;

    if (m_playlists) {
        PlaylistModel *model = m_playlists->activeModel();
        if (model && m_mpv)
            model->setStreamTitle(m_mpv->source(), title);
    }

    if (m_config && m_config->notifications() && m_mpv) {
        const QUrl url(m_mpv->source());
        m_notifier->notify(title, url.host());
    }
}

PlaylistModel* MediaManager::playlist() const {
    return m_playlists ? m_playlists->activeModel() : nullptr;
}

void MediaManager::setPlaylistManager(PlaylistManager *pm) {
    if (m_playlists == pm) return;
    if (m_playlists) m_playlists->disconnect(this);
    m_playlists = pm;
    if (m_playlists) {
        // Each playlist has its own playback mode: follow the active one.
        connect(m_playlists, &PlaylistManager::activeModelChanged, this, [this]() {
            if (!m_playlists) return;
            const QString mode = m_playlists->playlistMode(m_playlists->activeId());
            if (m_playMode == mode) return;
            m_playMode = mode;
            emit playModeChanged();
        });
        // A playlist's mode can also be changed from its Properties dialog
        // while that playlist is active.
        connect(m_playlists, &PlaylistManager::playlistModeChanged, this,
                [this](const QString &id, const QString &mode) {
            if (!m_playlists || id != m_playlists->activeId())
                return;
            if (m_playMode == mode) return;
            m_playMode = mode;
            emit playModeChanged();
        });
    }
    emit playlistChanged();
}

void MediaManager::setManualSwitch(bool val) {
    if (m_isManualSwitch != val) { m_isManualSwitch = val; emit isManualSwitchChanged(); }
}

void MediaManager::setPlayMode(const QString &mode) {
    static const QStringList modes = {
        QStringLiteral("sequential"), QStringLiteral("repeat"),
        QStringLiteral("shuffle"), QStringLiteral("stop")
    };
    const QString normalized = modes.contains(mode) ? mode : QStringLiteral("sequential");
    if (m_playMode == normalized) return;
    m_playMode = normalized;

    if (m_playlists) {
        // The settings only define the Default playlist's mode; every named
        // playlist stores its own mode in its playlist file.
        m_playlists->setPlaylistMode(m_playlists->activeId(), normalized);

        // Applying the mode (including the shuffle order) is owned by
        // PlaylistManager::setPlaylistMode.
    }

    emit playModeChanged();
}

void MediaManager::setAspectRatio(const QString &mode) {
    if (!m_mpv) return;
    if (mode == "original") {
        m_mpv->setProperty("video-aspect-override", "no");
    } else {
        m_mpv->setProperty("video-aspect-override", mode);
    }
}

QString MediaManager::getAspectRatio() const {
    if (!m_mpv) return "original";
    // Observed by MpvItem: reading it synchronously here could block while mpv
    // is opening a network stream.
    QVariant val = m_mpv->observedProperty("video-aspect-override");
    if (!val.isValid() || val.isNull()) return "original";
    if (val.typeId() == QMetaType::QString) {
        QString str = val.toString().trimmed();
        if (str == "no" || str.isEmpty()) return "original";
        return str;
    }
    if (val.canConvert<double>()) {
        double ratio = val.toDouble();
        if (ratio <= 0.0) return "original";
        if (qAbs(ratio - 1.777778) < 0.01) return "16:9";
        if (qAbs(ratio - 1.333333) < 0.01) return "4:3";
        if (qAbs(ratio - 2.333333) < 0.01) return "21:9";
        if (qAbs(ratio - 1.0) < 0.01)      return "1:1";
        if (qAbs(ratio - 0.5625) < 0.01)   return "9:16";
        return QString::number(ratio, 'f', 2);
    }
    return "original";
}

QString MediaManager::defaultFileManagerName() const
{
    return m_fileActions ? m_fileActions->defaultFileManagerName()
                         : QStringLiteral("File Manager");
}

QVariantMap MediaManager::pluginStats() const
{
    QVariantMap result = pluginState();
    if (!m_mpv)
        return result;
    const QStringList properties = {
        QStringLiteral("container-fps"), QStringLiteral("estimated-vf-fps"),
        QStringLiteral("display-fps"), QStringLiteral("video-codec"),
        QStringLiteral("audio-codec"), QStringLiteral("width"),
        QStringLiteral("height"), QStringLiteral("video-bitrate"),
        QStringLiteral("audio-bitrate"), QStringLiteral("hwdec-current"),
        QStringLiteral("frame-drop-count"), QStringLiteral("avsync"),
        QStringLiteral("demuxer-cache-duration"), QStringLiteral("vf")
    };
    for (const QString &property : properties)
        result.insert(property, m_mpv->observedProperty(property));
    // mpv's display-fps is often unavailable under libmpv/Wayland. Qt knows
    // the compositor-reported refresh rate and gives the plugin a useful
    // fallback instead of reporting n/a.
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (screen)
        result.insert(QStringLiteral("screen-refresh-rate"), screen->refreshRate());
    return result;
}

QVariantMap MediaManager::pluginState() const
{
    QVariantMap state;
    state.insert(QStringLiteral("hasMedia"), m_hasMedia);
    state.insert(QStringLiteral("loading"), m_loading);
    state.insert(QStringLiteral("live"), m_liveStream);
    state.insert(QStringLiteral("path"), m_mpv ? m_mpv->source() : QString());
    state.insert(QStringLiteral("title"), m_mpv ? m_mpv->mediaTitle() : QString());
    state.insert(QStringLiteral("position"), m_mpv ? m_mpv->position() : 0.0);
    state.insert(QStringLiteral("duration"), m_mpv ? m_mpv->duration() : 0.0);
    state.insert(QStringLiteral("paused"), m_mpv ? m_mpv->paused() : true);
    state.insert(QStringLiteral("volume"), m_mpv ? m_mpv->volume() : 0.0);
    state.insert(QStringLiteral("muted"), m_mpv ? m_mpv->muted() : false);
    state.insert(QStringLiteral("hasVideo"), hasVideo());
    return state;
}

void MediaManager::pluginTogglePause()
{
    if (m_mpv)
        m_mpv->setPaused(!m_mpv->paused());
}

void MediaManager::pluginSetPaused(bool paused)
{
    if (m_mpv)
        m_mpv->setPaused(paused);
}

void MediaManager::pluginSeek(double position)
{
    if (m_mpv && position >= 0.0)
        m_mpv->seekExact(position);
}

void MediaManager::pluginSetVolume(int volume)
{
    if (m_mpv)
        m_mpv->setVolume(qBound(0, volume, 100));
}

void MediaManager::pluginStop()
{
    stopPlayback();
}

bool MediaManager::pluginOpen(const QString &path)
{
    if (path.trimmed().isEmpty())
        return false;
    openFile(path);
    return true;
}

QVariant MediaManager::pluginMpvGetProperty(const QString &name) const
{
    if (!m_mpv || name.isEmpty())
        return {};
    return m_mpv->property(name);
}

void MediaManager::pluginMpvSetProperty(const QString &name, const QVariant &value)
{
    if (!m_mpv || name.isEmpty())
        return;
    m_mpv->setProperty(name, value);
}

bool MediaManager::pluginMpvCommand(const QStringList &args)
{
    if (!m_mpv || args.isEmpty())
        return false;
    const QVariant reply = m_mpv->commandBlocking(args);
    // MpvController::command() reports failures as an ErrorReturn value.
    return reply.userType() != qMetaTypeId<ErrorReturn>();
}

QString MediaManager::currentFileName() const {
    if (!m_mpv) return "";
    QString path = m_mpv->source();
    if (path.isEmpty()) return "";
    return QFileInfo(path).fileName();
}

bool MediaManager::hasVideo() const {
    if (!m_mpv) return false;

    // All three properties are observed by MpvItem. Using the cached values
    // avoids a blocking round-trip to the mpv thread on every menu/hotkey.
    const QString codec = m_mpv->observedProperty("video-codec").toString().trimmed();
    if (codec.isEmpty() || codec.compare(QStringLiteral("no"), Qt::CaseInsensitive) == 0)
        return false;

    auto flag = [this](const char *name) {
        const QVariant value = m_mpv->observedProperty(name);
        if (!value.isValid()) return false;
        if (value.toBool()) return true;
        const QString text = value.toString().trimmed().toLower();
        return text == QLatin1String("yes") || text == QLatin1String("true");
    };

    // Audio covers are exposed as image/album-art video tracks.
    return !flag("current-tracks/video/image")
        && !flag("current-tracks/video/albumart");
}

void MediaManager::openInFileManager(const QString &path) {
    if (m_fileActions) m_fileActions->openInFileManager(path);
}

bool MediaManager::openInNewWindow(const QString &path) {
    return m_fileActions && m_fileActions->openInNewWindow(path);
}

void MediaManager::copyToClipboard(const QString &text) {
    if (m_fileActions) m_fileActions->copyToClipboard(text);
}

bool MediaManager::isFileAvailable(const QString &path) const {
    return MediaFileActions::isFileAvailable(path);
}

void MediaManager::requestFileInfo(const QString &path) {
    m_mediaInfo->requestFileInfo(path);
}

void MediaManager::requestLiveBitrate(const QString &path, double position) {
    m_mediaInfo->requestLiveBitrate(path, position);
}

QVariantMap MediaManager::readTags(const QString &path) {
    return m_mediaInfo->readTags(path);
}

bool MediaManager::saveTags(const QString &path, const QVariantMap &tags) {
    return m_mediaInfo->saveTags(path, tags);
}

void MediaManager::requestSaveTags(const QString &path, const QVariantMap &tags) {
    m_mediaInfo->requestSaveTags(path, tags);
}

bool MediaManager::updateStreamUrl(const QString &oldUrl, const QString &newUrl) {
    const QString url = newUrl.trimmed();
    if (url.isEmpty() || !m_playlists) return false;

    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return false;

    const int index = model->indexOf(oldUrl);
    int index2 = index;
    if (index2 < 0) {
        // The URL may differ by a trailing "/" — look for the closest matching entry.
        for (int i = 0; i < model->rowCount(); ++i) {
            const QString p = model->fileAt(i);
            if (p.isEmpty() || oldUrl.isEmpty()) continue;
            QString a = p; while (a.endsWith('/')) a.chop(1);
            QString b = oldUrl; while (b.endsWith('/')) b.chop(1);
            if (a == b) { index2 = i; break; }
        }
    }
    if (index2 < 0) return false;
    const int streamIndex = index2;

    const bool wasCurrent = (model->currentIndex() == streamIndex);
    QVariantMap entry;
    entry["path"] = url;
    entry["kind"] = "audio";
    model->removeItem(streamIndex);
    model->appendEntry(entry);

    // Fetch the title for the new link: after a broken URL was corrected the
    // entry used to stay without a title forever (the old title belonged to
    // the previous URL). This also reports failures instead of silently
    // keeping a stale/empty title.
    if (url.startsWith(QStringLiteral("http://")) || url.startsWith(QStringLiteral("https://")))
        m_playlists->fetchUrlTitle(m_playlists->activeId(), url);

    if (wasCurrent && m_mpv) {
        preparePlaybackPosition(url, m_liveStream);
        m_mpv->setSource(url, m_liveStream);
    }
    return true;
}

void MediaManager::removeFromPlaylist(int index) {
    if (!m_playlists) return;
    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return;

    if (index == model->currentIndex()) {
        int nextIndex = model->currentIndex();
        model->removeItem(index);

        if (nextIndex >= 0 && nextIndex < model->rowCount()) {
            playIndex(nextIndex);
        } else {
            if (m_mpv) m_mpv->command(QStringList() << "stop");
        }
    } else {
        model->removeItem(index);
    }

    // PlaylistModel::removeItem() emits modified, so the named playlist is
    // autosaved in the background; no blocking save is needed here.
}

void MediaManager::addToPlaylist(const QString &path) {
    if (!m_mpv || !m_playlists) return;

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) return;

    // Explicit add goes to the ACTIVE playlist
    QString id = m_playlists->activeId();
    PlaylistModel *model = m_playlists->activeModel();
    if (!model) return;

    int existingIndex = model->indexOf(cleanPath);
    if (existingIndex >= 0) {
        playIndex(existingIndex);
        return;
    }

    m_playlists->addFilesToPlaylist(id, {cleanPath});

    int newIdx = model->indexOf(cleanPath);
    if (newIdx >= 0 && model->currentIndex() < 0) {
        playIndex(newIdx);
    }
}

void MediaManager::openFile(const QString &path) {
    if (!m_mpv || !m_playlists) return;

    // URLs go to Default
    if (path.startsWith("http://") || path.startsWith("https://")) {
        ++m_folderScanGeneration;
        clearPendingUrlPlayback();
        if (isDirectStreamUrl(QUrl(path))) playResolvedUrl(path, path);
        else openUrlAsync(path);
        return;
    }

    QString cleanPath = path;
    if (cleanPath.startsWith("file://")) cleanPath = QUrl(cleanPath).toLocalFile();
        QFileInfo fi(cleanPath);
    if (!fi.exists()) {
        emit fileUnavailable(cleanPath);
        return;
    }

    PlaylistModel *def = m_playlists->defaultModel();
    if (!def) return;

    // Append mode: add to Default
    if (m_config && m_config->appendToPlaylist() && def->rowCount() > 0) {
        m_playlists->addFilesToPlaylist(PlaylistManager::DEFAULT_ID, {cleanPath});
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        int idx = def->indexOf(cleanPath);
        if (idx >= 0) playIndex(idx);
        return;
    }

    if (def->currentFile() == cleanPath) return;

    int existingIndex = def->indexOf(cleanPath);
    if (existingIndex >= 0) {
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        playIndex(existingIndex);
        return;
    }

    // Play the opened file immediately. The other videos in its folder are
    // scanned on a worker thread and merged into the playlist afterwards, so
    // a folder with thousands of files cannot delay the first frame.
    const quint64 generation = ++m_folderScanGeneration;
    def->setFiles({cleanPath}, cleanPath);
    if (m_playlists) m_playlists->scanAudioPaths({cleanPath});
    if (m_playMode == "shuffle") def->setShuffle(true);

    if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
        m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);

    playIndex(def->currentIndex());
    scanFolderForPlaylist(cleanPath, generation);
}

void MediaManager::scanFolderForPlaylist(const QString &filePath, quint64 generation)
{
    auto *watcher = new QFutureWatcher<QVector<PlaylistModel::Entry>>(this);
    connect(watcher, &QFutureWatcher<QVector<PlaylistModel::Entry>>::finished, this,
            [this, watcher, filePath, generation]() {
        QVector<PlaylistModel::Entry> entries = watcher->result();
        watcher->deleteLater();
        if (generation != m_folderScanGeneration || !m_playlists)
            return;

        PlaylistModel *def = m_playlists->defaultModel();
        // Do not touch the playlist if the user changed it while the scan was
        // running; merging then could discard those changes.
        if (!def || def->count() != 1 || def->fileAt(0) != filePath)
            return;
        if (entries.size() < 2)
            return;

        def->setPreparedEntries(std::move(entries), filePath);
        if (m_playMode == "shuffle")
            def->setShuffle(true);
        m_playlists->scanAudioPaths({filePath});
    });

    watcher->setFuture(QtConcurrent::run([filePath]() {
        QVector<PlaylistModel::Entry> entries;
        const QFileInfo fi(filePath);
        QDir dir(fi.absolutePath());
        QStringList paths;
        for (const QString &f : dir.entryList({"*.mp4", "*.mkv", "*.avi", "*.webm"},
                                              QDir::Files, QDir::Name)) {
            paths << dir.absoluteFilePath(f);
        }
        if (!paths.contains(filePath)) paths.prepend(filePath);

        entries.reserve(paths.size());
        for (const QString &path : paths) {
            PlaylistModel::Entry entry;
            entry.path = path;
            entry.kind = QStringLiteral("video");
            entry.title = QFileInfo(path).completeBaseName();
            entries.append(entry);
        }
        return entries;
    }));
}

void MediaManager::openFolder(const QString &folder) {
    if (!m_mpv || !m_playlists) return;

    ++m_folderScanGeneration;
    QDir dir(folder);
    QStringList files;
    // Audio formats added
    QStringList filters = {
        "*.mp4", "*.mkv", "*.avi", "*.webm", "*.mov",
        "*.mp3", "*.flac", "*.aac", "*.wav", "*.ogg", "*.m4a", "*.opus"
    };

    for (const QString &f : dir.entryList(filters, QDir::Files, QDir::Name)) {
        files << dir.absoluteFilePath(f);
    }

    if (files.isEmpty()) return;

    PlaylistModel *def = m_playlists->defaultModel();
    if (!def) return;

    if (m_config && m_config->appendToPlaylist() && def->rowCount() > 0) {
        m_playlists->addFolderToPlaylist(PlaylistManager::DEFAULT_ID, folder);
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        int idx = def->indexOf(files.first());
        if (idx >= 0) playIndex(idx);
    } else {
        def->setFiles(files, files.first());
        if (m_playlists) m_playlists->scanAudioPaths(files);
        if (m_playMode == "shuffle") def->setShuffle(true);
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        playIndex(def->currentIndex());
    }
}


void MediaManager::openUrlAsync(const QString &url, bool keepPlaylist) {
    if (!m_mpv || !m_playlists) return;
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();
    setLoading(true);

    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    const QString systemYtdlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    const bool localYtdlp = QFileInfo(m_ytDlpRuntime->ytdlpLocalDir()
                                      + QStringLiteral("/yt_dlp/__init__.py")).exists();
    const bool localEjs = QFileInfo(m_ytDlpRuntime->ytdlpEjsLocalDir()
                                    + QStringLiteral("/yt_dlp_ejs/__init__.py")).exists();
    const bool systemEjs = m_ytDlpRuntime->runtime().value(QStringLiteral("ejsSystem")).toBool();
    QString exe;
    QStringList runtimePrefix;
    // System packages always win over Luma's own copies.
    if (!systemYtdlp.isEmpty()) {
        exe = systemYtdlp;
    } else if (localYtdlp && !python.isEmpty()) {
        exe = python;
        runtimePrefix << QStringLiteral("-m") << QStringLiteral("yt_dlp");
    }
    if (exe.isEmpty()) {
        // yt-dlp is not installed — hand the URL to mpv (it has its own ytdl).
        playStream(url, url, QString(), QString(), keepPlaylist, false);
        return;
    }

    if (m_ytdlpProcess) {
        m_ytdlpProcess->disconnect(this);
        m_ytdlpProcess->kill();
        // Opening another link used to block the GUI for up to one second
        // here. SIGKILL is enough; the process object is deleted afterwards.
        m_ytdlpProcess->waitForFinished(100);
        m_ytdlpProcess->deleteLater();
        m_ytdlpProcess = nullptr;
    }
    if (m_ytdlpTimeout) m_ytdlpTimeout->stop();

    m_resolvedTitle.clear();
    m_resolvedVideoUrl.clear();
    m_resolvedAudioUrl.clear();
    m_resolvedChapters.clear();
    m_resolvedDuration = 0.0;
    m_resolvedIsLive = false;
    m_ytdlpStdoutBuffer.clear();
    m_ytdlpStderr.clear();
    m_ytdlpTimedOut = false;

    const QString quality = m_config ? m_config->ytdlpQuality() : QStringLiteral("best");
    QString formatExpr;
    if (quality == QLatin1String("audio")) {
        formatExpr = QStringLiteral("bestaudio[ext=m4a]/bestaudio");
    } else if (quality.isEmpty() || quality == QLatin1String("best")) {
        formatExpr = QStringLiteral("bestvideo+bestaudio/best");
    } else {
        const int height = quality.toInt();
        formatExpr = QStringLiteral("bestvideo[height<=%1]+bestaudio/best[height<=%1]").arg(height);
    }

    auto *proc = new QProcess(this);
    m_ytdlpProcess = proc;
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    const QString ytdlpLogMode = Logger::instance().componentMode(QStringLiteral("yt-dlp"));
    if (ytdlpLogMode == QLatin1String("info") || ytdlpLogMode == QLatin1String("debug"))
        Logger::instance().writeComponent(QStringLiteral("yt-dlp"),
                                          QStringLiteral("yt-dlp %1").arg(url));

    QStringList args = m_config ? m_config->ytdlpConfigArgs() : QStringList();

    // YouTube's bot check needs the JavaScript challenge solved. yt-dlp uses
    // Deno for that; enable it explicitly when available and the user has not
    // chosen another runtime. No browser cookies are involved.
    bool hasJsRuntime = false;
    for (const QString &arg : std::as_const(args)) {
        if (arg == QLatin1String("--no-js-runtimes")
                || arg == QLatin1String("--js-runtimes")
                || arg.startsWith(QLatin1String("--js-runtimes="))) {
            hasJsRuntime = true;
            break;
        }
    }
    // System Deno wins; otherwise use Luma's private binary. The private
    // directory is added to PATH only for this yt-dlp process.
    QString denoPath = QStandardPaths::findExecutable(QStringLiteral("deno"));
    bool localDeno = false;
    if (denoPath.isEmpty()) {
        const QString candidate = m_ytDlpRuntime->denoLocalDir() + QStringLiteral("/deno");
        if (QFileInfo(candidate).isExecutable()) {
            denoPath = candidate;
            localDeno = true;
        }
    }
    if (!hasJsRuntime && !denoPath.isEmpty())
        args << QStringLiteral("--js-runtimes") << QStringLiteral("deno");

    if (!runtimePrefix.isEmpty())
        args = runtimePrefix + args;

    if (ytdlpLogMode == QLatin1String("debug"))
        args << QStringLiteral("--verbose");
    args << QStringLiteral("--no-playlist")
         << QStringLiteral("-j")
         << QStringLiteral("-f") << formatExpr
         << url;

    proc->setProgram(exe);
    proc->setArguments(args);

    // System packages take precedence. Luma's own copy is only put on
    // PYTHONPATH when it is the selected implementation, and its ejs package
    // only when the system one is absent.
    QStringList pythonPath;
    if (systemYtdlp.isEmpty() && localYtdlp)
        pythonPath << m_ytDlpRuntime->ytdlpLocalDir();
    if (!systemEjs && localEjs)
        pythonPath << m_ytDlpRuntime->ytdlpEjsLocalDir();

    QProcessEnvironment env;
    bool envChanged = false;
    if (!pythonPath.isEmpty()) {
        env = QProcessEnvironment::systemEnvironment();
        const QString oldPath = env.value(QStringLiteral("PYTHONPATH"));
        if (!oldPath.isEmpty()) pythonPath << oldPath;
        env.insert(QStringLiteral("PYTHONPATH"), pythonPath.join(QLatin1Char(':')));
        envChanged = true;
    }
    if (localDeno) {
        if (!envChanged)
            env = QProcessEnvironment::systemEnvironment();
        const QString oldPath = env.value(QStringLiteral("PATH"));
        env.insert(QStringLiteral("PATH"), m_ytDlpRuntime->denoLocalDir()
                   + (oldPath.isEmpty() ? QString() : QLatin1Char(':') + oldPath));
        envChanged = true;
    }
    if (envChanged)
        proc->setProcessEnvironment(env);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc]() {
        if (m_ytdlpProcess != proc) return;
        m_ytdlpStdoutBuffer += proc->readAllStandardOutput();
        if (m_ytdlpStdoutBuffer.size() > kMaxYtdlpStdout) {
            m_ytdlpStdoutBuffer.truncate(kMaxYtdlpStdout);
            if (proc->state() == QProcess::Running) {
                m_ytdlpStderr += QStringLiteral("yt-dlp output is too large");
                proc->kill();
            }
        }
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc, ytdlpLogMode]() {
        const QByteArray chunk = proc->readAllStandardError();
        m_ytdlpStderr += QString::fromUtf8(chunk);
        if (m_ytdlpStderr.size() > 65536)
            m_ytdlpStderr = m_ytdlpStderr.right(32768);
        if (ytdlpLogMode == QLatin1String("info") || ytdlpLogMode == QLatin1String("debug"))
            Logger::instance().writeComponent(QStringLiteral("yt-dlp"),
                                              QString::fromUtf8(chunk));
    });
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, ytdlpLogMode](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        if (m_ytdlpProcess != proc) return;
        if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
        if (m_ytdlpProcess == proc) m_ytdlpProcess = nullptr;
        proc->deleteLater();
        if (ytdlpLogMode != QLatin1String("off"))
            Logger::instance().writeComponent(
                QStringLiteral("yt-dlp"),
                QStringLiteral("failed to start yt-dlp: %1").arg(proc->errorString()));
        setLoading(false);
        emit downloadStatus(tr("download_failed_start"));
        m_notifier->notify(tr("download_failed_open"), tr("download_ytdlp_missing"));
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, url, keepPlaylist, ytdlpLogMode](int code, QProcess::ExitStatus status) {
        // FailedToStart is handled above and clears m_ytdlpProcess.
        if (m_ytdlpProcess != proc) return;
        m_ytdlpStdoutBuffer += proc->readAllStandardOutput();
        if (m_ytdlpStdoutBuffer.size() > kMaxYtdlpStdout)
            m_ytdlpStdoutBuffer.truncate(kMaxYtdlpStdout);
        m_ytdlpStderr += QString::fromUtf8(proc->readAllStandardError());
        if (m_ytdlpTimeout) m_ytdlpTimeout->stop();
        if (m_ytdlpProcess == proc) m_ytdlpProcess = nullptr;
        proc->deleteLater();

        const bool ok = status == QProcess::NormalExit && code == 0;
        if (ytdlpLogMode == QLatin1String("error") && !ok) {
            QString detail = m_ytdlpStderr.trimmed();
            if (detail.isEmpty())
                detail = QStringLiteral("yt-dlp exited with code %1").arg(code);
            Logger::instance().writeComponent(QStringLiteral("yt-dlp"), detail);
        }
        finishUrlResolution(url, keepPlaylist, ok);
    });

    m_ytdlpTimeout->start();
    // Show the link itself (not the video title): while it is being resolved
    // there is no title yet, and the link makes it obvious which URL is opening.
    emit downloadStatus(tr("stream_opening").arg(url));
    proc->start();
}

void MediaManager::finishUrlResolution(const QString &url, bool keepPlaylist, bool ok) {
    if (ok) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(m_ytdlpStdoutBuffer, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            ok = false;
        } else {
            const QJsonObject info = document.object();
            m_resolvedTitle = info.value(QStringLiteral("title")).toString();
            m_resolvedIsLive = info.value(QStringLiteral("is_live")).toBool(false);
            if (!m_resolvedIsLive)
                m_resolvedIsLive = info.value(QStringLiteral("live_status")).toString()
                                        == QLatin1String("is_live");

            QString muxedUrl;
            const QJsonArray requested = info.value(QStringLiteral("requested_formats")).toArray();
            for (const QJsonValue &value : requested) {
                const QJsonObject format = value.toObject();
                const QString formatUrl = format.value(QStringLiteral("url")).toString();
                if (formatUrl.isEmpty()) continue;
                // Live HLS formats often report acodec as null/"none" even
                // for audio-only formats. Use yt-dlp's stream type fields as
                // well, otherwise YouTube live is played without its audio URL.
                const QString vcodec = format.value(QStringLiteral("vcodec")).toString();
                const QString acodec = format.value(QStringLiteral("acodec")).toString();
                const QString videoExt = format.value(QStringLiteral("video_ext")).toString();
                const QString audioExt = format.value(QStringLiteral("audio_ext")).toString();
                const QString resolution = format.value(QStringLiteral("resolution")).toString();
                const bool hasVideo = (!vcodec.isEmpty() && vcodec != QLatin1String("none"))
                    || (!videoExt.isEmpty() && videoExt != QLatin1String("none"))
                    || resolution.contains(QLatin1Char('x'));
                const bool hasAudio = (!acodec.isEmpty() && acodec != QLatin1String("none"))
                    || (!audioExt.isEmpty() && audioExt != QLatin1String("none"))
                    || resolution == QLatin1String("audio only");
                if (hasVideo && hasAudio) {
                    if (muxedUrl.isEmpty()) muxedUrl = formatUrl;
                } else if (hasVideo) {
                    if (m_resolvedVideoUrl.isEmpty()) m_resolvedVideoUrl = formatUrl;
                } else if (hasAudio) {
                    if (m_resolvedAudioUrl.isEmpty()) m_resolvedAudioUrl = formatUrl;
                }
            }
            if (m_resolvedVideoUrl.isEmpty())
                m_resolvedVideoUrl = info.value(QStringLiteral("url")).toString();
            if (m_resolvedVideoUrl.isEmpty())
                m_resolvedVideoUrl = muxedUrl;
            if (m_resolvedVideoUrl.isEmpty()) ok = false;

            m_resolvedChapters.clear();
            m_resolvedDuration = info.value(QStringLiteral("duration")).toDouble(0.0);
            const QJsonArray chapters = info.value(QStringLiteral("chapters")).toArray();
            for (const QJsonValue &value : chapters) {
                const QJsonObject chapter = value.toObject();
                const double start = chapter.value(QStringLiteral("start_time")).toDouble(-1.0);
                if (start < 0.0) continue;
                ChapterInfo entry;
                entry.start = start;
                entry.end = chapter.value(QStringLiteral("end_time")).toDouble(0.0);
                entry.title = chapter.value(QStringLiteral("title")).toString().trimmed();
                m_resolvedChapters.append(entry);
            }
            std::sort(m_resolvedChapters.begin(), m_resolvedChapters.end(),
                      [](const ChapterInfo &a, const ChapterInfo &b) {
                          return a.start < b.start;
                      });
            // Drop duplicate timestamps (mpv needs strictly increasing starts)
            // and fill in missing end times from the next chapter / duration.
            QVector<ChapterInfo> uniqueChapters;
            for (ChapterInfo chapter : std::as_const(m_resolvedChapters)) {
                if (!uniqueChapters.isEmpty()
                        && chapter.start - uniqueChapters.last().start < 0.01)
                    continue;
                uniqueChapters.append(chapter);
            }
            for (int i = 0; i < uniqueChapters.size(); ++i) {
                ChapterInfo &chapter = uniqueChapters[i];
                if (chapter.end > chapter.start)
                    continue;
                if (i + 1 < uniqueChapters.size())
                    chapter.end = uniqueChapters[i + 1].start;
                else if (m_resolvedDuration > chapter.start)
                    chapter.end = m_resolvedDuration;
                else
                    chapter.end = chapter.start + 1.0;
            }
            m_resolvedChapters = uniqueChapters;
        }
    }

    if (!ok) {
        QString detail;
        if (m_ytdlpTimedOut) {
            detail = tr("download_timeout");
        } else {
            detail = m_ytdlpStderr.trimmed();
            if (detail.isEmpty()) {
                detail = tr("download_no_result");
            } else {
                detail = detail.section(QLatin1Char('\n'), -1).trimmed();
                if (detail.startsWith(QLatin1String("ERROR:")))
                    detail = detail.mid(6).trimmed();
                if (detail.size() > 300) detail = detail.left(300) + QStringLiteral("…");
            }
        }
        emit downloadStatus(tr("download_failed_open"));
        m_notifier->notify(tr("download_failed_open"), detail);
        m_resolvedTitle.clear();
        m_resolvedVideoUrl.clear();
        m_resolvedAudioUrl.clear();
        m_resolvedChapters.clear();
        m_resolvedIsLive = false;
        // The link failed to resolve: do not leave the loading entry marked as
        // playing. A playlist that was already playing keeps its marker.
        if (m_pendingUrlModel && !m_pendingUrlWasPlaying)
            m_pendingUrlModel->setPlaying(false);
        clearPendingUrlPlayback();
        setLoading(false);
        return;
    }

    playStream(url, m_resolvedVideoUrl, m_resolvedAudioUrl, m_resolvedTitle, keepPlaylist,
               m_resolvedIsLive);
}

void MediaManager::playStream(const QString &originalUrl, const QString &videoUrl,
                              const QString &audioUrl, const QString &title, bool keepPlaylist,
                              bool isLive) {
    if (!m_mpv || !m_playlists) return;
    m_crossfadeFadeInCurrent = false;
    m_pendingCrossfadeIn = false;
    m_crossfadeOutScheduled = false;
    setCrossfadeFilter(QString());
    resetSilenceState();
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();

    if (!keepPlaylist) {
        PlaylistModel *def = m_playlists->defaultModel();
        if (!def) return;
        const int existingIndex = def->indexOf(originalUrl);
        if (existingIndex < 0) {
            def->setFiles({originalUrl}, originalUrl);
            if (m_playMode == "shuffle") def->setShuffle(true);
        }
        if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
            m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
        def->setCurrentIndex(existingIndex >= 0 ? existingIndex : 0);
    }

    // Keep the playing marker on the playlist the URL was started from, not
    // on the tab that happens to be active after yt-dlp finishes (the user
    // may switch playlists while the link is being resolved).
    PlaylistModel *titleModel = keepPlaylist ? m_pendingUrlModel.data()
                                             : m_playlists->defaultModel();
    if (keepPlaylist && !titleModel)
        titleModel = m_playlists->activeModel();
    if (titleModel) {
        m_playlists->setPlayingModel(titleModel);
        // Re-find the row: the playlist could have been reordered while the
        // link was loading.
        const int index = titleModel->indexOf(originalUrl);
        if (index >= 0)
            titleModel->setCurrentIndex(index);
        if (!title.isEmpty())
            titleModel->setVideoTitle(originalUrl, title);
    }
    clearPendingUrlPlayback();

    m_lastPlayedFile = originalUrl;
    m_lastPlayedTimer.start();
    m_lastStreamTitle.clear();
    m_streamFromYtdlp = true;
    // Save the previous position before m_liveStream describes the new file.
    preparePlaybackPosition(originalUrl, isLive);
    setLiveStream(isLive);
    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    if (!isLive && !m_resolvedChapters.isEmpty()) {
        const QString chaptersFile = writeChaptersFile(m_resolvedChapters);
        if (!chaptersFile.isEmpty()) {
            m_mpv->setChaptersFile(chaptersFile);
            m_pendingChaptersFile = chaptersFile;
        }
    }
    m_resolvedChapters.clear();

    m_mpv->setExternalAudio(audioUrl);
    setLoading(true);
    setHasMedia(true);
    m_mpv->setSource(videoUrl, isLive);
    m_mpv->setPaused(false);

    if (m_history.isEmpty() || m_history.last() != originalUrl) {
        m_history.append(originalUrl);
        if (m_history.size() > 100) m_history.removeFirst();
    }

    // Show the video title in the notification (if yt-dlp provided one).
    const QString notifyTitle = title.isEmpty() ? originalUrl : title;
    QTimer::singleShot(100, this, [this, notifyTitle]() { m_notifier->notify(QString(), notifyTitle); });
    emit fileChanged(originalUrl);
}

void MediaManager::onMpvFileLoaded() {
    if (!m_mpv) return;
    m_silenceLoadTimer.start();
    setLoading(false);
    if (!m_pendingChaptersFile.isEmpty()) {
        QFile::remove(m_pendingChaptersFile);
        m_pendingChaptersFile.clear();
    }

    // Continue where the previous session stopped. Seeking right after
    // fileLoaded is safe: mpv is already playing and the seek is async.
    if (!m_liveStream && m_resumeTarget > 0.0
            && (!m_config || m_config->resumePlayback())) {
        const double target = m_resumeTarget;
        m_resumeTarget = 0.0;
        m_mpv->seekExact(target);
    } else {
        m_resumeTarget = 0.0;
    }

    updateCrossfade();

    // Start the ICY reader only after mpv has opened the stream. Opening a
    // second connection while the radio is still connecting could make some
    // Shoutcast servers stall the player.
    if (!m_icyReader || m_streamFromYtdlp) return;
    const QUrl url(m_mpv->source());
    if (isDirectStreamUrl(url) && isLiveStreamUrl(url))
        m_icyReader->start(url);
}

QString MediaManager::writeChaptersFile(const QVector<ChapterInfo> &chapters)
{
    if (chapters.isEmpty())
        return {};
    if (!m_chaptersDir)
        m_chaptersDir = new QTemporaryDir;
    if (!m_chaptersDir->isValid())
        return {};

    const QString path = m_chaptersDir->path()
                         + QStringLiteral("/chapters-%1.txt").arg(++m_chaptersCounter);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};

    // mpv's chapters-file does not accept OGM directly; ffmetadata is parsed
    // by FFmpeg's ffmetadata demuxer.
    QTextStream out(&file);
    out << QStringLiteral(";FFMETADATA1\n");
    for (const ChapterInfo &chapter : chapters) {
        const int startMs = qMax(0, static_cast<int>(chapter.start * 1000.0));
        const int endMs = qMax(startMs + 1, static_cast<int>(chapter.end * 1000.0));
        QString title = chapter.title;
        title.replace(QLatin1Char('\n'), QLatin1Char(' '));
        title.replace(QLatin1Char('\r'), QLatin1Char(' '));
        title = title.trimmed();
        out << QStringLiteral("[CHAPTER]\n")
            << QStringLiteral("TIMEBASE=1/1000\n")
            << QStringLiteral("START=%1\n").arg(startMs)
            << QStringLiteral("END=%1\n").arg(endMs);
        if (!title.isEmpty())
            out << QStringLiteral("title=%1\n").arg(title);
    }
    file.close();
    return path;
}

QString MediaManager::positionsFilePath() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache");
    return base + QStringLiteral("/luma/playback-positions.json");
}

void MediaManager::preparePlaybackPosition(const QString &key, bool live)
{
    // Store where the previous file stopped before switching the key.
    saveCurrentPosition();
    clearPlaybackPosition();

    if (live || key.isEmpty())
        return;
    if (m_config && !m_config->resumePlayback())
        return;

    m_positionKey = key.contains(QStringLiteral("://"))
                        ? key
                        : QFileInfo(key).absoluteFilePath();
    m_resumeTarget = m_positions.positionFor(m_positionKey);
    if (m_positionTimer)
        m_positionTimer->start();
}

void MediaManager::saveCurrentPosition()
{
    if (!m_mpv || m_positionKey.isEmpty() || m_liveStream)
        return;
    if (m_mpv->isCasting())
        return;
    if (m_config && !m_config->resumePlayback())
        return;

    const double duration = m_mpv->duration();
    const double position = m_mpv->position();
    if (duration <= 0.0 || position <= 0.0)
        return;
    if (!m_positions.setPosition(m_positionKey, position, duration))
        return;
    if (m_positionWriter)
        m_positionWriter->write(positionsFilePath(), m_positions.serialize());
}

void MediaManager::clearPlaybackPosition()
{
    if (m_positionTimer)
        m_positionTimer->stop();
    m_positionKey.clear();
    m_resumeTarget = 0.0;
}

void MediaManager::clearPendingUrlPlayback()
{
    m_pendingUrlModel.clear();
    m_pendingUrlWasPlaying = false;
}

void MediaManager::playResolvedUrl(const QString &originalUrl, const QString &playUrl) {
    if (!m_mpv || !m_playlists) return;
    clearPendingUrlPlayback();
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();

    PlaylistModel *def = m_playlists->defaultModel();
    if (!def) return;

    // The playlist keeps the original URL while mpv receives the direct stream.
    // If the URL is already in the list (playlist navigation), don't replace it.
    const int existingIndex = def->indexOf(originalUrl);
    if (existingIndex < 0) {
        def->setFiles({originalUrl}, originalUrl);
        if (m_playMode == "shuffle") def->setShuffle(true);
    }
    if (m_playlists->activeId() != PlaylistManager::DEFAULT_ID)
        m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);

    m_lastPlayedFile = originalUrl;
    m_lastPlayedTimer.start();
    m_lastStreamTitle.clear();
    m_streamFromYtdlp = false;
    const bool live = isLiveStreamUrl(QUrl(originalUrl)) && originalUrl == playUrl;
    preparePlaybackPosition(originalUrl, live);
    setLiveStream(live);
    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_playlists->setPlayingModel(def);
    def->setCurrentIndex(existingIndex >= 0 ? existingIndex : 0);
    setLoading(true);
    setHasMedia(true);
    m_mpv->setSource(playUrl, live);
    m_mpv->setPaused(false);

    if (m_history.isEmpty() || m_history.last() != originalUrl) {
        m_history.append(originalUrl);
        if (m_history.size() > 100) m_history.removeFirst();
    }

    const QUrl original(originalUrl);
    const QString title = urlTitle(originalUrl);
    // For direct streams the notification arrives with the ICY tag once mpv gets it.
    if (!isDirectStreamUrl(original))
        QTimer::singleShot(100, this, [this, title]() { m_notifier->notify(QString(), title); });
    emit fileChanged(originalUrl);
}

void MediaManager::playIndex(int index) {
    if (!m_playlists) return;
    playIndexInModel(m_playlists->activeModel(), index);
}

void MediaManager::playIndexInModel(PlaylistModel *model, int index) {
    if (!m_mpv || !m_playlists) return;
    if (m_icyReader) m_icyReader->stop();
    if (m_icyTitleTimer) m_icyTitleTimer->stop();
    m_pendingIcyTitle.clear();
    if (!model) return;

    QString file = model->fileAt(index);
    if (file.isEmpty()) return;

    // The file may have been deleted or moved while it sat in the playlist.
    // Do not hand it to mpv (which would fail silently): tell the UI and mark
    // the entry, and drop the mark if the file is back.
    if (!file.startsWith(QStringLiteral("http://"))
            && !file.startsWith(QStringLiteral("https://"))) {
        if (!QFileInfo::exists(file)) {
            model->setUnavailable(file, true);
            emit fileUnavailable(file);
            return;
        }
        model->setUnavailable(file, false);
    }

    if (file == m_lastPlayedFile && m_lastPlayedTimer.isValid() && m_lastPlayedTimer.elapsed() < 100) {
        return;
    }
    m_lastPlayedFile = file;
    m_lastPlayedTimer.start();
    resetSilenceState();

    // Resolve playlist URLs through yt-dlp so the proxy works.
    if ((file.startsWith(QStringLiteral("http://")) || file.startsWith(QStringLiteral("https://")))
            && !isDirectStreamUrl(QUrl(file))) {
        model->setCurrentIndex(index);
        // Mark the entry as playing right away: resolving the link can take a
        // while and the playlist should show what is about to play. Other
        // playlists keep their marker until the link actually resolves.
        m_pendingUrlWasPlaying = model->isPlaying();
        m_pendingUrlModel = model;
        model->setPlaying(true);
        openUrlAsync(file, true);
        return;
    }
    clearPendingUrlPlayback();

    if (!file.contains(QStringLiteral("://"))) {
        m_crossfadeFadeInCurrent = m_pendingCrossfadeIn;
        m_pendingCrossfadeIn = false;
    } else {
        m_crossfadeFadeInCurrent = false;
        m_pendingCrossfadeIn = false;
    }
    m_crossfadeOutScheduled = false;
    setCrossfadeFilter(QString());

    m_lastStreamTitle.clear();
    m_streamFromYtdlp = false;
    const bool live = isLiveStreamUrl(QUrl(file));
    preparePlaybackPosition(file, live);
    setLiveStream(live);

    setManualSwitch(true);
    QTimer::singleShot(1000, this, [this]() { setManualSwitch(false); });

    m_playlists->setPlayingModel(model);
    model->setCurrentIndex(index);
    setHasMedia(true);
    m_mpv->setSource(file, live);
    m_mpv->setPaused(false);

    if (m_history.isEmpty() || m_history.last() != file) {
        m_history.append(file);
        if (m_history.size() > 100) m_history.removeFirst();
    }

    QTimer::singleShot(100, this, [this, file]() { m_notifier->notifyForFile(file); });
    emit fileChanged(file);
}

void MediaManager::playNext() {
    if (!m_mpv || !m_playlists) return;
    // Navigation must follow the playlist that is actually playing, not the
    // tab the user happens to be viewing.
    PlaylistModel *model = m_playlists->playingModel();
    if (!model) model = m_playlists->activeModel();
    if (!model) return;

    int current = model->currentIndex();
    int count = model->rowCount();
    if (count == 0) return;

    const QString mode = m_playlists->playlistModeFor(model);
    if (mode == "repeat") {
        if (current >= 0) playIndexInModel(model, current);
    }
    else if (mode == "shuffle") {
        int next = current + 1;
        if (next >= count) {
            model->setShuffle(true);
            next = 0;
        }
        playIndexInModel(model, next);
    }
    else if (mode == "stop") {
        stopPlayback();
    }
    else {
        int next = current + 1;
        if (next < count) {
            playIndexInModel(model, next);
        } else {
            stopPlayback();
        }
    }
}

void MediaManager::stopPlayback() {
    if (!m_mpv) return;
    saveCurrentPosition();
    clearPlaybackPosition();
    clearPendingUrlPlayback();
    // Unload the file so the video area goes black and the control bar returns
    // to 00:00 instead of leaving the last frame of the finished playlist on
    // screen.
    m_mpv->stopPlayback();
    m_crossfadeFadeInCurrent = false;
    m_pendingCrossfadeIn = false;
    m_crossfadeOutScheduled = false;
    setCrossfadeFilter(QString());
    resetSilenceState();
    if (m_playlists)
        m_playlists->setPlayingModel(nullptr);
    setHasMedia(false);
    setLiveStream(false);
    setLoading(false);
}

void MediaManager::nextChapter()
{
    if (!m_mpv || m_mpv->isCasting()) return;
    m_mpv->command(QStringList() << QStringLiteral("add")
                                 << QStringLiteral("chapter") << QStringLiteral("1"));
}

void MediaManager::previousChapter()
{
    if (!m_mpv || m_mpv->isCasting()) return;
    m_mpv->command(QStringList() << QStringLiteral("add")
                                 << QStringLiteral("chapter") << QStringLiteral("-1"));
}

void MediaManager::playPrev() {
    if (!m_mpv || !m_playlists) return;
    PlaylistModel *model = m_playlists->playingModel();
    if (!model) model = m_playlists->activeModel();
    if (!model) return;

    int current = model->currentIndex();
    int count = model->rowCount();
    if (count == 0) return;

    if (current <= 0) {
        if (current == 0) m_mpv->seekExact(0.0);
        return;
    }

    int prev = current - 1;
    if (prev >= 0) playIndexInModel(model, prev);
}

void MediaManager::openFileDialog() {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();

    // Use the shared audio/video filter
    QString path = QFileDialog::getOpenFileName(
        nullptr,
        tr("dialog_open_video"),
                                                defaultDir,
                                                tr("dialog_media_filter") // <-- New localization key
    );
    if (!path.isEmpty()) openFile(path);
}

void MediaManager::openFolderDialog() {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString folder = QFileDialog::getExistingDirectory(nullptr, tr("dialog_open_folder"), defaultDir);
    if (!folder.isEmpty()) openFolder(folder);
}

void MediaManager::openSubtitlesDialog() {
    if (!m_mpv) return;
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString path = QFileDialog::getOpenFileName(nullptr, tr("dialog_open_subtitles"), defaultDir, tr("dialog_subtitles_filter"));
    if (!path.isEmpty()) {
        m_mpv->command(QStringList() << "sub-add" << path);
        m_mpv->setProperty("sub", "auto");
    }
}

void MediaManager::openAudioDialog() {
    if (!m_mpv) return;
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    QString path = QFileDialog::getOpenFileName(nullptr, tr("dialog_open_audio"), defaultDir, tr("dialog_audio_filter"));
    if (!path.isEmpty()) m_mpv->command(QStringList() << "audio-add" << path);
}

QVariantList MediaManager::getTracks(const QString &type) const {
    if (!m_mpv) return {};
    QVariantList res;
    // track-list is observed by MpvItem; no blocking call to the mpv thread.
    for (const QVariant &t : m_mpv->observedProperty("track-list").toList()) {
        QVariantMap map = t.toMap();
        if (map["type"].toString() != type) continue;
        QVariantMap item;
        int trackId = map["id"].toInt();
        item["id"] = trackId;
        QString title = map["title"].toString();
        if (title.isEmpty()) title = tr("track_default_name").arg(trackId);
        item["title"] = title;
        item["lang"] = map["lang"].toString();
        res << item;
    }
    return res;
}

void MediaManager::setTrack(const QString &type, int id) { if (m_mpv) m_mpv->setProperty(type, id); }
void MediaManager::setTrackOff(const QString &type) { if (m_mpv) m_mpv->setProperty(type, "no"); }

int MediaManager::currentTrackId(const QString &type) const {
    if (!m_mpv) return 0;
    // aid/sid/vid are observed by MpvItem (see its constructor).
    QVariant val = m_mpv->observedProperty(type);
    if (val.typeId() == QMetaType::QString && val.toString() == "no") return 0;
    return val.toInt();
}
