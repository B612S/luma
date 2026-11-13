#pragma once

#include <QObject>
#include <QSet>
#include <QThread>

// Reads audio tags in a background thread. Results are delivered on the
// object's thread (normally the GUI thread) through scanned().
class TagScanner : public QObject {
    Q_OBJECT
public:
    explicit TagScanner(QObject *parent = nullptr);
    ~TagScanner() override;

    void enqueue(const QString &path);
    void stop();

signals:
    void scanned(const QString &path, const QString &title, const QString &artist,
                 const QString &album, int year, bool ok);

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
    QSet<QString> m_pending;
};
