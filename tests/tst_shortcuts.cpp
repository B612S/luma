#include <QTest>
#include <QKeyEvent>
#include <QPair>
#include <QSet>

#include "ui/ShortcutAction.hpp"
#include "TestEnvironment.h"

// Checks the hotkey table: bindings, uniqueness, matching
// evdev codes and actionForEvent() behavior.
class TestShortcuts : public QObject
{
    Q_OBJECT

private slots:
    void tableIsNotEmpty();
    void noDuplicateBindings();
    void keycodesAreSane();
    void mapsDefaultBindings_data();
    void mapsDefaultBindings();
    void unknownKeyGivesNone();
    void helpKeyWorksWithAndWithoutShift();
    void actionIdsAreUnique();
    void toggleWindowHasNoDefault();
    void globalPortalTriggerIsNormalized();

private:
    static ShortcutAction actionFor(quint32 keycode, Qt::KeyboardModifiers mods)
    {
        QKeyEvent event(QEvent::KeyPress, 0, mods, keycode, 0, 0, QString(), false, 1);
        return actionForEvent(&event);
    }
};

void TestShortcuts::tableIsNotEmpty()
{
    QVERIFY(defaultShortcuts().size() >= 15);
}

void TestShortcuts::noDuplicateBindings()
{
    QSet<QPair<quint32, int>> seen;
    for (const ShortcutDef &def : defaultShortcuts()) {
        const QPair<quint32, int> binding(def.keycode, static_cast<int>(def.modifiers));
        QVERIFY2(!seen.contains(binding),
                 qPrintable(QStringLiteral("Duplicate binding for keycode %1").arg(def.keycode)));
        seen.insert(binding);
        QVERIFY(def.keyName != nullptr && *def.keyName != '\0');
    }
}

void TestShortcuts::keycodesAreSane()
{
    for (const ShortcutDef &def : defaultShortcuts()) {
        // Wayland keycode = evdev + 8.
        QVERIFY(def.keycode > 8);
        QVERIFY(def.keycode < 256);
        QVERIFY(def.action != ShortcutAction::None);
    }
}

void TestShortcuts::mapsDefaultBindings_data()
{
    QTest::addColumn<quint32>("keycode");
    QTest::addColumn<int>("modifiers");
    QTest::addColumn<int>("action");

    QTest::newRow("space / pause") << quint32(65) << int(Qt::NoModifier)
                                   << int(ShortcutAction::TogglePause);
    QTest::newRow("right / seek forward") << quint32(114) << int(Qt::NoModifier)
                                          << int(ShortcutAction::SeekForward);
    QTest::newRow("left / seek backward") << quint32(113) << int(Qt::NoModifier)
                                          << int(ShortcutAction::SeekBackward);
    QTest::newRow("ctrl+right / long forward") << quint32(114) << int(Qt::ControlModifier)
                                               << int(ShortcutAction::SeekForwardLong);
    QTest::newRow("ctrl+left / long backward") << quint32(113) << int(Qt::ControlModifier)
                                               << int(ShortcutAction::SeekBackwardLong);
    QTest::newRow("alt+left / frame backward") << quint32(113) << int(Qt::AltModifier)
                                               << int(ShortcutAction::FrameBackward);
    QTest::newRow("alt+right / frame forward") << quint32(114) << int(Qt::AltModifier)
                                               << int(ShortcutAction::FrameForward);
    QTest::newRow("f / picture in picture") << quint32(33) << int(Qt::NoModifier)
                                            << int(ShortcutAction::TogglePip);
    QTest::newRow("page up / previous chapter") << quint32(112) << int(Qt::NoModifier)
                                                << int(ShortcutAction::PreviousChapter);
    QTest::newRow("page down / next chapter") << quint32(117) << int(Qt::NoModifier)
                                              << int(ShortcutAction::NextChapter);
    QTest::newRow("w / fit window") << quint32(25) << int(Qt::NoModifier)
                                    << int(ShortcutAction::FitWindowToVideo);
    QTest::newRow("g / equalizer") << quint32(42) << int(Qt::NoModifier)
                                   << int(ShortcutAction::ToggleEqualizer);
}

void TestShortcuts::mapsDefaultBindings()
{
    QFETCH(quint32, keycode);
    QFETCH(int, modifiers);
    QFETCH(int, action);

    QCOMPARE(static_cast<int>(actionFor(keycode, static_cast<Qt::KeyboardModifiers>(modifiers))),
             action);
}

void TestShortcuts::unknownKeyGivesNone()
{
    QCOMPARE(static_cast<int>(actionFor(200, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::None));
}

void TestShortcuts::helpKeyWorksWithAndWithoutShift()
{
    // "?" is Shift+/, so both combinations must open the help.
    QCOMPARE(static_cast<int>(actionFor(61, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::ShowShortcutsHelp));
    QCOMPARE(static_cast<int>(actionFor(61, Qt::ShiftModifier)),
             static_cast<int>(ShortcutAction::ShowShortcutsHelp));
}

void TestShortcuts::actionIdsAreUnique()
{
    QSet<QString> ids;
    for (const ShortcutActionInfo &info : shortcutActions()) {
        const QString id = QString::fromLatin1(info.id);
        QVERIFY(!id.isEmpty());
        QVERIFY(!ids.contains(id));
        ids.insert(id);
        QCOMPARE(shortcutActionFromId(id), info.action);
        QCOMPARE(shortcutActionId(info.action), id);
        QVERIFY(info.name != nullptr && *info.name != '\0');
        QVERIFY(info.category != nullptr && *info.category != '\0');
    }
    QVERIFY(!ids.contains(QStringLiteral("toggle_help")));
}

void TestShortcuts::toggleWindowHasNoDefault()
{
    QCOMPARE(shortcutActionFromId(QStringLiteral("toggle_window")),
             ShortcutAction::ToggleWindow);

    // The new action is assignable, but intentionally has no default key.
    for (const ShortcutDef &def : defaultShortcuts())
        QVERIFY(def.action != ShortcutAction::ToggleWindow);
}

void TestShortcuts::globalPortalTriggerIsNormalized()
{
    // GlobalShortcuts must send triggers in the XDG shortcuts syntax.
    QCOMPARE(shortcutPortalTrigger(QStringLiteral("Ctrl+Shift+W")),
             QStringLiteral("CTRL+SHIFT+W"));
    QCOMPARE(shortcutPortalTrigger(QStringLiteral("Alt+F4")),
             QStringLiteral("ALT+F4"));
    QCOMPARE(shortcutPortalTrigger(QStringLiteral("Meta+Space")),
             QStringLiteral("LOGO+Space"));
    QCOMPARE(shortcutPortalTrigger(QStringLiteral("  Ctrl + Alt + L  ")),
             QStringLiteral("CTRL+ALT+L"));
    QCOMPARE(shortcutPortalTrigger(QString()), QString());
}

int main(int argc, char *argv[])
{
    const QString envRoot = initTestEnvironment();
    QCoreApplication app(argc, argv);

    TestShortcuts test;
    const int status = QTest::qExec(&test, argc, argv);

    cleanupTestEnvironment(envRoot);
    return status;
}

#include "tst_shortcuts.moc"
