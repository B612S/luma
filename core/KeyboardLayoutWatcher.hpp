#pragma once
#include <QHash>
#include <QMutex>
#include <QObject>

// Читает раскладку клавиатуры напрямую из Wayland (wl_keyboard.keymap) и
// отдаёт подписи клавиш в раскладке, которая активна прямо сейчас.
// Работает в отдельном потоке со своим wl_display; при смене раскладки
// композитор присылает новый keymap, и таблица обновляется.
class KeyboardLayoutWatcher : public QObject {
    Q_OBJECT
public:
    explicit KeyboardLayoutWatcher(QObject *parent = nullptr);
    ~KeyboardLayoutWatcher() override;

    // «Q» -> «Й» (в русской раскладке), «Q» -> «Q» (в английской).
    Q_INVOKABLE QString keyName(const QString &key) const;

    // true, если раскладка уже получена от композитора.
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
