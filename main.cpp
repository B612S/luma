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
#include <QTranslator>
#include <QLocalSocket>
#include <QLocalServer>
#include <QProcess>
#include <QRect>
#include <QSize>
#include <QUrl>
#include <thread>
#include <chrono>
#include <cstdlib>

#include "core/ConfigManager.hpp"
#include "core/PlaylistManager.hpp"
#include "core/MprisManager.hpp"
#include "core/MediaManager.hpp"
#include "core/OsdManager.hpp"
#include "core/KeyboardLayoutWatcher.hpp"
#include "core/ShortcutHandler.hpp"
#include "core/Logger.hpp"
#include "core/ThumbnailProvider.hpp"
#include "core/CastManager.hpp"
#include "core/SocksHttpProxy.hpp"
#include "mpvqt/MpvItem.hpp"

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
    qputenv("QT_QPA_PLATFORM", QByteArray("wayland"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QApplication app(argc, argv);
    app.setApplicationName("luma");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/assets/luma.png"));

    ConfigManager config;
    Logger::instance().setMode(config.value("ui/debug_logging", "info").toString());
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

    OsdManager osd(nullptr, &app);

    CastManager castManager;

    // Local HTTP bridge for SOCKS proxies: mpv/FFmpeg cannot do SOCKS themselves.
    SocksHttpProxy streamProxy(&app);

    KeyboardLayoutWatcher keyboardLayout;

    QQmlApplicationEngine engine;

    engine.addImageProvider("thumbnail", new ThumbnailProvider());

    engine.rootContext()->setContextProperty("Config", &config);
    engine.rootContext()->setContextProperty("Media", &media);
    engine.rootContext()->setContextProperty("Playlists", &playlists);
    engine.rootContext()->setContextProperty("Osd", &osd);
    engine.rootContext()->setContextProperty("CastManager", &castManager);
    engine.rootContext()->setContextProperty("Keyboard", &keyboardLayout);

    engine.load(QUrl(QStringLiteral("qrc:/luma/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;

    auto root = qobject_cast<QQuickWindow*>(engine.rootObjects().first());

    // ---- Switching the language on the fly ----
    QObject::connect(&config, &ConfigManager::languageChanged, [&]() {
        loadLanguage(config.language());
        engine.retranslate();
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

        auto *shortcuts = new ShortcutHandler(mpv, &osd, &media, root, &app);
        shortcuts->setCastManager(&castManager);

        engine.rootContext()->setContextProperty("shortcuts", shortcuts);

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
            QLocalSocket *clientSocket = server->nextPendingConnection();
            if (!clientSocket) return;
            clientSocket->waitForReadyRead(1000);
            QTextStream in(clientSocket);
            QStringList args;
            while (!in.atEnd()) {
                QString line = in.readLine();
                if (!line.isEmpty()) args << line;
            }
            clientSocket->deleteLater();
            handleArguments(args, media, root);
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
