#include "PluginManager.hpp"
#include "PluginRuntime.hpp"
#include "PluginLocalization.hpp"
#include "PluginWindow.hpp"
#include "ConfigManager.hpp"
#include "MediaManager.hpp"
#include "OsdManager.hpp"
#include "PlaylistManager.hpp"
#include <QCoreApplication>
#include <algorithm>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

namespace {
QString userPluginsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/luma/plugins");
}

QString systemPluginsDir()
{
    // Installed (or AppImage) prefix: <prefix>/share/luma/plugins.
    return QDir::cleanPath(QCoreApplication::applicationDirPath()
                           + QStringLiteral("/../share/luma/plugins"));
}

QString pluginDataDir(const QString &id)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
           + QStringLiteral("/luma/plugins/data/") + id;
}

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/luma/luma.conf");
}

bool copyDirectory(const QString &source, const QString &target)
{
    QDir sourceDir(source);
    if (!sourceDir.exists())
        return false;
    if (!QDir().mkpath(target))
        return false;
    const QFileInfoList entries = sourceDir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &entry : entries) {
        const QString destination = target + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            if (!copyDirectory(entry.absoluteFilePath(), destination))
                return false;
        } else if (!QFile::copy(entry.absoluteFilePath(), destination)) {
            return false;
        }
    }
    return true;
}
} // namespace

PluginManager::PluginManager(ConfigManager *config, MediaManager *media,
                             PlaylistManager *playlists, OsdManager *osd,
                             QObject *parent)
    : QObject(parent), m_config(config), m_media(media),
      m_playlists(playlists), m_osd(osd)
{
    connectEvents();
    const QStringList disabled = disabledFromSettings();
    m_disabled = QSet<QString>(disabled.cbegin(), disabled.cend());
    scan();
    loadAll();
    dispatch(QStringLiteral("startup"),
             {{QStringLiteral("version"), QCoreApplication::applicationVersion()}});
}

PluginManager::~PluginManager()
{
    dispatch(QStringLiteral("shutdown"));
    unloadAll();
}

QStringList PluginManager::dangerousPermissions(const PluginManifest &manifest) const
{
    // Direct mpv access and any task that can start a program outside Luma are
    // warning-gated. The process:* prefix keeps future external tasks covered.
    QStringList result;
    for (const QString &permission : manifest.permissions) {
        if (permission == QLatin1String("mpv:raw")
                || permission == QLatin1String("process")
                || permission.startsWith(QLatin1String("process:"))) {
            result << permission;
        }
    }
    result.sort();
    return result;
}

QStringList PluginManager::processPermissions(const PluginManifest &manifest) const
{
    QStringList result;
    for (const QString &permission : manifest.permissions) {
        if (permission == QLatin1String("process")
                || permission.startsWith(QLatin1String("process:")))
            result << permission;
    }
    result.sort();
    return result;
}

bool PluginManager::hasDangerousPermissions(const PluginManifest &manifest) const
{
    return !dangerousPermissions(manifest).isEmpty();
}

bool PluginManager::isMpvApproved(const PluginManifest &manifest) const
{
    if (!manifest.permissions.contains(QStringLiteral("mpv:raw")))
        return true;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("plugins/approved/mpv/") + manifest.id)
               .toString() == QLatin1String("1");
}

bool PluginManager::isProcessApproved(const PluginManifest &manifest) const
{
    const QStringList required = processPermissions(manifest);
    if (required.isEmpty())
        return true;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    const QStringList approved = settings.value(
        QStringLiteral("plugins/approved/process/") + manifest.id)
                                     .toString().split(QLatin1Char(','),
                                                       Qt::SkipEmptyParts);
    for (const QString &permission : required) {
        if (!approved.contains(permission))
            return false;
    }
    return true;
}

void PluginManager::saveMpvApproval(const PluginManifest &manifest)
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("plugins/approved/mpv/") + manifest.id,
                      QStringLiteral("1"));
    settings.sync();
}

