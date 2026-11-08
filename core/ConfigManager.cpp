#include "ConfigManager.hpp"
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QTextStream>
#include <QStandardPaths>
#include <QTimer>

// Allowed values for playback settings. For hwdec these are the variants
// that make sense on Linux (see `mpv --hwdec=help`); "auto" is the usual
// automatic choice, and "-copy" returns frames to system memory.
static const QStringList kAudioOutputs = {
    QStringLiteral("auto"), QStringLiteral("pipewire"),
    QStringLiteral("pulse"), QStringLiteral("alsa")
};

static const QStringList kHwdecModes = {
    QStringLiteral("auto"), QStringLiteral("auto-copy"),
    QStringLiteral("vaapi"), QStringLiteral("vaapi-copy"),
    QStringLiteral("nvdec"), QStringLiteral("nvdec-copy"),
    QStringLiteral("cuda"), QStringLiteral("cuda-copy"),
    QStringLiteral("vulkan"), QStringLiteral("vulkan-copy"),
    QStringLiteral("drm"), QStringLiteral("drm-copy"),
    QStringLiteral("vdpau"), QStringLiteral("vdpau-copy"),
    QStringLiteral("no")
};

// Proxy types that yt-dlp understands.
static const QStringList kProxyTypes = {
    QStringLiteral("none"), QStringLiteral("socks5"),
    QStringLiteral("socks5h"), QStringLiteral("http"), QStringLiteral("https")
};

static QString proxyScheme(const QString &url) {
    const int sep = url.indexOf(QStringLiteral("://"));
    return sep > 0 ? url.left(sep).toLower() : QString();
}

static QString proxyAddress(const QString &url) {
    const int sep = url.indexOf(QStringLiteral("://"));
    return sep > 0 ? url.mid(sep + 3) : url;
}

static QString normalizedProxyUrl(const QString &proxy) {
    QString value = proxy.trimmed();
    // Some clients hand out "socks://", but yt-dlp only understands
    // socks4/socks4a/socks5/socks5h. With an unknown scheme it silently hangs
    // on connect, so normalize it to SOCKS5.
    if (value.startsWith(QLatin1String("socks://"), Qt::CaseInsensitive))
        value = QStringLiteral("socks5://") + value.mid(8);
    return value;
}

static QString normalizedLogMode(QString mode) {
    mode = mode.trimmed().toLower();
    if (mode == QLatin1String("off"))
        return QStringLiteral("off");
    if (mode == QLatin1String("error"))
        return QStringLiteral("error");
    if (mode == QLatin1String("debug"))
        return QStringLiteral("debug");
    return QStringLiteral("info");
}

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/luma";
    QString path = configDir + "/luma.conf";
    m_configDir = configDir;

    QDir().mkpath(configDir);

    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);

            out << "[ui]\n";
            out << "remember_volume=true\n";
            out << "default_volume=80\n";
            out << "volume=80\n";
            out << "language=auto\n";
            out << "single_instance=true\n";
            out << "notifications=true\n";
            out << "append_to_playlist=false\n\n";

            out << "[playlist]\n";
            out << "mode=sequential\n\n";

            out << "[debug]\n";
            out << "luma=info\n";
            out << "mpv=off\n";
            out << "ytdlp=off\n\n";

            out << "[window]\n";
            out << "width=960\n";
            out << "height=640\n";

            f.close();
        }
    }

    QString mpvPath = configDir + "/mpv.conf";
    if (!QFile::exists(mpvPath)) {
        QFile f(mpvPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);

            out << "hwdec=auto\n";
            out << "deinterlace=auto\n";
            out << "ytdl=auto\n";
            out << "cache=yes\n";
            out << "cache-secs=300\n";

            f.close();
        }
    }

    m_settings = new QSettings(path, QSettings::IniFormat, this);

    loadPlaybackSettingsFromMpvConf();
    loadYtdlpConfig();

    setupFileWatcher();
}

