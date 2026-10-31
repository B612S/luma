#!/bin/bash
set -e

# ============================================================================
# build-appimage.sh — сборка AppImage для Luma (Arch Linux, Qt 6.10+)
# Основано на практике EdgeTX, Serial-Studio, AetherSDR и excludelist AppImage
# ============================================================================

# 1. Очистка и подготовка
rm -rf build AppDir
mkdir -p AppDir

# 2. Сборка
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -G Ninja
cmake --build build

# 3. Установка в AppDir
DESTDIR="$PWD/AppDir" cmake --install build
install -Dm644 "luma.desktop" "AppDir/usr/share/applications/luma.desktop"
install -Dm644 "assets/luma.png" "AppDir/usr/share/icons/hicolor/256x256/apps/luma.png"
rm -f "AppDir/usr/lib/qt6/qml/luma/luma_qml_module_dir_map.qrc"

# 4. Инструменты
if [ ! -f linuxdeploy-x86_64.AppImage ]; then
    wget -c "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
    chmod +x linuxdeploy-x86_64.AppImage
fi
if [ ! -f linuxdeploy-plugin-qt-x86_64.AppImage ]; then
    wget -c "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x linuxdeploy-plugin-qt-x86_64.AppImage
fi
if [ ! -f appimagetool-x86_64.AppImage ]; then
    wget -c "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x appimagetool-x86_64.AppImage
fi

# 5. Переменные окружения (ключевые настройки из практики других проектов)
export QMAKE=/usr/bin/qmake6
export QML_SOURCES_PATHS="$PWD/qml"

# linuxdeploy-plugin-qt по умолчанию разворачивает только libqxcb.so.
# Явно указываем Wayland-плагин. В Qt 6.10+ это единый libqwayland.so.
# (Serial-Studio использует именно такую формулировку для Qt 6.10+) [citation:13]
export EXTRA_PLATFORM_PLUGINS="libqwayland.so;libqxcb.so"

# "waylandcompositor" — это имя модуля, которое заставляет плагин развернуть
# подкаталоги wayland-shell-integration, wayland-decoration-client и т.д.
# Они загружаются через dlopen(), и их не видно при анализе зависимостей. [citation:4]
export EXTRA_QT_MODULES="waylandcompositor"

# Исключаем проблемный JXR-плагин (частая проблема на Arch)
export LINUXDEPLOY_EXCLUDE_LIBS="libjpegxr.so*:kimg_jxr.so*"

# Отключаем strip (встроенный strip не понимает .relr.dyn на свежем Arch)
export NO_STRIP=1

# Имя выходного файла с маленькой буквы
export OUTPUT="luma-x86_64.AppImage"

# 6. Запуск linuxdeploy (разворачивает Qt-библиотеки и плагины)
./linuxdeploy-x86_64.AppImage \
    --appdir AppDir \
    --plugin qt \
    --output appimage || true

# 7. ВАЖНО: linuxdeploy-plugin-qt разворачивает сам libqwayland.so,
# но НЕ копирует подкаталоги wayland-* (shell, decoration, graphics integration),
# потому что они загружаются динамически (dlopen). Копируем их вручную.
# Именно так делают EdgeTX и Serial-Studio. [citation:13]
PLUGIN_SRC="/usr/lib/qt6/plugins"
PLUGIN_DST="AppDir/usr/plugins"

for dir in wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    if [ -d "$PLUGIN_SRC/$dir" ]; then
        mkdir -p "$PLUGIN_DST/$dir"
        cp -a "$PLUGIN_SRC/$dir/"* "$PLUGIN_DST/$dir/"
    fi
done

# Проверка, что плагины платформы на месте
if [ ! -f "$PLUGIN_DST/platforms/libqwayland.so" ] && [ ! -f "$PLUGIN_DST/platforms/libqxcb.so" ]; then
    echo "ERROR: Плагины платформы не найдены в AppDir"
    ls -la "$PLUGIN_DST/platforms/" 2>/dev/null || true
    exit 1
fi

# 8. AppRun (минималистичный, как в успешных проектах)
# Не раздуваем LD_LIBRARY_PATH, не добавляем лишних путей.
cat > AppDir/AppRun << 'EOF'
#!/bin/bash
export QT_QPA_PLATFORM_PLUGIN_PATH="${APPDIR}/usr/plugins/platforms"
export LD_LIBRARY_PATH="${APPDIR}/usr/lib"
exec "${APPDIR}/usr/bin/luma" "$@"
EOF
chmod +x AppDir/AppRun

# 9. Финальная упаковка
ARCH=x86_64 ./appimagetool-x86_64.AppImage AppDir "$OUTPUT"

echo "OK: $OUTPUT создан"
