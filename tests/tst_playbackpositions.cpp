#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QtTest>
#include <QTemporaryDir>

#include "media/PlaybackPositions.hpp"
#include "TestEnvironment.h"

class TestPlaybackPositions : public QObject
{
    Q_OBJECT

private slots:
    void storesAndRestores();
    void ignoresShortAndFinished();
    void keepsCacheBounded();
    void dropsOldEntries();
    void ignoresCorruptFile();
    void serializedCacheStaysSmall();
};

void TestPlaybackPositions::storesAndRestores()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    PlaybackPositions positions;
    QCOMPARE(positions.count(), 0);
    QCOMPARE(positions.positionFor(QStringLiteral("/video.mkv")), 0.0);

    QVERIFY(positions.setPosition(QStringLiteral("/video.mkv"), 123.5, 3600.0, now));
    QCOMPARE(positions.count(), 1);
    QCOMPARE(positions.positionFor(QStringLiteral("/video.mkv")), 123.5);

    PlaybackPositions loaded;
    QVERIFY(loaded.loadFromData(positions.serialize()));
    QCOMPARE(loaded.positionFor(QStringLiteral("/video.mkv")), 123.5);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/positions.json";
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(positions.serialize());
    }
    PlaybackPositions fromFile;
    QVERIFY(fromFile.load(path));
    QCOMPARE(fromFile.positionFor(QStringLiteral("/video.mkv")), 123.5);
}

void TestPlaybackPositions::ignoresShortAndFinished()
{
    PlaybackPositions positions;
    // Too close to the beginning.
    QVERIFY(!positions.setPosition(QStringLiteral("/a"), 3.0, 100.0, 1000));
    QCOMPARE(positions.count(), 0);

    // Too close to the end.
    QVERIFY(!positions.setPosition(QStringLiteral("/a"), 95.0, 100.0, 1000));
    QCOMPARE(positions.count(), 0);

    QVERIFY(positions.setPosition(QStringLiteral("/a"), 20.0, 100.0, 1000));
    QCOMPARE(positions.count(), 1);
    QCOMPARE(positions.positionFor(QStringLiteral("/a")), 20.0);

    // Watching it to the end removes the stale entry.
    QVERIFY(positions.setPosition(QStringLiteral("/a"), 98.0, 100.0, 1001));
    QCOMPARE(positions.count(), 0);
    QCOMPARE(positions.positionFor(QStringLiteral("/a")), 0.0);
}

void TestPlaybackPositions::keepsCacheBounded()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    PlaybackPositions positions(3);
    for (int i = 0; i < 5; ++i) {
        QVERIFY(positions.setPosition(QStringLiteral("/v%1").arg(i), 30.0, 100.0,
                                      now + i));
    }
    QCOMPARE(positions.count(), 3);
    // The oldest entries are evicted, the newest are kept.
    QCOMPARE(positions.positionFor(QStringLiteral("/v0")), 0.0);
    QCOMPARE(positions.positionFor(QStringLiteral("/v1")), 0.0);
    QCOMPARE(positions.positionFor(QStringLiteral("/v2")), 30.0);
    QCOMPARE(positions.positionFor(QStringLiteral("/v4")), 30.0);

    // Loading a bigger file also prunes down to the limit.
    PlaybackPositions loaded(2);
    QVERIFY(loaded.loadFromData(positions.serialize()));
    QCOMPARE(loaded.count(), 2);
}

void TestPlaybackPositions::dropsOldEntries()
{
    PlaybackPositions positions;
    const qint64 old = 1000;
    positions.setPosition(QStringLiteral("/old"), 30.0, 100.0, old);
    positions.prune(old + 91LL * 24 * 60 * 60);
    QCOMPARE(positions.count(), 0);
}

void TestPlaybackPositions::ignoresCorruptFile()
{
    PlaybackPositions positions;
    QVERIFY(!positions.loadFromData("not json"));
    QCOMPARE(positions.count(), 0);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/missing.json";
    QVERIFY(positions.load(path));
    QCOMPARE(positions.count(), 0);
}

void TestPlaybackPositions::serializedCacheStaysSmall()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    PlaybackPositions positions;
    for (int i = 0; i < 500; ++i) {
        positions.setPosition(QStringLiteral("https://example.com/watch?v=%1").arg(i),
                              120.0, 3600.0, now + i);
    }
    QCOMPARE(positions.count(), 500);
    const QByteArray data = positions.serialize();
    // 500 entries stay in the tens of kilobytes, never megabytes.
    QVERIFY2(data.size() < 100 * 1024, qPrintable(QString::number(data.size())));
}

int main(int argc, char *argv[])
{
    const QString envRoot = initTestEnvironment();
    QCoreApplication app(argc, argv);
    TestPlaybackPositions test;
    const int status = QTest::qExec(&test, argc, argv);
    cleanupTestEnvironment(envRoot);
    return status;
}

#include "tst_playbackpositions.moc"
