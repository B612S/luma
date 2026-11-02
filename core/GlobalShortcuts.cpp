#include "GlobalShortcuts.hpp"
#include "ShortcutAction.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QKeySequence>
#include <QDBusMetaType>
#include <QDebug>
#include <QUuid>

namespace {

struct PortalShortcut {
    QString id;
    QVariantMap properties;
};

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
    return argument;
}

constexpr auto kService = "org.freedesktop.portal.Desktop";
constexpr auto kPath = "/org/freedesktop/portal/desktop";
constexpr auto kInterface = "org.freedesktop.portal.GlobalShortcuts";
constexpr auto kRequestInterface = "org.freedesktop.portal.Request";

QString token()
{
    QString value = QUuid::createUuid().toString(QUuid::WithoutBraces);
    value.remove(QLatin1Char('-'));
    return QStringLiteral("luma") + value;
}
} // namespace

Q_DECLARE_METATYPE(PortalShortcut)


GlobalShortcuts::GlobalShortcuts(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        m_errorString = tr("D-Bus session bus is unavailable");
        return;
    }

    const bool serviceRegistered = bus.interface()
        && bus.interface()->isServiceRegistered(QString::fromLatin1(kService));
    if (!serviceRegistered) {
        m_errorString = tr("The desktop portal is not running");
        return;
    }

    // Associate this D-Bus peer with luma.desktop before any portal call. Qt's
    // platform services may try the same registration later; that duplicate is
    // harmless and is filtered from the log.
    const QString appId = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("luma") : QCoreApplication::applicationName();
    QDBusMessage registerMessage = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QStringLiteral("org.freedesktop.host.portal.Registry"),
        QStringLiteral("Register"));
    registerMessage << appId << QVariantMap();
    const QDBusMessage registerReply = bus.call(registerMessage, QDBus::Block, 1000);
    if (registerReply.type() == QDBusMessage::ErrorMessage) {
        const QString error = registerReply.errorMessage();
        if (!error.contains(QStringLiteral("already"), Qt::CaseInsensitive)
                && !registerReply.errorName().contains(QStringLiteral("Already"),
                                                       Qt::CaseInsensitive)) {
            m_errorString = error;
        }
    }

    QDBusInterface iface(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                         QString::fromLatin1(kInterface), bus);
    m_available = iface.isValid();
    if (!m_available) {
        m_errorString = iface.lastError().message();
        return;
    }
    if (!m_available) {
        m_errorString = tr("The desktop portal does not provide GlobalShortcuts");
        return;
    }

    bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                QString::fromLatin1(kInterface), QStringLiteral("Activated"),
                this, SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
}

GlobalShortcuts::~GlobalShortcuts() = default;

QString GlobalShortcuts::requestPath(const QString &requestToken) const
{
    QString sender = QDBusConnection::sessionBus().baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
        .arg(sender, requestToken);
}

void GlobalShortcuts::setShortcuts(const QList<GlobalShortcut> &shortcuts)
{
    if (!m_available) return;

    m_shortcuts = shortcuts;
    if (m_shortcuts.isEmpty()) {
        closeSession();
        return;
    }

    // BindShortcuts may only be called once per session. A changed binding
    // therefore gets a fresh session.
    if (!m_sessionHandle.isEmpty())
        closeSession();
    createSession();
}

void GlobalShortcuts::createSession()
{
    const QString handleToken = token();
    m_pendingPath = requestPath(handleToken);
    m_pending = PendingRequest::CreateSession;

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), handleToken);
    options.insert(QStringLiteral("session_handle_token"), token());
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString::fromLatin1(kService), m_pendingPath,
                QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
                this, SLOT(onResponse(uint,QVariantMap)));

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QStringLiteral("CreateSession"));
    message << options;
    bus.send(message);
}

QDBusArgument GlobalShortcuts::buildShortcutsArgument() const
{
    QDBusArgument argument;
    argument.beginArray(QMetaType::fromType<PortalShortcut>());
    for (const GlobalShortcut &shortcut : m_shortcuts) {
        PortalShortcut item;
        item.id = shortcut.id;
        item.properties.insert(QStringLiteral("description"), shortcut.description);
        const QString trigger = portalTrigger(shortcut.trigger);
        if (!trigger.isEmpty())
            item.properties.insert(QStringLiteral("preferred_trigger"), trigger);
        argument << item;
    }
    argument.endArray();
    return argument;
}

