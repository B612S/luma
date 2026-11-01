#pragma once
#include <QHash>
#include <QMutex>
#include <QObject>

// Reads the keyboard layout directly from Wayland (wl_keyboard.keymap) and
// returns key labels for the layout that is currently active.
// Runs in a separate thread with its own wl_display; when the layout changes,
// the compositor sends a new keymap and the table is updated.
class KeyboardLayoutWatcher : public QObject {
    Q_OBJECT
public:
    explicit KeyboardLayoutWatcher(QObject *parent = nullptr);
    ~KeyboardLayoutWatcher() override;

    // "Q" -> the Cyrillic letter of the Russian layout, "Q" -> "Q" in English.
    Q_INVOKABLE QString keyName(const QString &key) const;

    // true if the layout has already been received from the compositor.
    Q_INVOKABLE bool available() const;

signals:
    void changed();

private:
    class Worker;

    void updateLabels(const QHash<int, QString> &labels);

    mutable QMutex m_mutex;
    QHash<int, QString> m_labels;
    Worker *m_worker = nullptr;
};