void ConfigManager::setupFileWatcher() {
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    // QSettings may write the file in several steps; coalesce the events.
    m_reloadTimer->setInterval(150);
    connect(m_reloadTimer, &QTimer::timeout, this, &ConfigManager::reloadFromDisk);

    m_watcher = new QFileSystemWatcher(this);
    const QStringList files = {
        m_settings->fileName(),
        m_configDir + QStringLiteral("/mpv.conf"),
        ytdlpConfigPath()
    };
    for (const QString &file : files) {
        if (QFile::exists(file) && !m_watcher->files().contains(file))
            m_watcher->addPath(file);
    }
    // luma.conf may not exist yet on the first run; watch the directory so a
    // file created by another instance is picked up as well.
    if (QFile::exists(m_configDir) && !m_watcher->directories().contains(m_configDir))
        m_watcher->addPath(m_configDir);

    auto scheduleReload = [this]() {
        // The watcher is restarted inside reloadFromDisk: a file replaced with
        // rename() (as some editors/tools do) is no longer watched afterwards.
        m_reloadTimer->start();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, scheduleReload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, scheduleReload);
}

void ConfigManager::reloadFromDisk() {
    if (m_reloading) return;
    m_reloading = true;

    // Read the current values first: after sync() the getters return the new ones.
    const bool oldRememberVolume = rememberVolume();
    const int oldDefaultVolume = defaultVolume();
    const int oldVolume = volume();
    const QString oldLanguage = language();
    const QString oldPlaylistMode = playlistMode();
    const bool oldAppendToPlaylist = appendToPlaylist();
    const bool oldSingleInstance = singleInstance();
    const bool oldNotifications = notifications();
    const QString oldControlBarMode = controlBarMode();
    const bool oldShowPanelsFullscreen = showPanelsFullscreen();
    const QString oldHwdec = m_hwdec;
    const QString oldAudioOutput = m_audioOutput;
    const QString oldYtdlpQuality = m_ytdlpQuality;
    const QString oldYtdlpProxy = m_ytdlpProxy;
    const QString oldYtdlpProxyType = m_ytdlpProxyType;
    const QString oldYtdlpProxyAddress = m_ytdlpProxyAddress;
    const QString oldLumaLogMode = lumaLogMode();
    const QString oldMpvLogMode = mpvLogMode();
    const QString oldYtdlpLogMode = ytdlpLogMode();
    const QString oldSubtitleFont = subtitleFont();
    const int oldSubtitleFontSize = subtitleFontSize();
    const bool oldSubtitleBold = subtitleBold();
    const QString oldSubtitleColor = subtitleColor();
    const int oldSubtitleBorderSize = subtitleBorderSize();
    const QString oldSubtitleBorderColor = subtitleBorderColor();
    const bool oldSubtitleBackground = subtitleBackground();
    const int oldSubtitlePosition = subtitlePosition();
    const bool oldSubtitleIgnoreFileStyle = subtitleIgnoreFileStyle();

    // Reloads the values changed by another process.
    m_settings->sync();
    loadPlaybackSettingsFromMpvConf();
    loadYtdlpConfig();

    // Re-add watchers for files that were replaced or recreated.
    const QStringList files = {
        m_settings->fileName(),
        m_configDir + QStringLiteral("/mpv.conf"),
        ytdlpConfigPath()
    };
    for (const QString &file : files) {
        if (QFile::exists(file) && !m_watcher->files().contains(file))
            m_watcher->addPath(file);
    }

    if (oldRememberVolume != rememberVolume()) emit rememberVolumeChanged();
    if (oldDefaultVolume != defaultVolume()) emit defaultVolumeChanged();
    if (oldVolume != volume()) emit volumeChanged();
    if (oldLanguage != language()) emit languageChanged();
    if (oldPlaylistMode != playlistMode()) emit playlistModeChanged();
    if (oldAppendToPlaylist != appendToPlaylist()) emit appendToPlaylistChanged();
    if (oldSingleInstance != singleInstance()) emit singleInstanceChanged();
    if (oldNotifications != notifications()) emit notificationsChanged();
    if (oldControlBarMode != controlBarMode()) emit controlBarModeChanged();
    if (oldShowPanelsFullscreen != showPanelsFullscreen()) emit showPanelsFullscreenChanged();
    if (oldHwdec != m_hwdec) emit hwdecChanged();
    if (oldAudioOutput != m_audioOutput) emit audioOutputChanged();
    if (oldYtdlpQuality != m_ytdlpQuality || oldYtdlpProxy != m_ytdlpProxy
            || oldYtdlpProxyType != m_ytdlpProxyType
            || oldYtdlpProxyAddress != m_ytdlpProxyAddress)
        emit ytdlpSettingsChanged();
    if (oldSubtitleFont != subtitleFont() || oldSubtitleFontSize != subtitleFontSize()
            || oldSubtitleBold != subtitleBold() || oldSubtitleColor != subtitleColor()
            || oldSubtitleBorderSize != subtitleBorderSize()
            || oldSubtitleBorderColor != subtitleBorderColor()
            || oldSubtitleBackground != subtitleBackground()
            || oldSubtitlePosition != subtitlePosition()
            || oldSubtitleIgnoreFileStyle != subtitleIgnoreFileStyle())
        emit subtitlesChanged();
    if (oldLumaLogMode != lumaLogMode() || oldMpvLogMode != mpvLogMode()
            || oldYtdlpLogMode != ytdlpLogMode())
        emit loggingChanged();

    m_reloading = false;
    emit settingsReloaded();
}

