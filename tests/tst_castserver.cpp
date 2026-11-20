// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 B612S <sem.alimov@inbox.ru>

#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <cmath>

#include "cast/CastMemoryHlsServer.hpp"

// End-to-end test for the cast HLS server: it generates a source with a video
// flash and an audio click every ten seconds, casts it, fetches the segments
// over the same HTTP path the TV uses and checks:
//   * the complete VOD playlist;
//   * every segment starts exactly at its playlist position;
//   * the MPEG-TS continuity counters never reset (one continuous encoder run);
//   * concurrent segment requests are answered on their own connection (the
//     receiver asks for several at once at cast start and after a seek);
//   * seeking restarts the encoder at the requested position;
//   * audio and video stay in sync, without drift, for the whole cast;
//   * the in-memory segment window stays bounded.
// It needs ffmpeg/ffprobe on PATH and skips itself when they are missing.
namespace {

constexpr int kSourceSeconds = 300;
constexpr int kMarkerPeriod = 10;
constexpr int kSegmentSeconds = 2;
constexpr double kPtsTolerance = 0.15;
constexpr double kDriftTolerance = 0.05;
constexpr int kSourceWidth = 640;
constexpr int kSourceHeight = 360;

constexpr int kRequestTimeoutMs = 120000;
constexpr int kProcessTimeoutMs = 180000;

struct SegmentPts {
    double video = -1.0;
    double audio = -1.0;
};

int runProcess(const QString &program, const QStringList &arguments,
               QByteArray *standardOutput, QByteArray *standardError)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        if (standardError) *standardError = "process did not start";
        return -1;
    }
    if (!process.waitForFinished(kProcessTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (standardError) *standardError = "process timed out";
        return -1;
    }
    if (standardOutput) *standardOutput = process.readAllStandardOutput();
    if (standardError) *standardError = process.readAllStandardError();
    if (process.exitStatus() != QProcess::NormalExit)
        return -1;
    return process.exitCode();
}

QString twoDigitSegment(int index)
{
    return QStringLiteral("seg%1.ts").arg(index, 5, 10, QLatin1Char('0'));
}

} // namespace

class TestCastServer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void playlistIsCompleteVod();
    void concurrentSegmentRequests();
    void everySegmentMatchesThePlaylist();
    void audioAndVideoStayInSync();
    void seekingRestartsTheEncoder();
    void memoryWindowIsBounded();

private:
    void generateSource();
    bool startServer();
    QUrl segmentUrl(int index) const;
    QByteArray fetch(const QUrl &url, int *status = nullptr);
    QVector<QByteArray> fetchConcurrently(const QList<int> &indices, int timeoutMs);
    SegmentPts parseSegment(const QByteArray &data, QHash<int, int> *continuity,
                            bool *continuityOk) const;
    QList<double> videoFlashTimes(const QString &path) const;
    QList<double> audioClickTimes(const QString &path) const;
    QString findServerTempDir() const;
    QTemporaryDir m_directory;
    QString m_ffmpeg;
    QString m_ffprobe;
    QString m_sourcePath;
    CastMemoryHlsServer m_server;
    QNetworkAccessManager m_network;
    QStringList m_playlistNames;
    QVector<QByteArray> m_segments;
};

void TestCastServer::initTestCase()
{
    QVERIFY2(m_directory.isValid(), "could not create the test directory");

    m_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    m_ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty())
        QSKIP("ffmpeg and ffprobe are required for the cast tests");

    // The Chromecast is reached directly; never let the system proxy (or a
    // WPAD lookup) interfere with the loopback HTTP test.
    m_network.setProxy(QNetworkProxy::NoProxy);

    generateSource();
    QVERIFY2(startServer(), "could not start the cast HLS server");
    QTRY_VERIFY_WITH_TIMEOUT(m_server.isPlaylistReady(), 60000);
    QVERIFY(!m_server.url().isEmpty());
}

