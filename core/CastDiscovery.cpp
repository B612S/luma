#include "CastDiscovery.hpp"
#include <QDebug>
#include <QTimer>
#include <QSet>

CastDiscovery::CastDiscovery(QObject *parent) : QObject(parent) {}

void CastDiscovery::refresh() {
    if (m_scanning) return;
    m_scanning = true;
    emit scanningChanged();

    m_devices.clear();
    emit devicesChanged();

    m_process = new QProcess(this);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                parseAvahiOutput(QString::fromUtf8(m_process->readAllStandardOutput()));
                m_scanning = false;
                emit scanningChanged();
                m_process->deleteLater();
                m_process = nullptr;
            });

    m_process->start("avahi-browse", QStringList() << "-rt" << "_googlecast._tcp");

    QTimer::singleShot(5000, this, [this]() {
        if (m_process && m_process->state() == QProcess::Running) {
            m_process->kill();
        }
    });
}

void CastDiscovery::parseAvahiOutput(const QString &output) {
    QString currentName;
    QString currentAddress;
    int currentPort = 8009;

    // For deduplication: "address:port"
    QSet<QString> seen;

    auto flushDevice = [&]() {
        if (currentName.isEmpty() || currentAddress.isEmpty()) return;

        if (currentAddress.startsWith("127.") || currentAddress == "::1") return;

        // Deduplication: skip the device if it is already present
        const QString key = currentAddress + ":" + QString::number(currentPort);
        if (seen.contains(key)) return;

        seen.insert(key);

        QVariantMap dev;
        dev["name"] = currentName;
        dev["address"] = currentAddress;
        dev["port"] = currentPort;
        m_devices.append(dev);
    };

    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();

        if (trimmed.startsWith("=") && trimmed.contains("_googlecast._tcp")) {
            flushDevice();
            currentName.clear();
            currentAddress.clear();
            currentPort = 8009;
        }
        else if (trimmed.startsWith("address = [")) {
            int start = trimmed.indexOf('[') + 1;
            int end = trimmed.indexOf(']');
            if (start > 0 && end > start)
                currentAddress = trimmed.mid(start, end - start);
        }
        else if (trimmed.startsWith("port = [")) {
            int start = trimmed.indexOf('[') + 1;
            int end = trimmed.indexOf(']');
            if (start > 0 && end > start) {
                bool ok = false;
                int port = trimmed.mid(start, end - start).toInt(&ok);
                if (ok) currentPort = port;
            }
        }
        else if (trimmed.startsWith("txt = [")) {
            int fnPos = trimmed.indexOf("fn=");
            if (fnPos >= 0) {
                int start = fnPos + 3;
                int end = trimmed.indexOf('"', start);
                if (end > start)
                    currentName = trimmed.mid(start, end - start);
            }
        }
    }
    flushDevice();

    emit devicesChanged();
}