bool ConfigManager::rememberVolume() const {
    return m_settings->value("ui/remember_volume", true).toBool();
}
void ConfigManager::setRememberVolume(bool v) {
    if (rememberVolume() == v) return;
    m_settings->setValue("ui/remember_volume", v);
    emit rememberVolumeChanged();
}

int ConfigManager::defaultVolume() const {
    return m_settings->value("ui/default_volume", 80).toInt();
}
void ConfigManager::setDefaultVolume(int v) {
    if (defaultVolume() == v) return;
    m_settings->setValue("ui/default_volume", v);
    emit defaultVolumeChanged();
}

int ConfigManager::volume() const {
    return m_settings->value("ui/volume", 80).toInt();
}
void ConfigManager::setVolume(int v) {
    if (volume() == v) return;
    m_settings->setValue("ui/volume", v);
    emit volumeChanged();
}

QString ConfigManager::language() const {
    const QString lang = m_settings->value("ui/language", "auto").toString();
    // "auto" follows the system locale; only languages with a compiled
    // translation are supported.
    if (lang == "auto") return lang;
    return lang == "ru" ? "ru" : "en";
}
void ConfigManager::setLanguage(const QString &lang) {
    if (language() == lang) return;
    m_settings->setValue("ui/language", lang);
    emit languageChanged();
}

QString ConfigManager::resolveLanguage(const QString &requested, const QString &systemLocale) {
    if (requested == "en" || requested == "ru") return requested;
    if (requested != "auto") return "en";
    return systemLocale.left(2) == "ru" ? "ru" : "en";
}

QString ConfigManager::playlistMode() const {
    return m_settings->value("playlist/mode", "sequential").toString();
}
void ConfigManager::setPlaylistMode(const QString &mode) {
    if (playlistMode() == mode) return;
    m_settings->setValue("playlist/mode", mode);
    emit playlistModeChanged();
}

bool ConfigManager::appendToPlaylist() const {
    return m_settings->value("ui/append_to_playlist", false).toBool();
}
void ConfigManager::setAppendToPlaylist(bool v) {
    if (appendToPlaylist() == v) return;
    m_settings->setValue("ui/append_to_playlist", v);
    emit appendToPlaylistChanged();
}

bool ConfigManager::singleInstance() const {
    return m_settings->value("ui/single_instance", true).toBool();
}
void ConfigManager::setSingleInstance(bool enabled) {
    if (singleInstance() == enabled) return;
    m_settings->setValue("ui/single_instance", enabled);
    emit singleInstanceChanged();
}

bool ConfigManager::notifications() const {
    return m_settings->value("ui/notifications", true).toBool();
}
void ConfigManager::setNotifications(bool enabled) {
    if (notifications() == enabled) return;
    m_settings->setValue("ui/notifications", enabled);
    emit notificationsChanged();
}

bool ConfigManager::resumePlayback() const {
    return m_settings->value("ui/resume_playback", true).toBool();
}
void ConfigManager::setResumePlayback(bool enabled) {
    if (resumePlayback() == enabled) return;
    m_settings->setValue("ui/resume_playback", enabled);
    emit resumePlaybackChanged();
}

bool ConfigManager::showChapterMarkers() const {
    return m_settings->value("ui/show_chapter_markers", true).toBool();
}
void ConfigManager::setShowChapterMarkers(bool enabled) {
    if (showChapterMarkers() == enabled) return;
    m_settings->setValue("ui/show_chapter_markers", enabled);
    emit showChapterMarkersChanged();
}