void TestCastServer::generateSource()
{
    const QString video = QStringLiteral("color=c=black:s=%1x%2:r=25:d=%3")
                              .arg(kSourceWidth).arg(kSourceHeight).arg(kSourceSeconds);
    const QString audio = QStringLiteral("aevalsrc='if(lt(mod(t-1,%1),0.06),"
                                         "0.8*sin(2*PI*1000*t),0)':s=48000:d=%2")
                              .arg(kMarkerPeriod).arg(kSourceSeconds);
    // A full-frame white flash for three frames, synchronised with the click.
    const QString flash = QStringLiteral(
        "drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:"
        "enable='lt(mod(t-1,%1),0.12)'").arg(kMarkerPeriod);
    const QString movie = m_directory.filePath(QStringLiteral("source.mp4"));

    QByteArray output;
    QByteArray error;
    const QStringList arguments{
        QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), video,
        QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), audio,
        QStringLiteral("-vf"), flash,
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
        QStringLiteral("ultrafast"), QStringLiteral("-crf"), QStringLiteral("20"),
        QStringLiteral("-g"), QStringLiteral("50"), QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"),
        QStringLiteral("128k"), QStringLiteral("-ac"), QStringLiteral("2"),
        QStringLiteral("-ar"), QStringLiteral("48000"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"), movie};
    const int code = runProcess(m_ffmpeg, arguments, &output, &error);
    if (code != 0)
        QFAIL(qPrintable(QStringLiteral("could not generate the marker source: %1")
                             .arg(QString::fromUtf8(error.right(500)))));

    // Remux into MPEG-TS with a non-zero first timestamp, like a TV recording.
    // Seeking must still be relative to the playlist position.
    m_sourcePath = m_directory.filePath(QStringLiteral("source.ts"));
    const QStringList remuxArguments{
        QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-i"), movie,
        QStringLiteral("-c"), QStringLiteral("copy"),
        QStringLiteral("-output_ts_offset"), QStringLiteral("3600"),
        QStringLiteral("-muxdelay"), QStringLiteral("0"),
        QStringLiteral("-f"), QStringLiteral("mpegts"), m_sourcePath};
    if (runProcess(m_ffmpeg, remuxArguments, &output, &error) != 0)
        QFAIL(qPrintable(QStringLiteral("could not remux the marker source: %1")
                             .arg(QString::fromUtf8(error.right(500)))));

    const QStringList probeArguments{
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-show_entries"), QStringLiteral("format=duration,start_time"),
        QStringLiteral("-of"), QStringLiteral("default=nw=1"), m_sourcePath};
    if (runProcess(m_ffprobe, probeArguments, &output, &error) != 0)
        QFAIL(qPrintable(QStringLiteral("could not probe the marker source: %1")
                             .arg(QString::fromUtf8(error.right(500)))));
    const QString probe = QString::fromUtf8(output);
    QVERIFY2(probe.contains(QLatin1String("start_time=3")),
             "the source must exercise a non-zero first timestamp");
}

bool TestCastServer::startServer()
{
    // start() returns before ffprobe finishes; the URL and the playlist become
    // available asynchronously, so only the probe path is started here.
    return m_server.start(m_sourcePath, QStringLiteral("192.168.0.1"));
}

void TestCastServer::cleanupTestCase()
{
    m_server.stop();
    // stop() removes the RAM directory on a worker thread; let it finish so a
    // test run does not leave stale /dev/shm entries behind.
    QCoreApplication::processEvents();
    QThreadPool::globalInstance()->waitForDone(5000);
}

QUrl TestCastServer::segmentUrl(int index) const
{
    QUrl url(m_server.url());
    QString path = url.path();
    path.truncate(path.lastIndexOf(QLatin1Char('/')) + 1);
    path += twoDigitSegment(index);
    url.setPath(path);
    return url;
}

QByteArray TestCastServer::fetch(const QUrl &url, int *status)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&loop, reply]() {
        reply->abort();
        loop.quit();
    });
    timer.start(kRequestTimeoutMs);
    loop.exec();

    if (status)
        *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->error() == QNetworkReply::NoError
                                ? reply->readAll() : QByteArray();
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "cast segment request failed:" << url.toString()
                   << reply->errorString();
    }
    reply->deleteLater();
    return data;
}

