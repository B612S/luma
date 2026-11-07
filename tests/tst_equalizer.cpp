#include <QTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include "ConfigManager.hpp"
#include "Equalizer.hpp"
#include "TestEnvironment.h"

class TestEqualizer : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void defaults();
    void filter();
    void bandLayouts();
    void builtinPresets();
    void customPresets();
    void applySignal();
    void reloadsExternalChanges();
    void persists();

private:
    static QString configHome()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    }
    static QString lumaDir() { return configHome() + "/luma"; }
};

void TestEqualizer::init()
{
    QDir(lumaDir()).removeRecursively();
}

void TestEqualizer::defaults()
{
    ConfigManager config;
    Equalizer equalizer(&config);

    QVERIFY(!equalizer.enabled());
    QCOMPARE(equalizer.bands(), 10);
    QCOMPARE(equalizer.preamp(), 0.0);
    QCOMPARE(equalizer.gains().size(), 10);
    QCOMPARE(equalizer.frequencies().size(), 10);
    QCOMPARE(equalizer.frequencies().first().toDouble(), 31.5);
    QCOMPARE(equalizer.frequencies().last().toDouble(), 16000.0);
    QCOMPARE(equalizer.presetId(), QStringLiteral("builtin:flat"));
    QVERIFY(equalizer.audioFilter().isEmpty());
    QCOMPARE(equalizer.customPresets().size(), 0);
}

void TestEqualizer::filter()
{
    ConfigManager config;
    Equalizer equalizer(&config);

    equalizer.setEnabled(true);
    QVERIFY(equalizer.audioFilter().isEmpty());

    equalizer.setPreamp(3.0);
    QVERIFY(equalizer.audioFilter().startsWith(QStringLiteral("lavfi=[")));
    QVERIFY(equalizer.audioFilter().contains(QStringLiteral("volume=volume=1.4125")));
    // The Chromecast encoder gets the bare graph, mpv the lavfi wrapper.
    QVERIFY(equalizer.ffmpegFilter().startsWith(QStringLiteral("volume=volume=1.4125")));
    QVERIFY(!equalizer.ffmpegFilter().contains(QStringLiteral("lavfi=[")));

    equalizer.setGain(0, 5.0);
    equalizer.setGain(5, -3.5);
    const QString filter = equalizer.audioFilter();
    QVERIFY(filter.contains(QStringLiteral("equalizer=f=31.5:width_type=o:width=1:g=5")));
    QVERIFY(filter.contains(QStringLiteral("equalizer=f=1000:width_type=o:width=1:g=-3.5")));
    QCOMPARE(filter, QStringLiteral("lavfi=[") + equalizer.ffmpegFilter() + QStringLiteral("]"));

    equalizer.setEnabled(false);
    QVERIFY(equalizer.audioFilter().isEmpty());
    QVERIFY(equalizer.ffmpegFilter().isEmpty());
}

void TestEqualizer::bandLayouts()
{
    ConfigManager config;
    Equalizer equalizer(&config);
    equalizer.setGain(0, 6.0);

    equalizer.setBands(20);
    QCOMPARE(equalizer.bands(), 20);
    QCOMPARE(equalizer.gains().size(), 20);
    QCOMPARE(equalizer.frequencies().size(), 20);
    // 31.5 Hz exists in both layouts, so the curve point survives the switch.
    QCOMPARE(equalizer.gains().at(1).toDouble(), 6.0);

    equalizer.setEnabled(true);
    QVERIFY(equalizer.audioFilter().contains(QStringLiteral("width=0.5")));

    // Switching back restores the original octave points.
    equalizer.setBands(10);
    QCOMPARE(equalizer.gains().size(), 10);
    QCOMPARE(equalizer.gains().at(0).toDouble(), 6.0);
}

void TestEqualizer::builtinPresets()
{
    ConfigManager config;
    Equalizer equalizer(&config);

    QVERIFY(equalizer.builtinPresets().contains(QStringLiteral("rock")));

    // Builtin presets include the preamp: selecting one clears the previous gain.
    equalizer.setPreamp(4.0);
    equalizer.applyBuiltinPreset("rock");
    QCOMPARE(equalizer.preamp(), 0.0);
    QCOMPARE(equalizer.presetId(), QStringLiteral("builtin:rock"));
    const QVariantList rock = equalizer.gains();
    QCOMPARE(rock.size(), 10);
    QCOMPARE(rock.at(0).toDouble(), 5.0);
    QCOMPARE(rock.at(5).toDouble(), -1.0);
    QCOMPARE(rock.at(9).toDouble(), 5.0);

    // Editing the curve or the preamp deselects the preset.
    equalizer.setGain(3, 2.0);
    QCOMPARE(equalizer.presetId(), QString());

    equalizer.applyBuiltinPreset("rock");
    equalizer.setPreamp(-1.5);
    QCOMPARE(equalizer.presetId(), QString());

    // A builtin preset is resampled for the 20-band layout.
    equalizer.setBands(20);
    equalizer.applyBuiltinPreset("bass");
    QCOMPARE(equalizer.bands(), 20);
    QCOMPARE(equalizer.gains().size(), 20);
    QCOMPARE(equalizer.gains().first().toDouble(), 7.0);
    QCOMPARE(equalizer.gains().last().toDouble(), 0.0);

    // Unknown presets are ignored.
    equalizer.applyBuiltinPreset("does-not-exist");
    QCOMPARE(equalizer.presetId(), QStringLiteral("builtin:bass"));
}