void GlobalShortcuts::bindShortcuts()
{
    if (m_sessionHandle.isEmpty()) return;

    const QString handleToken = token();
    m_pendingPath = requestPath(handleToken);
    m_pending = PendingRequest::BindShortcuts;

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), handleToken);

    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.connect(QString::fromLatin1(kService), m_pendingPath,
                QString::fromLatin1(kRequestInterface), QStringLiteral("Response"),
                this, SLOT(onResponse(uint,QVariantMap)));

    const QDBusArgument argument = buildShortcutsArgument();
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), QString::fromLatin1(kPath),
        QString::fromLatin1(kInterface), QStringLiteral("BindShortcuts"));
    message << QVariant::fromValue(QDBusObjectPath(m_sessionHandle))
            << QVariant::fromValue(argument)
            << QString()
            << options;
    bus.send(message);
}

void GlobalShortcuts::closeSession()
{
    if (m_sessionHandle.isEmpty()) return;

    // Close belongs to the Session object itself, not to GlobalShortcuts.
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), m_sessionHandle,
        QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
    QDBusConnection::sessionBus().send(message);
    m_sessionHandle.clear();
}

QString GlobalShortcuts::portalTrigger(const QString &text) const
{
    return shortcutPortalTrigger(text);
}

void GlobalShortcuts::onResponse(uint response, const QVariantMap &results)
{
    const PendingRequest pending = m_pending;
    m_pending = PendingRequest::None;
    m_pendingPath.clear();

    if (response != 0) {
        m_errorString = tr("The desktop portal rejected the shortcut request");
        emit availabilityChanged();
        return;
    }

    if (pending == PendingRequest::CreateSession) {
        const QVariant value = results.value(QStringLiteral("session_handle"));
        if (value.canConvert<QDBusObjectPath>())
            m_sessionHandle = value.value<QDBusObjectPath>().path();
        else
            m_sessionHandle = value.toString();
        if (m_sessionHandle.isEmpty()) {
            m_errorString = tr("The desktop portal returned no shortcut session");
            emit availabilityChanged();
            return;
        }
        bindShortcuts();
    } else if (pending == PendingRequest::BindShortcuts) {
        // KDE's portal keeps previously-known actions with an empty active
        // shortcut after a rejected/cancelled dialog and then skips the dialog
        // on later sessions. Repair only that empty state; do not override
        // shortcuts the user customized in System Settings.
        applyKGlobalAccelFallback();
    }
}

void GlobalShortcuts::applyKGlobalAccelFallback() const
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()
            || !bus.interface()->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel")))
        return;

    const QString component = QCoreApplication::applicationName().isEmpty()
        ? QStringLiteral("luma") : QCoreApplication::applicationName();
    const QString friendlyComponent = QStringLiteral("Luma");

    for (const GlobalShortcut &shortcut : m_shortcuts) {
        if (shortcut.id.isEmpty() || shortcut.trigger.isEmpty()) continue;

        const QStringList actionId = {component, shortcut.id,
                                      friendlyComponent, shortcut.description};

        QDBusMessage query = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"),
            QStringLiteral("org.kde.KGlobalAccel"), QStringLiteral("shortcut"));
        query << actionId;
        const QDBusMessage reply = bus.call(query, QDBus::Block, 500);
        if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
            continue;

        QList<int> active;
        const QVariant value = reply.arguments().first();
        if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
            const QDBusArgument argument = value.value<QDBusArgument>();
            argument >> active;
        } else {
            active = value.value<QList<int>>();
        }
        if (!active.isEmpty()) continue;

        const QKeySequence sequence = QKeySequence::fromString(
            shortcut.trigger, QKeySequence::PortableText);
        if (sequence.isEmpty()) continue;

        QDBusMessage set = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"),
            QStringLiteral("org.kde.KGlobalAccel"), QStringLiteral("setForeignShortcut"));
        set << actionId << QVariant::fromValue(QList<int>{sequence[0].toCombined()});
        const QDBusMessage setReply = bus.call(set, QDBus::Block, 500);
    }
}

void GlobalShortcuts::onActivated(const QDBusObjectPath &sessionHandle,
                                  const QString &shortcutId,
                                  qulonglong timestamp,
                                  const QVariantMap &options)
{
    Q_UNUSED(sessionHandle);
    Q_UNUSED(timestamp);
    Q_UNUSED(options);
    if (!shortcutId.isEmpty())
        emit activated(shortcutId);
}
