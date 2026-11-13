#pragma once
#include <QHash>
#include <QSet>
#include <QJSValue>
#include <QObject>
#include <QVariantList>
#include <QVector>
#include <QVariantMap>
#include "plugins/PluginManifest.hpp"
#include "plugins/PluginShortcut.hpp"

class ConfigManager;
class MediaManager;
class OsdManager;
class PlaylistManager;
class PluginRuntime;
class PluginWindow;

class PluginManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantList menuActions READ menuActions NOTIFY menuActionsChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
    Q_PROPERTY(QVariantList statsLines READ statsLines NOTIFY statsChanged)
    Q_PROPERTY(QVariantList statsPages READ statsPages NOTIFY statsPagesChanged)
    Q_PROPERTY(QString statsPage READ statsPage NOTIFY statsPageChanged)

public:
    PluginManager(ConfigManager *config, MediaManager *media,
                  PlaylistManager *playlists, OsdManager *osd,
                  QObject *parent = nullptr);
    ~PluginManager() override;

    QVariantList plugins() const;
    QVariantList menuActions() const;
    QList<PluginShortcutAction> shortcutActions() const;
    QVariantList windows() const;
    QVariantList statsLines() const;
    QVariantList statsPages() const;
    QString statsPage() const;
    Q_INVOKABLE void showStatsPage(const QString &id);
    bool showStatsPageForKey(const QString &key);
    int allocateStatsPageSlot(const QString &pluginId, const QString &pageId,
                              int preferredSlot = 0);
    void releaseStatsPageSlot(const QString &pluginId, const QString &pageId);

    Q_INVOKABLE bool setPluginEnabled(const QString &id, bool enabled);
    // Explicit, separate consent for direct mpv access and external tasks.
    Q_INVOKABLE void approveMpv(const QString &id);
    Q_INVOKABLE void approveProcess(const QString &id);
    Q_INVOKABLE void reload();
    Q_INVOKABLE QString installPluginDialog();
    Q_INVOKABLE bool removePlugin(const QString &id);
    Q_INVOKABLE void openPluginsFolder();
    Q_INVOKABLE QVariantList pluginSettings(const QString &id) const;
    Q_INVOKABLE void setPluginSetting(const QString &id, const QString &key,
                                      const QVariant &value);
    QString pluginTranslate(const PluginManifest &manifest, const QString &text) const;
    Q_INVOKABLE void runMenuAction(const QString &actionId,
                                   const QVariantMap &context = {});
    Q_INVOKABLE void closeWindow(QObject *window);
    bool handleShortcut(int key, Qt::KeyboardModifiers modifiers);
    bool invokeShortcutAction(const QString &pluginId, const QString &actionId);

    // Accessed by PluginApi.
    MediaManager *media() const { return m_media; }
    PlaylistManager *playlists() const { return m_playlists; }
    OsdManager *osd() const { return m_osd; }

    // Runtime callbacks.
    void notifyMenuActionsChanged();
    void notifyWindowsChanged();
    void notifyStatsChanged();
    void notifyStatsPagesChanged();
    void addWindow(PluginWindow *window);
    void removeWindow(PluginWindow *window);

signals:
    void pluginsChanged();
    void menuActionsChanged();
    void shortcutActionsChanged();
    void windowsChanged();
    void statsChanged();
    void statsPagesChanged();
    void statsPageChanged();
    void statsPageRequested(const QString &id);

private:
    void scan();
    void loadAll();
    QStringList disabledFromSettings() const;
    void saveDisabled(const QStringList &disabled);
    QStringList knownFromSettings() const;
    void saveKnown(const QStringList &known);
    void unloadAll();
    void connectEvents();
    void dispatch(const QString &event, const QVariantMap &payload = {});
    QStringList dangerousPermissions(const PluginManifest &manifest) const;
    QStringList processPermissions(const PluginManifest &manifest) const;
    bool hasDangerousPermissions(const PluginManifest &manifest) const;
    bool isMpvApproved(const PluginManifest &manifest) const;
    bool isProcessApproved(const PluginManifest &manifest) const;
    void saveMpvApproval(const PluginManifest &manifest);
    void saveProcessApproval(const PluginManifest &manifest);
    void removeDangerousApproval(const QString &id);

    ConfigManager *m_config = nullptr;
    MediaManager *m_media = nullptr;
    PlaylistManager *m_playlists = nullptr;
    OsdManager *m_osd = nullptr;
    QVector<PluginManifest> m_manifests;
    QHash<QString, PluginRuntime *> m_runtimes;
    QHash<QString, QString> m_errors;
    QSet<QString> m_disabled;
    QString m_statsPage = QStringLiteral("luma");
    QHash<QString, int> m_statsPageSlots;
    QVector<PluginWindow *> m_windows;
};
