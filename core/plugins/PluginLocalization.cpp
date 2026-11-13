#include "plugins/PluginLocalization.hpp"
#include "app/ConfigManager.hpp"

#include <QFile>
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

QVariantMap loadPluginTranslations(const QString &pluginDirectory,
                                   const QString &language)
{
    auto load = [&pluginDirectory](const QString &code) -> QVariantMap {
        QFile file(pluginDirectory + QStringLiteral("/locales/") + code
                   + QStringLiteral(".json"));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return {};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject())
            return {};
        return document.object().toVariantMap();
    };

    QVariantMap result = load(language);
    if (result.isEmpty() && language != QLatin1String("en"))
        result = load(QStringLiteral("en"));
    return result;
}

QString translatedPluginText(const QVariantMap &translations, const QString &text)
{
    return translations.value(text, text).toString();
}
