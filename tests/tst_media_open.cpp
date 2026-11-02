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
    void endOfPlaylistStopsPlayback();
    void fileInfoRequest();
    void youtubeLinkThroughYtdlp();
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
    QTRY_VERIFY_WITH_TIMEOUT(
        m_mpv->property("audio-codec").toString().contains("PCM", Qt::CaseInsensitive),
        5000);
    const QString videoCodec = m_mpv->property("video-codec").toString();
    QVERIFY2(videoCodec.isEmpty() || videoCodec == QLatin1String("no"),
             qPrintable("unexpected video codec for audio: " + videoCodec));
    QVERIFY(!m_media->hasVideo());
}

void TestMediaOpen::openVideo()
{
    stopPlayback();
    QSignalSpy loaded(m_mpv, &MpvItem::fileLoaded);
    m_media->openFile(QStringLiteral(LUMA_TEST_DATA_DIR "/test_video.mp4"));

    QTRY_VERIFY_WITH_TIMEOUT(loaded.count() > 0, 10000);
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
