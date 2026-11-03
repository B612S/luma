// Luma-specific file: not part of upstream KDE mpvqt (see mpvqt/README.md).
#pragma once
#include <QObject>
#include <QImage>
#include <QString>
#include <QCache>
#include <QAtomicInt>

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
}

class FrameDecoder : public QObject {
    Q_OBJECT
public:
    explicit FrameDecoder(QObject *parent = nullptr);
    ~FrameDecoder();

    QImage decodeFrame(const QString &filePath, double timeInSeconds);
    void cancelCurrentDecode();
    void clearCache();

private:
    bool openFile(const QString &filePath);
    void cleanup();

    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;

    // The open file is reused between thumbnail requests: a repeated
    // avformat_open_input for every frame is the most expensive part.
    QString m_currentPath;
    int m_videoStreamIndex = -1;

    QCache<QString, QImage> m_cache;
    QAtomicInt m_cancelFlag;
};
