cmake_minimum_required(VERSION 3.16)
project(luma VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Widgets DBus Network
    OpenGL OpenGLWidgets LinguistTools
)
find_package(PkgConfig REQUIRED)

pkg_check_modules(MPV REQUIRED mpv)

# FFmpeg для превью и Cast
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(AVUTIL REQUIRED libavutil)

find_package(Qt6 QUIET COMPONENTS WaylandClient)

qt_standard_project_setup()

qt_add_executable(luma
    main.cpp
    core/ConfigManager.cpp
    core/MprisManager.cpp
    core/Logger.cpp
    core/PlaylistModel.cpp
    core/MediaManager.cpp
    core/OsdManager.cpp
    core/ShortcutAction.cpp
    core/ShortcutHandler.cpp
    core/ThumbnailProvider.hpp
    # --- Cast ---
    core/CastDiscovery.cpp
    core/CastStreamServer.cpp
    core/CastV2Client.cpp
    core/CastManager.cpp
    # --- mpvqt ---
    mpvqt/mpvabstractitem.cpp
    mpvqt/mpvcontroller.cpp
    mpvqt/mpvrenderer.cpp
    mpvqt/MpvItem.cpp
    mpvqt/FrameDecoder.cpp
)

qt_add_qml_module(luma
    URI luma
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/ControlBar.qml
        qml/PlaylistPanel.qml
        qml/OsdToast.qml
        qml/ContextMenu.qml
        qml/StatsPanel.qml
        qml/AboutDialog.qml
        qml/SettingsDialog.qml
        qml/MediaInfoDialog.qml
    SOURCES
        mpvqt/MpvItem.hpp
        mpvqt/FrameDecoder.hpp
        core/ConfigManager.hpp
        core/MprisManager.hpp
        core/ShortcutAction.hpp
        core/ShortcutHandler.hpp
        core/PlaylistModel.hpp
        core/MediaManager.hpp
        core/OsdManager.hpp
        core/Logger.hpp
        core/ThumbnailProvider.hpp
        # --- Cast ---
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

target_include_directories(luma PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/mpvqt
    ${MPV_INCLUDE_DIRS}
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
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${MPV_LIBRARIES}
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${SWSCALE_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)

if (TARGET Qt6::WaylandClient)
    target_link_libraries(luma PRIVATE Qt6::WaylandClient)
endif()

install(TARGETS luma
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(IMPORTED_RUNTIME_ARTIFACTS luma DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/luma)
install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/luma/ DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/luma)
