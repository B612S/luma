#include <QTest>
#include <QKeyEvent>
#include <QPair>
#include <QSet>

#include "ShortcutAction.hpp"
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
    void mapsSpaceToTogglePause();
    void mapsSeekAndLongSeek();
    void mapsFrameStepping();
    void mapsPip();
    void mapsFitWindowToVideo();
    void removedCoversKeyDoesNothing();
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

void TestShortcuts::mapsSpaceToTogglePause()
{
    QCOMPARE(static_cast<int>(actionFor(65, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::TogglePause));
}

void TestShortcuts::mapsSeekAndLongSeek()
{
    QCOMPARE(static_cast<int>(actionFor(114, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::SeekForward));
    QCOMPARE(static_cast<int>(actionFor(113, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::SeekBackward));
    QCOMPARE(static_cast<int>(actionFor(114, Qt::ControlModifier)),
             static_cast<int>(ShortcutAction::SeekForwardLong));
    QCOMPARE(static_cast<int>(actionFor(113, Qt::ControlModifier)),
             static_cast<int>(ShortcutAction::SeekBackwardLong));
}

void TestShortcuts::mapsFrameStepping()
{
    QCOMPARE(static_cast<int>(actionFor(113, Qt::AltModifier)),
             static_cast<int>(ShortcutAction::FrameBackward));
    QCOMPARE(static_cast<int>(actionFor(114, Qt::AltModifier)),
             static_cast<int>(ShortcutAction::FrameForward));
}

void TestShortcuts::mapsPip()
{
    QCOMPARE(static_cast<int>(actionFor(33, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::TogglePip));
}

void TestShortcuts::mapsFitWindowToVideo()
{
    // W is the default: evdev KEY_W (17) + 8.
    QCOMPARE(static_cast<int>(actionFor(25, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::FitWindowToVideo));
    QCOMPARE(shortcutActionFromId(QStringLiteral("fit_window")),
             ShortcutAction::FitWindowToVideo);
}

void TestShortcuts::removedCoversKeyDoesNothing()
{
    // The N key (ToggleCsd) was removed — there must be no binding.
    QCOMPARE(static_cast<int>(actionFor(57, Qt::NoModifier)),
             static_cast<int>(ShortcutAction::None));
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
