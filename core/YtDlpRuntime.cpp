#include "YtDlpRuntime.hpp"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QVersionNumber>

YtDlpRuntime::YtDlpRuntime(QObject *parent)
    : QObject(parent)
{
}

QString YtDlpRuntime::ytdlpLocalDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/luma/yt-dlp");
}

QString YtDlpRuntime::ytdlpEjsLocalDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/luma/yt-dlp-ejs");
}

QString YtDlpRuntime::denoLocalDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/luma/deno");
}

QString YtDlpRuntime::localPackageVersion(const QString &dir, const QString &module) const
{
    // Wheels contain either version.py (yt-dlp) or _version.py (yt-dlp-ejs).
    // Read the declared version instead of starting a Python process.
    const QStringList candidates = {
        dir + QLatin1Char('/') + module + QStringLiteral("/version.py"),
        dir + QLatin1Char('/') + module + QStringLiteral("/_version.py"),
        dir + QLatin1Char('/') + module + QStringLiteral("/__init__.py")
    };
    static const QRegularExpression versionPattern(
        QStringLiteral("(?:__)?version(?:__)?\\s*=\\s*['\"]([^'\"]+)['\"]"));
    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QString text = QString::fromUtf8(file.readAll());
        const QRegularExpressionMatch match = versionPattern.match(text);
        if (match.hasMatch())
            return match.captured(1);
    }
    return {};
}

void YtDlpRuntime::probeSystemYtdlpVersion(const QString &exe)
{
    if (exe.isEmpty() || m_ytdlpVersionProbeRunning)
        return;
    m_ytdlpVersionProbeRunning = true;
    auto *probe = new QProcess(this);
    connect(probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, probe](int code, QProcess::ExitStatus status) {
                m_ytdlpVersionProbeRunning = false;
                const QString version = QString::fromUtf8(
                    probe->readAllStandardOutput()).trimmed();
                probe->deleteLater();
                if (!m_ytdlpRuntime.value(QStringLiteral("ytdlpSystem")).toBool())
                    return;
                m_ytdlpRuntime.insert(QStringLiteral("ytdlpSystemVersion"), version);
                if (status == QProcess::NormalExit && code == 0)
                    m_ytdlpRuntime.insert(QStringLiteral("ytdlpVersion"), version);
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
            });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_ytdlpVersionProbeRunning = false;
        probe->deleteLater();
    });
    probe->start(exe, {QStringLiteral("--version")});
}

void YtDlpRuntime::probeSystemEjs(const QString &python)
{
    if (python.isEmpty() || m_ejsProbeRunning)
        return;
    m_ejsProbeRunning = true;
    auto *probe = new QProcess(this);
    connect(probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, probe](int code, QProcess::ExitStatus status) {
                m_ejsProbeRunning = false;
                const QString version = QString::fromUtf8(
                    probe->readAllStandardOutput()).trimmed();
                probe->deleteLater();
                const bool available = status == QProcess::NormalExit && code == 0;

                QVariantMap info = m_ytdlpRuntime;
                const bool local = info.value(QStringLiteral("ejsLocal")).toBool();
                info.insert(QStringLiteral("ejsSystem"), available);
                info.insert(QStringLiteral("ejsSystemVersion"), available ? version : QString());
                info.insert(QStringLiteral("ejsSource"),
                            available ? QStringLiteral("system")
                                      : (local ? QStringLiteral("local") : QString()));
                info.insert(QStringLiteral("ejsAvailable"), available || local);
                info.insert(QStringLiteral("ejsVersion"),
                            available ? version
                                      : info.value(QStringLiteral("ejsLocalVersion")).toString());
                if (available) {
                    info.insert(QStringLiteral("ejsLatestVersion"), QString());
                    info.insert(QStringLiteral("ejsUpdateAvailable"), false);
                }
                m_ytdlpRuntime = info;
                emit ytdlpRuntimeReady(m_ytdlpRuntime);

                // If the system package disappeared, the local one (when
                // present) is now the active one and can be checked for updates.
                if (!available && local && !m_ejsUpdateCheckRunning)
                    checkLocalPackageUpdate(QStringLiteral("yt-dlp-ejs"),
                                            info.value(QStringLiteral("ejsLocalVersion")).toString(),
                                            QStringLiteral("ejsLatestVersion"),
                                            QStringLiteral("ejsUpdateAvailable"),
                                            &m_ejsUpdateCheckRunning);
            });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_ejsProbeRunning = false;
        probe->deleteLater();
    });
    probe->start(python, {QStringLiteral("-c"),
                          QStringLiteral("import yt_dlp_ejs as e; print(getattr(e, 'version', ''))")});
}

