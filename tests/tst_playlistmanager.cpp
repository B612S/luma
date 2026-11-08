#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "ConfigManager.hpp"
#include "PlaylistManager.hpp"
#include "PlaylistModel.hpp"
#include "TestEnvironment.h"

// Playlists: create/rename/delete, adding links,
// saving to disk and reading back.
class TestPlaylistManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void defaultPlaylist();
    void createRenameDelete();
    void movePlaylistKeepsDefaultFirst();
    void emptyNameRejected();
    void addUrlAndPersist();
    void urlEntryShowsLinkInPlaylist();
    void urlTitleIsFetched();
    void playlistModesPersist();
    void onlyPlayingPlaylistIsMarked();
    void addAudioAndVideoFiles();
    void addFolderAddsContents();
    void syncsNamedPlaylistsAcrossInstances();
    void shuffleModeReordersNamedPlaylist();
    void watchesFolderForNewFiles();
    void unavailableMarkPersists();
    void unavailableMarkClearedAtRuntime();
    void staleUnavailableMarkRemovedOnLoad();

private:
    static QString playlistsDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
               + "/luma/playlists";
    }
    static QString playlistJson(const QString &id)
    {
        QFile f(playlistsDir() + "/" + id + ".json");
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll());
    }
    static QString configDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/luma";
    }
};

void TestPlaylistManager::init()
{
    QDir(configDir()).removeRecursively();
}

void TestPlaylistManager::defaultPlaylist()
{
    ConfigManager config;
    PlaylistManager manager(&config);

    QCOMPARE(manager.activeId(), PlaylistManager::DEFAULT_ID);
    QVERIFY(manager.defaultModel() != nullptr);
    QCOMPARE(manager.defaultModel()->rowCount(), 0);
    QCOMPARE(manager.playlistName(PlaylistManager::DEFAULT_ID), QStringLiteral("playlist_default"));

    // The first entry in the playlist list is Default.
    const QVariantList list = manager.playlists();
    QVERIFY(!list.isEmpty());
    QCOMPARE(list.first().toMap().value("id").toString(), PlaylistManager::DEFAULT_ID);
    QVERIFY(list.first().toMap().value("isDefault").toBool());
}

void TestPlaylistManager::createRenameDelete()
{
    ConfigManager config;
    PlaylistManager manager(&config);

    const QString id = manager.createPlaylist("Radio");
    QVERIFY(!id.isEmpty());
    QCOMPARE(manager.playlistName(id), QStringLiteral("Radio"));
    QVERIFY(manager.modelById(id) != nullptr);

    const QString filePath = playlistsDir() + "/" + id + ".json";
    QVERIFY(QFile::exists(filePath));

    manager.renamePlaylist(id, "Relax");
    QCOMPARE(manager.playlistName(id), QStringLiteral("Relax"));

    manager.deletePlaylist(id);
    QVERIFY(manager.modelById(id) == nullptr);
    QVERIFY(!QFile::exists(filePath));
}

void TestPlaylistManager::movePlaylistKeepsDefaultFirst()
{
    ConfigManager config;
    PlaylistManager manager(&config);

    const QString a = manager.createPlaylist("A");
    const QString b = manager.createPlaylist("B");
    const QString c = manager.createPlaylist("C");
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());

    auto ids = [&manager]() {
        QStringList out;
        for (const QVariant &v : manager.playlists())
            out << v.toMap().value("id").toString();
        return out;
    };

    QCOMPARE(ids(), (QStringList{PlaylistManager::DEFAULT_ID, a, b, c}));

    manager.movePlaylist(3, 1); // C after Default
    QCOMPARE(ids(), (QStringList{PlaylistManager::DEFAULT_ID, c, a, b}));

    manager.movePlaylist(0, 3); // Default cannot move
    QCOMPARE(ids(), (QStringList{PlaylistManager::DEFAULT_ID, c, a, b}));

    manager.movePlaylist(1, 0); // nothing may go before Default
    QCOMPARE(ids(), (QStringList{PlaylistManager::DEFAULT_ID, c, a, b}));

    manager.movePlaylist(2, 4); // A to the end
    QCOMPARE(ids(), (QStringList{PlaylistManager::DEFAULT_ID, c, b, a}));

    // The order is persisted.
    ConfigManager config2;
    PlaylistManager manager2(&config2);
    QStringList reloaded;
    for (const QVariant &v : manager2.playlists())
        reloaded << v.toMap().value("id").toString();
    QCOMPARE(reloaded, (QStringList{PlaylistManager::DEFAULT_ID, c, b, a}));
}

