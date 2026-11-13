#include "cast/CastDiscovery.hpp"
#include <QDebug>
#include <QTimer>
#include <QSet>

CastDiscovery::CastDiscovery(QObject *parent) : QObject(parent) {}

void CastDiscovery::refresh() {
    if (m_scanning) return;
    m_scanning = true;
    const quint64 generation = ++m_scanGeneration;
    emit scanningChanged();

    m_devices.clear();
    emit devicesChanged();

    auto *proc = new QProcess(this);
    m_process = proc;

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, generation](int, QProcess::ExitStatus) {
                if (generation != m_scanGeneration || m_process != proc) return;
                m_process = nullptr;
                const QByteArray output = proc->readAllStandardOutput();
                proc->deleteLater();
                parseAvahiOutput(QString::fromUtf8(output));
                m_scanning = false;
                emit scanningChanged();
            });

    // FailedToStart does not emit finished(); without this the menu would stay
    // in "searching" forever and the process object would never be released.
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, generation](QProcess::ProcessError error) {
        if (generation != m_scanGeneration || error != QProcess::FailedToStart
                || m_process != proc)
            return;
        m_process = nullptr;
        proc->deleteLater();
        m_scanning = false;
        emit scanningChanged();
    });

    proc->start("avahi-browse", QStringList() << "-rt" << "_googlecast._tcp");

    QTimer::singleShot(5000, this, [this, proc, generation]() {
        if (generation == m_scanGeneration && m_process == proc
                && proc->state() == QProcess::Running)
            proc->kill();
    });
}

void CastDiscovery::parseAvahiOutput(const QString &output) {
    QString currentName;
    QString currentAddressV4;
    QString currentAddressV6;
    int currentPort = 8009;

    // For deduplication: "address:port"
    QSet<QString> seen;

    auto flushDevice = [&]() {
        // avahi-browse reports the same TV twice: an IPv6 link-local service
        // and an IPv4 service. The Cast/HLS server is IPv4-only; using the
        // link-local IPv6 entry makes the TV fetch 127.0.0.1 and fall back to
        // its ambient slideshow. Only expose IPv4 records.
        const QString currentAddress = currentAddressV4;
        if (currentName.isEmpty() || currentAddress.isEmpty()) return;

        if (currentAddress.startsWith("127.") || currentAddress == "::1") return;

        // Deduplication: skip the device if it is already present
        const QString key = currentAddress + ":" + QString::number(currentPort);
        if (seen.contains(key)) return;

        seen.insert(key);
        qInfo() << "CastDiscovery: found" << currentName << currentAddress << currentPort;

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
            currentAddressV4.clear();
            currentAddressV6.clear();
            currentPort = 8009;
        }
        else if (trimmed.startsWith("address = [")) {
            int start = trimmed.indexOf('[') + 1;
            int end = trimmed.indexOf(']');
            if (start > 0 && end > start) {
                const QString address = trimmed.mid(start, end - start);
                if (address.contains(QLatin1Char(':'))) {
                    if (currentAddressV6.isEmpty()) currentAddressV6 = address;
                } else if (currentAddressV4.isEmpty()) {
                    currentAddressV4 = address;
                }
            }
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
