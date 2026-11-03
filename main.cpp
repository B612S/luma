#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>
#include <QTimer>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTranslator>
#include <QLocalSocket>
#include <QLocalServer>
#include <QProcess>
#include <QRect>
#include <QSize>
#include <QUrl>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "core/ConfigManager.hpp"
#include "core/PlaylistManager.hpp"
#include "core/MprisManager.hpp"
#include "core/MediaManager.hpp"
#include "core/OsdManager.hpp"
#include "core/KWinBridge.hpp"
#include "core/ShortcutHandler.hpp"
#include "core/Logger.hpp"
#include "core/ThumbnailProvider.hpp"
#include "core/CastManager.hpp"
#include "core/SocksHttpProxy.hpp"
#include "mpvqt/MpvItem.hpp"

#ifndef LUMA_VERSION
#define LUMA_VERSION "dev"
#endif

namespace {
// A command line with a few paths is tiny; cap the single-instance handshake.
constexpr qsizetype kMaxInstanceArgs = 64 * 1024;
}

void handleArguments(const QStringList &args, MediaManager &media, QQuickWindow *rootWindow) {
    if (args.size() > 1) {
        QString filePath = args.at(1);
        QTimer::singleShot(100, &media, [&media, filePath]() { media.openFile(filePath); });
    }
    if (rootWindow) {
        rootWindow->show();
        rootWindow->raise();
        rootWindow->requestActivate();
    }
}

