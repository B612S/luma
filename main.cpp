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
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTranslator>
#include <QLocale>
#include <QLocalSocket>
#include <QLocalServer>
#include <QProcess>
#include <QSettings>
#include <QFont>
#include <QFontDatabase>
#include <QTextLayout>
#include <QStyle>
#include <QRect>
#include <QSize>
#include <QUrl>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "app/ConfigManager.hpp"
#include "media/Equalizer.hpp"
#include "playlist/PlaylistManager.hpp"
#include "platform/MprisManager.hpp"
#include "media/MediaManager.hpp"
#include "plugins/PluginManager.hpp"
#include "ui/OsdManager.hpp"
#include "platform/KWinBridge.hpp"
#include "platform/SleepInhibitor.hpp"
#include "ui/ShortcutHandler.hpp"
#include "app/Logger.hpp"
#include "media/ThumbnailProvider.hpp"
#include "cast/CastManager.hpp"
#include "mpvqt/MpvItem.hpp"

#ifndef LUMA_VERSION
#define LUMA_VERSION "dev"
#endif
#ifndef LUMA_GIT_HASH
#define LUMA_GIT_HASH ""
#endif

namespace {
// A command line with a few paths is tiny; cap the single-instance handshake.
constexpr qsizetype kMaxInstanceArgs = 64 * 1024;

// "1.0.0-c52a683": the version from VERSION plus the git revision, so About
// identifies the exact build.
QString versionWithRevision()
{
    QString version = QString::fromLatin1(LUMA_VERSION);
    const QString hash = QString::fromLatin1(LUMA_GIT_HASH);
    if (!hash.isEmpty())
        version += QLatin1Char('-') + hash;
    return version;
}

QString configHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
}

QString kdeIconTheme()
{
    QSettings kde(configHome() + QStringLiteral("/kdeglobals"), QSettings::IniFormat);
    return kde.value(QStringLiteral("Icons/Theme")).toString().trimmed();
}

QString gtkIconTheme()
{
    // Both GTK 3 and 4 use the same key; newer desktops may only write one.
    for (const QString &name : {QStringLiteral("gtk-3.0"), QStringLiteral("gtk-4.0")}) {
        QSettings settings(configHome() + QLatin1Char('/') + name
                               + QStringLiteral("/settings.ini"),
                           QSettings::IniFormat);
        const QString theme = settings.value(QStringLiteral("Settings/gtk-icon-theme-name"))
                                  .toString().trimmed();
        if (!theme.isEmpty())
            return theme;
    }
    return {};
}

QString gnomeIconTheme()
{
    QProcess process;
    process.start(QStringLiteral("gsettings"),
                  {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                   QStringLiteral("icon-theme")});
    if (!process.waitForStarted(200))
        return {};
    if (!process.waitForFinished(400)) {
        process.kill();
        return {};
    }
    QString theme = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (theme.size() >= 2
            && ((theme.startsWith(QLatin1Char('\'')) && theme.endsWith(QLatin1Char('\'')))
                || (theme.startsWith(QLatin1Char('"')) && theme.endsWith(QLatin1Char('"'))))) {
        theme = theme.mid(1, theme.size() - 2);
    }
    return theme;
}

bool iconThemeExists(const QString &theme)
{
    if (theme.isEmpty())
        return false;
    if (theme == QLatin1String("hicolor"))
        return true;
    const QString relative = QStringLiteral("icons/") + theme
                             + QStringLiteral("/index.theme");
    return !QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                      relative, QStandardPaths::LocateFile).isEmpty();
}

QString systemIconTheme()
{
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    const bool kde = desktop.contains(QStringLiteral("kde"))
                     || desktop.contains(QStringLiteral("plasma"));
    const bool gtk = desktop.contains(QStringLiteral("gnome"))
                     || desktop.contains(QStringLiteral("unity"))
                     || desktop.contains(QStringLiteral("pantheon"))
                     || desktop.contains(QStringLiteral("budgie"))
                     || desktop.contains(QStringLiteral("xfce"))
                     || desktop.contains(QStringLiteral("cinnamon"))
                     || desktop.contains(QStringLiteral("mate"));

    QString theme;
    if (kde)
        theme = kdeIconTheme();
    if (theme.isEmpty() && gtk)
        theme = gtkIconTheme();
    if (theme.isEmpty() && (gtk || desktop.isEmpty()))
        theme = gnomeIconTheme();
    if (theme.isEmpty() && !kde)
        theme = kdeIconTheme();
    if (theme.isEmpty() && !gtk && !desktop.isEmpty())
        theme = gtkIconTheme();

    if (!theme.isEmpty()) {
        if (iconThemeExists(theme))
            return theme;
        // Configured theme is not installed (for example a custom theme that
        // was removed); fall back to the desktop's usual theme below.
    }

    const QStringList candidates = kde
        ? QStringList{QStringLiteral("breeze"), QStringLiteral("Adwaita")}
        : QStringList{QStringLiteral("Adwaita"), QStringLiteral("breeze")};
    for (const QString &candidate : candidates) {
        if (iconThemeExists(candidate))
            return candidate;
    }
    return {};
}

