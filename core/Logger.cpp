#include "Logger.hpp"
#include <QDebug>
#include <QStandardPaths>

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
    if (m_file) {
        m_file->close();
        delete m_file;
    }
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

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss,zzz");
    QString logLine = QString("%1 - %2 - %3").arg(timestamp, levelToString(level), msg);

    if (m_debugMode || level >= Warning) {
        QTextStream(stderr) << logLine << "\n";
    }

    if (m_file && m_file->isOpen()) {
        m_stream << logLine << "\n";
    }
}
