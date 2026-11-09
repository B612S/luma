#include "ShortcutHandler.hpp"
#include "PluginManager.hpp"
#include <QKeySequence>
#include "CastManager.hpp"
#include "GlobalShortcuts.hpp"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <xkbcommon/xkbcommon.h>
#include <QWheelEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QApplication>
#include <QWidget>
#include <QQuickItem>
#include <QVariantMap>

namespace {

constexpr int kModifierMask = Qt::ShiftModifier | Qt::ControlModifier
                            | Qt::AltModifier | Qt::MetaModifier;

Qt::KeyboardModifiers parseModifiers(const QString &text)
{
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    if (text.contains(QStringLiteral("Ctrl"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("Control"), Qt::CaseInsensitive))
        mods |= Qt::ControlModifier;
    if (text.contains(QStringLiteral("Alt"), Qt::CaseInsensitive))
        mods |= Qt::AltModifier;
    if (text.contains(QStringLiteral("Shift"), Qt::CaseInsensitive))
        mods |= Qt::ShiftModifier;
    if (text.contains(QStringLiteral("Meta"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("Super"), Qt::CaseInsensitive))
        mods |= Qt::MetaModifier;
    return mods;
}

QString modifiersPrefix(Qt::KeyboardModifiers mods)
{
    QString prefix;
    if (mods & Qt::ControlModifier) prefix += QStringLiteral("Ctrl+");
    if (mods & Qt::AltModifier) prefix += QStringLiteral("Alt+");
    if (mods & Qt::ShiftModifier) prefix += QStringLiteral("Shift+");
    if (mods & Qt::MetaModifier) prefix += QStringLiteral("Meta+");
    return prefix;
}

xkb_keymap *physicalKeymap()
{
    static xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    static xkb_keymap *keymap = []() {
        xkb_rule_names names{};
        names.layout = "us";
        names.model = "pc105";
        return xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }();
    return keymap;
}

// Layout-independent label for a Wayland/evdev keycode. The US keymap is used
// only for display, so physical M stays "M" even if the user typed it on a
// Russian layout. Matching itself remains keycode-based.
QString physicalKeyName(quint32 nativeScanCode)
{
    if (nativeScanCode < 8) return QString();
    xkb_keymap *keymap = physicalKeymap();
    if (!keymap) return QString();

    // Wayland's native scan code is already the xkbcommon keycode
    // (evdev + 8), so do not subtract the offset here.
    const xkb_keycode_t code = nativeScanCode;
    const xkb_keysym_t *syms = nullptr;
    const int count = xkb_keymap_key_get_syms_by_level(keymap, code, 0, 0, &syms);
    if (count <= 0 || !syms || syms[0] == XKB_KEY_NoSymbol) return QString();

    const xkb_keysym_t sym = syms[0];
    char utf8[16] = {};
    const int length = xkb_keysym_to_utf8(sym, utf8, sizeof(utf8));
    QString text;
    // xkb_keysym_to_utf8 returns the byte count including the terminating NUL.
    if (length > 1)
        text = QString::fromUtf8(utf8, length - 1);
    if (text == QLatin1String(" "))
        return QStringLiteral("Space");
    if (text.size() == 1 && !text.at(0).isPrint())
        text.clear();
    if (text.isEmpty()) {
        char name[64] = {};
        xkb_keysym_get_name(sym, name, sizeof(name));
        text = QString::fromLatin1(name);
    }
    if (text == QLatin1String("space")) text = QStringLiteral("Space");
    if (text == QLatin1String("Return")) text = QStringLiteral("Enter");
    if (text == QLatin1String("Escape")) text = QStringLiteral("Esc");
    // XKB calls these Prior/Next; players show them as PageUp/PageDown.
    if (text == QLatin1String("Prior")) text = QStringLiteral("PageUp");
    if (text == QLatin1String("Next")) text = QStringLiteral("PageDown");
    if (text.startsWith(QLatin1String("XF86"))) text.remove(0, 4);
    if (text.size() == 1 && text.at(0).isLetter())
        text = text.toUpper();
    return text;
}

// A hidden or disabled text field (for example the search box of a closed
// playlist panel) must not keep swallowing player shortcuts: the OSD then
// looks broken until the user clicks the video or the seek bar.
bool textInputFocused()
{
    QObject *focus = QGuiApplication::focusObject();
    if (!focus || !(focus->inherits("QQuickTextInput")
                    || focus->inherits("QQuickTextField")))
        return false;
    auto *item = qobject_cast<QQuickItem *>(focus);
    return !item || (item->isVisible() && item->isEnabled());
}

ShortcutBinding defaultBindingFor(ShortcutAction action)
{
    for (const ShortcutDef &def : defaultShortcuts()) {
        if (def.action != action || action == ShortcutAction::ShowShortcutsHelp)
            continue;
        ShortcutBinding binding;
        binding.action = action;
        binding.keycode = def.keycode;
        binding.modifiers = def.modifiers;
        binding.text = modifiersPrefix(def.modifiers) + QString::fromLatin1(def.keyName);
        return binding;
    }
    return {};
}

ShortcutBinding parseBinding(const QString &value, ShortcutAction action)
{
    const int separator = value.lastIndexOf(QLatin1Char('|'));
    if (separator <= 0) return {};

    const QString text = value.left(separator).trimmed();
    bool ok = false;
    const uint code = value.mid(separator + 1).toUInt(&ok);
    if (!ok || code == 0 || text.isEmpty()) return {};

    ShortcutBinding binding;
    binding.action = action;
    binding.keycode = code;
    binding.modifiers = parseModifiers(text);
    const QString physical = physicalKeyName(code);
    binding.text = physical.isEmpty() ? text : modifiersPrefix(binding.modifiers) + physical;
    return binding;
}

QString serializeBinding(const ShortcutBinding &binding)
{
    if (!binding.isValid()) return QString();
    return binding.text + QLatin1Char('|') + QString::number(binding.keycode);
}

struct RawBinding {
    quint32 keycode = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    QString text;

    bool isValid() const { return keycode > 0 && !text.isEmpty(); }
};

RawBinding parseRawBinding(const QString &value)
{
    RawBinding result;
    const int separator = value.lastIndexOf(QLatin1Char('|'));
    if (separator <= 0) return result;

    const QString text = value.left(separator).trimmed();
    bool ok = false;
    const uint code = value.mid(separator + 1).toUInt(&ok);
    if (!ok || code == 0 || text.isEmpty()) return result;

    result.keycode = code;
    result.modifiers = parseModifiers(text);
    const QString physical = physicalKeyName(code);
    result.text = physical.isEmpty() ? text : modifiersPrefix(result.modifiers) + physical;
    return result;
}

QString serializeRawBinding(const QString &text, quint32 keycode)
{
    if (keycode == 0 || text.isEmpty()) return QString();
    return text + QLatin1Char('|') + QString::number(keycode);
}

// Resolve a key name such as "F9" or "Ctrl+F9" to a Wayland keycode. Only
// names that physicalKeyName() can produce are accepted, which is exactly what
// ShortcutField displays after the user presses a key.
bool keycodeFromName(const QString &text, quint32 *keycode, Qt::KeyboardModifiers *modifiers)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;
    const int plus = trimmed.lastIndexOf(QLatin1Char('+'));
    const QString name = (plus >= 0 ? trimmed.mid(plus + 1) : trimmed).trimmed();
    if (name.isEmpty()) return false;
    const Qt::KeyboardModifiers mods = parseModifiers(trimmed);
    for (quint32 code = 8; code < 256; ++code) {
        const QString physical = physicalKeyName(code);
        if (!physical.isEmpty() && physical.compare(name, Qt::CaseInsensitive) == 0) {
            *keycode = code;
            *modifiers = mods;
            return true;
        }
    }
    return false;
}

bool isReservedStatsShortcut(const QString &text)
{
    const QKeySequence sequence = QKeySequence::fromString(
        text.trimmed(), QKeySequence::PortableText);
    const QString normalized = sequence.toString(QKeySequence::PortableText);
    return normalized.size() == 1 && normalized.at(0).isDigit();
}

QString pluginShortcutId(const QString &pluginId, const QString &actionId)
{
    return QStringLiteral("plugin:") + pluginId + QLatin1Char(':') + actionId;
}

bool splitPluginShortcutId(const QString &id, QString *pluginId, QString *actionId)
{
    if (!id.startsWith(QLatin1String("plugin:"))) return false;
    const int separator = id.indexOf(QLatin1Char(':'), 7);
    if (separator <= 7 || separator + 1 >= id.size()) return false;
    *pluginId = id.mid(7, separator - 7);
    *actionId = id.mid(separator + 1);
    return !pluginId->isEmpty() && !actionId->isEmpty();
}

} // namespace

ShortcutHandler::ShortcutHandler(MpvItem *mpv, OsdManager *osd, MediaManager *media,
                                 QQuickWindow *window, QObject *parent,
                                 bool globalShortcutsAllowed)
: QObject(parent), m_mpv(mpv), m_osd(osd), m_media(media),
  m_globalShortcutsAllowed(globalShortcutsAllowed)
{
    m_seekTimer = new QTimer(this);
    m_seekTimer->setInterval(300);
    connect(m_seekTimer, &QTimer::timeout, this, &ShortcutHandler::updateSeekOsd);

    m_volTimer = new QTimer(this);
    m_volTimer->setInterval(100);
    connect(m_volTimer, &QTimer::timeout, this, &ShortcutHandler::updateVolOsd);

    m_volResetTimer = new QTimer(this);
    m_volResetTimer->setSingleShot(true);
    m_volResetTimer->setInterval(1000);
    connect(m_volResetTimer, &QTimer::timeout, this, [this]() {
        m_isInternalVolume = false;
    });

    m_wheelTargetTimer = new QTimer(this);
    m_wheelTargetTimer->setSingleShot(true);
    m_wheelTargetTimer->setInterval(250);
    connect(m_wheelTargetTimer, &QTimer::timeout, this, [this]() {
        m_wheelTargetValid = false;
    });

    QTimer::singleShot(1000, this, [this]() { m_isInternalVolume = false; });

    loadShortcuts();
    m_globalShortcuts = new GlobalShortcuts(this);
    connect(m_globalShortcuts, &GlobalShortcuts::activated,
            this, &ShortcutHandler::executeActionById);
    connect(m_globalShortcuts, &GlobalShortcuts::availabilityChanged,
            this, &ShortcutHandler::bindingsChanged);
    if (m_globalShortcutsAllowed) updateGlobalShortcuts();

    if (window) setWindow(window);
}

ShortcutHandler::~ShortcutHandler() {
    if (m_window) {
        m_window->removeEventFilter(this);
    }
}

void ShortcutHandler::setWindow(QQuickWindow *window) {
    if (m_window == window) return;
    if (m_window) m_window->removeEventFilter(this);
    m_window = window;
    if (m_window) m_window->installEventFilter(this);
}

void ShortcutHandler::setMpv(MpvItem *mpv) {
    if (m_mpv == mpv) return;
    m_mpv = mpv;
    if (!m_mpv) return;

    connect(m_mpv, &MpvItem::fileEnded, this, [this](const QString &reason) {
        if (reason != "eof") return;
        if (m_ignoreEndFile) return;
        if (m_castManager && m_castManager->casting()) return;

        QTimer::singleShot(100, this, [this]() {
            if (m_media) m_media->playNext();
        });
    });
    connect(m_mpv, &MpvItem::sourceChanged, this, &ShortcutHandler::onFileChanged);
    connect(m_mpv, &MpvItem::positionChanged, this, &ShortcutHandler::onPositionChanged);
    connect(m_mpv, &MpvItem::volumeChanged, this, &ShortcutHandler::onVolumeChanged);
}

void ShortcutHandler::setSliderSeeking(bool seeking) {
    m_isSliderSeeking = seeking;
}

void ShortcutHandler::setPluginManager(PluginManager *plugins)
{
    if (m_pluginManager == plugins) return;
    if (m_pluginManager) disconnect(m_pluginManager, nullptr, this, nullptr);
    m_pluginManager = plugins;
    if (m_pluginManager) {
        connect(m_pluginManager, &PluginManager::shortcutActionsChanged,
                this, &ShortcutHandler::reloadPluginActions);
    }
    reloadPluginActions();
}

void ShortcutHandler::reloadPluginActions()
{
    m_pluginActions = m_pluginManager ? m_pluginManager->shortcutActions()
                                      : QList<PluginShortcutAction>();
    rebuildPluginBindings();
    if (m_globalShortcuts && m_globalShortcutsAllowed) updateGlobalShortcuts();
    emit bindingsChanged();
}

void ShortcutHandler::rebuildPluginBindings()
{
    m_pluginLocal.clear();
    m_pluginGlobal.clear();
    for (const PluginShortcutAction &action : m_pluginActions) {
        const QString id = pluginShortcutId(action.pluginId, action.actionId);
        for (int pass = 0; pass < 2; ++pass) {
            const bool global = pass == 1;
            const QMap<QString, QString> &stored = global ? m_pluginGlobalStored
                                                          : m_pluginLocalStored;
            PluginBinding binding;
            binding.id = id;
            binding.pluginId = action.pluginId;
            binding.actionId = action.actionId;
            binding.label = action.label;
            if (stored.contains(id)) {
                // An empty stored value means the user cleared the binding; do
                // not fall back to the default key again.
                const RawBinding raw = parseRawBinding(stored.value(id));
                if (!raw.isValid()) continue;
                binding.keycode = raw.keycode;
                binding.modifiers = raw.modifiers;
                binding.text = raw.text;
            } else if (!global) {
                // The manifest default is a local key only; a global binding
                // is registered when the user assigns one.
                quint32 keycode = 0;
                Qt::KeyboardModifiers modifiers = Qt::NoModifier;
                if (!keycodeFromName(action.defaultKey, &keycode, &modifiers)) continue;
                binding.keycode = keycode;
                binding.modifiers = modifiers;
                binding.text = modifiersPrefix(modifiers) + physicalKeyName(keycode);
            } else {
                continue;
            }
            if (binding.isValid())
                (global ? m_pluginGlobal : m_pluginLocal).insert(id, binding);
        }
    }
}

void ShortcutHandler::setPluginShortcut(const QString &id, int keycode, int modifiers,
                                        const QString &text, bool global)
{
    QString pluginId, actionId;
    if (!splitPluginShortcutId(id, &pluginId, &actionId)) return;
    if (keycode <= 0 || text.trimmed().isEmpty() || isReservedStatsShortcut(text))
        return;

    bool known = false;
    for (const PluginShortcutAction &action : m_pluginActions) {
        if (action.pluginId == pluginId && action.actionId == actionId) {
            known = true;
            break;
        }
    }
    if (!known) return;

    const quint32 code = static_cast<quint32>(keycode);
    const Qt::KeyboardModifiers mods =
        static_cast<Qt::KeyboardModifiers>(modifiers) & kModifierMask;
    const QString normalized = text.trimmed();

    if (global) {
        m_pluginGlobalStored.insert(id, serializeRawBinding(normalized, code));
        rebuildPluginBindings();
        saveShortcuts();
        emit bindingsChanged();
        updateGlobalShortcuts();
        return;
    }

    m_pluginLocalStored.insert(id, serializeRawBinding(normalized, code));
    // Keep local bindings unambiguous: a key triggers only one action.
    for (auto it = m_localBindings.begin(); it != m_localBindings.end(); ) {
        if (it.value().keycode == code && it.value().modifiers == mods)
            it = m_localBindings.erase(it);
        else
            ++it;
    }
    clearPluginBindingKey(code, mods, id);
    rebuildPluginBindings();
    saveShortcuts();
    emit bindingsChanged();
}

void ShortcutHandler::clearPluginShortcut(const QString &id, bool global)
{
    QString pluginId, actionId;
    if (!splitPluginShortcutId(id, &pluginId, &actionId)) return;
    (global ? m_pluginGlobalStored : m_pluginLocalStored).insert(id, QString());
    rebuildPluginBindings();
    saveShortcuts();
    emit bindingsChanged();
    if (global) updateGlobalShortcuts();
}

void ShortcutHandler::clearPluginBindingKey(quint32 keycode,
                                            Qt::KeyboardModifiers modifiers,
                                            const QString &exceptId)
{
    // Active actions, including their default keys from plugin.json.
    for (const PluginShortcutAction &action : m_pluginActions) {
        const QString id = pluginShortcutId(action.pluginId, action.actionId);
        if (id == exceptId) continue;
        const PluginBinding binding = m_pluginLocal.value(id);
        if (binding.isValid() && binding.keycode == keycode
                && binding.modifiers == modifiers)
            m_pluginLocalStored.insert(id, QString());
    }
    // Stored bindings of plugins that are not loaded right now.
    for (auto it = m_pluginLocalStored.begin(); it != m_pluginLocalStored.end(); ++it) {
        if (it.key() == exceptId || m_pluginLocal.contains(it.key())) continue;
        const RawBinding raw = parseRawBinding(it.value());
        if (raw.isValid() && raw.keycode == keycode && raw.modifiers == modifiers)
            it.value() = QString();
    }
}

QString ShortcutHandler::matchPluginBinding(QKeyEvent *event) const
{
    const quint32 keycode = static_cast<quint32>(event->nativeScanCode());
    const Qt::KeyboardModifiers mods = event->modifiers()
        & ~Qt::GroupSwitchModifier
        & ~Qt::KeypadModifier;
    for (auto it = m_pluginLocal.cbegin(); it != m_pluginLocal.cend(); ++it) {
        if (it.value().isValid() && it.value().keycode == keycode
                && it.value().modifiers == mods)
            return it.key();
    }
    return QString();
}

QString ShortcutHandler::shortcutsPath() const
{
    return m_shortcutsPath;
}

QVariantList ShortcutHandler::bindings() const
{
    QVariantList result;
    for (const ShortcutActionInfo &info : shortcutActions()) {
        const ShortcutBinding local = m_localBindings.value(info.action);
        const ShortcutBinding global = m_globalBindings.value(info.action);
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), QString::fromLatin1(info.id));
        entry.insert(QStringLiteral("category"), QString::fromLatin1(info.category));
        entry.insert(QStringLiteral("localText"), local.text);
        entry.insert(QStringLiteral("globalText"), global.text);
        entry.insert(QStringLiteral("hasLocal"), local.isValid());
        entry.insert(QStringLiteral("hasGlobal"), global.isValid());
        result.append(entry);
    }
    for (const PluginShortcutAction &action : m_pluginActions) {
        const QString id = pluginShortcutId(action.pluginId, action.actionId);
        const PluginBinding local = m_pluginLocal.value(id);
        const PluginBinding global = m_pluginGlobal.value(id);
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), id);
        entry.insert(QStringLiteral("category"), QStringLiteral("plugins"));
        entry.insert(QStringLiteral("label"), action.label);
        entry.insert(QStringLiteral("localText"), local.text);
        entry.insert(QStringLiteral("globalText"), global.text);
        entry.insert(QStringLiteral("hasLocal"), local.isValid());
        entry.insert(QStringLiteral("hasGlobal"), global.isValid());
        result.append(entry);
    }
    return result;
}

