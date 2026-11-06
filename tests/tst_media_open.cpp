#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "ConfigManager.hpp"
#include "MediaManager.hpp"
#include "PlaylistManager.hpp"
#include "mpvqt/MpvItem.hpp"

// These tests use the real libmpv. They are kept separate from the fast core
// tests and run with QT_QPA_PLATFORM=offscreen and ao=null.
class TestMediaOpen : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void openAudio();
    void openVideo();
    void folderScanMergesSiblings();
    void nextFollowsPlayingPlaylist();
    void externalAudioStartsWithFile();
    void endOfPlaylistStopsPlayback();
    void missingFileReportsUnavailable();
    void fileAvailability();
    void fileInfoRequest();
    void fileInfoBitrateEstimate();
    void liveBitrateSample();
    void youtubeLinkThroughYtdlp();
    void youtubeFromPlaylistKeepsPlayingMarker();
    void realYouTubeLink();

private:
    bool waitForFileLoaded(MpvItem *mpv, int timeout = 10000);
    void stopPlayback();

    QTemporaryDir m_temp;
    ConfigManager *m_config = nullptr;
    PlaylistManager *m_playlists = nullptr;
    MpvItem *m_mpv = nullptr;
    MediaManager *m_media = nullptr;
    QByteArray m_oldPath;
};

bool TestMediaOpen::waitForFileLoaded(MpvItem *mpv, int timeout)
{
    QSignalSpy spy(mpv, &MpvItem::fileLoaded);
    if (!spy.wait(timeout))
        return false;
    // Let the demuxer expose the tracks to mpv properties.
    QTest::qWait(200);
    return true;
}

