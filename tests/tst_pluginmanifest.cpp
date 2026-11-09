#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "PluginManifest.hpp"
#include "PluginLocalization.hpp"

class TestPluginManifest : public QObject {
    Q_OBJECT

private slots:
    void validManifest();
    void rejectsUnknownApi();
    void rejectsUnknownPermission();
    void rejectsEntryOutsideDirectory();
    void rejectsMissingEntry();
    void rejectsReservedDataId();
    void acceptsProcessPermission();
    void acceptsPluginSettings();
    void rejectsInvalidPluginSetting();
    void acceptsPluginShortcuts();
    void ignoresUndeclaredShortcutPermission();
    void loadsPluginLocale();
};

static void writeFile(const QString &path, const QByteArray &data)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(data), data.size());
}

void TestPluginManifest::validManifest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "function activate() {}\n");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test",
        "name": "Test",
        "version": "1.2.3",
        "apiVersion": 1,
        "main": "main.js",
        "permissions": ["osd", "menus", "media:read", "process"]
    })");

    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY2(manifest.valid, qPrintable(manifest.error));
    QCOMPARE(manifest.id, QStringLiteral("com.example.test"));
    QCOMPARE(manifest.name, QStringLiteral("Test"));
    QCOMPARE(manifest.version, QStringLiteral("1.2.3"));
    QCOMPARE(manifest.apiVersion, 1);
    QCOMPARE(manifest.permissions,
             QStringList({QStringLiteral("osd"), QStringLiteral("menus"),
                          QStringLiteral("media:read"), QStringLiteral("process")}));
    QVERIFY(manifest.entry.endsWith(QStringLiteral("/main.js")));
}

void TestPluginManifest::rejectsUnknownApi()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test", "apiVersion": 2, "main": "main.js"
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY(!manifest.valid);
    QVERIFY(manifest.error.contains(QStringLiteral("apiVersion")));
}

void TestPluginManifest::rejectsUnknownPermission()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test", "apiVersion": 1, "main": "main.js",
        "permissions": ["root"]
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY(!manifest.valid);
    QVERIFY(manifest.error.contains(QStringLiteral("permission")));
}

void TestPluginManifest::rejectsEntryOutsideDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QTemporaryDir other;
    QVERIFY(other.isValid());
    writeFile(other.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test", "apiVersion": 1, "main": "../main.js"
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY(!manifest.valid);
    QVERIFY(!manifest.error.isEmpty());
}

void TestPluginManifest::rejectsMissingEntry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test", "apiVersion": 1, "main": "missing.js"
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY(!manifest.valid);
    QVERIFY(manifest.error.contains(QStringLiteral("inside")));
}

void TestPluginManifest::rejectsReservedDataId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "data", "apiVersion": 1, "main": "main.js"
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY(!manifest.valid);
    QVERIFY(manifest.error.contains(QStringLiteral("id")));
}

void TestPluginManifest::acceptsProcessPermission()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test", "apiVersion": 1, "main": "main.js",
        "permissions": ["process"]
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY2(manifest.valid, qPrintable(manifest.error));
}

void TestPluginManifest::acceptsPluginSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.settings", "apiVersion": 1, "main": "main.js",
        "settings": [
            { "key": "enabled", "type": "switch", "label": "Enabled", "default": true },
            { "key": "mode", "type": "select", "label": "Mode",
              "options": [{ "label": "A", "value": "a" }, { "label": "B", "value": "b" }],
              "default": "b" }
        ]
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY2(manifest.valid, qPrintable(manifest.error));
    QCOMPARE(manifest.settings.size(), 2);
    QCOMPARE(manifest.settings.first().toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("enabled"));
    QCOMPARE(manifest.settings.first().toMap().value(QStringLiteral("type")).toString(),
             QStringLiteral("switch"));
    QCOMPARE(manifest.settings.first().toMap().value(QStringLiteral("default")).toBool(), true);
    QCOMPARE(manifest.settings.last().toMap().value(QStringLiteral("options")).toList().size(), 2);
}

void TestPluginManifest::rejectsInvalidPluginSetting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.bad-settings", "apiVersion": 1, "main": "main.js",
        "settings": [{ "key": "mode", "type": "select", "label": "Mode", "options": [] }]
    })");
    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY(!manifest.valid);
    QVERIFY(manifest.error.contains(QStringLiteral("setting")));
}

void TestPluginManifest::loadsPluginLocale()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/locales/ru.json", R"({"Hello":"Привет"})");
    const QVariantMap translations = loadPluginTranslations(dir.path(), QStringLiteral("ru"));
    QCOMPARE(translatedPluginText(translations, QStringLiteral("Hello")),
             QStringLiteral("Привет"));
    QCOMPARE(translatedPluginText(translations, QStringLiteral("Missing")),
             QStringLiteral("Missing"));
}

void TestPluginManifest::acceptsPluginShortcuts()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test", "apiVersion": 1, "main": "main.js",
        "permissions": ["shortcuts"],
        "shortcuts": [
            { "id": "toggle", "label": "Test on / off", "default": "F9" },
            { "id": "Bad Id", "label": "ignored" },
            { "id": "toggle", "label": "duplicate" }
        ]
    })");

    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY2(manifest.valid, qPrintable(manifest.error));
    QCOMPARE(manifest.shortcuts.size(), 1);
    QCOMPARE(manifest.shortcuts.first().id, QStringLiteral("toggle"));
    QCOMPARE(manifest.shortcuts.first().label, QStringLiteral("Test on / off"));
    QCOMPARE(manifest.shortcuts.first().defaultKey, QStringLiteral("F9"));
}

void TestPluginManifest::ignoresUndeclaredShortcutPermission()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/main.js", "");
    writeFile(dir.path() + "/plugin.json", R"({
        "id": "com.example.test", "apiVersion": 1, "main": "main.js",
        "permissions": ["osd"],
        "shortcuts": [ { "id": "toggle", "label": "Test on / off" } ]
    })");

    const PluginManifest manifest = PluginManifest::fromDirectory(dir.path(), false);
    QVERIFY2(manifest.valid, qPrintable(manifest.error));
    QVERIFY(manifest.shortcuts.isEmpty());
}

QTEST_MAIN(TestPluginManifest)
#include "tst_pluginmanifest.moc"
