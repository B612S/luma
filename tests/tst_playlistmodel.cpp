#include <QTest>
#include <QVariantMap>

#include "PlaylistModel.hpp"
#include "TestEnvironment.h"

// Логика плейлиста без GUI: загрузка, порядок, текущий трек, живые теги потоков.
class TestPlaylistModel : public QObject
{
    Q_OBJECT

private slots:
    void setFilesAndCurrent();
    void displayAddsCurrentMarker();
    void moveItemKeepsCurrent();
    void removeItemAdjustsCurrent();
    void clearRemovesEverything();
    void appendEntryRejectsDuplicates();
    void urlEntryShowsHost();
    void streamTitleReplacesUrl();
    void streamTitleIsNotSaved();
    void shuffleKeepsCurrentFirst();
};

void TestPlaylistModel::setFilesAndCurrent()
{
    PlaylistModel model;
    model.setFiles({"a.mp4", "b.mp4", "c.mp4"}, "b.mp4");

    QCOMPARE(model.count(), 3);
    QCOMPARE(model.indexOf("b.mp4"), 1);
    QCOMPARE(model.currentIndex(), 1);
    QCOMPARE(model.currentFile(), QStringLiteral("b.mp4"));
    QCOMPARE(model.fileAt(2), QStringLiteral("c.mp4"));
    QCOMPARE(model.fileAt(5), QString());
    QVERIFY(model.contains("c.mp4"));
    QVERIFY(!model.contains("z.mp4"));
}

void TestPlaylistModel::displayAddsCurrentMarker()
{
    PlaylistModel model;
    model.setFiles({"a.mp4", "b.mp4"}, "a.mp4");

    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("▶ a.mp4"));
    QCOMPARE(model.data(model.index(1), Qt::DisplayRole).toString(), QStringLiteral("b.mp4"));
}

void TestPlaylistModel::moveItemKeepsCurrent()
{
    PlaylistModel model;
    model.setFiles({"a.mp4", "b.mp4", "c.mp4"}, "a.mp4");

    model.moveItem(0, 2);

    QCOMPARE(model.fileAt(0), QStringLiteral("b.mp4"));
    QCOMPARE(model.fileAt(1), QStringLiteral("c.mp4"));
    QCOMPARE(model.fileAt(2), QStringLiteral("a.mp4"));
    QCOMPARE(model.currentIndex(), 2);
    QCOMPARE(model.currentFile(), QStringLiteral("a.mp4"));
}

void TestPlaylistModel::removeItemAdjustsCurrent()
{
    PlaylistModel model;
    model.setFiles({"a.mp4", "b.mp4", "c.mp4"}, "b.mp4");

    model.removeItem(0);

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.currentFile(), QStringLiteral("b.mp4"));
}

void TestPlaylistModel::clearRemovesEverything()
{
    PlaylistModel model;
    model.setFiles({"a.mp4"}, "a.mp4");

    model.clear();

    QCOMPARE(model.count(), 0);
    QCOMPARE(model.currentIndex(), -1);
    QVERIFY(model.currentFile().isEmpty());
}

void TestPlaylistModel::appendEntryRejectsDuplicates()
{
    PlaylistModel model;
    QVariantMap entry;
    entry["path"] = "/tmp/track.mp3";
    entry["kind"] = "audio";
    entry["title"] = "Track";

    model.appendEntry(entry);
    model.appendEntry(entry);

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("Track"));
}

void TestPlaylistModel::urlEntryShowsHost()
{
    PlaylistModel model;
    QVariantMap entry;
    entry["path"] = "http://kathy.torontocast.com:3060/";

    model.appendEntry(entry);

    QCOMPARE(model.count(), 1);
    const QString display = model.data(model.index(0), Qt::DisplayRole).toString();
    QVERIFY(display.contains(QStringLiteral("kathy.torontocast.com")));
    QVERIFY(!display.contains(QStringLiteral("http://")));
}

void TestPlaylistModel::streamTitleReplacesUrl()
{
    PlaylistModel model;
    const QString url = QStringLiteral("http://kathy.torontocast.com:3060/");
    QVariantMap entry;
    entry["path"] = url;
    model.appendEntry(entry);

    model.setStreamTitle(url, QStringLiteral("Artist - Track"));

    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("Artist - Track"));
}

void TestPlaylistModel::streamTitleIsNotSaved()
{
    PlaylistModel model;
    const QString url = QStringLiteral("http://kathy.torontocast.com:3060/");
    QVariantMap entry;
    entry["path"] = url;
    model.appendEntry(entry);
    model.setStreamTitle(url, QStringLiteral("Live Tag"));

    const QVariantList saved = model.toVariantList();
    QCOMPARE(int(saved.size()), 1);
    QVERIFY(!saved.first().toMap().contains(QStringLiteral("title")));
}

void TestPlaylistModel::shuffleKeepsCurrentFirst()
{
    PlaylistModel model;
    model.setFiles({"a.mp4", "b.mp4", "c.mp4", "d.mp4"}, "c.mp4");

    model.setShuffle(true);
    QCOMPARE(model.count(), 4);
    QCOMPARE(model.currentIndex(), 0);
    QCOMPARE(model.currentFile(), QStringLiteral("c.mp4"));

    model.setShuffle(false);
    QCOMPARE(model.fileAt(0), QStringLiteral("a.mp4"));
    QCOMPARE(model.fileAt(3), QStringLiteral("d.mp4"));
    QCOMPARE(model.currentFile(), QStringLiteral("c.mp4"));
}

int main(int argc, char *argv[])
{
    const QString envRoot = initTestEnvironment();
    QCoreApplication app(argc, argv);

    TestPlaylistModel test;
    const int status = QTest::qExec(&test, argc, argv);

    cleanupTestEnvironment(envRoot);
    return status;
}

#include "tst_playlistmodel.moc"