void PluginManager::saveProcessApproval(const PluginManifest &manifest)
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("plugins/approved/process/") + manifest.id,
                      processPermissions(manifest).join(QLatin1Char(',')));
    settings.sync();
}

void PluginManager::removeDangerousApproval(const QString &id)
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.remove(QStringLiteral("plugins/approved/mpv/") + id);
    settings.remove(QStringLiteral("plugins/approved/process/") + id);
    settings.remove(QStringLiteral("plugins/approved/") + id); // pre-split key
    settings.sync();
}

QVariantList PluginManager::plugins() const
{
    QVariantList list;
    for (const PluginManifest &manifest : m_manifests) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), manifest.id);
        map.insert(QStringLiteral("name"), pluginTranslate(manifest, manifest.name));
        map.insert(QStringLiteral("version"), manifest.version);
        map.insert(QStringLiteral("description"),
                   pluginTranslate(manifest, manifest.description));
        map.insert(QStringLiteral("info"), pluginTranslate(manifest, manifest.info));
        map.insert(QStringLiteral("author"), manifest.author);
        map.insert(QStringLiteral("license"), manifest.license);
        map.insert(QStringLiteral("permissions"), manifest.permissions);
        map.insert(QStringLiteral("directory"), manifest.directory);
        map.insert(QStringLiteral("system"), manifest.system);
        map.insert(QStringLiteral("hasSettings"), !manifest.settings.isEmpty());
        map.insert(QStringLiteral("enabled"), !m_disabled.contains(manifest.id));
        map.insert(QStringLiteral("loaded"), m_runtimes.value(manifest.id)
                   && m_runtimes.value(manifest.id)->isLoaded());
        map.insert(QStringLiteral("error"), m_errors.value(manifest.id));
        const QStringList process = processPermissions(manifest);
        const bool mpvRequired = manifest.permissions.contains(QStringLiteral("mpv:raw"))
                                 && !isMpvApproved(manifest);
        const bool processRequired = !process.isEmpty() && !isProcessApproved(manifest);
        map.insert(QStringLiteral("dangerous"), hasDangerousPermissions(manifest));
        map.insert(QStringLiteral("dangerousPermissions"), dangerousPermissions(manifest));
        map.insert(QStringLiteral("mpvRequired"), mpvRequired);
        map.insert(QStringLiteral("processRequired"), processRequired);
        map.insert(QStringLiteral("mpvApproved"), isMpvApproved(manifest));
        map.insert(QStringLiteral("processApproved"), isProcessApproved(manifest));
        list.append(map);
    }
    return list;
}

QVariantList PluginManager::menuActions() const
{
    QVariantList result;
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime && runtime->isLoaded())
            result.append(runtime->menuActions());
    }
    return result;
}

QList<PluginShortcutAction> PluginManager::shortcutActions() const
{
    QList<PluginShortcutAction> result;
    for (PluginRuntime *runtime : m_runtimes) {
        if (!runtime || !runtime->isLoaded())
            continue;
        const PluginManifest &manifest = runtime->manifest();
        for (const PluginShortcutDef &def : manifest.shortcuts) {
            PluginShortcutAction action;
            action.pluginId = manifest.id;
            action.actionId = def.id;
            action.label = pluginTranslate(manifest, def.label);
            action.defaultKey = def.defaultKey;
            result.append(action);
        }
    }
    return result;
}

QVariantList PluginManager::windows() const
{
    QVariantList result;
    for (PluginWindow *window : m_windows)
        result.append(QVariant::fromValue<QObject *>(window));
    return result;
}

QVariantList PluginManager::statsLines() const
{
    QVariantList result;
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime && runtime->isLoaded())
            result.append(runtime->statsLines());
    }
    return result;
}

QVariantList PluginManager::statsPages() const
{
    QVariantList result;
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime && runtime->isLoaded())
            result.append(runtime->statsPages());
    }
    return result;
}

QString PluginManager::statsPage() const
{
    return m_statsPage;
}

void PluginManager::showStatsPage(const QString &id)
{
    m_statsPage = id;
    emit statsPageChanged();
    emit statsPageRequested(id);
}

