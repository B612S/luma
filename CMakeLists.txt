cmake_minimum_required(VERSION 3.20)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LUMA_VERSION LIMIT_COUNT 1)
string(STRIP "${LUMA_VERSION}" LUMA_VERSION)
if(NOT LUMA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH (got '${LUMA_VERSION}')")
endif()
project(luma VERSION ${LUMA_VERSION} LANGUAGES CXX)

# Revision shown in the About dialog as "<version>-<git hash>". The hash is
# optional (source tarballs have no repository); packages that copy the tree
# without .git pass it explicitly with -DLUMA_GIT_HASH.
find_package(Git QUIET)
set(LUMA_GIT_HASH "" CACHE STRING
    "Short git revision shown in About; detected automatically when empty")
if(NOT LUMA_GIT_HASH AND GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=7 HEAD
        OUTPUT_VARIABLE LUMA_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

include(GNUInstallDirs)
if(NOT CMAKE_INSTALL_DATADIR)
    set(CMAKE_INSTALL_DATADIR "share")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Luma is Wayland-only: the Qt Wayland client integration is required, not optional.
find_package(Qt6 6.9 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Widgets DBus Network Concurrent
    OpenGL OpenGLWidgets LinguistTools WaylandClient
)
find_package(PkgConfig REQUIRED)

# libmpv 2.6 (mpv 0.42) is required: the libcurl network backend from that
# release accepts SOCKS5/SOCKS5h proxies directly, without a local HTTP bridge.
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv>=2.6)

# TagLib — reading and writing audio file tags.
pkg_check_modules(TAGLIB REQUIRED taglib)

# Keyboard layout is read directly from Wayland.
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(XKB REQUIRED xkbcommon)

# FFmpeg for thumbnails and Cast
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(AVUTIL REQUIRED libavutil)

qt_standard_project_setup(REQUIRES 6.9)

qt_add_executable(luma
    main.cpp
    core/app/ConfigManager.cpp
    core/app/Logger.cpp
    core/media/Equalizer.cpp
    core/media/Equalizer.hpp
    core/media/MediaFileActions.cpp
    core/media/MediaInfoService.cpp
    core/media/MediaManager.cpp
    core/media/MediaNotifier.cpp
    core/media/PlaybackPositions.cpp
    core/media/PlaybackPositions.hpp
    core/media/RadioNameResolver.cpp
    core/media/RadioNameResolver.hpp
    core/media/ThumbnailProvider.hpp
    core/media/TrailingSilenceDetector.cpp
    core/media/YtDlpRuntime.cpp
    core/playlist/PlaylistFilterModel.cpp
    core/playlist/PlaylistManager.cpp
    core/playlist/PlaylistModel.cpp
    core/playlist/PlaylistSaver.cpp
    core/playlist/PlaylistSaver.hpp
    core/cast/CastDiscovery.cpp
    core/cast/CastDeviceLock.cpp
    core/cast/CastManager.cpp
    core/cast/CastMemoryHlsServer.cpp
    core/cast/CastMemoryHlsServer.hpp
    core/cast/CastV2Client.cpp
    core/cast/SocksHttpProxy.cpp
    core/platform/GlobalShortcuts.cpp
    core/platform/KWinBridge.cpp
    core/platform/MprisManager.cpp
    core/platform/SleepInhibitor.cpp
    core/plugins/PluginApi.cpp
    core/plugins/PluginLocalization.cpp
    core/plugins/PluginManager.cpp
    core/plugins/PluginManifest.cpp
    core/plugins/PluginRuntime.cpp
    core/plugins/PluginWindow.cpp
    core/ui/OsdManager.cpp
    core/ui/ShortcutAction.cpp
    core/ui/ShortcutHandler.cpp
    core/tags/TagManager.cpp
    core/tags/TagScanner.cpp
    core/util/AsyncFileWriter.cpp
    core/util/AsyncFileWriter.hpp
    mpvqt/FrameDecoder.cpp
    mpvqt/MpvItem.cpp
    mpvqt/mpvabstractitem.cpp
    mpvqt/mpvcontroller.cpp
    mpvqt/mpvrenderer.cpp
)

qt_add_qml_module(luma
    URI luma
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    # Kirigami and qqc2-desktop-style are runtime-only QML imports, not build
    # dependencies. Without this, qmlimportscanner tries to link their static
    # plugin targets (KF6Kirigamiplugin, ...), which do not exist here, and
    # emits a warning for every Kirigami submodule.
    NO_IMPORT_SCAN
    QML_FILES
        qml/Main.qml
        qml/ControlBar.qml
        qml/PlaylistPanel.qml
        qml/PlaylistPropertiesDialog.qml
        qml/PlaylistTabs.qml
        qml/PluginMenu.qml
        qml/PluginSettingsDialog.qml
        qml/PluginWindowDialog.qml
        qml/PluginWindowField.qml
        qml/PlaylistTabsContextMenu.qml
        qml/PlaylistDelegate.qml
        qml/PlaylistContextMenu.qml
        qml/PlaylistDialogs.qml
        qml/OsdToast.qml
        qml/PipControls.qml
        qml/ShortcutsHelp.qml
        qml/ShortcutField.qml
        qml/AddUrlDialog.qml
        qml/ContextMenu.qml
        qml/LumaMenu.qml
        qml/LumaMenuButton.qml
        qml/LumaMenuSeparator.qml
        qml/StatsPanel.qml
        qml/SettingsDialog.qml
        qml/EqualizerDialog.qml
        qml/MediaInfoDialog.qml
        qml/FileUnavailableDialog.qml
        qml/FocusScroller.qml
    SOURCES
        core/app/ConfigManager.hpp
        core/app/Logger.hpp
        core/media/Equalizer.hpp
        core/media/MediaManager.hpp
        core/media/ThumbnailProvider.hpp
        core/playlist/PlaylistFilterModel.hpp
        core/playlist/PlaylistManager.hpp
        core/playlist/PlaylistModel.hpp
        core/cast/CastDiscovery.hpp
        core/cast/CastManager.hpp
        core/cast/CastV2Client.hpp
        core/platform/GlobalShortcuts.hpp
        core/platform/KWinBridge.hpp
        core/platform/MprisManager.hpp
        core/platform/SleepInhibitor.hpp
        core/ui/OsdManager.hpp
        core/ui/ShortcutAction.hpp
        core/ui/ShortcutHandler.hpp
        core/tags/TagManager.hpp
        mpvqt/FrameDecoder.hpp
        mpvqt/MpvItem.hpp
)

qt_add_resources(luma "assets"
    PREFIX "/"
    FILES
        assets/luma.png
)

qt_add_translations(luma
    TS_FILES
        locales/en.ts
        locales/ru.ts
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/en.qm
    ${CMAKE_CURRENT_BINARY_DIR}/ru.qm
    DESTINATION ${CMAKE_INSTALL_DATADIR}/luma/translations
)

install(FILES luma.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/luma.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
install(FILES assets/luma.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
# Optional bundled JavaScript plugins. Their data stays in the user's data
# directory; only the plugin code is installed here.
install(DIRECTORY plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luma/plugins)

target_include_directories(luma PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    # The generated QML type registration includes QML_ELEMENT headers by
    # basename, so the directory with PlaylistFilterModel.hpp must be visible.
    ${CMAKE_CURRENT_SOURCE_DIR}/core/playlist
    ${CMAKE_CURRENT_SOURCE_DIR}/mpvqt
    ${MPV_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${XKB_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

target_link_libraries(luma PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
    Qt6::DBus
    Qt6::Network
    Qt6::Concurrent
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    PkgConfig::MPV
    ${TAGLIB_LIBRARIES}
    ${WAYLAND_CLIENT_LIBRARIES}
    ${XKB_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

target_compile_definitions(luma PRIVATE
    LUMA_VERSION="${PROJECT_VERSION}"
    LUMA_GIT_HASH="${LUMA_GIT_HASH}"
)

# Required: Luma has no X11 fallback.
target_link_libraries(luma PRIVATE Qt6::WaylandClient)

install(TARGETS luma
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(IMPORTED_RUNTIME_ARTIFACTS luma DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/luma)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/luma/ DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/luma)

# ---- Tests (off by default, never ship in a package) ----
option(LUMA_BUILD_TESTS "Build autotests (Qt Test)" OFF)
if (LUMA_BUILD_TESTS)
    enable_testing()

add_subdirectory(tests)
endif()

# ---- Manual cast streaming test (not part of ctest) ----
option(LUMA_BUILD_CAST_TEST "Build the manual Chromecast/HLS streaming test" OFF)
if (LUMA_BUILD_CAST_TEST)
    add_executable(cast_stream_test
        tests/cast_stream_test.cpp
        core/cast/CastMemoryHlsServer.cpp
        core/cast/CastMemoryHlsServer.hpp
        core/cast/SocksHttpProxy.cpp
    )
    target_include_directories(cast_stream_test PRIVATE ${CMAKE_SOURCE_DIR}/core)
    target_link_libraries(cast_stream_test PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)

    # Full CastManager/TV smoke test: actually connects to a Chromecast and
    # performs native HLS seeking. Manual only.
    add_executable(cast_tv_smoke
        tests/cast_tv_smoke.cpp
        core/cast/CastDeviceLock.cpp
        core/cast/CastManager.cpp
        core/cast/CastDiscovery.cpp
        core/cast/CastV2Client.cpp
        core/cast/CastMemoryHlsServer.cpp
        core/cast/SocksHttpProxy.cpp
        core/media/Equalizer.cpp
        core/app/ConfigManager.cpp)
    target_include_directories(cast_tv_smoke PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/mpvqt)
    target_link_libraries(cast_tv_smoke PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)
    set_target_properties(cast_tv_smoke PROPERTIES AUTOMOC ON)
endif()