void TestPlaylistManager::emptyNameRejected()
{
    ConfigManager config;
    PlaylistManager manager(&config);

    QVERIFY(manager.createPlaylist("   ").isEmpty());
    QCOMPARE(int(manager.playlists().size()), 1); // only Default
}

void TestPlaylistManager::addUrlAndPersist()
{
    QString id;
    {
        ConfigManager config;
        PlaylistManager manager(&config);

        id = manager.createPlaylist("Streams");
        QVERIFY(!id.isEmpty());

        manager.addUrlToPlaylist(id, "http://kathy.torontocast.com:3060/");

        PlaylistModel *model = manager.modelById(id);
        QVERIFY(model != nullptr);
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->fileAt(0), QStringLiteral("http://kathy.torontocast.com:3060/"));
    }
    // A second manager reads the playlist from disk.
    ConfigManager config2;
    PlaylistManager manager2(&config2);

    PlaylistModel *model2 = manager2.modelById(id);
    QVERIFY(model2 != nullptr);
    QCOMPARE(model2->rowCount(), 1);
    QCOMPARE(model2->fileAt(0), QStringLiteral("http://kathy.torontocast.com:3060/"));

    bool found = false;
    for (const QVariant &v : manager2.playlists()) {
        if (v.toMap().value("id").toString() == id) found = true;
    }
    QVERIFY(found);
}

void TestPlaylistManager::urlEntryShowsLinkInPlaylist()
{
    // No yt-dlp in PATH: no title can arrive while the test runs, so the
    // displayed name is the fallback.
    QTemporaryDir emptyBin;
    QVERIFY(emptyBin.isValid());
    const QByteArray oldPath = qgetenv("PATH");
    struct PathRestore {
        QByteArray old;
        ~PathRestore() { qputenv("PATH", old); }
    } pathRestore{oldPath};
    qputenv("PATH", emptyBin.path().toUtf8());

    ConfigManager config;
    PlaylistManager manager(&config);

    const QString id = manager.createPlaylist("Links");
    QVERIFY(!id.isEmpty());

    // A broken YouTube link must show the whole link (including the query),
    // not a generic "youtube.com/watch" that hides what is wrong with it.
    const QString broken = QStringLiteral("https://www.youtube.com/watch?");
    manager.addUrlToPlaylist(id, broken);

    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model != nullptr);
    QCOMPARE(model->data(model->index(0), Qt::DisplayRole).toString(), broken);

    const QString radio = QStringLiteral("http://kathy.torontocast.com:3060/");
    manager.addUrlToPlaylist(id, radio);
    QCOMPARE(model->data(model->index(1), Qt::DisplayRole).toString(), radio);
}

