// The IMU and gesture managers follow their settings live, like the ESP32
// knob does: a page or widget only saves, the manager reacts. Before this the
// dashboard's gesture toggle saved without stopping the sensor, and the saved
// IMU rate was applied only when the Sensors page was first built.
//
// No sensor hardware is needed: these assert on manager state, not on the
// I2C worker (which fails to open and retries in the background).

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "managers/berryimumanager.h"
#include "managers/gesturemanager.h"
#include "managers/settingsmanager.h"

class TstSensorSettings : public QObject
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

    // ── IMU ─────────────────────────────────────────────────────────────

    void imuReadsSavedStateAtConnect()
    {
        SettingsManager settings;
        settings.save_imu_enabled(false);
        settings.save_sensor_emit_rate(30);
        BerryIMUManager imu;
        imu.connect_settings_manager(&settings);
        QVERIFY(!imu.isEnabled());
        QCOMPARE(imu.emitRate(), 30);   // not 60 until the Sensors page opens
    }

    void imuFollowsEnabledSetting()
    {
        SettingsManager settings;
        BerryIMUManager imu;
        imu.connect_settings_manager(&settings);
        QVERIFY(imu.isEnabled());

        settings.save_imu_enabled(false);
        QVERIFY(!imu.isEnabled());
        settings.save_imu_enabled(true);
        QVERIFY(imu.isEnabled());
    }

    void imuFollowsEmitRateSetting()
    {
        SettingsManager settings;
        BerryIMUManager imu;
        imu.connect_settings_manager(&settings);
        QCOMPARE(imu.emitRate(), 60);   // the default

        settings.save_sensor_emit_rate(20);
        QCOMPARE(settings.sensorEmitRate(), 20);
        QCOMPARE(imu.emitRate(), 20);
        settings.save_sensor_emit_rate(500);   // clamped to the manager's range
        QCOMPARE(settings.sensorEmitRate(), 120);
        QCOMPARE(imu.emitRate(), 120);
    }

    void imuSetEnabledDoesNotSave()
    {
        // One writer: the setting. setEnabled is a runtime switch.
        SettingsManager settings;
        BerryIMUManager imu;
        imu.connect_settings_manager(&settings);
        QSignalSpy saved(&settings, &SettingsManager::imuEnabledChanged);
        imu.setEnabled(false);
        QVERIFY(!imu.isEnabled());
        QVERIFY(settings.imuEnabled());
        QCOMPARE(saved.count(), 0);
    }

    // ── gesture sensor ──────────────────────────────────────────────────

    void imuRunsFullRateWhileAnyConsumerWantsIt()
    {
        // Pages register themselves (ImuDemand) instead of Main.qml keeping
        // a list of page names
        BerryIMUManager imu;
        QVERIFY(!imu.m_active);   // idle until a page asks
        imu.setDemand("mainMenu", false);
        QVERIFY(!imu.m_active);
        imu.setDemand("sensorMenu", true);
        imu.setDemand("obdMenu", true);
        QVERIFY(imu.m_active);
        imu.setDemand("sensorMenu", false);
        QVERIFY(imu.m_active);   // the OBD page still shows a compass
        imu.setDemand("obdMenu", false);
        QVERIFY(!imu.m_active);
    }

    void gestureReadsSavedStateAtConnect()
    {
        SettingsManager settings;
        settings.save_gesture_sensor_enabled(false);
        settings.save_gesture_cooldown(800);
        GestureManager gesture;
        gesture.connect_settings_manager(&settings);
        QVERIFY(!gesture.isEnabled());
        QCOMPARE(gesture.cooldownMs(), 800);
    }

    void gestureFollowsSettings()
    {
        // The dashboard's Accessories widget only calls
        // save_gesture_sensor_enabled: that alone must switch the sensor
        SettingsManager settings;
        GestureManager gesture;
        gesture.connect_settings_manager(&settings);
        QVERIFY(gesture.isEnabled());

        settings.save_gesture_sensor_enabled(false);
        QVERIFY(!gesture.isEnabled());
        settings.save_gesture_sensor_enabled(true);
        QVERIFY(gesture.isEnabled());

        settings.save_gesture_cooldown(450);
        QCOMPARE(gesture.cooldownMs(), 450);
    }

    void gestureSetEnabledDoesNotSave()
    {
        SettingsManager settings;
        GestureManager gesture;
        gesture.connect_settings_manager(&settings);
        QSignalSpy saved(&settings, &SettingsManager::gestureSensorEnabledChanged);
        gesture.setEnabled(false);
        QVERIFY(!gesture.isEnabled());
        QVERIFY(settings.gestureSensorEnabled());
        QCOMPARE(saved.count(), 0);
    }

    void gestureVocabularyIsOneTable()
    {
        // The settings page lists GestureManager's gestures and actions (they
        // were a QML copy); every gesture has a default action, every default
        // action is offered, and the settings default is the same mapping
        GestureManager gesture;
        const QStringList gestures = gesture.property("gestures").toStringList();
        QCOMPARE(gestures.size(), 9);
        QVERIFY(gestures.contains("WAVE"));
        QStringList actionKeys;
        for (const QVariant &a : gesture.property("actions").toList())
            actionKeys << a.toMap().value("key").toString();
        QVERIFY(actionKeys.contains("none"));
        const QVariantMap defaults = GestureVocabulary::defaultMapping();
        QCOMPARE(defaults.size(), gestures.size());
        for (const QString &g : gestures) {
            QVERIFY2(defaults.contains(g), qPrintable(g));
            QVERIFY2(actionKeys.contains(defaults.value(g).toString()), qPrintable(g));
        }
        SettingsManager settings;
        const QVariantMap saved = settings.get_gesture_mapping_dict();
        for (const QString &g : gestures)
            QCOMPARE(saved.value(g), defaults.value(g));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_GUILESS_MAIN(TstSensorSettings)
#include "tst_sensorsettings.moc"
