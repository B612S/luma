#pragma once
#include <QObject>
#include <QJSValue>
#include <QSettings>
#include <QString>
#include <QVariantMap>
#include "plugins/PluginManifest.hpp"

class QNetworkAccessManager;
class PluginManager;
class PluginRuntime;

// One small QObject per API namespace. PluginRuntime puts them on the `luma`
// object; methods that mutate the player check the manifest permissions.
// Plugins deliberately have no process-spawning API: any ffmpeg/yt-dlp work is
// owned by Luma's media pipeline, never by plugin code.

class PluginEventsApi : public QObject {
    Q_OBJECT
public:
    explicit PluginEventsApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE void on(const QString &event, const QJSValue &handler);
    Q_INVOKABLE void once(const QString &event, const QJSValue &handler);

private:
    PluginRuntime *m_runtime;
};

class PluginLogApi : public QObject {
    Q_OBJECT
public:
    explicit PluginLogApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE void info(const QString &message);
    Q_INVOKABLE void warn(const QString &message);
    Q_INVOKABLE void error(const QString &message);

private:
    PluginRuntime *m_runtime;
};

class PluginMediaApi : public QObject {
    Q_OBJECT
public:
    explicit PluginMediaApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap state() const;
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double position);
    Q_INVOKABLE void setVolume(int volume);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool open(const QString &path);

private:
    bool allowed(const char *permission) const;
    PluginRuntime *m_runtime;
};

class PluginPlaylistApi : public QObject {
    Q_OBJECT
public:
    explicit PluginPlaylistApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE QVariantList list() const;
    Q_INVOKABLE QString active() const;
    Q_INVOKABLE QVariantList entries(const QString &id) const;
    Q_INVOKABLE bool add(const QString &id, const QStringList &paths);
    Q_INVOKABLE int remove(const QString &id, const QVariantList &indices);
    Q_INVOKABLE QString create(const QString &name);
    Q_INVOKABLE bool setMode(const QString &id, const QString &mode);

private:
    bool allowed(const char *permission) const;
    PluginRuntime *m_runtime;
};

class PluginConfigApi : public QObject {
    Q_OBJECT
public:
    explicit PluginConfigApi(PluginRuntime *runtime, const QString &dataDir,
                             QObject *parent = nullptr);
    Q_INVOKABLE QVariant get(const QString &key, const QVariant &fallback = QVariant()) const;
    Q_INVOKABLE void set(const QString &key, const QVariant &value);

private:
    PluginRuntime *m_runtime;
    QString m_dataDir;
    mutable QSettings m_settings;
};

class PluginStorageApi : public QObject {
    Q_OBJECT
public:
    explicit PluginStorageApi(PluginRuntime *runtime, const QString &dataDir,
                              QObject *parent = nullptr);
    Q_INVOKABLE QString readText(const QString &name) const;
    Q_INVOKABLE bool writeText(const QString &name, const QString &text);
    Q_INVOKABLE bool exists(const QString &name) const;

private:
    bool validName(const QString &name) const;
    PluginRuntime *m_runtime;
    QString m_dataDir;
};

class PluginNetApi : public QObject {
    Q_OBJECT
public:
    explicit PluginNetApi(PluginRuntime *runtime, QObject *parent = nullptr);
    // callback({ ok, status, headers, body, error })
    Q_INVOKABLE void fetch(const QString &url, const QVariantMap &options,
                           const QJSValue &callback);

private:
    PluginRuntime *m_runtime;
    QNetworkAccessManager *m_network;
};

class PluginMpvApi : public QObject {
    Q_OBJECT
public:
    explicit PluginMpvApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE QVariant getProperty(const QString &name) const;
    Q_INVOKABLE bool setProperty(const QString &name, const QVariant &value);
    Q_INVOKABLE bool command(const QVariantList &args);

private:
    PluginRuntime *m_runtime;
};