bool PluginManager::showStatsPageForKey(const QString &key)
{
    const QVariantList pages = statsPages();
    for (const QVariant &value : pages) {
        const QVariantMap page = value.toMap();
        if (page.value(QStringLiteral("key")).toString() == key) {
            showStatsPage(page.value(QStringLiteral("id")).toString());
            return true;
        }
    }
    return false;
}

int PluginManager::allocateStatsPageSlot(const QString &pluginId,
                                         const QString &pageId,
                                         int preferredSlot)
{
    const QString registryKey = pluginId + QLatin1Char('/') + pageId;
    const auto existing = m_statsPageSlots.constFind(registryKey);
    if (existing != m_statsPageSlots.cend())
        return existing.value();

    QSet<int> used;
    for (int slot : m_statsPageSlots)
        used.insert(slot);
    if (preferredSlot >= 2 && preferredSlot <= 10
            && !used.contains(preferredSlot)) {
        m_statsPageSlots.insert(registryKey, preferredSlot);
        return preferredSlot;
    }
    for (int slot = 2; slot <= 10; ++slot) {
        if (!used.contains(slot)) {
            m_statsPageSlots.insert(registryKey, slot);
            return slot;
        }
    }
    return -1;
}

void PluginManager::releaseStatsPageSlot(const QString &pluginId,
                                         const QString &pageId)
{
    m_statsPageSlots.remove(pluginId + QLatin1Char('/') + pageId);
}

bool PluginManager::setPluginEnabled(const QString &id, bool enabled)
{
    const auto it = std::find_if(m_manifests.cbegin(), m_manifests.cend(),
                                 [&id](const PluginManifest &manifest) {
        return manifest.id == id;
    });
    if (it == m_manifests.cend())
        return false;

    if (!enabled) {
        if (m_disabled.contains(id))
            return false;
        m_disabled.insert(id);
        if (PluginRuntime *runtime = m_runtimes.take(id)) {
            runtime->unload();
            runtime->deleteLater();
        }
        m_errors.remove(id);
    } else {
        const QStringList process = processPermissions(*it);
        const bool mpvRequired = it->permissions.contains(QStringLiteral("mpv:raw"))
                                 && !isMpvApproved(*it);
        const bool processRequired = !process.isEmpty() && !isProcessApproved(*it);
        if (mpvRequired || processRequired) {
            m_errors.insert(id, QCoreApplication::translate(
                "PluginManager", "plugins_requires_approval"));
            emit pluginsChanged();
            return false;
        }
        if (!m_disabled.remove(id))
            return false;
        if (!m_runtimes.contains(id)) {
            auto *runtime = new PluginRuntime(this, *it, this);
            if (!runtime->load()) {
                m_errors.insert(id, runtime->error());
                runtime->deleteLater();
            } else {
                m_runtimes.insert(id, runtime);
                m_errors.remove(id);
            }
        }
    }
    saveDisabled(QStringList(m_disabled.cbegin(), m_disabled.cend()));
    emit pluginsChanged();
    emit menuActionsChanged();
    emit shortcutActionsChanged();
    emit windowsChanged();
    return true;
}

void PluginManager::approveMpv(const QString &id)
{
    const auto it = std::find_if(m_manifests.cbegin(), m_manifests.cend(),
                                 [&id](const PluginManifest &manifest) {
        return manifest.id == id;
    });
    if (it == m_manifests.cend()
            || !it->permissions.contains(QStringLiteral("mpv:raw")))
        return;
    saveMpvApproval(*it);
    m_errors.remove(id);
}

void PluginManager::approveProcess(const QString &id)
{
    const auto it = std::find_if(m_manifests.cbegin(), m_manifests.cend(),
                                 [&id](const PluginManifest &manifest) {
        return manifest.id == id;
    });
    if (it == m_manifests.cend() || processPermissions(*it).isEmpty())
        return;
    saveProcessApproval(*it);
    m_errors.remove(id);
}

