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

private:
    void cleanup();

    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;

    QCache<QString, QImage> m_cache;
    QAtomicInt m_cancelFlag;
};
