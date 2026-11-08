#include "PluginRuntime.hpp"
#include "PluginApi.hpp"
#include "PluginManager.hpp"
#include "PluginWindow.hpp"
#include <QFile>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QDebug>

PluginRuntime::PluginRuntime(PluginManager *manager, const PluginManifest &manifest,
                             QObject *parent)
    : QObject(parent), m_manager(manager), m_manifest(manifest)
{
}

PluginRuntime::~PluginRuntime()
{
    unload();
}

bool PluginRuntime::fail(const QString &message)
{
    m_error = message;
    qWarning().noquote() << "[plugin:" << m_manifest.id << "]" << message;
    return false;
}

bool PluginRuntime::load()
{
    if (m_loaded)
        return true;
    m_error.clear();
    m_engine = new QJSEngine(this);
    m_engine->installExtensions(QJSEngine::ConsoleExtension);
    m_api = new PluginApi(this, m_manifest, this);

    QJSValue luma = m_engine->newObject();
    luma.setProperty(QStringLiteral("apiVersion"), 1);
    luma.setProperty(QStringLiteral("id"), m_manifest.id);
    luma.setProperty(QStringLiteral("version"), m_manifest.version);
    luma.setProperty(QStringLiteral("dataDir"), m_api->dataDir());
    luma.setProperty(QStringLiteral("events"), m_engine->newQObject(m_api->eventsApi()));
    luma.setProperty(QStringLiteral("log"), m_engine->newQObject(m_api->logApi()));
    luma.setProperty(QStringLiteral("media"), m_engine->newQObject(m_api->mediaApi()));
    luma.setProperty(QStringLiteral("playlists"), m_engine->newQObject(m_api->playlistsApi()));
    luma.setProperty(QStringLiteral("config"), m_engine->newQObject(m_api->configApi()));
    luma.setProperty(QStringLiteral("storage"), m_engine->newQObject(m_api->storageApi()));
    luma.setProperty(QStringLiteral("net"), m_engine->newQObject(m_api->netApi()));
    luma.setProperty(QStringLiteral("mpv"), m_engine->newQObject(m_api->mpvApi()));
    luma.setProperty(QStringLiteral("process"), m_engine->newQObject(m_api->processApi()));
    luma.setProperty(QStringLiteral("ui"), m_engine->newQObject(m_api->uiApi()));
    m_engine->globalObject().setProperty(QStringLiteral("luma"), luma);

    QFile file(m_manifest.entry);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("cannot open %1").arg(m_manifest.entry));
    const QString source = QString::fromUtf8(file.readAll());
    const QJSValue result = m_engine->evaluate(source, m_manifest.entry);
    if (result.isError()) {
        return fail(QStringLiteral("%1:%2: %3")
                    .arg(m_manifest.entry)
                    .arg(result.property(QStringLiteral("lineNumber")).toInt())
                    .arg(result.toString()));
    }

    const QJSValue activate = m_engine->globalObject().property(QStringLiteral("activate"));
    if (activate.isCallable()) {
        const QJSValue value = activate.call({luma});
        if (value.isError())
            return fail(QStringLiteral("activate(): %1").arg(value.toString()));
    }

    m_loaded = true;
    return true;
}

void PluginRuntime::unload()
{

    if (m_engine) {
        const QJSValue deactivate = m_engine->globalObject().property(QStringLiteral("deactivate"));
        if (deactivate.isCallable()) {
            const QJSValue value = deactivate.call();
            if (value.isError())
                qWarning().noquote() << "[plugin:" << m_manifest.id
                                     << "] deactivate():" << value.toString();
        }
    }
    for (PluginWindow *window : std::as_const(m_windows)) {
        delete window;
    }
    m_windows.clear();
    m_handlers.clear();
    m_menuCallbacks.clear();
    m_menuSpecs.clear();
    if (m_api) {
        m_api->deleteLater();
        m_api = nullptr;
    }
    if (m_engine) {
        m_engine->deleteLater();
        m_engine = nullptr;
    }
    m_loaded = false;
}

