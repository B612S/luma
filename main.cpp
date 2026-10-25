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

#include "core/ConfigManager.hpp"
#include "core/MprisManager.hpp"
#include "core/MediaManager.hpp"
#include "core/OsdManager.hpp"
#include "core/ShortcutHandler.hpp"
#include "core/Logger.hpp"
#include "core/ThumbnailProvider.hpp"
#include "mpvqt/MpvItem.hpp"

void ensureMpvConfig() {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/luma";
    QDir().mkpath(configDir);
    QString configPath = configDir + "/mpv.conf";

    QFile f(configPath);
    QString content;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        content = QString::fromUtf8(f.readAll());
        f.close();
    }

    bool modified = false;

    if (content.isEmpty()) {
        content = "# mpv Configuration for Luma\n";
    }

    if (!content.contains("deinterlace=")) {
        content += "\n# Автоматический деинтерлейс (включается только для чересстрочного видео)\ndeinterlace=auto\n";
        modified = true;
    }

    if (!content.contains("hwdec=")) {
        content += "\nhwdec=auto\n";
        modified = true;
    }

    if (!content.contains("video-sync=")) {
        content += "\nvideo-sync=display-resample\n";
        modified = true;
    }

    if (!content.contains("ytdl=")) {
        content += "\nytdl=no\n";
        modified = true;
    }

    if (modified || !QFile::exists(configPath)) {
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&f);
            out << content;
            f.close();
        }
    }
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
    qputenv("QT_QPA_PLATFORM", QByteArray("wayland;xcb"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QApplication app(argc, argv);
    app.setApplicationName("luma");
    app.setApplicationVersion("1.0.0");
    app.setWindowIcon(QIcon(":/assets/luma.png"));

    ensureMpvConfig();

    ConfigManager config;
    Logger::instance().setMode(config.value("ui/debug_logging", "info").toString());

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

    // --- ДИНАМИЧЕСКИЙ ПЕРЕВОДЧИК ---
    QTranslator *translator = new QTranslator(&app);

    auto loadLanguage = [&](const QString &lang) {
        app.removeTranslator(translator);
        QString sysPath = "/usr/share/luma/translations/luma_" + lang + ".qm";
        QString localPath = QCoreApplication::applicationDirPath() + "/luma_" + lang + ".qm";

        if (translator->load(sysPath) || translator->load(localPath)) {
            app.installTranslator(translator);
        }
    };

    loadLanguage(config.language());

    MediaManager media(nullptr, &app);
    media.setConfig(&config);
    media.setPlayMode(config.playlistMode());
    OsdManager osd(nullptr, &app);

    QQmlApplicationEngine engine;

    engine.addImageProvider("thumbnail", new ThumbnailProvider());

    engine.rootContext()->setContextProperty("Config", &config);
    engine.rootContext()->setContextProperty("Media", &media);
    engine.rootContext()->setContextProperty("Osd", &osd);

    engine.load(QUrl(QStringLiteral("qrc:/luma/qml/main.qml")));
    if (engine.rootObjects().isEmpty()) return -1;

    auto root = qobject_cast<QQuickWindow*>(engine.rootObjects().first());

    // --- СЛУШАЕМ СМЕНУ ЯЗЫКА ---
    QObject::connect(&config, &ConfigManager::languageChanged, [&]() {
        loadLanguage(config.language());
        engine.retranslate(); // Мгновенно обновляем QML
    });

    int w = config.value("window/width", 960).toInt();
    int h = config.value("window/height", 640).toInt();
    root->setWidth(w);
    root->setHeight(h);

    auto mpv = root->findChild<MpvItem*>("mpv");
    if (mpv) {
        media.setMpv(mpv);
        osd.setMpv(mpv);

        if (config.rememberVolume()) {
            mpv->setVolume(config.volume());
        } else {
            mpv->setVolume(config.defaultVolume());
        }

        auto *shortcuts = new ShortcutHandler(mpv, &osd, &media, root, &app);
        app.installEventFilter(shortcuts);
        engine.rootContext()->setContextProperty("shortcuts", shortcuts);

        QObject::connect(root, SIGNAL(toggleCropRequested()), shortcuts, SLOT(toggleCrop()));
        QObject::connect(shortcuts, &ShortcutHandler::togglePlaylist, root, [root]() {
            auto *panel = root->findChild<QObject*>("playlistPanel");
            if (panel) panel->setProperty("isOpen", !panel->property("isOpen").toBool());
        });
            QObject::connect(shortcuts, &ShortcutHandler::toggleStats, root, [root]() {
                auto *panel = root->findChild<QObject*>("statsPanel");
                if (panel) panel->setProperty("isOpen", !panel->property("isOpen").toBool());
            });
                QObject::connect(shortcuts, &ShortcutHandler::toggleCsd, root, [root]() {
                    if (root->flags() & Qt::FramelessWindowHint) root->setFlags(root->flags() & ~Qt::FramelessWindowHint);
                    else root->setFlags(root->flags() | Qt::FramelessWindowHint);
                    root->show();
                });

                new MprisManager(mpv, &media, &app);
    }

    handleArguments(app.arguments(), media, root);

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

    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        config.saveWindowSize(root->width(), root->height());
        config.setPlaylistMode(media.playMode());

        if (mpv && config.rememberVolume()) {
            config.setVolume(static_cast<int>(mpv->volume()));
        }
    });

    return app.exec();
}