bool ShortcutHandler::globalShortcutsAvailable() const
{
    return m_globalShortcuts && m_globalShortcuts->available();
}

QString ShortcutHandler::keyText(int keycode, int key, int modifiers) const
{
    const Qt::KeyboardModifiers mods =
        static_cast<Qt::KeyboardModifiers>(modifiers) & kModifierMask;
    const QString physical = physicalKeyName(static_cast<quint32>(keycode));
    if (!physical.isEmpty())
        return modifiersPrefix(mods) + physical;

    // Fallback for unusual keys without a usable keysym in the display map.
    return QKeySequence(key | static_cast<int>(mods)).toString(QKeySequence::PortableText);
}

void ShortcutHandler::setShortcut(const QString &id, int keycode, int modifiers,
                                  const QString &text, bool global)
{
    if (id.startsWith(QLatin1String("plugin:"))) {
        setPluginShortcut(id, keycode, modifiers, text, global);
        return;
    }

    const ShortcutAction action = shortcutActionFromId(id);
    if (action == ShortcutAction::None || keycode <= 0 || text.trimmed().isEmpty()
            || isReservedStatsShortcut(text))
        return;

    ShortcutBinding binding;
    binding.action = action;
    binding.keycode = static_cast<quint32>(keycode);
    binding.modifiers = static_cast<Qt::KeyboardModifiers>(modifiers) & kModifierMask;
    binding.text = text.trimmed();

    QMap<ShortcutAction, ShortcutBinding> &target = global ? m_globalBindings : m_localBindings;
    target.insert(action, binding);

    if (!global) {
        // Keep local bindings unambiguous: assigning a key to one action
        // removes it from the action that previously owned it.
        QList<ShortcutAction> conflicts;
        for (auto it = m_localBindings.cbegin(); it != m_localBindings.cend(); ++it) {
            if (it.key() == action) continue;
            if (it.value().keycode == binding.keycode
                    && it.value().modifiers == binding.modifiers)
                conflicts.append(it.key());
        }
        for (ShortcutAction conflict : conflicts) m_localBindings.remove(conflict);
        // Plugin actions share the local key space with the built-in ones.
        clearPluginBindingKey(binding.keycode, binding.modifiers, QString());
        rebuildPluginBindings();
    }

    saveShortcuts();
    emit bindingsChanged();
    if (global) updateGlobalShortcuts();
}

