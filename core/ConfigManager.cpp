#include "ConfigManager.hpp"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

ConfigManager::ConfigManager(QObject *parent) : QObject(parent) {
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/luma";
    QString path = configDir + "/luma.conf";
    m_configDir = configDir;

    QDir().mkpath(configDir);

    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);

            out << "[ui]\n";
            out << "remember_volume=true\n";
            out << "default_volume=80\n";
            out << "volume=80\n";
            out << "language=en\n";
            out << "single_instance=true\n";
            out << "notifications=true\n";
            out << "append_to_playlist=false\n";
            out << "pin_controls=false\n\n";

            out << "[playlist]\n";
            out << "mode=sequential\n\n";

            out << "[window]\n";
            out << "width=960\n";
            out << "height=640\n";

            f.close();
        }
    }

    QString mpvPath = configDir + "/mpv.conf";
    if (!QFile::exists(mpvPath)) {
        QFile f(mpvPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&f);

            out << "hwdec=auto\n";
            out << "deinterlace=auto\n";
            out << "ytdl=auto\n";
            out << "cache=yes\n";
            out << "cache-secs=300\n";

            f.close();
        }
    }

    m_settings = new QSettings(path, QSettings::IniFormat, this);

    loadYtdlpConfig();
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

QString ConfigManager::language() const {
    const QString lang = m_settings->value("ui/language", "en").toString();
    // Поддерживаются только языки, для которых собран перевод.
    return lang == "ru" ? "ru" : "en";
}
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

QString ConfigManager::controlBarMode() const {
    QString mode = m_settings->value("ui/control_bar_mode").toString();
    if (mode.isEmpty()) {
        // Перенос старой настройки «закрепить панель».
        mode = m_settings->value("ui/pin_controls", false).toBool()
                   ? QStringLiteral("pinned") : QStringLiteral("float");
    }
    if (mode != QLatin1String("hidden") && mode != QLatin1String("pinned"))
        mode = QStringLiteral("float");
    return mode;
}
void ConfigManager::setControlBarMode(const QString &mode) {
    const QString normalized = (mode == QLatin1String("hidden") || mode == QLatin1String("pinned"))
                                   ? mode : QStringLiteral("float");
    if (controlBarMode() == normalized) return;
    m_settings->setValue("ui/control_bar_mode", normalized);
    emit controlBarModeChanged();
}

QString ConfigManager::subtitleFont() const {
    return m_settings->value("subtitles/font", "sans-serif").toString();
}
void ConfigManager::setSubtitleFont(const QString &font) {
    if (subtitleFont() == font) return;
    m_settings->setValue("subtitles/font", font);
    emit subtitlesChanged();
}

int ConfigManager::subtitleFontSize() const {
    return m_settings->value("subtitles/size", 55).toInt();
}
void ConfigManager::setSubtitleFontSize(int size) {
    size = qBound(10, size, 200);
    if (subtitleFontSize() == size) return;
    m_settings->setValue("subtitles/size", size);
    emit subtitlesChanged();
}

bool ConfigManager::subtitleBold() const {
    return m_settings->value("subtitles/bold", false).toBool();
}
void ConfigManager::setSubtitleBold(bool bold) {
    if (subtitleBold() == bold) return;
    m_settings->setValue("subtitles/bold", bold);
    emit subtitlesChanged();
}

QString ConfigManager::subtitleColor() const {
    return m_settings->value("subtitles/color", "#FFFFFFFF").toString();
}
void ConfigManager::setSubtitleColor(const QString &color) {
    if (subtitleColor() == color) return;
    m_settings->setValue("subtitles/color", color);
    emit subtitlesChanged();
}

int ConfigManager::subtitleBorderSize() const {
    return m_settings->value("subtitles/border_size", 3).toInt();
}
void ConfigManager::setSubtitleBorderSize(int size) {
    size = qBound(0, size, 20);
    if (subtitleBorderSize() == size) return;
    m_settings->setValue("subtitles/border_size", size);
    emit subtitlesChanged();
}

QString ConfigManager::subtitleBorderColor() const {
    return m_settings->value("subtitles/border_color", "#FF000000").toString();
}
void ConfigManager::setSubtitleBorderColor(const QString &color) {
    if (subtitleBorderColor() == color) return;
    m_settings->setValue("subtitles/border_color", color);
    emit subtitlesChanged();
}

bool ConfigManager::subtitleBackground() const {
    return m_settings->value("subtitles/background", false).toBool();
}
void ConfigManager::setSubtitleBackground(bool enabled) {
    if (subtitleBackground() == enabled) return;
    m_settings->setValue("subtitles/background", enabled);
    emit subtitlesChanged();
}

int ConfigManager::subtitlePosition() const {
    return m_settings->value("subtitles/position", 100).toInt();
}
void ConfigManager::setSubtitlePosition(int position) {
    position = qBound(0, position, 100);
    if (subtitlePosition() == position) return;
    m_settings->setValue("subtitles/position", position);
    emit subtitlesChanged();
}

