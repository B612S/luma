#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include "ConfigManager.hpp"
#include "TestEnvironment.h"

// Settings: default values, legacy key migration, control bar mode,
// yt-dlp options (the ~/.config/luma/yt-dlp.conf file).
class TestConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void defaults();
    void loggingModes();
    void legacyDebugLoggingMigrates();
    void playbackSettings();
    void playbackSettingsPersist();
    void playbackMigratesToMpvConf();
    void controlBarModePersists();
    void legacyPinControlsMigrates();
    void unsupportedLanguageFallsBack();
    void languageResolution();
    void reloadsExternalChanges();
    void ytdlpQualityAndProxy();
    void ytdlpProxyTypeAndAddress();
    void ytdlpSocksSchemeMigrates();
    void ytdlpCommentsAreRemoved();
    void ytdlpKeepsCustomOptions();

private:
    static QString configHome()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
    static QString lumaDir() { return configHome() + "/luma"; }
};

void TestConfigManager::init()
{
    // Every test starts with a clean config.
    QDir(lumaDir()).removeRecursively();
}

void TestConfigManager::defaults()
{
    ConfigManager config;

    QVERIFY(config.rememberVolume());
    QCOMPARE(config.defaultVolume(), 80);
    QCOMPARE(config.volume(), 80);
    QCOMPARE(config.language(), QStringLiteral("auto"));
    QCOMPARE(config.playlistMode(), QStringLiteral("sequential"));
    QVERIFY(config.notifications());
    QVERIFY(config.showPanelsFullscreen());
    QCOMPARE(config.controlBarMode(), QStringLiteral("float"));
    QCOMPARE(config.ytdlpQuality(), QStringLiteral("best"));
    QVERIFY(config.ytdlpProxy().isEmpty());

    // hwdec is stored in mpv.conf in mpv's own syntax; "auto" audio means
    // there is no ao line.
    QFile mpv(lumaDir() + "/mpv.conf");
    QVERIFY(mpv.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString mpvContent = QString::fromUtf8(mpv.readAll());
    mpv.close();
    QVERIFY(mpvContent.contains(QStringLiteral("hwdec=auto")));
    QVERIFY(!mpvContent.contains(QStringLiteral("ao=")));

    QFile luma(lumaDir() + "/luma.conf");
    QVERIFY(luma.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString lumaContent = QString::fromUtf8(luma.readAll());
    luma.close();
    QVERIFY(!lumaContent.contains(QStringLiteral("[playback]")));
    QVERIFY(lumaContent.contains(QStringLiteral("language=auto")));
}

void TestConfigManager::loggingModes()
{
    {
        ConfigManager config;
        QCOMPARE(config.lumaLogMode(), QStringLiteral("info"));
        QCOMPARE(config.mpvLogMode(), QStringLiteral("off"));
        QCOMPARE(config.ytdlpLogMode(), QStringLiteral("off"));

        QSignalSpy changed(&config, &ConfigManager::loggingChanged);
        config.setLumaLogMode("debug");
        config.setMpvLogMode("error");
        config.setYtdlpLogMode("info");
        QCOMPARE(changed.count(), 3);

        // Setting the same value must not emit another change.
        config.setMpvLogMode("error");
        QCOMPARE(changed.count(), 3);
    }

    {
        QSettings settings(lumaDir() + "/luma.conf", QSettings::IniFormat);
        QCOMPARE(settings.value("debug/luma").toString(), QStringLiteral("debug"));
        QCOMPARE(settings.value("debug/mpv").toString(), QStringLiteral("error"));
        QCOMPARE(settings.value("debug/ytdlp").toString(), QStringLiteral("info"));
    }

    ConfigManager config2;
    QCOMPARE(config2.lumaLogMode(), QStringLiteral("debug"));
    QCOMPARE(config2.mpvLogMode(), QStringLiteral("error"));
    QCOMPARE(config2.ytdlpLogMode(), QStringLiteral("info"));

    // "none" is the legacy spelling of "off".
    config2.setLumaLogMode("none");
    QCOMPARE(config2.lumaLogMode(), QStringLiteral("off"));
}

void TestConfigManager::legacyDebugLoggingMigrates()
{
    QDir().mkpath(lumaDir());
    {
        QFile luma(lumaDir() + "/luma.conf");
        QVERIFY(luma.open(QIODevice::WriteOnly | QIODevice::Text));
        luma.write("[ui]\ndebug_logging=debug\n");
    }

    {
        ConfigManager config;
        QCOMPARE(config.lumaLogMode(), QStringLiteral("debug"));
    }

    QSettings settings(lumaDir() + "/luma.conf", QSettings::IniFormat);
    QVERIFY(!settings.contains(QStringLiteral("ui/debug_logging")));
    QCOMPARE(settings.value("debug/luma").toString(), QStringLiteral("debug"));
}

void TestConfigManager::playbackSettings()
{
    ConfigManager config;
    QCOMPARE(config.audioOutput(), QStringLiteral("auto"));
    QCOMPARE(config.hwdec(), QStringLiteral("auto"));

    config.setAudioOutput("pipewire");
    config.setHwdec("nvdec-copy");
    QCOMPARE(config.audioOutput(), QStringLiteral("pipewire"));
    QCOMPARE(config.hwdec(), QStringLiteral("nvdec-copy"));

    {
        QFile mpv(lumaDir() + "/mpv.conf");
        QVERIFY(mpv.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(mpv.readAll());
        mpv.close();
        QVERIFY(content.contains(QStringLiteral("ao=pipewire")));
        QVERIFY(content.contains(QStringLiteral("hwdec=nvdec-copy")));
    }

    // Unknown values (including the old "pulseaudio" name) fall back
    // to "auto" and never reach mpv as a driver name.
    config.setAudioOutput("pulseaudio");
    config.setHwdec("mediacodec");
    QCOMPARE(config.audioOutput(), QStringLiteral("auto"));
    QCOMPARE(config.hwdec(), QStringLiteral("auto"));

    {
        QFile mpv(lumaDir() + "/mpv.conf");
        QVERIFY(mpv.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(mpv.readAll());
        mpv.close();
        QVERIFY(!content.contains(QStringLiteral("ao=")));
        QVERIFY(content.contains(QStringLiteral("hwdec=auto")));
    }

    // "no" is a valid value (software decoding).
    config.setHwdec("no");
    QCOMPARE(config.hwdec(), QStringLiteral("no"));
    {
        QFile mpv(lumaDir() + "/mpv.conf");
        QVERIFY(mpv.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(mpv.readAll());
        mpv.close();
        QVERIFY(content.contains(QStringLiteral("hwdec=no")));
    }
}

void TestConfigManager::playbackSettingsPersist()
{
    {
        ConfigManager config;
        config.setAudioOutput("alsa");
        config.setHwdec("vaapi-copy");
    }

    ConfigManager config2;
    QCOMPARE(config2.audioOutput(), QStringLiteral("alsa"));
    QCOMPARE(config2.hwdec(), QStringLiteral("vaapi-copy"));

    QFile mpv(lumaDir() + "/mpv.conf");
    QVERIFY(mpv.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(mpv.readAll());
    mpv.close();
    QVERIFY(content.contains(QStringLiteral("ao=alsa")));
    QVERIFY(content.contains(QStringLiteral("hwdec=vaapi-copy")));

    QSettings settings(lumaDir() + "/luma.conf", QSettings::IniFormat);
    QVERIFY(!settings.contains(QStringLiteral("playback/audio_output")));
    QVERIFY(!settings.contains(QStringLiteral("playback/hwdec")));
}

void TestConfigManager::playbackMigratesToMpvConf()
{
    // Older versions kept these values in luma.conf; mpv.conf had no options.
    QDir().mkpath(lumaDir());
    {
        QFile luma(lumaDir() + "/luma.conf");
        QVERIFY(luma.open(QIODevice::WriteOnly | QIODevice::Text));
        luma.write("[playback]\naudio_output=alsa\nhwdec=vaapi-copy\n");
    }
    {
        QFile mpv(lumaDir() + "/mpv.conf");
        QVERIFY(mpv.open(QIODevice::WriteOnly | QIODevice::Text));
        mpv.write("deinterlace=auto\n");
    }

    {
        ConfigManager config;
        QCOMPARE(config.audioOutput(), QStringLiteral("alsa"));
        QCOMPARE(config.hwdec(), QStringLiteral("vaapi-copy"));
    }

    // Values were written into mpv.conf; other options stay; legacy keys are gone.
    {
        QFile mpv(lumaDir() + "/mpv.conf");
        QVERIFY(mpv.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(mpv.readAll());
        mpv.close();
        QVERIFY(content.contains(QStringLiteral("ao=alsa")));
        QVERIFY(content.contains(QStringLiteral("hwdec=vaapi-copy")));
        QVERIFY(content.contains(QStringLiteral("deinterlace=auto")));
    }
    {
        QSettings settings(lumaDir() + "/luma.conf", QSettings::IniFormat);
        QVERIFY(!settings.contains(QStringLiteral("playback/audio_output")));
        QVERIFY(!settings.contains(QStringLiteral("playback/hwdec")));
    }

    // If both files have values, mpv.conf is authoritative.
    {
        ConfigManager config;
        config.setValue("playback/audio_output", QStringLiteral("auto"));
        config.setValue("playback/hwdec", QStringLiteral("auto"));
    }
    {
        QFile mpv(lumaDir() + "/mpv.conf");
        QVERIFY(mpv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        mpv.write("hwdec=vaapi\nao=pulse\n");
    }

    ConfigManager config2;
    QCOMPARE(config2.audioOutput(), QStringLiteral("pulse"));
    QCOMPARE(config2.hwdec(), QStringLiteral("vaapi"));

    QSettings settings(lumaDir() + "/luma.conf", QSettings::IniFormat);
    QVERIFY(!settings.contains(QStringLiteral("playback/audio_output")));
    QVERIFY(!settings.contains(QStringLiteral("playback/hwdec")));
}

void TestConfigManager::controlBarModePersists()
{
    {
        ConfigManager config;
        config.setControlBarMode("hidden");
    }

    ConfigManager config2;
    QCOMPARE(config2.controlBarMode(), QStringLiteral("hidden"));

    config2.setControlBarMode("pinned");
    QCOMPARE(config2.controlBarMode(), QStringLiteral("pinned"));

    // An unknown value is normalized to "float".
    config2.setControlBarMode("nonsense");
    QCOMPARE(config2.controlBarMode(), QStringLiteral("float"));
}

void TestConfigManager::legacyPinControlsMigrates()
{
    {
        ConfigManager config;
        config.setValue("ui/pin_controls", true);
    }

    ConfigManager config2;
    QCOMPARE(config2.controlBarMode(), QStringLiteral("pinned"));
}

void TestConfigManager::unsupportedLanguageFallsBack()
{
    ConfigManager config;
    QCOMPARE(config.language(), QStringLiteral("auto"));

    config.setLanguage("ja");
    QCOMPARE(config.language(), QStringLiteral("en"));

    config.setLanguage("auto");
    QCOMPARE(config.language(), QStringLiteral("auto"));

    config.setLanguage("ru");
    QCOMPARE(config.language(), QStringLiteral("ru"));
}

void TestConfigManager::languageResolution()
{
    // An explicit choice wins regardless of the system locale.
    QCOMPARE(ConfigManager::resolveLanguage("en", "ru_RU"), QStringLiteral("en"));
    QCOMPARE(ConfigManager::resolveLanguage("ru", "en_US"), QStringLiteral("ru"));

    // "auto" follows the system locale; only Russian has a translation.
    QCOMPARE(ConfigManager::resolveLanguage("auto", "ru_RU"), QStringLiteral("ru"));
    QCOMPARE(ConfigManager::resolveLanguage("auto", "ru_UA"), QStringLiteral("ru"));
    QCOMPARE(ConfigManager::resolveLanguage("auto", "en_US"), QStringLiteral("en"));
    QCOMPARE(ConfigManager::resolveLanguage("auto", "ja_JP"), QStringLiteral("en"));
    QCOMPARE(ConfigManager::resolveLanguage("auto", "C"), QStringLiteral("en"));

    // Unknown stored values never leave the UI untranslated.
    QCOMPARE(ConfigManager::resolveLanguage("ja", "ru_RU"), QStringLiteral("en"));
}

void TestConfigManager::reloadsExternalChanges()
{
    ConfigManager config;
    QVERIFY(config.singleInstance());
    QVERIFY(config.notifications());
    QCOMPARE(config.playlistMode(), QStringLiteral("sequential"));
    QCOMPARE(config.controlBarMode(), QStringLiteral("float"));
    QCOMPARE(config.hwdec(), QStringLiteral("auto"));

    // Another instance (single instance mode off there) rewrites luma.conf
    // while this one is running.
    {
        QSettings other(lumaDir() + "/luma.conf", QSettings::IniFormat);
        other.setValue("ui/single_instance", false);
        other.setValue("ui/notifications", false);
        other.setValue("playlist/mode", "repeat");
        other.setValue("ui/control_bar_mode", "hidden");
        other.sync();
    }

    QTRY_VERIFY_WITH_TIMEOUT(!config.singleInstance(), 5000);
    QVERIFY(!config.notifications());
    QCOMPARE(config.playlistMode(), QStringLiteral("repeat"));
    QCOMPARE(config.controlBarMode(), QStringLiteral("hidden"));

    // Playback settings live in mpv.conf and must be picked up as well.
    {
        QFile mpv(lumaDir() + "/mpv.conf");
        QVERIFY(mpv.open(QIODevice::ReadOnly | QIODevice::Text));
        QString content = QString::fromUtf8(mpv.readAll());
        mpv.close();
        content.replace(QStringLiteral("hwdec=auto"), QStringLiteral("hwdec=vaapi"));
        QVERIFY(mpv.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        mpv.write(content.toUtf8());
        mpv.close();
    }
    QTRY_COMPARE_WITH_TIMEOUT(config.hwdec(), QStringLiteral("vaapi"), 5000);
}

void TestConfigManager::ytdlpQualityAndProxy()
{
    ConfigManager config;
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("bestvideo+bestaudio/best"));

    config.setYtdlpQuality("720");
    QCOMPARE(config.ytdlpQuality(), QStringLiteral("720"));
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("bestvideo[height<=720]+bestaudio/best[height<=720]"));

    config.setYtdlpQuality("audio");
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("bestaudio[ext=m4a]/bestaudio"));

    config.setYtdlpQuality("1080");
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("bestvideo[height<=1080]+bestaudio/best[height<=1080]"));

    config.setYtdlpProxy("socks5://127.0.0.1:1080");
    QCOMPARE(config.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));

    // The file really was rewritten.
    QFile file(config.ytdlpConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("--format=bestvideo[height<=1080]+bestaudio/best[height<=1080]")));
    QVERIFY(content.contains(QStringLiteral("--proxy=socks5://127.0.0.1:1080")));
    QVERIFY(content.contains(QStringLiteral("--no-playlist")));

    // And it is read back by a new instance.
    ConfigManager config2;
    QCOMPARE(config2.ytdlpQuality(), QStringLiteral("1080"));
    QCOMPARE(config2.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));
}

void TestConfigManager::ytdlpProxyTypeAndAddress()
{
    {
        ConfigManager config;
        QCOMPARE(config.ytdlpProxyType(), QStringLiteral("none"));
        QVERIFY(config.ytdlpProxyAddress().isEmpty());

        // The type can be chosen before an address is entered — it must not "bounce back".
        config.setYtdlpProxyType("socks5");
        QCOMPARE(config.ytdlpProxyType(), QStringLiteral("socks5"));
        QVERIFY(config.ytdlpProxy().isEmpty());

        config.setYtdlpProxyAddress("127.0.0.1:1080");
        QCOMPARE(config.ytdlpProxyType(), QStringLiteral("socks5"));
        QCOMPARE(config.ytdlpProxyAddress(), QStringLiteral("127.0.0.1:1080"));
        QCOMPARE(config.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));

        // Changing the type rebuilds the URL.
        config.setYtdlpProxyType("socks5h");
        QCOMPARE(config.ytdlpProxy(), QStringLiteral("socks5h://127.0.0.1:1080"));

        // Pasting a full URL into the address field moves the scheme into the type.
        config.setYtdlpProxyAddress("http://10.0.0.1:3128");
        QCOMPARE(config.ytdlpProxyType(), QStringLiteral("http"));
        QCOMPARE(config.ytdlpProxyAddress(), QStringLiteral("10.0.0.1:3128"));
        QCOMPARE(config.ytdlpProxy(), QStringLiteral("http://10.0.0.1:3128"));

        // The old "socks://" name is mapped to SOCKS5: yt-dlp hangs with it.
        config.setYtdlpProxy("socks://127.0.0.1:1080");
        QCOMPARE(config.ytdlpProxyType(), QStringLiteral("socks5"));
        QCOMPARE(config.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));

        // "Not used" removes the proxy but remembers the address until restart.
        config.setYtdlpProxyType("none");
        QVERIFY(config.ytdlpProxy().isEmpty());
        QCOMPARE(config.ytdlpProxyAddress(), QStringLiteral("127.0.0.1:1080"));

        QFile file(config.ytdlpConfigPath());
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        file.close();
        QVERIFY(!content.contains(QStringLiteral("--proxy=")));

        config.setYtdlpProxyType("socks5");
        QCOMPARE(config.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));
    }

    // All of this is saved and read back by a new instance.
    ConfigManager config2;
    QCOMPARE(config2.ytdlpProxyType(), QStringLiteral("socks5"));
    QCOMPARE(config2.ytdlpProxyAddress(), QStringLiteral("127.0.0.1:1080"));
    QCOMPARE(config2.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));
}