void ShortcutHandler::setGlobalShortcutsAllowed(bool allowed)
{
    if (m_globalShortcutsAllowed == allowed) return;
    m_globalShortcutsAllowed = allowed;

    if (m_globalShortcuts) {
        if (!allowed)
            m_globalShortcuts->setShortcuts({});
        else
            updateGlobalShortcuts();
    }
    emit bindingsChanged();
}

void ShortcutHandler::clearShortcut(const QString &id, bool global)
{
    if (id.startsWith(QLatin1String("plugin:"))) {
        clearPluginShortcut(id, global);
        return;
    }

    const ShortcutAction action = shortcutActionFromId(id);
    if (action == ShortcutAction::None) return;

    QMap<ShortcutAction, ShortcutBinding> &target = global ? m_globalBindings : m_localBindings;
    if (!target.contains(action)) return;

    target.remove(action);
    saveShortcuts();
    emit bindingsChanged();
    if (global) updateGlobalShortcuts();
}

void ShortcutHandler::loadShortcuts()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty()) base = QDir::homePath() + "/.config";
    const QString dir = base + "/luma";
    QDir().mkpath(dir);
    m_shortcutsPath = dir + "/shortcuts.conf";

    const bool existed = QFile::exists(m_shortcutsPath);
    QSettings settings(m_shortcutsPath, QSettings::IniFormat);

    for (const ShortcutActionInfo &info : shortcutActions()) {
        const QString id = QString::fromLatin1(info.id);
        const QString localKey = QStringLiteral("local/") + id;
        if (settings.contains(localKey)) {
            const ShortcutBinding binding = parseBinding(settings.value(localKey).toString(), info.action);
            if (binding.isValid() && !isReservedStatsShortcut(binding.text))
                m_localBindings.insert(info.action, binding);
        } else {
            const ShortcutBinding binding = defaultBindingFor(info.action);
            if (binding.isValid()) m_localBindings.insert(info.action, binding);
        }

        const QString globalKey = QStringLiteral("global/") + id;
        if (settings.contains(globalKey)) {
            const ShortcutBinding binding = parseBinding(settings.value(globalKey).toString(), info.action);
            if (binding.isValid()) m_globalBindings.insert(info.action, binding);
        }
    }

    // Keep every declared plugin binding, including actions of plugins that are
    // currently disabled, so enabling a plugin again restores its keys.
    for (const QString &key : settings.allKeys()) {
        if (key.startsWith(QLatin1String("local/plugin:")))
            m_pluginLocalStored.insert(key.mid(6), settings.value(key).toString());
        else if (key.startsWith(QLatin1String("global/plugin:")))
            m_pluginGlobalStored.insert(key.mid(7), settings.value(key).toString());
    }
    rebuildPluginBindings();

    if (!existed) saveShortcuts();
}

