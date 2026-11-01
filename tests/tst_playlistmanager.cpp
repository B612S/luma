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
    void urlEntryShowsHostInPlaylist();
    void urlTitleIsFetched();
    void onlyPlayingPlaylistIsMarked();
    void addAudioAndVideoFiles();

private:
    static QString playlistsDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
               + "/luma/playlists";
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

void TestPlaylistManager::urlEntryShowsHostInPlaylist()
{
    ConfigManager config;
    PlaylistManager manager(&config);

    const QString id = manager.createPlaylist("Radio");
    QVERIFY(!id.isEmpty());
    manager.addUrlToPlaylist(id, "http://kathy.torontocast.com:3060/");

    PlaylistModel *model = manager.modelById(id);
    QVERIFY(model != nullptr);
    QVERIFY(model->data(model->index(0), Qt::DisplayRole).toString()
                .contains(QStringLiteral("kathy.torontocast.com")));
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
    QCOMPARE(modelA->data(modelA->index(0), Qt::UserRole + 1).toBool(), true);
    QCOMPARE(modelB->data(modelB->index(0), Qt::UserRole + 1).toBool(), false);
    QCOMPARE(modelB->data(modelB->index(0), Qt::DisplayRole).toString(), QStringLiteral("b"));

    manager.setPlayingModel(modelB);
    QCOMPARE(modelA->data(modelA->index(0), Qt::UserRole + 1).toBool(), false);
    QCOMPARE(modelB->data(modelB->index(0), Qt::UserRole + 1).toBool(), true);
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
