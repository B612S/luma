#include "KWinBridge.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace {

constexpr auto kService = "org.kde.KWin";
constexpr auto kScriptingPath = "/Scripting";
constexpr auto kScriptingInterface = "org.kde.kwin.Scripting";
constexpr auto kScriptInterface = "org.kde.kwin.Script";

// Title of the window while picture-in-picture is active (see Main.qml). The
// KWin script uses it to distinguish the PiP window from the normal one.
constexpr auto kPipTitle = "Luma PiP";

constexpr auto kWatcherScript = R"(
function isLumaWindow(window) {
    return window.resourceClass === "luma" || window.resourceName === "luma";
}

function update(window) {
    if (!isLumaWindow(window))
        return;
    const keepAbove = window.caption.indexOf("Luma PiP") === 0;
    if (window.keepAbove !== keepAbove)
        window.keepAbove = keepAbove;
}

function watch(window) {
    if (!isLumaWindow(window))
        return;
    update(window);
    window.captionChanged.connect(function () {
        update(window);
    });
}

workspace.windowList().forEach(watch);
workspace.windowAdded.connect(watch);
)";

QString cacheDirectory()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                        + QStringLiteral("/kwin");
    return QDir().mkpath(dir) ? dir : QString();
}

QString writeScript(const QString &name, const QString &source)
{
    const QString path = cacheDirectory() + QLatin1Char('/') + name + QStringLiteral(".js");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    if (file.write(source.toUtf8()) < 0)
        return QString();
    file.close();
    return path;
}

int loadScript(const QString &path, const QString &name)
{
    QDBusInterface scripting(QString::fromLatin1(kService),
                             QString::fromLatin1(kScriptingPath),
                             QString::fromLatin1(kScriptingInterface));
    if (!scripting.isValid())
        return -1;
    // A previous run may have left a script with this name loaded; KWin refuses
    // to load a second one under the same name.
    scripting.call(QStringLiteral("unloadScript"), name);
    const QDBusMessage reply = scripting.call(QStringLiteral("loadScript"), path, name);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return -1;
    bool ok = false;
    const int id = reply.arguments().constFirst().toInt(&ok);
    return ok ? id : -1;
}

bool runScript(int id)
{
    QDBusInterface script(QString::fromLatin1(kService),
                          QStringLiteral("/Scripting/Script") + QString::number(id),
                          QString::fromLatin1(kScriptInterface));
    if (!script.isValid())
        return false;
    return script.call(QStringLiteral("run")).type() != QDBusMessage::ErrorMessage;
}

void unloadScript(const QString &name)
{
    QDBusInterface scripting(QString::fromLatin1(kService),
                             QString::fromLatin1(kScriptingPath),
                             QString::fromLatin1(kScriptingInterface));
    if (scripting.isValid())
        scripting.call(QStringLiteral("unloadScript"), name);
}

} // namespace

KWinBridge::KWinBridge(QObject *parent)
    : QObject(parent)
{
    // The window is gone with the application, but do not leave the script
    // behind if we can help it.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { unloadWatcher(); });
}

KWinBridge::~KWinBridge()
{
    unloadWatcher();
}

bool KWinBridge::available() const
{
    // Escape hatch for debugging: LUMA_NO_KWIN_KEEP_ABOVE=1 turns the whole
    // integration off, so PiP can be compared with and without it.
    if (qEnvironmentVariableIsSet("LUMA_NO_KWIN_KEEP_ABOVE"))
        return false;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    if (!bus.interface()->isServiceRegistered(QString::fromLatin1(kService)))
        return false;
    QDBusInterface scripting(QString::fromLatin1(kService),
                             QString::fromLatin1(kScriptingPath),
                             QString::fromLatin1(kScriptingInterface));
    return scripting.isValid();
}

void KWinBridge::setPipActive(bool active)
{
    if (active) {
        loadWatcher();
        return;
    }
    // update() in the script keeps only the "Luma PiP" title above the others,
    // so unloading it is safe even if a window is re-created right after.
    unloadWatcher();
}

bool KWinBridge::loadWatcher()
{
    if (!m_watcherName.isEmpty())
        return true;
    if (!available())
        return false;

    // A unique plugin name per load keeps KWin from confusing the new script
    // with a previous instance that is still awaiting deletion. The file name
    // stays the same, so the cache does not fill up with copies.
    const QString name = QStringLiteral("luma-pip-%1").arg(++m_loadCount);
    const QString path = writeScript(QStringLiteral("luma-pip"), QString::fromUtf8(kWatcherScript));
    if (path.isEmpty())
        return false;
    const int id = loadScript(path, name);
    if (id < 0 || !runScript(id)) {
        unloadScript(name);
        return false;
    }
    m_watcherName = name;
    return true;
}

void KWinBridge::unloadWatcher()
{
    if (m_watcherName.isEmpty())
        return;
    unloadScript(m_watcherName);
    m_watcherName.clear();
}