void ShortcutHandler::saveShortcuts()
{
    if (m_shortcutsPath.isEmpty()) return;

    QSettings settings(m_shortcutsPath, QSettings::IniFormat);
    settings.clear();
    for (const ShortcutActionInfo &info : shortcutActions()) {
        const QString id = QString::fromLatin1(info.id);
        settings.setValue(QStringLiteral("local/") + id,
                          serializeBinding(m_localBindings.value(info.action)));
        settings.setValue(QStringLiteral("global/") + id,
                          serializeBinding(m_globalBindings.value(info.action)));
    }
    for (auto it = m_pluginLocalStored.cbegin(); it != m_pluginLocalStored.cend(); ++it)
        settings.setValue(QStringLiteral("local/") + it.key(), it.value());
    for (auto it = m_pluginGlobalStored.cbegin(); it != m_pluginGlobalStored.cend(); ++it)
        settings.setValue(QStringLiteral("global/") + it.key(), it.value());
    settings.sync();
}

ShortcutAction ShortcutHandler::matchAction(QKeyEvent *event) const
{
    const quint32 keycode = static_cast<quint32>(event->nativeScanCode());
    const Qt::KeyboardModifiers mods = event->modifiers()
        & ~Qt::GroupSwitchModifier
        & ~Qt::KeypadModifier;

    // The help overlay is intentionally not configurable: "?" always works.
    if (keycode == 61 && (mods == Qt::NoModifier || mods == Qt::ShiftModifier))
        return ShortcutAction::ShowShortcutsHelp;

    for (auto it = m_localBindings.cbegin(); it != m_localBindings.cend(); ++it) {
        if (it.value().keycode == keycode && it.value().modifiers == mods)
            return it.key();
    }
    return ShortcutAction::None;
}

void ShortcutHandler::updateGlobalShortcuts()
{
    if (!m_globalShortcuts) return;
    if (!m_globalShortcutsAllowed) {
        m_globalShortcuts->setShortcuts({});
        return;
    }

    QList<GlobalShortcut> list;
    for (const ShortcutActionInfo &info : shortcutActions()) {
        const ShortcutBinding binding = m_globalBindings.value(info.action);
        if (!binding.isValid()) continue;
        GlobalShortcut shortcut;
        shortcut.id = QString::fromLatin1(info.id);
        shortcut.description = QString::fromLatin1(info.name);
        shortcut.trigger = binding.text;
        list.append(shortcut);
    }
    for (const PluginBinding &binding : m_pluginGlobal) {
        if (!binding.isValid()) continue;
        GlobalShortcut shortcut;
        shortcut.id = binding.id;
        shortcut.description = binding.label;
        shortcut.trigger = binding.text;
        list.append(shortcut);
    }
    m_globalShortcuts->setShortcuts(list);
}

void ShortcutHandler::executeActionById(const QString &id)
{
    if (id.startsWith(QLatin1String("plugin:"))) {
        QString pluginId, actionId;
        if (m_pluginManager && splitPluginShortcutId(id, &pluginId, &actionId))
            m_pluginManager->invokeShortcutAction(pluginId, actionId);
        return;
    }

    const ShortcutAction action = shortcutActionFromId(id);
    if (action != ShortcutAction::None)
        executeAction(action, false);
}

