# Tests (Qt Test). Run: ctest --test-dir build --output-on-failure
find_package(Qt6 QUIET COMPONENTS Test)

if (NOT Qt6Test_FOUND)
    message(STATUS "Qt6::Test not found — autotests disabled")
    return()
endif()

# The tested part of the application: no mpv, GUI or QML.
add_library(luma_testcore STATIC
    ${CMAKE_SOURCE_DIR}/core/ConfigManager.cpp
    ${CMAKE_SOURCE_DIR}/core/ConfigManager.hpp
    ${CMAKE_SOURCE_DIR}/core/Logger.cpp
    ${CMAKE_SOURCE_DIR}/core/Logger.hpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistModel.cpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistModel.hpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistFilterModel.hpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistManager.cpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistManager.hpp
    ${CMAKE_SOURCE_DIR}/core/AsyncFileWriter.cpp
    ${CMAKE_SOURCE_DIR}/core/AsyncFileWriter.hpp
    ${CMAKE_SOURCE_DIR}/core/PlaybackPositions.cpp
    ${CMAKE_SOURCE_DIR}/core/PlaybackPositions.hpp
    ${CMAKE_SOURCE_DIR}/core/Equalizer.cpp
    ${CMAKE_SOURCE_DIR}/core/Equalizer.hpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistSaver.cpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistSaver.hpp
    ${CMAKE_SOURCE_DIR}/core/PluginManifest.cpp
    ${CMAKE_SOURCE_DIR}/core/PluginManifest.hpp
    ${CMAKE_SOURCE_DIR}/core/RadioNameResolver.cpp
    ${CMAKE_SOURCE_DIR}/core/RadioNameResolver.hpp
    ${CMAKE_SOURCE_DIR}/core/TagScanner.cpp
    ${CMAKE_SOURCE_DIR}/core/TagScanner.hpp
    ${CMAKE_SOURCE_DIR}/core/ShortcutAction.cpp
    ${CMAKE_SOURCE_DIR}/core/ShortcutAction.hpp
)

target_include_directories(luma_testcore PUBLIC
    ${CMAKE_SOURCE_DIR}/core
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
)

target_link_libraries(luma_testcore PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
    ${SWSCALE_LIBRARIES}
)

set(LUMA_TESTS
    tst_shortcuts
    tst_playlistmodel
    tst_configmanager
    tst_playlistmanager
    tst_pluginmanifest
    tst_playbackpositions
    tst_equalizer
    tst_logger
)

foreach(test_name IN LISTS LUMA_TESTS)
    add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE luma_testcore Qt6::Test)

    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 60
    )
endforeach()

# Wayland-only policy: the binary must refuse to start without a Wayland
# session instead of falling back to X11. It exits before QApplication is
# created, so no display or mpv is needed.
if(TARGET luma)
    add_test(NAME wayland_only
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/check_wayland_only.sh $<TARGET_FILE:luma>)
    set_tests_properties(wayland_only PROPERTIES
        ENVIRONMENT "WAYLAND_DISPLAY=;WAYLAND_SOCKET=;DISPLAY=:99;QT_QPA_PLATFORM=offscreen"
        TIMEOUT 15
    )
endif()

# Integration tests with the real libmpv: headless, no display. One case uses
# the real YouTube URL; disable it with LUMA_TEST_NO_NETWORK for offline runs.
add_executable(tst_media_open
    tst_media_open.cpp
    ${CMAKE_SOURCE_DIR}/core/MediaManager.cpp
    ${CMAKE_SOURCE_DIR}/core/YtDlpRuntime.cpp
    ${CMAKE_SOURCE_DIR}/core/MediaInfoService.cpp
    ${CMAKE_SOURCE_DIR}/core/MediaNotifier.cpp
    ${CMAKE_SOURCE_DIR}/core/TrailingSilenceDetector.cpp
    ${CMAKE_SOURCE_DIR}/core/MediaFileActions.cpp
    ${CMAKE_SOURCE_DIR}/core/IcyMetadataReader.cpp
    ${CMAKE_SOURCE_DIR}/core/TagManager.cpp
    ${CMAKE_SOURCE_DIR}/mpvqt/MpvItem.cpp
    ${CMAKE_SOURCE_DIR}/mpvqt/mpvabstractitem.cpp
    ${CMAKE_SOURCE_DIR}/mpvqt/mpvcontroller.cpp
    ${CMAKE_SOURCE_DIR}/mpvqt/mpvrenderer.cpp
    ${CMAKE_SOURCE_DIR}/mpvqt/FrameDecoder.cpp
)
target_include_directories(tst_media_open PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/mpvqt
    ${MPV_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
    ${SWSCALE_INCLUDE_DIRS}
)
target_link_libraries(tst_media_open PRIVATE
    luma_testcore
    Qt6::Test
    Qt6::Quick
    Qt6::DBus
    Qt6::Network
    Qt6::Concurrent
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${MPV_LIBRARIES}
    ${TAGLIB_LIBRARIES}
)
target_compile_definitions(tst_media_open PRIVATE
    LUMA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    LUMA_TEST_MPV_VO
)
add_test(NAME tst_media_open COMMAND tst_media_open)
set_tests_properties(tst_media_open PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/media-test-config;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/media-test-cache"
    TIMEOUT 180
    LABELS "integration;libmpv"
)
