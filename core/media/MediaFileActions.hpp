#pragma once
#include <QObject>
#include <QString>

// Small desktop integrations that do not depend on playback state: revealing a
// file in the file manager, opening a second Luma process, clipboard copy and
// the asynchronous lookup of the file manager's display name.
class MediaFileActions : public QObject {
    Q_OBJECT

public:
    explicit MediaFileActions(QObject *parent = nullptr);

    void resolveDefaultFileManagerName();
    QString defaultFileManagerName() const { return m_defaultFileManagerName; }

    void openInFileManager(const QString &path);
    bool openInNewWindow(const QString &path);
    void copyToClipboard(const QString &text);
    static bool isFileAvailable(const QString &path);

signals:
    void defaultFileManagerNameChanged();

private:
    QString m_defaultFileManagerName = QStringLiteral("File Manager");
    bool m_lookupStarted = false;
};
