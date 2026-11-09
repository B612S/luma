#include "PluginRuntime.hpp"
#include "PluginApi.hpp"
#include "PluginManager.hpp"
#include "PluginWindow.hpp"
#include <QFile>
#include <QJSEngine>
#include <QKeySequence>
#include <QKeyCombination>
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
    luma.setProperty(QStringLiteral("i18n"), m_engine->newQObject(m_api->i18nApi()));
    luma.setProperty(QStringLiteral("stats"), m_engine->newQObject(m_api->statsApi()));
    luma.setProperty(QStringLiteral("shortcuts"), m_engine->newQObject(m_api->shortcutsApi()));
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

static QString normalizedPluginShortcut(const QString &key)
{
    const QKeySequence sequence = QKeySequence::fromString(
        key.trimmed(), QKeySequence::PortableText);
    if (sequence.isEmpty())
        return {};
    return sequence.toString(QKeySequence::PortableText);
}

QVariantList PluginRuntime::statsLines() const
{
    QVariantList result;
    for (const QVariantMap &line : m_stats)
        result.append(line);
    return result;
}

QVariantList PluginRuntime::statsPages() const
{
    QVariantList result;
    for (const QVariantMap &page : m_statsPages)
        result.append(page);
    return result;
}

QVariantMap PluginRuntime::setStatsPage(const QString &id, const QString &title,
                                        const QVariantList &content,
                                        int preferredSlot)
{
    if (id.isEmpty() || !m_manager)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("invalid statistics page")}};

    const int slot = m_manager->allocateStatsPageSlot(m_manifest.id, id, preferredSlot);
    if (slot < 0) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"),
                 QStringLiteral("No free statistics key (2-9, 0 for the tenth slot). "
                                "Register the plugin without a statistics page.")}};
    }

    QVariantMap page;
    page.insert(QStringLiteral("id"), id);
    page.insert(QStringLiteral("pluginId"), m_manifest.id);
    page.insert(QStringLiteral("title"), title);
    page.insert(QStringLiteral("slot"), slot);
    page.insert(QStringLiteral("key"),
                slot == 10 ? QStringLiteral("0") : QString::number(slot));
    page.insert(QStringLiteral("content"), content);
    m_statsPages.insert(id, page);
    m_manager->notifyStatsPagesChanged();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("key"), page.value(QStringLiteral("key"))},
            {QStringLiteral("slot"), slot}};
}

void PluginRuntime::removeStatsPage(const QString &id)
{
    if (m_statsPages.remove(id)) {
        if (m_manager) {
            m_manager->releaseStatsPageSlot(m_manifest.id, id);
            m_manager->notifyStatsPagesChanged();
        }
    }
}

void PluginRuntime::clearStatsPages()
{
    if (m_statsPages.isEmpty())
        return;
    if (m_manager) {
        for (auto it = m_statsPages.cbegin(); it != m_statsPages.cend(); ++it)
            m_manager->releaseStatsPageSlot(m_manifest.id, it.key());
    }
    m_statsPages.clear();
    if (m_manager)
        m_manager->notifyStatsPagesChanged();
}

void PluginRuntime::setStat(const QString &key, const QString &label,
                            const QVariant &value)
{
    if (key.isEmpty())
        return;
    QVariantMap line;
    line.insert(QStringLiteral("pluginId"), m_manifest.id);
    line.insert(QStringLiteral("key"), key);
    line.insert(QStringLiteral("label"), label.isEmpty() ? key : label);
    line.insert(QStringLiteral("value"), value);
    m_stats.insert(key, line);
    if (m_manager)
        m_manager->notifyStatsChanged();
}

void PluginRuntime::removeStat(const QString &key)
{
    if (m_stats.remove(key) && m_manager)
        m_manager->notifyStatsChanged();
}

void PluginRuntime::clearStats()
{
    if (m_stats.isEmpty())
        return;
    m_stats.clear();
    if (m_manager)
        m_manager->notifyStatsChanged();
}

bool PluginRuntime::addShortcut(const QString &key, const QJSValue &callback)
{
    const QString normalized = normalizedPluginShortcut(key);
    if (normalized.isEmpty()
            || (normalized.size() == 1 && normalized.at(0).isDigit())
            || !callback.isCallable())
        return false;
    m_shortcuts.insert(normalized, callback);
    return true;
}

void PluginRuntime::removeShortcut(const QString &key)
{
    m_shortcuts.remove(normalizedPluginShortcut(key));
}

bool PluginRuntime::invokeShortcut(int key, Qt::KeyboardModifiers modifiers)
{
    const QKeySequence pressed(QKeyCombination(modifiers, static_cast<Qt::Key>(key)));
    const QString normalized = pressed.toString(QKeySequence::PortableText);
    const QJSValue callback = m_shortcuts.value(normalized);
    if (!callback.isCallable())
        return false;
    QVariantMap payload;
    payload.insert(QStringLiteral("key"), normalized);
    const QJSValue result = callback.call({m_engine->toScriptValue(payload)});
    if (result.isError())
        qWarning().noquote() << "[plugin:" << m_manifest.id << "] shortcut:"
                             << result.toString();
    return true;
}

bool PluginRuntime::addShortcutAction(const QString &id, const QJSValue &callback)
{
    if (id.isEmpty() || !callback.isCallable())
        return false;
    bool declared = false;
    for (const PluginShortcutDef &def : m_manifest.shortcuts) {
        if (def.id == id) { declared = true; break; }
    }
    if (!declared) {
        qWarning().noquote() << "[plugin:" << m_manifest.id
                             << "] undeclared shortcut action:" << id;
        return false;
    }
    m_shortcutActions.insert(id, callback);
    return true;
}

bool PluginRuntime::invokeShortcutAction(const QString &id)
{
    const QJSValue callback = m_shortcutActions.value(id);
    if (!callback.isCallable())
        return false;
    QVariantMap payload;
    payload.insert(QStringLiteral("id"), id);
    const QJSValue result = callback.call({m_engine->toScriptValue(payload)});
    if (result.isError())
        qWarning().noquote() << "[plugin:" << m_manifest.id << "] shortcut action:"
                             << result.toString();
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
    m_shortcuts.clear();
    m_shortcutActions.clear();
    if (!m_stats.isEmpty()) {
        m_stats.clear();
        if (m_manager)
            m_manager->notifyStatsChanged();
    }
    if (!m_statsPages.isEmpty()) {
        m_statsPages.clear();
        if (m_manager)
            m_manager->notifyStatsPagesChanged();
    }
    for (PluginWindow *window : std::as_const(m_windows)) {
        delete window;
    }
    m_windows.clear();
    m_handlers.clear();
    m_menuCallbacks.clear();
    m_menuSpecs.clear();
    if (m_api) {
        if (m_api->processApi())
            m_api->processApi()->abortAll();
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
