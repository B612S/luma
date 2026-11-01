#include "Logger.hpp"
#include <QDebug>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>
#include <cstdlib>

namespace {
bool g_loggerAlive = true;
// Сообщения Qt могут приходить из разных потоков (mpv, рендерер),
// поэтому запись в файл и stderr сериализуем.
QMutex g_logMutex;
}

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::Logger(QObject *parent) : QObject(parent), m_file(nullptr) {
    // Путь: ~/.cache/luma/luma.log
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cacheDir.isEmpty()) {
        cacheDir = QDir::homePath() + "/.cache";
    }
    cacheDir += "/luma";

    QDir().mkpath(cacheDir);

    QString logFile = cacheDir + "/luma.log";
    m_file = new QFile(logFile);
    if (m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        m_stream.setDevice(m_file);
    }
}

Logger::~Logger() {
    g_loggerAlive = false;
    if (m_file) {
        m_file->close();
        delete m_file;
    }
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
    QString m = mode.toLower().trimmed();
    if (m == "none") {
        m_noneMode = true;
        m_debugMode = false;
    } else if (m == "debug") {
        m_noneMode = false;
        m_debugMode = true;
    } else {
        m_noneMode = false;
        m_debugMode = false;
    }
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
    if (m_noneMode) return;
    if (level == Debug && !m_debugMode) return;

    QMutexLocker locker(&g_logMutex);

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss,zzz");
    QString logLine = QString("%1 - %2 - %3").arg(timestamp, levelToString(level), msg);

    if (m_debugMode || level >= Warning) {
        QTextStream(stderr) << logLine << "\n";
    }

    if (m_file && m_file->isOpen()) {
        m_stream << logLine << "\n";
    }
}