bool ConfigManager::subtitleIgnoreFileStyle() const {
    return m_settings->value("subtitles/ignore_file_style", false).toBool();
}
void ConfigManager::setSubtitleIgnoreFileStyle(bool enabled) {
    if (subtitleIgnoreFileStyle() == enabled) return;
    m_settings->setValue("subtitles/ignore_file_style", enabled);
    emit subtitlesChanged();
}

QString ConfigManager::ytdlpConfigPath() const {
    return m_configDir + "/yt-dlp.conf";
}

bool ConfigManager::showPanelsFullscreen() const {
    return m_settings->value("ui/show_panels_fullscreen", true).toBool();
}
void ConfigManager::setShowPanelsFullscreen(bool enabled) {
    if (showPanelsFullscreen() == enabled) return;
    m_settings->setValue("ui/show_panels_fullscreen", enabled);
    emit showPanelsFullscreenChanged();
}

QString ConfigManager::ytdlpQuality() const { return m_ytdlpQuality; }

void ConfigManager::setYtdlpQuality(const QString &quality) {
    if (m_ytdlpQuality == quality) return;
    m_ytdlpQuality = quality;
    writeYtdlpConfig();
    emit ytdlpSettingsChanged();
}

QString ConfigManager::ytdlpProxy() const { return m_ytdlpProxy; }

void ConfigManager::setYtdlpProxy(const QString &proxy) {
    const QString trimmed = proxy.trimmed();
    if (m_ytdlpProxy == trimmed) return;
    m_ytdlpProxy = trimmed;
    writeYtdlpConfig();
    emit ytdlpSettingsChanged();
}

QString ConfigManager::ytdlpFormatExpression() const {
    // Для прямого воспроизведения нужен ровно один URL, поэтому берём
    // «слитые» форматы (best...), а не bestvideo+bestaudio.
    if (m_ytdlpQuality == "1080")  return QStringLiteral("best[height<=1080]");
    if (m_ytdlpQuality == "720")   return QStringLiteral("best[height<=720]");
    if (m_ytdlpQuality == "480")   return QStringLiteral("best[height<=480]");
    if (m_ytdlpQuality == "audio") return QStringLiteral("bestaudio[ext=m4a]/bestaudio");
    return QStringLiteral("best");
}

static QString ytdlpQualityFromFormat(const QString &expr) {
    if (expr.contains(QStringLiteral("bestaudio"))) return QStringLiteral("audio");
    if (expr.contains(QStringLiteral("height<=1080"))) return QStringLiteral("1080");
    if (expr.contains(QStringLiteral("height<=720"))) return QStringLiteral("720");
    if (expr.contains(QStringLiteral("height<=480"))) return QStringLiteral("480");
    return QStringLiteral("best");
}

void ConfigManager::loadYtdlpConfig() {
    m_ytdlpQuality = QStringLiteral("best");
    m_ytdlpProxy.clear();
    m_ytdlpExtra.clear();

    QFile f(ytdlpConfigPath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        writeYtdlpConfig();
        return;
    }

    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        if (line.startsWith(QStringLiteral("--format="))) {
            m_ytdlpQuality = ytdlpQualityFromFormat(line.mid(9));
            continue;
        }
        if (line.startsWith(QStringLiteral("--format "))) {
            m_ytdlpQuality = ytdlpQualityFromFormat(line.mid(9).trimmed());
            continue;
        }
        if (line.startsWith(QStringLiteral("-f="))) {
            m_ytdlpQuality = ytdlpQualityFromFormat(line.mid(3));
            continue;
        }
        if (line.startsWith(QStringLiteral("--proxy="))) {
            m_ytdlpProxy = line.mid(8).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("--proxy "))) {
            m_ytdlpProxy = line.mid(8).trimmed();
            continue;
        }
        if (line == QLatin1String("--no-playlist")
                || line == QLatin1String("--no-warnings")
                || line == QLatin1String("--no-progress"))
            continue;

        m_ytdlpExtra << line;
    }
    f.close();
}

void ConfigManager::writeYtdlpConfig() {
    QFile f(ytdlpConfigPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;

    QTextStream out(&f);
    out << "# Luma: параметры yt-dlp для воспроизведения ссылок.\n";
    out << "# Формат и прокси задаются в настройках Luma, остальные строки сохраняются.\n";
    out << "# Luma только воспроизводит поток и никогда не скачивает файлы.\n";
    out << "--no-playlist\n";
    out << "--no-warnings\n";
    out << "--no-progress\n";
    out << "--format=" << ytdlpFormatExpression() << "\n";
    if (!m_ytdlpProxy.isEmpty())
        out << "--proxy=" << m_ytdlpProxy << "\n";
    for (const QString &line : m_ytdlpExtra)
        out << line << "\n";

    f.close();
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
