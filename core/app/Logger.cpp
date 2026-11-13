#include "app/Logger.hpp"
#include <QDebug>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <cstdlib>

namespace {
std::atomic_bool g_loggerAlive{true};
// Qt messages can arrive from different threads (mpv, renderer),
// so writes to the files and stderr are serialized.
QMutex g_logMutex;

QString normalizedLogMode(QString mode)
{
    mode = mode.trimmed().toLower();
    if (mode == QLatin1String("none") || mode == QLatin1String("off"))
        return QStringLiteral("off");
    if (mode == QLatin1String("error") || mode == QLatin1String("warn")
            || mode == QLatin1String("warning"))
        return QStringLiteral("error");
    if (mode == QLatin1String("debug"))
        return QStringLiteral("debug");
    return QStringLiteral("info");
}
} // namespace

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger(QObject *parent) : QObject(parent), m_file(nullptr) {
    // ~/.cache/luma/{luma,mpv,yt-dlp}.log
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cacheDir.isEmpty())
        cacheDir = QDir::homePath() + "/.cache";
    m_cacheDir = cacheDir + "/luma";

    QDir().mkpath(m_cacheDir);

    m_file = new QFile(m_cacheDir + "/luma.log");
    if (m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        m_stream.setDevice(m_file);
}

Logger::~Logger() {
    // Stop new messages first, then wait for a message that is already being
    // written before closing the files (messages arrive from other threads).
    g_loggerAlive.store(false);
    QMutexLocker locker(&g_logMutex);
    if (m_file) {
        m_stream.flush();
        m_stream.setDevice(nullptr);
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    for (QFile *file : m_componentFiles) {
        if (!file) continue;
        file->close();
        delete file;
    }
    m_componentFiles.clear();
}

void Logger::installMessageHandler() {
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &message) {
        Level level = Info;
        switch (type) {
            case QtDebugMsg:    level = Debug; break;
            case QtInfoMsg:     level = Info; break;
            case QtWarningMsg:  level = Warning; break;
            case QtCriticalMsg: level = Critical; break;
            case QtFatalMsg:    level = Critical; break;
        }

        // Qt's platform services may try to register the app with the portal
        // after Luma has already used a portal call. The app id is associated;
        // this duplicate registration is harmless and only creates log noise.
        if (context.category
                && qstrcmp(context.category, "qt.qpa.services") == 0
                && message.contains(QStringLiteral("Connection already associated with an application ID")))
            return;

        QString text = message;
        if (context.category && *context.category)
            text = QStringLiteral("[%1] %2").arg(QString::fromUtf8(context.category), message);
        if (context.file)
            text += QStringLiteral(" (%1:%2)").arg(QString::fromUtf8(context.file)).arg(context.line);

        if (g_loggerAlive)
            Logger::instance().log(level, text);

        if (type == QtFatalMsg)
            std::abort();
    });
}

void Logger::setMode(const QString &mode) {
    const QString normalized = normalizedLogMode(mode);
    int minimum = Info;
    if (normalized == QLatin1String("off"))
        minimum = 4;
    else if (normalized == QLatin1String("error"))
        minimum = Warning;
    else if (normalized == QLatin1String("debug"))
        minimum = Debug;
    m_minLevel.store(minimum);
}

void Logger::setComponentMode(const QString &component, const QString &mode) {
    if (component.isEmpty()) return;
    const QString normalized = normalizedLogMode(mode);

    QMutexLocker locker(&g_logMutex);
    m_componentModes[component] = normalized;
    if (normalized == QLatin1String("off")) {
        QFile *file = m_componentFiles.take(component);
        if (file) {
            file->close();
            delete file;
        }
    } else if (!m_componentFiles.contains(component)) {
        // Create the file as soon as logging is enabled, even if the
        // component has not produced a message yet.
        QDir().mkpath(m_cacheDir);
        auto *file = new QFile(componentPath(component));
        if (file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            m_componentFiles.insert(component, file);
        else
            delete file;
    }
}

QString Logger::componentMode(const QString &component) const {
    QMutexLocker locker(&g_logMutex);
    return m_componentModes.value(component, QStringLiteral("off"));
}

QString Logger::componentPath(const QString &component) const {
    QString safe = component;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    safe.replace(QLatin1Char('\\'), QLatin1Char('_'));
    return m_cacheDir + "/" + safe + ".log";
}

void Logger::writeComponent(const QString &component, const QString &message) {
    if (component.isEmpty() || message.isEmpty()) return;
    if (!g_loggerAlive.load()) return;

    QMutexLocker locker(&g_logMutex);
    if (!g_loggerAlive.load()) return;

    const QString mode = m_componentModes.value(component, QStringLiteral("off"));
    if (mode == QLatin1String("off")) return;

    QFile *file = m_componentFiles.value(component, nullptr);
    if (!file) {
        QDir().mkpath(m_cacheDir);
        file = new QFile(componentPath(component));
        if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            delete file;
            return;
        }
        m_componentFiles.insert(component, file);
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss,zzz");
    const QStringList lines = message.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.isEmpty()) continue;
        file->write(QStringLiteral("%1 - %2\n").arg(timestamp, line).toUtf8());
    }
    file->flush();
}

QString Logger::levelToString(Level level) {
    switch (level) {
        case Debug: return "DEBUG";
        case Info: return "INFO";
        case Warning: return "WARNING";
        case Critical: return "CRITICAL";
    }
    return "UNKNOWN";
}

void Logger::log(Level level, const QString &msg) {
    const int minimum = m_minLevel.load();
    if (minimum > Critical || level < minimum) return;

    QMutexLocker locker(&g_logMutex);
    if (!g_loggerAlive.load()) return;

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss,zzz");
    QString logLine = QString("%1 - %2 - %3").arg(timestamp, levelToString(level), msg);

    if (minimum == Debug || level >= Warning) {
        QTextStream(stderr) << logLine << "\n";
    }

    if (m_file && m_file->isOpen()) {
        m_stream << logLine << "\n";
        // Keep the tail of the log available if the process is killed (the
        // exit watchdog can terminate it).
        m_stream.flush();
        m_file->flush();
    }
}
