#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QString>

// Tests must not touch the user's real settings: we replace
// XDG_CONFIG_HOME/XDG_CACHE_HOME with a temporary folder BEFORE QCoreApplication
// is created and before any access to QStandardPaths.
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
        // PlaylistManager probes radio links for their station name; tests must
        // stay offline (they use a fake yt-dlp for link titles).
        qputenv("LUMA_TEST_NO_NETWORK", "1");
    }
    return root;
}

inline void cleanupTestEnvironment(const QString &root)
{
    if (root.isEmpty()) return;
    QDir(root).removeRecursively();
}
