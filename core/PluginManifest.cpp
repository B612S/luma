#include "PluginManifest.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace {

const QStringList &knownPermissions()
{
    static const QStringList permissions = {
        QStringLiteral("media:read"),
        QStringLiteral("media:control"),
        QStringLiteral("playlists:read"),
        QStringLiteral("playlists:write"),
        QStringLiteral("osd"),
        QStringLiteral("menus"),
        QStringLiteral("windows"),
        QStringLiteral("network"),
        QStringLiteral("filesystem:plugin"),
        QStringLiteral("notifications"),
        QStringLiteral("mpv:raw"),
        QStringLiteral("process")
    };
    return permissions;
}

} // namespace

PluginManifest PluginManifest::fromDirectory(const QString &directory, bool system)
{
    PluginManifest manifest;
    manifest.directory = QFileInfo(directory).canonicalFilePath();
    manifest.system = system;
    if (manifest.directory.isEmpty()) {
        manifest.error = QStringLiteral("plugin directory does not exist");
        return manifest;
    }

    QFile file(manifest.directory + QStringLiteral("/plugin.json"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        manifest.error = QStringLiteral("plugin.json is missing");
        return manifest;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        manifest.error = QStringLiteral("plugin.json is not valid JSON");
        return manifest;
    }

    const QJsonObject obj = document.object();
    manifest.id = obj.value(QStringLiteral("id")).toString().trimmed();
    manifest.name = obj.value(QStringLiteral("name")).toString().trimmed();
    manifest.version = obj.value(QStringLiteral("version")).toString().trimmed();
    manifest.description = obj.value(QStringLiteral("description")).toString().trimmed();
    manifest.author = obj.value(QStringLiteral("author")).toString().trimmed();
    manifest.license = obj.value(QStringLiteral("license")).toString().trimmed();
    manifest.apiVersion = obj.value(QStringLiteral("apiVersion")).toInt(0);
    if (manifest.name.isEmpty())
        manifest.name = manifest.id;
    if (manifest.version.isEmpty())
        manifest.version = QStringLiteral("0.0.0");

    static const QRegularExpression idPattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    if (manifest.id.isEmpty() || !idPattern.match(manifest.id).hasMatch()
            || manifest.id.contains(QStringLiteral(".."))) {
        manifest.error = QStringLiteral("invalid plugin id");
        return manifest;
    }
    if (manifest.apiVersion != 1) {
        manifest.error = QStringLiteral("unsupported apiVersion (expected 1)");
        return manifest;
    }

    const QString main = obj.value(QStringLiteral("main")).toString().trimmed();
    if (main.isEmpty()) {
        manifest.error = QStringLiteral("main is missing");
        return manifest;
    }
    const QFileInfo entryInfo(manifest.directory + QLatin1Char('/') + main);
    const QString canonicalEntry = entryInfo.canonicalFilePath();
    if (!entryInfo.isFile() || canonicalEntry.isEmpty()
            || !canonicalEntry.startsWith(manifest.directory + QLatin1Char('/'))) {
        manifest.error = QStringLiteral("main must be a file inside the plugin directory");
        return manifest;
    }
    manifest.entry = canonicalEntry;

    for (const QJsonValue &value : obj.value(QStringLiteral("permissions")).toArray()) {
        const QString permission = value.toString();
        if (permission.isEmpty())
            continue;
        if (!knownPermissions().contains(permission)) {
            manifest.error = QStringLiteral("unknown permission: %1").arg(permission);
            return manifest;
        }
        manifest.permissions << permission;
    }
    manifest.permissions.removeDuplicates();
    manifest.valid = true;
    return manifest;
}
