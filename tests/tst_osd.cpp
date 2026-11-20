// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

#include <QTest>
#include <QTimer>
#include "ui/OsdManager.hpp"

// Checks the temporary OSD lifetime. Every show must get a full interval; a
// hide timeout left over from an earlier overlay must not cut a later toast.
class TestOsd : public QObject
{
    Q_OBJECT

private slots:
    void infoLivesFullInterval();
    void restartGetsFullInterval();
};

void TestOsd::infoLivesFullInterval()
{
    OsdManager osd(nullptr);
    osd.showInfo("toast");
    QVERIFY(osd.visible());

    QTest::qWait(1000);
    QVERIFY(osd.visible());

    QTest::qWait(1800);
    QVERIFY(!osd.visible());
}

void TestOsd::restartGetsFullInterval()
{
    OsdManager osd(nullptr);
    osd.showInfo("first");
    QTest::qWait(1500);

    osd.showInfo("second");
    QTest::qWait(1500);
    QVERIFY(osd.visible());

    QTest::qWait(1200);
    QVERIFY(!osd.visible());
}

QTEST_MAIN(TestOsd)
#include "tst_osd.moc"