int main(int argc, char *argv[]) {
    // Luma is Wayland-only. Without a Wayland session there is no supported
    // platform, so stop with an explanation instead of letting Qt fall back to
    // X11 (or any other platform plugin).
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
            && qEnvironmentVariableIsEmpty("WAYLAND_SOCKET")) {
        // Use stdio here: no Qt application exists yet, and Qt may suppress its
        // message handler when stderr is not a terminal (e.g. launched from a
        // desktop environment or a launcher).
        std::fprintf(stderr,
                     "Luma requires a Wayland session (WAYLAND_DISPLAY is not set). "
                     "X11 is not supported and support for it is not planned.\n");
        return 1;
    }
    // Ignore whatever QT_QPA_PLATFORM contains (xcb, offscreen, …): only the
    // Wayland platform plugin is supported.
    qputenv("QT_QPA_PLATFORM", QByteArray("wayland"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QApplication app(argc, argv);

    if (!QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
        std::fprintf(stderr,
                     "Luma requires the Wayland platform plugin, but Qt initialized \"%s\" instead.\n",
                     qPrintable(QGuiApplication::platformName()));
        return 1;
    }
    // Use the KDE widget style for native dialogs and for qqc2-desktop-style's
    // style items. QT_STYLE_OVERRIDE would also change the QML controls style,
    // which must remain org.kde.desktop.
    const QStringList styleKeys = QStyleFactory::keys();
    for (const QString &key : styleKeys) {
        if (key.compare(QStringLiteral("Breeze"), Qt::CaseInsensitive) != 0)
            continue;
        if (QStyle *breeze = QStyleFactory::create(key))
            QApplication::setStyle(breeze);
        break;
    }
    // The AppImage bundles a small hicolor icon set; use it there so the
    // controls do not depend on a KDE icon theme being installed.
    if (qEnvironmentVariableIsSet("LUMA_APPIMAGE"))
        QIcon::setThemeName(QStringLiteral("hicolor"));
    app.setApplicationName("luma");
    app.setApplicationVersion(QString::fromLatin1(LUMA_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("luma"));
    app.setWindowIcon(QIcon(":/assets/luma.png"));

    ConfigManager config;
    Logger::instance().setMode(config.lumaLogMode());
    Logger::instance().setComponentMode(QStringLiteral("mpv"), config.mpvLogMode());
    Logger::instance().setComponentMode(QStringLiteral("yt-dlp"), config.ytdlpLogMode());
    Logger::installMessageHandler();

    // ---- Single instance ----
    QString serverName = "luma_single_instance_server";
    QLocalSocket socket;
    QLocalServer *server = nullptr;
    bool isPrimaryInstance = true;

    if (config.singleInstance()) {
        socket.connectToServer(serverName);
        if (socket.waitForConnected(500)) {
            isPrimaryInstance = false;
            QTextStream stream(&socket);
            for (const QString &arg : app.arguments()) {
                stream << arg << "\n";
            }
            stream.flush();
            socket.waitForBytesWritten(1000);
            socket.disconnectFromServer();
            return 0;
        }

        QLocalServer::removeServer(serverName);
        server = new QLocalServer(&app);
        // Only the current user may forward arguments to this instance.
        server->setSocketOptions(QLocalServer::UserAccessOption);
        if (!server->listen(serverName)) {
            qWarning() << "Failed to start local server:" << server->errorString();
        }
    }

    if (!isPrimaryInstance) return 0;

    // ---- Translations ----
    QTranslator *translator = new QTranslator(&app);

    auto loadLanguage = [&](const QString &lang) {
        app.removeTranslator(translator);
        // Translations are embedded in the binary (qt_add_translations), so the
        // resource is tried first: it always matches this build.
        const QString resourcePath = QStringLiteral(":/i18n/") + lang + ".qm";
        const QString sysPath = "/usr/share/luma/translations/" + lang + ".qm";
        const QString localPath = QCoreApplication::applicationDirPath() + "/" + lang + ".qm";

        if (translator->load(resourcePath)
                || translator->load(sysPath)
                || translator->load(localPath)) {
            app.installTranslator(translator);
        }
    };

    loadLanguage(config.language());

    // ---- Managers ----
    PlaylistManager playlists(&config);

    MediaManager media(nullptr, &app);
    media.setConfig(&config);
    media.setPlaylistManager(&playlists);
    media.setPlayMode(config.playlistMode());

    // The settings define the Default playlist's mode only. Changing it (also
    // from another instance) must not override a named playlist's own mode.
    QObject::connect(&config, &ConfigManager::playlistModeChanged, &media, [&]() {
        if (playlists.activeId() == PlaylistManager::DEFAULT_ID)
            media.setPlayMode(config.playlistMode());
    });

    OsdManager osd(nullptr, &app);

    CastManager castManager;

    // Keeps the picture-in-picture window above the others on KDE Plasma.
    KWinBridge kwinBridge;

    // Local HTTP bridge for SOCKS proxies: mpv/FFmpeg cannot do SOCKS themselves.
    SocksHttpProxy streamProxy(&app);


    // The picture-in-picture window draws its own shadow inside the window.
    // The transparent margin around it keeps the composition free of the 1px
    // edge artifact that KWin/NVIDIA shows around a bare window.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QQmlApplicationEngine engine;

    engine.addImageProvider("thumbnail", new ThumbnailProvider());

    engine.rootContext()->setContextProperty("Config", &config);
    engine.rootContext()->setContextProperty("Media", &media);
    engine.rootContext()->setContextProperty("Playlists", &playlists);
    engine.rootContext()->setContextProperty("Osd", &osd);
    engine.rootContext()->setContextProperty("CastManager", &castManager);
    engine.rootContext()->setContextProperty("KWinBridge", &kwinBridge);

    auto *shortcuts = new ShortcutHandler(nullptr, nullptr, nullptr, nullptr, &app,
                                          config.singleInstance());
    engine.rootContext()->setContextProperty("shortcuts", shortcuts);

    QObject::connect(&config, &ConfigManager::singleInstanceChanged, shortcuts,
                     [shortcuts, &config]() {
        shortcuts->setGlobalShortcutsAllowed(config.singleInstance());
    });

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/luma/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;

    auto root = qobject_cast<QQuickWindow*>(engine.rootObjects().first());

    // ---- Switching the language on the fly ----
    QObject::connect(&config, &ConfigManager::languageChanged, [&]() {
        loadLanguage(config.language());
        engine.retranslate();
        // The playlist names (including "Default") are translated on the C++
        // side, so the tab bar and the menus need a refresh as well.
        playlists.retranslate();
    });

    // ---- Window size ----
    int w = qMax(400, config.value("window/width", 960).toInt());
    int h = qMax(300, config.value("window/height", 640).toInt());
    root->setWidth(w);
    root->setHeight(h);

    // ---- Mpv and dependencies ----
    auto mpv = root->findChild<MpvItem*>("mpv");
    if (mpv) {
        media.setMpv(mpv);
        osd.setMpv(mpv);

        // ---- Logging (each component writes its own file) ----
        QObject::connect(mpv, &MpvItem::mpvLogMessage, &app,
                         [](const QString &prefix, const QString &level, const QString &message) {
            Logger::instance().writeComponent(
                QStringLiteral("mpv"),
                QStringLiteral("[%1] [%2] %3").arg(prefix, level, message));
        });
        auto applyLogSettings = [&config, mpv]() {
            Logger::instance().setMode(config.lumaLogMode());
            Logger::instance().setComponentMode(QStringLiteral("mpv"), config.mpvLogMode());
            Logger::instance().setComponentMode(QStringLiteral("yt-dlp"), config.ytdlpLogMode());
            mpv->setLogLevel(config.mpvLogMode());
        };
        applyLogSettings();
        QObject::connect(&config, &ConfigManager::loggingChanged, mpv, applyLogSettings);

        // Show the yt-dlp link opening status on top of the video.
        QObject::connect(&media, &MediaManager::downloadStatus, &osd,
                         [&osd](const QString &message) {
                             if (!message.isEmpty()) osd.showInfo(message);
                         });

        if (config.rememberVolume()) {
            mpv->setVolume(config.volume());
        } else {
            mpv->setVolume(config.defaultVolume());
        }

        // ---- Audio output and hardware decoding ----
        // An empty string for "ao" means automatic driver selection:
        // mpv has no "auto" value (it disables sound).
        auto applyPlaybackSettings = [&config, mpv]() {
            const QString audio = config.audioOutput();
            mpv->setProperty("ao", audio == QLatin1String("auto") ? QStringLiteral("") : audio);
            mpv->setProperty("hwdec", config.hwdec());
        };
        applyPlaybackSettings();
        QObject::connect(&config, &ConfigManager::audioOutputChanged, mpv, applyPlaybackSettings);
        QObject::connect(&config, &ConfigManager::hwdecChanged, mpv, applyPlaybackSettings);

        // ---- Proxy for streams ----
        // yt-dlp gets the proxy on its own, but mpv/FFmpeg cannot do SOCKS. For them
        // we start a local HTTP bridge: mpv downloads the stream through it, so
        // Range requests and seeking work as with plain HTTP.
        auto applyStreamProxy = [&config, mpv, &streamProxy]() {
            const QString proxy = config.ytdlpProxy();
            const int sep = proxy.indexOf(QStringLiteral("://"));
            const QString scheme = sep > 0 ? proxy.left(sep).toLower() : QString();

            if (scheme == QLatin1String("socks5") || scheme == QLatin1String("socks5h")) {
                if (streamProxy.start(QUrl(proxy))) {
                    mpv->setProperty("http-proxy", streamProxy.localUrl());
                } else {
                    qWarning() << "SocksHttpProxy:" << streamProxy.lastError();
                    mpv->setProperty("http-proxy", QStringLiteral(""));
                }
            } else if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
                // FFmpeg understands an HTTP proxy itself, no bridge needed.
                streamProxy.stop();
                mpv->setProperty("http-proxy", proxy);
            } else {
                streamProxy.stop();
                mpv->setProperty("http-proxy", QStringLiteral(""));
            }
        };
        applyStreamProxy();
        QObject::connect(&config, &ConfigManager::ytdlpSettingsChanged, mpv, applyStreamProxy);

        // ---- Subtitles: apply the settings and react to changes ----
        auto applySubtitleSettings = [&config, mpv]() {
            mpv->setProperty("sub-font", config.subtitleFont());
            mpv->setProperty("sub-font-size", config.subtitleFontSize());
            mpv->setProperty("sub-color", config.subtitleColor());
            mpv->setProperty("sub-bold", config.subtitleBold() ? QStringLiteral("yes") : QStringLiteral("no"));
            mpv->setProperty("sub-border-size", config.subtitleBorderSize());
            mpv->setProperty("sub-border-color", config.subtitleBorderColor());
            mpv->setProperty("sub-back-color", config.subtitleBackground() ? QStringLiteral("#80000000")
                                                                           : QStringLiteral("#00000000"));
            mpv->setProperty("sub-pos", config.subtitlePosition());
            mpv->setProperty("sub-ass-override", config.subtitleIgnoreFileStyle() ? QStringLiteral("force")
                                                                                   : QStringLiteral("scale"));
        };
        applySubtitleSettings();
        QObject::connect(&config, &ConfigManager::subtitlesChanged, mpv, applySubtitleSettings);

        shortcuts->setMpv(mpv);
        shortcuts->setOsd(&osd);
        shortcuts->setMedia(&media);
        shortcuts->setWindow(root);
        shortcuts->setCastManager(&castManager);

        QObject::connect(root, SIGNAL(toggleCropRequested()), shortcuts, SLOT(toggleCrop()));
        QObject::connect(shortcuts, &ShortcutHandler::togglePlaylist, root, [root]() {
            if (root->property("pipMode").toBool()) return;
            auto *panel = root->findChild<QObject*>("playlistPanel");
            if (panel) panel->setProperty("isOpen", !panel->property("isOpen").toBool());
        });
            QObject::connect(shortcuts, &ShortcutHandler::toggleStats, root, [root]() {
                if (root->property("pipMode").toBool()) return;
                auto *panel = root->findChild<QObject*>("statsPanel");
                if (panel) panel->setProperty("isOpen", !panel->property("isOpen").toBool());
            });
                QObject::connect(shortcuts, &ShortcutHandler::togglePip, root, [root]() {
                    QMetaObject::invokeMethod(root, "togglePip");
                });
                QObject::connect(shortcuts, &ShortcutHandler::fitWindowToVideo, root, [root]() {
                    QMetaObject::invokeMethod(root, "fitWindowToVideo");
                });
                QObject::connect(shortcuts, &ShortcutHandler::shortcutsHelpVisibleChanged, root,
                                 [root](bool visible) { root->setProperty("shortcutsHelpVisible", visible); });

                new MprisManager(mpv, &media, &app);
    }

    // Guard against a hang on exit: if stopping mpv/the bridge or tearing
    // down QML does not finish within 5 seconds, the process is terminated
    // forcibly. This does not affect a normal exit.
    bool exitWatchdogStarted = false;
    auto startExitWatchdog = [&]() {
        if (exitWatchdogStarted) return;
        exitWatchdogStarted = true;
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            std::_Exit(0);
        }).detach();
    };

    QObject::connect(root, &QQuickWindow::closing, &app, [&]() {
        startExitWatchdog();
        // Do not block the close handler: stop goes to the mpv thread,
        // the main stop and the bridge shutdown happen in aboutToQuit.
        if (mpv) mpv->command(QStringList() << QStringLiteral("stop"));
    });

    // ---- Wiring: active playlist change -> QML binding update ----
    QObject::connect(&playlists, &PlaylistManager::activeModelChanged,
                     &media, &MediaManager::playlistChanged);

    handleArguments(app.arguments(), media, root);

    // ---- Single instance: accepting new arguments ----
    if (config.singleInstance() && server) {
        QObject::connect(server, &QLocalServer::newConnection, [server, &media, root]() {
            while (QLocalSocket *clientSocket = server->nextPendingConnection()) {
                auto buffer = std::make_shared<QByteArray>();
                auto done = std::make_shared<bool>(false);

                // Read asynchronously: waiting on the socket blocked the GUI
                // thread for up to a second if a client sent data late.
                auto finish = [clientSocket, buffer, done, &media, root]() {
                    if (*done) return;
                    *done = true;
                    const qsizetype room = kMaxInstanceArgs - buffer->size();
                    if (room > 0)
                        buffer->append(clientSocket->read(room));

                    QStringList args;
                    const QList<QByteArray> lines = buffer->split('\n');
                    for (QByteArray line : lines) {
                        if (line.endsWith('\r')) line.chop(1);
                        if (!line.isEmpty() && args.size() < 64)
                            args << QString::fromUtf8(line);
                    }
                    clientSocket->deleteLater();
                    handleArguments(args, media, root);
                };

                QObject::connect(clientSocket, &QLocalSocket::readyRead, clientSocket,
                                 [clientSocket, buffer]() {
                    const qsizetype room = kMaxInstanceArgs - buffer->size();
                    if (room > 0) {
                        buffer->append(clientSocket->read(room));
                    } else {
                        // Discard, but do not allocate an unbounded buffer.
                        clientSocket->read(8192);
                    }
                });
                QObject::connect(clientSocket, &QLocalSocket::disconnected, clientSocket, finish);
                // Also handle a client that keeps the connection open.
                QTimer::singleShot(1000, clientSocket, finish);
            }
        });
    }

    // ---- Saving state on exit ----
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        // Save the state first: if the watchdog timer fires,
        // the settings are already written.
        QSize windowSize(root->width(), root->height());
        if (root->property("pipMode").toBool()) {
            const QRect saved = root->property("pipSavedGeometry").toRect();
            windowSize = (saved.width() > 0 && saved.height() > 0) ? saved.size() : QSize(960, 640);
        }
        if (windowSize.width() < 400 || windowSize.height() < 300)
            windowSize = QSize(960, 640);
        config.saveWindowSize(windowSize.width(), windowSize.height());
        // Only the Default playlist's mode belongs in the settings — named
        // playlists saved theirs when the mode was changed.
        if (playlists.activeId() == PlaylistManager::DEFAULT_ID)
            config.setPlaylistMode(media.playMode());
        if (mpv && config.rememberVolume())
            config.setVolume(static_cast<int>(mpv->volume()));

        // Stop playback and the bridge before mpv/QML teardown, so
        // network threads do not leave the process hanging with audio still playing.
        startExitWatchdog();
        if (mpv) {
            mpv->commandBlocking(QStringList() << QStringLiteral("stop"));
            mpv->setPaused(true);
        }
        streamProxy.stop();
        castManager.stopCast();
    });

    return app.exec();
}
