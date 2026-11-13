#include <QTest>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "app/Logger.hpp"
#include "TestEnvironment.h"

// Luma's own log level filtering and the per-component (mpv, yt-dlp) files.
class TestLogger : public QObject
{
    Q_OBJECT

private slots:
    void lumaLevels();
    void componentFiles();
};

void TestLogger::lumaLevels()
{
    Logger &logger = Logger::instance();
    const QString path = QStandardPaths::writableLocation(
        QStandardPaths::GenericCacheLocation) + "/luma/luma.log";

    logger.setMode("off");
    logger.info(QStringLiteral("hidden-off-info"));
    logger.critical(QStringLiteral("hidden-off-critical"));

    logger.setMode("error");
    logger.info(QStringLiteral("hidden-error-info"));
    logger.warning(QStringLiteral("visible-error-warning"));

    logger.setMode("debug");
    logger.debug(QStringLiteral("visible-debug"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(file.readAll());
    QVERIFY(!content.contains(QStringLiteral("hidden-off-info")));
    QVERIFY(!content.contains(QStringLiteral("hidden-off-critical")));
    QVERIFY(!content.contains(QStringLiteral("hidden-error-info")));
    QVERIFY(content.contains(QStringLiteral("visible-error-warning")));
    QVERIFY(content.contains(QStringLiteral("visible-debug")));
}

void TestLogger::componentFiles()
{
    Logger &logger = Logger::instance();
    const QString path = logger.componentPath(QStringLiteral("mpv"));
    QFile::remove(path);

    logger.setComponentMode(QStringLiteral("mpv"), QStringLiteral("info"));
    QCOMPARE(logger.componentMode(QStringLiteral("mpv")), QStringLiteral("info"));
    logger.writeComponent(QStringLiteral("mpv"), QStringLiteral("mpv-test-line"));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(file.readAll()).contains(QStringLiteral("mpv-test-line")));
    file.close();

    logger.setComponentMode(QStringLiteral("mpv"), QStringLiteral("off"));
    QCOMPARE(logger.componentMode(QStringLiteral("mpv")), QStringLiteral("off"));
    const qint64 sizeBefore = QFileInfo(path).size();
    logger.writeComponent(QStringLiteral("mpv"), QStringLiteral("mpv-hidden-line"));
    QCOMPARE(QFileInfo(path).size(), sizeBefore);
}

int main(int argc, char *argv[])
{
    const QString envRoot = initTestEnvironment();
    QCoreApplication app(argc, argv);

    TestLogger test;
    const int status = QTest::qExec(&test, argc, argv);

    cleanupTestEnvironment(envRoot);
    return status;
}

#include "tst_logger.moc"
