#include "PlaylistManager.hpp"
#include "MediaPaths.hpp"
#include "ConfigManager.hpp"
#include "Logger.hpp"
#include "TagScanner.hpp"
#include "RadioNameResolver.hpp"
#include "PlaylistSaver.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDirIterator>
#include <QFileSystemWatcher>
#include <QUuid>
#include <QDebug>
#include <QProcess>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

const QString PlaylistManager::DEFAULT_ID = "__default__";

static bool isPlaylistMode(const QString &mode)
{
    return mode == QLatin1String("sequential") || mode == QLatin1String("repeat")
           || mode == QLatin1String("shuffle") || mode == QLatin1String("stop");
}

static QString normalizedPlaylistMode(const QString &mode)
{
    return isPlaylistMode(mode) ? mode : QStringLiteral("sequential");
}

static bool isMediaFile(const QString &path)
{
    static const QStringList allowed = {
        "mp4","mkv","avi","webm","mov","flv",
        "mp3","flac","aac","wav","ogg","m4a","opus","wma"
    };
    return allowed.contains(QFileInfo(path).suffix().toLower());
}

struct FolderScanResult {
    QStringList files;
    QStringList dirs;
};

// Recursively lists media files and directories under the given roots. Runs
// off the GUI thread for the folder watcher; also used synchronously by
// "Add folder" and by dropped folders.
static FolderScanResult scanFolderTree(const QStringList &roots)
{
    QSet<QString> files;
    QSet<QString> dirs;

    for (const QString &root : roots) {
        const QFileInfo rootInfo(root);
        if (!rootInfo.isDir())
            continue;
        const QString rootPath = rootInfo.canonicalFilePath().isEmpty()
            ? rootInfo.absoluteFilePath() : rootInfo.canonicalFilePath();
        dirs.insert(rootPath);

        QDirIterator it(rootPath,
                        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (it.fileInfo().isDir())
                dirs.insert(path);
            else if (isMediaFile(path))
                files.insert(path);
        }
    }

    FolderScanResult result;
    result.files = files.values();
    result.files.sort(Qt::CaseInsensitive);
    result.dirs = dirs.values();
    result.dirs.sort(Qt::CaseInsensitive);
    return result;
}

static QStringList mediaFilesInFolder(const QString &folder)
{
    return scanFolderTree({folder}).files;
}

static QStringList foldersFromJson(const QJsonArray &array)
{
    QStringList folders;
    for (const QJsonValue &value : array) {
        if (value.isString() && !value.toString().isEmpty())
            folders << value.toString();
    }
    return folders;
}

PlaylistManager::PlaylistManager(ConfigManager *config, QObject *parent)
: QObject(parent), m_config(config)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(300);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlaylistManager::flushPendingSaves);

    m_defaultModel = new PlaylistModel(this);
    m_models.insert(DEFAULT_ID, m_defaultModel);
    m_names.insert(DEFAULT_ID, tr("playlist_default"));

    // Autosave on changes in named models
    connect(m_defaultModel, &PlaylistModel::modified, this, [this]() {
        scheduleSave(DEFAULT_ID);
    });

    loadAll();

    m_saver = new PlaylistSaver(this);
    connect(m_saver, &PlaylistSaver::saved, this, [this](const QString &filePath) {
        const QString id = QFileInfo(filePath).completeBaseName();
        auto it = m_pendingPlaylistSaves.find(id);
        if (it == m_pendingPlaylistSaves.end())
            return;
        if (--it.value() <= 0)
            m_pendingPlaylistSaves.erase(it);
    });

    m_radioNames = new RadioNameResolver(this);
    connect(m_radioNames, &RadioNameResolver::nameResolved, this,
            [this](const QString &url, const QString &name) {
        m_stationNames.insert(url, name);
        for (PlaylistModel *model : m_models) {
            if (model && model->contains(url))
                model->setStationTitle(url, name);
        }
    });

    m_tagScanner = new TagScanner(this);
    connect(m_tagScanner, &TagScanner::scanned, this,
            [this](const QString &path, const QString &title, const QString &artist,
                   const QString &album, int year, bool ok) {
        if (!ok) return;
        for (PlaylistModel *model : m_models) {
            if (model)
                model->updateMetadata(path, title, artist, album, year);
        }
    });

    // Do not rescan every saved playlist at startup: reading tags for hundreds
    // of files competed with the first paint and with mpv opening the requested
    // file. The saved tags are still shown; a playlist is refreshed when it is
    // opened (see setActiveId), and newly added files are scanned immediately.

    setupPlaylistWatcher();
    setupFolderWatching();
}

PlaylistManager::~PlaylistManager() {
    if (m_tagScanner)
        m_tagScanner->stop();
    if (m_saveTimer.isActive() || !m_dirtyIds.isEmpty())
        flushPendingSaves();
    if (m_saver)
        m_saver->waitForIdle();
}

void PlaylistManager::scheduleSave(const QString &id) {
    if (id != DEFAULT_ID)
        m_dirtyIds.insert(id);
    m_playlistsChangedPending = true;
    m_saveTimer.start();
}

