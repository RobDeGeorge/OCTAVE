// VolumeController is the single entry point for a UI volume percent: it
// clamps, stores the percent as settings' currentVolume, and tells every
// output the curved linear value. Pins the quadratic curve so the outputs
// can't drift apart.

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/settingsmanager.h"
#include "managers/volumecontroller.h"

class TstVolumeController : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp->path() + "/appdata").toUtf8());
    }

    void applyVolumeStoresAndNotifiesOnce()
    {
        SettingsManager settings;
        VolumeController volume(&settings);
        QSignalSpy applied(&volume, &VolumeController::volumeApplied);

        volume.applyVolume(75);

        QCOMPARE(applied.count(), 1);
        QCOMPARE(applied.first().at(0).toInt(), 75);
        QCOMPARE(applied.first().at(1).toFloat(), 0.5625f);
        QCOMPARE(settings.currentVolume(), 75);
    }

    void applyVolumeClamps_data()
    {
        QTest::addColumn<int>("in");
        QTest::addColumn<int>("percent");
        QTest::addColumn<float>("linear");
        QTest::newRow("below zero") << -5 << 0 << 0.0f;
        QTest::newRow("above 100") << 150 << 100 << 1.0f;
        QTest::newRow("zero") << 0 << 0 << 0.0f;
        QTest::newRow("full") << 100 << 100 << 1.0f;
    }

    void applyVolumeClamps()
    {
        QFETCH(int, in);
        QFETCH(int, percent);
        QFETCH(float, linear);
        SettingsManager settings;
        VolumeController volume(&settings);
        QSignalSpy applied(&volume, &VolumeController::volumeApplied);

        volume.applyVolume(in);

        QCOMPARE(applied.count(), 1);
        QCOMPARE(applied.first().at(0).toInt(), percent);
        QCOMPARE(applied.first().at(1).toFloat(), linear);
        QCOMPARE(settings.currentVolume(), percent);
    }

    void curveIsQuadraticAndBothHelpersAgree()
    {
        VolumeController volume(nullptr);   // no settings: the math still works
        for (int pct = 0; pct <= 100; ++pct) {
            const float expected = (pct / 100.0f) * (pct / 100.0f);
            QCOMPARE(VolumeController::toLinearStatic(float(pct)), expected);
            QCOMPARE(volume.toLinear(float(pct)), expected);
        }
        QCOMPARE(VolumeController::toLinearStatic(-20.0f), 0.0f);
        QCOMPARE(VolumeController::toLinearStatic(250.0f), 1.0f);
        QCOMPARE(VolumeController::toLinearStatic(50.0f), 0.25f);
    }

    void withoutSettingsStillNotifies()
    {
        VolumeController volume(nullptr);
        QSignalSpy applied(&volume, &VolumeController::volumeApplied);
        volume.applyVolume(40);
        QCOMPARE(applied.count(), 1);
        QCOMPARE(applied.first().at(1).toFloat(), 0.16f);
    }

    // ── the inverse curve (was six QML copies plus SettingsManager) ──

    void toPercentInvertsTheCurve()
    {
        for (int pct = 0; pct <= 100; ++pct)
            QCOMPARE(VolumeController::toPercentStatic(VolumeController::toLinearStatic(float(pct))), pct);
        QCOMPARE(VolumeController::toPercentStatic(0.25f), 50);
        QCOMPARE(VolumeController::toPercentStatic(0.1f), 32);   // the default startup volume
        QCOMPARE(VolumeController::toPercentStatic(-1.0f), 0);
        QCOMPARE(VolumeController::toPercentStatic(4.0f), 100);
    }

    void percentFollowsApplyVolume()
    {
        SettingsManager settings;
        VolumeController volume(&settings);
        QCOMPARE(volume.percent(), settings.currentVolume());
        QSignalSpy changed(&volume, &VolumeController::percentChanged);

        volume.applyVolume(60);
        QCOMPARE(volume.percent(), 60);
        QCOMPARE(changed.count(), 1);
        volume.applyVolume(60);   // unchanged: outputs re-told, no notify
        QCOMPARE(changed.count(), 1);
    }

    // ── mute rules ──────────────────────────────────────────────────────

    void toggleMuteFlipsAndNotifies()
    {
        VolumeController volume(nullptr);
        volume.applyVolume(40);
        QSignalSpy muted(&volume, &VolumeController::mutedChanged);

        volume.toggleMute();
        QVERIFY(volume.muted());
        QVERIFY(volume.effectivelyMuted());
        QCOMPARE(muted.count(), 1);
        QCOMPARE(volume.percent(), 40);   // muting keeps the level

        QTest::qWait(200);   // past the debounce
        volume.toggleMute();
        QVERIFY(!volume.muted());
        QVERIFY(!volume.effectivelyMuted());
        QCOMPARE(muted.count(), 2);
    }

    void toggleMuteIsDebounced()
    {
        // The ESP32 button bounces: a second press inside 150 ms is ignored
        VolumeController volume(nullptr);
        volume.applyVolume(40);
        volume.toggleMute();
        volume.toggleMute();
        QVERIFY(volume.muted());
    }

    void anyRaiseUnmutes()
    {
        // Decided 2026-10-22: knob, gesture, slider or command server, turning
        // the volume up while muted brings the sound back
        VolumeController volume(nullptr);
        volume.applyVolume(40);
        volume.setMuted(true);

        volume.applyVolume(30);   // lowering stays muted
        QVERIFY(volume.muted());
        volume.applyVolume(30);   // same level stays muted
        QVERIFY(volume.muted());
        volume.applyVolume(31);
        QVERIFY(!volume.muted());
    }

    void raiseAtMaxStillUnmutes()
    {
        // Knob or gesture "up" at 100 % clamps, but the user still turned it up
        VolumeController volume(nullptr);
        volume.applyVolume(100);
        volume.setMuted(true);
        volume.applyVolume(100);   // re-applying the level is not a raise
        QVERIFY(volume.muted());
        volume.applyVolume(105);   // currentVolume + step past the top
        QVERIFY(!volume.muted());
        QCOMPARE(volume.percent(), 100);
    }

    void sliderEchoDoesNotDoubleDispatch()
    {
        // The QML sliders set their value in percentChanged, which calls
        // applyVolume(same percent) again from inside the first call: the
        // outputs must still hear about each change once
        VolumeController volume(nullptr);
        connect(&volume, &VolumeController::percentChanged, &volume,
                [&volume](int p) { volume.applyVolume(p); });
        QSignalSpy applied(&volume, &VolumeController::volumeApplied);
        volume.applyVolume(55);
        QCOMPARE(applied.count(), 1);
    }

    void restoreAtZeroNotifiesMuteOnce()
    {
        SettingsManager settings;
        settings.save_start_volume(0.25f);
        VolumeController volume(&settings);
        volume.applyVolume(40);
        volume.setMuted(true);
        volume.applyVolume(0);
        QSignalSpy muted(&volume, &VolumeController::mutedChanged);
        volume.toggleMute();
        QCOMPARE(muted.count(), 1);
    }

    void unmuteAtZeroRestoresStartupVolume()
    {
        SettingsManager settings;
        settings.save_start_volume(0.25f);   // linear: 50 %
        VolumeController volume(&settings);
        volume.applyVolume(0);
        QVERIFY(volume.effectivelyMuted());
        QSignalSpy applied(&volume, &VolumeController::volumeApplied);

        volume.toggleMute();   // the mute icon at 0
        QCOMPARE(volume.percent(), 50);   // not 25: startUpVolume is linear
        QVERIFY(!volume.muted());
        QVERIFY(!volume.effectivelyMuted());
        QCOMPARE(applied.count(), 1);   // every output hears about it
        QCOMPARE(applied.first().at(0).toInt(), 50);
        QCOMPARE(settings.currentVolume(), 50);
    }

    void unmuteAtZeroWhileMutedAlsoRestores()
    {
        SettingsManager settings;
        settings.save_start_volume(0.25f);
        VolumeController volume(&settings);
        volume.applyVolume(40);
        volume.setMuted(true);
        volume.applyVolume(0);   // dragged to 0 while muted
        volume.toggleMute();
        QCOMPARE(volume.percent(), 50);
        QVERIFY(!volume.muted());
    }

    void effectivelyMutedAtZero()
    {
        VolumeController volume(nullptr);
        volume.applyVolume(10);
        QSignalSpy eff(&volume, &VolumeController::effectivelyMutedChanged);
        volume.applyVolume(0);
        QVERIFY(volume.effectivelyMuted());
        QVERIFY(!volume.muted());
        QCOMPARE(eff.count(), 1);
        volume.applyVolume(5);
        QVERIFY(!volume.effectivelyMuted());
        QCOMPARE(eff.count(), 2);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstVolumeController)
#include "tst_volumecontroller.moc"
