#pragma once
#include <QObject>
#include <QString>

class ConfigManager;
class QDBusInterface;

// Desktop notifications and the cover-art cache. Notification tags and cover
// art are prepared on the thread pool; the DBus call stays on the GUI thread.
class MediaNotifier : public QObject {
    Q_OBJECT

public:
    explicit MediaNotifier(QObject *parent = nullptr);

    void setConfig(ConfigManager *config) { m_config = config; }
    // Free-form notification (OSD-like messages such as download failures).
    void notify(const QString &summary, const QString &body);
    // Track notification with tags and cover art for a local file.
    void notifyForFile(const QString &path);

private:
    bool enabled() const;
    void send(const QString &summary, const QString &body, const QString &coverPath);

    ConfigManager *m_config = nullptr;
    QDBusInterface *m_interface = nullptr;
    quint64 m_generation = 0;
};