void PlaylistManager::flushPendingSaves() {
    const QSet<QString> ids = m_dirtyIds;
    m_dirtyIds.clear();
    for (const QString &id : ids)
        queuePlaylistSave(id);
    if (m_playlistsChangedPending) {
        m_playlistsChangedPending = false;
        emit playlistsChanged();
    }
}

void PlaylistManager::setupPlaylistWatcher()
{
    m_externalReloadTimer.setSingleShot(true);
    m_externalReloadTimer.setInterval(250);
    connect(&m_externalReloadTimer, &QTimer::timeout,
            this, &PlaylistManager::reloadExternalPlaylists);

    m_playlistWatcher = new QFileSystemWatcher(this);
    watchPlaylistFiles();
    connect(m_playlistWatcher, &QFileSystemWatcher::fileChanged, this, [this]() {
        watchPlaylistFiles();
        m_externalReloadTimer.start();
    });
    connect(m_playlistWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
        watchPlaylistFiles();
        m_externalReloadTimer.start();
    });
    if (m_config) {
        // The tab order lives in luma.conf; ConfigManager already watches it.
        connect(m_config, &ConfigManager::settingsReloaded, this, [this]() {
            m_externalReloadTimer.start();
        });
    }
    // Catch a file that changed while this instance was loading (or between the
    // directory scan and the watcher setup).
    m_externalReloadTimer.start();
}

void PlaylistManager::watchPlaylistFiles()
{
    if (!m_playlistWatcher) return;
    const QString dirPath = playlistsDir();
    if (!m_playlistWatcher->directories().contains(dirPath))
        m_playlistWatcher->addPath(dirPath);

    const QDir dir(dirPath);
    for (const QFileInfo &fi : dir.entryInfoList({"*.json"}, QDir::Files)) {
        // The Default playlist belongs to each window and is not shared.
        if (fi.completeBaseName() == DEFAULT_ID) continue;
        if (!m_playlistWatcher->files().contains(fi.absoluteFilePath()))
            m_playlistWatcher->addPath(fi.absoluteFilePath());
    }
}

void PlaylistManager::reloadExternalPlaylists()
{
    if (m_loading || m_reloadingExternal || !m_playlistWatcher)
        return;
    m_reloadingExternal = true;

    bool changed = false;
    const QDir dir(playlistsDir());
    const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
    QSet<QString> present;

    for (const QFileInfo &fi : files) {
        const QString id = fi.completeBaseName();
        if (id == DEFAULT_ID) continue;
        present.insert(id);
        m_missingPlaylistIds.remove(id);

        // Our own save is still queued/in flight: reading the file now could
        // restore the previous snapshot and undo the local change.
        if (m_dirtyIds.contains(id) || m_pendingPlaylistSaves.value(id) > 0)
            continue;

        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (!doc.isObject()) continue;
        const QJsonObject obj = doc.object();
        const QJsonArray entries = obj.value("entries").toArray();
        QString name = obj.value("name").toString();
        if (name.isEmpty()) name = id;
        const QString mode = normalizedPlaylistMode(obj.value("mode").toString());
        const QStringList folders = foldersFromJson(obj.value("watchFolders").toArray());

        PlaylistModel *model = m_models.value(id, nullptr);
        if (!model) {
            // A playlist created by another instance.
            model = new PlaylistModel(this);
            m_models.insert(id, model);
            m_names.insert(id, name);
            m_modes.insert(id, mode);
            connect(model, &PlaylistModel::modified, this, [this, id]() {
                if (m_loading || m_reloadingExternal) return;
                scheduleSave(id);
            });
            model->setEntriesFromJson(entries, QString());
            if (mode == QLatin1String("shuffle"))
                model->setShuffle(true);
            if (!m_order.contains(id)) m_order << id;
            m_watchFolders.insert(id, folders);
            rebuildFolderWatcher();
            scheduleWatchScan(id);
            emit watchFoldersChanged(id);
            changed = true;
            continue;
        }

        bool entriesChanged = false;
        if (model->toJsonArray() != entries) {
            // Keep the currently playing file selected if it is still there.
            model->setEntriesFromJson(entries, model->currentFile());
            entriesChanged = true;
            changed = true;
        }
        if (m_names.value(id) != name) {
            m_names.insert(id, name);
            changed = true;
        }
        bool modeChanged = false;
        if (m_modes.value(id) != mode) {
            m_modes.insert(id, mode);
            modeChanged = true;
            changed = true;
        }
        if (entriesChanged || modeChanged)
            model->setShuffle(mode == QLatin1String("shuffle"));
        if (m_watchFolders.value(id) != folders) {
            m_watchFolders.insert(id, folders);
            m_watchedDirs.remove(id);
            invalidateWatchScan(id);
            rebuildFolderWatcher();
            scheduleWatchScan(id);
            emit watchFoldersChanged(id);
            changed = true;
        }
    }

    // Deletions: wait one recheck so an atomic save cannot look like one.
    QStringList missing;
    for (auto it = m_models.constBegin(); it != m_models.constEnd(); ++it) {
        const QString id = it.key();
        if (id != DEFAULT_ID && !present.contains(id))
            missing << id;
    }
    for (const QString &id : missing) {
        if (!m_missingPlaylistIds.contains(id)) {
            m_missingPlaylistIds.insert(id);
            QTimer::singleShot(1200, this, [this]() {
                m_externalReloadTimer.start();
            });
            continue;
        }

        if (m_activeId == id)
            setActiveId(DEFAULT_ID);
        PlaylistModel *model = m_models.take(id);
        if (model && m_playingModel == model) {
            m_playingModel = nullptr;
            emit playingIdChanged();
        }
        if (model) {
            model->disconnect(this);
            model->deleteLater();
        }
        m_names.remove(id);
        m_modes.remove(id);
        m_scannedPlaylists.remove(id);
        m_order.removeAll(id);
        stopWatchingPlaylist(id);
        m_missingPlaylistIds.remove(id);
        changed = true;
    }

    // Tab order: the order is stored in luma.conf by saveOrder(). Rebuild it
    // from the configured order, then keep any playlist the config does not
    // know yet (it can arrive from a window that has not saved the order yet).
    QStringList newOrder;
    for (const QString &id : playlistOrder()) {
        if (id != DEFAULT_ID && m_models.contains(id) && !newOrder.contains(id))
            newOrder << id;
    }
    for (const QString &id : m_order) {
        if (m_models.contains(id) && !newOrder.contains(id))
            newOrder << id;
    }
    QStringList extras;
    for (auto it = m_models.constBegin(); it != m_models.constEnd(); ++it) {
        if (it.key() != DEFAULT_ID && !newOrder.contains(it.key()))
            extras << it.key();
    }
    extras.sort();
    newOrder << extras;
    if (newOrder != m_order) {
        m_order = newOrder;
        changed = true;
    }

    m_reloadingExternal = false;
    watchPlaylistFiles();
    if (changed)
        emit playlistsChanged();

    // A missing file may have appeared during the recheck; start once more if
    // something is still pending so the delete decision is re-evaluated.
    if (!m_missingPlaylistIds.isEmpty()) {
        QTimer::singleShot(1200, this, [this]() {
            m_externalReloadTimer.start();
        });
    }
}