// Titles in playlists can mix scripts and symbols that the desktop's primary
// font does not carry. Qt then asks fontconfig for a fallback font the first
// time each such run is shaped, which is slow (hundreds of milliseconds on a
// large font database) and showed up as dropped frames when a playlist with
// Japanese titles was selected. Give the application font an explicit stack of
// installed fonts for those scripts instead, so Qt finds them without the
// expensive search. The first family stays the desktop font, so normal text is
// unchanged.
void applyFontFallbackStack()
{
    QFont font = QGuiApplication::font();
    QStringList families = font.families();
    if (families.isEmpty())
        families << font.family();

    const QStringList installed = QFontDatabase::families();
    auto add = [&](const QString &name) {
        if (!name.isEmpty() && installed.contains(name) && !families.contains(name))
            families << name;
    };
    // CJK, then maths/dingbats, then a broad symbol font and emoji. Only
    // installed families are added, so this works on systems without Noto.
    add(QStringLiteral("Noto Sans CJK JP"));
    add(QStringLiteral("Noto Sans CJK SC"));
    add(QStringLiteral("Noto Sans CJK KR"));
    add(QStringLiteral("Noto Sans Math"));
    add(QStringLiteral("Symbola"));
    add(QStringLiteral("Noto Sans Symbols2"));
    add(QStringLiteral("DejaVu Sans"));
    add(QStringLiteral("Noto Color Emoji"));

    font.setFamilies(families);
    QGuiApplication::setFont(font);
}

