#include "Translator.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

Translator* g_translator = nullptr;

Translator::Translator(QObject *parent) : QObject(parent) {}

void Translator::setLanguage(const QString &lang) {
    m_translations.clear();
    m_currentLang = lang;
    QString path = ":/locales/" + lang + ".json";
    QFile f(path);

    if (f.open(QIODevice::ReadOnly)) {
        QByteArray data = f.readAll();
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

        if (parseError.error == QJsonParseError::NoError) {
            QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                m_translations[it.key()] = it.value().toString();
            }
            qDebug() << "=== TRANSLATOR LOADED ===";
            qDebug() << "File:" << path << "| Keys:" << m_translations.size();
            qDebug() << "osd_crop_not_found:" << m_translations.value("osd_crop_not_found", "!!! MISSING !!!");
            qDebug() << "dialog_video_filter:" << m_translations.value("dialog_video_filter", "!!! MISSING !!!");
            qDebug() << "========================";
        } else {
            qDebug() << "JSON PARSE ERROR:" << parseError.errorString();
        }
    } else {
        qDebug() << "Failed to open translation file:" << path;
    }
    emit langChanged();
}

QString Translator::tr(const QString &key) const {
    return m_translations.value(key, key);
}