void PlaylistManager::setupFolderWatching()
{
    m_folderWatcher = new QFileSystemWatcher(this);
    m_watchScanDebounce.setSingleShot(true);
    m_watchScanDebounce.setInterval(500);
    connect(&m_watchScanDebounce, &QTimer::timeout, this, [this]() {
        const QSet<QString> pending = m_watchScanPending;
        m_watchScanPending.clear();
        for (const QString &id : pending)
            startWatchScan(id);
    });
    connect(m_folderWatcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &path) {
        const QSet<QString> ids = m_folderDirOwners.value(path);
        for (const QString &id : ids)
            scheduleWatchScan(id);
    });

    rebuildFolderWatcher();
    for (auto it = m_watchFolders.cbegin(); it != m_watchFolders.cend(); ++it) {
        if (!it.value().isEmpty())
            scheduleWatchScan(it.key());
    }
}

void PlaylistManager::rebuildFolderWatcher()
{
    if (!m_folderWatcher) return;

    QHash<QString, QSet<QString>> owners;
    auto addOwner = [&owners](const QString &dir, const QString &id) {
        if (!dir.isEmpty() && QFileInfo(dir).isDir())
            owners[dir].insert(id);
    };

    for (auto it = m_watchFolders.cbegin(); it != m_watchFolders.cend(); ++it) {
        const QString &id = it.key();
        if (id != DEFAULT_ID && !m_models.contains(id))
            continue;
        for (const QString &root : it.value()) {
            addOwner(root, id);
            // Also watch the parent, so a deleted and recreated root is seen.
            addOwner(QFileInfo(root).absolutePath(), id);
        }
        for (const QString &dir : m_watchedDirs.value(id))
            addOwner(dir, id);
    }

    const QStringList current = m_folderWatcher->directories();
    for (const QString &dir : current) {
        if (!owners.contains(dir))
            m_folderWatcher->removePath(dir);
    }
    for (auto it = owners.cbegin(); it != owners.cend(); ++it) {
        const QString &dir = it.key();
        if (!m_folderWatcher->directories().contains(dir))
            m_folderWatcher->addPath(dir);
    }
    m_folderDirOwners = owners;
}

void PlaylistManager::scheduleWatchScan(const QString &id)
{
    if (!m_watchFolders.contains(id) || m_watchFolders.value(id).isEmpty())
        return;
    if (id != DEFAULT_ID && !m_models.contains(id))
        return;
    m_watchScanPending.insert(id);
    m_watchScanDebounce.start();
}

