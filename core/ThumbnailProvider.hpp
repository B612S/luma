#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QHash>
#include <QMutex>

class ThumbnailProvider : public QQuickImageProvider {
public:
    ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    Q_INVOKABLE void insertImage(const QString &id, const QImage &image) {
        if (image.isNull()) return;
        QMutexLocker locker(&m_mutex);
        if (m_images.size() > 50)
            m_images.clear();
        m_images.insert(id, image);
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        Q_UNUSED(requestedSize)
        // requestImage() may be called from the image-loading thread while the
        // GUI thread inserts a freshly decoded thumbnail.
        QMutexLocker locker(&m_mutex);
        const auto it = m_images.constFind(id);
        if (it == m_images.constEnd()) return QImage();
        const QImage img = it.value();
        if (size) *size = img.size();
        return img;
    }

private:
    QMutex m_mutex;
    QHash<QString, QImage> m_images;
};