void TestEqualizer::customPresets()
{
    ConfigManager config;
    Equalizer equalizer(&config);
    equalizer.setGain(0, 4.0);
    equalizer.setPreamp(-2.0);

    equalizer.saveCustomPreset("  My curve  ");
    QCOMPARE(equalizer.presetId(), QStringLiteral("custom:My curve"));
    QCOMPARE(equalizer.customPresets().size(), 1);
    QCOMPARE(equalizer.customPresets().first().toMap().value("name").toString(),
             QStringLiteral("My curve"));

    // Changing the curve does not touch the saved preset.
    equalizer.setGain(0, 0.0);
    equalizer.setPreamp(0.0);
    QCOMPARE(equalizer.presetId(), QString());

    equalizer.applyCustomPreset("My curve");
    QCOMPARE(equalizer.presetId(), QStringLiteral("custom:My curve"));
    QCOMPARE(equalizer.gains().at(0).toDouble(), 4.0);
    QCOMPARE(equalizer.preamp(), -2.0);

    // Reset removes only the user presets; the builtin list stays.
    equalizer.setBands(20);
    QCOMPARE(equalizer.customPresets().size(), 1);
    equalizer.resetCustomPresets();
    QCOMPARE(equalizer.customPresets().size(), 0);
    QVERIFY(equalizer.builtinPresets().contains(QStringLiteral("flat")));
}

void TestEqualizer::applySignal()
{
    ConfigManager config;
    Equalizer equalizer(&config);
    QSignalSpy spy(&equalizer, &Equalizer::filterRequested);

    // A disabled, flat equalizer has nothing to remove: mpv's af chain from
    // mpv.conf must stay untouched on startup.
    equalizer.applyNow();
    QCOMPARE(spy.count(), 0);

    equalizer.setEnabled(true);
    equalizer.setGain(0, 3.0);
    equalizer.applyNow();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.last().at(0).toString().isEmpty());

    // Disabling removes the filter that was applied.
    equalizer.setEnabled(false);
    equalizer.applyNow();
    QCOMPARE(spy.count(), 2);
    QVERIFY(spy.last().at(0).toString().isEmpty());

    // Nothing to do afterwards.
    equalizer.applyNow();
    QCOMPARE(spy.count(), 2);
}

void TestEqualizer::reloadsExternalChanges()
{
    // luma.conf must exist before the reading ConfigManager starts its
    // watcher, otherwise there is no file to observe.
    {
        ConfigManager seed;
        seed.setValue("ui/volume", 80);
    }

    ConfigManager config;
    Equalizer equalizer(&config);
    QVERIFY(!equalizer.enabled());

    // Another Luma instance writes new equalizer settings.
    {
        ConfigManager writer;
        writer.setValue("equalizer/enabled", true);
        writer.setValue("equalizer/preamp", 4.0);
        writer.setValue("equalizer/gains10",
                        "0.0,1.0,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0");
    }

    QTRY_VERIFY_WITH_TIMEOUT(equalizer.enabled(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(equalizer.preamp(), 4.0, 2000);
    QCOMPARE(equalizer.gains().at(1).toDouble(), 1.0);
    QVERIFY(!equalizer.audioFilter().isEmpty());
}

void TestEqualizer::persists()
{
    ConfigManager config;
    {
        Equalizer equalizer(&config);
        equalizer.setEnabled(true);
        equalizer.setBands(20);
        equalizer.setPreamp(1.5);
        equalizer.setGain(3, -4.5);
        equalizer.saveCustomPreset("Saved");
    }

    Equalizer reloaded(&config);
    QVERIFY(reloaded.enabled());
    QCOMPARE(reloaded.bands(), 20);
    QCOMPARE(reloaded.preamp(), 1.5);
    QCOMPARE(reloaded.gains().at(3).toDouble(), -4.5);
    QCOMPARE(reloaded.customPresets().size(), 1);
    QCOMPARE(reloaded.customPresets().first().toMap().value("name").toString(),
             QStringLiteral("Saved"));
    QCOMPARE(reloaded.presetId(), QStringLiteral("custom:Saved"));
}

int main(int argc, char *argv[])
{
    const QString envRoot = initTestEnvironment();
    QCoreApplication app(argc, argv);

    TestEqualizer test;
    const int status = QTest::qExec(&test, argc, argv);

    cleanupTestEnvironment(envRoot);
    return status;
}

#include "tst_equalizer.moc"