void TestMediaOpen::initTestCase()
{
    QVERIFY2(m_temp.isValid(), "temporary directory");

    // Make sure libmpv does not try to open a real audio device in CI.
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME"))
        qputenv("XDG_CONFIG_HOME", (m_temp.path() + "/config").toUtf8());
    if (qEnvironmentVariableIsEmpty("XDG_CACHE_HOME"))
        qputenv("XDG_CACHE_HOME", (m_temp.path() + "/cache").toUtf8());
    const QString configDir = qEnvironmentVariable("XDG_CONFIG_HOME") + "/luma";
    QVERIFY(QDir().mkpath(configDir));
    QFile mpvConf(configDir + "/mpv.conf");
    QVERIFY(mpvConf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    mpvConf.write("ao=null\nhwdec=no\n");
    mpvConf.close();

    QVERIFY2(QFileInfo(QStringLiteral(LUMA_TEST_DATA_DIR "/test_audio.wav")).exists(),
             "audio fixture missing");
    QVERIFY2(QFileInfo(QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4")).exists(),
             "video fixture missing");

    m_config = new ConfigManager;
    m_playlists = new PlaylistManager(m_config);
    m_mpv = new MpvItem;
    m_media = new MediaManager(m_mpv);
    m_media->setPlaylistManager(m_playlists);
    m_media->setConfig(m_config);
    m_oldPath = qgetenv("PATH");
    QVERIFY(!m_media->hasMedia());
}

void TestMediaOpen::cleanupTestCase()
{
    qputenv("PATH", m_oldPath);
    delete m_media;
    m_media = nullptr;
    delete m_playlists;
    m_playlists = nullptr;
    delete m_config;
    m_config = nullptr;
    delete m_mpv;
    m_mpv = nullptr;
}

void TestMediaOpen::stopPlayback()
{
    if (m_mpv)
        m_mpv->command(QStringList() << QStringLiteral("stop"));
    QTest::qWait(100);
}

void TestMediaOpen::openAudio()
{
    stopPlayback();
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->openFile(QStringLiteral(LUMA_TEST_DATA_DIR "/test_audio.wav"));

    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(m_mpv->duration() > 0.0, 5000);
    QVERIFY(m_media->hasMedia());
    QTRY_VERIFY_WITH_TIMEOUT(
        m_mpv->property("audio-codec").toString().contains("PCM", Qt::CaseInsensitive),
        5000);
    const QString videoCodec = m_mpv->property("video-codec").toString();
    QVERIFY2(videoCodec.isEmpty() || videoCodec == QLatin1String("no"),
             qPrintable("unexpected video codec for audio: " + videoCodec));
    QVERIFY(!m_media->hasVideo());
}

void TestMediaOpen::folderScanMergesSiblings()
{
    // Opening a file must start it immediately and add the rest of the folder
    // only after the background scan, without replacing the current entry.
    const QString folder = m_temp.path() + "/folder-scan";
    QVERIFY(QDir().mkpath(folder));
    const QString audio = folder + "/test_audio.wav";
    const QString video = folder + "/test_video.mp4";
    QVERIFY(QFile::copy(QStringLiteral(LUMA_TEST_DATA_DIR "/test_audio.wav"), audio));
    QVERIFY(QFile::copy(QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4"), video));

    stopPlayback();
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->openFile(audio);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 10000);
    QCOMPARE(m_media->playlist()->currentFile(), audio);
    QVERIFY(m_media->hasMedia());

    PlaylistModel *def = m_playlists->defaultModel();
    QVERIFY(def);
    QTRY_VERIFY_WITH_TIMEOUT(def->contains(video), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(def->count() >= 2, 5000);
    QCOMPARE(def->currentFile(), audio);
}

void TestMediaOpen::nextFollowsPlayingPlaylist()
{
    // Play from Default, switch the visible tab to another playlist, then move
    // to the next file: it must come from the playlist that is playing.
    stopPlayback();
    const QString audio = QStringLiteral(LUMA_TEST_DATA_DIR "/test_audio.wav");
    const QString video = QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4");
    const QString viewFile = m_temp.path() + "/view-playlist-file.mp4";
    QVERIFY(QFile::copy(video, viewFile));

    PlaylistModel *def = m_playlists->defaultModel();
    QVERIFY(def);
    def->setFiles({audio, video}, audio);
    m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);

    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->playIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 10000);
    QCOMPARE(m_mpv->source(), audio);

    const QString viewId = m_playlists->createPlaylist(QStringLiteral("View"));
    PlaylistModel *view = m_playlists->modelById(viewId);
    QVERIFY(view);
    view->setFiles({viewFile}, viewFile);
    m_playlists->setActiveId(viewId);
    QCOMPARE(m_playlists->activeId(), viewId);

    QSignalSpy nextLoaded(m_mpv, &MpvItem::fileLoaded);
    m_media->playNext();
    QTRY_VERIFY_WITH_TIMEOUT(nextLoaded.count() > 0, 10000);
    QCOMPARE(m_mpv->source(), video);
    QCOMPARE(m_playlists->activeId(), viewId);

    m_playlists->deletePlaylist(viewId);
}

void TestMediaOpen::openVideo()
{
    stopPlayback();
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->openFile(QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4"));

    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 10000);
    QVERIFY(m_media->hasMedia());
    QTRY_VERIFY_WITH_TIMEOUT(m_mpv->duration() > 0.0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        m_mpv->property("video-codec").toString().contains("264", Qt::CaseInsensitive),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        m_mpv->property("audio-codec").toString().contains("AAC", Qt::CaseInsensitive),
        5000);
    QCOMPARE(m_mpv->property("width").toInt(), 64);
    QCOMPARE(m_mpv->property("height").toInt(), 64);
    QVERIFY(m_media->hasVideo());

    // These read values observed by MpvItem instead of blocking the mpv thread.
    QTRY_VERIFY_WITH_TIMEOUT(!m_media->getTracks("video").isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!m_media->getTracks("audio").isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(m_media->currentTrackId("vid") > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(m_media->currentTrackId("aid") > 0, 5000);

    m_media->setAspectRatio("16:9");
    QTRY_COMPARE_WITH_TIMEOUT(m_media->getAspectRatio(), QStringLiteral("16:9"), 5000);
    m_media->setAspectRatio("original");
    QTRY_COMPARE_WITH_TIMEOUT(m_media->getAspectRatio(), QStringLiteral("original"), 5000);
}

void TestMediaOpen::externalAudioStartsWithFile()
{
    stopPlayback();

    // yt-dlp returns separate video/audio URLs. The audio must be attached at
    // load time, not after fileLoaded, or the first seconds are silent.
    const QString video = m_temp.path() + QStringLiteral("/external-video.mp4");
    const QString audio = m_temp.path() + QStringLiteral("/external,audio=1.wav");
    QVERIFY(QFile::copy(QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4"), video));
    QVERIFY(QFile::copy(QStringLiteral(LUMA_TEST_DATA_DIR "/test_audio.wav"), audio));

    m_mpv->setExternalAudio(audio);
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->openFile(video);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 10000);
    QTest::qWait(200);

    int audioTracks = 0;
    int externalId = -1;
    const QVariantList tracks = m_mpv->getProperty(QStringLiteral("track-list")).toList();
    for (const QVariant &item : tracks) {
        const QVariantMap track = item.toMap();
        if (track.value(QStringLiteral("type")).toString() != QLatin1String("audio"))
            continue;
        ++audioTracks;
        if (track.value(QStringLiteral("external")).toBool())
            externalId = track.value(QStringLiteral("id")).toInt();
    }
    QVERIFY2(audioTracks >= 2, "the external audio track was not attached");
    QVERIFY2(externalId > 0, "the external audio track is not marked external");
    QCOMPARE(m_mpv->getProperty(QStringLiteral("aid")).toInt(), externalId);
    QVERIFY2(m_mpv->property("audio-codec").toString().contains("PCM", Qt::CaseInsensitive),
             "the external WAV track is not the selected audio track");

    stopPlayback();
}

void TestMediaOpen::endOfPlaylistStopsPlayback()
{
    stopPlayback();

    // openVideo() already left this file in the playlist and as the current
    // entry, so openFile() would be a no-op; restart it explicitly.
    const QString path = QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4");
    PlaylistModel *def = m_playlists->defaultModel();
    QVERIFY(def);
    int index = def->indexOf(path);
    if (index < 0) {
        m_media->openFile(path);
        index = def->indexOf(path);
    }
    QVERIFY(index >= 0);

    // The fixture is 0.5 s long: play it through and let it end.
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    QSignalSpy ended(m_mpv, &MpvItem::fileEnded);
    m_media->playIndex(index);
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(ended.count() > 0, 15000);

    // What ShortcutHandler does for EOF. The last item of the playlist in
    // sequential mode must unload the file, not leave the finished frame.
    m_media->playNext();
    QTRY_COMPARE_WITH_TIMEOUT(m_mpv->duration(), 0.0, 2000);
    QCOMPARE(m_mpv->position(), 0.0);
    QTRY_VERIFY_WITH_TIMEOUT(m_mpv->paused(), 2000);
    QVERIFY(!m_media->isLoading());
    QVERIFY(!m_media->hasMedia());
}

// A playlist entry whose file was deleted must not be handed to mpv silently:
// the UI gets a signal and the entry is marked unavailable.
void TestMediaOpen::missingFileReportsUnavailable()
{
    PlaylistModel *def = m_playlists->defaultModel();
    QVERIFY(def);
    m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);

    const QString missing = m_temp.path() + "/deleted-file.mkv";
    def->setFiles({missing}, missing);

    QSignalSpy spy(m_media, &MediaManager::fileUnavailable);
    m_media->playIndex(0);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), missing);
    QCOMPARE(def->data(def->index(0), Qt::UserRole + 7).toBool(), true);
    QVERIFY(def->isUnavailable(missing));
    QVERIFY(def->toVariantList().first().toMap().value("unavailable").toBool());
}

// The playlist context menu hides Information for missing local files but
// keeps it for streams (where the URL can still be edited).
void TestMediaOpen::fileAvailability()
{
    QVERIFY(!m_media->isFileAvailable(QString()));
    QVERIFY(!m_media->isFileAvailable(QStringLiteral("/no/such/file.mkv")));
    QVERIFY(m_media->isFileAvailable(QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4")));
    QVERIFY(m_media->isFileAvailable(QStringLiteral("https://example.com/stream")));
}

void TestMediaOpen::fileInfoRequest()
{
    const QString path = QStringLiteral(LUMA_TEST_DATA_DIR "/test_audio.wav");
    QSignalSpy spy(m_media, &MediaManager::fileInfoReady);
    m_media->requestFileInfo(path);

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), path);
    const QVariantMap info = args.at(1).toMap();
    QCOMPARE(info.value("path").toString(), path);
    QCOMPARE(info.value("isAudio").toBool(), true);
}

// Matroska/WebM store no per-track bitrate. The information dialog used to
// show 0 kbps for both streams; the missing values are now estimated.
void TestMediaOpen::fileInfoBitrateEstimate()
{
    const QString path = QStringLiteral(LUMA_TEST_DATA_DIR "/webm/test_video.webm");
    QSignalSpy spy(m_media, &MediaManager::fileInfoReady);
    m_media->requestFileInfo(path);

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);
    const QVariantMap info = spy.takeFirst().at(1).toMap();

    QCOMPARE(info.value("isAudio").toBool(), false);
    QCOMPARE(info.value("video_bitrate_estimated").toBool(), true);
    QCOMPARE(info.value("audio_bitrate_estimated").toBool(), true);
    QVERIFY2(info.value("video_bitrate").toLongLong() > 0, "video bitrate not estimated");
    QVERIFY2(info.value("audio_bitrate").toLongLong() > 0, "audio bitrate not estimated");

    // The video estimate is the container total minus the other streams, so
    // both values together should stay close to the container bitrate.
    const qint64 total = info.value("bitrate").toLongLong();
    const qint64 sum = info.value("video_bitrate").toLongLong()
        + info.value("audio_bitrate").toLongLong();
    QVERIFY(total > 0);
    QVERIFY(sum <= total * 6 / 5);
}

// The statistics panel samples the bitrate of a short window around the
// playback position: mpv's own video-bitrate is a last-GOP average and only
// changes at keyframes.
void TestMediaOpen::liveBitrateSample()
{
    const QString path = QStringLiteral(LUMA_TEST_DATA_DIR "/webm/test_video.webm");
    QSignalSpy spy(m_media, &MediaManager::liveBitrateReady);
    m_media->requestLiveBitrate(path, 1.0);

    QVERIFY(spy.wait(5000));
    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), path);
    QCOMPARE(args.at(1).toDouble(), 1.0);
    QVERIFY2(args.at(2).toLongLong() > 0, "video bitrate not sampled");
    QVERIFY2(args.at(3).toLongLong() > 0, "audio bitrate not sampled");
}

