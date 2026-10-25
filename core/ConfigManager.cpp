#include "ConfigManager.hpp"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/luma";
    QString path = configDir + "/luma.conf";

    QDir().mkpath(configDir);

    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);
            out << "# Luma Configuration\n\n";
            out << "[ui]\n";
            out << "remember_volume=true\n";
            out << "default_volume=80\n";
            out << "volume=80\n";
            out << "language=en\n";
            out << "single_instance=true\n";
            out << "notifications=true\n";
            out << "append_to_playlist=false\n\n";
            out << "[playlist]\n";
            out << "mode=sequential\n\n";
            out << "[window]\n";
            out << "width=960\n";
            out << "height=640\n";
            f.close();
        }
    }

    m_settings = new QSettings(path, QSettings::IniFormat, this);
}

bool ConfigManager::rememberVolume() const {
    return m_settings->value("ui/remember_volume", true).toBool();
}
void ConfigManager::setRememberVolume(bool v) {
    if (rememberVolume() == v) return;
    m_settings->setValue("ui/remember_volume", v);
    emit rememberVolumeChanged();
}

int ConfigManager::defaultVolume() const {
    return m_settings->value("ui/default_volume", 80).toInt();
}
void ConfigManager::setDefaultVolume(int v) {
    if (defaultVolume() == v) return;
    m_settings->setValue("ui/default_volume", v);
    emit defaultVolumeChanged();
}

int ConfigManager::volume() const {
    return m_settings->value("ui/volume", 80).toInt();
}
void ConfigManager::setVolume(int v) {
    if (volume() == v) return;
    m_settings->setValue("ui/volume", v);
    emit volumeChanged();
}

QString ConfigManager::language() const { return m_settings->value("ui/language", "en").toString(); }
void ConfigManager::setLanguage(const QString &lang) {
    if (language() == lang) return;
    m_settings->setValue("ui/language", lang);
    emit languageChanged();
}

QString ConfigManager::playlistMode() const {
    return m_settings->value("playlist/mode", "sequential").toString();
}
void ConfigManager::setPlaylistMode(const QString &mode) {
    if (playlistMode() == mode) return;
    m_settings->setValue("playlist/mode", mode);
    emit playlistModeChanged();
}

bool ConfigManager::appendToPlaylist() const {
    return m_settings->value("ui/append_to_playlist", false).toBool();
}
void ConfigManager::setAppendToPlaylist(bool v) {
    if (appendToPlaylist() == v) return;
    m_settings->setValue("ui/append_to_playlist", v);
    emit appendToPlaylistChanged();
}

bool ConfigManager::singleInstance() const {
    return m_settings->value("ui/single_instance", true).toBool();
}
void ConfigManager::setSingleInstance(bool enabled) {
    if (singleInstance() == enabled) return;
    m_settings->setValue("ui/single_instance", enabled);
    emit singleInstanceChanged();
}

bool ConfigManager::notifications() const {
    return m_settings->value("ui/notifications", true).toBool();
}
void ConfigManager::setNotifications(bool enabled) {
    if (notifications() == enabled) return;
    m_settings->setValue("ui/notifications", enabled);
    emit notificationsChanged();
}

QVariant ConfigManager::value(const QString &key, const QVariant &defaultValue) const {
    return m_settings->value(key, defaultValue);
}

void ConfigManager::setValue(const QString &key, const QVariant &value) {
    m_settings->setValue(key, value);
}

void ConfigManager::saveWindowSize(int width, int height) {
    m_settings->setValue("window/width", width);
    m_settings->setValue("window/height", height);
    m_settings->sync();
}