QVector<QByteArray> TestCastServer::fetchConcurrently(const QList<int> &indices,
                                                      int timeoutMs)
{
    QVector<QByteArray> results(indices.size());
    QVector<QNetworkReply *> replies(indices.size());
    QVector<bool> finished(indices.size(), false);
    int remaining = indices.size();
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    for (int i = 0; i < indices.size(); ++i) {
        QNetworkRequest request(segmentUrl(indices.at(i)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply *reply = m_network.get(request);
        replies[i] = reply;
        QObject::connect(reply, &QNetworkReply::finished, &loop,
                         [&, i, reply]() {
            if (!finished.at(i)) {
                finished[i] = true;
                results[i] = reply->error() == QNetworkReply::NoError
                                 ? reply->readAll() : QByteArray();
                if (--remaining == 0)
                    loop.quit();
            }
        });
    }
    timer.start(timeoutMs);
    loop.exec();
    for (int i = 0; i < replies.size(); ++i) {
        if (!finished.at(i)) {
            qWarning() << "concurrent request" << indices.at(i) << "did not finish";
            replies[i]->abort();
        }
        replies[i]->deleteLater();
    }
    return results;
}

SegmentPts TestCastServer::parseSegment(const QByteArray &data,
                                        QHash<int, int> *continuity,
                                        bool *continuityOk) const
{
    SegmentPts result;
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    const int packetSize = 188;
    for (int offset = 0; offset + packetSize <= data.size(); offset += packetSize) {
        const uchar *packet = bytes + offset;
        if (packet[0] != 0x47) {
            // Segment files are a clean TS; anything else is a bug in the
            // server or in the segmenter.
            *continuityOk = false;
            continue;
        }
        const int pid = ((packet[1] & 0x1f) << 8) | packet[2];
        const int adaptation = (packet[3] >> 4) & 0x03;
        const int counter = packet[3] & 0x0f;
        const bool hasPayload = (adaptation & 0x01) != 0;
        if (continuity->contains(pid)) {
            const int previous = continuity->value(pid);
            const int expected = hasPayload ? ((previous + 1) & 0x0f) : previous;
            if (counter != expected)
                *continuityOk = false;
        }
        continuity->insert(pid, counter);
        if (!hasPayload) continue;

        int payload = 4;
        if (adaptation & 0x02)
            payload += 1 + int(packet[4]);
        if (payload + 14 > packetSize) continue;
        if (packet[payload] != 0x00 || packet[payload + 1] != 0x00
                || packet[payload + 2] != 0x01)
            continue;
        const int streamId = packet[payload + 3];
        const int flags = packet[payload + 7];
        if ((flags & 0x80) == 0) continue;
        // PES: payload[6] is the flags byte, [7] holds the PTS/DTS flags,
        // [8] is the header-data length and the 5-byte PTS starts at [9].
        const uchar *pts = packet + payload + 9;
        const qint64 timestamp = (qint64((pts[0] >> 1) & 0x07) << 30)
                                 | (qint64(pts[1]) << 22)
                                 | (qint64((pts[2] >> 1) & 0x7f) << 15)
                                 | (qint64(pts[3]) << 7)
                                 | (qint64((pts[4] >> 1) & 0x7f));
        const double seconds = double(timestamp) / 90000.0;
        if (streamId >= 0xe0 && streamId <= 0xef && result.video < 0.0)
            result.video = seconds;
        else if (streamId >= 0xc0 && streamId <= 0xdf && result.audio < 0.0)
            result.audio = seconds;
    }
    return result;
}

void TestCastServer::playlistIsCompleteVod()
{
    int status = 0;
    const QByteArray data = fetch(QUrl(m_server.url()), &status);
    QCOMPARE(status, 200);
    QVERIFY(!data.isEmpty());
    const QString playlist = QString::fromUtf8(data);
    QVERIFY(playlist.startsWith(QLatin1String("#EXTM3U")));
    QVERIFY(playlist.contains(QLatin1String("#EXT-X-PLAYLIST-TYPE:VOD")));
    QVERIFY(playlist.contains(QLatin1String("#EXT-X-ENDLIST")));
    QVERIFY(playlist.contains(QLatin1String("#EXT-X-TARGETDURATION:2")));

    m_playlistNames.clear();
    for (const QString &rawLine : playlist.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        QVERIFY2(line == twoDigitSegment(m_playlistNames.size()),
                 qPrintable(QStringLiteral("unexpected segment name: %1").arg(line)));
        m_playlistNames.append(line);
    }
    QVERIFY(m_playlistNames.size() >= kSourceSeconds / kSegmentSeconds);
    QVERIFY(m_playlistNames.size() <= kSourceSeconds / kSegmentSeconds + 2);

    // Every segment but the short final one is two seconds long.
    const QStringList lines = playlist.split(QLatin1Char('\n'));
    int segmentIndex = 0;
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (!line.startsWith(QLatin1String("#EXTINF:")))
            continue;
        const double duration = line.mid(8).section(QLatin1Char(','), 0, 0).toDouble();
        QVERIFY(duration > 0.0 && duration <= kSegmentSeconds + 0.01);
        if (segmentIndex < m_playlistNames.size() - 1)
            QVERIFY2(duration > kSegmentSeconds - 0.01,
                     qPrintable(QStringLiteral("segment %1 is too short: %2")
                                    .arg(segmentIndex).arg(duration)));
        ++segmentIndex;
    }
    QCOMPARE(segmentIndex, m_playlistNames.size());
}

void TestCastServer::concurrentSegmentRequests()
{
    // The receiver asks for several segments at once when a cast starts or
    // after a seek. They must all be answered on their own connection and with
    // the requested segment, not with another pending request's body.
    const QList<int> indices{0, 1, 2, 3, 4, 5};
    const QVector<QByteArray> responses = fetchConcurrently(indices, kRequestTimeoutMs);
    m_segments.resize(m_playlistNames.size());
    for (int i = 0; i < indices.size(); ++i) {
        const int index = indices.at(i);
        QVERIFY2(!responses.at(i).isEmpty(),
                 qPrintable(QStringLiteral("segment %1 was not delivered").arg(index)));
        m_segments[index] = responses.at(i);
        bool continuityOk = true;
        QHash<int, int> continuity;
        const SegmentPts pts = parseSegment(responses.at(i), &continuity, &continuityOk);
        QVERIFY(continuityOk);
        QVERIFY2(pts.video >= 0.0, qPrintable(QStringLiteral("segment %1 has no video")
                                                  .arg(index)));
        QVERIFY2(std::abs(pts.video - index * kSegmentSeconds) < kPtsTolerance,
                 qPrintable(QStringLiteral("segment %1 starts at %2 s, expected %3 s")
                                .arg(index).arg(pts.video)
                                .arg(index * kSegmentSeconds)));
    }
}

void TestCastServer::everySegmentMatchesThePlaylist()
{
    const int count = m_playlistNames.size();
    QVERIFY(count > 0);
    m_segments.resize(count);
    for (int index = 0; index < count; ++index) {
        if (m_segments.at(index).isEmpty()) {
            int status = 0;
            m_segments[index] = fetch(segmentUrl(index), &status);
            QCOMPARE(status, 200);
            QVERIFY2(!m_segments.at(index).isEmpty(),
                     qPrintable(QStringLiteral("segment %1 is empty").arg(index)));
        }
    }

    // Parse all segments in order: one continuous encoder run must keep the
    // transport-stream continuity counters monotonic and the segment
    // timestamps exactly at the playlist positions.
    QHash<int, int> continuity;
    bool continuityOk = true;
    double firstAudioVideoOffset = 0.0;
    bool haveOffset = false;
    QString aggregate = m_directory.filePath(QStringLiteral("cast.ts"));
    QFile aggregateFile(aggregate);
    QVERIFY(aggregateFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    for (int index = 0; index < count; ++index) {
        const QByteArray &data = m_segments.at(index);
        aggregateFile.write(data);
        const SegmentPts pts = parseSegment(data, &continuity, &continuityOk);
        if (index < count - 1) {
            QVERIFY2(pts.video >= 0.0,
                     qPrintable(QStringLiteral("segment %1 has no video PES").arg(index)));
            QVERIFY2(std::abs(pts.video - index * kSegmentSeconds) < kPtsTolerance,
                     qPrintable(QStringLiteral("segment %1 starts at %2 s, expected %3 s")
                                    .arg(index).arg(pts.video)
                                    .arg(index * kSegmentSeconds)));
        }
        if (pts.video >= 0.0 && pts.audio >= 0.0) {
            const double offset = pts.audio - pts.video;
            if (!haveOffset) {
                firstAudioVideoOffset = offset;
                haveOffset = true;
            }
            // A new AAC encoder run adds priming; a continuous run keeps the
            // audio/video offset stable across the whole cast.
            QVERIFY2(std::abs(offset - firstAudioVideoOffset) < kDriftTolerance,
                     qPrintable(QStringLiteral("segment %1 moves audio by %2 s")
                                    .arg(index).arg(offset - firstAudioVideoOffset)));
            QVERIFY2(std::abs(offset) < kPtsTolerance,
                     qPrintable(QStringLiteral("segment %1 audio/video offset %2 s")
                                    .arg(index).arg(offset)));
        }
    }
    aggregateFile.close();
    QVERIFY(continuityOk);
    QVERIFY(aggregateFile.size() > 0);
}

QList<double> TestCastServer::videoFlashTimes(const QString &path) const
{
    const QStringList arguments{
        QStringLiteral("-hide_banner"), QStringLiteral("-nostats"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-copyts"), QStringLiteral("-i"), path,
        QStringLiteral("-map"), QStringLiteral("0:v:0"),
        QStringLiteral("-vf"),
        QStringLiteral("signalstats,metadata=mode=print:file=-:key=lavfi.signalstats.YAVG"),
        QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
    QByteArray output;
    QByteArray error;
    if (runProcess(m_ffmpeg, arguments, &output, &error) != 0)
        return {};
    QList<double> times;
    double pts = -1.0;
    double previous = 0.0;
    for (const QByteArray &rawLine : output.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith("frame:")) {
            const int ptsPos = line.indexOf("pts_time:");
            if (ptsPos >= 0) {
                const QByteArray number = line.mid(ptsPos + 9).split(' ').value(0);
                bool ok = false;
                const double value = number.toDouble(&ok);
                if (ok) pts = value;
            }
            continue;
        }
        if (!line.startsWith("lavfi.signalstats.YAVG="))
            continue;
        bool ok = false;
        const double brightness = line.mid(line.indexOf('=') + 1).toDouble(&ok);
        if (ok && brightness > 100.0 && previous <= 100.0 && pts >= 0.0)
            times.append(pts);
        if (ok) previous = brightness;
    }
    return times;
}

QList<double> TestCastServer::audioClickTimes(const QString &path) const
{
    const QStringList arguments{
        QStringLiteral("-hide_banner"), QStringLiteral("-nostats"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-copyts"), QStringLiteral("-i"), path,
        QStringLiteral("-map"), QStringLiteral("0:a:0"),
        QStringLiteral("-af"),
        QStringLiteral("astats=metadata=1:reset=1,"
                       "ametadata=mode=print:file=-:key=lavfi.astats.Overall.RMS_level"),
        QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")};
    QByteArray output;
    QByteArray error;
    if (runProcess(m_ffmpeg, arguments, &output, &error) != 0)
        return {};
    QList<double> times;
    double pts = -1.0;
    double previous = -999.0;
    for (const QByteArray &rawLine : output.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith("frame:")) {
            const int ptsPos = line.indexOf("pts_time:");
            if (ptsPos >= 0) {
                const QByteArray number = line.mid(ptsPos + 9).split(' ').value(0);
                bool ok = false;
                const double value = number.toDouble(&ok);
                if (ok) pts = value;
            }
            continue;
        }
        if (!line.startsWith("lavfi.astats.Overall.RMS_level="))
            continue;
        const QByteArray value = line.mid(line.indexOf('=') + 1).trimmed();
        const double rms = value == "-inf" ? -999.0 : value.toDouble();
        if (rms > -30.0 && previous <= -30.0 && pts >= 0.0)
            times.append(pts);
        previous = rms;
    }
    return times;
}

void TestCastServer::audioAndVideoStayInSync()
{
    const QString aggregate = m_directory.filePath(QStringLiteral("cast.ts"));
    QVERIFY(QFileInfo::exists(aggregate));
    const QList<double> flashes = videoFlashTimes(aggregate);
    const QList<double> clicks = audioClickTimes(aggregate);
    QVERIFY2(flashes.size() >= 25, qPrintable(QStringLiteral("only %1 video markers")
                                                  .arg(flashes.size())));
    QVERIFY2(clicks.size() >= 25, qPrintable(QStringLiteral("only %1 audio markers")
                                                 .arg(clicks.size())));

    const int markers = qMin(flashes.size(), clicks.size());
    double firstOffset = 0.0;
    for (int i = 0; i < markers; ++i) {
        const double offset = flashes.at(i) - clicks.at(i);
        if (i == 0) {
            firstOffset = offset;
            QVERIFY2(std::abs(offset) < kPtsTolerance,
                     qPrintable(QStringLiteral("audio/video offset at the first marker: %1 s")
                                    .arg(offset)));
        } else {
            // The symptom of per-chunk re-encoding was audio drifting away from
            // the video over the cast. It must stay constant.
            QVERIFY2(std::abs(offset - firstOffset) < kDriftTolerance,
                     qPrintable(QStringLiteral("audio/video drift at marker %1: %2 s")
                                    .arg(i).arg(offset - firstOffset)));
        }
        QVERIFY2(std::abs(flashes.at(i) - (1.0 + kMarkerPeriod * i)) < kPtsTolerance,
                 qPrintable(QStringLiteral("marker %1 is at %2 s, expected %3 s")
                                .arg(i).arg(flashes.at(i))
                                .arg(1.0 + kMarkerPeriod * i)));
    }
}

void TestCastServer::seekingRestartsTheEncoder()
{
    // A backward seek to an evicted segment restarts the encoder there.
    const int count = m_playlistNames.size();
    QVERIFY(count > 40);
    const int backward = 10;
    int status = 0;
    QByteArray data = fetch(segmentUrl(backward), &status);
    QCOMPARE(status, 200);
    bool ok = true;
    QHash<int, int> continuity;
    SegmentPts pts = parseSegment(data, &continuity, &ok);
    QVERIFY2(std::abs(pts.video - backward * kSegmentSeconds) < kPtsTolerance,
             qPrintable(QStringLiteral("backward seek starts at %1 s, expected %2 s")
                            .arg(pts.video).arg(backward * kSegmentSeconds)));

    // A far forward seek must not encode everything in between.
    const int forward = count - 5;
    data = fetch(segmentUrl(forward), &status);
    QCOMPARE(status, 200);
    ok = true;
    continuity.clear();
    pts = parseSegment(data, &continuity, &ok);
    QVERIFY2(std::abs(pts.video - forward * kSegmentSeconds) < kPtsTolerance,
             qPrintable(QStringLiteral("forward seek starts at %1 s, expected %2 s")
                            .arg(pts.video).arg(forward * kSegmentSeconds)));

    // The segments right after the seek are produced by the same encoder run.
    data = fetch(segmentUrl(forward + 1), &status);
    QCOMPARE(status, 200);
    continuity.clear();
    pts = parseSegment(data, &continuity, &ok);
    QVERIFY2(std::abs(pts.video - (forward + 1) * kSegmentSeconds) < kPtsTolerance,
             qPrintable(QStringLiteral("segment after the seek starts at %1 s")
                            .arg(pts.video)));
}

QString TestCastServer::findServerTempDir() const
{
    const QString prefix = QStringLiteral("luma-cast-%1-")
                               .arg(QCoreApplication::applicationPid());
    QStringList roots{QDir::tempPath(), QStringLiteral("/dev/shm")};
    roots.removeDuplicates();
    for (const QString &root : roots) {
        const QDir dir(root);
        const QStringList entries = dir.entryList({QStringLiteral("luma-cast-*")},
                                                  QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            if (entry.startsWith(prefix))
                return dir.filePath(entry);
        }
    }
    return {};
}

void TestCastServer::memoryWindowIsBounded()
{
    const QString directory = findServerTempDir();
    if (directory.isEmpty())
        QSKIP("the server's RAM segment directory is not visible");
    const QStringList names = QDir(directory).entryList({QStringLiteral("seg*.ts")},
                                                       QDir::Files);
    const int segmentFiles = names.size();
    const int count = m_playlistNames.size();
    QVERIFY2(segmentFiles > 0, qPrintable(QStringLiteral("no segments in %1").arg(directory)));
    // The window is about a minute of video, never the whole cast.
    QVERIFY2(segmentFiles < 60,
             qPrintable(QStringLiteral("the server kept %1 of %2 segments in %3: %4")
                            .arg(segmentFiles).arg(count).arg(directory)
                            .arg(names.mid(0, 3).join(QLatin1Char(','))
                                 + QStringLiteral(" ... ")
                                 + names.mid(names.size() - 3).join(QLatin1Char(',')))));
}

QTEST_GUILESS_MAIN(TestCastServer)
#include "tst_castserver.moc"
