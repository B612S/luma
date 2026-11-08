#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "PluginManifest.hpp"

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

QTEST_MAIN(TestPluginManifest)
#include "tst_pluginmanifest.moc"
