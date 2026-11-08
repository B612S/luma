#pragma once
#include <QString>
#include <QVariantMap>

class ConfigManager;

// Plugin translations live in <plugin>/locales/<language>.json, separate from
// Luma's own locales. Keys are the English text used in plugin.json or passed
// to luma.i18n.t().
QString pluginLanguage(ConfigManager *config);
QVariantMap loadPluginTranslations(const QString &pluginDirectory,
                                   const QString &language);
QString translatedPluginText(const QVariantMap &translations, const QString &text);
