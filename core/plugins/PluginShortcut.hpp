#pragma once
#include <QString>

// A keyboard action declared by a loaded plugin (plugin.json "shortcuts") and
// shown in Settings → Shortcuts. `label` is already translated through the
// plugin's locales.
struct PluginShortcutAction {
    QString pluginId;
    QString actionId;
    QString label;
    QString defaultKey;
};