void PluginRuntime::addEventHandler(const QString &event, const QJSValue &handler, bool once)
{
    static const QStringList known = {
        QStringLiteral("startup"), QStringLiteral("shutdown"),
        QStringLiteral("fileOpened"), QStringLiteral("fileEnded"),
        QStringLiteral("mediaChanged"), QStringLiteral("liveStreamChanged"),
        QStringLiteral("playModeChanged"), QStringLiteral("playlistChanged"),
        QStringLiteral("playlistActivated"), QStringLiteral("playingChanged"),
        QStringLiteral("playlistModeChanged"), QStringLiteral("positionChanged"),
        QStringLiteral("pausedChanged"), QStringLiteral("volumeChanged")
    };
    if (!known.contains(event) || !handler.isCallable())
        return;
    m_handlers[event].append({handler, once});
}

void PluginRuntime::dispatch(const QString &event, const QVariantMap &payload)
{
    if (!m_loaded || !m_engine)
        return;
    auto it = m_handlers.find(event);
    if (it == m_handlers.end())
        return;
    QVariantMap data = payload;
    data.insert(QStringLiteral("pluginId"), m_manifest.id);
    const QJSValue argument = m_engine->toScriptValue(data);
    QVector<EventHandler> handlers = it.value();
    for (const EventHandler &entry : handlers) {
        const QJSValue result = entry.handler.call({argument});
        if (result.isError())
            qWarning().noquote() << "[plugin:" << m_manifest.id << "]" << event
                                 << ":" << result.toString();
        if (entry.once) {
            auto &stored = m_handlers[event];
            for (int i = stored.size() - 1; i >= 0; --i) {
                if (stored.at(i).handler.equals(entry.handler)) {
                    stored.removeAt(i);
                    break;
                }
            }
        }
    }
}

void PluginRuntime::addMenuAction(const QVariantMap &spec, const QJSValue &callback)
{
    const QString localId = spec.value(QStringLiteral("id")).toString().trimmed();
    const QString label = spec.value(QStringLiteral("label")).toString().trimmed();
    const QString location = spec.value(QStringLiteral("location")).toString().trimmed();
    static const QStringList locations = {
        QStringLiteral("main"), QStringLiteral("playlist"),
        QStringLiteral("tab"), QStringLiteral("stats")
    };
    if (localId.isEmpty() || label.isEmpty() || !locations.contains(location)
            || !callback.isCallable())
        return;

    QVariantMap action = spec;
    action.insert(QStringLiteral("id"), m_manifest.id + QLatin1Char(':') + localId);
    action.insert(QStringLiteral("pluginId"), m_manifest.id);
    action.insert(QStringLiteral("label"), label);
    action.insert(QStringLiteral("location"), location);
    m_menuSpecs.insert(action.value(QStringLiteral("id")).toString(), action);
    m_menuCallbacks.insert(action.value(QStringLiteral("id")).toString(), callback);
    if (m_manager)
        m_manager->notifyMenuActionsChanged();
}

QVariantList PluginRuntime::menuActions() const
{
    QVariantList result;
    for (auto it = m_menuSpecs.cbegin(); it != m_menuSpecs.cend(); ++it)
        result.append(it.value());
    return result;
}

bool PluginRuntime::invokeMenuAction(const QString &actionId, const QVariantMap &context)
{
    auto callback = m_menuCallbacks.constFind(actionId);
    if (callback == m_menuCallbacks.cend() || !m_engine)
        return false;
    const QJSValue argument = m_engine->toScriptValue(context);
    const QJSValue result = callback.value().call({argument});
    if (result.isError())
        qWarning().noquote() << "[plugin:" << m_manifest.id << "] menu action:"
                             << result.toString();
    return true;
}

void PluginRuntime::registerWindow(PluginWindow *window)
{
    if (window && !m_windows.contains(window))
        m_windows.append(window);
}

void PluginRuntime::removeWindow(PluginWindow *window)
{
    m_windows.removeAll(window);
}

void PluginRuntime::log(const QString &level, const QString &message) const
{
    if (level == QLatin1String("error"))
        qCritical().noquote() << "[plugin:" << m_manifest.id << "]" << message;
    else if (level == QLatin1String("warn"))
        qWarning().noquote() << "[plugin:" << m_manifest.id << "]" << message;
    else
        qInfo().noquote() << "[plugin:" << m_manifest.id << "]" << message;
}