QString ConfigManager::controlBarMode() const {
    QString mode = m_settings->value("ui/control_bar_mode").toString();
    if (mode != QLatin1String("hidden") && mode != QLatin1String("pinned"))
        mode = QStringLiteral("float");
    return mode;
}
void ConfigManager::setControlBarMode(const QString &mode) {
    const QString normalized = (mode == QLatin1String("hidden") || mode == QLatin1String("pinned"))
                                   ? mode : QStringLiteral("float");
    if (controlBarMode() == normalized) return;
    m_settings->setValue("ui/control_bar_mode", normalized);
    emit controlBarModeChanged();
}

QString ConfigManager::subtitleFont() const {
    return m_settings->value("subtitles/font", "sans-serif").toString();
}
void ConfigManager::setSubtitleFont(const QString &font) {
    if (subtitleFont() == font) return;
    m_settings->setValue("subtitles/font", font);
    emit subtitlesChanged();
}

int ConfigManager::subtitleFontSize() const {
    return m_settings->value("subtitles/size", 55).toInt();
}
void ConfigManager::setSubtitleFontSize(int size) {
    size = qBound(10, size, 200);
    if (subtitleFontSize() == size) return;
    m_settings->setValue("subtitles/size", size);
    emit subtitlesChanged();
}

bool ConfigManager::subtitleBold() const {
    return m_settings->value("subtitles/bold", false).toBool();
}
void ConfigManager::setSubtitleBold(bool bold) {
    if (subtitleBold() == bold) return;
    m_settings->setValue("subtitles/bold", bold);
    emit subtitlesChanged();
}

QString ConfigManager::subtitleColor() const {
    return m_settings->value("subtitles/color", "#FFFFFFFF").toString();
}
void ConfigManager::setSubtitleColor(const QString &color) {
    if (subtitleColor() == color) return;
    m_settings->setValue("subtitles/color", color);
    emit subtitlesChanged();
}

int ConfigManager::subtitleBorderSize() const {
    return m_settings->value("subtitles/border_size", 3).toInt();
}
void ConfigManager::setSubtitleBorderSize(int size) {
    size = qBound(0, size, 20);
    if (subtitleBorderSize() == size) return;
    m_settings->setValue("subtitles/border_size", size);
    emit subtitlesChanged();
}

QString ConfigManager::subtitleBorderColor() const {
    return m_settings->value("subtitles/border_color", "#FF000000").toString();
}
void ConfigManager::setSubtitleBorderColor(const QString &color) {
    if (subtitleBorderColor() == color) return;
    m_settings->setValue("subtitles/border_color", color);
    emit subtitlesChanged();
}

bool ConfigManager::subtitleBackground() const {
    return m_settings->value("subtitles/background", false).toBool();
}
void ConfigManager::setSubtitleBackground(bool enabled) {
    if (subtitleBackground() == enabled) return;
    m_settings->setValue("subtitles/background", enabled);
    emit subtitlesChanged();
}

int ConfigManager::subtitlePosition() const {
    return m_settings->value("subtitles/position", 100).toInt();
}
void ConfigManager::setSubtitlePosition(int position) {
    position = qBound(0, position, 100);
    if (subtitlePosition() == position) return;
    m_settings->setValue("subtitles/position", position);
    emit subtitlesChanged();
}

bool ConfigManager::subtitleIgnoreFileStyle() const {
    return m_settings->value("subtitles/ignore_file_style", false).toBool();
}
void ConfigManager::setSubtitleIgnoreFileStyle(bool enabled) {
    if (subtitleIgnoreFileStyle() == enabled) return;
    m_settings->setValue("subtitles/ignore_file_style", enabled);
    emit subtitlesChanged();
}

QStringList ConfigManager::ytdlpConfigArgs() const {
    QStringList args;
    QFile f(ytdlpConfigPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return args;

    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        args << QProcess::splitCommand(line);
    }
    return args;
}

QString ConfigManager::ytdlpConfigPath() const {
    return m_configDir + "/yt-dlp.conf";
}

QString ConfigManager::lumaLogMode() const {
    return normalizedLogMode(m_settings->value("debug/luma", "info").toString());
}