void TestPlaylistManager::onlyPlayingPlaylistIsMarked()
{
    ConfigManager config;
    PlaylistManager manager(&config);
    const QString a = manager.createPlaylist("A");
    const QString b = manager.createPlaylist("B");
    QVERIFY(!a.isEmpty() && !b.isEmpty());

    PlaylistModel *modelA = manager.modelById(a);
    PlaylistModel *modelB = manager.modelById(b);
    QVERIFY(modelA && modelB);
    modelA->setFiles({"a.mp3"}, "a.mp3");
    modelB->setFiles({"b.mp3"}, "b.mp3");
    modelA->setCurrentIndex(0);
    modelB->setCurrentIndex(0);

    manager.setPlayingModel(modelA);
    QCOMPARE(manager.playingId(), a);
    QCOMPARE(modelA->data(modelA->index(0), Qt::UserRole + 1).toBool(), true);
    QCOMPARE(modelB->data(modelB->index(0), Qt::UserRole + 1).toBool(), false);
    QCOMPARE(modelB->data(modelB->index(0), Qt::DisplayRole).toString(), QStringLiteral("b"));

    manager.setPlayingModel(modelB);
    QCOMPARE(manager.playingId(), b);
    QCOMPARE(modelA->data(modelA->index(0), Qt::UserRole + 1).toBool(), false);
    QCOMPARE(modelB->data(modelB->index(0), Qt::UserRole + 1).toBool(), true);

    manager.setPlayingModel(nullptr);
    QVERIFY(manager.playingId().isEmpty());
}

void TestPlaylistManager::addAudioAndVideoFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString audio = dir.path() + "/Song.mp3";
    const QString video = dir.path() + "/Movie.mp4";
    for (const QString &path : {audio, video}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    ConfigManager config;
    PlaylistManager manager(&config);
    const QString id = manager.createPlaylist("Media");
    manager.addFilesToPlaylist(id, {audio, video});

    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->data(model->index(0), Qt::UserRole + 2).toString(), QStringLiteral("audio"));
    QCOMPARE(model->data(model->index(1), Qt::UserRole + 2).toString(), QStringLiteral("video"));
    QCOMPARE(model->data(model->index(0), Qt::UserRole + 6).toString(), QStringLiteral("Song"));
    QCOMPARE(model->data(model->index(1), Qt::UserRole + 6).toString(), QStringLiteral("Movie"));
}

void TestPlaylistManager::addFolderAddsContents()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("Season 1")));
    const QString top = dir.path() + "/OP.mp3";
    const QString nested = dir.path() + "/Season 1/Episode.mkv";
    for (const QString &path : {top, nested}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    ConfigManager config;
    PlaylistManager manager(&config);

    // "Add folder" scans the folder and its subfolders.
    const QString id = manager.createPlaylist("Folder");
    manager.addFolderToPlaylist(id, dir.path());
    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(model->contains(top));
    QVERIFY(model->contains(nested));

    // Drag & drop passes the folder to addFilesToPlaylist.
    const QString droppedId = manager.createPlaylist("Dropped");
    manager.addFilesToPlaylist(droppedId, {dir.path()});
    PlaylistModel *dropped = manager.modelById(droppedId);
    QVERIFY(dropped != nullptr);
    QCOMPARE(dropped->rowCount(), 2);
    QVERIFY(dropped->contains(top));
    QVERIFY(dropped->contains(nested));
}

void TestPlaylistManager::urlTitleIsFetched()
{
    QTemporaryDir binDir;
    QVERIFY(binDir.isValid());
    const QString fakeYtdlp = binDir.path() + "/yt-dlp";
    QFile script(fakeYtdlp);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("#!/bin/sh\nprintf 'Fake YouTube title\\n'\n");
    script.close();
    QVERIFY(QFile::setPermissions(fakeYtdlp,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
        | QFile::ReadGroup | QFile::ExeGroup
        | QFile::ReadOther | QFile::ExeOther));

    const QByteArray oldPath = qgetenv("PATH");
    struct PathRestore {
        QByteArray old;
        ~PathRestore() { qputenv("PATH", old); }
    } pathRestore{oldPath};
    qputenv("PATH", binDir.path().toUtf8() + ':' + oldPath);

    ConfigManager config;
    PlaylistManager manager(&config);
    const QString id = manager.createPlaylist("Links");
    QVERIFY(!id.isEmpty());
    manager.addUrlToPlaylist(id, "https://www.youtube.com/watch?v=test");

    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model != nullptr);
    QTRY_COMPARE_WITH_TIMEOUT(
        model->data(model->index(0), Qt::UserRole + 6).toString(),
        QStringLiteral("Fake YouTube title"), 5000);
}

