#include <QTest>
#include <QSignalSpy>
#include <QVariantMap>

#include "PlaylistModel.hpp"
#include "TestEnvironment.h"

// Playlist logic without a GUI: loading, order, current track, live stream tags.
class TestPlaylistModel : public QObject
{
    Q_OBJECT

private slots:
    void setFilesAndCurrent();
    void displayAddsCurrentMarker();
    void displayStripsExtension();
    void updateMetadataChangesFields();
    void sortByMetadata();
    void playingMarkerIsControllable();
    void moveItemKeepsCurrent();
    void removeItemAdjustsCurrent();
    void clearRemovesEverything();
    void appendEntryRejectsDuplicates();
    void removeAllowsReappend();
    void urlEntryShowsLink();
    void streamTitleReplacesUrl();
    void streamTitleIsNotSaved();
    void videoTitlePersists();
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

    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("▶ a"));
    QCOMPARE(model.data(model.index(1), Qt::DisplayRole).toString(), QStringLiteral("b"));
}

void TestPlaylistModel::displayStripsExtension()
{
    PlaylistModel model;
    model.setFiles({"movie.mkv"}, "movie.mkv");
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("▶ movie"));

    // Playlists saved by older versions could still contain "clip.mp4" as title.
    QVariantMap entry;
    entry["path"] = QStringLiteral("/tmp/clip.mp4");
    entry["title"] = QStringLiteral("clip.mp4");
    model.setEntries(QVariantList() << entry, QStringLiteral("/tmp/clip.mp4"));
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("▶ clip"));
}

void TestPlaylistModel::playingMarkerIsControllable()
{
    PlaylistModel model;
    model.setFiles({"a.mp3"}, "a.mp3");
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 1).toBool(), true);
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("▶ a"));

    model.setPlaying(false);
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 1).toBool(), false);
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QStringLiteral("a"));

    model.setPlaying(true);
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 1).toBool(), true);
}

void TestPlaylistModel::sortByMetadata()
{
    QVariantList entries;
    auto add = [&entries](const QString &path, const QString &title, const QString &artist,
                           const QString &album, int year) {
        QVariantMap e;
        e["path"] = path;
        e["kind"] = "audio";
        e["title"] = title;
        e["artist"] = artist;
        e["album"] = album;
        e["year"] = year;
        entries << e;
    };
    add("b.mp3", "Beta", "Zed", "B", 2020);
    add("a.mp3", "Alpha", "Ann", "A", 2019);
    add("c.mp3", "Gamma", "Ann", "C", 2021);

    PlaylistModel model;
    model.setEntries(entries, "");

    model.sortBy("name", true);
    QCOMPARE(model.fileAt(0), QStringLiteral("a.mp3"));
    QCOMPARE(model.fileAt(1), QStringLiteral("b.mp3"));
    QCOMPARE(model.fileAt(2), QStringLiteral("c.mp3"));

    model.sortBy("name", false);
    QCOMPARE(model.fileAt(0), QStringLiteral("c.mp3"));
    QCOMPARE(model.fileAt(2), QStringLiteral("a.mp3"));

    model.sortBy("artist", true);
    QCOMPARE(model.fileAt(0), QStringLiteral("a.mp3"));
    QCOMPARE(model.fileAt(1), QStringLiteral("c.mp3"));
    QCOMPARE(model.fileAt(2), QStringLiteral("b.mp3"));

    model.sortBy("year", true);
    QCOMPARE(model.fileAt(0), QStringLiteral("a.mp3"));
    QCOMPARE(model.fileAt(1), QStringLiteral("b.mp3"));
    QCOMPARE(model.fileAt(2), QStringLiteral("c.mp3"));

    model.sortBy("year", false);
    QCOMPARE(model.fileAt(0), QStringLiteral("c.mp3"));
    QCOMPARE(model.fileAt(2), QStringLiteral("a.mp3"));

    model.sortBy("album", true);
    QCOMPARE(model.fileAt(0), QStringLiteral("a.mp3"));
    QCOMPARE(model.fileAt(1), QStringLiteral("b.mp3"));
    QCOMPARE(model.fileAt(2), QStringLiteral("c.mp3"));
}

void TestPlaylistModel::updateMetadataChangesFields()
{
    PlaylistModel model;
    model.setFiles({"song.mp3"}, "song.mp3");
    QSignalSpy modified(&model, &PlaylistModel::modified);

    QVERIFY(model.updateMetadata("song.mp3", "Song title", "Artist", "Album", 2024, 123.0));
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 6).toString(), QStringLiteral("Song title"));
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 3).toString(), QStringLiteral("Artist"));
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 4).toString(), QStringLiteral("Album"));
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 5).toDouble(), 123.0);
    QCOMPARE(modified.count(), 1);

    QVERIFY(!model.updateMetadata("song.mp3", "Song title", "Artist", "Album", 2024, 123.0));
    QCOMPARE(modified.count(), 1);

    // Removed tags fall back to the file name.
    QVERIFY(model.updateMetadata("song.mp3", "", "", "", 0, 0.0));
    QCOMPARE(model.data(model.index(0), Qt::UserRole + 6).toString(), QStringLiteral("song"));
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

void TestPlaylistModel::removeAllowsReappend()
{
    PlaylistModel model;
    QVariantMap entry;
    entry["path"] = "/tmp/track.mp3";
    entry["kind"] = "audio";

    model.appendEntry(entry);
    model.removeItem(0);
    QVERIFY(!model.contains("/tmp/track.mp3"));

    // The fast duplicate index must be updated on removal too.
    model.appendEntry(entry);
    QCOMPARE(model.count(), 1);
    QVERIFY(model.contains("/tmp/track.mp3"));
}

void TestPlaylistModel::urlEntryShowsLink()
{
    // Until a title is fetched, the entry shows the actual link — including
    // the query. Otherwise a broken YouTube URL looks like a valid
    // "youtube.com/watch" and the mistake is invisible.
    PlaylistModel model;
    QVariantMap entry;
    const QString url = QStringLiteral("https://www.youtube.com/watch?v=abc");
    entry["path"] = url;

    model.appendEntry(entry);

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), url);

    QVariantMap radio;
    radio["path"] = QStringLiteral("http://kathy.torontocast.com:3060/");
    model.appendEntry(radio);
    QCOMPARE(model.data(model.index(1), Qt::DisplayRole).toString(),
             QStringLiteral("http://kathy.torontocast.com:3060/"));
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

void TestPlaylistModel::videoTitlePersists()
{
    PlaylistModel model;
    const QString url = QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    model.setFiles({url}, url);
    model.setVideoTitle(url, QStringLiteral("Rick Astley - Never Gonna Give You Up"));

    QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(),
             QStringLiteral("▶ Rick Astley - Never Gonna Give You Up"));

    const QVariantList saved = model.toVariantList();
    QCOMPARE(int(saved.size()), 1);
    QCOMPARE(saved.first().toMap().value(QStringLiteral("title")).toString(),
             QStringLiteral("Rick Astley - Never Gonna Give You Up"));

    PlaylistModel loaded;
    loaded.setEntries(saved, url);
    QCOMPARE(loaded.data(loaded.index(0), Qt::DisplayRole).toString(),
             QStringLiteral("▶ Rick Astley - Never Gonna Give You Up"));
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
