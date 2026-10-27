#pragma once
#include <QObject>
#include <QVariantList>
#include <QProcess>

class CastDiscovery : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
public:
    explicit CastDiscovery(QObject *parent = nullptr);

    QVariantList devices() const { return m_devices; }
    bool scanning() const { return m_scanning; }

    Q_INVOKABLE void refresh();

signals:
    void devicesChanged();
    void scanningChanged();

private:
    void parseAvahiOutput(const QString &output);

    QVariantList m_devices;
    bool m_scanning = false;
    QProcess *m_process = nullptr;
};