// Load the font engines for that stack once, at the size the playlist rows
// use, so the first playlist with such titles does not pay the load while a
// video is playing. This is a fixed cost, not proportional to the playlist:
// the list only lays out the rows that are visible.
void preloadTextEngines()
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(14); // PlaylistDelegate's title size.
    const QString sample = QStringLiteral("日本語かなカナ漢字中文한국어⧸＂☆✕「」？！😀🎌");
    QTextLayout layout(sample, font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid())
        line.setLineWidth(10000.0);
    layout.endLayout();
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
    // Use the desktop's icon theme. The AppImage ships a small hicolor
    // fallback for names the theme does not provide; it must not override the
    // user's theme (the old code forced hicolor in the AppImage).
    // The bundled KDE platform theme should provide the system font, but older
    // Plasma integration builds do not always apply it inside an AppImage.
    // Read the same kdeglobals value the package's platform theme uses.
    if (qEnvironmentVariableIsSet("LUMA_APPIMAGE")) {
        QSettings kde(configHome() + QStringLiteral("/kdeglobals"), QSettings::IniFormat);
        const QString fontSpec = kde.value(QStringLiteral("General/font")).toString().trimmed();
        if (!fontSpec.isEmpty()) {
            QFont font;
            if (font.fromString(fontSpec))
                QApplication::setFont(font);
        }
    }
    QIcon::setFallbackThemeName(QStringLiteral("hicolor"));
    // The KDE/GTK platform theme, when available, is the source of truth for
    // the icon theme. The AppImage now bundles the KDE platform theme, so do
    // not override its choice from kdeglobals here. Only fall back to explicit
    // detection when the platform theme left Qt with hicolor/nothing.
    const QString currentIconTheme = QIcon::themeName();
    if (currentIconTheme.isEmpty()
            || currentIconTheme == QLatin1String("hicolor")) {
        const QString iconTheme = systemIconTheme();
        if (!iconTheme.isEmpty())
            QIcon::setThemeName(iconTheme);
    }
    app.setApplicationName("luma");
    app.setApplicationVersion(versionWithRevision());
    QGuiApplication::setDesktopFileName(QStringLiteral("luma"));

    // Diagnostic: a frozen UI shows up as a gap between these heartbeats.
    auto *stallTimer = new QTimer(&app);
    stallTimer->setInterval(20);
    QObject::connect(stallTimer, &QTimer::timeout, &app, []() {
        static QElapsedTimer last;
        if (!last.isValid()) {
            last.start();
            return;
        }
        const qint64 gap = last.restart();
        if (gap > 70)
            qDebug() << "[perf] GUI stall" << gap << "ms";
    });
    stallTimer->start();
    app.setWindowIcon(QIcon(":/assets/luma.png"));
    applyFontFallbackStack();
    preloadTextEngines();

    ConfigManager config;
    Logger::instance().setMode(config.lumaLogMode());
    Logger::instance().setComponentMode(QStringLiteral("mpv"), config.mpvLogMode());
    Logger::instance().setComponentMode(QStringLiteral("yt-dlp"), config.ytdlpLogMode());
    Logger::installMessageHandler();

    // ---- Single instance ----
    QString serverName = "luma_single_instance_server";
    QLocalSocket socket;
    QLocalServer *server = nullptr;

    if (config.singleInstance()) {
        socket.connectToServer(serverName);
        if (socket.waitForConnected(500)) {
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

    // ---- Equalizer ----
    Equalizer equalizer(&config);

    // ---- Translations ----
    QTranslator *translator = new QTranslator(&app);

    auto loadLanguage = [&](const QString &requested) {
        app.removeTranslator(translator);
        const QString language = ConfigManager::resolveLanguage(requested, QLocale::system().name());
        // Translations are embedded in the binary (qt_add_translations), so the
        // resource is tried first: it always matches this build.
        const QString resourcePath = QStringLiteral(":/i18n/") + language + ".qm";
        const QString sysPath = "/usr/share/luma/translations/" + language + ".qm";
        const QString localPath = QCoreApplication::applicationDirPath() + "/" + language + ".qm";

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

    PluginManager plugins(&config, &media, &playlists, &osd, &app);

    CastManager castManager;
    castManager.setEqualizer(&equalizer);
    // Casting a yt-dlp stream must use the direct video/audio URLs, not the
    // page URL stored in the playlist. The resolver reads what MediaManager
    // already resolved for the playing media.
    castManager.setSourceResolver([&media](const QString &filePath) {
        return media.castSourceFor(filePath);
    });
    // The cast encoder uses the configured proxy for remote sources. The
    // player's own playback path is not touched (libmpv handles SOCKS itself).
    castManager.setProxyUrl(config.ytdlpProxy());
    QObject::connect(&config, &ConfigManager::ytdlpSettingsChanged, &castManager,
                     [&config, &castManager]() {
        castManager.setProxyUrl(config.ytdlpProxy());
    });
    QObject::connect(&media, &MediaManager::fileChanged,
                     &castManager, &CastManager::onMediaFileChanged);
    QObject::connect(&castManager, &CastManager::castingChanged, &osd,
                     [&osd, &castManager]() {
        osd.setCastMode(castManager.casting());
        osd.setCastPosition(castManager.remotePosition(), castManager.remoteDuration(),
                            castManager.isRemotePlaying());
    });
    QObject::connect(&castManager, &CastManager::remoteStatusChanged, &osd,
                     [&osd, &castManager]() {
        osd.setCastPosition(castManager.remotePosition(), castManager.remoteDuration(),
                            castManager.isRemotePlaying());
    });

    // Keeps the picture-in-picture window above the others on KDE Plasma and
    // restores/persists its geometry through KWin scripting.
    KWinBridge kwinBridge;

    // The picture-in-picture window draws its own shadow inside the window.
    // The transparent margin around it keeps the composition free of the 1px
    // edge artifact that KWin/NVIDIA shows around a bare window.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QQmlApplicationEngine engine;

    engine.addImageProvider("thumbnail", new ThumbnailProvider());

    engine.rootContext()->setContextProperty("Config", &config);
    engine.rootContext()->setContextProperty("Equalizer", &equalizer);
    engine.rootContext()->setContextProperty("Media", &media);
    engine.rootContext()->setContextProperty("Playlists", &playlists);
    engine.rootContext()->setContextProperty("Osd", &osd);
    engine.rootContext()->setContextProperty("CastManager", &castManager);
    engine.rootContext()->setContextProperty("Plugins", &plugins);
    engine.rootContext()->setContextProperty("KWinBridge", &kwinBridge);

    auto *shortcuts = new ShortcutHandler(nullptr, nullptr, nullptr, nullptr, &app,
                                          config.singleInstance());
    shortcuts->setPluginManager(&plugins);
    engine.rootContext()->setContextProperty("shortcuts", shortcuts);

    QObject::connect(shortcuts, &ShortcutHandler::toggleEqualizerRequested, &equalizer,
                     [&equalizer, &osd]() {
        equalizer.setEnabled(!equalizer.enabled());
        osd.showInfo(equalizer.enabled()
            ? QCoreApplication::translate("ShortcutHandler", "osd_equalizer_on")
            : QCoreApplication::translate("ShortcutHandler", "osd_equalizer_off"));
    });

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

        // ---- Sleep inhibition ----
        // Keep the session awake while something is playing. Pausing or
        // stopping releases the portal inhibition; the setting lets users opt
        // out (for example while listening to music with the screen off).
        auto *sleepInhibitor = new SleepInhibitor(&app);
        auto updateSleepInhibitor = [&config, &media, mpv, sleepInhibitor]() {
            sleepInhibitor->setActive(config.inhibitSleep() && media.hasMedia()
                                      && !mpv->paused());
        };
        QObject::connect(mpv, &MpvItem::pausedChanged,
                         sleepInhibitor, updateSleepInhibitor);
        QObject::connect(mpv, &MpvItem::sourceChanged,
                         sleepInhibitor, updateSleepInhibitor);
        QObject::connect(&media, &MediaManager::hasMediaChanged,
                         sleepInhibitor, updateSleepInhibitor);
        QObject::connect(&config, &ConfigManager::inhibitSleepChanged,
                         sleepInhibitor, updateSleepInhibitor);
        updateSleepInhibitor();

        // Probe system and private yt-dlp installations once at startup, so the
        // first URL already prefers system packages and the UI has versions.
        media.refreshYtdlpRuntime();
        // The equalizer and the music crossfade both live in mpv's single "af"
        // property, so build the combined chain in one place.
        auto applyAudioFilters = [mpv, &equalizer, &media]() {
            QStringList filters;
            const QString equalizerFilter = equalizer.audioFilter();
            if (!equalizerFilter.isEmpty())
                filters << equalizerFilter;
            const QString crossfade = media.crossfadeFilter();
            if (!crossfade.isEmpty())
                filters << crossfade;
            mpv->setProperty(QStringLiteral("af"), filters.join(QLatin1Char(',')));
        };
        QObject::connect(&equalizer, &Equalizer::filterRequested, mpv,
                         [applyAudioFilters](const QString &) { applyAudioFilters(); });
        QObject::connect(&media, &MediaManager::crossfadeFilterChanged,
                         mpv, applyAudioFilters);
        equalizer.applyNow();

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
        // yt-dlp gets the proxy on its own. mpv 0.42's libcurl backend accepts
        // the same URL directly, including SOCKS5/SOCKS5h, so Range requests and
        // seeking work without a local proxy bridge.
        auto applyStreamProxy = [&config, mpv]() {
            mpv->setProperty("http-proxy", config.ytdlpProxy());
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
            QObject::connect(shortcuts, &ShortcutHandler::toggleStats, root, [root, &media]() {
                if (root->property("pipMode").toBool() || !media.hasMedia()) return;
                auto *panel = root->findChild<QObject*>("statsPanel");
                if (panel) panel->setProperty("isOpen", !panel->property("isOpen").toBool());
            });
            QObject::connect(shortcuts, &ShortcutHandler::statsSelected, root, [root]() {
                if (root->property("pipMode").toBool()) return;
                auto *panel = root->findChild<QObject*>("statsPanel");
                if (panel && panel->property("isOpen").toBool()) {
                    panel->setProperty("activePage", "luma");
                    QMetaObject::invokeMethod(panel, "selectCoreStats");
                }
            });
                QObject::connect(shortcuts, &ShortcutHandler::togglePip, root, [root, &media]() {
                    // The menu entry is hidden when nothing is loaded; keep the
                    // shortcut consistent, but still allow leaving PiP.
                    if (!root->property("pipMode").toBool() && !media.hasMedia()) return;
                    QMetaObject::invokeMethod(root, "togglePip");
                });
                QObject::connect(shortcuts, &ShortcutHandler::fitWindowToVideo, root, [root]() {
                    QMetaObject::invokeMethod(root, "fitWindowToVideo");
                });
                QObject::connect(shortcuts, &ShortcutHandler::shortcutsHelpVisibleChanged, root,
                                 [root](bool visible) { root->setProperty("shortcutsHelpVisible", visible); });

                // With multiple instances every window needs its own MPRIS
                // name, otherwise only the first one is visible to playerctl
                // and the desktop's media applet.
                QString mprisService = QStringLiteral("org.mpris.MediaPlayer2.luma");
                QString mprisIdentity = QStringLiteral("Luma");
                if (!config.singleInstance()) {
                    const QString pid = QString::number(QCoreApplication::applicationPid());
                    mprisService += QStringLiteral(".i") + pid;
                    mprisIdentity = QStringLiteral("Luma (%1)").arg(pid);
                }
                new MprisManager(mpv, &media, mprisService, mprisIdentity, &app);
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

        // Stop playback before mpv/QML teardown, so network threads do not
        // leave the process hanging with audio still playing.
        startExitWatchdog();
        if (mpv) {
            mpv->commandBlocking(QStringList() << QStringLiteral("stop"));
            mpv->setPaused(true);
        }
        castManager.stopCast();
    });

    const int exitCode = app.exec();
    return exitCode;
}
