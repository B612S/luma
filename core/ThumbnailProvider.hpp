#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QHash>

class ThumbnailProvider : public QQuickImageProvider {
public:
    ThumbnailProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    Q_INVOKABLE void insertImage(const QString &id, const QImage &image) {
        if (!image.isNull()) {
            if (m_images.size() > 50) {
                m_images.clear();
            }
            m_images.insert(id, image);
        }
    }

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override {
        Q_UNUSED(requestedSize)
        if (m_images.contains(id)) {
            QImage img = m_images.value(id);
            *size = img.size();
            return img;
        }
        return QImage();
    }

private:
    QHash<QString, QImage> m_images;
};
