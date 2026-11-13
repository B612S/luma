#include <QtTest>
#include <QCoreApplication>
#include <QTemporaryDir>

#include "cast/CastDeviceLock.hpp"

// Two Luma windows are separate processes, but QLockFile also refuses a second
// lease from the same process, so the cross-instance rule can be tested here.
class TestCastLock : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void acquiresDevice();
    void refusesSecondOwner();
    void allowsDifferentDevices();
    void isIdempotentForTheSameDevice();
    void reacquiresAfterRelease();
    void continuesWhenLeaseCannotBeCreated();

private:
    std::unique_ptr<QTemporaryDir> m_directory;
};

void TestCastLock::init()
{
    m_directory = std::make_unique<QTemporaryDir>();
    QVERIFY(m_directory->isValid());
}

void TestCastLock::cleanup()
{
    m_directory.reset();
}

void TestCastLock::acquiresDevice()
{
    CastDeviceLock lock(m_directory->path());
    QVERIFY(lock.acquire(QStringLiteral("192.168.0.55"), 8009));
    QVERIFY(lock.isHeldFor(QStringLiteral("192.168.0.55"), 8009));
    QVERIFY(!lock.isHeldFor(QStringLiteral("192.168.0.56"), 8009));
}

void TestCastLock::refusesSecondOwner()
{
    CastDeviceLock first(m_directory->path());
    CastDeviceLock second(m_directory->path());
    QVERIFY(first.acquire(QStringLiteral("192.168.0.55"), 8009));

    qint64 ownerPid = 0;
    QVERIFY(!second.acquire(QStringLiteral("192.168.0.55"), 8009, &ownerPid));
    QCOMPARE(ownerPid, QCoreApplication::applicationPid());
    QVERIFY(!second.isHeldFor(QStringLiteral("192.168.0.55"), 8009));
}

void TestCastLock::allowsDifferentDevices()
{
    CastDeviceLock first(m_directory->path());
    CastDeviceLock second(m_directory->path());
    QVERIFY(first.acquire(QStringLiteral("192.168.0.55"), 8009));
    QVERIFY(second.acquire(QStringLiteral("192.168.0.56"), 8009));
}

void TestCastLock::isIdempotentForTheSameDevice()
{
    CastDeviceLock lock(m_directory->path());
    QVERIFY(lock.acquire(QStringLiteral("192.168.0.55"), 8009));
    QVERIFY(lock.acquire(QStringLiteral("192.168.0.55"), 8009));
    QVERIFY(lock.isHeldFor(QStringLiteral("192.168.0.55"), 8009));
}

void TestCastLock::reacquiresAfterRelease()
{
    CastDeviceLock first(m_directory->path());
    CastDeviceLock second(m_directory->path());
    QVERIFY(first.acquire(QStringLiteral("192.168.0.55"), 8009));
    first.release();
    QVERIFY(!first.isHeldFor(QStringLiteral("192.168.0.55"), 8009));
    QVERIFY(second.acquire(QStringLiteral("192.168.0.55"), 8009));
}

void TestCastLock::continuesWhenLeaseCannotBeCreated()
{
    // /proc rejects mkdir(2). The cross-instance guard is best effort: casting
    // must still work when the lease cannot be created at all.
    CastDeviceLock lock(QStringLiteral("/proc/luma-cast-lock-test"));
    QVERIFY(lock.acquire(QStringLiteral("192.168.0.55"), 8009));
    QVERIFY(!lock.isHeldFor(QStringLiteral("192.168.0.55"), 8009));
}

QTEST_GUILESS_MAIN(TestCastLock)
#include "tst_castlock.moc"