void ConfigManager::setLumaLogMode(const QString &mode) {
    const QString normalized = normalizedLogMode(mode);
    if (lumaLogMode() == normalized) return;
    m_settings->setValue("debug/luma", normalized);
    emit loggingChanged();
}

QString ConfigManager::mpvLogMode() const {
    return normalizedLogMode(m_settings->value("debug/mpv", "off").toString());
}

void ConfigManager::setMpvLogMode(const QString &mode) {
    const QString normalized = normalizedLogMode(mode);
    if (mpvLogMode() == normalized) return;
    m_settings->setValue("debug/mpv", normalized);
    emit loggingChanged();
}

QString ConfigManager::ytdlpLogMode() const {
    return normalizedLogMode(m_settings->value("debug/ytdlp", "off").toString());
}

void ConfigManager::setYtdlpLogMode(const QString &mode) {
    const QString normalized = normalizedLogMode(mode);
    if (ytdlpLogMode() == normalized) return;
    m_settings->setValue("debug/ytdlp", normalized);
    emit loggingChanged();
}

bool ConfigManager::showPanelsFullscreen() const {
    return m_settings->value("ui/show_panels_fullscreen", true).toBool();
}
void ConfigManager::setShowPanelsFullscreen(bool enabled) {
    if (showPanelsFullscreen() == enabled) return;
    m_settings->setValue("ui/show_panels_fullscreen", enabled);
    emit showPanelsFullscreenChanged();
}

int ConfigManager::musicCrossfade() const {
    return qBound(0, m_settings->value("music/crossfade", 0).toInt(), 12);
}
void ConfigManager::setMusicCrossfade(int seconds) {
    const int value = qBound(0, seconds, 12);
    if (musicCrossfade() == value) return;
    m_settings->setValue("music/crossfade", value);
    emit musicCrossfadeChanged();
}

QString ConfigManager::audioOutput() const { return m_audioOutput; }
void ConfigManager::setAudioOutput(const QString &output) {
    const QString normalized = kAudioOutputs.contains(output) ? output : QStringLiteral("auto");
    if (m_audioOutput == normalized) return;
    m_audioOutput = normalized;
    savePlaybackToMpvConf();
    emit audioOutputChanged();
}

QString ConfigManager::hwdec() const { return m_hwdec; }
void ConfigManager::setHwdec(const QString &mode) {
    const QString normalized = kHwdecModes.contains(mode) ? mode : QStringLiteral("auto");
    if (m_hwdec == normalized) return;
    m_hwdec = normalized;
    savePlaybackToMpvConf();
    emit hwdecChanged();
}

QString ConfigManager::ytdlpQuality() const { return m_ytdlpQuality; }

void ConfigManager::setYtdlpQuality(const QString &quality) {
    if (m_ytdlpQuality == quality) return;
    m_ytdlpQuality = quality;
    writeYtdlpConfig();
    emit ytdlpSettingsChanged();
}

QString ConfigManager::ytdlpProxy() const { return m_ytdlpProxy; }

void ConfigManager::setYtdlpProxy(const QString &proxy) {
    const QString normalized = normalizedProxyUrl(proxy);
    if (m_ytdlpProxy == normalized) return;
    m_ytdlpProxy = normalized;
    updateProxyParts();
    writeYtdlpConfig();
    emit ytdlpSettingsChanged();
}

QString ConfigManager::ytdlpProxyType() const { return m_ytdlpProxyType; }

void ConfigManager::setYtdlpProxyType(const QString &type) {
    const QString normalized = kProxyTypes.contains(type) ? type : QStringLiteral("none");
    if (m_ytdlpProxyType == normalized) return;
    m_ytdlpProxyType = normalized;

    if (normalized == QLatin1String("none")) {
        if (!m_ytdlpProxy.isEmpty()) {
            m_ytdlpProxy.clear();
            writeYtdlpConfig();
        }
        // Do not erase the address: picking the type again then does not
        // require re-entering it.
        emit ytdlpSettingsChanged();
        return;
    }

    if (!m_ytdlpProxyAddress.isEmpty())
        setYtdlpProxy(normalized + QStringLiteral("://") + m_ytdlpProxyAddress);
    else
        emit ytdlpSettingsChanged();
}

