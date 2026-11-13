#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

// File properties, editable audio tags and live bitrate sampling. The heavy
// FFmpeg/TagLib work runs on the thread pool; the results are delivered as
// signals and forwarded by MediaManager to QML.
class MediaInfoService : public QObject {
    Q_OBJECT

public:
    explicit MediaInfoService(QObject *parent = nullptr);

    void requestFileInfo(const QString &path);
    void requestLiveBitrate(const QString &path, double position);
    QVariantMap readTags(const QString &path) const;
    bool saveTags(const QString &path, const QVariantMap &tags) const;
    void requestSaveTags(const QString &path, const QVariantMap &tags);

signals:
    void fileInfoReady(const QString &path, const QVariantMap &info);
    void tagsSaved(const QString &path, bool success);
    void liveBitrateReady(const QString &path, double position,
                          qint64 videoBps, qint64 audioBps);
};