void ShortcutHandler::onFileChanged() {
    m_seekTimer->stop();
    m_volTimer->stop();
    m_volResetTimer->stop();
    m_wheelTargetTimer->stop();
    m_targetSeekTime = 0.0;
    m_lastPosition = 0.0;
    m_seekStep = 0.0;
    m_volStep = 0.0;
    m_wheelVolumeTarget = 0.0;
    m_wheelTargetValid = false;

    m_cropEnabled = false;
    m_cropPending = false;
    if (m_mpv) m_mpv->setCropEnabled(false);

    if (m_osd) m_osd->hideOsd();
    m_isInternalVolume = true;
    m_volResetTimer->start(100);
}

void ShortcutHandler::onPositionChanged() {
    if (!m_mpv) return;
    if (m_isSliderSeeking) return;
    if (m_isInternalSeeking) return;
    if (m_castManager && m_castManager->casting()) return; // Ignore local changes while casting

    double currentPos = m_mpv->position();
    if (currentPos < 0) return;

    if (qAbs(currentPos - m_lastPosition) > 2.0) {
        if (m_osd && !m_osd->visible()) {
            m_osd->requestTimeOsd();
        }
    }
    m_lastPosition = currentPos;
}

void ShortcutHandler::onVolumeChanged() {
    if (!m_mpv || !m_osd) return;
    if (m_isInternalVolume) return;
    if (m_castManager && m_castManager->casting()) return; // Ignore local volume while casting
    m_osd->showVolume();
}

bool ShortcutHandler::isCursorOverQmlPanel() {
    if (!m_window) return false;
    if (!m_playlistItem) m_playlistItem = m_window->findChild<QQuickItem*>("playlistPanel");
    if (!m_statsItem) m_statsItem = m_window->findChild<QQuickItem*>("statsPanel");
    QPoint localPos = m_window->mapFromGlobal(QCursor::pos());
    if (m_playlistItem && m_playlistItem->isVisible()) {
        if (m_playlistItem->contains(m_playlistItem->mapFromItem(m_window->contentItem(), localPos))) return true;
    }
    if (m_statsItem && m_statsItem->isVisible()) {
        if (m_statsItem->contains(m_statsItem->mapFromItem(m_window->contentItem(), localPos))) return true;
    }
    return false;
}

void ShortcutHandler::updateSeekOsd() {
    if (m_castManager && m_castManager->casting()) {
        double dur = m_castManager->remoteDuration();
        if (dur <= 0) { m_seekTimer->stop(); return; }
        if ((m_seekStep > 0 && m_targetSeekTime >= dur) || (m_seekStep < 0 && m_targetSeekTime <= 0.0)) {
            if (m_osd) m_osd->requestSeekOsd(m_targetSeekTime, dur);
            return;
        }
        m_targetSeekTime += m_seekStep;
        m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
        m_castManager->seekTo(m_targetSeekTime);
        if (m_osd) m_osd->requestSeekOsd(m_targetSeekTime, dur);
        return;
    }

    if (!m_mpv || !m_osd) return;
    double dur = m_mpv->duration();
    if (dur <= 0) { m_seekTimer->stop(); return; }
    if ((m_seekStep > 0 && m_targetSeekTime >= dur) || (m_seekStep < 0 && m_targetSeekTime <= 0.0)) {
        m_osd->requestSeekOsd(m_targetSeekTime, dur);
        return;
    }
    m_targetSeekTime += m_seekStep;
    m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
    m_mpv->setPosition(m_targetSeekTime);
    m_osd->requestSeekOsd(m_targetSeekTime, dur);
}

void ShortcutHandler::updateVolOsd() {
    if (m_castManager && m_castManager->casting()) {
        double currentVol = m_castManager->remoteVolume();
        double newVol = qBound(0.0, currentVol + m_volStep, 100.0);
        if (qFuzzyCompare(newVol, currentVol)) return;
        m_castManager->setVolume(newVol);
        if (m_osd) m_osd->showVolume(newVol);
        return;
    }

    if (!m_mpv || !m_osd) return;
    double currentVol = m_mpv->volume();
    double newVol = qBound(0.0, currentVol + m_volStep, 100.0);
    if (qFuzzyCompare(newVol, currentVol)) return;

    m_isInternalVolume = true;
    m_mpv->setVolume(newVol);
    m_osd->showVolume(newVol);
}

void ShortcutHandler::startSeek(double step) {
    m_isInternalSeeking = true;

    if (m_castManager && m_castManager->casting()) {
        double dur = m_castManager->remoteDuration();
        m_targetSeekTime = m_castManager->remotePosition();
        m_seekStep = step;
        m_targetSeekTime += step;
        if (dur > 0) m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
        else m_targetSeekTime = qMax(0.0, m_targetSeekTime);
        m_castManager->seekTo(m_targetSeekTime);
        if (m_osd) m_osd->requestSeekOsd(m_targetSeekTime, dur);
        m_seekTimer->start();
        return;
    }

    if (!m_mpv || !m_osd) return;
    double pos = m_mpv->position();
    double dur = m_mpv->duration();
    m_targetSeekTime = (pos < 0 || dur <= 0) ? 0.0 : pos;
    m_seekStep = step;
    m_targetSeekTime += step;
    if (dur > 0) m_targetSeekTime = qBound(0.0, m_targetSeekTime, dur);
    else m_targetSeekTime = qMax(0.0, m_targetSeekTime);
    m_mpv->setPosition(m_targetSeekTime);
    m_osd->requestSeekOsd(m_targetSeekTime, dur);
    m_seekTimer->start();
}

void ShortcutHandler::startVolume(double step) {
    m_isInternalVolume = true;
    m_volStep = step;

    if (m_castManager && m_castManager->casting()) {
        double currentVol = m_castManager->remoteVolume();
        double newVol = qBound(0.0, currentVol + step, 100.0);
        m_castManager->setVolume(newVol);
        if (m_osd) m_osd->showVolume(newVol);
        m_volTimer->start();
        m_volResetTimer->start();
        return;
    }

    if (!m_mpv || !m_osd) return;
    double currentVol = m_mpv->volume();
    double newVol = qBound(0.0, currentVol + step, 100.0);

    m_mpv->setVolume(newVol);
    m_osd->showVolume(newVol);
    m_volTimer->start();
    m_volResetTimer->start();
}