void YtDlpRuntime::checkLocalPackageUpdate(const QString &package,
                                           const QString &localVersion,
                                           const QString &latestKey,
                                           const QString &updateKey,
                                           bool *running)
{
    if (!running || *running || localVersion.isEmpty())
        return;
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        return;
    *running = true;
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, package, localVersion, latestKey, updateKey, running]
            (int code, QProcess::ExitStatus status) {
                *running = false;
                const QString latest = QString::fromUtf8(
                    proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                if (status != QProcess::NormalExit || code != 0 || latest.isEmpty())
                    return;

                const bool system = package == QLatin1String("yt-dlp")
                    ? m_ytdlpRuntime.value(QStringLiteral("ytdlpSystem")).toBool()
                    : m_ytdlpRuntime.value(QStringLiteral("ejsSystem")).toBool();
                QVariantMap info = m_ytdlpRuntime;
                if (system) {
                    info.insert(latestKey, QString());
                    info.insert(updateKey, false);
                    m_ytdlpRuntime = info;
                    emit ytdlpRuntimeReady(m_ytdlpRuntime);
                    return;
                }

                const QVersionNumber current = QVersionNumber::fromString(localVersion);
                const QVersionNumber newest = QVersionNumber::fromString(latest);
                const bool newer = !current.isNull() && !newest.isNull()
                    ? newest > current : latest != localVersion;
                info.insert(latestKey, latest);
                info.insert(updateKey, newer);
                if (newer) {
                    qInfo().noquote() << "yt-dlp update available:"
                                      << package << localVersion << "->" << latest;
                }
                m_ytdlpRuntime = info;
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
            });
    connect(proc, &QProcess::errorOccurred, this, [proc, running](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        *running = false;
        proc->deleteLater();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"),
                        QStringLiteral("import json, sys, urllib.request\n"
                                       "with urllib.request.urlopen("
                                       "\"https://pypi.org/pypi/%s/json\" % sys.argv[1], timeout=20) as r:\n"
                                       "    print(json.load(r)[\"info\"][\"version\"])"),
                        package});
    proc->start();
}

void YtDlpRuntime::probeDenoVersion(const QString &exe)
{
    if (exe.isEmpty() || m_denoVersionProbeRunning)
        return;
    m_denoVersionProbeRunning = true;
    auto *probe = new QProcess(this);
    connect(probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, probe, exe](int code, QProcess::ExitStatus status) {
                m_denoVersionProbeRunning = false;
                const QString firstLine = QString::fromUtf8(
                    probe->readAllStandardOutput()).split(QLatin1Char('\n')).value(0).trimmed();
                probe->deleteLater();
                if (m_ytdlpRuntime.value(QStringLiteral("denoPath")).toString() != exe)
                    return;
                const QString version = status == QProcess::NormalExit && code == 0
                    ? firstLine.section(QLatin1Char(' '), 1, 1).trimmed() : QString();
                m_ytdlpRuntime.insert(QStringLiteral("denoVersion"), version);
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
                if (!version.isEmpty()
                        && !m_ytdlpRuntime.value(QStringLiteral("denoSystem")).toBool())
                    checkDenoUpdate(version);
            });
    connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_denoVersionProbeRunning = false;
        probe->deleteLater();
    });
    probe->start(exe, {QStringLiteral("--version")});
}

