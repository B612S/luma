#include "PluginApi.hpp"
#include "PluginManager.hpp"
#include "PluginWindow.hpp"
#include "PluginRuntime.hpp"
#include "MediaManager.hpp"
#include "OsdManager.hpp"
#include "PlaylistManager.hpp"
#include "PlaylistModel.hpp"
#include <QDir>
#include <algorithm>
#include <functional>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QProcess>
#include <QPointer>
#include <memory>
#include <QTimer>
#include <QStandardPaths>
#include <QDebug>

// ---------------------------------------------------------------- helpers

static bool isCallable(const QJSValue &value)
{
    return value.isCallable();
}

// Raw mpv access must not become a way to execute a shell or load a script.
// Normal playback commands (seek, loadfile, set volume, ...) remain available.
static bool isDangerousMpvProperty(const QString &name)
{
    const QString lower = name.toLower();
    return lower == QLatin1String("script")
            || lower == QLatin1String("scripts")
            || lower == QLatin1String("script-opts")
            || lower == QLatin1String("load-script")
            || lower.startsWith(QLatin1String("input-conf"))
            || lower.startsWith(QLatin1String("ytdl-raw-options"))
            || lower == QLatin1String("include")
            || lower == QLatin1String("config")
            || lower == QLatin1String("stream-open-filename")
            || lower.startsWith(QLatin1String("script-"));
}

static bool isDangerousMpvCommand(const QStringList &args)
{
    if (args.isEmpty())
        return false;
    const QString command = args.value(0).toLower();
    if (command == QLatin1String("run")
            || command == QLatin1String("subprocess")
            || command == QLatin1String("load-script")
            || command == QLatin1String("loadfile")
            || command == QLatin1String("loadlist")
            || command == QLatin1String("define-section")
            || command == QLatin1String("enable-section")
            || command == QLatin1String("disable-section")
            || command == QLatin1String("keybind")
            || command.startsWith(QLatin1String("script"))) {
        return true;
    }
    if ((command == QLatin1String("set") || command == QLatin1String("setprop")
         || command == QLatin1String("change-list"))
            && args.size() > 1) {
        return isDangerousMpvProperty(args.value(1));
    }
    return false;
}

static void callJs(const QJSValue &callback, const QJSValue &argument,
                   const QString &pluginId)
{
    if (!isCallable(callback))
        return;
    const QJSValue result = callback.call({argument});
    if (result.isError()) {
        qWarning().noquote() << "[plugin:" << pluginId << "]" << result.toString();
    }
}

// ---------------------------------------------------------------- events

PluginEventsApi::PluginEventsApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

void PluginEventsApi::on(const QString &event, const QJSValue &handler)
{
    m_runtime->addEventHandler(event, handler, false);
}

void PluginEventsApi::once(const QString &event, const QJSValue &handler)
{
    m_runtime->addEventHandler(event, handler, true);
}

// ---------------------------------------------------------------- log

PluginLogApi::PluginLogApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

void PluginLogApi::info(const QString &message)
{
    qInfo().noquote() << "[plugin:" << m_runtime->manifest().id << "]" << message;
}

void PluginLogApi::warn(const QString &message)
{
    qWarning().noquote() << "[plugin:" << m_runtime->manifest().id << "]" << message;
}

void PluginLogApi::error(const QString &message)
{
    qCritical().noquote() << "[plugin:" << m_runtime->manifest().id << "]" << message;
}

// ---------------------------------------------------------------- media

PluginMediaApi::PluginMediaApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

bool PluginMediaApi::allowed(const char *permission) const
{
    return m_runtime->api() && m_runtime->api()->hasPermission(permission);
}

QVariantMap PluginMediaApi::state() const
{
    if (!allowed("media:read") || !m_runtime->manager()->media())
        return {};
    return m_runtime->manager()->media()->pluginState();
}

void PluginMediaApi::play()
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->pluginSetPaused(false);
}

void PluginMediaApi::pause()
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->pluginSetPaused(true);
}

void PluginMediaApi::togglePause()
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->pluginTogglePause();
}

void PluginMediaApi::seek(double position)
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->pluginSeek(position);
}

void PluginMediaApi::setVolume(int volume)
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->pluginSetVolume(volume);
}

void PluginMediaApi::next()
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->playNext();
}