void ShortcutHandler::executeAction(ShortcutAction action, bool autoRepeat) {
    if (m_castManager && m_castManager->casting()) {
        switch (action) {
            case ShortcutAction::TogglePause:
                if (!autoRepeat) {
                    if (m_castManager->isRemotePlaying()) m_castManager->pause();
                    else m_castManager->play();
                }
                return;
            case ShortcutAction::SeekBackward:
                if (!autoRepeat) startSeek(-5.0); else m_seekStep = -5.0;
                return;
            case ShortcutAction::SeekForward:
                if (!autoRepeat) startSeek(5.0); else m_seekStep = 5.0;
                return;
            case ShortcutAction::SeekBackwardLong:
                if (!autoRepeat) startSeek(-60.0); else m_seekStep = -60.0;
                return;
            case ShortcutAction::SeekForwardLong:
                if (!autoRepeat) startSeek(60.0); else m_seekStep = 60.0;
                return;
            case ShortcutAction::VolumeUp:
                if (!autoRepeat) startVolume(5.0); else m_volStep = 5.0;
                return;
            case ShortcutAction::VolumeDown:
                if (!autoRepeat) startVolume(-5.0); else m_volStep = -5.0;
                return;
            case ShortcutAction::ToggleMute:
                if (!autoRepeat) {
                    if (m_castManager->remoteVolume() > 0.0) {
                        m_castManager->setVolume(0.0);
                        if (m_osd) m_osd->showVolume(0.0);
                    } else {
                        m_castManager->setVolume(50.0);
                        if (m_osd) m_osd->showVolume(50.0);
                    }
                }
                return;
            default:
                break;
        }
    }

    const bool videoOnly = action == ShortcutAction::FrameForward
                        || action == ShortcutAction::FrameBackward
                        || action == ShortcutAction::CycleSubtitles
                        || action == ShortcutAction::CycleAudio
                        || action == ShortcutAction::CycleDeinterlace
                        || action == ShortcutAction::CycleAspectRatio
                        || action == ShortcutAction::ToggleCrop
                        || action == ShortcutAction::FitWindowToVideo;
    if (videoOnly && (!m_media || !m_media->hasVideo()))
        return;

    // Live TV/radio has no DVR window: seeking and frame stepping would only
    // move playback away from the live edge (which followLiveEdge() then has
    // to undo). Pause is still allowed; it jumps back on resume.
    if (m_media && m_media->liveStream()) {
        switch (action) {
            case ShortcutAction::SeekBackward:
            case ShortcutAction::SeekForward:
            case ShortcutAction::SeekBackwardLong:
            case ShortcutAction::SeekForwardLong:
            case ShortcutAction::FrameBackward:
            case ShortcutAction::FrameForward:
                return;
            default:
                break;
        }
    }

    switch (action) {
        case ShortcutAction::TogglePause:
            if (!autoRepeat && m_mpv) m_mpv->setPaused(!m_mpv->paused());
            break;
        case ShortcutAction::SeekBackward:
            if (!autoRepeat) startSeek(-5.0); else m_seekStep = -5.0;
            break;
        case ShortcutAction::SeekForward:
            if (!autoRepeat) startSeek(5.0); else m_seekStep = 5.0;
            break;
        case ShortcutAction::SeekBackwardLong:
            if (!autoRepeat) startSeek(-60.0); else m_seekStep = -60.0;
            break;
        case ShortcutAction::SeekForwardLong:
            if (!autoRepeat) startSeek(60.0); else m_seekStep = 60.0;
            break;

        case ShortcutAction::FrameBackward:
            if (!autoRepeat && m_mpv) m_mpv->command(QStringList() << "frame-back-step");
            break;

        case ShortcutAction::FrameForward:
            if (!autoRepeat && m_mpv) {
                bool oldState = m_mpv->blockSignals(true);
                bool wasMuted = m_mpv->muted();
                m_mpv->setMuted(true);
                m_mpv->command(QStringList() << "frame-step");
                QTimer::singleShot(50, this, [this, wasMuted, oldState]() {
                    if (m_mpv) {
                        m_mpv->setMuted(wasMuted);
                        m_mpv->blockSignals(oldState);
                    }
                });
            }
            break;

        case ShortcutAction::VolumeUp:
            if (!autoRepeat) startVolume(5.0); else m_volStep = 5.0;
            break;
        case ShortcutAction::VolumeDown:
            if (!autoRepeat) startVolume(-5.0); else m_volStep = -5.0;
            break;
        case ShortcutAction::ToggleMute:
            if (!autoRepeat && m_mpv) {
                m_isInternalVolume = true;
                // muted() is updated asynchronously by mpv, so decide from the
                // new state; otherwise the OSD showed the inverted status.
                const bool newMuted = !m_mpv->muted();
                m_mpv->setMuted(newMuted);
                if (m_osd) m_osd->showVolume(newMuted ? 0.0 : m_mpv->volume(), newMuted);
                m_volResetTimer->start();
            }
            break;
        case ShortcutAction::NextFile:
            if (!autoRepeat) {
                m_ignoreEndFile = true;
                QTimer::singleShot(1000, this, [this]() { m_ignoreEndFile = false; });
                if (m_media) m_media->playNext();
            }
            break;
        case ShortcutAction::PreviousFile:
            if (!autoRepeat) {
                m_ignoreEndFile = true;
                QTimer::singleShot(1000, this, [this]() { m_ignoreEndFile = false; });
                if (m_media) m_media->playPrev();
            }
            break;
        case ShortcutAction::PreviousChapter:
            if (!autoRepeat && m_media) m_media->previousChapter();
            break;
        case ShortcutAction::NextChapter:
            if (!autoRepeat && m_media) m_media->nextChapter();
            break;
        case ShortcutAction::TogglePlaylist:
            if (!autoRepeat) emit togglePlaylist();
            break;
        case ShortcutAction::ToggleStats:
            if (!autoRepeat) emit toggleStats();
            break;
        case ShortcutAction::ToggleFullscreen:
            // QML owns the PiP window flags; toggleFullscreen() also leaves PiP
            // before going fullscreen.
            if (!autoRepeat && m_window)
                QMetaObject::invokeMethod(m_window, "toggleFullscreen");
            break;
        case ShortcutAction::ToggleTimeOsd:
            if (!autoRepeat && m_osd) m_osd->togglePinTimeOsd();
            break;
        case ShortcutAction::TogglePip:
            if (!autoRepeat) emit togglePip();
            break;
        case ShortcutAction::FitWindowToVideo:
            if (!autoRepeat) emit fitWindowToVideo();
            break;
        case ShortcutAction::ToggleWindow:
            if (!autoRepeat && m_window) {
                if (m_window->visibility() == QWindow::Minimized) {
                    m_window->showNormal();
                    m_window->raise();
                    m_window->requestActivate();
                } else if (m_window->isVisible()) {
                    m_window->hide();
                } else {
                    m_window->show();
                    m_window->raise();
                    m_window->requestActivate();
                }
            }
            break;
        case ShortcutAction::ShowShortcutsHelp:
            if (!autoRepeat) emit shortcutsHelpVisibleChanged(true);
            break;
        case ShortcutAction::CycleSubtitles:
            if (!autoRepeat) cycleSubtitles();
            break;
        case ShortcutAction::CycleAudio:
            if (!autoRepeat) cycleAudio();
            break;
        case ShortcutAction::CycleDeinterlace:
            break;
        case ShortcutAction::CycleAspectRatio:
            if (!autoRepeat) cycleAspectRatio();
            break;
        case ShortcutAction::ToggleCrop:
            if (!autoRepeat) toggleCrop();
            break;
        case ShortcutAction::ToggleEqualizer:
            if (!autoRepeat) emit toggleEqualizerRequested();
            break;
        case ShortcutAction::None:
        default:
            break;
    }
}