void PlaylistManager::invalidateWatchScan(const QString &id)
{
    ++m_watchScanGeneration[id];
    m_watchScanRunning.remove(id);
    m_watchScanQueued.remove(id);
    m_watchScanPending.remove(id);
}

void PlaylistManager::startWatchScan(const QString &id)
{
    if (!m_watchFolders.contains(id) || m_watchFolders.value(id).isEmpty())
        return;
    if (id != DEFAULT_ID && !m_models.contains(id))
        return;
    if (m_watchScanRunning.contains(id)) {
        m_watchScanQueued.insert(id);
        return;
    }

    m_watchScanRunning.insert(id);
    const quint64 generation = ++m_watchScanGeneration[id];
    const QStringList roots = m_watchFolders.value(id);

    auto *future = new QFutureWatcher<FolderScanResult>(this);
    connect(future, &QFutureWatcher<FolderScanResult>::finished, this,
            [this, id, generation, future]() {
        const FolderScanResult result = future->result();
        future->deleteLater();
        if (m_watchScanGeneration.value(id, 0) != generation)
            return;
        m_watchScanRunning.remove(id);
        if (!m_watchFolders.contains(id)
                || (id != DEFAULT_ID && !m_models.contains(id)))
            return;

        QSet<QString> dirs;
        for (const QString &dir : result.dirs) {
            if (QFileInfo(dir).isDir())
                dirs.insert(dir);
        }
        m_watchedDirs.insert(id, dirs);
        rebuildFolderWatcher();

        PlaylistModel *model = id == DEFAULT_ID
            ? m_defaultModel : m_models.value(id, nullptr);
        if (model) {
            QStringList newFiles;
            for (const QString &path : result.files) {
                if (!model->contains(path))
                    newFiles << path;
            }
            if (!newFiles.isEmpty())
                addFilesToPlaylist(id, newFiles);
        }

        if (m_watchScanQueued.remove(id))
            scheduleWatchScan(id);
    });
    future->setFuture(QtConcurrent::run([roots]() { return scanFolderTree(roots); }));
}

void PlaylistManager::stopWatchingPlaylist(const QString &id)
{
    invalidateWatchScan(id);
    m_watchFolders.remove(id);
    m_watchedDirs.remove(id);
    rebuildFolderWatcher();
}

QString PlaylistManager::playlistsDir() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/luma/playlists";
    QDir().mkpath(dir);
    return dir;
}

QString PlaylistManager::playlistFilePath(const QString &id) const {
    return playlistsDir() + "/" + id + ".json";
}

QString PlaylistManager::sanitizeName(const QString &name) const {
    return name.trimmed();
}

QStringList PlaylistManager::playlistOrder() const {
    if (!m_config) return {};
    QString raw = m_config->value("playlists/order").toString();
    if (raw.isEmpty()) return {};
    return raw.split(',', Qt::SkipEmptyParts);
}

void PlaylistManager::saveOrder() {
    if (!m_config) return;
    m_config->setValue("playlists/order", m_order.join(','));
}

void PlaylistManager::loadAll() {
    m_loading = true;
    QString dir = playlistsDir();

    QStringList order = playlistOrder();
    QStringList existing;

    QDir d(dir);
    for (const QFileInfo &fi : d.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        existing << fi.completeBaseName();
    }

    // First those from order (if the file exists), then the new ones (alphabetically)
    QStringList finalOrder;
    for (const QString &id : order) {
        if (existing.contains(id)) finalOrder << id;
    }
    for (const QString &id : existing) {
        if (!finalOrder.contains(id)) finalOrder << id;
    }

    m_order.clear();
    for (const QString &id : finalOrder) {
        loadPlaylist(id);
        m_order << id;
    }
    saveOrder();
    m_loading = false;
    emit playlistsChanged();
}

void PlaylistManager::loadPlaylist(const QString &id) {
    QFile f(playlistFilePath(id));
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();

    QString name = obj.value("name").toString();
    if (name.isEmpty()) name = id;
    m_names.insert(id, name);
    if (obj.contains("mode"))
        m_modes.insert(id, normalizedPlaylistMode(obj.value("mode").toString()));

    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) {
        model = new PlaylistModel(this);
        m_models.insert(id, model);

        // Autosave on changes
        connect(model, &PlaylistModel::modified, this, [this, id]() {
            if (m_loading || m_reloadingExternal) return;
            scheduleSave(id);
        });
    }

    model->setEntriesFromJson(obj.value("entries").toArray(), "");
    m_watchFolders.insert(id, foldersFromJson(obj.value("watchFolders").toArray()));
    // A named playlist with shuffle mode must start shuffled. This happens
    // while m_loading is true, so the shuffled order is not written to disk.
    if (playlistMode(id) == QLatin1String("shuffle"))
        model->setShuffle(true);

    // The file came back while Luma was closed: setEntries() dropped its stale
    // "unavailable" mark, so rewrite the playlist without it.
    if (model->takeDroppedUnavailableMarks())
        savePlaylist(id);
}