void PluginMediaApi::previous()
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->playPrev();
}

void PluginMediaApi::stop()
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return;
    m_runtime->manager()->media()->pluginStop();
}

bool PluginMediaApi::open(const QString &path)
{
    if (!allowed("media:control") || !m_runtime->manager()->media()) return false;
    return m_runtime->manager()->media()->pluginOpen(path);
}

// ---------------------------------------------------------------- playlists

PluginPlaylistApi::PluginPlaylistApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

bool PluginPlaylistApi::allowed(const char *permission) const
{
    return m_runtime->api() && m_runtime->api()->hasPermission(permission);
}

QVariantList PluginPlaylistApi::list() const
{
    if (!allowed("playlists:read") || !m_runtime->manager()->playlists())
        return {};
    QVariantList result = m_runtime->manager()->playlists()->playlists();
    const QString playing = m_runtime->manager()->playlists()->playingId();
    for (QVariant &value : result) {
        QVariantMap map = value.toMap();
        map.insert(QStringLiteral("isPlaying"), map.value(QStringLiteral("id")).toString() == playing);
        value = map;
    }
    return result;
}

QString PluginPlaylistApi::active() const
{
    if (!allowed("playlists:read") || !m_runtime->manager()->playlists())
        return {};
    return m_runtime->manager()->playlists()->activeId();
}

QVariantList PluginPlaylistApi::entries(const QString &id) const
{
    if (!allowed("playlists:read") || !m_runtime->manager()->playlists())
        return {};
    PlaylistModel *model = m_runtime->manager()->playlists()->modelById(id);
    return model ? model->toVariantList() : QVariantList();
}

bool PluginPlaylistApi::add(const QString &id, const QStringList &paths)
{
    if (!allowed("playlists:write") || !m_runtime->manager()->playlists()
            || paths.isEmpty())
        return false;
    m_runtime->manager()->playlists()->addFilesToPlaylist(id, paths);
    return true;
}

