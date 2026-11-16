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
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSet>
#include <QHash>
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

    // ── the settings table (2026-11: one row per plain setting) ──────────
    // Every row against the meta-object and the defaults, and every writable
    // property against the table, so neither can gain an entry alone.

    void tableMatchesTheMetaObject()
    {
        SettingsManager sm;
        const QMetaObject *mo = sm.metaObject();
        const QVariantList table = SettingsManager::describeTable();
        QVERIFY2(table.size() > 80, qPrintable(QString::number(table.size())));
        QSet<QString> keys;
        for (const QVariant &rv : table) {
            const QVariantMap r = rv.toMap();
            const QString key = r.value("key").toString();
            const QByteArray k = key.toLatin1();
            QVERIFY2(!keys.contains(key), k.constData());
            keys.insert(key);
            const QString type = r.value("type").toString();
            // The default is of the declared type, and is the file's default
            QCOMPARE(QString::fromLatin1(r.value("default").metaType().name()), type);
            QVERIFY2(sm.m_defaultSettings.contains(key), k.constData());
            QCOMPARE(sm.m_defaultSettings.value(key).toVariant().toString(), r.value("default").toString());

            const int pi = mo->indexOfProperty(k.constData());
            const QString shape = r.value("shape").toString();
            if (shape == "part") {
                QVERIFY2(pi < 0, k.constData());
                continue;
            }
            QVERIFY2(pi >= 0, k.constData());
            const QMetaProperty prop = mo->property(pi);
            QVERIFY2(prop.hasNotifySignal(), k.constData());
            if (shape == "derived") {
                QVERIFY2(!prop.isWritable(), k.constData());   // a list or a map made of the stored string
                continue;
            }
            QVERIFY2(prop.isWritable(), k.constData());
            const QString propType = QString::fromLatin1(prop.typeName());
            QVERIFY2(propType == type || (type == "double" && propType == "float"), k.constData());
            // Its save slot exists and takes one value
            const QByteArray slot = r.value("slot").toString().toLatin1();
            QVERIFY2(!slot.isEmpty(), k.constData());
            bool found = false;
            for (int i = mo->methodOffset(); i < mo->methodCount(); ++i)
                found = found || (mo->method(i).name() == slot && mo->method(i).parameterCount() == 1);
            QVERIFY2(found, slot.constData());
        }

        // Every property is a row, or one of these that are not plain settings
        const QSet<QString> writableElsewhere = {"mediaFolder", "homeOBDParameters", "scrcpyAudioGainDb",
                                                 "scrcpyAudioDuckLevelDb"};
        const QSet<QString> readOnly = {"albumArtColors", "obdSavedAdapters", "settingsMenuVisibility", "currentVolume",
                                        "maxHomeObdParameters", "customThemes", "customThemeData", "nowPlayingPresets",
                                        "directoryHistory", "sensorCards", "pinnedSettings"};
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QString name = QString::fromLatin1(mo->property(i).name());
            if (mo->property(i).isWritable())
                QVERIFY2(keys.contains(name) || writableElsewhere.contains(name), qPrintable(name));
            else
                QVERIFY2(readOnly.contains(name), qPrintable(name));
        }

        // The pinned-settings registry names the same slot as the table
        const QJsonObject registry = QJsonDocument::fromJson(sm.get_settings_registry().toUtf8()).object();
        QVERIFY(registry.size() > 20);
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            const QJsonObject e = it.value().toObject();
            for (const QVariant &rv : table) {
                if (rv.toMap().value("key").toString() == e.value("key").toString())
                    QCOMPARE(e.value("saveSlot").toString(), rv.toMap().value("slot").toString());
            }
        }
    }

    // A value the row takes that is not `current`
    static QVariant another(const QVariantMap &r, const QVariant &current)
    {
        const QString type = r.value("type").toString();
        const QString rule = r.value("rule").toString();
        const QString key = r.value("key").toString();
        if (type == "bool")
            return !current.toBool();
        if (type == "int") {
            if (key == "scrcpyDisplayDpi")
                return current.toInt() == 200 ? 240 : 200;
            const bool ranged = rule == "clamp" || rule == "within";
            const int hi = ranged ? r.value("hi").toInt() : 1000000;
            return current.toInt() + 1 <= hi ? current.toInt() + 1 : current.toInt() - 1;
        }
        if (type == "double") {
            if (key == "esp32VolumeStepSize")
                return current.toDouble() == 2.5 ? 3.0 : 2.5;
            const bool ranged = rule == "clamp" || rule == "within";
            const double lo = ranged ? r.value("lo").toDouble() : 0.0;
            const double hi = ranged ? r.value("hi").toDouble() : 100.0;
            const double mid = lo + (hi - lo) * 0.25;   // 0.25, 0.5, 0.875: exact as a float too
            return qFuzzyCompare(current.toDouble() + 1.0, mid + 1.0) ? lo + (hi - lo) * 0.5 : mid;
        }
        if (rule == "oneOf") {
            const QStringList allowed = r.value("allowed").toStringList();
            return allowed.first() == current.toString() ? allowed.last() : allowed.first();
        }
        if (key == "obdSavedAdapters")
            return current.toString() == "[]" ? QStringLiteral(R"([{"mac":"AA:BB","name":"n"}])") : QStringLiteral("[]");
        if (key == "albumArtColors")
            return current.toString().isEmpty() ? QStringLiteral(R"({"accent":"#ff8800"})") : QString();
        return current.toString() + QStringLiteral("x");
    }

    static bool same(const QVariant &a, const QVariant &b)
    {
        if (a.typeId() == QMetaType::Double || b.typeId() == QMetaType::Double || a.typeId() == QMetaType::Float)
            return std::abs(a.toDouble() - b.toDouble()) < 1e-6;
        return a.toString() == b.toString();
    }

    // What says a row changed: its own property's signal, or the property it is part of
    static QMetaMethod notifierOf(const SettingsManager &sm, const QVariantMap &r)
    {
        const QString key = r.value("key").toString();
        const QByteArray name = r.value("shape").toString() == "part" ? QByteArray("sensorCards") : key.toLatin1();
        return sm.metaObject()->property(sm.metaObject()->indexOfProperty(name.constData())).notifySignal();
    }

    // The review's bug: save_setting("uiScale", 2.0) wrote the file and left
    // the property and its signal behind. For EVERY row: the generic path
    // and the typed path read and write one value.
    void genericAndTypedPathsAgreeForEveryRow()
    {
        SettingsManager sm;
        const QVariantList table = SettingsManager::describeTable();
        for (const QVariant &rv : table) {
            const QVariantMap r = rv.toMap();
            const QString key = r.value("key").toString();
            const QByteArray k = key.toLatin1();
            const bool plain = r.value("shape").toString() == "plain";
            QSignalSpy notify(&sm, notifierOf(sm, r));
            QSignalSpy generic(&sm, &SettingsManager::genericSettingChanged);
            QVERIFY2(notify.isValid(), k.constData());

            const QVariant original = sm.get_setting(key);
            if (plain)
                QVERIFY2(same(sm.property(k.constData()), original), k.constData());

            // In through the generic path, out through the typed one
            const QVariant value = another(r, original);
            if (key == "albumArtColors") {
                // The player's to set, not a settings client's: refused
                // here as in apply_settings, and its own setter is the way in
                sm.save_setting(key, value);
                QVERIFY(same(sm.get_setting(key), original));
                QCOMPARE(notify.count(), 0);
                QCOMPARE(generic.count(), 0);
                sm.set_album_art_colors(value.toString());
                QVERIFY(same(sm.get_setting(key), value));
                QVERIFY(same(sm.m_settings.value(key).toVariant(), value));
                QCOMPARE(notify.count(), 1);
                // The same palette again is announced (QML has no other
                // way to get it back) and not written
                sm.flushPendingSave();
                sm.set_album_art_colors(value.toString());
                QCOMPARE(notify.count(), 2);
                QVERIFY(!sm.m_savePending);
                continue;
            }
            sm.save_setting(key, value);
            QVERIFY2(same(sm.get_setting(key), value), k.constData());
            if (plain)
                QVERIFY2(same(sm.property(k.constData()), value), k.constData());
            QVERIFY2(same(sm.m_settings.value(key).toVariant(), value), k.constData());
            QVERIFY2(notify.count() == 1, k.constData());
            QVERIFY2(generic.count() == 1 && generic.first().at(0).toString() == key, k.constData());

            // The same value again: nothing to say, nothing to write
            sm.flushPendingSave();
            sm.save_setting(key, value);
            QVERIFY2(notify.count() == 1 && generic.count() == 1, k.constData());
            QVERIFY2(!sm.m_savePending, k.constData());

            // In through the typed path (the writable property), out through the generic one
            if (plain) {
                QVERIFY2(sm.setProperty(k.constData(), original), k.constData());
                QVERIFY2(same(sm.get_setting(key), original), k.constData());
                QVERIFY2(same(sm.m_settings.value(key).toVariant(), original), k.constData());
                QVERIFY2(notify.count() == 2, k.constData());
                QVERIFY2(generic.count() == 1, k.constData());   // the property write is not a generic save
                sm.setProperty(k.constData(), original);
                QVERIFY2(notify.count() == 2, k.constData());   // unchanged
            }
        }
    }

    void saveSettingMovesTheLiveValueAndItsSignal()
    {
        SettingsManager sm;
        QSignalSpy scale(&sm, &SettingsManager::uiScaleChanged);
        sm.save_setting("uiScale", 1.5);
        QCOMPARE(sm.uiScale(), 1.5f);
        QCOMPARE(scale.count(), 1);
        QCOMPARE(scale.first().at(0).toFloat(), 1.5f);
        // ...and the typed setter is what get_setting reads
        sm.save_ui_scale(0.75f);
        QCOMPARE(sm.get_setting("uiScale").toDouble(), 0.75);
        // A list kept in a member follows a generic save too
        QSignalSpy pinned(&sm, &SettingsManager::pinnedSettingsChanged);
        sm.save_setting("pinnedSettings", QVariantList{QStringLiteral("ui_scale")});
        QCOMPARE(sm.pinnedSettings(), QVariantList{QStringLiteral("ui_scale")});
        QCOMPARE(pinned.count(), 1);
        QVERIFY(sm.is_setting_pinned("ui_scale"));
    }

    void everyRowSurvivesARestart()
    {
        const QVariantList table = SettingsManager::describeTable();
        QVariantMap wanted;
        {
            SettingsManager first;
            for (const QVariant &rv : table) {
                const QVariantMap r = rv.toMap();
                const QString key = r.value("key").toString();
                wanted.insert(key, another(r, first.get_setting(key)));
                if (key == "albumArtColors")
                    first.set_album_art_colors(wanted.value(key).toString());   // not save_setting's to write
                else
                    first.save_setting(key, wanted.value(key));
            }
            first.flushPendingSave();
        }
        SettingsManager second;
        for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it)
            QVERIFY2(same(second.get_setting(it.key()), it.value()), qPrintable(it.key()));
    }

    void valuesARowDoesNotTakeAreRefused()
    {
        SettingsManager sm;
        QSignalSpy scale(&sm, &SettingsManager::uiScaleChanged);
        QSignalSpy clock(&sm, &SettingsManager::clockSizeChanged);
        QSignalSpy show(&sm, &SettingsManager::showClockChanged);
        QSignalSpy window(&sm, &SettingsManager::windowStateChanged);
        // Outside the slider's range, by the slot, the property and the generic path
        sm.save_ui_scale(5.0f);
        sm.setProperty("uiScale", 0.1);
        sm.save_setting("uiScale", 9);
        QCOMPARE(sm.uiScale(), 1.0f);
        sm.save_clock_size(500);
        sm.save_setting("clockSize", 2);
        QCOMPARE(sm.clockSize(), 18);
        sm.save_start_volume(1.5f);
        QCOMPARE(sm.startUpVolume(), 0.1f);
        // The limits themselves are values
        sm.save_ui_scale(2.0f);
        QCOMPARE(sm.uiScale(), 2.0f);
        sm.save_ui_scale(0.5f);
        QCOMPARE(scale.count(), 2);
        // Another type, from outside
        sm.save_setting("uiScale", QStringLiteral("big"));
        sm.save_setting("showClock", 1);
        sm.save_setting("showClock", QStringLiteral("false"));
        sm.save_setting("clockSize", QStringLiteral("40"));
        sm.save_setting("windowState", 3);
        sm.save_setting("windowState", QStringLiteral("sideways"));
        QCOMPARE(sm.uiScale(), 0.5f);
        QCOMPARE(sm.showClock(), true);
        QCOMPARE(sm.windowState(), QStringLiteral("windowed"));
        QCOMPARE(clock.count(), 0);
        QCOMPARE(show.count(), 0);
        QCOMPARE(window.count(), 0);
        QCOMPARE(sm.m_settings.value("clockSize").toInt(), 18);
        // A number for an int row is rounded; a clamped row clamps, as its slot always did
        sm.save_setting("clockSize", 39.6);
        QCOMPARE(sm.clockSize(), 40);
        sm.save_setting("sideCardAngle", 999);
        QCOMPARE(sm.sideCardAngle(), 60);
        sm.save_side_card_angle(-3);
        QCOMPARE(sm.sideCardAngle(), 5);
    }

    void anUnchangedValueIsNotSavedOrAnnounced()
    {
        SettingsManager sm;
        sm.save_theme_setting(QStringLiteral("Midnight"));
        sm.flushPendingSave();
        QSignalSpy theme(&sm, &SettingsManager::themeSettingChanged);
        sm.save_theme_setting(QStringLiteral("Midnight"));
        sm.setProperty("themeSetting", QStringLiteral("Midnight"));
        sm.save_setting("themeSetting", QStringLiteral("Midnight"));
        sm.save_side_card_angle(999);          // clamped to 60 ...
        sm.flushPendingSave();
        QSignalSpy angle(&sm, &SettingsManager::sideCardAngleChanged);
        sm.save_side_card_angle(61);           // ... and 61 is 60 again
        QCOMPARE(theme.count(), 0);
        QCOMPARE(angle.count(), 0);
        QVERIFY(!sm.m_savePending);
    }

    void aResetAnnouncesExactlyWhatChanged()
    {
        SettingsManager sm;
        sm.save_show_clock(false);
        sm.save_ui_scale(1.5f);
        sm.save_sensor_card_visible(QStringLiteral("Roll"), false);
        sm.save_sensor_card_visible(QStringLiteral("Pitch"), false);
        sm.save_home_obd_parameters({QStringLiteral("RPM")});
        sm.save_setting("airPlayName", QStringLiteral("Jeep"));
        sm.save_start_volume(0.5f);

        // A spy on every property's signal
        const QMetaObject *mo = sm.metaObject();
        QHash<QString, QSignalSpy *> spies;
        for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
            const QMetaProperty p = mo->property(i);
            const QString signal = QString::fromLatin1(p.notifySignal().name());
            if (p.hasNotifySignal() && !spies.contains(signal))
                spies.insert(signal, new QSignalSpy(&sm, p.notifySignal()));
        }
        QSignalSpy generic(&sm, &SettingsManager::genericSettingChanged);

        sm.reset_to_defaults();
        QCOMPARE(sm.showClock(), true);
        QCOMPARE(sm.uiScale(), 1.0f);
        QCOMPARE(sm.sensorCards().value("Roll").toBool(), true);
        QStringList fired;
        for (auto it = spies.constBegin(); it != spies.constEnd(); ++it) {
            if (it.value()->count() > 1)
                fired << it.key() + " x" + QString::number(it.value()->count());
            else if (it.value()->count() == 1)
                fired << it.key();
        }
        fired.sort();
        // (not currentVolumeChanged: the live volume was never moved from
        // where the default start-up volume put it)
        QCOMPARE(fired, (QStringList{"homeOBDParametersChanged", "sensorCardsChanged", "showClockChanged",
                                     "startUpVolumeChanged", "uiScaleChanged"}));
        QStringList genericKeys;
        for (const QList<QVariant> &call : generic)
            genericKeys << call.at(0).toString();
        QCOMPARE(genericKeys, QStringList{"airPlayName"});
        qDeleteAll(spies);

        // A second reset has nothing to say
        QSignalSpy clock(&sm, &SettingsManager::showClockChanged);
        QSignalSpy again(&sm, &SettingsManager::genericSettingChanged);
        sm.reset_to_defaults();
        QCOMPARE(clock.count(), 0);
        QCOMPARE(again.count(), 0);
    }

    // What the file holds is brought within each row's rule when it loads,
    // and the file's value and the property are then the same one
    void aFileOutsideTheRulesLoadsWithinThem()
    {
        const QString dir = SettingsManager::getAppDataDir();
        QVERIFY(QDir().mkpath(dir));
        const QJsonObject edited{{"blackBoxMaxMB", 99999}, {"sensorEmitRate", 3}, {"windowState", "sideways"},
                                 {"gaugeSpeedUnit", "knots"}, {"uiScale", 2.5}, {"clockSize", 39.6},
                                 {"obdBluetoothPort", "/dev/rfcomm0"}, {"lastSettingsSection", "clockSettings"},
                                 {"obdSavedAdapters", ""}};
        {
            QFile f(dir + "/settingsConfigure.json");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(edited).toJson());
        }
        SettingsManager sm;
        QCOMPARE(sm.blackBoxMaxMB(), 5000);
        QCOMPARE(sm.sensorEmitRate(), 10);
        QCOMPARE(sm.windowState(), QStringLiteral("windowed"));
        QCOMPARE(sm.gaugeSpeedUnit(), QStringLiteral("mph"));
        QCOMPARE(sm.uiScale(), 2.5f);   // a range the UI implies is not forced on a file that has more
        QCOMPARE(sm.clockSize(), 40);
        QCOMPARE(sm.obdBluetoothPort(), QString());
        QCOMPARE(sm.lastSettingsSection(), QStringLiteral("displaySettings"));
        QCOMPARE(sm.obdSavedAdapters(), QVariantList{});
        for (const char *key : {"blackBoxMaxMB", "sensorEmitRate", "windowState", "gaugeSpeedUnit", "clockSize"})
            QVERIFY2(same(sm.get_setting(key), sm.property(key)) && same(sm.m_settings.value(key).toVariant(), sm.property(key)), key);
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
};

QTEST_MAIN(TstSettingsManager)
#include "tst_settingsmanager.moc"
