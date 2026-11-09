#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

// A keyboard action declared in plugin.json. The host adds it to
// Settings → Shortcuts so the user can assign a local or global key.
struct PluginShortcutDef {
    QString id;
    QString label;
    QString defaultKey;
};

// Parsed and validated plugin.json. `entry` is an absolute path inside
// `directory`; invalid manifests keep the error text and are not loaded.
struct PluginManifest {
    QString id;
    QString name;
    QString version;
    QString description;
    QString info;
    QString author;
    QString license;
    QString entry;
    QString directory;
    QStringList permissions;
    QVariantList settings;
    QList<PluginShortcutDef> shortcuts;
    int apiVersion = 1;
    bool system = false;
    bool valid = false;
    QString error;

    static PluginManifest fromDirectory(const QString &directory, bool system);
};