QString PluginManager::installPluginDialog()
{
    const QString source = QFileDialog::getExistingDirectory(
        nullptr, QCoreApplication::translate("PluginManager", "plugins_choose_directory"),
        QDir::homePath());
    if (source.isEmpty())
        return {};

    const PluginManifest manifest = PluginManifest::fromDirectory(source, false);
    if (!manifest.valid)
        return manifest.error;
    const bool exists = std::any_of(m_manifests.cbegin(), m_manifests.cend(),
                                    [&manifest](const PluginManifest &item) {
        return item.id == manifest.id;
    });
    if (exists || QFileInfo::exists(userPluginsDir() + QLatin1Char('/') + manifest.id))
        return QCoreApplication::translate("PluginManager", "plugins_already_installed")
                   .arg(manifest.id);

    QDir().mkpath(userPluginsDir());
    const QString destination = userPluginsDir() + QLatin1Char('/') + manifest.id;
    if (!copyDirectory(source, destination)) {
        QDir(destination).removeRecursively();
        return QCoreApplication::translate("PluginManager", "plugins_install_failed");
    }
    // Newly installed plugins start disabled; the user enables them explicitly.
    m_disabled.insert(manifest.id);
    removeDangerousApproval(manifest.id);
    saveDisabled(QStringList(m_disabled.cbegin(), m_disabled.cend()));
    reload();
    return {};
}

bool PluginManager::removePlugin(const QString &id)
{
    const auto it = std::find_if(m_manifests.cbegin(), m_manifests.cend(),
                                 [&id](const PluginManifest &manifest) {
        return manifest.id == id;
    });
    if (it == m_manifests.cend())
        return false;
    if (it->system)
        return false; // Bundled plugins are optional but not removable.
    const QString root = QFileInfo(userPluginsDir()).canonicalFilePath();
    const QString directory = QFileInfo(it->directory).canonicalFilePath();
    if (root.isEmpty() || directory.isEmpty()
            || !directory.startsWith(root + QLatin1Char('/')))
        return false;

    if (PluginRuntime *runtime = m_runtimes.take(id)) {
        runtime->unload();
        runtime->deleteLater();
    }
    m_disabled.remove(id);
    removeDangerousApproval(id);
    QStringList known = knownFromSettings();
    known.removeAll(id);
    saveKnown(known);
    saveDisabled(QStringList(m_disabled.cbegin(), m_disabled.cend()));
    if (!QDir(directory).removeRecursively())
        return false;
    scan();
    loadAll();
    emit pluginsChanged();
    emit menuActionsChanged();
    emit shortcutActionsChanged();
    emit windowsChanged();
    return true;
}

void PluginManager::reload()
{
    const QStringList disabled = disabledFromSettings();
    m_disabled = QSet<QString>(disabled.cbegin(), disabled.cend());
    unloadAll();
    scan();
    loadAll();
    emit pluginsChanged();
    emit menuActionsChanged();
    emit shortcutActionsChanged();
    emit windowsChanged();
    dispatch(QStringLiteral("startup"),
             {{QStringLiteral("version"), QCoreApplication::applicationVersion()}});
}

void PluginManager::openPluginsFolder()
{
    const QString folder = userPluginsDir();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void PluginManager::runMenuAction(const QString &actionId, const QVariantMap &context)
{
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime && runtime->isLoaded() && runtime->invokeMenuAction(actionId, context))
            return;
    }
}

void PluginManager::closeWindow(QObject *object)
{
    if (auto *window = qobject_cast<PluginWindow *>(object))
        window->close();
}

bool PluginManager::handleShortcut(int key, Qt::KeyboardModifiers modifiers)
{
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime && runtime->isLoaded()
                && runtime->invokeShortcut(key, modifiers))
            return true;
    }
    return false;
}

bool PluginManager::invokeShortcutAction(const QString &pluginId, const QString &actionId)
{
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime && runtime->isLoaded()
                && runtime->manifest().id == pluginId
                && runtime->invokeShortcutAction(actionId))
            return true;
    }
    return false;
}

void PluginManager::notifyMenuActionsChanged()
{
    emit menuActionsChanged();
}

