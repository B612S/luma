// Manual cast streaming test. Not part of ctest: build with
//   cmake -B build -S . -DLUMA_BUILD_CAST_TEST=ON
// and run:
//   ./build/bin/cast_stream_test /path/to/video
//   ./build/bin/cast_stream_test --serve /path/to/video
//   ./build/bin/cast_stream_test --client http://192.168.1.10:PORT/stream/.../playlist.m3u8
//
// The local and --client modes use FFmpeg as a remote HLS player, so they
// exercise the same HTTP/segment path a Chromecast uses, including seeking
// (which restarts the server-side stream at the requested position).

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <QFileInfo>
#include <QUrl>
#include <QDebug>
#include <functional>
#include <cstdio>

#include "cast/CastMemoryHlsServer.hpp"

namespace {
bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        if (condition()) return true;
        QThread::msleep(50);
    }
    return false;
}

int runPlayer(const QString &url, int seconds, QString *log)
{
    QProcess player;
    player.setProcessChannelMode(QProcess::MergedChannels);
    player.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                  QStringLiteral("warning"), QStringLiteral("-i"), url,
                  QStringLiteral("-t"), QString::number(seconds),
                  QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    if (!player.waitForStarted(5000)) {
        *log = QStringLiteral("ffmpeg did not start");
        return -1;
    }
    while (player.state() != QProcess::NotRunning) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
    *log = QString::fromUtf8(player.readAll());
    return player.exitStatus() == QProcess::NormalExit ? player.exitCode() : -1;
}

bool checkLog(const QString &log)
{
    const QString lower = log.toLower();
    return !lower.contains(QStringLiteral("invalid data"))
        && !lower.contains(QStringLiteral("corrupt"))
        && !lower.contains(QStringLiteral("no such file"))
        && !lower.contains(QStringLiteral("connection refused"));
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Manual Chromecast/HLS streaming test (not a ctest)"));
    parser.addHelpOption();
    const QCommandLineOption serveOption(
        QStringLiteral("serve"),
        QStringLiteral("Run only the HLS server and print its LAN URL."));
    const QCommandLineOption clientOption(
        QStringLiteral("client"),
        QStringLiteral("Run only as a remote client against an existing HLS URL."),
        QStringLiteral("url"));
    const QCommandLineOption secondsOption(
        QStringLiteral("seconds"),
        QStringLiteral("How many seconds to decode (default: 20)."),
        QStringLiteral("seconds"), QStringLiteral("20"));

    const QCommandLineOption targetOption(
        QStringLiteral("target"),
        QStringLiteral("Target IP used to choose the LAN interface (default: 192.168.0.1)."),
        QStringLiteral("ip"), QStringLiteral("192.168.0.1"));
    parser.addOption(serveOption);
    parser.addOption(clientOption);
    parser.addOption(secondsOption);
    parser.addOption(targetOption);
    parser.addPositionalArgument(QStringLiteral("file"),
                                 QStringLiteral("Video file for the server/local mode."));
    parser.process(app);

    const int seconds = qMax(1, parser.value(secondsOption).toInt());
    if (parser.isSet(clientOption)) {
        QString log;
        const int rc = runPlayer(parser.value(clientOption), seconds, &log);
        std::fprintf(stderr, "client URL: %s\n%s", qPrintable(parser.value(clientOption)), qPrintable(log));
        if (rc != 0 || !checkLog(log)) {
            std::fprintf(stderr, "FAIL: remote HLS playback failed (exit %d)\n", rc);
            return 1;
        }
        std::fprintf(stderr, "PASS: remote HLS playback\n");
        return 0;
    }

    const QStringList files = parser.positionalArguments();
    if (files.isEmpty() || !QFileInfo::exists(files.first())) {
        std::fprintf(stderr, "usage: cast_stream_test [options] <video-file>\n");
        return 2;
    }

    CastMemoryHlsServer server;
    if (!server.start(files.first(), parser.value(targetOption))) {
        std::fprintf(stderr, "FAIL: could not start the cast server\n");
        return 1;
    }
    if (!waitFor([&]() { return server.isPlaylistReady(); }, 15000)) {
        std::fprintf(stderr, "FAIL: HLS playlist did not become ready\n");
        server.stop();
        return 1;
    }

    const QString url = server.url();
    std::fprintf(stderr, "HLS URL: %s\n", qPrintable(url));
    std::fprintf(stderr, "local IP: %s\n", qPrintable(QUrl(url).host()));

    if (parser.isSet(serveOption)) {
        std::fprintf(stderr, "Serving. Press Ctrl+C to stop.\n");
        const int rc = app.exec();
        server.stop();
        return rc;
    }

    QString log;
    int rc = runPlayer(url, seconds, &log);
    std::fprintf(stderr, "decode %ds: exit=%d\n%s", seconds, rc, qPrintable(log));
    if (rc != 0 || !checkLog(log)) {
        server.stop();
        std::fprintf(stderr, "FAIL: local HLS playback failed\n");
        return 1;
    }


    server.stop();
    std::fprintf(stderr, "PASS: local HLS playback\n");
    return 0;
}