void PlaylistManager::queuePlaylistSave(const QString &id)
{
    if (id == DEFAULT_ID || !m_saver) return;
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return;
    ++m_pendingPlaylistSaves[id];
    m_saver->save(playlistFilePath(id), id, m_names.value(id, id),
                  normalizedPlaylistMode(m_modes.value(id)), model->entries(),
                  m_watchFolders.value(id));
}

void PlaylistManager::savePlaylist(const QString &id) {
    if (id == DEFAULT_ID) return;
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return;

    // A synchronous save must reflect the newest state: wait until snapshots
    // queued earlier have been written, otherwise an older one could land
    // after this file and overwrite it.
    if (m_saver) m_saver->waitForIdle();

    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = m_names.value(id, id);
    obj["mode"] = normalizedPlaylistMode(m_modes.value(id));

    obj["entries"] = model->toJsonArray();
    const QStringList watchFolders = m_watchFolders.value(id);
    if (!watchFolders.isEmpty())
        obj["watchFolders"] = QJsonArray::fromStringList(watchFolders);

    // Atomic replace: a crash while saving must not truncate the playlist.
    QSaveFile f(playlistFilePath(id));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

void PlaylistManager::saveActive() {
    if (m_activeId != DEFAULT_ID) savePlaylist(m_activeId);
}

QVariantList PlaylistManager::playlists() const {
    QVariantList list;

    QVariantMap def;
    def["id"] = DEFAULT_ID;
    def["name"] = tr("playlist_default");
    def["isDefault"] = true;
    def["count"] = m_defaultModel ? m_defaultModel->rowCount() : 0;
    list.append(def);

    for (const QString &id : m_order) {
        PlaylistModel *model = m_models.value(id, nullptr);
        QVariantMap m;
        m["id"] = id;
        m["name"] = m_names.value(id, id);
        m["isDefault"] = false;
        m["count"] = model ? model->rowCount() : 0;
        list.append(m);
    }
    return list;
}

QStringList PlaylistManager::warmupTitles() const
{
    // About one screen of rows per playlist: that is what is painted when a
    // playlist is selected for the first time.
    QStringList titles;
    auto collect = [&titles](PlaylistModel *model) {
        if (!model) return;
        const int rows = qMin(model->rowCount(), 24);
        for (int row = 0; row < rows; ++row)
            titles << model->data(model->index(row, 0), Qt::DisplayRole).toString();
    };
    collect(m_defaultModel);
    for (PlaylistModel *model : m_models)
        collect(model);
    return titles;
}

QStringList PlaylistManager::allDisplayTitles() const
{
    QStringList titles;
    auto collect = [&titles](PlaylistModel *model) {
        if (!model) return;
        const int rows = model->rowCount();
        titles.reserve(titles.size() + rows);
        for (int row = 0; row < rows; ++row)
            titles << model->data(model->index(row, 0), Qt::DisplayRole).toString();
    };
    collect(m_defaultModel);
    for (PlaylistModel *model : m_models)
        collect(model);
    return titles;
}

QString PlaylistManager::playlistName(const QString &id) const {
    if (id == DEFAULT_ID) return tr("playlist_default");
    return m_names.value(id, id);
}

void PlaylistManager::setActiveId(const QString &id) {
    if (m_activeId == id) return;
    if (!m_models.contains(id)) return;
    qDebug() << "[perf] activate playlist" << id;
    m_activeId = id;
    emit activeIdChanged();
    emit activeModelChanged();
    scanPlaylistTags(id);
    resolveStationNames(activeModel());
}

void PlaylistManager::resolveStationNames(PlaylistModel *model)
{
    if (!model || !m_radioNames) return;
    for (const PlaylistModel::Entry &entry : model->entries()) {
        if (!entry.station.isEmpty() || entry.persistTitle) continue;
        if (!entry.path.startsWith(QStringLiteral("http://"))
                && !entry.path.startsWith(QStringLiteral("https://")))
            continue;
        const QString known = m_stationNames.value(entry.path);
        if (!known.isEmpty())
            model->setStationTitle(entry.path, known);
        else
            m_radioNames->resolve(entry.path);
    }
}

void PlaylistManager::scanPlaylistTags(const QString &id) {
    if (!m_tagScanner || m_scannedPlaylists.contains(id)) return;
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return;
    m_scannedPlaylists.insert(id);

    for (const PlaylistModel::Entry &entry : model->entries()) {
        if (!entry.path.contains(QStringLiteral("://")) && isAudioFilePath(entry.path))
            m_tagScanner->enqueue(entry.path);
    }
}

PlaylistModel* PlaylistManager::activeModel() const {
    return m_models.value(m_activeId, m_defaultModel);
}

PlaylistModel* PlaylistManager::modelById(const QString &id) const {
    return m_models.value(id, nullptr);
}

QString PlaylistManager::createPlaylist(const QString &name) {
    QString clean = sanitizeName(name);
    if (clean.isEmpty()) return {};

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    PlaylistModel *model = new PlaylistModel(this);
    m_models.insert(id, model);
    m_names.insert(id, clean);
    m_modes.insert(id, QStringLiteral("sequential"));
    m_order << id;

    connect(model, &PlaylistModel::modified, this, [this, id]() {
        if (m_loading) return;
        scheduleSave(id);
    });

    savePlaylist(id);
    saveOrder();
    emit playlistsChanged();
    return id;
}

void PlaylistManager::deletePlaylist(const QString &id) {
    if (id == DEFAULT_ID) return;
    if (!m_models.contains(id)) return;

    // A pending save must not resurrect a deleted playlist.
    m_dirtyIds.remove(id);
    if (m_saver) m_saver->waitForIdle();

    if (m_activeId == id) {
        setActiveId(DEFAULT_ID);
    }

    QFile::remove(playlistFilePath(id));
    PlaylistModel *model = m_models.take(id);
    if (model && m_playingModel == model) {
        m_playingModel = nullptr;
        emit playingIdChanged();
    }
    if (model) {
        model->disconnect(this);
        model->deleteLater();
    }
    m_names.remove(id);
    m_modes.remove(id);
    m_scannedPlaylists.remove(id);
    m_order.removeAll(id);
    stopWatchingPlaylist(id);

    saveOrder();
    emit playlistsChanged();
}

QString PlaylistManager::playlistMode(const QString &id) const
{
    if (id == DEFAULT_ID)
        return m_config ? normalizedPlaylistMode(m_config->playlistMode())
                        : QStringLiteral("sequential");
    return normalizedPlaylistMode(m_modes.value(id));
}

void PlaylistManager::setPlaylistMode(const QString &id, const QString &mode)
{
    const QString normalized = normalizedPlaylistMode(mode);
    if (id == DEFAULT_ID) {
        // The Default playlist's mode is the one edited in the settings.
        if (m_config) m_config->setPlaylistMode(normalized);
        if (m_defaultModel)
            m_defaultModel->setShuffle(normalized == QLatin1String("shuffle"));
        return;
    }
    if (!m_models.contains(id)) return;
    if (m_modes.value(id) == normalized) return;
    m_modes.insert(id, normalized);
    // Applying shuffle to a named playlist used to be skipped to protect the
    // saved order, which meant the playlist was never actually shuffled.
    if (PlaylistModel *model = m_models.value(id, nullptr))
        model->setShuffle(normalized == QLatin1String("shuffle"));
    savePlaylist(id);
    emit playlistModeChanged(id, normalized);
}

void PlaylistManager::retranslate()
{
    // The playlist names (including the Default one) come from C++ tr(),
    // so the QML bindings have to be re-evaluated after a language change.
    emit playlistsChanged();
}

void PlaylistManager::movePlaylist(int from, int to) {
    // Indices are positions in the tab bar, where index 0 is Default. Default
    // itself cannot move, and no tab can be placed before it.
    if (from <= 0 || from > m_order.size()) return;
    const int fromRow = from - 1;
    const int toRow = qBound(0, to - 1, m_order.size() - 1);
    if (fromRow == toRow) return;

    m_order.move(fromRow, toRow);
    saveOrder();
    emit playlistsChanged();
}

void PlaylistManager::sortPlaylist(const QString &id, const QString &key, bool ascending) {
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return;
    if (key == QLatin1String("shuffle")) {
        model->setShuffle(true);
        return;
    }
    model->sortBy(key, ascending);
}

void PlaylistManager::shufflePlaylist(const QString &id) {
    if (PlaylistModel *model = m_models.value(id, nullptr))
        model->setShuffle(true);
}

bool PlaylistManager::playlistHasAudio(const QString &id) const {
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return false;
    for (const PlaylistModel::Entry &entry : model->entries()) {
        if (isAudioFilePath(entry.path))
            return true;
    }
    return false;
}

void PlaylistManager::renamePlaylist(const QString &id, const QString &newName) {
    if (id == DEFAULT_ID) return;
    QString clean = sanitizeName(newName);
    if (clean.isEmpty()) return;
    m_names[id] = clean;
    savePlaylist(id);
    emit playlistsChanged();
}

QVariantMap PlaylistManager::buildEntry(const QString &path) const {
    QVariantMap entry;
    QFileInfo fi(path);
    entry["path"] = fi.absoluteFilePath();
    entry["kind"] = isAudioFilePath(path) ? "audio" : "video";
    // Tags are read by TagScanner in the background.
    entry["title"] = fi.completeBaseName().isEmpty() ? fi.fileName() : fi.completeBaseName();
    return entry;
}

void PlaylistManager::addFilesToPlaylist(const QString &id, const QStringList &paths) {
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model || paths.isEmpty()) return;

    QStringList files;
    for (const QString &path : paths) {
        const QFileInfo fi(path);
        if (!fi.exists()) continue;
        if (fi.isDir()) {
            // Drag & drop can hand us a folder: add its contents recursively
            // instead of silently skipping it.
            files << mediaFilesInFolder(fi.absoluteFilePath());
            continue;
        }
        files << fi.absoluteFilePath();
    }

    for (const QString &path : files) {
        const QFileInfo fi(path);
        if (!fi.exists() || fi.isDir()) continue;
        QVariantMap entry = buildEntry(fi.absoluteFilePath());
        model->appendEntry(entry);
        if (m_tagScanner
                && entry.value("kind").toString() == QLatin1String("audio")
                && !fi.absoluteFilePath().contains(QStringLiteral("://"))) {
            m_tagScanner->enqueue(fi.absoluteFilePath());
        }
    }

    if (id != DEFAULT_ID) scheduleSave(id);
    else emit playlistsChanged();
}

