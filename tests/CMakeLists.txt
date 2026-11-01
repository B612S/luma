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
    ${CMAKE_SOURCE_DIR}/core/PlaylistModel.cpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistModel.hpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistManager.cpp
    ${CMAKE_SOURCE_DIR}/core/PlaylistManager.hpp
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