int PluginPlaylistApi::remove(const QString &id, const QVariantList &indices)
{
    if (!allowed("playlists:write") || !m_runtime->manager()->playlists())
        return 0;
    PlaylistModel *model = m_runtime->manager()->playlists()->modelById(id);
    if (!model)
        return 0;
    QVector<int> rows;
    for (const QVariant &value : indices) {
        const int row = value.toInt();
        if (row >= 0 && row < model->rowCount() && !rows.contains(row))
            rows.append(row);
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        model->removeItem(row);
    return rows.size();
}

QString PluginPlaylistApi::create(const QString &name)
{
    if (!allowed("playlists:write") || !m_runtime->manager()->playlists())
        return {};
    return m_runtime->manager()->playlists()->createPlaylist(name);
}

bool PluginPlaylistApi::setMode(const QString &id, const QString &mode)
{
    if (!allowed("playlists:write") || !m_runtime->manager()->playlists())
        return false;
    if (!m_runtime->manager()->playlists()->modelById(id))
        return false;
    m_runtime->manager()->playlists()->setPlaylistMode(id, mode);
    return true;
}

// ---------------------------------------------------------------- config

PluginConfigApi::PluginConfigApi(PluginRuntime *runtime, const QString &dataDir,
                                 QObject *parent)
    : QObject(parent), m_runtime(runtime), m_dataDir(dataDir),
      m_settings(dataDir + QStringLiteral("/config.ini"), QSettings::IniFormat)
{
}

QVariant PluginConfigApi::get(const QString &key, const QVariant &fallback) const
{
    QVariant value = m_settings.value(key, fallback);
    // INI files store booleans as text; plugins expect a real bool when the
    // declared fallback is bool.
    if (fallback.userType() == QMetaType::Bool && value.userType() == QMetaType::QString) {
        const QString text = value.toString().trimmed();
        if (text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
                || text == QLatin1String("1"))
            value = true;
        else if (text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0
                 || text == QLatin1String("0"))
            value = false;
    }
    return value;
}

void PluginConfigApi::set(const QString &key, const QVariant &value)
{
    if (key.isEmpty())
        return;
    m_settings.setValue(key, value);
    m_settings.sync();
}

// ---------------------------------------------------------------- storage

PluginStorageApi::PluginStorageApi(PluginRuntime *runtime, const QString &dataDir,
                                   QObject *parent)
    : QObject(parent), m_runtime(runtime), m_dataDir(dataDir)
{
}

bool PluginStorageApi::validName(const QString &name) const
{
    return !name.isEmpty() && !name.contains(QLatin1Char('/'))
            && !name.contains(QLatin1Char('\\')) && !name.contains(QStringLiteral(".."));
}

QString PluginStorageApi::readText(const QString &name) const
{
    if (!m_runtime->api()
            || !m_runtime->api()->hasPermission("filesystem:plugin")
            || !validName(name))
        return {};
    QFile file(m_dataDir + QLatin1Char('/') + name);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

bool PluginStorageApi::writeText(const QString &name, const QString &text)
{
    if (!m_runtime->api()
            || !m_runtime->api()->hasPermission("filesystem:plugin")
            || !validName(name))
        return false;
    QDir().mkpath(m_dataDir);
    QFile file(m_dataDir + QLatin1Char('/') + name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    return file.write(text.toUtf8()) >= 0;
}

bool PluginStorageApi::exists(const QString &name) const
{
    if (!m_runtime->api()
            || !m_runtime->api()->hasPermission("filesystem:plugin")
            || !validName(name))
        return false;
    return QFileInfo::exists(m_dataDir + QLatin1Char('/') + name);
}

// ---------------------------------------------------------------- network

PluginNetApi::PluginNetApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime), m_network(new QNetworkAccessManager(this))
{
}

void PluginNetApi::fetch(const QString &url, const QVariantMap &options,
                         const QJSValue &callback)
{
    if (!m_runtime->api()->hasPermission("network") || !isCallable(callback))
        return;
    const QUrl target(url);
    if (!target.isValid()
            || (target.scheme() != QStringLiteral("http")
                && target.scheme() != QStringLiteral("https"))) {
        QJSEngine *engine = m_runtime->findChild<QJSEngine *>();
        if (engine) {
            QVariantMap response;
            response.insert(QStringLiteral("ok"), false);
            response.insert(QStringLiteral("error"), QStringLiteral("unsupported URL"));
            callJs(callback, engine->toScriptValue(response), m_runtime->manifest().id);
        }
        return;
    }

    QNetworkRequest request(target);
    const QString method = options.value(QStringLiteral("method"),
                                         QStringLiteral("GET")).toString().toUpper();
    const QVariantMap headers = options.value(QStringLiteral("headers")).toMap();
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        request.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());
    const int timeout = qBound(1000, options.value(QStringLiteral("timeoutMs"), 15000).toInt(), 60000);

    QNetworkReply *reply = method == QLatin1String("POST")
        ? m_network->post(request, options.value(QStringLiteral("body")).toByteArray())
        : m_network->get(request);

    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    timer->start(timeout);

    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > 2 * 1024 * 1024)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [this, reply, callback, timer]() {
        timer->stop();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QVariantMap headers;
        for (const auto &pair : reply->rawHeaderPairs())
            headers.insert(QString::fromUtf8(pair.first), QString::fromUtf8(pair.second));
        QVariantMap response;
        response.insert(QStringLiteral("ok"), reply->error() == QNetworkReply::NoError);
        response.insert(QStringLiteral("status"), status);
        response.insert(QStringLiteral("headers"), headers);
        response.insert(QStringLiteral("body"), QString::fromUtf8(body.left(2 * 1024 * 1024)));
        response.insert(QStringLiteral("error"), reply->error() == QNetworkReply::NoError
                         ? QString() : reply->errorString());
        if (QJSEngine *engine = m_runtime->findChild<QJSEngine *>())
            callJs(callback, engine->toScriptValue(response), m_runtime->manifest().id);
        reply->deleteLater();
    });
}

// ---------------------------------------------------------------- mpv

PluginMpvApi::PluginMpvApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

QVariant PluginMpvApi::getProperty(const QString &name) const
{
    if (!m_runtime->api() || !m_runtime->api()->hasPermission("mpv:raw")
            || !m_runtime->manager()->media())
        return {};
    return m_runtime->manager()->media()->pluginMpvGetProperty(name);
}