void PlaylistManager::setPlayingModel(PlaylistModel *model) {
    const bool changed = m_playingModel != model;
    m_playingModel = model;
    for (PlaylistModel *candidate : m_models) {
        if (candidate)
            candidate->setPlaying(candidate == model);
    }
    if (changed)
        emit playingIdChanged();
}

QString PlaylistManager::playingId() const
{
    if (!m_playingModel)
        return {};
    for (auto it = m_models.cbegin(); it != m_models.cend(); ++it) {
        if (it.value() == m_playingModel)
            return it.key();
    }
    return {};
}

QString PlaylistManager::playlistModeFor(PlaylistModel *model) const {
    for (auto it = m_models.cbegin(); it != m_models.cend(); ++it) {
        if (it.value() == model)
            return playlistMode(it.key());
    }
    return QStringLiteral("sequential");
}

void PlaylistManager::scanAudioPaths(const QStringList &paths) {
    if (!m_tagScanner) return;
    for (const QString &path : paths) {
        if (!path.contains(QStringLiteral("://")) && isAudioFilePath(path))
            m_tagScanner->enqueue(path);
    }
}

void PlaylistManager::addUrlToPlaylist(const QString &id, const QString &url) {
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return;

    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;

    QVariantMap entry;
    entry["path"] = trimmed;
    // A stream may be either video or radio — assume audio by default.
    entry["kind"] = "audio";
    entry["title"] = "";

    model->appendEntry(entry);

    // The same link may already have been resolved in another playlist.
    const QString known = m_stationNames.value(trimmed);
    if (!known.isEmpty())
        model->setStationTitle(trimmed, known);

    if (id != DEFAULT_ID) scheduleSave(id);
    else emit playlistsChanged();

    if (trimmed.startsWith(QStringLiteral("http://"))
            || trimmed.startsWith(QStringLiteral("https://"))) {
        fetchUrlTitle(id, trimmed);
    }
}

