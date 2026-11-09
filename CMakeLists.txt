cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" LUMA_VERSION LIMIT_COUNT 1)
string(STRIP "${LUMA_VERSION}" LUMA_VERSION)
if(NOT LUMA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must be MAJOR.MINOR.PATCH (got '${LUMA_VERSION}')")
endif()
project(luma VERSION ${LUMA_VERSION} LANGUAGES CXX)

# Build stamp shown in the About dialog: "<version>-<git hash> (<build time>)".
# The hash is optional (source tarballs have no repository) and marked when the
# tree had uncommitted tracked changes at configure time. The timestamp is the
# configure time; packages always build from a fresh build directory.
find_package(Git QUIET)
set(LUMA_GIT_HASH "")
if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=7 HEAD
        OUTPUT_VARIABLE LUMA_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}"
                status --porcelain --untracked-files=no
        OUTPUT_VARIABLE LUMA_GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(LUMA_GIT_HASH AND NOT LUMA_GIT_DIRTY STREQUAL "")
        string(APPEND LUMA_GIT_HASH "-dirty")
    endif()
endif()
string(TIMESTAMP LUMA_BUILD_TIME "%Y-%m-%d %H:%M")

include(GNUInstallDirs)
if(NOT CMAKE_INSTALL_DATADIR)
    set(CMAKE_INSTALL_DATADIR "share")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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

pkg_check_modules(MPV REQUIRED mpv)

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
    core/ConfigManager.cpp
    core/GlobalShortcuts.cpp
    core/MprisManager.cpp
    core/Logger.cpp
    core/PlaylistModel.cpp
    core/PlaylistFilterModel.cpp
    core/PlaylistManager.cpp
    core/AsyncFileWriter.cpp
    core/AsyncFileWriter.hpp
    core/PlaybackPositions.cpp
    core/PlaybackPositions.hpp
    core/Equalizer.cpp
    core/Equalizer.hpp
    core/PlaylistSaver.cpp
    core/PlaylistSaver.hpp
    core/MediaManager.cpp
    core/YtDlpRuntime.cpp
    core/MediaInfoService.cpp
    core/MediaNotifier.cpp
    core/TrailingSilenceDetector.cpp
    core/MediaFileActions.cpp
    core/PluginManifest.cpp
    core/PluginLocalization.cpp
    core/PluginApi.cpp
    core/PluginRuntime.cpp
    core/PluginManager.cpp
    core/PluginWindow.cpp
    core/IcyMetadataReader.cpp
    core/IcyMetadataReader.hpp
    core/RadioNameResolver.cpp
    core/RadioNameResolver.hpp
    core/OsdManager.cpp
    core/KWinBridge.cpp
    core/TagManager.cpp
    core/TagScanner.cpp
    core/ShortcutAction.cpp
    core/ShortcutHandler.cpp
    core/ThumbnailProvider.hpp
    core/CastDiscovery.cpp
    core/CastStreamServer.cpp
    core/CastV2Client.cpp
    core/CastManager.cpp
    core/SocksHttpProxy.cpp
    core/SocksHttpProxy.hpp
    mpvqt/mpvabstractitem.cpp
    mpvqt/mpvcontroller.cpp
    mpvqt/mpvrenderer.cpp
    mpvqt/MpvItem.cpp
    mpvqt/FrameDecoder.cpp
)

qt_add_qml_module(luma
    URI luma
    VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
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
        mpvqt/MpvItem.hpp
        mpvqt/FrameDecoder.hpp
        core/ConfigManager.hpp
        core/GlobalShortcuts.hpp
        core/MprisManager.hpp
        core/ShortcutAction.hpp
        core/ShortcutHandler.hpp
        core/PlaylistModel.hpp
        core/PlaylistFilterModel.hpp
        core/PlaylistManager.hpp
        core/Equalizer.hpp
        core/MediaManager.hpp
        core/OsdManager.hpp
        core/KWinBridge.hpp
        core/TagManager.hpp
        core/Logger.hpp
        core/ThumbnailProvider.hpp
        core/CastDiscovery.hpp
        core/CastStreamServer.hpp
        core/CastV2Client.hpp
        core/CastManager.hpp
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
# Optional bundled JavaScript plugins. Their data stays in the user's data
# directory; only the plugin code is installed here.
install(DIRECTORY plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/luma/plugins)

target_include_directories(luma PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
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
    ${MPV_LIBRARIES}
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
    LUMA_BUILD_TIME="${LUMA_BUILD_TIME}"
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