void TestConfigManager::ytdlpSocksSchemeMigrates()
{
    // Old config with the generic "socks://" scheme: yt-dlp hangs with it.
    QDir().mkpath(lumaDir());
    {
        QFile file(lumaDir() + "/yt-dlp.conf");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("--no-playlist\n--proxy=socks://127.0.0.1:1080\n");
    }

    ConfigManager config;
    QCOMPARE(config.ytdlpProxyType(), QStringLiteral("socks5"));
    QCOMPARE(config.ytdlpProxyAddress(), QStringLiteral("127.0.0.1:1080"));
    QCOMPARE(config.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));

    // The file is rewritten right away so yt-dlp gets a working scheme.
    QFile file(config.ytdlpConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("--proxy=socks5://127.0.0.1:1080")));
    QVERIFY(!content.contains(QStringLiteral("--proxy=socks://")));
}

void TestConfigManager::ytdlpCommentsAreRemoved()
{
    // An old config may contain generated/user comments; they must disappear
    // while keeping the actual options.
    QDir().mkpath(lumaDir());
    {
        QFile file(lumaDir() + "/yt-dlp.conf");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("# legacy header\n--no-playlist\n# another comment\n--format=best[height<=720]\n");
    }

    ConfigManager config;
    QFile file(config.ytdlpConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(!content.contains(QLatin1Char('#')));
    QVERIFY(content.contains(QStringLiteral("--no-playlist")));
    QVERIFY(content.contains(QStringLiteral("--format=best[height<=720]")));

    // A freshly generated file has no comments either.
    QVERIFY(QFile::remove(config.ytdlpConfigPath()));
    ConfigManager fresh;
    QFile freshFile(fresh.ytdlpConfigPath());
    QVERIFY(freshFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString freshContent = QString::fromUtf8(freshFile.readAll());
    freshFile.close();
    QVERIFY(!freshContent.contains(QLatin1Char('#')));
    QVERIFY(freshContent.startsWith(QStringLiteral("--no-playlist")));
}

void TestConfigManager::ytdlpKeepsCustomOptions()
{
    ConfigManager config;

    // The user added their own option to the file.
    {
        QFile file(config.ytdlpConfigPath());
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        QString content = QString::fromUtf8(file.readAll());
        file.close();
        content += "--cookies-from-browser firefox\n";
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        file.write(content.toUtf8());
        file.close();
    }

    ConfigManager reloaded;
    reloaded.setYtdlpQuality("480");

    QFile file(reloaded.ytdlpConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QVERIFY(content.contains(QStringLiteral("--cookies-from-browser firefox")));
    QVERIFY(content.contains(QStringLiteral("--format=bestvideo[height<=480]+bestaudio/best[height<=480]")));
}

int main(int argc, char *argv[])
{
    const QString envRoot = initTestEnvironment();
    QCoreApplication app(argc, argv);

    TestConfigManager test;
    const int status = QTest::qExec(&test, argc, argv);

    cleanupTestEnvironment(envRoot);
    return status;
}

#include "tst_configmanager.moc"