void PluginManager::notifyWindowsChanged()
{
    emit windowsChanged();
}

void PluginManager::notifyStatsChanged()
{
    emit statsChanged();
}

void PluginManager::notifyStatsPagesChanged()
{
    emit statsPagesChanged();
}

void PluginManager::addWindow(PluginWindow *window)
{
    if (!window || m_windows.contains(window))
        return;
    m_windows.append(window);
    emit windowsChanged();
}

void PluginManager::removeWindow(PluginWindow *window)
{
    if (m_windows.removeAll(window) > 0)
        emit windowsChanged();
}

QString PluginManager::pluginTranslate(const PluginManifest &manifest,
                                      const QString &text) const
{
    if (text.isEmpty())
        return text;
    const QVariantMap translations = loadPluginTranslations(
        manifest.directory, pluginLanguage(m_config));
    return translatedPluginText(translations, text);
}

QVariantList PluginManager::pluginSettings(const QString &id) const
{
    const auto it = std::find_if(m_manifests.cbegin(), m_manifests.cend(),
                                 [&id](const PluginManifest &manifest) {
        return manifest.id == id;
    });
    if (it == m_manifests.cend())
        return {};
    QSettings settings(pluginDataDir(id) + QStringLiteral("/config.ini"),
                       QSettings::IniFormat);
    QVariantList result;
    for (const QVariant &value : it->settings) {
        QVariantMap item = value.toMap();
        const QString key = item.value(QStringLiteral("key")).toString();
        for (const QString &field : {QStringLiteral("label"), QStringLiteral("description"),
                                     QStringLiteral("placeholder")}) {
            if (item.contains(field))
                item.insert(field, pluginTranslate(*it, item.value(field).toString()));
        }
        if (item.contains(QStringLiteral("options"))) {
            QVariantList options;
            for (const QVariant &option : item.value(QStringLiteral("options")).toList()) {
                QVariantMap localized = option.toMap();
                if (localized.contains(QStringLiteral("label"))) {
                    localized.insert(QStringLiteral("label"),
                                     pluginTranslate(*it, localized.value(QStringLiteral("label")).toString()));
                }
                options << localized;
            }
            item.insert(QStringLiteral("options"), options);
        }
        QVariant settingValue = settings.value(key, item.value(QStringLiteral("default")));
        if (item.value(QStringLiteral("type")).toString() == QLatin1String("switch"))
            settingValue = settingValue.toBool();
        item.insert(QStringLiteral("value"), settingValue);
        result << item;
    }
    return result;
}

void PluginManager::setPluginSetting(const QString &id, const QString &key,
                                     const QVariant &value)
{
    const auto it = std::find_if(m_manifests.cbegin(), m_manifests.cend(),
                                 [&id](const PluginManifest &manifest) {
        return manifest.id == id;
    });
    if (it == m_manifests.cend())
        return;
    const bool known = std::any_of(it->settings.cbegin(), it->settings.cend(),
                                   [&key](const QVariant &entry) {
        return entry.toMap().value(QStringLiteral("key")).toString() == key;
    });
    if (!known)
        return;
    QDir().mkpath(pluginDataDir(id));
    QSettings settings(pluginDataDir(id) + QStringLiteral("/config.ini"),
                       QSettings::IniFormat);
    settings.setValue(key, value);
    settings.sync();

    if (PluginRuntime *runtime = m_runtimes.value(id, nullptr)) {
        if (runtime->isLoaded()) {
            runtime->dispatch(QStringLiteral("settingsChanged"),
                              {{QStringLiteral("key"), key},
                               {QStringLiteral("value"), value},
                               {QStringLiteral("settings"), pluginSettings(id)}});
        }
    }
}

