#include "PluginWindow.hpp"
#include "PluginManager.hpp"
#include "PluginRuntime.hpp"
#include <QDebug>
#include <QJSEngine>

PluginWindow::PluginWindow(PluginManager *manager, PluginRuntime *runtime,
                           const QVariantMap &spec, const QJSValue &callback,
                           QObject *parent)
    : QObject(parent), m_manager(manager), m_runtime(runtime), m_callback(callback),
      m_title(spec.value(QStringLiteral("title")).toString()),
      m_content(spec.value(QStringLiteral("content")).toList()),
      m_actions(spec.value(QStringLiteral("actions")).toList())
{
}

PluginWindow::~PluginWindow()
{
    if (m_manager)
        m_manager->removeWindow(this);
    if (m_runtime)
        m_runtime->removeWindow(this);
}

QString PluginWindow::pluginId() const
{
    return m_runtime ? m_runtime->manifest().id : QString();
}

void PluginWindow::close()
{
    if (m_closed)
        return;
    m_closed = true;
    if (m_callback.isCallable()) {
        QJSEngine *engine = m_runtime ? m_runtime->findChild<QJSEngine *>() : nullptr;
        if (engine) {
            QVariantMap payload;
            payload.insert(QStringLiteral("action"), QStringLiteral("__closed"));
            payload.insert(QStringLiteral("pluginId"), pluginId());
            const QJSValue result = m_callback.call({engine->toScriptValue(payload)});
            if (result.isError())
                qWarning().noquote() << "[plugin:" << pluginId()
                                     << "] closed callback:" << result.toString();
        }
    }
    emit changed();
    emit closed();
    deleteLater();
}

void PluginWindow::submit(const QString &actionId, const QVariantMap &values)
{
    if (!m_callback.isCallable())
        return;
    QVariantMap payload = values;
    payload.insert(QStringLiteral("action"), actionId);
    payload.insert(QStringLiteral("pluginId"), pluginId());
    QJSEngine *engine = m_runtime ? m_runtime->findChild<QJSEngine *>() : nullptr;
    if (!engine)
        return;
    const QJSValue result = m_callback.call({engine->toScriptValue(payload)});
    if (result.isError())
        qWarning().noquote() << "[plugin:" << pluginId() << "] window action:"
                             << result.toString();
}

void PluginWindow::setTitle(const QString &title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit changed();
}

void PluginWindow::setContent(const QVariantList &content)
{
    m_content = content;
    emit changed();
}

void PluginWindow::setActions(const QVariantList &actions)
{
    m_actions = actions;
    emit changed();
}
