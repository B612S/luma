#pragma once
#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

class Logger : public QObject {
    Q_OBJECT
public:
    enum Level { Debug, Info, Warning, Critical };

    static Logger& instance();

    void setMode(const QString &mode); // "none", "info", "debug"
    void log(Level level, const QString &msg);

    void debug(const QString &msg) { log(Debug, msg); }
    void info(const QString &msg) { log(Info, msg); }
    void warning(const QString &msg) { log(Warning, msg); }
    void critical(const QString &msg) { log(Critical, msg); }

private:
    Logger(QObject *parent = nullptr);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    QString levelToString(Level level);
    QFile *m_file;
    QTextStream m_stream;
    bool m_debugMode = false;
    bool m_noneMode = false;
};