void PluginManager::scan()
{
    m_manifests.clear();

    auto scanRoot = [this](const QString &root, bool system) {
        QDir dir(root);
        const QFileInfoList entries = dir.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            // Plugin-scoped data lives at plugins/data/<id>; it is not a plugin.
            if (entry.fileName() == QLatin1String("data"))
                continue;
            if (!QFileInfo::exists(entry.absoluteFilePath() + QStringLiteral("/plugin.json")))
                continue;
            PluginManifest manifest = PluginManifest::fromDirectory(entry.absoluteFilePath(), system);
            if (!manifest.valid) {
                qWarning().noquote() << "[plugin]" << entry.fileName() << ":" << manifest.error;
                continue;
            }
            // A user-installed plugin takes precedence over a bundled one with
            // the same id; duplicates would otherwise fight over one runtime.
            const bool duplicate = std::any_of(m_manifests.cbegin(), m_manifests.cend(),
                                               [&manifest](const PluginManifest &item) {
                return item.id == manifest.id;
            });
            if (duplicate)
                continue;
            m_manifests.append(manifest);
        }
    };

    scanRoot(userPluginsDir(), false);
    scanRoot(systemPluginsDir(), true);

    // Every plugin that appears for the first time starts disabled, including
    // plugins copied into the directory by hand. A plugin that disappears is
    // forgotten so a later reinstall starts disabled again.
    QStringList known = knownFromSettings();
    QStringList currentIds;
    bool disabledChanged = false;
    for (const PluginManifest &manifest : m_manifests) {
        currentIds << manifest.id;
        if (known.contains(manifest.id))
            continue;
        known << manifest.id;
        m_disabled.insert(manifest.id);
        removeDangerousApproval(manifest.id);
        disabledChanged = true;
    }
    const int before = known.size();
    known.erase(std::remove_if(known.begin(), known.end(), [&currentIds](const QString &id) {
        return !currentIds.contains(id);
    }), known.end());
    if (disabledChanged) {
        saveDisabled(QStringList(m_disabled.cbegin(), m_disabled.cend()));
    }
    if (known.size() != before)
        saveKnown(known);
    else if (disabledChanged)
        saveKnown(known);
}

void PluginManager::loadAll()
{
    bool disabledChanged = false;
    for (const PluginManifest &manifest : m_manifests) {
        if (m_disabled.contains(manifest.id) || m_runtimes.contains(manifest.id))
            continue;
        const QStringList process = processPermissions(manifest);
        const bool mpvRequired = manifest.permissions.contains(QStringLiteral("mpv:raw"))
                                 && !isMpvApproved(manifest);
        const bool processRequired = !process.isEmpty() && !isProcessApproved(manifest);
        if (mpvRequired || processRequired) {
            // Direct mpv or external-task access must never start just because
            // the plugin was dropped into the plugins directory.
            m_disabled.insert(manifest.id);
            disabledChanged = true;
            m_errors.insert(manifest.id, QCoreApplication::translate(
                "PluginManager", "plugins_requires_approval"));
            continue;
        }
        auto *runtime = new PluginRuntime(this, manifest, this);
        if (!runtime->load()) {
            m_errors.insert(manifest.id, runtime->error());
            qWarning().noquote() << "[plugin:" << manifest.id << "]"
                                 << runtime->error();
            runtime->deleteLater();
        } else {
            m_runtimes.insert(manifest.id, runtime);
            m_errors.remove(manifest.id);
        }
    }
    if (disabledChanged)
        saveDisabled(QStringList(m_disabled.cbegin(), m_disabled.cend()));
    emit pluginsChanged();
    emit menuActionsChanged();
    emit shortcutActionsChanged();
}

QStringList PluginManager::disabledFromSettings() const
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("plugins/disabled")).toStringList();
}

void PluginManager::saveDisabled(const QStringList &disabled)
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("plugins/disabled"), disabled);
    settings.sync();
}

QStringList PluginManager::knownFromSettings() const
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("plugins/known")).toStringList();
}

void PluginManager::saveKnown(const QStringList &known)
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("plugins/known"), known);
    settings.sync();
}

void PluginManager::unloadAll()
{
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime) {
            runtime->unload();
            runtime->deleteLater();
        }
    }
    m_runtimes.clear();
    m_windows.clear();
    emit menuActionsChanged();
    emit shortcutActionsChanged();
    emit windowsChanged();
}

