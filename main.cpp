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
        QString sysPath = "/usr/share/luma/translations/" + lang + ".qm";
        QString localPath = QCoreApplication::applicationDirPath() + "/" + lang + ".qm";

        if (translator->load(sysPath) || translator->load(localPath)) {
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

    // ---- Смена языка на лету ----
    QObject::connect(&config, &ConfigManager::languageChanged, [&]() {
        loadLanguage(config.language());
        engine.retranslate();
    });

    // ---- Размер окна ----
    int w = qMax(400, config.value("window/width", 960).toInt());
    int h = qMax(300, config.value("window/height", 640).toInt());
    root->setWidth(w);
    root->setHeight(h);

    // ---- Mpv и зависимости ----
    auto mpv = root->findChild<MpvItem*>("mpv");
    if (mpv) {
        media.setMpv(mpv);
        osd.setMpv(mpv);

        if (config.rememberVolume()) {
            mpv->setVolume(config.volume());
        } else {
            mpv->setVolume(config.defaultVolume());
        }

        // ---- Субтитры: применяем настройки и реагируем на изменения ----
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

    // ---- Связь: смена активного плейлиста -> обновление QML-биндинга ----
    QObject::connect(&playlists, &PlaylistManager::activeModelChanged,
                     &media, &MediaManager::playlistChanged);

    handleArguments(app.arguments(), media, root);

    // ---- Единственный экземпляр: приём новых аргументов ----
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

    // ---- Сохранение состояния при выходе ----
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        // В режиме PiP окно маленькое — сохраняем размер, который был до него.
        QSize windowSize(root->width(), root->height());
        if (root->property("pipMode").toBool()) {
            const QRect saved = root->property("pipSavedGeometry").toRect();
            windowSize = (saved.width() > 0 && saved.height() > 0) ? saved.size() : QSize(960, 640);
        }
        if (windowSize.width() < 400 || windowSize.height() < 300)
            windowSize = QSize(960, 640);
        config.saveWindowSize(windowSize.width(), windowSize.height());
        config.setPlaylistMode(media.playMode());
        castManager.stopCast();

        if (mpv && config.rememberVolume()) {
            config.setVolume(static_cast<int>(mpv->volume()));
        }
    });

    return app.exec();
}
