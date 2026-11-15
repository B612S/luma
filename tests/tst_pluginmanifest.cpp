#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>
#include "plugins/PluginManifest.hpp"
#include "plugins/PluginLocalization.hpp"

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
    void readsLegacyEnglishLocale();
    void buildsTranslationKeys();
    void bundledPluginLocalesUseTranslationKeys();
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
    // en.json is the translation template; ru.json may translate a subset.
    writeFile(dir.path() + "/locales/en.json",
              R"({"name":"English name","settings.mode.label":"Mode"})");
    writeFile(dir.path() + "/locales/ru.json", R"({"name":"Русское имя"})");
    const QVariantMap translations = loadPluginTranslations(dir.path(), QStringLiteral("ru"));
    QCOMPARE(translatedPluginText(translations, QStringLiteral("name")),
             QStringLiteral("Русское имя"));
    // Missing in ru.json: the per-key English fallback, not the raw key.
    QCOMPARE(translatedPluginText(translations, QStringLiteral("settings.mode.label")),
             QStringLiteral("Mode"));
    // Missing everywhere: the English fallback, or nothing when there is none
    // (the host must not show "settings.other.label" in the UI).
    QCOMPARE(translatedPluginText(translations, QStringLiteral("settings.other.label"),
                                  QStringLiteral("Other")),
             QStringLiteral("Other"));
    QVERIFY(translatedPluginText(translations,
                                 QStringLiteral("settings.other.label")).isEmpty());
}

// Locale files written before translation keys used the English text as the
// key; installed plugins must keep their translations.
void TestPluginManifest::readsLegacyEnglishLocale()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeFile(dir.path() + "/locales/ru.json", R"({"Frame rate":"Частота кадров"})");
    const QVariantMap translations = loadPluginTranslations(dir.path(), QStringLiteral("ru"));
    QCOMPARE(translatedPluginText(translations, QStringLiteral("settings.mode.label"),
                                  QStringLiteral("Frame rate")),
             QStringLiteral("Частота кадров"));
}

void TestPluginManifest::buildsTranslationKeys()
{
    QCOMPARE(pluginSettingTextKey(QStringLiteral("mode"), QStringLiteral("label")),
             QStringLiteral("settings.mode.label"));
    QCOMPARE(pluginSettingTextKey(QStringLiteral("mode"), QStringLiteral("description")),
             QStringLiteral("settings.mode.description"));
    QCOMPARE(pluginSettingOptionTextKey(QStringLiteral("mode"), QStringLiteral("display")),
             QStringLiteral("settings.mode.options.display"));
    QCOMPARE(pluginSettingOptionTextKey(QStringLiteral("mode"), 60),
             QStringLiteral("settings.mode.options.60"));
    QCOMPARE(pluginShortcutTextKey(QStringLiteral("toggle")),
             QStringLiteral("shortcuts.toggle.label"));
}

namespace {
QVariantMap readLocaleFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object().toVariantMap() : QVariantMap();
}
} // namespace

// Bundled plugins ship complete en/ru locale files; every key the host derives
// from plugin.json must exist or the UI would fall back to English silently.
void TestPluginManifest::bundledPluginLocalesUseTranslationKeys()
{
    const QDir root(QStringLiteral(LUMA_PLUGINS_DIR));
    QVERIFY(root.exists());
    const QStringList plugins = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QVERIFY(!plugins.isEmpty());

    for (const QString &name : plugins) {
        const QString directory = root.filePath(name);
        const PluginManifest manifest = PluginManifest::fromDirectory(directory, true);
        QVERIFY2(manifest.valid, qPrintable(name + ": " + manifest.error));

        const QVariantMap en = readLocaleFile(directory + "/locales/en.json");
        const QVariantMap ru = readLocaleFile(directory + "/locales/ru.json");
        QVERIFY2(!en.isEmpty(), qPrintable(name + ": en.json is missing or empty"));
        QVERIFY2(!ru.isEmpty(), qPrintable(name + ": ru.json is missing or empty"));

        QMap<QString, QString> expected;
        expected.insert(QStringLiteral("name"), manifest.name);
        expected.insert(QStringLiteral("description"), manifest.description);
        if (!manifest.info.isEmpty())
            expected.insert(QStringLiteral("info"), manifest.info);
        for (const QVariant &setting : manifest.settings) {
            const QVariantMap map = setting.toMap();
            const QString settingKey = map.value(QStringLiteral("key")).toString();
            for (const QString &field : {QStringLiteral("label"), QStringLiteral("description"),
                                         QStringLiteral("placeholder")}) {
                if (map.contains(field)) {
                    expected.insert(pluginSettingTextKey(settingKey, field),
                                    map.value(field).toString());
                }
            }
            for (const QVariant &option : map.value(QStringLiteral("options")).toList()) {
                const QVariantMap optionMap = option.toMap();
                expected.insert(pluginSettingOptionTextKey(
                                    settingKey, optionMap.value(QStringLiteral("value"))),
                                optionMap.value(QStringLiteral("label")).toString());
            }
        }
        for (const PluginShortcutDef &shortcut : manifest.shortcuts) {
            expected.insert(pluginShortcutTextKey(shortcut.id), shortcut.label);
        }

        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            QVERIFY2(en.contains(it.key()), qPrintable(name + ": en.json is missing " + it.key()));
            QVERIFY2(ru.contains(it.key()), qPrintable(name + ": ru.json is missing " + it.key()));
            // en.json is the translation template; keep it equal to the
            // English text in plugin.json so the two cannot drift.
            QCOMPARE(en.value(it.key()).toString(), it.value());
        }
    }
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
