#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QString>

// Тесты не должны трогать настоящие настройки пользователя: подменяем
// XDG_CONFIG_HOME/XDG_CACHE_HOME на временную папку ДО создания QCoreApplication
// и любых обращений к QStandardPaths.
inline QString initTestEnvironment()
{
    static QString root;
    if (root.isEmpty()) {
        root = QDir::tempPath() + "/luma-tests-"
               + QString::number(QCoreApplication::applicationPid()) + "-"
               + QString::number(QDateTime::currentMSecsSinceEpoch());
        QDir(root).removeRecursively();
        QDir().mkpath(root + "/config");
        QDir().mkpath(root + "/cache");
        qputenv("XDG_CONFIG_HOME", (root + "/config").toUtf8());
        qputenv("XDG_CACHE_HOME", (root + "/cache").toUtf8());
    }
    return root;
}

inline void cleanupTestEnvironment(const QString &root)
{
    if (root.isEmpty()) return;
    QDir(root).removeRecursively();
}
