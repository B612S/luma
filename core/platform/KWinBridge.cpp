#include "platform/KWinBridge.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

namespace {

constexpr auto kService = "org.kde.KWin";
constexpr auto kScriptingPath = "/Scripting";
constexpr auto kScriptingInterface = "org.kde.kwin.Scripting";
constexpr auto kScriptInterface = "org.kde.kwin.Script";

constexpr int kDbusTimeoutMs = 500;

QString watcherScriptFor(int pid, const QString &service)
{
    return QStringLiteral(R"(
var lumaPid = %1;
var lumaService = "%2";

function isLumaWindow(window) {
    return window.pid === lumaPid &&
           (window.resourceClass === "luma" || window.resourceName === "luma");
}

function geometryKey(window) {
    return window.caption;
}

var savedGeometries = {};

function remember(window) {
    if (!isLumaWindow(window) || window.hidden || window.minimized)
        return;
    savedGeometries[geometryKey(window)] = Object.assign({}, window.frameGeometry);
}

function restore(window) {
    if (!isLumaWindow(window))
        return;
    var geometry = savedGeometries[geometryKey(window)];
    if (geometry)
        window.frameGeometry = geometry;
}

function savePip(window) {
    if (!isLumaWindow(window) || window.caption.indexOf("Luma PiP") !== 0)
        return;
    if (window.hidden || window.minimized)
        return;
    var g = window.frameGeometry;
    callDBus(lumaService, "/LumaKWin", "org.luma.KWin", "savePipGeometry",
             Math.round(g.x), Math.round(g.y), Math.round(g.width), Math.round(g.height));
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
    if (savedGeometries[geometryKey(window)])
        restore(window);
    else
        remember(window);
    window.frameGeometryChanged.connect(function () {
        if (!window.move && !window.resize) {
            remember(window);
            savePip(window);
        }
    });
    window.interactiveMoveResizeFinished.connect(function () {
        remember(window);
        savePip(window);
    });
    window.hiddenChanged.connect(function () {
        if (!window.hidden)
            restore(window);
    });
    window.captionChanged.connect(function () {
        update(window);
        restore(window);
    });
}

workspace.windowList().forEach(watch);
workspace.windowAdded.connect(watch);
)").arg(pid).arg(service);
}

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

QDBusMessage callKWin(const QString &path, const QString &interface,
                     const QString &method, const QVariantList &arguments = {})
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kService), path, interface, method);
    message.setArguments(arguments);
    return QDBusConnection::sessionBus().call(message, QDBus::Block, kDbusTimeoutMs);
}

int loadScript(const QString &path, const QString &name)
{
    callKWin(QString::fromLatin1(kScriptingPath), QString::fromLatin1(kScriptingInterface),
             QStringLiteral("unloadScript"), {name});
    const QDBusMessage reply = callKWin(QString::fromLatin1(kScriptingPath),
                                        QString::fromLatin1(kScriptingInterface),
                                        QStringLiteral("loadScript"), {path, name});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return -1;
    bool ok = false;
    const int id = reply.arguments().constFirst().toInt(&ok);
    return ok ? id : -1;
}

bool runScript(int id)
{
    const QDBusMessage reply = callKWin(
        QStringLiteral("/Scripting/Script") + QString::number(id),
        QString::fromLatin1(kScriptInterface), QStringLiteral("run"));
    return reply.type() != QDBusMessage::ErrorMessage;
}

void unloadScript(const QString &name)
{
    callKWin(QString::fromLatin1(kScriptingPath), QString::fromLatin1(kScriptingInterface),
             QStringLiteral("unloadScript"), {name});
}

QString watcherNameForPid(int pid)
{
    return QStringLiteral("luma-pip-%1").arg(pid);
}

QString moveNameForPid(int pid)
{
    return QStringLiteral("luma-pip-move-%1").arg(pid);
}

} // namespace

