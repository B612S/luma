#include "cast/CastDeviceLock.hpp"

#include <QDebug>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

#include <utility>

namespace {

QString defaultLockDirectory()
{
    // RuntimeLocation is $XDG_RUNTIME_DIR on Linux: per user, per login, and
    // cleaned up on logout, so a stale file cannot survive a reboot and block
    // casting through a recycled PID.
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    return base + QStringLiteral("/luma");
}

QString deviceFileName(const QString &address, quint16 port)
{
    // The address is an IPv4 literal from CastDiscovery; keep the file name
    // readable and safe even if a different address form shows up later.
    QString id = address;
    for (QChar &character : id) {
        if (!character.isLetterOrNumber() && character != QLatin1Char('.')
                && character != QLatin1Char('-'))
            character = QLatin1Char('_');
    }
    return QStringLiteral("cast-%1-%2.lock").arg(id, QString::number(port));
}

} // namespace

CastDeviceLock::CastDeviceLock(QString directory)
    : m_directory(directory.isEmpty() ? defaultLockDirectory() : std::move(directory))
{
}

CastDeviceLock::~CastDeviceLock() = default; // QLockFile removes the file on destruction.

QString CastDeviceLock::lockPath(const QString &address, quint16 port) const
{
    return m_directory + QLatin1Char('/') + deviceFileName(address, port);
}

bool CastDeviceLock::acquire(const QString &address, quint16 port, qint64 *ownerPid)
{
    if (isHeldFor(address, port))
        return true;

    if (!QDir().mkpath(m_directory))
        qWarning() << "CastDeviceLock: cannot create" << m_directory;

    auto lock = std::make_unique<QLockFile>(lockPath(address, port));
    // Never wait: the cast is either allowed or refused right away. QLockFile
    // also takes over a lock whose owner process no longer exists.
    if (!lock->tryLock(0)) {
        if (lock->error() != QLockFile::LockFailedError) {
            // The lease could not be created (read-only runtime directory,
            // full filesystem, ...). Casting is more important than the
            // cross-instance guard, so continue without a lease.
            qWarning() << "CastDeviceLock: cannot create" << lock->fileName()
                       << "- continuing without a device lease";
            return true;
        }
        qint64 pid = 0;
        QString hostname;
        QString appname;
        if (lock->getLockInfo(&pid, &hostname, &appname)) {
            if (ownerPid)
                *ownerPid = pid;
            qInfo() << "CastDeviceLock:" << address << port << "is held by"
                    << (appname.isEmpty() ? QStringLiteral("Luma") : appname)
                    << "pid" << pid << "on" << hostname;
        } else {
            qInfo() << "CastDeviceLock:" << address << port
                    << "is held by another process";
        }
        return false;
    }

    if (m_lock) {
        // Switching TVs: the new lease is ours, so the previous device is
        // already free. Its receiver session is stopped by the caller.
        m_lock->unlock();
    }
    m_lock = std::move(lock);
    m_address = address;
    m_port = port;
    qInfo() << "CastDeviceLock: acquired" << m_lock->fileName();
    return true;
}

void CastDeviceLock::release()
{
    if (!m_lock)
        return;
    m_lock->unlock();
    m_lock.reset();
    m_address.clear();
    m_port = 0;
}

bool CastDeviceLock::isHeldFor(const QString &address, quint16 port) const
{
    return m_lock && m_address == address && m_port == port;
}
