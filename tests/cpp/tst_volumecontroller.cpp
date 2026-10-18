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

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstVolumeController)
#include "tst_volumecontroller.moc"
