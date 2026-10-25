// Settings must survive a power cut: a truncated settings file is restored
// from the last known-good backup instead of being replaced with defaults,
// and a failed write never damages the file on disk. Every value set through
// the setters survives a restart, and a file from an older build loads with
// its valid values kept and defaults filled in.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <cmath>

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

    void serviceKeysAreFileOnly()
    {
        // A client of the service must not turn it on or pick what it
        // serves for the next start: only an edit of the file does
        SettingsManager sm;
        sm.save_setting("serviceWebRoot", "/");
        sm.save_setting("serviceEnabled", true);
        QCOMPARE(sm.fileOnlySetting("serviceWebRoot").toString(), QString());
        // ...and nothing reads them through the generic getters either
        QVERIFY(!sm.get_setting("serviceTokens").isValid());
        QCOMPARE(sm.get_setting_with_default("serviceLoopbackOpen", "x").toString(), QStringLiteral("x"));
        QVERIFY(!sm.fileOnlySetting("serviceEnabled").toBool());
        sm.save_setting("someOtherKey", 5);
        QCOMPARE(sm.get_setting("someOtherKey").toInt(), 5);

        // A reset keeps what the file says for them
        QFile f(SettingsManager::getAppDataDir() + "/settingsConfigure.json");
        sm.flushPendingSave();
        QVERIFY(f.open(QIODevice::ReadOnly));
        QJsonObject file = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        file.insert("serviceEnabled", true);
        file.insert("serviceWebRoot", "/srv/ui");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(file).toJson());
        f.close();
        SettingsManager fresh;
        fresh.reset_to_defaults();
        QVERIFY(fresh.fileOnlySetting("serviceEnabled").toBool());
        QCOMPARE(fresh.fileOnlySetting("serviceWebRoot").toString(), QStringLiteral("/srv/ui"));
        QCOMPARE(fresh.get_setting("someOtherKey"), QVariant());   // the rest is reset
    }

    void applySettingsGoesThroughTheSaveSlots()
    {
        SettingsManager sm;
        QSignalSpy grid(&sm, &SettingsManager::backgroundGridChanged);
        const QVariantMap result = sm.apply_settings({
            {"roundedAlbumArt", false},          // registry slot
            {"backgroundGrid", "Normal"},        // derived: save_background_grid
            {"show3DButtonTilt", true},          // derived through the digits
            {"backgroundBlurRadius", 42.6},      // int slot: rounded
            {"sideCardOpacity", 0.35},
            {"vinylRecordMode", "yes"},          // not a bool
            {"backgroundOverlayOpacity", "50"},  // not a number
            {"noSuchSetting", 1},                // no setter
            {"serviceEnabled", true},            // file-only
            {"serviceWebRoot", "/"}});
        QCOMPARE(result.value("applied").toStringList(),
                 (QStringList{"backgroundBlurRadius", "backgroundGrid", "roundedAlbumArt", "show3DButtonTilt", "sideCardOpacity"}));
        const QVariantMap refused = result.value("refused").toMap();
        QCOMPARE(refused.keys(), (QStringList{"backgroundOverlayOpacity", "noSuchSetting", "serviceEnabled", "serviceWebRoot", "vinylRecordMode"}));
        QVERIFY(refused.value("serviceEnabled").toString().contains("settingsConfigure.json"));
        QVERIFY(!sm.roundedAlbumArt());
        QCOMPARE(sm.backgroundGrid(), QStringLiteral("Normal"));
        QCOMPARE(grid.size(), 1);   // the slot's own notify
        QCOMPARE(sm.backgroundBlurRadius(), 43);
        QCOMPARE(sm.sideCardOpacity(), 0.35);
        QVERIFY(!sm.fileOnlySetting("serviceEnabled").toBool());
        QVERIFY(!sm.vinylRecordMode());
    }

    void mediaFolderMustBeAFolder()
    {
        SettingsManager sm;
        const QString before = sm.mediaFolder();
        QVERIFY(!sm.save_media_folder("/"));
        QVERIFY(!sm.save_media_folder("relative/path"));
        QVERIFY(!sm.save_media_folder(""));
        QCOMPARE(sm.mediaFolder(), before);
        QVERIFY(sm.save_media_folder(m_tmp->path()));
        QCOMPARE(sm.mediaFolder(), m_tmp->path());
        QVERIFY(!sm.save_media_folder("/tmp/../"));   // the root, however spelled
        QVERIFY(sm.save_media_folder(QUrl::fromLocalFile(m_tmp->path() + "/").toString()));
        QCOMPARE(sm.mediaFolder(), m_tmp->path());
    }

    void nowPlayingPresetsLiveInCpp()
    {
        SettingsManager sm;
        const QVariantList presets = sm.property("nowPlayingPresets").toList();
        QStringList labels;
        for (const QVariant &p : presets)
            labels << p.toMap().value("label").toString();
        QCOMPARE(labels, (QStringList{"Minimal", "Vinyl", "Showcase 3D", "Cinema"}));
        // Every preset key has a setter: nothing a preset holds is refused
        for (const QVariant &p : presets) {
            SettingsManager probe;
            const QVariantMap r = probe.apply_settings(p.toMap().value("settings").toMap());
            QVERIFY2(r.value("refused").toMap().isEmpty(), qPrintable(p.toMap().value("label").toString()));
        }
        QVERIFY(sm.applyNowPlayingPreset("Vinyl"));
        QVERIFY(sm.vinylRecordMode());
        QCOMPARE(sm.albumArtTransition(), QStringLiteral("Vinyl Lift"));
        QVERIFY(sm.applyNowPlayingPreset("Showcase 3D"));
        QCOMPARE(sm.sideCardOpacity(), 0.4);
        QCOMPARE(sm.backgroundGrid(), QStringLiteral("4x4"));
        QVERIFY(!sm.applyNowPlayingPreset("Nope"));
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

    void homeCardsAreCappedUniqueAndMove()
    {
        // The OBD settings page asks; the manager enforces (it was the page's
        // job: a cap of 8 in three places, a swap for a move)
        SettingsManager sm;
        sm.save_home_obd_parameters({"SPEED", "RPM", "SPEED", "", "COOLANT_TEMP"});
        QCOMPARE(sm.homeOBDParameters(), (QVariantList{"SPEED", "RPM", "COOLANT_TEMP"}));
        QVERIFY(!sm.add_home_obd_parameter("RPM"));                  // already there
        QVERIFY(sm.add_home_obd_parameter("OIL_TEMP", 1));
        QCOMPARE(sm.homeOBDParameters(), (QVariantList{"SPEED", "OIL_TEMP", "RPM", "COOLANT_TEMP"}));
        for (const char *id : {"A", "B", "C", "D"})
            QVERIFY(sm.add_home_obd_parameter(id));
        QCOMPARE(sm.homeOBDParameters().size(), SettingsManager::kMaxHomeObdParameters);
        QVERIFY(!sm.add_home_obd_parameter("E"));                    // full
        QCOMPARE(sm.property("maxHomeObdParameters").toInt(), 8);

        // Move, not swap: SPEED to index 2 shifts OIL_TEMP and RPM left
        QVERIFY(sm.move_home_obd_parameter(0, 2));
        QCOMPARE(sm.homeOBDParameters().mid(0, 4), (QVariantList{"OIL_TEMP", "RPM", "SPEED", "COOLANT_TEMP"}));
        QVERIFY(sm.move_home_obd_parameter(0, 99));                  // past the end: last
        QCOMPARE(sm.homeOBDParameters().last().toString(), QStringLiteral("OIL_TEMP"));
        QVERIFY(!sm.move_home_obd_parameter(3, 3));

        QVERIFY(sm.remove_home_obd_parameter("A"));
        QVERIFY(!sm.remove_home_obd_parameter("A"));
        QCOMPARE(sm.homeOBDParameters().size(), 7);
        QCOMPARE(sm.m_settings.value("homeOBDParameters").toArray().size(), 7);
    }

    void phoneAudioLevelsInDecibels()
    {
        // The sliders work in dB; the file and the audio path keep linear
        SettingsManager sm;
        sm.save_scrcpy_audio_gain_db(6);
        QVERIFY(std::abs(sm.scrcpyAudioGain() - 1.9953) < 1e-3);
        QVERIFY(std::abs(sm.property("scrcpyAudioGainDb").toDouble() - 6.0) < 1e-9);
        QVERIFY(std::abs(sm.m_settings.value("scrcpyAudioGain").toDouble() - 1.9953) < 1e-3);
        sm.save_scrcpy_audio_gain_db(40);                      // clamped to +18 dB
        QVERIFY(std::abs(sm.scrcpyAudioGainDb() - 18.0) < 1e-9);
        sm.save_scrcpy_audio_duck_level_db(-20);
        QVERIFY(std::abs(sm.scrcpyAudioDuckLevel() - 0.1) < 1e-9);
        sm.save_scrcpy_audio_duck_level_db(-90);               // clamped to -40 dB
        QVERIFY(std::abs(sm.scrcpyAudioDuckLevelDb() + 40.0) < 1e-9);
        sm.save_scrcpy_audio_duck_level(0);                    // silence, from the linear saver
        QCOMPARE(sm.scrcpyAudioDuckLevelDb(), -120.0);
    }

    void screenSizeIsClampedAndUnchangedIsSkipped()
    {
        SettingsManager sm;
        QSignalSpy w(&sm, &SettingsManager::screenWidthChanged);
        sm.save_screen_width(1300);
        QCOMPARE(w.count(), 1);
        const int afterSame = w.count();
        sm.save_screen_width(1300);
        QCOMPARE(w.count(), afterSame);                       // unchanged: no emit, no write
        sm.save_screen_width(100);
        QCOMPARE(sm.screenWidth(), SettingsManager::kMinScreenWidth);
        sm.save_screen_height(99999);
        QCOMPARE(sm.screenHeight(), SettingsManager::kMaxScreenHeight);
        QCOMPARE(sm.m_settings.value("screenHeight").toInt(), SettingsManager::kMaxScreenHeight);
    }

    void pickedMusicFolderBecomesAPath()
    {
        // The Media page hands over what FolderDialog returns
        SettingsManager sm;
        QVERIFY(sm.save_media_folder_url(QUrl("file:///home/rob/My%20Music")));
        QCOMPARE(sm.mediaFolder(), QStringLiteral("/home/rob/My Music"));
        QVERIFY(sm.save_media_folder_url(QUrl("content://com.android.externalstorage.documents/tree/1234-5678%3AMusic")));
        QCOMPARE(sm.mediaFolder(), QStringLiteral("/storage/1234-5678/Music"));
        // Not a path: refused, the folder stays
        QVERIFY(!sm.save_media_folder_url(QUrl("content://com.google.android.apps.docs.storage/tree/abc")));
        QCOMPARE(sm.mediaFolder(), QStringLiteral("/storage/1234-5678/Music"));
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
            first.save_audio_output_device(QStringLiteral("alsa_output.usb-Sennheiser_BTD_700.analog-stereo"));
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

    void audioOutputDeviceDefaultsToTheSystemAndNotifies()
    {
        SettingsManager mgr;
        QCOMPARE(mgr.audioOutputDevice(), QString());   // the system default
        QSignalSpy changed(&mgr, &SettingsManager::audioOutputDeviceChanged);
        mgr.save_audio_output_device(QStringLiteral("earbuds"));
        mgr.save_audio_output_device(QStringLiteral("earbuds"));   // unchanged: no second signal
        QCOMPARE(changed.count(), 1);
        QCOMPARE(mgr.audioOutputDevice(), QStringLiteral("earbuds"));
        mgr.save_audio_output_device(QString(600, QLatin1Char('x')));   // not a device id
        QCOMPARE(mgr.audioOutputDevice(), QStringLiteral("earbuds"));
        mgr.save_audio_output_device(QString());
        QCOMPARE(mgr.audioOutputDevice(), QString());
        QCOMPARE(changed.count(), 2);
    }

    void retiredFeatureKeysAreDropped()
    {
        const QString dir = SettingsManager::getAppDataDir();
        QVERIFY(QDir().mkpath(dir));
        const QJsonObject old{{"downloadSubfolder", "Downloads"}, {"downloadFormat", "mp3"},
                              {"downloadBitrate", "auto"}, {"spotifyClientSecret", "s"},
                              {"themeSetting", "Ocean"}};
        {
            QFile f(dir + "/settingsConfigure.json");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(old).toJson());
        }

        SettingsManager mgr;
        for (const char *key : {"downloadSubfolder", "downloadFormat", "downloadBitrate", "spotifyClientSecret"})
            QVERIFY2(!mgr.m_settings.contains(key), key);
        QCOMPARE(mgr.m_settings.value("themeSetting").toString(), QStringLiteral("Ocean"));
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