QString ConfigManager::ytdlpProxyAddress() const { return m_ytdlpProxyAddress; }

void ConfigManager::setYtdlpProxyAddress(const QString &address) {
    QString value = address.trimmed();

    // Allow pasting a full URL: the scheme becomes the proxy type.
    const QString scheme = proxyScheme(value);
    if (!scheme.isEmpty()) {
        value = proxyAddress(value);
        if (kProxyTypes.contains(scheme) && scheme != QLatin1String("none"))
            m_ytdlpProxyType = scheme;
    }
    value = value.trimmed();
    m_ytdlpProxyAddress = value;

    if (value.isEmpty()) {
        if (!m_ytdlpProxy.isEmpty()) {
            m_ytdlpProxy.clear();
            writeYtdlpConfig();
        }
        emit ytdlpSettingsChanged();
        return;
    }

    if (m_ytdlpProxyType == QLatin1String("none") || m_ytdlpProxyType.isEmpty())
        m_ytdlpProxyType = QStringLiteral("socks5");

    const QString rebuilt = m_ytdlpProxyType + QStringLiteral("://") + value;
    if (m_ytdlpProxy == rebuilt) {
        emit ytdlpSettingsChanged();
        return;
    }
    setYtdlpProxy(rebuilt);
}

void ConfigManager::updateProxyParts() {
    const QString scheme = proxyScheme(m_ytdlpProxy);
    m_ytdlpProxyType = kProxyTypes.contains(scheme) ? scheme : QStringLiteral("none");
    m_ytdlpProxyAddress = m_ytdlpProxy.isEmpty() ? QString() : proxyAddress(m_ytdlpProxy);
}

QString ConfigManager::ytdlpFormatExpression() const {
    // Stream video separately from audio: this makes 1080p and above
    // available, not just muxed 360p. yt-dlp will merge the streams if needed.
    if (m_ytdlpQuality == "1080")  return QStringLiteral("bestvideo[height<=1080]+bestaudio/best[height<=1080]");
    if (m_ytdlpQuality == "720")   return QStringLiteral("bestvideo[height<=720]+bestaudio/best[height<=720]");
    if (m_ytdlpQuality == "480")   return QStringLiteral("bestvideo[height<=480]+bestaudio/best[height<=480]");
    if (m_ytdlpQuality == "audio") return QStringLiteral("bestaudio[ext=m4a]/bestaudio");
    return QStringLiteral("bestvideo+bestaudio/best");
}

static QString ytdlpQualityFromFormat(const QString &expr) {
    // Check the height limit first: bestaudio+bestvideo expressions also
    // contain the word "bestaudio" and would otherwise be treated as audio.
    if (expr.contains(QStringLiteral("height<=1080"))) return QStringLiteral("1080");
    if (expr.contains(QStringLiteral("height<=720"))) return QStringLiteral("720");
    if (expr.contains(QStringLiteral("height<=480"))) return QStringLiteral("480");
    if (expr.contains(QStringLiteral("bestvideo"))) return QStringLiteral("best");
    if (expr.contains(QStringLiteral("bestaudio"))) return QStringLiteral("audio");
    return QStringLiteral("best");
}

void ConfigManager::loadYtdlpConfig() {
    m_ytdlpQuality = QStringLiteral("best");
    m_ytdlpProxy.clear();
    m_ytdlpProxyType = QStringLiteral("none");
    m_ytdlpProxyAddress.clear();
    m_ytdlpExtra.clear();

    QFile f(ytdlpConfigPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        updateProxyParts();
        writeYtdlpConfig();
        return;
    }

    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('#')))
            continue;

        if (line.startsWith(QStringLiteral("--format="))) {
            m_ytdlpQuality = ytdlpQualityFromFormat(line.mid(9));
            continue;
        }
        if (line.startsWith(QStringLiteral("--format "))) {
            m_ytdlpQuality = ytdlpQualityFromFormat(line.mid(9).trimmed());
            continue;
        }
        if (line.startsWith(QStringLiteral("-f="))) {
            m_ytdlpQuality = ytdlpQualityFromFormat(line.mid(3));
            continue;
        }
        if (line.startsWith(QStringLiteral("--proxy="))) {
            const QString raw = line.mid(8).trimmed();
            m_ytdlpProxy = normalizedProxyUrl(raw);
            continue;
        }
        if (line.startsWith(QStringLiteral("--proxy "))) {
            const QString raw = line.mid(8).trimmed();
            m_ytdlpProxy = normalizedProxyUrl(raw);
            continue;
        }
        if (line == QLatin1String("--no-playlist")
                || line == QLatin1String("--no-warnings")
                || line == QLatin1String("--no-progress"))
            continue;

        m_ytdlpExtra << line;
    }
    f.close();

    updateProxyParts();
}