bool ShortcutHandler::eventFilter(QObject *obj, QEvent *event) {
    if (!m_enabled) return false;

    // A click outside the focused text field should leave it. Qt Quick only
    // moves focus when the clicked item is focusable, so clicking the video or
    // a button would otherwise leave the playlist search focused and block all
    // keyboard shortcuts.
    if (event->type() == QEvent::MouseButtonPress) {
        QObject *focus = QGuiApplication::focusObject();
        if (textInputFocused()) {
            auto *item = qobject_cast<QQuickItem *>(focus);
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (item && mouse && m_window) {
                QQuickItem *control = item;
                // The focus object is usually the TextInput inside a TextField;
                // use the whole TextField for the hit test.
                for (QQuickItem *p = item; p; p = p->parentItem()) {
                    if (p->inherits("QQuickTextField")) { control = p; break; }
                }
                const QPointF local = control->mapFromGlobal(mouse->globalPosition());
                if (!control->contains(local)) {
                    // forceActiveFocus() on the root item does not move active
                    // focus away from a child TextField, so clear the field's
                    // focus explicitly.
                    item->setFocus(false);
                    if (control != item) control->setFocus(false);
                }
            }
        }
        return false;
    }

    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *key = static_cast<QKeyEvent*>(event);
        if (!m_mpv) return false;
        // The help dialog is modal for the player: let QML handle Tab, Space,
        // Escape and its close button; only the ? key is handled on release.
        if (m_helpVisible) return false;

        // Ignore when focus is in a visible text field (TextField, TextInput,
        // etc.); hidden fields must not block playback shortcuts.
        if (textInputFocused())
            return false;

        if (key->key() == Qt::Key_Escape && !key->isAutoRepeat()) {
            // With an in-app dialog open, Escape belongs to the dialog (close
            // it) even when the player window is fullscreen.
            const bool dialogOpen = m_window && m_window->property("isDialogOpen").toBool();
            if (!dialogOpen && m_window && (m_window->windowStates() & Qt::WindowFullScreen)) {
                m_window->showNormal();
                return true;
            }
        }

        // Numeric keys are reserved for statistics pages. They never open
        // Stats themselves; when the panel is closed they are simply
        // swallowed. Key 1 is Luma's page, 2-9 are plugin pages, and 0 is the
        // tenth stats slot.
        if (key->modifiers() == Qt::NoModifier && !key->isAutoRepeat()
                && ((key->key() >= Qt::Key_1 && key->key() <= Qt::Key_9)
                    || key->key() == Qt::Key_0)) {
            if (m_window && !m_statsItem)
                m_statsItem = m_window->findChild<QQuickItem*>("statsPanel");
            const bool statsOpen = m_statsItem && m_statsItem->isVisible();
            if (statsOpen) {
                if (key->key() == Qt::Key_1) {
                    emit statsSelected();
                } else if (m_pluginManager) {
                    const QString digit = key->key() == Qt::Key_0
                        ? QStringLiteral("0")
                        : QString::number(key->key() - Qt::Key_1 + 1);
                    m_pluginManager->showStatsPageForKey(digit);
                }
            }
            return true;
        }

        ShortcutAction action = matchAction(key);
        if (action == ShortcutAction::None) {
            // Core bindings always win. Plugin shortcuts only run for otherwise
            // unhandled keys, and never while a modal dialog is open.
            if (m_pluginManager && !key->isAutoRepeat()
                    && !(m_window && m_window->property("isDialogOpen").toBool())) {
                const QString pluginShortcut = matchPluginBinding(key);
                if (!pluginShortcut.isEmpty()) {
                    QString pluginId, actionId;
                    if (splitPluginShortcutId(pluginShortcut, &pluginId, &actionId)
                            && m_pluginManager->invokeShortcutAction(pluginId, actionId))
                        return true;
                }
                if (m_pluginManager->handleShortcut(key->key(), key->modifiers()))
                    return true;
            }
            return false;
        }

        executeAction(action, key->isAutoRepeat());
        return true;
    }
    else if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *key = static_cast<QKeyEvent*>(event);
        if (m_helpVisible) {
            if (matchAction(key) == ShortcutAction::ShowShortcutsHelp) {
                emit shortcutsHelpVisibleChanged(false);
                return true;
            }
            return false;
        }
        if (!key->isAutoRepeat()) {
            ShortcutAction action = matchAction(key);
            switch (action) {
                case ShortcutAction::SeekBackward:
                case ShortcutAction::SeekForward:
                case ShortcutAction::SeekBackwardLong:
                case ShortcutAction::SeekForwardLong:
                    m_seekTimer->stop();
                    m_isInternalSeeking = false;
                    QTimer::singleShot(50, this, [this]() { if (m_osd) m_osd->endTemporaryOsd(); });
                    return true;

                case ShortcutAction::FrameBackward:
                case ShortcutAction::FrameForward:
                    return true;

                case ShortcutAction::ShowShortcutsHelp:
                    emit shortcutsHelpVisibleChanged(false);
                    return true;

                case ShortcutAction::VolumeUp:
                case ShortcutAction::VolumeDown:
                    m_volTimer->stop();
                    m_volResetTimer->start();
                    return true;
                default:
                    break;
            }
        }
    }
    else if (event->type() == QEvent::Wheel) {
        QWidget *focusWidget = QApplication::focusWidget();
        if (focusWidget) {
            QWidget *activeWindow = focusWidget->window();
            if (activeWindow && m_window && activeWindow->winId() != m_window->winId()) {
                return false;
            }
        }

        if (m_helpVisible) return false;
        if (isCursorOverQmlPanel()) return false;
        // A dialog has its own Flickables (settings, media information); the
        // wheel must scroll them instead of changing the player volume.
        if (m_window && m_window->property("isDialogOpen").toBool()) return false;
        QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
        if (!m_mpv) return false;

        m_isInternalVolume = true;

        int delta = wheel->angleDelta().y();
        if (delta == 0) {
            delta = wheel->pixelDelta().y() * 15;
        }

        double volStep = (delta / 120.0) * 5.0;

        // Apply the change on the same event that produced it: the slider reads
        // mpv's volume, so a deferred update made the handle jump after the
        // wheel had already stopped. The target only coalesces a burst of
        // events, so steps are not lost while mpv reports the old volume.
        if (!m_wheelTargetValid) {
            m_wheelVolumeTarget = (m_castManager && m_castManager->casting())
                ? m_castManager->remoteVolume() : m_mpv->volume();
            m_wheelTargetValid = true;
        }
        m_wheelVolumeTarget = qBound(0.0, m_wheelVolumeTarget + volStep, 100.0);

        if (m_castManager && m_castManager->casting()) {
            m_castManager->setVolume(m_wheelVolumeTarget);
        } else {
            m_mpv->setVolume(m_wheelVolumeTarget);
        }
        if (m_osd) m_osd->showVolume(m_wheelVolumeTarget);

        m_wheelTargetTimer->start();
        m_volResetTimer->start();
        return true;
    }
    return QObject::eventFilter(obj, event);
}

