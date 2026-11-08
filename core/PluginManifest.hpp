#pragma once
#include <QString>
#include <QStringList>
#include <QVariantList>

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
    int apiVersion = 1;
    bool system = false;
    bool valid = false;
    QString error;

    static PluginManifest fromDirectory(const QString &directory, bool system);
};