void TestMediaOpen::youtubeLinkThroughYtdlp()
{
    stopPlayback();

    QTemporaryDir fakeBin;
    QVERIFY(fakeBin.isValid());
    const QString video = QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4");
    const QString scriptPath = fakeBin.path() + "/yt-dlp";
    QFile script(scriptPath);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\n"
                 "printf '{\"title\":\"Fake YouTube\",\"url\":\"%s\"}\\n' \"$LUMA_FAKE_VIDEO\"\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));
    qputenv("LUMA_FAKE_VIDEO", video.toUtf8());
    qputenv("PATH", fakeBin.path().toUtf8() + ':' + m_oldPath);

    const QString url = QStringLiteral("https://www.youtube.com/watch?v=test");
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->openFile(url);

    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(m_mpv->duration() > 0.0, 5000);
    QCOMPARE(QFileInfo(m_mpv->source()).canonicalFilePath(),
             QFileInfo(video).canonicalFilePath());

    PlaylistModel *def = m_playlists->defaultModel();
    QVERIFY(def);
    const int index = def->indexOf(url);
    QVERIFY(index >= 0);
    QTRY_COMPARE_WITH_TIMEOUT(
        def->data(def->index(index), Qt::UserRole + 6).toString(),
        QStringLiteral("Fake YouTube"), 5000);
    QVERIFY(m_media->hasVideo());
}