class PluginProcessApi : public QObject {
    Q_OBJECT
public:
    explicit PluginProcessApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE QString path(const QString &program) const;
    // callback({ ok, exitCode, stdout, stderr, error })
    Q_INVOKABLE void run(const QString &program, const QStringList &args,
                         const QVariantMap &options, const QJSValue &callback);
    void abortAll();

private:
    bool allowed() const;
    bool supported(const QString &program) const;
    PluginRuntime *m_runtime;
    bool m_aborted = false;
};

class PluginI18nApi : public QObject {
    Q_OBJECT
public:
    explicit PluginI18nApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE QString t(const QString &key,
                          const QString &fallback = QString()) const;

private:
    PluginRuntime *m_runtime;
};

class PluginStatsApi : public QObject {
    Q_OBJECT
public:
    explicit PluginStatsApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap snapshot() const;
    Q_INVOKABLE void set(const QString &key, const QString &label,
                         const QVariant &value);
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap setPage(const QString &id, const QString &title,
                                    const QVariantList &content,
                                    int preferredSlot = 0);
    Q_INVOKABLE void removePage(const QString &id);
    Q_INVOKABLE void clearPages();
    Q_INVOKABLE void showPage(const QString &id);

private:
    bool allowed() const;
    PluginRuntime *m_runtime;
};

class PluginShortcutsApi : public QObject {
    Q_OBJECT
public:
    explicit PluginShortcutsApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE bool add(const QString &key, const QJSValue &callback);
    Q_INVOKABLE void remove(const QString &key);
    // Register the handler for an action declared in plugin.json under
    // "shortcuts"; the key is assigned in Settings → Shortcuts.
    Q_INVOKABLE bool on(const QString &id, const QJSValue &callback);

private:
    bool allowed() const;
    PluginRuntime *m_runtime;
};

class PluginUiApi : public QObject {
    Q_OBJECT
public:
    explicit PluginUiApi(PluginRuntime *runtime, QObject *parent = nullptr);
    Q_INVOKABLE void osd(const QString &text);
    Q_INVOKABLE void menuAdd(const QVariantMap &spec, const QJSValue &callback);
    Q_INVOKABLE QObject *window(const QVariantMap &spec, const QJSValue &callback);

private:
    bool allowed(const char *permission) const;
    PluginRuntime *m_runtime;
};

class PluginApi : public QObject {
    Q_OBJECT
public:
    PluginApi(PluginRuntime *runtime, const PluginManifest &manifest,
              QObject *parent = nullptr);

    bool hasPermission(const char *permission) const;
    QString dataDir() const { return m_dataDir; }
    PluginRuntime *runtime() const { return m_runtime; }
    PluginManager *manager() const;

    PluginEventsApi *eventsApi() const { return m_events; }
    PluginLogApi *logApi() const { return m_log; }
    PluginMediaApi *mediaApi() const { return m_media; }
    PluginPlaylistApi *playlistsApi() const { return m_playlists; }
    PluginConfigApi *configApi() const { return m_config; }
    PluginStorageApi *storageApi() const { return m_storage; }
    PluginNetApi *netApi() const { return m_net; }
    PluginMpvApi *mpvApi() const { return m_mpv; }
    PluginProcessApi *processApi() const { return m_process; }
    PluginI18nApi *i18nApi() const { return m_i18n; }
    PluginStatsApi *statsApi() const { return m_stats; }
    PluginShortcutsApi *shortcutsApi() const { return m_shortcuts; }
    PluginUiApi *uiApi() const { return m_ui; }

private:
    PluginRuntime *m_runtime;
    PluginManifest m_manifest;
    QString m_dataDir;
    PluginEventsApi *m_events;
    PluginLogApi *m_log;
    PluginMediaApi *m_media;
    PluginPlaylistApi *m_playlists;
    PluginConfigApi *m_config;
    PluginStorageApi *m_storage;
    PluginNetApi *m_net;
    PluginMpvApi *m_mpv;
    PluginProcessApi *m_process;
    PluginI18nApi *m_i18n;
    PluginStatsApi *m_stats;
    PluginShortcutsApi *m_shortcuts;
    PluginUiApi *m_ui;
};