void YtDlpRuntime::checkDenoUpdate(const QString &localVersion)
{
    if (m_denoUpdateCheckRunning || localVersion.isEmpty())
        return;
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty())
        return;
    m_denoUpdateCheckRunning = true;
    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, localVersion](int code, QProcess::ExitStatus status) {
                m_denoUpdateCheckRunning = false;
                const QString latest = QString::fromUtf8(
                    proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                if (status != QProcess::NormalExit || code != 0 || latest.isEmpty())
                    return;
                QVariantMap info = m_ytdlpRuntime;
                if (info.value(QStringLiteral("denoSystem")).toBool()) {
                    info.insert(QStringLiteral("denoLatestVersion"), QString());
                    info.insert(QStringLiteral("denoUpdateAvailable"), false);
                    m_ytdlpRuntime = info;
                    emit ytdlpRuntimeReady(m_ytdlpRuntime);
                    return;
                }
                const QVersionNumber current = QVersionNumber::fromString(localVersion);
                const QVersionNumber newest = QVersionNumber::fromString(latest);
                const bool newer = !current.isNull() && !newest.isNull()
                    ? newest > current : latest != localVersion;
                info.insert(QStringLiteral("denoLatestVersion"), latest);
                info.insert(QStringLiteral("denoUpdateAvailable"), newer);
                m_ytdlpRuntime = info;
                emit ytdlpRuntimeReady(m_ytdlpRuntime);
            });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_denoUpdateCheckRunning = false;
        proc->deleteLater();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"),
                        QStringLiteral("import json, urllib.request\n"
                                       "with urllib.request.urlopen("
                                       "\"https://api.github.com/repos/denoland/deno/releases/latest\", "
                                       "timeout=20) as r:\n"
                                       "    print(json.load(r)[\"tag_name\"].lstrip(\"v\"))")});
    proc->start();
}

void YtDlpRuntime::refreshYtdlpRuntime()
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    const QString systemYtdlp = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"));
    const QString ytdlpDir = ytdlpLocalDir();
    const QString ejsDir = ytdlpEjsLocalDir();
    const bool localYtdlp = QFileInfo(ytdlpDir + QStringLiteral("/yt_dlp/__init__.py")).exists();
    const bool localEjs = QFileInfo(ejsDir + QStringLiteral("/yt_dlp_ejs/__init__.py")).exists();
    const QString systemDeno = QStandardPaths::findExecutable(QStringLiteral("deno"));
    const QString denoDir = denoLocalDir();
    const QString localDenoPath = denoDir + QStringLiteral("/deno");
    const bool localDeno = QFileInfo(localDenoPath).isExecutable();
    const QString denoPath = !systemDeno.isEmpty()
        ? systemDeno : (localDeno ? localDenoPath : QString());
    const bool denoSystem = !systemDeno.isEmpty();
    const QString previousDenoPath = m_ytdlpRuntime.value(QStringLiteral("denoPath")).toString();
    const QString denoVersion = previousDenoPath == denoPath
        ? m_ytdlpRuntime.value(QStringLiteral("denoVersion")).toString() : QString();

    // Keep the previous probe result until the new one arrives, so the status
    // in the dialog does not flicker while Python starts.
    const bool systemEjs = !python.isEmpty()
        && m_ytdlpRuntime.value(QStringLiteral("ejsSystem")).toBool();
    const QString systemYtdlpVersion = systemYtdlp.isEmpty()
        ? QString() : m_ytdlpRuntime.value(QStringLiteral("ytdlpSystemVersion")).toString();
    const QString systemEjsVersion = systemEjs
        ? m_ytdlpRuntime.value(QStringLiteral("ejsSystemVersion")).toString() : QString();
    const QString localYtdlpVersion = localPackageVersion(ytdlpDir, QStringLiteral("yt_dlp"));
    const QString localEjsVersion = localPackageVersion(ejsDir, QStringLiteral("yt_dlp_ejs"));

    const QString ytdlpSource = !systemYtdlp.isEmpty()
        ? QStringLiteral("system") : (localYtdlp ? QStringLiteral("local") : QString());
    const QString ejsSource = systemEjs
        ? QStringLiteral("system") : (localEjs ? QStringLiteral("local") : QString());

    QVariantMap info;
    info.insert(QStringLiteral("ytdlp"), !systemYtdlp.isEmpty() || localYtdlp);
    info.insert(QStringLiteral("ytdlpLocal"), localYtdlp);
    info.insert(QStringLiteral("ytdlpSystem"), !systemYtdlp.isEmpty());
    info.insert(QStringLiteral("ytdlpPath"),
                !systemYtdlp.isEmpty() ? systemYtdlp : (localYtdlp ? ytdlpDir : QString()));
    info.insert(QStringLiteral("ytdlpSource"), ytdlpSource);
    info.insert(QStringLiteral("ytdlpVersion"),
                !systemYtdlp.isEmpty() ? systemYtdlpVersion : localYtdlpVersion);
    info.insert(QStringLiteral("ytdlpLocalVersion"), localYtdlpVersion);
    info.insert(QStringLiteral("ytdlpSystemVersion"), systemYtdlpVersion);
    info.insert(QStringLiteral("ytdlpLatestVersion"), QString());
    info.insert(QStringLiteral("ytdlpUpdateAvailable"), false);

    info.insert(QStringLiteral("ejsLocal"), localEjs);
    info.insert(QStringLiteral("ejsSystem"), systemEjs);
    info.insert(QStringLiteral("ejsAvailable"), systemEjs || localEjs);
    info.insert(QStringLiteral("ejsSource"), ejsSource);
    info.insert(QStringLiteral("ejsVersion"),
                systemEjs ? systemEjsVersion : localEjsVersion);
    info.insert(QStringLiteral("ejsLocalVersion"), localEjsVersion);
    info.insert(QStringLiteral("ejsSystemVersion"), systemEjsVersion);
    info.insert(QStringLiteral("ejsLatestVersion"), QString());
    info.insert(QStringLiteral("ejsUpdateAvailable"), false);
    info.insert(QStringLiteral("ejsLocalDir"), ejsDir);
    info.insert(QStringLiteral("deno"), !denoPath.isEmpty());
    info.insert(QStringLiteral("denoPath"), denoPath);
    info.insert(QStringLiteral("denoSystem"), denoSystem);
    info.insert(QStringLiteral("denoSource"),
                denoSystem ? QStringLiteral("system")
                           : (localDeno ? QStringLiteral("local") : QString()));
    info.insert(QStringLiteral("denoVersion"), denoVersion);
    info.insert(QStringLiteral("denoLocalDir"), denoDir);
    info.insert(QStringLiteral("denoLatestVersion"), QString());
    info.insert(QStringLiteral("denoUpdateAvailable"), false);
    info.insert(QStringLiteral("python"), python);
    m_ytdlpRuntime = info;
    emit ytdlpRuntimeReady(m_ytdlpRuntime);

    if (!systemYtdlp.isEmpty())
        probeSystemYtdlpVersion(systemYtdlp);
    if (!python.isEmpty())
        probeSystemEjs(python);
    if (!denoPath.isEmpty())
        probeDenoVersion(denoPath);

    // Updates are checked only for our local copies, and only while no system
    // package of the same kind is installed.
    if (systemYtdlp.isEmpty() && localYtdlp)
        checkLocalPackageUpdate(QStringLiteral("yt-dlp"), localYtdlpVersion,
                                QStringLiteral("ytdlpLatestVersion"),
                                QStringLiteral("ytdlpUpdateAvailable"),
                                &m_ytdlpUpdateCheckRunning);
    if (!systemEjs && localEjs)
        checkLocalPackageUpdate(QStringLiteral("yt-dlp-ejs"), localEjsVersion,
                                QStringLiteral("ejsLatestVersion"),
                                QStringLiteral("ejsUpdateAvailable"),
                                &m_ejsUpdateCheckRunning);
}

