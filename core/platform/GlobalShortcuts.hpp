#pragma once

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

// One shortcut registered through org.freedesktop.portal.GlobalShortcuts.
struct GlobalShortcut {
    QString id;
    QString description;
    QString trigger; // portable text, e.g. "Ctrl+Alt+M"
};

// Best-effort XDG Desktop Portal GlobalShortcuts client. When the portal or
// the interface is unavailable, the object stays valid but does nothing.
class GlobalShortcuts : public QObject {
    Q_OBJECT

public:
    explicit GlobalShortcuts(QObject *parent = nullptr);
    ~GlobalShortcuts() override;

    bool available() const { return m_available; }
    QString errorString() const { return m_errorString; }

    void setShortcuts(const QList<GlobalShortcut> &shortcuts);

signals:
    void activated(const QString &id);
    void availabilityChanged();

private slots:
    void onResponse(uint response, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &sessionHandle, const QString &shortcutId,
                     qulonglong timestamp, const QVariantMap &options);

private:
    enum class PendingRequest { None, CreateSession, BindShortcuts };

    void createSession();
    void bindShortcuts();
    void closeSession();
    void applyKGlobalAccelFallback() const;
    QString requestPath(const QString &token) const;
    QDBusArgument buildShortcutsArgument() const;
    QString portalTrigger(const QString &text) const;

    bool m_available = false;
    QString m_errorString;
    QString m_sessionHandle;
    QList<GlobalShortcut> m_shortcuts;
    PendingRequest m_pending = PendingRequest::None;
    QString m_pendingPath;
};
