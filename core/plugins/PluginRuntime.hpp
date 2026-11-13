#pragma once
#include <QHash>
#include <QVector>
#include <QJSValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include "plugins/PluginManifest.hpp"

class QJSEngine;
class PluginApi;
class PluginManager;
class PluginWindow;

// One loaded plugin: a private QJSEngine, the host API object, event
// subscriptions and menu callbacks. Everything is destroyed with unload().
class PluginRuntime : public QObject {
    Q_OBJECT

public:
    PluginRuntime(PluginManager *manager, const PluginManifest &manifest,
                  QObject *parent = nullptr);
    ~PluginRuntime() override;

    bool load();
    void unload();
    bool isLoaded() const { return m_loaded; }
    QString error() const { return m_error; }
    const PluginManifest &manifest() const { return m_manifest; }
    PluginApi *api() const { return m_api; }
    QJSEngine *engine() const { return m_engine; }
    PluginManager *manager() const { return m_manager; }

    void addEventHandler(const QString &event, const QJSValue &handler, bool once);
    void dispatch(const QString &event, const QVariantMap &payload);

    void addMenuAction(const QVariantMap &spec, const QJSValue &callback);
    QVariantList menuActions() const;
    bool invokeMenuAction(const QString &actionId, const QVariantMap &context);

    void registerWindow(PluginWindow *window);
    void removeWindow(PluginWindow *window);

    void log(const QString &level, const QString &message) const;

    QVariantList statsLines() const;
    QVariantList statsPages() const;
    QVariantMap setStatsPage(const QString &id, const QString &title,
                             const QVariantList &content, int preferredSlot = 0);
    void removeStatsPage(const QString &id);
    void clearStatsPages();
    void setStat(const QString &key, const QString &label, const QVariant &value);
    void removeStat(const QString &key);
    void clearStats();
    bool addShortcut(const QString &key, const QJSValue &callback);
    void removeShortcut(const QString &key);
    bool invokeShortcut(int key, Qt::KeyboardModifiers modifiers);
    // Actions declared in plugin.json under "shortcuts"; the host lists them in
    // Settings → Shortcuts and calls back here when their key is pressed.
    bool addShortcutAction(const QString &id, const QJSValue &callback);
    bool invokeShortcutAction(const QString &id);

private:
    struct EventHandler {
        QJSValue handler;
        bool once = false;
    };

    bool fail(const QString &message);

    PluginManager *m_manager;
    PluginManifest m_manifest;
    QJSEngine *m_engine = nullptr;
    PluginApi *m_api = nullptr;
    bool m_loaded = false;
    QString m_error;
    QHash<QString, QVector<EventHandler>> m_handlers;
    QHash<QString, QVariantMap> m_stats;
    QHash<QString, QVariantMap> m_statsPages;
    QHash<QString, QJSValue> m_shortcuts;
    QHash<QString, QJSValue> m_shortcutActions;
    QHash<QString, QJSValue> m_menuCallbacks;
    QHash<QString, QVariantMap> m_menuSpecs;
    QVector<PluginWindow *> m_windows;
};
