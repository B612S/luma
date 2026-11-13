#include "media/MediaFileActions.hpp"
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QLocale>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace {
// Runs on a worker thread: xdg-mime can wait for the desktop database and the
// first call used to stall the GUI for up to a second when the track menu was
// built.
QString lookupDefaultFileManagerName()
{
    QProcess process;
    process.start("xdg-mime", QStringList() << "query" << "default" << "inode/directory");
    process.waitForFinished(1000);

    // Only a desktop-file name is expected; never follow a path from the tool.
    const QString desktopFile = QFileInfo(
        QString::fromUtf8(process.readAllStandardOutput()).trimmed()).fileName();
    QString result;
    if (desktopFile.isEmpty()) {
        result = QStringLiteral("File Manager");
    } else {
        QStringList searchPaths = {
            QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation),
            "/usr/share/applications",
            "/usr/local/share/applications"
        };

        for (const QString &path : searchPaths) {
            QFileInfo fi(path + "/" + desktopFile);
            if (!fi.exists()) continue;
            QSettings desktop(fi.absoluteFilePath(), QSettings::IniFormat);
            desktop.beginGroup("Desktop Entry");
            const QString name = desktop.value("Name[" + QLocale::system().name() + "]",
                                               desktop.value("Name")).toString();
            if (!name.isEmpty()) {
                result = name;
                break;
            }
        }
        if (result.isEmpty()) result = QString(desktopFile).remove(".desktop");
    }
    return result;
}
} // namespace

MediaFileActions::MediaFileActions(QObject *parent)
    : QObject(parent)
{
}

void MediaFileActions::resolveDefaultFileManagerName()
{
    if (m_lookupStarted) return;
    m_lookupStarted = true;

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        const QString name = watcher->result();
        watcher->deleteLater();
        if (name.isEmpty() || name == m_defaultFileManagerName)
            return;
        m_defaultFileManagerName = name;
        emit defaultFileManagerNameChanged();
    });
    watcher->setFuture(QtConcurrent::run(&lookupDefaultFileManagerName));
}

void MediaFileActions::openInFileManager(const QString &path)
{
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists()) return;

    const QString folderPath = fi.absolutePath();
    QProcess process;
    process.setProgram("xdg-open");
    process.setArguments({folderPath});
    if (!process.startDetached())
        QDesktopServices::openUrl(QUrl::fromLocalFile(folderPath));
}

bool MediaFileActions::openInNewWindow(const QString &path)
{
    if (path.isEmpty()) return false;

    QString program = QCoreApplication::applicationFilePath();
    // Inside an AppImage applicationFilePath() points into the temporary
    // mount. Restart the AppImage itself so the new process gets the same
    // runtime (libraries, QML modules, translations).
    const QByteArray appImage = qgetenv("APPIMAGE");
    if (!appImage.isEmpty())
        program = QString::fromLocal8Bit(appImage);

    return QProcess::startDetached(program, {path});
}

void MediaFileActions::copyToClipboard(const QString &text)
{
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

bool MediaFileActions::isFileAvailable(const QString &path)
{
    if (path.isEmpty()) return false;
    // Network streams have no local file to stat; the information dialog still
    // works for them (it edits the URL).
    if (path.startsWith(QStringLiteral("http://")) || path.startsWith(QStringLiteral("https://")))
        return true;
    return QFileInfo::exists(path);
}
