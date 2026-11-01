#include <QTest>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include "ConfigManager.hpp"
#include "TestEnvironment.h"

// Настройки: значения по умолчанию, миграция старого ключа, режим панели,
// параметры yt-dlp (файл ~/.config/luma/yt-dlp.conf).
class TestConfigManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void defaults();
    void controlBarModePersists();
    void legacyPinControlsMigrates();
    void unsupportedLanguageFallsBack();
    void ytdlpQualityAndProxy();
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
    // Каждый тест начинается с чистого конфига.
    QDir(lumaDir()).removeRecursively();
}

void TestConfigManager::defaults()
{
    ConfigManager config;

    QVERIFY(config.rememberVolume());
    QCOMPARE(config.defaultVolume(), 80);
    QCOMPARE(config.volume(), 80);
    QCOMPARE(config.language(), QStringLiteral("en"));
    QCOMPARE(config.playlistMode(), QStringLiteral("sequential"));
    QVERIFY(config.notifications());
    QVERIFY(config.showPanelsFullscreen());
    QCOMPARE(config.controlBarMode(), QStringLiteral("float"));
    QCOMPARE(config.ytdlpQuality(), QStringLiteral("best"));
    QVERIFY(config.ytdlpProxy().isEmpty());
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

    // Неизвестное значение нормализуется в "float".
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
    config.setLanguage("ja");
    QCOMPARE(config.language(), QStringLiteral("en"));

    config.setLanguage("ru");
    QCOMPARE(config.language(), QStringLiteral("ru"));
}

void TestConfigManager::ytdlpQualityAndProxy()
{
    ConfigManager config;
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("best"));

    config.setYtdlpQuality("720");
    QCOMPARE(config.ytdlpQuality(), QStringLiteral("720"));
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("best[height<=720]"));

    config.setYtdlpQuality("audio");
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("bestaudio[ext=m4a]/bestaudio"));

    config.setYtdlpQuality("1080");
    QCOMPARE(config.ytdlpFormatExpression(), QStringLiteral("best[height<=1080]"));

    config.setYtdlpProxy("socks5://127.0.0.1:1080");
    QCOMPARE(config.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));

    // Файл действительно переписан.
    QFile file(config.ytdlpConfigPath());
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains(QStringLiteral("--format=best[height<=1080]")));
    QVERIFY(content.contains(QStringLiteral("--proxy=socks5://127.0.0.1:1080")));
    QVERIFY(content.contains(QStringLiteral("--no-playlist")));

    // И читается обратно новым экземпляром.
    ConfigManager config2;
    QCOMPARE(config2.ytdlpQuality(), QStringLiteral("1080"));
    QCOMPARE(config2.ytdlpProxy(), QStringLiteral("socks5://127.0.0.1:1080"));
}

void TestConfigManager::ytdlpKeepsCustomOptions()
{
    ConfigManager config;

    // Пользователь дописал свою опцию в файл.
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
    QVERIFY(content.contains(QStringLiteral("--format=best[height<=480]")));
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
