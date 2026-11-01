#include "KeyboardLayoutWatcher.hpp"

#include <QMutexLocker>
#include <QThread>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace {

// XKB-коды клавиш (evdev + 8), которые встречаются в подсказках по клавишам.
const QHash<QString, int> &keyTable() {
    static const QHash<QString, int> table = {
        { QStringLiteral("Q"), 24 }, { QStringLiteral("E"), 26 }, { QStringLiteral("S"), 39 },
        { QStringLiteral("I"), 31 }, { QStringLiteral("F"), 41 }, { QStringLiteral("P"), 33 },
        { QStringLiteral("T"), 28 }, { QStringLiteral("C"), 54 }, { QStringLiteral("A"), 38 },
        { QStringLiteral("D"), 40 }, { QStringLiteral("R"), 27 }, { QStringLiteral("J"), 44 },
        { QStringLiteral("M"), 58 }
    };
    return table;
}

} // namespace

class KeyboardLayoutWatcher::Worker : public QThread {
    Q_OBJECT
public:
    explicit Worker(QObject *parent = nullptr) : QThread(parent) {}

    void stop() { m_stop.store(true); }

signals:
    void labelsUpdated(const QHash<int, QString> &labels);

protected:
    void run() override;

private:
    static void onGlobal(void *data, wl_registry *registry, uint32_t name,
                         const char *interface, uint32_t version);
    static void onGlobalRemove(void *, wl_registry *, uint32_t) {}
    static void onKeymap(void *data, wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size);
    static void onEnter(void *, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {}
    static void onLeave(void *, wl_keyboard *, uint32_t, wl_surface *) {}
    static void onKey(void *, wl_keyboard *, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void onModifiers(void *data, wl_keyboard *keyboard, uint32_t serial,
                            uint32_t modsDepressed, uint32_t modsLatched, uint32_t modsLocked,
                            uint32_t group);
    static void onRepeatInfo(void *, wl_keyboard *, int32_t, int32_t) {}

    void applyKeyboardState();
    void rebuildLabels();

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    wl_keyboard *m_keyboard = nullptr;
    xkb_context *m_xkb = nullptr;
    xkb_keymap *m_keymap = nullptr;
    xkb_state *m_state = nullptr;

    quint32 m_depressed = 0;
    quint32 m_latched = 0;
    quint32 m_locked = 0;
    quint32 m_group = 0;

    std::atomic<bool> m_stop { false };
};

void KeyboardLayoutWatcher::Worker::run() {
    m_display = wl_display_connect(nullptr);
    if (!m_display) return;

    m_xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!m_xkb) {
        wl_display_disconnect(m_display);
        m_display = nullptr;
        return;
    }

    m_registry = wl_display_get_registry(m_display);
    if (m_registry) {
        static const wl_registry_listener registryListener = {
            &KeyboardLayoutWatcher::Worker::onGlobal,
            &KeyboardLayoutWatcher::Worker::onGlobalRemove
        };
        wl_registry_add_listener(m_registry, &registryListener, this);
        wl_display_roundtrip(m_display);
    }

    if (m_seat) {
        m_keyboard = wl_seat_get_keyboard(m_seat);
        if (m_keyboard) {
            static const wl_keyboard_listener keyboardListener = {
                &KeyboardLayoutWatcher::Worker::onKeymap,
                &KeyboardLayoutWatcher::Worker::onEnter,
                &KeyboardLayoutWatcher::Worker::onLeave,
                &KeyboardLayoutWatcher::Worker::onKey,
                &KeyboardLayoutWatcher::Worker::onModifiers,
                &KeyboardLayoutWatcher::Worker::onRepeatInfo
            };
            wl_keyboard_add_listener(m_keyboard, &keyboardListener, this);
            wl_display_roundtrip(m_display);
        }
    }

    pollfd pfd { wl_display_get_fd(m_display), static_cast<short>(POLLIN), 0 };
    while (!m_stop.load()) {
        const int ret = poll(&pfd, 1, 200);
        if (ret < 0) break;
        if (ret == 0) continue;
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(m_display) < 0) break;
        } else if (pfd.revents & (POLLERR | POLLHUP)) {
            break;
        }
    }

    if (m_keyboard) { wl_keyboard_destroy(m_keyboard); m_keyboard = nullptr; }
    if (m_seat) { wl_seat_destroy(m_seat); m_seat = nullptr; }
    if (m_registry) { wl_registry_destroy(m_registry); m_registry = nullptr; }
    if (m_state) { xkb_state_unref(m_state); m_state = nullptr; }
    if (m_keymap) { xkb_keymap_unref(m_keymap); m_keymap = nullptr; }
    if (m_xkb) { xkb_context_unref(m_xkb); m_xkb = nullptr; }
    if (m_display) { wl_display_disconnect(m_display); m_display = nullptr; }
}