bool PluginMpvApi::setProperty(const QString &name, const QVariant &value)
{
    if (!m_runtime->api() || !m_runtime->api()->hasPermission("mpv:raw")
            || !m_runtime->manager()->media())
        return false;
    if (isDangerousMpvProperty(name)) {
        qWarning().noquote() << "[plugin:" << m_runtime->manifest().id
                             << "] blocked raw mpv property:" << name;
        return false;
    }
    m_runtime->manager()->media()->pluginMpvSetProperty(name, value);
    return true;
}

bool PluginMpvApi::command(const QVariantList &args)
{
    if (!m_runtime->api() || !m_runtime->api()->hasPermission("mpv:raw")
            || !m_runtime->manager()->media())
        return false;
    QStringList command;
    for (const QVariant &arg : args)
        command << arg.toString();
    if (isDangerousMpvCommand(command)) {
        qWarning().noquote() << "[plugin:" << m_runtime->manifest().id
                             << "] blocked raw mpv command:" << command.value(0);
        return false;
    }
    return m_runtime->manager()->media()->pluginMpvCommand(command);
}

// ---------------------------------------------------------------- processes

PluginProcessApi::PluginProcessApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

bool PluginProcessApi::allowed() const
{
    return m_runtime->api() && m_runtime->api()->hasPermission("process");
}

bool PluginProcessApi::supported(const QString &program) const
{
    // With explicit approval the plugin may start any executable available on
    // this computer. There is still no shell interpolation: arguments are
    // passed as an explicit list.
    return !program.trimmed().isEmpty();
}

QString PluginProcessApi::path(const QString &program) const
{
    if (!allowed() || !supported(program))
        return {};
    return QStandardPaths::findExecutable(program);
}

void PluginProcessApi::abortAll()
{
    m_aborted = true;
    const QList<QProcess *> processes = findChildren<QProcess *>();
    for (QProcess *process : processes) {
        process->disconnect();
        process->kill();
        process->deleteLater();
    }
}