KWinBridge::KWinBridge(QObject *parent)
    : QObject(parent)
{
    m_dbusService = QStringLiteral("org.luma.KWin.p%1")
                        .arg(QCoreApplication::applicationPid());
    registerDbus();
    // Start at application launch, not only on the first PiP: normal-window
    // hide/show also benefits from the runtime geometry memory.
    QTimer::singleShot(0, this, [this]() { loadWatcher(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { unloadWatcher(); });
}

KWinBridge::~KWinBridge()
{
    unloadWatcher();
    if (!m_dbusService.isEmpty()) {
        QDBusConnection::sessionBus().unregisterObject(QStringLiteral("/LumaKWin"));
        QDBusConnection::sessionBus().unregisterService(m_dbusService);
    }
}

void KWinBridge::registerDbus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    if (!bus.registerService(m_dbusService)) return;
    bus.registerObject(QStringLiteral("/LumaKWin"), this,
                       QDBusConnection::ExportAllSlots);
}

void KWinBridge::savePipGeometry(int x, int y, int width, int height)
{
    const QRect geometry(x, y, width, height);
    if (geometry.width() < 160 || geometry.height() < 90) return;
    m_runtimePipGeometry = geometry;
}

void KWinBridge::restorePipGeometry()
{
    if (!available() || !m_runtimePipGeometry.isValid())
        return;

    const int pid = QCoreApplication::applicationPid();
    const QString name = moveNameForPid(pid);
    const QRect geometry = m_runtimePipGeometry;
    // QML calls this while show()/resize is still queued; give KWin time to
    // manage the recreated PiP window.
    QTimer::singleShot(180, this, [this, pid, name, geometry]() {
        const QString path = writeScript(name, QStringLiteral(R"(
var targetPid = %1;
var targetGeometry = { x: %2, y: %3, width: %4, height: %5 };
workspace.windowList().forEach(function (window) {
    if (window.pid !== targetPid || window.caption.indexOf("Luma PiP") !== 0)
        return;
    var g = Object.assign({}, window.frameGeometry);
    g.x = targetGeometry.x;
    g.y = targetGeometry.y;
    g.width = targetGeometry.width;
    g.height = targetGeometry.height;
    window.frameGeometry = g;
});
)").arg(pid).arg(geometry.x()).arg(geometry.y())
                                                       .arg(geometry.width()).arg(geometry.height()));
        if (path.isEmpty())
            return;
        const int id = loadScript(path, name);
        if (id < 0 || !runScript(id)) {
            unloadScript(name);
            QFile::remove(path);
            return;
        }
        QTimer::singleShot(500, this, [name, path]() {
            unloadScript(name);
            QFile::remove(path);
        });
    });
}

bool KWinBridge::available() const
{
    if (qEnvironmentVariableIsSet("LUMA_NO_KWIN_KEEP_ABOVE"))
        return false;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    return bus.interface()->isServiceRegistered(QString::fromLatin1(kService));
}

void KWinBridge::cleanupStaleArtifacts()
{
    const QString dirPath = cacheDirectory();
    if (dirPath.isEmpty()) return;
    const QDir dir(dirPath);
    const QRegularExpression filePattern(
        QStringLiteral("^luma-pip-(?:move-)?(\\d+)\\.js$"));
    const qint64 currentPid = QCoreApplication::applicationPid();
    const QStringList files = dir.entryList({QStringLiteral("luma-pip-*.js")},
                                            QDir::Files | QDir::NoDotAndDotDot);
    for (const QString &file : files) {
        const QRegularExpressionMatch match = filePattern.match(file);
        if (!match.hasMatch()) {
            QFile::remove(dir.filePath(file));
            continue;
        }
        const qint64 pid = match.captured(1).toLongLong();
        if (pid == currentPid || QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid)))
            continue;
        unloadScript(watcherNameForPid(static_cast<int>(pid)));
        unloadScript(moveNameForPid(static_cast<int>(pid)));
        QFile::remove(dir.filePath(file));
    }
    // Older Luma versions used luma-pip-<counter> names and a shared
    // luma-pip.js file. Remove those leftovers once at startup.
    for (int i = 1; i <= 64; ++i) {
        unloadScript(QStringLiteral("luma-pip-%1").arg(i));
        QFile::remove(dir.filePath(QStringLiteral("luma-pip-%1.js").arg(i)));
    }
    QFile::remove(dir.filePath(QStringLiteral("luma-pip.js")));
    if (dir.isEmpty()) QDir().rmdir(dirPath);
}

void KWinBridge::setPipActive(bool active)
{
    if (active)
        loadWatcher();
}

bool KWinBridge::loadWatcher()
{
    if (!m_watcherName.isEmpty())
        return true;
    if (!available())
        return false;

    cleanupStaleArtifacts();
    const int pid = QCoreApplication::applicationPid();
    const QString name = watcherNameForPid(pid);
    m_watcherPath = writeScript(name, watcherScriptFor(pid, m_dbusService));
    if (m_watcherPath.isEmpty())
        return false;
    const int id = loadScript(m_watcherPath, name);
    if (id < 0 || !runScript(id)) {
        unloadScript(name);
        QFile::remove(m_watcherPath);
        m_watcherPath.clear();
        return false;
    }
    m_watcherName = name;
    return true;
}

void KWinBridge::unloadWatcher()
{
    if (!m_watcherName.isEmpty()) {
        unloadScript(m_watcherName);
        m_watcherName.clear();
    }
    if (!m_watcherPath.isEmpty()) {
        QFile::remove(m_watcherPath);
        m_watcherPath.clear();
    }
    const int pid = QCoreApplication::applicationPid();
    unloadScript(moveNameForPid(pid));
    const QString dirPath = cacheDirectory();
    if (!dirPath.isEmpty())
        QFile::remove(dirPath + QStringLiteral("/luma-pip-move-%1.js").arg(pid));
    if (!dirPath.isEmpty()) {
        QDir dir(dirPath);
        if (dir.isEmpty()) QDir().rmdir(dirPath);
    }
}
