#pragma once
#include <QString>
#include <QVariantMap>

class ConfigManager;

// Plugin translations live in <plugin>/locales/<language>.json, separate from
// Luma's own locales. Keys are semantic names, not English source text:
//
//   name, description, info          plugin.json fields
//   settings.<key>.label             setting label
//   settings.<key>.description       setting description
//   settings.<key>.placeholder       text/textarea placeholder
//   settings.<key>.options.<value>   select option label
//   shortcuts.<id>.label             shortcut label
//
// JavaScript strings use luma.i18n.t(<key>, <English fallback>). English text
// remains in plugin.json and in the JavaScript fallback; en.json may list every
// key with its English text as the translation template.
QString pluginLanguage(ConfigManager *config);
// Flat keys for text declared in plugin.json; plugin manager and tests build
// them through these helpers so the format stays in one place.
QString pluginSettingTextKey(const QString &settingKey, const QString &field);
QString pluginSettingOptionTextKey(const QString &settingKey, const QVariant &value);
QString pluginShortcutTextKey(const QString &shortcutId);
// Loads en.json first and overlays the requested language, so a missing key
// falls back to English instead of to the raw key.
QVariantMap loadPluginTranslations(const QString &pluginDirectory,
                                   const QString &language);
// Looks up key, then (for locale files written before translation keys) the
// English fallback text used as a key, then returns the fallback (empty when
// the caller has none).
QString translatedPluginText(const QVariantMap &translations, const QString &key,
                             const QString &fallback = QString());