void PluginProcessApi::run(const QString &program, const QStringList &args,
                           const QVariantMap &options, const QJSValue &callback)
{
    if (!allowed() || !isCallable(callback) || m_aborted)
        return;
    const QPointer<PluginProcessApi> apiGuard(this);
    const QPointer<PluginRuntime> runtimeGuard(m_runtime);
    if (!supported(program)) {
        QVariantMap response;
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"),
                        QStringLiteral("unsupported program: %1").arg(program));
        callJs(callback, m_runtime->engine()->toScriptValue(response),
               m_runtime->manifest().id);
        return;
    }
    const QString programPath = QStandardPaths::findExecutable(program);
    if (programPath.isEmpty()) {
        QVariantMap response;
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), program + QStringLiteral(" is not installed"));
        callJs(callback, m_runtime->engine()->toScriptValue(response),
               m_runtime->manifest().id);
        return;
    }

    QStringList processArgs = args;
    if (program == QLatin1String("yt-dlp")) {
        static const QStringList forbidden = {
            QStringLiteral("--exec"), QStringLiteral("--exec-before-download"),
            QStringLiteral("--config-location"), QStringLiteral("--batch-file"),
            QStringLiteral("--load-info-json"), QStringLiteral("--update"),
            QStringLiteral("--rm-cache-dir"), QStringLiteral("--downloader"),
            QStringLiteral("--external-downloader"), QStringLiteral("--plugin-dirs")
        };
        for (const QString &arg : processArgs) {
            for (const QString &bad : forbidden) {
                if (arg == bad || arg.startsWith(bad + QLatin1Char('='))
                        || (bad == QLatin1String("--config-location")
                            && arg.startsWith(QStringLiteral("--config-locations")))) {
                    QVariantMap response;
                    response.insert(QStringLiteral("ok"), false);
                    response.insert(QStringLiteral("error"),
                                    QStringLiteral("yt-dlp option is not allowed: %1").arg(bad));
                    callJs(callback, m_runtime->engine()->toScriptValue(response),
                           m_runtime->manifest().id);
                    return;
                }
            }
        }
        if (!processArgs.contains(QStringLiteral("--ignore-config")))
            processArgs.prepend(QStringLiteral("--ignore-config"));
    }

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->setWorkingDirectory(m_runtime->api()->dataDir());
    proc->setProgram(programPath);
    proc->setArguments(processArgs);

    auto output = std::make_shared<QByteArray>();
    auto errors = std::make_shared<QByteArray>();
    auto reported = std::make_shared<bool>(false);
    auto timedOut = std::make_shared<bool>(false);
    const int maxBytes = qBound(1024, options.value(QStringLiteral("maxOutputBytes"),
                                                    2 * 1024 * 1024).toInt(),
                                8 * 1024 * 1024);
    const int timeoutMs = qBound(1000, options.value(QStringLiteral("timeoutMs"),
                                                     60000).toInt(), 600000);

    auto collect = [proc, output, errors, maxBytes](bool stdoutChannel) {
        QByteArray &target = stdoutChannel ? *output : *errors;
        QByteArray chunk = stdoutChannel ? proc->readAllStandardOutput()
                                         : proc->readAllStandardError();
        if (target.size() < maxBytes)
            target.append(chunk.left(maxBytes - target.size()));
    };
    QObject::connect(proc, &QProcess::readyReadStandardOutput, proc,
                     [collect]() { collect(true); });
    QObject::connect(proc, &QProcess::readyReadStandardError, proc,
                     [collect]() { collect(false); });

    QObject::connect(proc, &QProcess::errorOccurred, proc,
                     [this, proc, callback, reported, program, apiGuard, runtimeGuard]
                     (QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *reported)
            return;
        *reported = true;
        QVariantMap response;
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("exitCode"), -1);
        response.insert(QStringLiteral("stdout"), QString());
        response.insert(QStringLiteral("stderr"), QString());
        response.insert(QStringLiteral("error"), QStringLiteral("failed to start %1").arg(program));
        if (!apiGuard || apiGuard->m_aborted || !runtimeGuard
                || !runtimeGuard->engine()) {
            proc->deleteLater();
            return;
        }
        callJs(callback, runtimeGuard->engine()->toScriptValue(response),
               runtimeGuard->manifest().id);
        proc->deleteLater();
    });
    QObject::connect(proc, &QProcess::finished, proc,
                     [this, proc, callback, output, errors, maxBytes, reported, timedOut,
                      collect, apiGuard, runtimeGuard]
                     (int code, QProcess::ExitStatus status) {
        if (*reported)
            return;
        *reported = true;
        collect(true);
        collect(false);
        QVariantMap response;
        response.insert(QStringLiteral("ok"),
                        !*timedOut && status == QProcess::NormalExit && code == 0);
        response.insert(QStringLiteral("exitCode"), code);
        response.insert(QStringLiteral("stdout"), QString::fromUtf8(output->left(maxBytes)));
        response.insert(QStringLiteral("stderr"), QString::fromUtf8(errors->left(maxBytes)));
        response.insert(QStringLiteral("error"), *timedOut
                        ? QStringLiteral("timeout")
                        : (status == QProcess::NormalExit ? QString()
                                                          : proc->errorString()));
        if (!apiGuard || apiGuard->m_aborted || !runtimeGuard
                || !runtimeGuard->engine()) {
            proc->deleteLater();
            return;
        }
        callJs(callback, runtimeGuard->engine()->toScriptValue(response),
               runtimeGuard->manifest().id);
        proc->deleteLater();
    });

    QTimer::singleShot(timeoutMs, proc, [proc, timedOut]() {
        if (proc->state() != QProcess::NotRunning) {
            *timedOut = true;
            proc->kill();
        }
    });

    proc->start();
    if (options.contains(QStringLiteral("stdin"))) {
        proc->write(options.value(QStringLiteral("stdin")).toByteArray());
        proc->closeWriteChannel();
    }
}

// ---------------------------------------------------------------- i18n

PluginI18nApi::PluginI18nApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

QString PluginI18nApi::t(const QString &key, const QString &fallback) const
{
    if (!m_runtime->manager())
        return fallback.isEmpty() ? key : fallback;
    const QString translated = m_runtime->manager()->pluginTranslate(
        m_runtime->manifest(), key);
    if (translated == key && !fallback.isEmpty())
        return fallback;
    return translated;
}

// ---------------------------------------------------------------- stats

PluginStatsApi::PluginStatsApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

bool PluginStatsApi::allowed() const
{
    return m_runtime->api() && m_runtime->api()->hasPermission("stats");
}

QVariantMap PluginStatsApi::snapshot() const
{
    if (!allowed() || !m_runtime->manager()->media())
        return {};
    return m_runtime->manager()->media()->pluginStats();
}

