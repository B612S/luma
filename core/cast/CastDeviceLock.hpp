#pragma once
#include <QString>
#include <memory>

class QLockFile;

// Cross-process lease on one Chromecast device. Luma runs several independent
// windows in multi-instance mode, and a TV can only play one cast session at a
// time; without a shared lease the second window would silently take the TV
// over from the first one.
//
// The lock file lives in the per-user runtime directory, so it is shared by all
// Luma processes of the same user and disappears on logout. It is released when
// casting stops or the owning process exits; a file left by a crashed instance
// is recognized by its dead PID and taken over.
class CastDeviceLock {
public:
    // An empty directory selects the per-user runtime directory. Tests pass a
    // temporary directory of their own.
    explicit CastDeviceLock(QString directory = {});
    ~CastDeviceLock();

    CastDeviceLock(const CastDeviceLock &) = delete;
    CastDeviceLock &operator=(const CastDeviceLock &) = delete;

    // Returns true when this object owns the device lease, including when it
    // already held it. When another live process owns it, returns false and
    // stores that process's PID in ownerPid when the lock file allows it.
    bool acquire(const QString &address, quint16 port, qint64 *ownerPid = nullptr);
    void release();
    bool isHeldFor(const QString &address, quint16 port) const;

private:
    QString lockPath(const QString &address, quint16 port) const;

    QString m_directory;
    std::unique_ptr<QLockFile> m_lock;
    QString m_address;
    quint16 m_port = 0;
};