void ShortcutHandler::toggleCrop() {
    if (!m_mpv || !m_media || !m_media->hasVideo()) return;
    if (m_castManager && m_castManager->casting()) return;

    if (m_cropEnabled) {
        m_cropEnabled = false;
        m_cropPending = false;

        m_mpv->setCropEnabled(false);
        m_mpv->commandBlocking(QStringList() << "set" << "video-crop" << "");
        m_mpv->commandBlocking(QStringList() << "set" << "video-aspect-override" << "no");

        QVariantList vfList = m_mpv->getProperty("vf").toList();
        for (const QVariant &vf : vfList) {
            if (vf.toMap().value("label").toString() == "autocrop-cropdetect") {
                m_mpv->commandBlocking(QStringList() << "vf" << "remove" << "@autocrop-cropdetect");
                break;
            }
        }
        double pos = m_mpv->position();
        if (pos > 0.0) m_mpv->commandBlocking(QStringList() << "seek" << QString::number(pos) << "absolute+exact");
        m_mpv->update();
        if (m_osd) m_osd->showInfo(tr("osd_crop_off"));
        return;
    }

    if (m_cropPending) return;
    m_cropPending = true;
    if (m_osd) m_osd->showInfo(tr("osd_crop_detecting"));

    m_mpv->commandBlocking(QStringList() << "set" << "video-crop" << "");
    QVariantList vfList = m_mpv->getProperty("vf").toList();
    for (const QVariant &vf : vfList) {
        if (vf.toMap().value("label").toString() == "autocrop-cropdetect") {
            m_mpv->commandBlocking(QStringList() << "vf" << "remove" << "@autocrop-cropdetect");
            break;
        }
    }

    const QString savedHwdec = m_mpv->getProperty("hwdec").toString();
    m_mpv->commandBlocking(QStringList() << "set" << "hwdec" << "no");

    QVariant addResult = m_mpv->commandBlocking(QStringList()
    << "vf" << "add"
    << "@autocrop-cropdetect:cropdetect=limit=24/255:round=2:reset=0");

    {
        ErrorReturn err = addResult.value<ErrorReturn>();
        if (err.error != 0) {
            m_mpv->commandBlocking(QStringList() << "set" << "hwdec" << (savedHwdec.isEmpty() ? "auto" : savedHwdec));
            m_cropPending = false;
            if (m_osd) m_osd->showInfo(tr("osd_crop_failed"));
            return;
        }
    }

    const bool wasPaused = m_mpv->paused();
    m_mpv->setPaused(false);

    QTimer::singleShot(1500, this, [this, wasPaused, savedHwdec]() {
        if (!m_mpv) { m_cropPending = false; return; }
        QVariantMap meta = m_mpv->getProperty("vf-metadata/autocrop-cropdetect").toMap();
        const QString w = meta.value("lavfi.cropdetect.w").toString();
        const QString h = meta.value("lavfi.cropdetect.h").toString();
        const QString x = meta.value("lavfi.cropdetect.x").toString();
        const QString y = meta.value("lavfi.cropdetect.y").toString();

        m_mpv->commandBlocking(QStringList() << "vf" << "remove" << "@autocrop-cropdetect");

        if (!w.isEmpty() && !h.isEmpty() && w != "0" && h != "0") {
            const QString cropRect = QString("%1x%2+%3+%4").arg(w, h, x, y);
            m_mpv->commandBlocking(QStringList() << "set" << "video-crop" << cropRect);
            m_mpv->update();
            if (m_osd) m_osd->showInfo(tr("osd_crop_on"));
            m_cropEnabled = true;
            m_mpv->setCropEnabled(true);
        } else {
            if (m_osd) m_osd->showInfo(tr("osd_crop_failed"));
            m_cropEnabled = false;
            m_mpv->setCropEnabled(false);
        }
        m_mpv->setPaused(wasPaused);
        m_mpv->commandBlocking(QStringList() << "set" << "hwdec" << (savedHwdec.isEmpty() ? "auto" : savedHwdec));
        m_cropPending = false;
    });
}

void ShortcutHandler::cycleAspectRatio() {
    if (!m_mpv) return;
    if (m_castManager && m_castManager->casting()) return;

    QStringList aspects = {"original", "16:9", "4:3", "21:9", "1:1", "9:16"};
    QStringList labels  = {"Original", "16:9", "4:3", "21:9", "1:1", "9:16"};
    m_aspectIndex = (m_aspectIndex + 1) % aspects.size();
    QString mode = aspects[m_aspectIndex];
    QString label = labels[m_aspectIndex];
    if (mode == "original") m_mpv->setProperty("video-aspect-override", "no");
    else m_mpv->setProperty("video-aspect-override", mode);
    if (m_osd) m_osd->showInfo(tr("osd_aspect").arg(label));
}

void ShortcutHandler::cycleSubtitles() {
    if (!m_mpv) return;
    if (m_castManager && m_castManager->casting()) return;

    // Read the cached observations instead of blocking the GUI thread while
    // the mpv core may be opening or buffering a file.
    QVariantList tracks = m_mpv->observedProperty("track-list").toList();
    QList<int> subIds;
    for (const QVariant &t : tracks) {
        QVariantMap map = t.toMap();
        if (map["type"].toString() == "sub") subIds << map["id"].toInt();
    }
    if (subIds.isEmpty()) {
        if (m_osd) m_osd->showInfo(tr("osd_no_subtitles"));
        return;
    }
    int currentId = m_mpv->observedProperty("sid").toInt();
    int nextIndex = 0;
    if (currentId > 0) {
        int idx = subIds.indexOf(currentId);
        if (idx != -1) nextIndex = (idx + 1) % (subIds.size() + 1);
    }
    if (nextIndex == subIds.size()) {
        m_mpv->setProperty("sid", "no");
        if (m_osd) m_osd->showInfo(tr("osd_sub_off"));
    } else {
        m_mpv->setProperty("sid", subIds[nextIndex]);
        if (m_osd) m_osd->showInfo(tr("osd_sub_track").arg(subIds[nextIndex]));
    }
}

void ShortcutHandler::cycleAudio() {
    if (!m_mpv) return;
    if (m_castManager && m_castManager->casting()) return;

    // Read the cached observations instead of blocking the GUI thread while
    // the mpv core may be opening or buffering a file.
    QVariantList tracks = m_mpv->observedProperty("track-list").toList();
    QList<int> audioIds;
    for (const QVariant &t : tracks) {
        QVariantMap map = t.toMap();
        if (map["type"].toString() == "audio") audioIds << map["id"].toInt();
    }
    if (audioIds.isEmpty()) {
        if (m_osd) m_osd->showInfo(tr("osd_no_audio"));
        return;
    }
    int currentId = m_mpv->observedProperty("aid").toInt();
    int nextIndex = 0;
    if (currentId > 0) {
        int idx = audioIds.indexOf(currentId);
        if (idx != -1) nextIndex = (idx + 1) % audioIds.size();
    }
    m_mpv->setProperty("aid", audioIds[nextIndex]);
    if (m_osd) m_osd->showInfo(tr("osd_audio_track").arg(audioIds[nextIndex]));
}