void PlaylistManager::fetchUrlTitle(const QString &id, const QString &url) {
    // The link may be a radio stream: its icy-name is the only useful title
    // and yt-dlp fails on those. Probe it in parallel with yt-dlp.
    if (m_radioNames) m_radioNames->resolve(url);

    const QString exe = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    if (exe.isEmpty()) return;

    constexpr qsizetype kMaxTitleOutput = 64 * 1024;
    auto output = std::make_shared<QByteArray>();
    auto errorOutput = std::make_shared<QByteArray>();
    const QString ytdlpLogMode = Logger::instance().componentMode(QStringLiteral("yt-dlp"));
    if (ytdlpLogMode == QLatin1String("info") || ytdlpLogMode == QLatin1String("debug"))
        Logger::instance().writeComponent(
            QStringLiteral("yt-dlp"),
            QStringLiteral("yt-dlp --get-title %1").arg(url));

    auto *proc = new QProcess(this);
    QStringList args = m_config ? m_config->ytdlpConfigArgs() : QStringList();
    if (ytdlpLogMode == QLatin1String("debug"))
        args << QStringLiteral("--verbose");
    args << QStringLiteral("--get-title")
         << QStringLiteral("--no-playlist");
    if (ytdlpLogMode != QLatin1String("debug"))
        args << QStringLiteral("--no-warnings");
    args << QStringLiteral("--no-progress")
         << url;
    proc->setProgram(exe);
    proc->setArguments(args);
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    auto *timeout = new QTimer(proc);
    timeout->setSingleShot(true);
    timeout->setInterval(15000);
    connect(timeout, &QTimer::timeout, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, output]() {
        // Always drain the pipe, even when the retained buffer is full.
        const QByteArray chunk = proc->readAllStandardOutput();
        if (output->size() < kMaxTitleOutput)
            output->append(chunk);
        if (output->size() >= kMaxTitleOutput) {
            output->truncate(kMaxTitleOutput);
            if (proc->state() == QProcess::Running)
                proc->kill();
        }
    });
    connect(proc, &QProcess::readyReadStandardError, this,
            [proc, errorOutput, ytdlpLogMode]() {
        const QByteArray chunk = proc->readAllStandardError();
        if (errorOutput->size() < 64 * 1024)
            errorOutput->append(chunk);
        if (ytdlpLogMode == QLatin1String("info") || ytdlpLogMode == QLatin1String("debug"))
            Logger::instance().writeComponent(QStringLiteral("yt-dlp"),
                                              QString::fromUtf8(chunk));
    });
    connect(proc, &QProcess::errorOccurred, this,
            [proc, ytdlpLogMode](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        if (ytdlpLogMode != QLatin1String("off"))
            Logger::instance().writeComponent(
                QStringLiteral("yt-dlp"),
                QStringLiteral("failed to start yt-dlp: %1").arg(proc->errorString()));
        proc->deleteLater();
    });
    connect(proc, &QProcess::finished, this,
            [this, proc, id, url, output, errorOutput, ytdlpLogMode]
            (int code, QProcess::ExitStatus status) {
        output->append(proc->readAllStandardOutput());
        if (output->size() > kMaxTitleOutput)
            output->truncate(kMaxTitleOutput);

        const QByteArray rest = proc->readAllStandardError();
        if (!rest.isEmpty()) {
            if (errorOutput->size() < 64 * 1024)
                errorOutput->append(rest);
            if (ytdlpLogMode == QLatin1String("info") || ytdlpLogMode == QLatin1String("debug"))
                Logger::instance().writeComponent(QStringLiteral("yt-dlp"),
                                                  QString::fromUtf8(rest));
        }

        const bool ok = status == QProcess::NormalExit && code == 0;
        if (ytdlpLogMode == QLatin1String("error") && !ok) {
            QString detail = QString::fromUtf8(*errorOutput).trimmed();
            if (detail.isEmpty())
                detail = QStringLiteral("yt-dlp exited with code %1").arg(code);
            Logger::instance().writeComponent(QStringLiteral("yt-dlp"), detail);
        }

        const QString text = QString::fromUtf8(*output).trimmed();
        if (ok && !text.isEmpty()) {
            const QString title = text.section(QLatin1Char('\n'), 0, 0).trimmed();
            if (!title.isEmpty()) {
                if (PlaylistModel *model = m_models.value(id, nullptr))
                    model->setVideoTitle(url, title);
            }
        }
        proc->deleteLater();
    });
    timeout->start();
    proc->start();
}

