#pragma once
#include <QObject>
#include <QFile>
#include <QHash>
#include <QTextStream>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <atomic>

class Logger : public QObject {
    Q_OBJECT
public:
    enum Level { Debug, Info, Warning, Critical };

    static Logger& instance();

    // Redirects Qt messages (including QML errors) to the log file.
    static void installMessageHandler();

    // Luma's own log: "off"/"none", "error", "info", "debug".
    void setMode(const QString &mode);
    void log(Level level, const QString &msg);

    // Extra component logs (mpv, yt-dlp): each one has its own file under
    // ~/.cache/luma/. Modes use the same names as setMode().
    void setComponentMode(const QString &component, const QString &mode);
    QString componentMode(const QString &component) const;
    void writeComponent(const QString &component, const QString &message);
    QString componentPath(const QString &component) const;

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

    QString m_cacheDir;
    QFile *m_file = nullptr;
    QTextStream m_stream;
    std::atomic<int> m_minLevel{Info};

    QHash<QString, QFile *> m_componentFiles;
    QHash<QString, QString> m_componentModes;
};