void YtDlpRuntime::runWheelInstall(const QString &package, const QString &dir,
                                   std::function<void(bool, const QString &)> done)
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        done(false, QCoreApplication::translate("MediaManager", "ytdlp_python_missing"));
        return;
    }
    const QString parent = QFileInfo(dir).absolutePath();
    if (!parent.isEmpty() && !QDir().mkpath(parent)) {
        done(false, QCoreApplication::translate("MediaManager", "ytdlp_install_dir_failed").arg(parent));
        return;
    }

    // Install the pure-Python wheel without pip. pip is not part of a Python
    // installation on many distributions, and installing into the system is
    // explicitly not wanted: the files only go to Luma's own data directory.
    const QString script = QStringLiteral(R"PY(
import json, os, shutil, sys, tempfile, urllib.request, zipfile
pkg, target = sys.argv[1], sys.argv[2]
request = urllib.request.Request("https://pypi.org/pypi/%s/json" % pkg,
                                 headers={"User-Agent": "Luma"})
with urllib.request.urlopen(request, timeout=30) as response:
    info = json.load(response)
version = info["info"]["version"]
wheel = next((u for u in info["urls"] if u["packagetype"] == "bdist_wheel"), None)
if wheel is None:
    raise RuntimeError("no wheel for " + pkg)
tmp = tempfile.mkdtemp(prefix="luma-wheel-")
stage = target + ".new"
try:
    archive = os.path.join(tmp, wheel["filename"])
    with urllib.request.urlopen(wheel["url"], timeout=120) as response, open(archive, "wb") as out:
        shutil.copyfileobj(response, out)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    with zipfile.ZipFile(archive) as z:
        z.extractall(stage)
    shutil.rmtree(target, ignore_errors=True)
    os.replace(stage, target)
    print(version)
except Exception:
    shutil.rmtree(stage, ignore_errors=True)
    raise
finally:
    shutil.rmtree(tmp, ignore_errors=True)
)PY");

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, done](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(
                    proc->readAllStandardOutput() + proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                done(status == QProcess::NormalExit && code == 0,
                     output.isEmpty() ? QString() : output.right(1000));
            });
    connect(proc, &QProcess::errorOccurred, this, [proc, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        done(false, QCoreApplication::translate("MediaManager", "ytdlp_install_start_failed"));
    });
    QTimer::singleShot(180000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"), script, package, dir});
    proc->start();
}

