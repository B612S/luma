#pragma once
#include <QObject>
#include <QMap>

class Translator : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentLang READ currentLang NOTIFY langChanged)
public:
    explicit Translator(QObject *parent = nullptr);
    Q_INVOKABLE void setLanguage(const QString &lang);
    Q_INVOKABLE QString tr(const QString &key) const;
    QString currentLang() const { return m_currentLang; }

signals:
    void langChanged();

private:
    QMap<QString, QString> m_translations;
    QString m_currentLang;
};

extern Translator* g_translator;
