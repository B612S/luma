#include "CastDiscovery.hpp"
#include <QDebug>
#include <QTimer>

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

    // avahi-browse -rt _googlecast._tcp  ->  ищем строки с address = [...]
    m_process->start("avahi-browse", QStringList() << "-rt" << "_googlecast._tcp");
    // Таймаут на случай, если avahi-browse не установлен
    QTimer::singleShot(5000, this, [this]() {
        if (m_process && m_process->state() == QProcess::Running) {
            m_process->kill();
        }
    });
}

void CastDiscovery::parseAvahiOutput(const QString &output) {
    // Парсим блоки вида:
    // = eth0 IPv4 Upstairs-Chromecast    _googlecast._tcp local
    //    hostname = [Upstairs-Chromecast.local]
    //    address = [192.168.0.52]
    //    port = [8009]
    //    txt = ["fn=Upstairs-Chromecast" ...]
    QString currentName;
    QString currentAddress;
    int currentPort = 8009;

    const QStringList lines = output.split('\n');
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();

        if (trimmed.startsWith("=") && trimmed.contains("_googlecast._tcp")) {
            // Начало нового устройства — сохраняем предыдущее
            if (!currentName.isEmpty() && !currentAddress.isEmpty()) {
                QVariantMap dev;
                dev["name"] = currentName;
                dev["address"] = currentAddress;
                dev["port"] = currentPort;
                m_devices.append(dev);
            }
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
            // Извлекаем fn= из TXT-записи
            int fnPos = trimmed.indexOf("fn=");
            if (fnPos >= 0) {
                int start = fnPos + 3;
                int end = trimmed.indexOf('"', start);
                if (end > start)
                    currentName = trimmed.mid(start, end - start);
            }
        }
    }
    // Последнее устройство
    if (!currentName.isEmpty() && !currentAddress.isEmpty()) {
        QVariantMap dev;
        dev["name"] = currentName;
        dev["address"] = currentAddress;
        dev["port"] = currentPort;
        m_devices.append(dev);
    }

    emit devicesChanged();
}