void KeyboardLayoutWatcher::Worker::onGlobal(void *data, wl_registry *registry, uint32_t name,
                                             const char *interface, uint32_t version) {
    auto *self = static_cast<Worker*>(data);
    if (!self->m_seat && std::strcmp(interface, wl_seat_interface.name) == 0)
        self->m_seat = static_cast<wl_seat*>(wl_registry_bind(
            registry, name, &wl_seat_interface, std::min<uint32_t>(version, 7)));
}

void KeyboardLayoutWatcher::Worker::onKeymap(void *data, wl_keyboard *, uint32_t format,
                                             int32_t fd, uint32_t size) {
    auto *self = static_cast<Worker*>(data);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
        close(fd);
        return;
    }

    void *addr = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (addr == MAP_FAILED) return;

    xkb_keymap *keymap = xkb_keymap_new_from_string(self->m_xkb, static_cast<const char*>(addr),
                                                    XKB_KEYMAP_FORMAT_TEXT_V1,
                                                    XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(addr, size);
    if (!keymap) return;

    xkb_state *state = xkb_state_new(keymap);
    if (!state) {
        xkb_keymap_unref(keymap);
        return;
    }

    if (self->m_state) xkb_state_unref(self->m_state);
    if (self->m_keymap) xkb_keymap_unref(self->m_keymap);
    self->m_keymap = keymap;
    self->m_state = state;

    self->applyKeyboardState();
    self->rebuildLabels();
}

void KeyboardLayoutWatcher::Worker::onModifiers(void *data, wl_keyboard *, uint32_t,
                                                uint32_t modsDepressed, uint32_t modsLatched,
                                                uint32_t modsLocked, uint32_t group) {
    auto *self = static_cast<Worker*>(data);

    self->m_depressed = modsDepressed;
    self->m_latched = modsLatched;
    self->m_locked = modsLocked;
    self->m_group = group;

    self->applyKeyboardState();
    self->rebuildLabels();
}

void KeyboardLayoutWatcher::Worker::applyKeyboardState() {
    if (!m_state) return;
    xkb_state_update_mask(m_state, m_depressed, m_latched, m_locked, 0, 0, m_group);
}

void KeyboardLayoutWatcher::Worker::rebuildLabels() {
    if (!m_state) return;

    QHash<int, QString> labels;
    for (auto it = keyTable().cbegin(); it != keyTable().cend(); ++it) {
        const xkb_keysym_t sym = xkb_state_key_get_one_sym(m_state, static_cast<xkb_keycode_t>(it.value()));
        if (sym == XKB_KEY_NoSymbol) continue;

        char buffer[32] = {0};
        if (xkb_keysym_to_utf8(sym, buffer, sizeof(buffer)) <= 1) continue;

        const QString text = QString::fromUtf8(buffer).trimmed().toUpper();
        if (!text.isEmpty()) labels.insert(it.value(), text);
    }

    if (!labels.isEmpty()) emit labelsUpdated(labels);
}

KeyboardLayoutWatcher::KeyboardLayoutWatcher(QObject *parent) : QObject(parent) {
    qRegisterMetaType<QHash<int, QString>>();

    m_worker = new Worker(this);
    connect(m_worker, &Worker::labelsUpdated, this,
            [this](const QHash<int, QString> &labels) { updateLabels(labels); });
    m_worker->start();
}

KeyboardLayoutWatcher::~KeyboardLayoutWatcher() {
    if (!m_worker) return;
    m_worker->stop();
    m_worker->wait(2000);
}

QString KeyboardLayoutWatcher::keyName(const QString &key) const {
    const auto it = keyTable().constFind(key);
    if (it == keyTable().constEnd()) return key;

    QMutexLocker locker(&m_mutex);
    const QString label = m_labels.value(it.value());
    return label.isEmpty() ? key : label;
}

bool KeyboardLayoutWatcher::available() const {
    QMutexLocker locker(&m_mutex);
    return !m_labels.isEmpty();
}

void KeyboardLayoutWatcher::updateLabels(const QHash<int, QString> &labels) {
    {
        QMutexLocker locker(&m_mutex);
        m_labels = labels;
    }
    emit changed();
}

#include "KeyboardLayoutWatcher.moc"