void PluginStatsApi::set(const QString &key, const QString &label,
                         const QVariant &value)
{
    if (!allowed() || key.isEmpty())
        return;
    m_runtime->setStat(key, label, value);
}

void PluginStatsApi::remove(const QString &key)
{
    if (allowed())
        m_runtime->removeStat(key);
}

void PluginStatsApi::clear()
{
    if (allowed())
        m_runtime->clearStats();
}

QVariantMap PluginStatsApi::setPage(const QString &id, const QString &title,
                                    const QVariantList &content,
                                    int preferredSlot)
{
    if (!allowed())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("missing stats permission")}};
    return m_runtime->setStatsPage(id, title, content, preferredSlot);
}

void PluginStatsApi::removePage(const QString &id)
{
    if (allowed())
        m_runtime->removeStatsPage(id);
}

void PluginStatsApi::clearPages()
{
    if (allowed())
        m_runtime->clearStatsPages();
}

void PluginStatsApi::showPage(const QString &id)
{
    if (allowed())
        m_runtime->manager()->showStatsPage(id);
}

// ---------------------------------------------------------------- shortcuts

PluginShortcutsApi::PluginShortcutsApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

bool PluginShortcutsApi::allowed() const
{
    return m_runtime->api() && m_runtime->api()->hasPermission("shortcuts");
}

bool PluginShortcutsApi::add(const QString &key, const QJSValue &callback)
{
    return allowed() && m_runtime->addShortcut(key, callback);
}

void PluginShortcutsApi::remove(const QString &key)
{
    if (allowed())
        m_runtime->removeShortcut(key);
}

bool PluginShortcutsApi::on(const QString &id, const QJSValue &callback)
{
    return allowed() && m_runtime->addShortcutAction(id.trimmed(), callback);
}

// ---------------------------------------------------------------- UI

PluginUiApi::PluginUiApi(PluginRuntime *runtime, QObject *parent)
    : QObject(parent), m_runtime(runtime)
{
}

bool PluginUiApi::allowed(const char *permission) const
{
    return m_runtime->api() && m_runtime->api()->hasPermission(permission);
}

void PluginUiApi::osd(const QString &text)
{
    if (!allowed("osd") || !m_runtime->manager()->osd())
        return;
    m_runtime->manager()->osd()->showInfo(text);
}

void PluginUiApi::menuAdd(const QVariantMap &spec, const QJSValue &callback)
{
    if (!allowed("menus") || !isCallable(callback))
        return;
    m_runtime->addMenuAction(spec, callback);
}

QObject *PluginUiApi::window(const QVariantMap &spec, const QJSValue &callback)
{
    if (!allowed("windows") || !isCallable(callback))
        return nullptr;
    auto *window = new PluginWindow(m_runtime->manager(), m_runtime,
                                    spec, callback, m_runtime);
    m_runtime->registerWindow(window);
    m_runtime->manager()->addWindow(window);
    return window;
}

// ---------------------------------------------------------------- facade

PluginApi::PluginApi(PluginRuntime *runtime, const PluginManifest &manifest,
                     QObject *parent)
    : QObject(parent), m_runtime(runtime), m_manifest(manifest)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString dataRoot = base + QStringLiteral("/luma/plugins/data");
    m_dataDir = dataRoot + QLatin1Char('/') + manifest.id;
    QDir().mkpath(m_dataDir);

    m_events = new PluginEventsApi(runtime, this);
    m_log = new PluginLogApi(runtime, this);
    m_media = new PluginMediaApi(runtime, this);
    m_playlists = new PluginPlaylistApi(runtime, this);
    m_config = new PluginConfigApi(runtime, m_dataDir, this);
    m_storage = new PluginStorageApi(runtime, m_dataDir, this);
    m_net = new PluginNetApi(runtime, this);
    m_mpv = new PluginMpvApi(runtime, this);
    m_process = new PluginProcessApi(runtime, this);
    m_i18n = new PluginI18nApi(runtime, this);
    m_stats = new PluginStatsApi(runtime, this);
    m_shortcuts = new PluginShortcutsApi(runtime, this);
    m_ui = new PluginUiApi(runtime, this);
}

bool PluginApi::hasPermission(const char *permission) const
{
    return m_manifest.permissions.contains(QString::fromLatin1(permission));
}

PluginManager *PluginApi::manager() const
{
    return m_runtime->manager();
}