void TestMediaOpen::youtubeFromPlaylistKeepsPlayingMarker()
{
    stopPlayback();

    // yt-dlp is slow enough for the tab switch to happen while the link is
    // being resolved.
    QTemporaryDir fakeBin;
    QVERIFY(fakeBin.isValid());
    const QString video = QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4");
    const QString scriptPath = fakeBin.path() + "/yt-dlp";
    QFile script(scriptPath);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("#!/bin/sh\n"
                 "sleep 0.4\n"
                 "printf '{\"title\":\"Fake YouTube\",\"url\":\"%s\"}\\n' \"$LUMA_FAKE_VIDEO\"\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                  | QFileDevice::ExeOwner));
    qputenv("LUMA_FAKE_VIDEO", video.toUtf8());
    qputenv("PATH", fakeBin.path().toUtf8() + ':' + m_oldPath);

    const QString firstId = m_playlists->createPlaylist(QStringLiteral("First"));
    const QString secondId = m_playlists->createPlaylist(QStringLiteral("Second"));
    QVERIFY(!firstId.isEmpty());
    QVERIFY(!secondId.isEmpty());

    const QString url = QStringLiteral("https://www.youtube.com/watch?v=playlist-marker");
    m_playlists->addUrlToPlaylist(firstId, url);
    PlaylistModel *first = m_playlists->modelById(firstId);
    PlaylistModel *second = m_playlists->modelById(secondId);
    QVERIFY(first);
    QVERIFY(second);

    m_playlists->setActiveId(firstId);
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->playIndex(0);
    // The entry is marked as playing immediately, while yt-dlp is still busy.
    QVERIFY(first->isPlaying());
    // The user switches to the other playlist before yt-dlp has resolved the
    // link; the playing marker must stay on the first playlist.
    m_playlists->setActiveId(secondId);

    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(first->isPlaying(), 5000);
    QCOMPARE(first->currentIndex(), 0);
    QVERIFY2(first->data(first->index(0), Qt::DisplayRole).toString()
                 .startsWith(QStringLiteral("▶ ")),
             qPrintable(first->data(first->index(0), Qt::DisplayRole).toString()));
    QVERIFY(!second->isPlaying());

    stopPlayback();
    m_playlists->setActiveId(PlaylistManager::DEFAULT_ID);
    m_playlists->deletePlaylist(firstId);
    m_playlists->deletePlaylist(secondId);
}

void TestMediaOpen::realYouTubeLink()
{
    const QByteArray url = qgetenv("LUMA_TEST_YOUTUBE_URL");
    if (url.isEmpty())
        QSKIP("set LUMA_TEST_YOUTUBE_URL to run the real YouTube playback test");

    stopPlayback();
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->openFile(QString::fromUtf8(url));
    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(m_mpv->duration() > 0.0, 5000);
    QVERIFY(m_media->hasVideo());
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestMediaOpen test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_media_open.moc"
