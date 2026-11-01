#include "PlaylistManager.hpp"
#include "ConfigManager.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QUuid>
#include <QDebug>

extern "C" {
    #include <libavformat/avformat.h>
}

const QString PlaylistManager::DEFAULT_ID = "__default__";

PlaylistManager::PlaylistManager(ConfigManager *config, QObject *parent)
: QObject(parent), m_config(config)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(300);
    connect(&m_saveTimer, &QTimer::timeout, this, &PlaylistManager::flushPendingSaves);

    m_defaultModel = new PlaylistModel(this);
    m_models.insert(DEFAULT_ID, m_defaultModel);
    m_names.insert(DEFAULT_ID, tr("playlist_default"));

    // Автосохранение при изменениях в именованных моделях
    connect(m_defaultModel, &PlaylistModel::modified, this, [this]() {
        scheduleSave(DEFAULT_ID);
    });

    loadAll();
}

PlaylistManager::~PlaylistManager() {
    if (m_saveTimer.isActive())
        flushPendingSaves();
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
        savePlaylist(id);
    if (m_playlistsChangedPending) {
        m_playlistsChangedPending = false;
        emit playlistsChanged();
    }
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

    // Сначала — из order (если файл есть), потом новые (по алфавиту)
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

    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) {
        model = new PlaylistModel(this);
        m_models.insert(id, model);

        // Автосохранение при изменениях
        connect(model, &PlaylistModel::modified, this, [this, id]() {
            if (m_loading) return;
            scheduleSave(id);
        });
    }

    QVariantList entries;
    for (const QJsonValue &v : obj.value("entries").toArray()) {
        entries.append(v.toObject().toVariantMap());
    }
    model->setEntries(entries, "");
}

void PlaylistManager::savePlaylist(const QString &id) {
    if (id == DEFAULT_ID) return;
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return;

    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = m_names.value(id, id);

    QJsonArray entries;
    for (const QVariant &v : model->toVariantList()) {
        entries.append(QJsonObject::fromVariantMap(v.toMap()));
    }
    obj["entries"] = entries;

    QFile f(playlistFilePath(id));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        f.close();
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

QString PlaylistManager::playlistName(const QString &id) const {
    if (id == DEFAULT_ID) return tr("playlist_default");
    return m_names.value(id, id);
}

void PlaylistManager::setActiveId(const QString &id) {
    if (m_activeId == id) return;
    if (!m_models.contains(id)) return;
    m_activeId = id;
    emit activeIdChanged();
    emit activeModelChanged();
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

    // Отложенная запись не должна «воскресить» удалённый плейлист.
    m_dirtyIds.remove(id);

    if (m_activeId == id) {
        setActiveId(DEFAULT_ID);
    }

    QFile::remove(playlistFilePath(id));
    PlaylistModel *model = m_models.take(id);
    if (model) {
        model->disconnect(this);
        model->deleteLater();
    }
    m_names.remove(id);
    m_order.removeAll(id);

    saveOrder();
    emit playlistsChanged();
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

    QString ext = fi.suffix().toLower();
    static const QStringList audioExt = {"mp3","flac","aac","wav","ogg","m4a","opus","wma"};
    bool isAudio = audioExt.contains(ext);
    entry["kind"] = isAudio ? "audio" : "video";

    if (isAudio) {
        AVFormatContext *fmt_ctx = nullptr;
        if (avformat_open_input(&fmt_ctx, path.toUtf8().constData(), nullptr, nullptr) == 0) {
            if (avformat_find_stream_info(fmt_ctx, nullptr) >= 0) {
                AVDictionaryEntry *tag = nullptr;
                tag = av_dict_get(fmt_ctx->metadata, "title", nullptr, 0);
                if (tag) entry["title"] = QString::fromUtf8(tag->value);
                tag = av_dict_get(fmt_ctx->metadata, "artist", nullptr, 0);
                if (tag) entry["artist"] = QString::fromUtf8(tag->value);
                tag = av_dict_get(fmt_ctx->metadata, "album", nullptr, 0);
                if (tag) entry["album"] = QString::fromUtf8(tag->value);
                entry["duration"] = (double)fmt_ctx->duration / AV_TIME_BASE;
            }
            avformat_close_input(&fmt_ctx);
        }
    }

    if (entry.value("title").toString().isEmpty())
        entry["title"] = fi.fileName();

    return entry;
}

void PlaylistManager::addFilesToPlaylist(const QString &id, const QStringList &paths) {
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model || paths.isEmpty()) return;

    for (const QString &path : paths) {
        QFileInfo fi(path);
        if (!fi.exists() || fi.isDir()) continue;
        QVariantMap entry = buildEntry(fi.absoluteFilePath());
        model->appendEntry(entry);
    }

    if (id != DEFAULT_ID) scheduleSave(id);
    else emit playlistsChanged();
}

void PlaylistManager::addUrlToPlaylist(const QString &id, const QString &url) {
    PlaylistModel *model = m_models.value(id, nullptr);
    if (!model) return;

    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) return;

    QVariantMap entry;
    entry["path"] = trimmed;
    // Поток может оказаться и видео, и радио — по умолчанию считаем аудио.
    entry["kind"] = "audio";
    entry["title"] = "";
    entry["duration"] = 0.0;

    model->appendEntry(entry);

    if (id != DEFAULT_ID) scheduleSave(id);
    else emit playlistsChanged();
}

void PlaylistManager::addFolderToPlaylist(const QString &id, const QString &folder) {
    QDir dir(folder);
    if (!dir.exists()) return;

    static const QStringList allowed = {
        "mp4","mkv","avi","webm","mov","flv",
        "mp3","flac","aac","wav","ogg","m4a","opus","wma"
    };

    QStringList files;
    for (const QFileInfo &fi : dir.entryInfoList(QDir::Files, QDir::Name)) {
        if (allowed.contains(fi.suffix().toLower()))
            files << fi.absoluteFilePath();
    }

    addFilesToPlaylist(id, files);
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
