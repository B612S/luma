#pragma once
#include <QHash>
#include <QVector>
#include <QJSValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include "PluginManifest.hpp"

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

private:
    struct EventHandler {
        QJSValue handler;
        bool once = false;
    };

    bool fail(const QString &message);
    QJSValue toScript(const QVariant &value) const;

    PluginManager *m_manager;
    PluginManifest m_manifest;
    QJSEngine *m_engine = nullptr;
    PluginApi *m_api = nullptr;
    bool m_loaded = false;
    QString m_error;
    QHash<QString, QVector<EventHandler>> m_handlers;
    QHash<QString, QJSValue> m_menuCallbacks;
    QHash<QString, QVariantMap> m_menuSpecs;
    QVector<PluginWindow *> m_windows;
};