void YtDlpRuntime::installYtdlp()
{
    runWheelInstall(QStringLiteral("yt-dlp"), ytdlpLocalDir(),
                    [this](bool ok, const QString &message) {
                        if (ok)
                            refreshYtdlpRuntime();
                        emit ytdlpInstallFinished(ok, message);
                    });
}

void YtDlpRuntime::installYtdlpEjs()
{
    runWheelInstall(QStringLiteral("yt-dlp-ejs"), ytdlpEjsLocalDir(),
                    [this](bool ok, const QString &message) {
                        if (ok)
                            refreshYtdlpRuntime();
                        emit ytdlpEjsInstallFinished(ok, message);
                    });
}

void YtDlpRuntime::runDenoInstall(std::function<void(bool, const QString &)> done)
{
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (python.isEmpty()) {
        done(false, QCoreApplication::translate("MediaManager", "ytdlp_python_missing"));
        return;
    }
    const QString cpu = QSysInfo::currentCpuArchitecture();
    QString arch;
    if (cpu == QLatin1String("x86_64"))
        arch = QStringLiteral("x86_64");
    else if (cpu == QLatin1String("arm64") || cpu == QLatin1String("aarch64"))
        arch = QStringLiteral("aarch64");
    else {
        done(false, QCoreApplication::translate("MediaManager", "ytdlp_deno_arch_unsupported").arg(cpu));
        return;
    }
    const QString dir = denoLocalDir();
    const QString parent = QFileInfo(dir).absolutePath();
    if (!parent.isEmpty() && !QDir().mkpath(parent)) {
        done(false, QCoreApplication::translate("MediaManager", "ytdlp_install_dir_failed").arg(parent));
        return;
    }

    const QString script = QStringLiteral(R"PY(
import os, shutil, sys, tempfile, urllib.request, zipfile
target, arch = sys.argv[1], sys.argv[2]
url = ("https://github.com/denoland/deno/releases/latest/download/"
       "deno-%s-unknown-linux-gnu.zip" % arch)
tmp = tempfile.mkdtemp(prefix="luma-deno-")
stage = target + ".new"
try:
    archive = os.path.join(tmp, "deno.zip")
    with urllib.request.urlopen(url, timeout=180) as response, open(archive, "wb") as out:
        shutil.copyfileobj(response, out)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    with zipfile.ZipFile(archive) as z:
        z.extractall(stage)
    binary = os.path.join(stage, "deno")
    if not os.path.isfile(binary):
        raise RuntimeError("deno binary is missing from the archive")
    os.chmod(binary, 0o755)
    shutil.rmtree(target, ignore_errors=True)
    os.replace(stage, target)
except Exception:
    shutil.rmtree(stage, ignore_errors=True)
    raise
finally:
    shutil.rmtree(tmp, ignore_errors=True)
)PY");

    auto *proc = new QProcess(this);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [proc, done](int code, QProcess::ExitStatus status) {
                const QString output = QString::fromUtf8(
                    proc->readAllStandardOutput() + proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                done(status == QProcess::NormalExit && code == 0,
                     output.isEmpty() ? QString() : output.right(1000));
            });
    connect(proc, &QProcess::errorOccurred, this, [proc, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        proc->deleteLater();
        done(false, QCoreApplication::translate("MediaManager", "ytdlp_install_start_failed"));
    });
    QTimer::singleShot(300000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning)
            proc->kill();
    });
    proc->setProgram(python);
    proc->setArguments({QStringLiteral("-c"), script, dir, arch});
    proc->start();
}

void YtDlpRuntime::installDeno()
{
    runDenoInstall([this](bool ok, const QString &message) {
        if (ok)
            refreshYtdlpRuntime();
        emit denoInstallFinished(ok, message);
    });
}