void TestPlaylistManager::playlistModesPersist()
{
    QString id;
    {
        ConfigManager config;
        PlaylistManager manager(&config);

        id = manager.createPlaylist("Modes");
        QVERIFY(!id.isEmpty());
        QCOMPARE(manager.playlistMode(id), QStringLiteral("sequential"));
        QCOMPARE(manager.playlistMode(PlaylistManager::DEFAULT_ID),
                 QStringLiteral("sequential"));

        manager.setPlaylistMode(id, QStringLiteral("repeat"));
        QCOMPARE(manager.playlistMode(id), QStringLiteral("repeat"));

        // The Default playlist's mode is the settings value, not a per-file one.
        manager.setPlaylistMode(PlaylistManager::DEFAULT_ID, QStringLiteral("stop"));
        QCOMPARE(config.playlistMode(), QStringLiteral("stop"));
        QCOMPARE(manager.playlistMode(PlaylistManager::DEFAULT_ID), QStringLiteral("stop"));

        // Unknown modes fall back to sequential instead of being saved.
        manager.setPlaylistMode(id, QStringLiteral("bogus"));
        QCOMPARE(manager.playlistMode(id), QStringLiteral("sequential"));
        manager.setPlaylistMode(id, QStringLiteral("repeat"));
    }

    // The named playlist's mode survives a restart; the Default one comes
    // from luma.conf.
    ConfigManager config2;
    config2.setPlaylistMode(QStringLiteral("shuffle"));
    PlaylistManager manager2(&config2);
    QCOMPARE(manager2.playlistMode(id), QStringLiteral("repeat"));
    QCOMPARE(manager2.playlistMode(PlaylistManager::DEFAULT_ID), QStringLiteral("shuffle"));
}

