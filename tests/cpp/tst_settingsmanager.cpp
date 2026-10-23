// Settings must survive a power cut: a truncated settings file is restored
// from the last known-good backup instead of being replaced with defaults,
// and a failed write never damages the file on disk. Every value set through
// the setters survives a restart, and a file from an older build loads with
// its valid values kept and defaults filled in.

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "managers/settingsmanager.h"

class TstSettingsManager : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Every test gets its own config dir (SettingsManager::getAppDataDir()
        // reads these at construction)
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        qputenv("HOME", m_tmp->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_tmp->path() + "/config").toUtf8());
        qputenv("APPDATA", (m_tmp->path() + "/appdata").toUtf8());
    }

    void corruptSettingsRestoredFromBackup()
    {
        const QString file = SettingsManager::getAppDataDir() + "/settingsConfigure.json";
        {
            SettingsManager first;
            first.save_setting("startUpVolume", 0.37);
            first.flushPendingSave();
        }
        QVERIFY(QFile::exists(file));

        // Second boot reads the good file and backs it up
        { SettingsManager second; }
        QVERIFY(QFile::exists(file + ".bak"));

        // Power cut mid-write: file truncated
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("{\"startUpVol");
        }

        SettingsManager third;
        QCOMPARE(third.get_setting_with_default("startUpVolume", 0).toDouble(), 0.37);
        QVERIFY(QFile::exists(file + ".corrupt"));
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value("startUpVolume").toDouble(), 0.37);
    }

    void failedWriteLeavesExistingFileIntact()
    {
#ifndef Q_OS_UNIX
        QSKIP("uses a read-only directory to make the write fail");
#else
        if (geteuid() == 0)
            QSKIP("root ignores directory permissions");
        const QString dir = SettingsManager::getAppDataDir();
        const QString file = dir + "/settingsConfigure.json";
        SettingsManager manager;
        QFile before(file);
        QVERIFY(before.open(QIODevice::ReadOnly));
        const QByteArray original = before.readAll();
        before.close();

        // The write fails partway, as on a full disk: QSaveFile can't create
        // its temp file next to the target
        QVERIFY(::chmod(QFile::encodeName(dir).constData(), 0555) == 0);
        manager.save_setting("startUpVolume", 0.81);
        manager.flushPendingSave();
        ::chmod(QFile::encodeName(dir).constData(), 0755);

        QFile after(file);
        QVERIFY(after.open(QIODevice::ReadOnly));
        QCOMPARE(after.readAll(), original);
        const QStringList leftovers = QDir(dir).entryList({"*.json.*", "settingsConfigure.json?*"}, QDir::Files);
        QVERIFY2(leftovers.isEmpty() || leftovers == QStringList{"settingsConfigure.json.bak"},
                 qPrintable(leftovers.join(", ")));
#endif
    }

    void savedObdAdaptersAreAListStoredAsBefore()
    {
        // Pages read a list; the file keeps the compact JSON string it always had
        SettingsManager sm;
        QSignalSpy changed(&sm, &SettingsManager::obdSavedAdaptersChanged);
        QCOMPARE(sm.obdSavedAdapters(), QVariantList{});
        sm.add_obd_saved_adapter(QStringLiteral("NEXAS"), QStringLiteral("aa:bb:cc:dd:ee:ff"));
        sm.add_obd_saved_adapter(QString(), QStringLiteral("rfcomm://11:22:33:44:55:66/2"));
        QCOMPARE(changed.count(), 2);
        const QVariantList list = sm.property("obdSavedAdapters").toList();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value("mac").toString(), QStringLiteral("rfcomm://11:22:33:44:55:66/2"));
        QCOMPARE(list.at(0).toMap().value("name").toString(), QStringLiteral("rfcomm://11:22:33:44:55:66/2"));
        QCOMPARE(list.at(1).toMap(), (QVariantMap{{"name", "NEXAS"}, {"mac", "AA:BB:CC:DD:EE:FF"}}));
        QCOMPARE(sm.m_settings.value("obdSavedAdapters").toString(),
                 QStringLiteral(R"([{"mac":"RFCOMM://11:22:33:44:55:66/2","name":"RFCOMM://11:22:33:44:55:66/2"},)"
                                R"({"mac":"AA:BB:CC:DD:EE:FF","name":"NEXAS"}])").replace("RFCOMM", "rfcomm"));
        sm.remove_obd_saved_adapter(QStringLiteral("aa:bb:cc:dd:ee:ff"));
        QCOMPARE(sm.obdSavedAdapters().size(), 1);
    }

    void themesReachQmlAsObjectsStoredAsBefore()
    {
        // Album Art Capture palette: QML gets the object, the file keeps the string
        SettingsManager sm;
        QSignalSpy art(&sm, &SettingsManager::albumArtColorsChanged);
        QCOMPARE(sm.albumArtColors(), QVariantMap{});
        const QString json = QStringLiteral(R"({"accent":"#ff8800","base":"#101010","text":{"primary":"#ffffff"}})");
        sm.set_album_art_colors(json);
        QCOMPARE(art.count(), 1);
        QCOMPARE(sm.m_settings.value("albumArtColors").toString(), json);
        const QVariantMap colors = sm.property("albumArtColors").toMap();
        QCOMPARE(colors.value("accent").toString(), QStringLiteral("#ff8800"));
        QCOMPARE(colors.value("text").toMap().value("primary").toString(), QStringLiteral("#ffffff"));

        // Custom themes: saved from an object, read back whole, in one map
        QSignalSpy themes(&sm, &SettingsManager::customThemesChanged);
        sm.save_custom_theme(QStringLiteral("Dusk"), colors);
        QCOMPARE(themes.count(), 1);
        QCOMPARE(sm.customThemes(), QVariantList{QStringLiteral("Dusk")});
        const QVariantMap data = sm.property("customThemeData").toMap();
        QCOMPARE(data.keys(), QStringList{QStringLiteral("Dusk")});
        QCOMPARE(data.value("Dusk").toMap(), colors);
        QCOMPARE(sm.m_settings.value("customThemes").toObject().value("Dusk").toObject(),
                 QJsonDocument::fromJson(json.toUtf8()).object());
        sm.delete_custom_theme(QStringLiteral("Dusk"));
        QVERIFY(sm.customThemeData().isEmpty());
    }

    void everySettingSurvivesARestart()
    {
        QJsonObject saved;
        {
            SettingsManager first;
            first.setCurrentVolume(63);
            first.save_start_volume(0.42f);
            first.save_theme_setting(QStringLiteral("Midnight"));
            first.save_ui_scale(1.25f);
            first.save_clock_format(false);
            first.save_clock_show_seconds(true);
            first.save_media_folder(m_tmp->path() + "/Music");
            first.save_obd_bluetooth_port(QStringLiteral("AA:BB:CC:DD:EE:FF"));
            first.save_obd_saved_adapters(QStringLiteral("[{\"mac\":\"AA:BB:CC:DD:EE:FF\",\"name\":\"NEXAS\"}]"));
            first.save_home_obd_parameters({QStringLiteral("RPM"), QStringLiteral("SPEED")});
            first.save_obd_parameter_enabled(QStringLiteral("COOLANT_TEMP"), false);
            first.save_custom_theme(QStringLiteral("Mine"), QVariantMap{{"accent", "#ff0000"}});
            first.save_gesture_action(QStringLiteral("up"), QStringLiteral("volume_up"));
            first.save_scrcpy_bit_rate_wifi(6);
            first.save_gauge_temp_unit(QStringLiteral("F"));
            first.save_gauge_speed_unit(QStringLiteral("km/h"));
            first.save_settings_section_visibility(QStringLiteral("obd"), false);
            first.save_playback_state(QStringLiteral("a.mp3"), 1234, QStringLiteral("Road"));
            first.flushPendingSave();
            saved = first.m_settings;

            // The setters really changed things, so the comparison below means something
            int changed = 0;
            for (auto it = saved.begin(); it != saved.end(); ++it)
                changed += first.m_defaultSettings.value(it.key()) != it.value();
            QVERIFY2(changed >= 12, qPrintable(QString::number(changed)));
        }

        SettingsManager second;
        QStringList differ;
        for (auto it = saved.begin(); it != saved.end(); ++it) {
            if (second.m_settings.value(it.key()) != it.value())
                differ << it.key();
        }
        QVERIFY2(differ.isEmpty(), qPrintable(differ.join(", ")));
        QCOMPARE(second.m_settings.size(), saved.size());
        // The live volume is not saved: a restart starts at the start-up
        // volume (linear 0.42 -> sqrt -> 65 %), not the last volume (63)
        QCOMPARE(second.currentVolume(), 65);
    }

    void olderFileLoadsWithDefaultsFilledIn()
    {
        const QString dir = SettingsManager::getAppDataDir();
        QVERIFY(QDir().mkpath(dir));
        // An older build: few keys, one with the wrong type, one this build doesn't know
        const QJsonObject old{{"startUpVolume", 0.3}, {"themeSetting", "Ocean"},
                              {"showClock", "yes"}, {"someFutureKey", "kept"}};
        {
            QFile f(dir + "/settingsConfigure.json");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(old).toJson());
        }

        SettingsManager mgr;
        QCOMPARE(mgr.m_settings.value("startUpVolume").toDouble(), 0.3);
        QCOMPARE(mgr.m_settings.value("themeSetting").toString(), QStringLiteral("Ocean"));
        QCOMPARE(mgr.m_settings.value("showClock"), mgr.m_defaultSettings.value("showClock"));   // wrong type: default
        QCOMPARE(mgr.m_settings.value("someFutureKey").toString(), QStringLiteral("kept"));
        QStringList missing;
        for (auto it = mgr.m_defaultSettings.begin(); it != mgr.m_defaultSettings.end(); ++it) {
            if (!mgr.m_settings.contains(it.key()))
                missing << it.key();
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(", ")));
        QVERIFY(!QFile::exists(dir + "/settingsConfigure.json.corrupt"));
    }

    // ── sensor display + OBD card style: typed, notifying (open-backend #11) ──
    // SensorMenu used to read these once and is cached by the page stack, so
    // a change in Settings showed only after a restart. They keep their old
    // on-disk key names so existing files load.

    void sensorDisplaySettingsNotifyAndPersist()
    {
        {
            SettingsManager first;
            QCOMPARE(first.sensorDecimalPlaces(), 1);
            QCOMPARE(first.sensorTempUnit(), QStringLiteral("F"));
            QCOMPARE(first.sensorAltitudeUnit(), QStringLiteral("m"));
            QCOMPARE(first.sensorCards().value("Pitch").toBool(), true);
            QCOMPARE(first.obdCardStyleCircular(), false);

            QSignalSpy decimals(&first, &SettingsManager::sensorDecimalPlacesChanged);
            QSignalSpy temp(&first, &SettingsManager::sensorTempUnitChanged);
            QSignalSpy alt(&first, &SettingsManager::sensorAltitudeUnitChanged);
            QSignalSpy cards(&first, &SettingsManager::sensorCardsChanged);
            QSignalSpy circ(&first, &SettingsManager::obdCardStyleCircularChanged);

            first.save_sensor_decimal_places(3);
            first.save_sensor_temp_unit(QStringLiteral("C"));
            first.save_sensor_altitude_unit(QStringLiteral("ft"));
            first.save_sensor_card_visible(QStringLiteral("Roll"), false);
            first.save_obd_card_style_circular(true);
            QCOMPARE(decimals.count(), 1);
            QCOMPARE(temp.count(), 1);
            QCOMPARE(alt.count(), 1);
            QCOMPARE(cards.count(), 1);
            QCOMPARE(circ.count(), 1);
            QCOMPARE(first.sensorCards().value("Roll").toBool(), false);
            QCOMPARE(first.sensorCards().value("Pitch").toBool(), true);

            // Same names on disk as before
            QCOMPARE(first.m_settings.value("sensorDecimalPlaces").toInt(), 3);
            QCOMPARE(first.m_settings.value("sensorTempUnit").toString(), QStringLiteral("C"));
            QCOMPARE(first.m_settings.value("sensorAltitudeUnit").toString(), QStringLiteral("ft"));
            QCOMPARE(first.m_settings.value("sensorShowRoll").toBool(), false);
            QCOMPARE(first.m_settings.value("obdCardStyleCircular").toBool(), true);
            first.flushPendingSave();
        }
        SettingsManager second;
        QCOMPARE(second.sensorDecimalPlaces(), 3);
        QCOMPARE(second.sensorTempUnit(), QStringLiteral("C"));
        QCOMPARE(second.sensorAltitudeUnit(), QStringLiteral("ft"));
        QCOMPARE(second.sensorCards().value("Roll").toBool(), false);
        QCOMPARE(second.obdCardStyleCircular(), true);
    }

    void sensorDisplaySettingsRejectBadValues()
    {
        SettingsManager mgr;
        QSignalSpy temp(&mgr, &SettingsManager::sensorTempUnitChanged);
        QSignalSpy cards(&mgr, &SettingsManager::sensorCardsChanged);
        mgr.save_sensor_decimal_places(9);
        QCOMPARE(mgr.sensorDecimalPlaces(), 4);   // the settings slider's range
        mgr.save_sensor_decimal_places(-2);
        QCOMPARE(mgr.sensorDecimalPlaces(), 0);
        mgr.save_sensor_temp_unit(QStringLiteral("K"));
        QCOMPARE(mgr.sensorTempUnit(), QStringLiteral("F"));
        QCOMPARE(temp.count(), 0);
        mgr.save_sensor_altitude_unit(QStringLiteral("yards"));
        QCOMPARE(mgr.sensorAltitudeUnit(), QStringLiteral("m"));
        mgr.save_sensor_card_visible(QStringLiteral("Warp"), false);
        QVERIFY(!mgr.sensorCards().contains("Warp"));
        QCOMPARE(cards.count(), 0);
        QCOMPARE(mgr.sensorCards().size(), 6);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TstSettingsManager)
#include "tst_settingsmanager.moc"