void ConfigManager::writeYtdlpConfig() {
    QFile f(ytdlpConfigPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;

    QTextStream out(&f);
    out << "--no-playlist\n";
    out << "--no-warnings\n";
    out << "--no-progress\n";
    out << "--format=" << ytdlpFormatExpression() << "\n";
    if (!m_ytdlpProxy.isEmpty())
        out << "--proxy=" << m_ytdlpProxy << "\n";
    for (const QString &line : m_ytdlpExtra)
        out << line << "\n";

    f.close();
}

QVariant ConfigManager::value(const QString &key, const QVariant &defaultValue) const {
    return m_settings->value(key, defaultValue);
}

void ConfigManager::setValue(const QString &key, const QVariant &value) {
    m_settings->setValue(key, value);
}

void ConfigManager::loadPlaybackSettingsFromMpvConf() {
    // hwdec/ao are stored in mpv.conf in mpv's own syntax.
    QString audio = QStringLiteral("auto");
    QString mode = QStringLiteral("auto");
    bool hasHwdec = false;
    bool hasAo = false;
    bool invalidHwdec = false;

    QFile f(m_configDir + "/mpv.conf");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0) continue;
            const QString key = line.left(eq).trimmed();
            const QString value = line.mid(eq + 1).trimmed();

            if (key == QLatin1String("hwdec")) {
                hasHwdec = true;
                if (kHwdecModes.contains(value))
                    mode = value;
                else
                    invalidHwdec = true;
            } else if (key == QLatin1String("ao")) {
                hasAo = true;
                if (kAudioOutputs.contains(value))
                    audio = value;
            }
        }
        f.close();
    }

    m_hwdec = kHwdecModes.contains(mode) ? mode : QStringLiteral("auto");
    m_audioOutput = kAudioOutputs.contains(audio) ? audio : QStringLiteral("auto");

    bool dirty = false;
    if (!hasHwdec)
        dirty = true; // Always keep an explicit hwdec line in mpv.conf.
    if (invalidHwdec)
        dirty = true; // normalize an unsupported value to "auto"
    if (hasAo && audio == QLatin1String("auto"))
        dirty = true; // "ao=auto" is not a valid driver; remove the line

    if (dirty)
        savePlaybackToMpvConf();
}

void ConfigManager::savePlaybackToMpvConf() {
    QStringList lines;
    bool hasHwdec = false;
    bool hasAo = false;

    QFile f(m_configDir + "/mpv.conf");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            const int eq = line.indexOf(QLatin1Char('='));
            if (!line.startsWith(QLatin1Char('#')) && eq > 0) {
                const QString key = line.left(eq).trimmed();
                if (key == QLatin1String("hwdec")) {
                    if (!hasHwdec) {
                        lines << QStringLiteral("hwdec=") + m_hwdec;
                        hasHwdec = true;
                    }
                    continue;
                }
                if (key == QLatin1String("ao")) {
                    if (!hasAo) {
                        if (m_audioOutput != QLatin1String("auto"))
                            lines << QStringLiteral("ao=") + m_audioOutput;
                        hasAo = true;
                    }
                    continue;
                }
            }
            lines << line;
        }
        f.close();
    }

    if (!hasHwdec)
        lines << QStringLiteral("hwdec=") + m_hwdec;
    if (!hasAo && m_audioOutput != QLatin1String("auto"))
        lines << QStringLiteral("ao=") + m_audioOutput;

    QFile out(m_configDir + "/mpv.conf");
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    QTextStream stream(&out);
    for (const QString &line : lines)
        stream << line << "\n";
}

void ConfigManager::saveWindowSize(int width, int height) {
    m_settings->setValue("window/width", width);
    m_settings->setValue("window/height", height);
    m_settings->sync();
}