// The "unavailable" mark is saved in named playlists, so a deleted file stays
// red after a restart until it comes back.
void TestPlaylistManager::syncsNamedPlaylistsAcrossInstances()
{
    QTemporaryDir mediaDir;
    QVERIFY(mediaDir.isValid());
    QStringList files;
    for (int i = 0; i < 3; ++i) {
        const QString path = mediaDir.path() + QStringLiteral("/Track%1.mp4").arg(i);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        files << path;
    }

    ConfigManager configA;
    configA.setSingleInstance(false);
    ConfigManager configB;
    configB.setSingleInstance(false);

    PlaylistManager a(&configA);
    PlaylistManager b(&configB);

    const QString id = a.createPlaylist(QStringLiteral("Shared"));
    QVERIFY(!id.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(b.modelById(id) != nullptr, 5000);

    a.addFilesToPlaylist(id, files);
    QTRY_VERIFY_WITH_TIMEOUT(b.modelById(id) && b.modelById(id)->rowCount() == 3, 5000);

    a.renamePlaylist(id, QStringLiteral("Renamed"));
    QTRY_COMPARE_WITH_TIMEOUT(b.playlistName(id), QStringLiteral("Renamed"), 5000);

    a.setPlaylistMode(id, QStringLiteral("repeat"));
    QTRY_COMPARE_WITH_TIMEOUT(b.playlistMode(id), QStringLiteral("repeat"), 5000);

    // Watch folders are part of the playlist file and follow the same sync.
    QTemporaryDir watchDir;
    QVERIFY(watchDir.isValid());
    QVERIFY(a.addWatchFolder(id, watchDir.path()));
    const QString canonicalWatchDir = QFileInfo(watchDir.path()).canonicalFilePath();
    QTRY_VERIFY_WITH_TIMEOUT(b.watchFolders(id).contains(canonicalWatchDir), 5000);

    PlaylistModel *aModel = a.modelById(id);
    QVERIFY(aModel);
    aModel->moveItem(0, 2);
    QTRY_VERIFY_WITH_TIMEOUT(
        b.modelById(id)
            && b.modelById(id)->data(b.modelById(id)->index(2), Qt::UserRole).toString() == files.first(),
        5000);

    const QString second = a.createPlaylist(QStringLiteral("Second"));
    QVERIFY(!second.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(b.modelById(second) != nullptr, 5000);

    a.movePlaylist(2, 1);
    auto playlistIds = [](const QVariantList &list) {
        QStringList ids;
        for (const QVariant &entry : list)
            ids << entry.toMap().value(QStringLiteral("id")).toString();
        return ids;
    };
    QTRY_COMPARE_WITH_TIMEOUT(playlistIds(b.playlists()),
                              (QStringList{PlaylistManager::DEFAULT_ID, second, id}), 5000);

    a.deletePlaylist(second);
    QTRY_VERIFY_WITH_TIMEOUT(b.modelById(second) == nullptr, 6000);
    QVERIFY(!playlistIds(b.playlists()).contains(second));

    // Deleting the active playlist in one window switches the other window
    // back to its own Default playlist.
    b.setActiveId(id);
    QCOMPARE(b.activeId(), id);
    a.deletePlaylist(id);
    QTRY_COMPARE_WITH_TIMEOUT(b.activeId(), PlaylistManager::DEFAULT_ID, 6000);
    QTRY_VERIFY_WITH_TIMEOUT(b.modelById(id) == nullptr, 6000);
}

void TestPlaylistManager::shuffleModeReordersNamedPlaylist()
{
    ConfigManager config;
    PlaylistManager manager(&config);
    const QString id = manager.createPlaylist(QStringLiteral("Shuffle"));
    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model);

    // setFiles(..., current) records the playing file; shuffle keeps it first
    // and randomises the rest. Before shuffle it is at index 2.
    model->setFiles({"a.mp4", "b.mp4", "c.mp4", "d.mp4"}, "c.mp4");
    QCOMPARE(model->currentIndex(), 2);
    manager.setPlaylistMode(id, QStringLiteral("shuffle"));
    QCOMPARE(manager.playlistMode(id), QStringLiteral("shuffle"));
    QCOMPARE(model->currentIndex(), 0);
    QCOMPARE(model->fileAt(0), QStringLiteral("c.mp4"));
}

void TestPlaylistManager::watchesFolderForNewFiles()
{
    QTemporaryDir mediaDir;
    QVERIFY(mediaDir.isValid());
    QVERIFY(QDir(mediaDir.path()).mkpath(QStringLiteral("Season 1")));
    const QString existing = mediaDir.path() + QStringLiteral("/Existing.mp4");
    const QString nested = mediaDir.path() + QStringLiteral("/Season 1/Nested.mp4");
    for (const QString &path : {existing, nested}) {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

    ConfigManager config;
    PlaylistManager manager(&config);
    const QString id = manager.createPlaylist(QStringLiteral("Watched"));
    QVERIFY(manager.addWatchFolder(id, mediaDir.path()));
    QVERIFY(manager.watchFolders(id).contains(mediaDir.path())
            || manager.watchFolders(id).contains(QFileInfo(mediaDir.path()).canonicalFilePath()));

    // The initial background scan imports the existing tree.
    QTRY_VERIFY_WITH_TIMEOUT(manager.modelById(id)->contains(existing), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(manager.modelById(id)->contains(nested), 5000);
    QVERIFY(playlistJson(id).contains(QStringLiteral("watchFolders")));

    // A new file in the root is picked up without polling.
    const QString added = mediaDir.path() + QStringLiteral("/New.mkv");
    {
        QFile f(added);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QTRY_VERIFY_WITH_TIMEOUT(manager.modelById(id)->contains(added), 5000);

    // A new file in a subfolder is picked up once the scan has registered the
    // subfolder with the filesystem watcher.
    QTest::qWait(800);
    const QString nestedAdded = mediaDir.path() + QStringLiteral("/Season 1/New2.mkv");
    {
        QFile f(nestedAdded);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QTRY_VERIFY_WITH_TIMEOUT(manager.modelById(id)->contains(nestedAdded), 5000);

    // The folders are saved in the playlist and restored by another instance.
    PlaylistManager reloaded(&config);
    QVERIFY(reloaded.watchFolders(id).contains(manager.watchFolders(id).first()));
}

void TestPlaylistManager::unavailableMarkPersists()
{
    ConfigManager config;
    PlaylistManager manager(&config);
    const QString id = manager.createPlaylist("Missing");
    QVERIFY(!id.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/deleted.mkv";
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    manager.addFilesToPlaylist(id, {path});
    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model);
    QCOMPARE(model->rowCount(), 1);
    QVERIFY(QFile::remove(path));

    // What playIndex() does for a local file that no longer exists.
    model->setUnavailable(path, true);
    QVERIFY(model->isUnavailable(path));
    manager.setActiveId(id);
    manager.saveActive();
    // Duration is metadata, not playlist data: it must not be written.
    QVERIFY(!playlistJson(id).contains("\"duration\""));
    // The fixture is a video file: empty artist/album/year are omitted too.
    QVERIFY(!playlistJson(id).contains("\"artist\""));
    QVERIFY(!playlistJson(id).contains("\"album\""));
    QVERIFY(!playlistJson(id).contains("\"year\""));

    PlaylistManager reloaded(&config);
    PlaylistModel *restored = reloaded.modelById(id);
    QVERIFY(restored);
    QCOMPARE(restored->rowCount(), 1);
    QCOMPARE(restored->data(restored->index(0), Qt::UserRole + 7).toBool(), true);
    QVERIFY(restored->isUnavailable(path));
}

// The file comes back while Luma is running: the watcher clears the mark and
// the autosave removes "unavailable" from the playlist file.
void TestPlaylistManager::unavailableMarkClearedAtRuntime()
{
    ConfigManager config;
    PlaylistManager manager(&config);
    const QString id = manager.createPlaylist("Runtime");
    QVERIFY(!id.isEmpty());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/deleted.mkv";
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    manager.addFilesToPlaylist(id, {path});
    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model);
    QVERIFY(QFile::remove(path));
    model->setUnavailable(path, true);
    manager.setActiveId(id);
    manager.saveActive();
    QVERIFY(playlistJson(id).contains("\"unavailable\""));

    QFile restored(path);
    QVERIFY(restored.open(QIODevice::WriteOnly));
    restored.close();

    QTRY_VERIFY_WITH_TIMEOUT(!model->isUnavailable(path), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!playlistJson(id).contains("\"unavailable\""), 3000);
}

// The file came back while Luma was closed: loading the playlist drops the
// stale mark and rewrites the file immediately.
void TestPlaylistManager::staleUnavailableMarkRemovedOnLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + "/deleted.mkv";

    QString id;
    {
        ConfigManager config;
        PlaylistManager manager(&config);
        id = manager.createPlaylist("Stale");
        QVERIFY(!id.isEmpty());

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        manager.addFilesToPlaylist(id, {path});
        PlaylistModel *model = manager.modelById(id);
        QVERIFY(model);
        QVERIFY(QFile::remove(path));
        model->setUnavailable(path, true);
        manager.setActiveId(id);
        manager.saveActive();
        QVERIFY(playlistJson(id).contains("\"unavailable\""));
    }

    // Restored while the player is not running.
    QFile restored(path);
    QVERIFY(restored.open(QIODevice::WriteOnly));
    restored.close();

    ConfigManager config2;
    PlaylistManager manager2(&config2);
    PlaylistModel *model2 = manager2.modelById(id);
    QVERIFY(model2);
    QCOMPARE(model2->rowCount(), 1);
    QCOMPARE(model2->data(model2->index(0), Qt::UserRole + 7).toBool(), false);
    QVERIFY(!playlistJson(id).contains("\"unavailable\""));
}

int main(int argc, char *argv[])
{
    const QString envRoot = initTestEnvironment();
    QCoreApplication app(argc, argv);

    TestPlaylistManager test;
    const int status = QTest::qExec(&test, argc, argv);

    cleanupTestEnvironment(envRoot);
    return status;
}

#include "tst_playlistmanager.moc"
