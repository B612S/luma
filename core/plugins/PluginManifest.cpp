#include "plugins/PluginManifest.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

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
        QStringLiteral("process"),
        QStringLiteral("stats"),
        QStringLiteral("shortcuts")
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
    manifest.info = obj.value(QStringLiteral("info")).toString().trimmed();
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
            || manifest.id.contains(QStringLiteral(".."))
            || manifest.id.compare(QStringLiteral("data"), Qt::CaseInsensitive) == 0) {
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

    const QJsonArray settings = obj.value(QStringLiteral("settings")).toArray();
    QSet<QString> settingKeys;
    static const QRegularExpression keyPattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]*$"));
    static const QStringList settingTypes = {
        QStringLiteral("switch"), QStringLiteral("select"),
        QStringLiteral("text"), QStringLiteral("textarea"),
        QStringLiteral("number")
    };
    for (const QJsonValue &value : settings) {
        if (!value.isObject()) {
            manifest.error = QStringLiteral("invalid plugin setting");
            return manifest;
        }
        const QJsonObject setting = value.toObject();
        const QString key = setting.value(QStringLiteral("key")).toString().trimmed();
        const QString type = setting.value(QStringLiteral("type")).toString().trimmed();
        const QString label = setting.value(QStringLiteral("label")).toString().trimmed();
        if (key.isEmpty() || !keyPattern.match(key).hasMatch()
                || settingKeys.contains(key)
                || !settingTypes.contains(type) || label.isEmpty()) {
            manifest.error = QStringLiteral("invalid plugin setting: %1").arg(key);
            return manifest;
        }
        settingKeys.insert(key);

        QVariantMap parsed;
        parsed.insert(QStringLiteral("key"), key);
        parsed.insert(QStringLiteral("type"), type);
        parsed.insert(QStringLiteral("label"), label);
        const QString description = setting.value(QStringLiteral("description")).toString().trimmed();
        if (!description.isEmpty())
            parsed.insert(QStringLiteral("description"), description);
        const QString placeholder = setting.value(QStringLiteral("placeholder")).toString();
        if (!placeholder.isEmpty())
            parsed.insert(QStringLiteral("placeholder"), placeholder);

        if (type == QLatin1String("switch")) {
            parsed.insert(QStringLiteral("default"), setting.value(QStringLiteral("default")).toBool(false));
        } else if (type == QLatin1String("text") || type == QLatin1String("textarea")) {
            parsed.insert(QStringLiteral("default"), setting.value(QStringLiteral("default")).toString());
        } else if (type == QLatin1String("number")) {
            const double defaultValue = setting.value(QStringLiteral("default")).toDouble(0);
            parsed.insert(QStringLiteral("default"), defaultValue);
            if (setting.contains(QStringLiteral("min")))
                parsed.insert(QStringLiteral("min"), setting.value(QStringLiteral("min")).toDouble());
            if (setting.contains(QStringLiteral("max")))
                parsed.insert(QStringLiteral("max"), setting.value(QStringLiteral("max")).toDouble());
            if (setting.contains(QStringLiteral("step")))
                parsed.insert(QStringLiteral("step"), setting.value(QStringLiteral("step")).toDouble(1));
        } else {
            const QJsonArray options = setting.value(QStringLiteral("options")).toArray();
            QVariantList parsedOptions;
            for (const QJsonValue &optionValue : options) {
                if (!optionValue.isObject())
                    continue;
                const QJsonObject option = optionValue.toObject();
                const QString optionLabel = option.value(QStringLiteral("label")).toString().trimmed();
                const QJsonValue optionData = option.value(QStringLiteral("value"));
                if (optionLabel.isEmpty() || optionData.isUndefined())
                    continue;
                QVariantMap parsedOption;
                parsedOption.insert(QStringLiteral("label"), optionLabel);
                parsedOption.insert(QStringLiteral("value"), optionData.toVariant());
                parsedOptions << parsedOption;
            }
            if (parsedOptions.isEmpty()) {
                manifest.error = QStringLiteral("plugin setting has no options: %1").arg(key);
                return manifest;
            }
            parsed.insert(QStringLiteral("options"), parsedOptions);
            QVariant defaultValue = setting.value(QStringLiteral("default")).toVariant();
            if (!defaultValue.isValid())
                defaultValue = parsedOptions.first().toMap().value(QStringLiteral("value"));
            parsed.insert(QStringLiteral("default"), defaultValue);
        }
        manifest.settings << parsed;
    }

    // Shortcut actions are only usable with the "shortcuts" permission; they
    // are listed in Settings → Shortcuts and stored in shortcuts.conf.
    if (manifest.permissions.contains(QStringLiteral("shortcuts"))) {
        QSet<QString> shortcutIds;
        static const QRegularExpression shortcutIdPattern(
            QStringLiteral("^[a-z0-9][a-z0-9_-]*$"));
        const QJsonArray shortcuts = obj.value(QStringLiteral("shortcuts")).toArray();
        for (const QJsonValue &value : shortcuts) {
            if (!value.isObject() || manifest.shortcuts.size() >= 16)
                continue;
            const QJsonObject shortcut = value.toObject();
            const QString id = shortcut.value(QStringLiteral("id")).toString().trimmed();
            const QString label = shortcut.value(QStringLiteral("label")).toString().trimmed();
            const QString defaultKey = shortcut.value(QStringLiteral("default")).toString().trimmed();
            if (id.isEmpty() || label.isEmpty()
                    || !shortcutIdPattern.match(id).hasMatch()
                    || shortcutIds.contains(id))
                continue;
            shortcutIds.insert(id);
            PluginShortcutDef def;
            def.id = id;
            def.label = label;
            def.defaultKey = defaultKey;
            manifest.shortcuts << def;
        }
    }

    manifest.valid = true;
    return manifest;
}
