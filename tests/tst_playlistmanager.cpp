#include <QTest>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "ConfigManager.hpp"
#include "PlaylistManager.hpp"
#include "PlaylistModel.hpp"
#include "TestEnvironment.h"

// Плейлисты: создание/переименование/удаление, добавление ссылок,
// сохранение на диск и чтение обратно.
class TestPlaylistManager : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void defaultPlaylist();
    void createRenameDelete();
    void emptyNameRejected();
    void addUrlAndPersist();
    void urlEntryShowsHostInPlaylist();

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

    // В списке плейлистов первый — Default.
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

    manager.renamePlaylist(id, "Отдых");
    QCOMPARE(manager.playlistName(id), QStringLiteral("Отдых"));

    manager.deletePlaylist(id);
    QVERIFY(manager.modelById(id) == nullptr);
    QVERIFY(!QFile::exists(filePath));
}

void TestPlaylistManager::emptyNameRejected()
{
    ConfigManager config;
    PlaylistManager manager(&config);

    QVERIFY(manager.createPlaylist("   ").isEmpty());
    QCOMPARE(int(manager.playlists().size()), 1); // только Default
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
    // Второй менеджер читает плейлист с диска.
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
