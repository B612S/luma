#include <QCoreApplication>
#include <QTimer>
#include <QTextStream>
#include "cast/CastManager.hpp"

// Manual TV smoke test: not registered with ctest.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        QTextStream(stderr) << "usage: cast_tv_smoke <file> [tv-ip] [tv-port] [second-file]\n";
        return 2;
    }
    const QString file = QString::fromLocal8Bit(argv[1]);
    const QString ip = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("192.168.0.1");
    const quint16 port = argc > 3 ? quint16(QString::fromLocal8Bit(argv[3]).toUShort()) : 8009;

    CastManager manager;
    QTextStream out(stdout);
    // castTo() can report a refusal synchronously, before the event loop runs;
    // exit() is ignored at that point, so schedule the exit instead.
    auto quitWith = [](int code) {
        QTimer::singleShot(0, QCoreApplication::instance(), [code]() {
            QCoreApplication::exit(code);
        });
    };
    QObject::connect(&manager, &CastManager::castFailed, &app, [&out, quitWith](const QString &error) {
        out << "CAST FAILED: " << error << "\n";
        out.flush();
        quitWith(1);
    });
    QObject::connect(&manager, &CastManager::castBusy, &app, [&out, quitWith](const QString &deviceName) {
        out << "CAST BUSY: " << deviceName << " is already casting in another instance\n";
        out.flush();
        quitWith(2);
    });
    manager.castTo(ip, port, file, QStringLiteral("Luma TV smoke"), QStringLiteral("TV"));

    auto *poll = new QTimer(&app);
    QObject::connect(poll, &QTimer::timeout, &app, [&]() {
        out << "casting=" << manager.casting()
            << " playing=" << manager.isRemotePlaying()
            << " pos=" << manager.remotePosition()
            << " dur=" << manager.remoteDuration() << "\n";
        out.flush();
    });
    poll->start(1000);

    if (argc > 4) {
        const QString second = QString::fromLocal8Bit(argv[4]);
        QTimer::singleShot(15000, &app, [&, second]() {
            out << "SWITCH FILE " << second << "\n"; out.flush();
            manager.onMediaFileChanged(second);
        });
    }
    QTimer::singleShot(20000, &app, [&]() { out << "SEEK 60\n"; out.flush(); manager.seekTo(60); });
    QTimer::singleShot(30000, &app, [&]() { out << "SEEK 3600\n"; out.flush(); manager.seekTo(3600); });
    QTimer::singleShot(40000, &app, [&]() { out << "SEEK 5\n"; out.flush(); manager.seekTo(5); });
    QTimer::singleShot(50000, &app, [&]() {
        manager.stopCast();
        poll->stop();
        app.quit();
    });
    return app.exec();
}