void PluginManager::connectEvents()
{
    if (m_media) {
        connect(m_media, &MediaManager::fileChanged, this, [this]() {
            QVariantMap payload;
            const QVariantMap state = m_media ? m_media->pluginState() : QVariantMap();
            payload.insert(QStringLiteral("path"), state.value(QStringLiteral("path")));
            payload.insert(QStringLiteral("state"), state);
            dispatch(QStringLiteral("fileOpened"), payload);
        });
        connect(m_media, &MediaManager::hasMediaChanged, this, [this]() {
            dispatch(QStringLiteral("mediaChanged"),
                     m_media ? m_media->pluginState() : QVariantMap());
        });
        connect(m_media, &MediaManager::liveStreamChanged, this, [this]() {
            dispatch(QStringLiteral("liveStreamChanged"),
                     {{QStringLiteral("live"), m_media && m_media->liveStream()}});
        });
        connect(m_media, &MediaManager::playModeChanged, this, [this]() {
            dispatch(QStringLiteral("playModeChanged"),
                     {{QStringLiteral("mode"), m_media ? m_media->playMode() : QString()}});
        });
        connect(m_media, &MediaManager::pluginPositionChanged, this,
                [this](double position, double duration) {
            if (!m_media) return;
            QVariantMap payload = m_media->pluginState();
            payload.insert(QStringLiteral("position"), position);
            payload.insert(QStringLiteral("duration"), duration);
            dispatch(QStringLiteral("positionChanged"), payload);
        });
        connect(m_media, &MediaManager::pluginPausedChanged, this, [this](bool paused) {
            dispatch(QStringLiteral("pausedChanged"), {{QStringLiteral("paused"), paused}});
        });
        connect(m_media, &MediaManager::pluginVolumeChanged, this, [this](double volume) {
            dispatch(QStringLiteral("volumeChanged"), {{QStringLiteral("volume"), volume}});
        });
    }

    if (m_playlists) {
        connect(m_playlists, &PlaylistManager::playlistsChanged, this, [this]() {
            dispatch(QStringLiteral("playlistChanged"),
                     {{QStringLiteral("active"), m_playlists->activeId()},
                      {QStringLiteral("playing"), m_playlists->playingId()}});
        });
        connect(m_playlists, &PlaylistManager::activeIdChanged, this, [this]() {
            if (!m_playlists) return;
            dispatch(QStringLiteral("playlistActivated"),
                     {{QStringLiteral("id"), m_playlists->activeId()},
                      {QStringLiteral("name"), m_playlists->playlistName(m_playlists->activeId())},
                      {QStringLiteral("isDefault"), m_playlists->activeId() == PlaylistManager::DEFAULT_ID}});
        });
        connect(m_playlists, &PlaylistManager::playingIdChanged, this, [this]() {
            dispatch(QStringLiteral("playingChanged"),
                     {{QStringLiteral("id"), m_playlists ? m_playlists->playingId() : QString()}});
        });
        connect(m_playlists, &PlaylistManager::playlistModeChanged, this,
                [this](const QString &id, const QString &mode) {
            dispatch(QStringLiteral("playlistModeChanged"),
                     {{QStringLiteral("id"), id}, {QStringLiteral("mode"), mode}});
        });
    }

    if (m_config) {
        connect(m_config, &ConfigManager::settingsReloaded, this, [this]() {
            const QStringList disabled = disabledFromSettings();
            if (QSet<QString>(disabled.cbegin(), disabled.cend()) != m_disabled)
                reload();
        });
        connect(m_config, &ConfigManager::languageChanged, this, [this]() {
            if (m_config)
                dispatch(QStringLiteral("languageChanged"),
                         {{QStringLiteral("language"), pluginLanguage(m_config)}});
            emit pluginsChanged();
            emit menuActionsChanged();
            emit shortcutActionsChanged();
        });
    }
}

void PluginManager::dispatch(const QString &event, const QVariantMap &payload)
{
    for (PluginRuntime *runtime : m_runtimes) {
        if (runtime && runtime->isLoaded())
            runtime->dispatch(event, payload);
    }
}
