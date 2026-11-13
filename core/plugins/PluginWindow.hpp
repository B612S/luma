#pragma once
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class PluginManager;
class PluginRuntime;

// Host-rendered window declared by a plugin through luma.ui.window(). The
// plugin never supplies QML: `content` is a list of native control
// descriptions (heading/label/separator/text/textarea/switch/select/button)
// and `actions` is the footer button list.
class PluginWindow : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QVariantList content READ content NOTIFY changed)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(QString pluginId READ pluginId CONSTANT)
    Q_PROPERTY(bool isClosed READ isClosed NOTIFY changed)

public:
    PluginWindow(PluginManager *manager, PluginRuntime *runtime,
                 const QVariantMap &spec, const QJSValue &callback,
                 QObject *parent = nullptr);
    ~PluginWindow() override;

    QString title() const { return m_title; }
    QVariantList content() const { return m_content; }
    QVariantList actions() const { return m_actions; }
    QString pluginId() const;
    bool isClosed() const { return m_closed; }

    Q_INVOKABLE void close();
    Q_INVOKABLE void submit(const QString &actionId, const QVariantMap &values);
    Q_INVOKABLE void setTitle(const QString &title);
    Q_INVOKABLE void setContent(const QVariantList &content);
    Q_INVOKABLE void setActions(const QVariantList &actions);

signals:
    void changed();
    void closed();

private:
    QPointer<PluginManager> m_manager;
    QPointer<PluginRuntime> m_runtime;
    QJSValue m_callback;
    QString m_title;
    QVariantList m_content;
    QVariantList m_actions;
    bool m_closed = false;
};
