#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

// Runtime management for yt-dlp, its EJS package and Deno: locates system or
// Luma-local copies, probes versions/updates in the background, and installs
// the local copies without touching the system Python installation.
class YtDlpRuntime : public QObject {
    Q_OBJECT

public:
    explicit YtDlpRuntime(QObject *parent = nullptr);

    void refreshYtdlpRuntime();
    void installYtdlp();
    void installYtdlpEjs();
    void installDeno();

    // Locations of Luma's private copies; the stream resolver uses the same
    // directories and the current runtime snapshot.
    QString ytdlpLocalDir() const;
    QString ytdlpEjsLocalDir() const;
    QString denoLocalDir() const;
    QVariantMap runtime() const { return m_ytdlpRuntime; }

signals:
    void ytdlpInstallFinished(bool success, const QString &message);
    void ytdlpEjsInstallFinished(bool success, const QString &message);
    void denoInstallFinished(bool success, const QString &message);
    void ytdlpRuntimeReady(const QVariantMap &info);

private:
    QString localPackageVersion(const QString &dir, const QString &module) const;
    void probeSystemYtdlpVersion(const QString &exe);
    void probeSystemEjs(const QString &python);
    void checkLocalPackageUpdate(const QString &package, const QString &localVersion,
                                 const QString &latestKey, const QString &updateKey,
                                 bool *running);
    void probeDenoVersion(const QString &exe);
    void checkDenoUpdate(const QString &localVersion);
    void runWheelInstall(const QString &package, const QString &dir,
                         std::function<void(bool, const QString &)> done);
    void runDenoInstall(std::function<void(bool, const QString &)> done);

    QVariantMap m_ytdlpRuntime;
    bool m_ytdlpVersionProbeRunning = false;
    bool m_ejsProbeRunning = false;
    bool m_ytdlpUpdateCheckRunning = false;
    bool m_ejsUpdateCheckRunning = false;
    bool m_denoVersionProbeRunning = false;
    bool m_denoUpdateCheckRunning = false;
};
