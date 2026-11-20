// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

#include "plugins/PluginLocalization.hpp"
#include "app/ConfigManager.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

QString pluginLanguage(ConfigManager *config)
{
    QString language = config ? config->language() : QStringLiteral("auto");
    if (language.isEmpty() || language == QLatin1String("auto"))
        language = QLocale::system().name();

    QLocale locale(language);
    QString code = QLocale::languageToCode(locale.language()).toLower();
    if (code.isEmpty() || code == QLatin1String("c"))
        code = QStringLiteral("en");
    return code;
}

QString pluginSettingTextKey(const QString &settingKey, const QString &field)
{
    return QStringLiteral("settings.") + settingKey + QLatin1Char('.') + field;
}

QString pluginSettingOptionTextKey(const QString &settingKey, const QVariant &value)
{
    return pluginSettingTextKey(settingKey,
                                QStringLiteral("options.") + value.toString());
}

QString pluginShortcutTextKey(const QString &shortcutId)
{
    return QStringLiteral("shortcuts.") + shortcutId + QStringLiteral(".label");
}

QVariantMap loadPluginTranslations(const QString &pluginDirectory,
                                   const QString &language)
{
    auto load = [&pluginDirectory](const QString &code) -> QVariantMap {
        const QString pluginRoot = QFileInfo(pluginDirectory).canonicalFilePath();
        const QString localesRoot = QFileInfo(pluginDirectory
                                              + QStringLiteral("/locales"))
                                        .canonicalFilePath();
        if (pluginRoot.isEmpty() || localesRoot.isEmpty()
                || !localesRoot.startsWith(pluginRoot + QLatin1Char('/')))
            return {};
        const QString filePath = QFileInfo(localesRoot + QLatin1Char('/') + code
                                           + QStringLiteral(".json"))
                                     .canonicalFilePath();
        if (filePath.isEmpty()
                || !filePath.startsWith(localesRoot + QLatin1Char('/')))
            return {};
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            return {};
        return document.object().toVariantMap();
    };

    // English is the template every other language falls back to per key, so
    // a partial translation never shows raw key names.
    QVariantMap result = load(QStringLiteral("en"));
    if (language != QLatin1String("en")) {
        const QVariantMap localized = load(language);
        for (auto it = localized.cbegin(); it != localized.cend(); ++it) {
            if (!it.value().toString().isEmpty())
                result.insert(it.key(), it.value());
        }
    }
    return result;
}

QString translatedPluginText(const QVariantMap &translations, const QString &key,
                             const QString &fallback)
{
    if (key.isEmpty())
        return fallback;

    QString translated = translations.value(key).toString();
    if (!translated.isEmpty())
        return translated;

    // Locale files written before translation keys used the English source
    // text as the key. Keep reading those so installed plugins keep their
    // translations until their authors move to keys.
    if (!fallback.isEmpty()) {
        const QString legacy = translations.value(fallback).toString();
        if (!legacy.isEmpty())
            return legacy;
    }
    // An empty fallback means the caller has no English text for this key
    // (for example an optional plugin.json field); returning the key would
    // leak "info" or "description" into the UI.
    return fallback;
}