void PlaylistManager::addFolderToPlaylist(const QString &id, const QString &folder) {
    if (!QFileInfo(folder).isDir()) return;
    addFilesToPlaylist(id, {folder});
}

QStringList PlaylistManager::watchFolders(const QString &id) const
{
    if (id == DEFAULT_ID)
        return {};
    return m_watchFolders.value(id);
}

bool PlaylistManager::isDefaultPlaylist(const QString &id) const
{
    return id == DEFAULT_ID;
}

bool PlaylistManager::addWatchFolder(const QString &id, const QString &folder)
{
    // The Default playlist is not saved, so it has no properties and no
    // folder watching.
    if (id == DEFAULT_ID || !m_models.contains(id))
        return false;
    const QFileInfo info(folder);
    if (!info.exists() || !info.isDir())
        return false;

    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        canonical = info.absoluteFilePath();
    QStringList &folders = m_watchFolders[id];
    if (folders.contains(canonical))
        return false;
    folders << canonical;

    m_watchedDirs.remove(id);
    invalidateWatchScan(id);
    rebuildFolderWatcher();
    if (id != DEFAULT_ID)
        savePlaylist(id);
    scheduleWatchScan(id);
    emit watchFoldersChanged(id);
    return true;
}

void PlaylistManager::removeWatchFolder(const QString &id, const QString &folder)
{
    auto it = m_watchFolders.find(id);
    if (it == m_watchFolders.end())
        return;

    QString target = folder;
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    if (!canonical.isEmpty())
        target = canonical;
    if (it->removeAll(target) == 0)
        return;

    m_watchedDirs.remove(id);
    invalidateWatchScan(id);
    rebuildFolderWatcher();
    if (id != DEFAULT_ID)
        savePlaylist(id);
    if (!it.value().isEmpty())
        scheduleWatchScan(id);
    emit watchFoldersChanged(id);
}

void PlaylistManager::openAddWatchFolderDialog(const QString &id)
{
    if (id == DEFAULT_ID)
        return;
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();
    const QString folder = QFileDialog::getExistingDirectory(
        nullptr, tr("playlist_watch_add_folder"), defaultDir);
    if (!folder.isEmpty())
        addWatchFolder(id, folder);
}

void PlaylistManager::openAddFilesDialog(const QString &id) {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();

    QStringList paths = QFileDialog::getOpenFileNames(
        nullptr,
        tr("playlist_add_files"),
                                                      defaultDir,
                                                      tr("playlist_files_filter")
    );
    if (!paths.isEmpty()) addFilesToPlaylist(id, paths);
}

void PlaylistManager::openAddFolderDialog(const QString &id) {
    QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultDir.isEmpty()) defaultDir = QDir::homePath();

    QString folder = QFileDialog::getExistingDirectory(
        nullptr,
        tr("playlist_add_folder"),
                                                       defaultDir
    );
    if (!folder.isEmpty()) addFolderToPlaylist(id, folder);
}
