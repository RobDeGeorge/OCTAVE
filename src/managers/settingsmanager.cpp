// settingsmanager.cpp — C++ port of backend/settings_manager.py
// OCTAVE Settings Manager

#include "settingsmanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>
#include <QLoggingCategory>

#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <io.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

Q_LOGGING_CATEGORY(lcSettings, "octave.settings")

// ---------------------------------------------------------------------------
// Static registry (populated once)
// ---------------------------------------------------------------------------
QJsonObject SettingsManager::s_settingsRegistry;

// ---------------------------------------------------------------------------
// buildSettingsRegistry — mirrors Python SETTINGS_REGISTRY dict
// ---------------------------------------------------------------------------
QJsonObject SettingsManager::buildSettingsRegistry()
{
    if (!s_settingsRegistry.isEmpty())
        return s_settingsRegistry;

    auto entry = [](const QString &key, const QString &label, const QString &category,
                    const QString &controlType, const QString &saveSlot,
                    const QJsonObject &params = {}) -> QJsonObject {
        QJsonObject o;
        o[QStringLiteral("key")]         = key;
        o[QStringLiteral("label")]       = label;
        o[QStringLiteral("category")]    = category;
        o[QStringLiteral("controlType")] = controlType;
        o[QStringLiteral("saveSlot")]    = saveSlot;
        if (!params.isEmpty())
            o[QStringLiteral("params")] = params;
        return o;
    };

    auto sliderParams = [](double from, double to, double step, bool log = false) {
        QJsonObject p;
        p[QStringLiteral("from")]     = from;
        p[QStringLiteral("to")]       = to;
        p[QStringLiteral("stepSize")] = step;
        if (log) p[QStringLiteral("logarithmic")] = true;
        return p;
    };

    auto chipParams = [](const QJsonArray &opts) {
        QJsonObject p;
        p[QStringLiteral("options")] = opts;
        return p;
    };

    auto chipSource = [](const QString &src) {
        QJsonObject p;
        p[QStringLiteral("optionsSource")] = src;
        return p;
    };

    QJsonObject reg;
    reg[QStringLiteral("startup_volume")]            = entry("startUpVolume", "Startup Volume", "mediaSettings", "slider", "save_start_volume", sliderParams(0, 1, 0.01, true));
    reg[QStringLiteral("auto_play_on_startup")]      = entry("autoPlayOnStartup", "Autoplay on Startup", "mediaSettings", "toggle", "save_auto_play_on_startup");
    reg[QStringLiteral("persist_shuffle_state")]      = entry("persistShuffleState", "Remember Shuffle State", "mediaSettings", "toggle", "save_persist_shuffle_state");
    reg[QStringLiteral("show_waveform_visualizer")]   = entry("showWaveformVisualizer", "Waveform Visualizer", "mediaSettings", "toggle", "save_show_waveform_visualizer");
    reg[QStringLiteral("show_3d_button_tilt")]        = entry("show3DButtonTilt", "3D Button Tilt", "mediaSettings", "toggle", "save_show_3d_button_tilt");
    reg[QStringLiteral("show_3d_album_preview")]      = entry("show3DAlbumPreview", "3D Album Preview", "mediaSettings", "toggle", "save_show_3d_album_preview");
    reg[QStringLiteral("rounded_album_art")]          = entry("roundedAlbumArt", "Rounded Album Art", "mediaSettings", "toggle", "save_rounded_album_art");
    reg[QStringLiteral("album_art_corner_radius")]    = entry("albumArtCornerRadius", "Corner Radius", "mediaSettings", "slider", "save_album_art_corner_radius", sliderParams(2, 32, 1));
    reg[QStringLiteral("show_album_art_shadow")]      = entry("showAlbumArtShadow", "Album Art Shadow", "mediaSettings", "toggle", "save_show_album_art_shadow");
    reg[QStringLiteral("vinyl_record_mode")]          = entry("vinylRecordMode", "Vinyl Record Mode", "mediaSettings", "toggle", "save_vinyl_record_mode");
    reg[QStringLiteral("album_art_transition")]       = entry("albumArtTransition", "Track Transition", "mediaSettings", "chips", "save_album_art_transition",
        chipParams({QStringLiteral("Crossfade"), QStringLiteral("Slide"), QStringLiteral("Vinyl Lift"), QStringLiteral("Dissolve"), QStringLiteral("Flip"),
                    QStringLiteral("Coverflow"), QStringLiteral("Conveyor"), QStringLiteral("Stack"), QStringLiteral("Depth"), QStringLiteral("Swing")}));
    reg[QStringLiteral("background_overlay_opacity")] = entry("backgroundOverlayOpacity", "Overlay Opacity", "mediaSettings", "slider", "save_background_overlay_opacity", sliderParams(0, 100, 1));
    reg[QStringLiteral("side_card_opacity")]          = entry("sideCardOpacity", "Side Card Opacity", "mediaSettings", "slider", "save_side_card_opacity", sliderParams(0.1, 1.0, 0.05));
    reg[QStringLiteral("side_card_angle")]            = entry("sideCardAngle", "Side Card Angle", "mediaSettings", "slider", "save_side_card_angle", sliderParams(5, 60, 1));
    reg[QStringLiteral("button_tilt_duration")]       = entry("buttonTiltDuration", "Tilt Duration", "mediaSettings", "slider", "save_button_tilt_duration", sliderParams(50, 500, 10));
    reg[QStringLiteral("text_scroll_speed")]          = entry("textScrollSpeed", "Text Scroll Speed", "mediaSettings", "slider", "save_text_scroll_speed", sliderParams(1000, 10000, 500));
    reg[QStringLiteral("ui_scale")]                   = entry("uiScale", "UI Scaling", "displaySettings", "slider", "save_ui_scale", sliderParams(0.5, 2.0, 0.05));
    reg[QStringLiteral("theme_setting")]              = entry("themeSetting", "Theme", "displaySettings", "chips", "save_theme_setting", chipSource("themeList"));
    reg[QStringLiteral("show_clock")]                 = entry("showClock", "Show Clock", "displaySettings", "toggle", "save_show_clock");
    reg[QStringLiteral("bottom_bar_media_controls")]  = entry("showBottomBarMediaControls", "Nav Bar Media Controls", "displaySettings", "toggle", "save_show_bottom_bar_media_controls");
    reg[QStringLiteral("background_blur_radius")]     = entry("backgroundBlurRadius", "Album Art Blur", "mediaSettings", "slider", "save_background_blur_radius", sliderParams(0, 100, 1));
    reg[QStringLiteral("color_transition_ms")]        = entry("colorTransitionMs", "Theme Transition Speed", "displaySettings", "slider", "save_color_transition_ms", sliderParams(0, 5000, 100));
    reg[QStringLiteral("obd_fast_mode")]              = entry("obdFastMode", "OBD Fast Mode", "obdSettings", "toggle", "save_obd_fast_mode");
    reg[QStringLiteral("gesture_sensor_enabled")]     = entry("gestureSensorEnabled", "Gesture Sensor", "gestureSensorSettings", "toggle", "save_gesture_sensor_enabled");
    reg[QStringLiteral("imu_enabled")]                = entry("imuEnabled", "IMU Sensor", "imuSettings", "toggle", "save_imu_enabled");

    s_settingsRegistry = reg;
    return reg;
}

// ---------------------------------------------------------------------------
// buildDefaultSettings — mirrors Python _default_settings dict
// ---------------------------------------------------------------------------
QJsonObject SettingsManager::buildDefaultSettings() const
{
    QJsonObject d;
    d[QStringLiteral("deviceName")]          = QStringLiteral("Default Device");
    d[QStringLiteral("themeSetting")]         = QStringLiteral("CosmicVoyager");
    d[QStringLiteral("fontSetting")]          = QStringLiteral("Google Sans");
    d[QStringLiteral("startUpVolume")]        = 0.1;
    d[QStringLiteral("showClock")]            = true;
    d[QStringLiteral("clockFormat24Hour")]    = true;
    d[QStringLiteral("clockShowSeconds")]     = false;
    d[QStringLiteral("clockSize")]            = 18;
    d[QStringLiteral("backgroundGrid")]       = QStringLiteral("4x4");
    d[QStringLiteral("screenWidth")]          = 1280;
    d[QStringLiteral("screenHeight")]         = 720;
    d[QStringLiteral("backgroundBlurRadius")] = 40;
    d[QStringLiteral("uiScale")]              = 1.0;
    d[QStringLiteral("colorTransitionMs")]    = 1000;
    d[QStringLiteral("songLengthTransition")] = false;
    d[QStringLiteral("obdBluetoothPort")]     = QString();
    d[QStringLiteral("obdSavedAdapters")]     = QStringLiteral("[]");
    d[QStringLiteral("obdFastMode")]          = true;
    d[QStringLiteral("obdAutoReconnectAttempts")] = 0;
    d[QStringLiteral("bottomBarOrientation")]  = QStringLiteral("bottom");
    d[QStringLiteral("showBottomBarMediaControls")] = true;
    d[QStringLiteral("fuelTankCapacity")]      = 15.0;

    // OBD parameters
    QJsonObject obdParams;
    // Enabled by default
    for (const auto &p : {
        "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE", "ENGINE_LOAD", "THROTTLE_POS",
        "INTAKE_TEMP", "TIMING_ADVANCE", "MAF", "SPEED", "RPM",
        "COMMANDED_EQUIV_RATIO", "FUEL_LEVEL", "INTAKE_PRESSURE",
        "SHORT_FUEL_TRIM_1", "LONG_FUEL_TRIM_1", "O2_B1S1", "FUEL_PRESSURE",
        "OIL_TEMP", "FUEL_ECONOMY", "DISTANCE_TO_EMPTY", "IGNITION_TIMING"})
    {
        obdParams[QString::fromLatin1(p)] = true;
    }
    // Disabled by default
    for (const auto &p : {
        "RUN_TIME", "DISTANCE_W_MIL", "FUEL_RAIL_PRESSURE_VAC",
        "FUEL_RAIL_PRESSURE_DIRECT", "BAROMETRIC_PRESSURE", "AMBIANT_AIR_TEMP",
        "RELATIVE_THROTTLE_POS", "THROTTLE_POS_B", "ACCELERATOR_POS_D",
        "CATALYST_TEMP_B1S1", "CATALYST_TEMP_B1S2", "EVAP_VAPOR_PRESSURE",
        "SHORT_FUEL_TRIM_2", "LONG_FUEL_TRIM_2", "O2_B1S2", "O2_B2S1",
        "O2_B2S2", "DISTANCE_SINCE_DTC_CLEAR", "WARMUPS_SINCE_DTC_CLEAR",
        "ABSOLUTE_LOAD", "COMMANDED_EGR", "EGR_ERROR", "ETHANOL_PERCENT",
        "O2_B1S3", "O2_B1S4", "O2_B2S3", "O2_B2S4",
        "O2_S1_WR_VOLTAGE", "O2_S2_WR_VOLTAGE", "O2_S3_WR_VOLTAGE",
        "O2_S4_WR_VOLTAGE", "O2_S5_WR_VOLTAGE", "O2_S6_WR_VOLTAGE",
        "O2_S7_WR_VOLTAGE", "O2_S8_WR_VOLTAGE",
        "O2_S1_WR_CURRENT", "O2_S2_WR_CURRENT", "O2_S3_WR_CURRENT",
        "O2_S4_WR_CURRENT", "O2_S5_WR_CURRENT", "O2_S6_WR_CURRENT",
        "O2_S7_WR_CURRENT", "O2_S8_WR_CURRENT",
        "CATALYST_TEMP_B2S1", "CATALYST_TEMP_B2S2",
        "THROTTLE_POS_C", "ACCELERATOR_POS_E", "ACCELERATOR_POS_F",
        "THROTTLE_ACTUATOR", "EVAPORATIVE_PURGE", "FUEL_RAIL_PRESSURE_ABS",
        "FUEL_INJECT_TIMING", "FUEL_RATE", "RUN_TIME_MIL",
        "TIME_SINCE_DTC_CLEARED", "MAX_MAF", "FUEL_TYPE",
        "EVAP_VAPOR_PRESSURE_ABS", "EVAP_VAPOR_PRESSURE_ALT",
        "SHORT_O2_TRIM_B1", "LONG_O2_TRIM_B1", "SHORT_O2_TRIM_B2",
        "LONG_O2_TRIM_B2", "RELATIVE_ACCEL_POS", "HYBRID_BATTERY_REMAINING",
        "ELM_VOLTAGE"})
    {
        obdParams[QString::fromLatin1(p)] = false;
    }
    d[QStringLiteral("obdParameters")] = obdParams;

    d[QStringLiteral("homeOBDParameters")] = QJsonArray({
        QStringLiteral("SPEED"), QStringLiteral("RPM"),
        QStringLiteral("COOLANT_TEMP"), QStringLiteral("CONTROL_MODULE_VOLTAGE")
    });
    d[QStringLiteral("supportedOBDParameters")] = QJsonArray();
    d[QStringLiteral("lastSettingsSection")]     = QStringLiteral("deviceSettings");
    d[QStringLiteral("spotifyClientId")]         = QString();
    d[QStringLiteral("spotifyClientSecret")]     = QString();
    d[QStringLiteral("mediaSource")]             = QStringLiteral("local");
    d[QStringLiteral("mediaFolder")]             = QString();
    d[QStringLiteral("customThemes")]            = QJsonObject();
    d[QStringLiteral("autoPlayOnStartup")]       = false;
    d[QStringLiteral("persistShuffleState")]     = false;
    d[QStringLiteral("lastShuffleState")]        = false;
    d[QStringLiteral("lastPlayedSong")]          = QString();
    d[QStringLiteral("lastPlayedPosition")]      = 0;
    d[QStringLiteral("lastPlayedPlaylist")]      = QString();
    d[QStringLiteral("windowState")]             = QStringLiteral("windowed");
    d[QStringLiteral("musicButtonDefaultPage")]  = QStringLiteral("mediaRoom");
    d[QStringLiteral("returnToLibraryAfterSelection")] = false;
    d[QStringLiteral("androidAutoEnabled")]      = false;
    d[QStringLiteral("phoneMirrorEnabled")]      = false;
    d[QStringLiteral("scrcpyAudioEnabled")]      = false;
    d[QStringLiteral("scrcpyDisplaySize")]       = QStringLiteral("1280x800");     // --new-display WxH, "" = phone screen
    d[QStringLiteral("scrcpyAudioGain")]         = 2.0;   // linear gain on phone audio before OCTAVE's volume (+6 dB)
    d[QStringLiteral("scrcpyAudioDuckEnabled")]  = true;  // duck local media while the phone produces sound
    d[QStringLiteral("scrcpyAudioDuckLevel")]    = 0.1;   // linear factor applied to local media while ducked (-20 dB)

    // Settings menu visibility
    QJsonObject menuVis;
    menuVis[QStringLiteral("deviceSettings")]       = true;
    menuVis[QStringLiteral("mediaSettings")]        = true;
    menuVis[QStringLiteral("displaySettings")]      = true;
    menuVis[QStringLiteral("obdSettings")]          = true;
    menuVis[QStringLiteral("androidAutoSettings")]  = true;
    menuVis[QStringLiteral("phoneMirrorSettings")]  = true;
    menuVis[QStringLiteral("volumeKnobSettings")]   = false;
    menuVis[QStringLiteral("gestureSensorSettings")] = true;
    menuVis[QStringLiteral("about")]                = true;
    d[QStringLiteral("settingsMenuVisibility")]     = menuVis;

    // ESP32
    d[QStringLiteral("esp32VolumeEnabled")]    = true;
    d[QStringLiteral("esp32VolumePort")]       = QStringLiteral("COM7");
    d[QStringLiteral("esp32VolumeStepSize")]   = 1;
    d[QStringLiteral("esp32AutoReconnect")]    = true;
    d[QStringLiteral("esp32LedSleepEnabled")]  = true;
    d[QStringLiteral("esp32LedColorMode")]     = QStringLiteral("theme");
    d[QStringLiteral("esp32LedStaticColor")]   = QStringLiteral("#00FFFF");

    // Waveform
    d[QStringLiteral("showWaveformVisualizer")] = true;

    // 3D effects
    d[QStringLiteral("show3DButtonTilt")]        = false;
    d[QStringLiteral("show3DAlbumPreview")]      = false;
    d[QStringLiteral("roundedAlbumArt")]         = true;
    d[QStringLiteral("albumArtCornerRadius")]    = 16;
    d[QStringLiteral("showAlbumArtShadow")]      = true;
    d[QStringLiteral("vinylRecordMode")]         = false;
    d[QStringLiteral("albumArtTransition")]      = QStringLiteral("Crossfade");
    d[QStringLiteral("backgroundOverlayOpacity")] = 80;
    d[QStringLiteral("sideCardOpacity")]         = 0.4;
    d[QStringLiteral("sideCardAngle")]           = 30;
    d[QStringLiteral("buttonTiltDuration")]      = 200;
    d[QStringLiteral("textScrollSpeed")]         = 5000;
    d[QStringLiteral("settingsLayoutStyle")]     = QStringLiteral("Sidebar");
    d[QStringLiteral("albumArtColors")]          = QString();

    // IMU
    d[QStringLiteral("imuEnabled")]              = true;
    d[QStringLiteral("sensorEmitRate")]          = 60;
    d[QStringLiteral("sensorDecimalPlaces")]     = 1;
    d[QStringLiteral("sensorTempUnit")]          = QStringLiteral("F");
    d[QStringLiteral("sensorAltitudeUnit")]      = QStringLiteral("m");
    d[QStringLiteral("sensorShowPitch")]         = true;
    d[QStringLiteral("sensorShowRoll")]          = true;
    d[QStringLiteral("sensorShowHeading")]       = true;
    d[QStringLiteral("sensorShowGForce")]        = true;
    d[QStringLiteral("sensorShowAltitude")]      = true;
    d[QStringLiteral("sensorShowTemperature")]   = true;

    // Gesture
    d[QStringLiteral("gestureSensorEnabled")]    = true;
    QJsonObject gmap;
    gmap[QStringLiteral("RIGHT")]              = QStringLiteral("next_track");
    gmap[QStringLiteral("LEFT")]               = QStringLiteral("previous_track");
    gmap[QStringLiteral("UP")]                 = QStringLiteral("volume_up");
    gmap[QStringLiteral("DOWN")]               = QStringLiteral("volume_down");
    gmap[QStringLiteral("FORWARD")]            = QStringLiteral("play_pause");
    gmap[QStringLiteral("BACKWARD")]           = QStringLiteral("mute_toggle");
    gmap[QStringLiteral("CLOCKWISE")]          = QStringLiteral("volume_up");
    gmap[QStringLiteral("COUNTER-CLOCKWISE")]  = QStringLiteral("volume_down");
    gmap[QStringLiteral("WAVE")]               = QStringLiteral("play_pause");
    d[QStringLiteral("gestureMapping")]          = gmap;
    d[QStringLiteral("gestureVolumeStep")]       = 5;
    d[QStringLiteral("gestureCooldown")]         = 300;
    d[QStringLiteral("pinnedSettings")]          = QJsonArray();

    // Music downloads
    d[QStringLiteral("downloadSubfolder")] = QStringLiteral("Downloads");
    d[QStringLiteral("downloadFormat")]    = QStringLiteral("mp3");
    d[QStringLiteral("downloadBitrate")]   = QStringLiteral("auto");

    // Expanded cards
    d[QStringLiteral("expandedCards")] = QJsonObject();

    // Directory history
    d[QStringLiteral("directoryHistory")] = QJsonArray();

    return d;
}

// ---------------------------------------------------------------------------
// getAppDataDir  — OS-specific config directory (static)
// ---------------------------------------------------------------------------
QString SettingsManager::getAppDataDir()
{
    const QString appName = QStringLiteral("OCTAVE");
    QString dataDir;

#ifdef Q_OS_WIN
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
        base = QDir::homePath();
    dataDir = base + QDir::separator() + appName;
#elif defined(Q_OS_MACOS)
    dataDir = QDir::homePath() + QStringLiteral("/Library/Application Support/") + appName;
#else
    // Linux / other Unix
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    dataDir = base + QDir::separator() + appName;
#endif

    QDir().mkpath(dataDir);
    return dataDir;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    m_appDataDir  = getAppDataDir();
    m_settingsFile = m_appDataDir + QStringLiteral("/settingsConfigure.json");

    // backend_dir equivalent: directory of the executable (or compile-time path)
    m_backendDir = QCoreApplication::applicationDirPath();

    // Build defaults
    m_defaultSettings = buildDefaultSettings();

    // Build registry (once)
    buildSettingsRegistry();

    // Load settings
    m_settings = loadSettings();     // reads the file: m_settingsLoaded is still false

    // Migrate uiScale 0.6 -> 1.0
    if (qFuzzyCompare(m_settings.value(QStringLiteral("uiScale")).toDouble(), 0.6)) {
        qCInfo(lcSettings) << "Migrating uiScale from old default 0.6 to new default 1.0";
        m_settings[QStringLiteral("uiScale")] = 1.0;
        saveSettings(m_settings);
    }

    m_settingsLoaded = true;
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &SettingsManager::flushPendingSave);

    // Helper lambda: read a value from m_settings falling back to defaults
    auto s = [&](const QString &key) -> QJsonValue {
        if (m_settings.contains(key))
            return m_settings.value(key);
        return m_defaultSettings.value(key);
    };

    // --- Populate member variables from loaded settings ---
    m_deviceName        = s(QStringLiteral("deviceName")).toString();
    m_themeSetting      = s(QStringLiteral("themeSetting")).toString();
    m_fontSetting       = s(QStringLiteral("fontSetting")).toString();
    m_startVolume       = static_cast<float>(s(QStringLiteral("startUpVolume")).toDouble());
    m_showClock         = s(QStringLiteral("showClock")).toBool();
    m_clockFormat24Hour = s(QStringLiteral("clockFormat24Hour")).toBool();
    m_clockShowSeconds  = s(QStringLiteral("clockShowSeconds")).toBool();
    m_clockSize         = s(QStringLiteral("clockSize")).toInt();
    m_backgroundGrid    = s(QStringLiteral("backgroundGrid")).toString();
    m_screenWidth       = s(QStringLiteral("screenWidth")).toInt();
    m_screenHeight      = s(QStringLiteral("screenHeight")).toInt();
    m_backgroundBlurRadius = s(QStringLiteral("backgroundBlurRadius")).toInt();
    m_uiScale           = static_cast<float>(s(QStringLiteral("uiScale")).toDouble());
    m_colorTransitionMs = s(QStringLiteral("colorTransitionMs")).toInt();
    m_songLengthTransition = s(QStringLiteral("songLengthTransition")).toBool();
    m_obdBluetoothPort  = s(QStringLiteral("obdBluetoothPort")).toString();
    // Migrate legacy "/dev/rfcomm0" placeholder — it was the desktop default but
    // is meaningless on Android (where the field now holds a MAC address) and
    // was leaking into the UI as if the user had entered it themselves.
    if (m_obdBluetoothPort == QStringLiteral("/dev/rfcomm0")) {
        m_obdBluetoothPort.clear();
    }
    m_obdSavedAdapters  = s(QStringLiteral("obdSavedAdapters")).toString();
    if (m_obdSavedAdapters.isEmpty()) m_obdSavedAdapters = QStringLiteral("[]");
    m_obdFastMode       = s(QStringLiteral("obdFastMode")).toBool();
    m_obdAutoReconnectAttempts = s(QStringLiteral("obdAutoReconnectAttempts")).toInt();

    // Media folder: default to the OS-standard music location if empty.
    // QStandardPaths::MusicLocation is ~/Music on desktop, /storage/emulated/0/Music
    // on Android, Documents/Music on Windows, ~/Music on macOS.
    m_mediaFolder = s(QStringLiteral("mediaFolder")).toString();
    if (m_mediaFolder.isEmpty()) {
        m_mediaFolder = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (m_mediaFolder.isEmpty())
            m_mediaFolder = QDir::homePath() + QStringLiteral("/Music");
    }
#ifndef Q_OS_ANDROID
    // Only mkpath on desktop — on Android the shared Music dir must not be
    // created by us (and /sdcard/Music exists on every install).
    QDir().mkpath(m_mediaFolder);
#endif

    m_fuelTankCapacity      = static_cast<float>(s(QStringLiteral("fuelTankCapacity")).toDouble());
    m_bottomBarOrientation  = s(QStringLiteral("bottomBarOrientation")).toString();
    m_showBottomBarMediaControls = s(QStringLiteral("showBottomBarMediaControls")).toBool();

    // Home OBD parameters (JSON array -> QVariantList)
    {
        QJsonArray arr = s(QStringLiteral("homeOBDParameters")).toArray();
        for (const auto &v : arr)
            m_homeObdParameters.append(v.toString());
    }

    // Spotify
    m_spotifyClientId     = s(QStringLiteral("spotifyClientId")).toString();
    m_spotifyClientSecret = s(QStringLiteral("spotifyClientSecret")).toString();

    // Media source
    m_mediaSource = s(QStringLiteral("mediaSource")).toString();

    // Auto-play / resume
    m_autoPlayOnStartup   = s(QStringLiteral("autoPlayOnStartup")).toBool();
    m_persistShuffleState = s(QStringLiteral("persistShuffleState")).toBool();
    m_lastShuffleState    = s(QStringLiteral("lastShuffleState")).toBool();
    m_lastPlayedSong      = s(QStringLiteral("lastPlayedSong")).toString();
    m_lastPlayedPosition  = s(QStringLiteral("lastPlayedPosition")).toInt();
    m_lastPlayedPlaylist  = s(QStringLiteral("lastPlayedPlaylist")).toString();

    // Window
    m_windowState              = s(QStringLiteral("windowState")).toString();
    m_musicButtonDefaultPage   = s(QStringLiteral("musicButtonDefaultPage")).toString();
    m_returnToLibraryAfterSelection = s(QStringLiteral("returnToLibraryAfterSelection")).toBool();

    // Android Auto / Phone Mirror
    m_androidAutoEnabled  = s(QStringLiteral("androidAutoEnabled")).toBool();
    m_phoneMirrorEnabled  = s(QStringLiteral("phoneMirrorEnabled")).toBool();
    m_scrcpyAudioEnabled  = s(QStringLiteral("scrcpyAudioEnabled")).toBool();
    m_scrcpyDisplaySize   = s(QStringLiteral("scrcpyDisplaySize")).toString();
    m_scrcpyAudioGain     = s(QStringLiteral("scrcpyAudioGain")).toDouble();
    m_scrcpyAudioDuckEnabled = s(QStringLiteral("scrcpyAudioDuckEnabled")).toBool();
    m_scrcpyAudioDuckLevel   = s(QStringLiteral("scrcpyAudioDuckLevel")).toDouble();

    // Settings menu visibility
    {
        QJsonObject vis = s(QStringLiteral("settingsMenuVisibility")).toObject();
        // Ensure all default sections exist
        QJsonObject defVis = m_defaultSettings.value(QStringLiteral("settingsMenuVisibility")).toObject();
        for (auto it = defVis.begin(); it != defVis.end(); ++it) {
            if (!vis.contains(it.key()))
                vis[it.key()] = it.value();
        }
        for (auto it = vis.begin(); it != vis.end(); ++it)
            m_settingsMenuVisibility[it.key()] = it.value().toVariant();
    }

    // ESP32
    m_esp32VolumeEnabled   = s(QStringLiteral("esp32VolumeEnabled")).toBool();
    m_esp32VolumePort      = s(QStringLiteral("esp32VolumePort")).toString();
    m_esp32VolumeStepSize  = s(QStringLiteral("esp32VolumeStepSize")).toDouble();
    m_esp32AutoReconnect   = s(QStringLiteral("esp32AutoReconnect")).toBool();
    m_esp32LedSleepEnabled = s(QStringLiteral("esp32LedSleepEnabled")).toBool();
    m_esp32LedColorMode    = s(QStringLiteral("esp32LedColorMode")).toString();
    m_esp32LedStaticColor  = s(QStringLiteral("esp32LedStaticColor")).toString();

    // Waveform
    m_showWaveformVisualizer = s(QStringLiteral("showWaveformVisualizer")).toBool();

    // 3D effects
    m_show3DButtonTilt        = s(QStringLiteral("show3DButtonTilt")).toBool();
    m_show3DAlbumPreview      = s(QStringLiteral("show3DAlbumPreview")).toBool();
    m_roundedAlbumArt         = s(QStringLiteral("roundedAlbumArt")).toBool();
    m_albumArtCornerRadius    = s(QStringLiteral("albumArtCornerRadius")).toInt();
    m_showAlbumArtShadow      = s(QStringLiteral("showAlbumArtShadow")).toBool();
    m_vinylRecordMode         = s(QStringLiteral("vinylRecordMode")).toBool();
    m_albumArtTransition      = s(QStringLiteral("albumArtTransition")).toString();
    m_backgroundOverlayOpacity = s(QStringLiteral("backgroundOverlayOpacity")).toInt();
    m_sideCardOpacity         = s(QStringLiteral("sideCardOpacity")).toDouble();
    m_sideCardAngle           = s(QStringLiteral("sideCardAngle")).toInt();
    m_buttonTiltDuration      = s(QStringLiteral("buttonTiltDuration")).toInt();
    m_textScrollSpeed         = s(QStringLiteral("textScrollSpeed")).toInt();

    // Layout
    m_settingsLayoutStyle = s(QStringLiteral("settingsLayoutStyle")).toString();

    // IMU
    m_imuEnabled = s(QStringLiteral("imuEnabled")).toBool();

    // Gesture
    m_gestureSensorEnabled = s(QStringLiteral("gestureSensorEnabled")).toBool();
    {
        QJsonObject gm = s(QStringLiteral("gestureMapping")).toObject();
        for (auto it = gm.begin(); it != gm.end(); ++it)
            m_gestureMapping[it.key()] = it.value().toVariant();
    }
    m_gestureVolumeStep = s(QStringLiteral("gestureVolumeStep")).toInt();
    m_gestureCooldown   = s(QStringLiteral("gestureCooldown")).toInt();

    // Pinned settings
    {
        QJsonArray arr = s(QStringLiteral("pinnedSettings")).toArray();
        for (const auto &v : arr)
            m_pinnedSettings.append(v.toString());
    }

    // Current volume: from startUpVolume, converted to 0-100 scale
    m_currentVolume = static_cast<int>(std::round(std::sqrt(static_cast<double>(m_startVolume)) * 100.0));

    // Last settings section (migrate clockSettings -> displaySettings)
    m_lastSettingsSection = s(QStringLiteral("lastSettingsSection")).toString();
    if (m_lastSettingsSection == QStringLiteral("clockSettings"))
        m_lastSettingsSection = QStringLiteral("displaySettings");

    // OBD parameters
    {
        QJsonObject stored = s(QStringLiteral("obdParameters")).toObject();
        QJsonObject defaults = m_defaultSettings.value(QStringLiteral("obdParameters")).toObject();
        // Merge: stored wins, but add missing from defaults
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            if (!stored.contains(it.key()))
                stored[it.key()] = it.value();
        }
        for (auto it = stored.begin(); it != stored.end(); ++it)
            m_obdParameters[it.key()] = it.value().toVariant();
    }

    // Supported OBD parameters
    {
        QJsonArray arr = s(QStringLiteral("supportedOBDParameters")).toArray();
        for (const auto &v : arr)
            m_supportedObdParameters.append(v.toString());
    }

    // Directory history
    {
        QJsonArray arr = s(QStringLiteral("directoryHistory")).toArray();
        for (const auto &v : arr)
            m_directoryHistory.append(v.toString());
    }

    // Album art colors — persisted so the last-extracted palette is reapplied
    // on startup before a fresh extraction runs (avoids flashing the placeholder).
    m_albumArtColors = s(QStringLiteral("albumArtColors")).toString();

    // --- OBD parameter debounce timer ---
    m_obdParamsSaveTimer.setSingleShot(true);
    m_obdParamsSaveTimer.setInterval(800);
    connect(&m_obdParamsSaveTimer, &QTimer::timeout, this, &SettingsManager::flushObdParameters);
}

// ---------------------------------------------------------------------------
// File locking helpers
// ---------------------------------------------------------------------------
static void lockFile(QFile &f, bool exclusive)
{
#ifdef Q_OS_WIN
    Q_UNUSED(exclusive);
    // Windows: LockFileEx
    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(f.handle()));
    OVERLAPPED ov = {};
    DWORD flags = LOCKFILE_FAIL_IMMEDIATELY;
    if (exclusive)
        flags |= LOCKFILE_EXCLUSIVE_LOCK;
    LockFileEx(hFile, flags, 0, 1, 0, &ov);
#else
    int op = exclusive ? (LOCK_EX | LOCK_NB) : (LOCK_SH | LOCK_NB);
    flock(f.handle(), op);
#endif
}

static void unlockFile(QFile &f)
{
#ifdef Q_OS_WIN
    HANDLE hFile = reinterpret_cast<HANDLE>(_get_osfhandle(f.handle()));
    OVERLAPPED ov = {};
    UnlockFileEx(hFile, 0, 1, 0, &ov);
#else
    flock(f.handle(), LOCK_UN);
#endif
}

// ---------------------------------------------------------------------------
// setFilePermissions — chmod 600 on Unix
// ---------------------------------------------------------------------------
void SettingsManager::setFilePermissions(const QString &filepath)
{
#ifndef Q_OS_WIN
    QFile::setPermissions(filepath,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#else
    Q_UNUSED(filepath);
#endif
}

// ---------------------------------------------------------------------------
// validateSettings — sanitize loaded JSON against defaults
// ---------------------------------------------------------------------------
QJsonObject SettingsManager::validateSettings(const QJsonObject &settings)
{
    if (settings.isEmpty())
        return m_defaultSettings;

    QJsonObject validated = m_defaultSettings;

    for (auto it = m_defaultSettings.begin(); it != m_defaultSettings.end(); ++it) {
        const QString &key = it.key();
        if (!settings.contains(key))
            continue;

        QJsonValue val     = settings.value(key);
        QJsonValue defVal  = it.value();

        // Type-check against default
        if (defVal.isBool()) {
            if (val.isBool())
                validated[key] = val;
        } else if (defVal.isDouble()) {
            if (val.isDouble())
                validated[key] = val;
        } else if (defVal.isString()) {
            if (val.isString())
                validated[key] = val;
        } else if (defVal.isObject()) {
            if (val.isObject())
                validated[key] = val;
        } else if (defVal.isArray()) {
            if (val.isArray())
                validated[key] = val;
        } else {
            validated[key] = val;
        }
    }

    // Copy extra keys not in defaults (forward compatibility)
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        if (!m_defaultSettings.contains(it.key()))
            validated[it.key()] = it.value();
    }

    return validated;
}

// ---------------------------------------------------------------------------
// loadSettings — read JSON file with file locking
// ---------------------------------------------------------------------------
QJsonObject SettingsManager::loadSettings()
{
    // After the constructor has read the file once, m_settings is the
    // authoritative copy: every setter goes through saveSettings(), which
    // updates it and schedules one disk write. Re-reading the file per key
    // (as this used to) cost a parse + validate on every slider release and
    // every track change, and always returned what we had just written.
    if (m_settingsLoaded)
        return m_settings;
    return readSettingsFromDisk();
}

QJsonObject SettingsManager::readSettingsFromDisk()
{
    QFile f(m_settingsFile);
    if (!f.exists()) {
        writeSettingsToDisk(m_defaultSettings);
        return m_defaultSettings;
    }

    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(lcSettings) << "Cannot open settings file for reading:" << m_settingsFile;
        return m_defaultSettings;
    }

    lockFile(f, false);

    QByteArray data = f.readAll();
    unlockFile(f);
    f.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qCWarning(lcSettings) << "Settings file corrupted:" << err.errorString();
        return m_defaultSettings;
    }

    return validateSettings(doc.object());
}

// ---------------------------------------------------------------------------
// saveSettings — update the in-memory copy, write to disk once things settle
// ---------------------------------------------------------------------------
void SettingsManager::saveSettings(const QJsonObject &settings)
{
    m_settings = validateSettings(settings);
    if (!m_settingsLoaded) {
        // Constructor path (first run, repair): write straight away.
        writeSettingsToDisk(m_settings);
        return;
    }
    // Coalesce bursts (album colours per track, several toggles in a row,
    // a slider drag) into a single atomic write; also spares the SD card.
    m_savePending = true;
    m_saveTimer.start();
}

void SettingsManager::flushPendingSave()
{
    if (!m_savePending)
        return;
    m_saveTimer.stop();
    m_savePending = false;
    writeSettingsToDisk(m_settings);
}

// ---------------------------------------------------------------------------
// writeSettingsToDisk — atomic write (temp + rename) with file locking
// ---------------------------------------------------------------------------
void SettingsManager::writeSettingsToDisk(const QJsonObject &settings)
{
    QJsonObject validated = validateSettings(settings);
    QJsonDocument doc(validated);

    QString dirName = QFileInfo(m_settingsFile).absolutePath();

    // Write to a temp file, then rename
    QString tempPath = dirName + QStringLiteral("/settings_tmp_XXXXXX.json");
    QTemporaryFile tmp(tempPath);
    tmp.setAutoRemove(false);

    if (!tmp.open()) {
        qCWarning(lcSettings) << "Cannot create temp file for settings:" << tmp.errorString();
        // Fallback: direct write
        QFile direct(m_settingsFile);
        if (direct.open(QIODevice::WriteOnly | QIODevice::Text)) {
            direct.write(doc.toJson(QJsonDocument::Indented));
            direct.close();
            setFilePermissions(m_settingsFile);
        }
        return;
    }

    lockFile(tmp, true);
    tmp.write(doc.toJson(QJsonDocument::Indented));
    unlockFile(tmp);

    QString tmpName = tmp.fileName();
    tmp.close();

    setFilePermissions(tmpName);

    // Atomic rename
#ifdef Q_OS_WIN
    // Windows cannot rename over existing file
    if (QFile::exists(m_settingsFile))
        QFile::remove(m_settingsFile);
#endif
    if (!QFile::rename(tmpName, m_settingsFile)) {
        qCDebug(lcSettings) << "Atomic rename unavailable, using direct write";
        QFile::remove(tmpName);
        QFile direct(m_settingsFile);
        if (direct.open(QIODevice::WriteOnly | QIODevice::Text)) {
            direct.write(doc.toJson(QJsonDocument::Indented));
            direct.close();
            setFilePermissions(m_settingsFile);
        }
    }
}

// ---------------------------------------------------------------------------
// updateSetting — load, update one key, save
// ---------------------------------------------------------------------------
void SettingsManager::updateSetting(const QString &key, const QVariant &value)
{
    QJsonObject settings = loadSettings();
    settings[key] = QJsonValue::fromVariant(value);
    saveSettings(settings);
}

// =========================================================================
// Property getters
// =========================================================================

// --- Device ---
QString SettingsManager::deviceName() const        { return m_deviceName; }
QString SettingsManager::fontSetting() const       { return m_fontSetting; }

// --- Theme / Display ---
QString SettingsManager::themeSetting() const      { return m_themeSetting; }
float   SettingsManager::uiScale() const           { return m_uiScale; }
int     SettingsManager::colorTransitionMs() const { return m_colorTransitionMs; }
bool    SettingsManager::songLengthTransition() const { return m_songLengthTransition; }
bool    SettingsManager::showClock() const         { return m_showClock; }
bool    SettingsManager::clockFormat24Hour() const { return m_clockFormat24Hour; }
bool    SettingsManager::clockShowSeconds() const  { return m_clockShowSeconds; }
int     SettingsManager::clockSize() const         { return m_clockSize; }
QString SettingsManager::backgroundGrid() const    { return m_backgroundGrid; }
int     SettingsManager::screenWidth() const       { return m_screenWidth; }
int     SettingsManager::screenHeight() const      { return m_screenHeight; }
int     SettingsManager::backgroundBlurRadius() const { return m_backgroundBlurRadius; }
QString SettingsManager::bottomBarOrientation() const  { return m_bottomBarOrientation; }
bool    SettingsManager::showBottomBarMediaControls() const { return m_showBottomBarMediaControls; }
QString SettingsManager::settingsLayoutStyle() const { return m_settingsLayoutStyle; }
QString SettingsManager::albumArtColors() const    { return m_albumArtColors; }

QString SettingsManager::settingsMenuVisibility() const
{
    QJsonObject obj;
    for (auto it = m_settingsMenuVisibility.begin(); it != m_settingsMenuVisibility.end(); ++it)
        obj[it.key()] = QJsonValue::fromVariant(it.value());
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// --- Media ---
float   SettingsManager::startUpVolume() const     { return m_startVolume; }
int     SettingsManager::currentVolume() const     { return m_currentVolume; }
QString SettingsManager::mediaFolder() const       { return m_mediaFolder; }
QString SettingsManager::mediaSource() const       { return m_mediaSource; }
bool    SettingsManager::autoPlayOnStartup() const { return m_autoPlayOnStartup; }
bool    SettingsManager::persistShuffleState() const { return m_persistShuffleState; }
bool    SettingsManager::lastShuffleState() const  { return m_lastShuffleState; }
QString SettingsManager::lastPlayedSong() const    { return m_lastPlayedSong; }
int     SettingsManager::lastPlayedPosition() const { return m_lastPlayedPosition; }
QString SettingsManager::lastPlayedPlaylist() const { return m_lastPlayedPlaylist; }
QString SettingsManager::musicButtonDefaultPage() const { return m_musicButtonDefaultPage; }
bool    SettingsManager::returnToLibraryAfterSelection() const { return m_returnToLibraryAfterSelection; }
bool    SettingsManager::showWaveformVisualizer() const { return m_showWaveformVisualizer; }

// --- 3D effects ---
bool    SettingsManager::show3DButtonTilt() const        { return m_show3DButtonTilt; }
bool    SettingsManager::show3DAlbumPreview() const      { return m_show3DAlbumPreview; }
bool    SettingsManager::roundedAlbumArt() const         { return m_roundedAlbumArt; }
int     SettingsManager::albumArtCornerRadius() const    { return m_albumArtCornerRadius; }
bool    SettingsManager::showAlbumArtShadow() const      { return m_showAlbumArtShadow; }
bool    SettingsManager::vinylRecordMode() const         { return m_vinylRecordMode; }
QString SettingsManager::albumArtTransition() const      { return m_albumArtTransition; }
int     SettingsManager::backgroundOverlayOpacity() const { return m_backgroundOverlayOpacity; }
double  SettingsManager::sideCardOpacity() const         { return m_sideCardOpacity; }
int     SettingsManager::sideCardAngle() const           { return m_sideCardAngle; }
int     SettingsManager::buttonTiltDuration() const      { return m_buttonTiltDuration; }
int     SettingsManager::textScrollSpeed() const         { return m_textScrollSpeed; }

// --- OBD ---
QString SettingsManager::obdBluetoothPort() const        { return m_obdBluetoothPort; }
QString SettingsManager::obdSavedAdapters() const        { return m_obdSavedAdapters; }
bool    SettingsManager::obdFastMode() const             { return m_obdFastMode; }
int     SettingsManager::obdAutoReconnectAttempts() const { return m_obdAutoReconnectAttempts; }
float   SettingsManager::fuelTankCapacity() const        { return m_fuelTankCapacity; }
QVariantList SettingsManager::homeOBDParameters() const  { return m_homeObdParameters; }

QVariantList SettingsManager::customThemes() const
{
    QJsonObject settings = const_cast<SettingsManager *>(this)->loadSettings();
    QVariantList names;
    if (settings.contains(QStringLiteral("customThemes"))) {
        QJsonObject ct = settings.value(QStringLiteral("customThemes")).toObject();
        for (auto it = ct.begin(); it != ct.end(); ++it)
            names.append(it.key());
    }
    return names;
}

QVariantList SettingsManager::directoryHistory() const { return m_directoryHistory; }

// --- Window ---
QString SettingsManager::windowState() const         { return m_windowState; }
QString SettingsManager::lastSettingsSection() const { return m_lastSettingsSection; }

// --- Android Auto / Phone Mirror ---
bool    SettingsManager::androidAutoEnabled() const  { return m_androidAutoEnabled; }
bool    SettingsManager::phoneMirrorEnabled() const  { return m_phoneMirrorEnabled; }
bool    SettingsManager::scrcpyAudioEnabled() const  { return m_scrcpyAudioEnabled; }
QString SettingsManager::scrcpyDisplaySize() const   { return m_scrcpyDisplaySize; }
double  SettingsManager::scrcpyAudioGain() const     { return m_scrcpyAudioGain; }
bool    SettingsManager::scrcpyAudioDuckEnabled() const { return m_scrcpyAudioDuckEnabled; }
double  SettingsManager::scrcpyAudioDuckLevel() const { return m_scrcpyAudioDuckLevel; }

// --- ESP32 ---
bool    SettingsManager::esp32VolumeEnabled() const    { return m_esp32VolumeEnabled; }
QString SettingsManager::esp32VolumePort() const       { return m_esp32VolumePort; }
double  SettingsManager::esp32VolumeStepSize() const   { return m_esp32VolumeStepSize; }
bool    SettingsManager::esp32AutoReconnect() const    { return m_esp32AutoReconnect; }
bool    SettingsManager::esp32LedSleepEnabled() const  { return m_esp32LedSleepEnabled; }
QString SettingsManager::esp32LedColorMode() const     { return m_esp32LedColorMode; }
QString SettingsManager::esp32LedStaticColor() const   { return m_esp32LedStaticColor; }

// --- IMU ---
bool    SettingsManager::imuEnabled() const            { return m_imuEnabled; }

// --- Gesture ---
bool    SettingsManager::gestureSensorEnabled() const  { return m_gestureSensorEnabled; }
int     SettingsManager::gestureVolumeStep() const     { return m_gestureVolumeStep; }
int     SettingsManager::gestureCooldown() const       { return m_gestureCooldown; }

// --- Pinned ---
QVariantList SettingsManager::pinnedSettings() const   { return m_pinnedSettings; }

// =========================================================================
// Save / setter slots
// =========================================================================

// --- Device ---
void SettingsManager::save_device_name(const QString &name)
{
    qCDebug(lcSettings) << "Saving device name:" << name;
    m_deviceName = name;
    updateSetting(QStringLiteral("deviceName"), name);
    emit deviceNameChanged(name);
}

void SettingsManager::save_font_setting(const QString &font)
{
    qCDebug(lcSettings) << "Saving font setting:" << font;
    m_fontSetting = font;
    updateSetting(QStringLiteral("fontSetting"), font);
    emit fontSettingChanged(font);
}

// --- Theme / Display ---
void SettingsManager::save_theme_setting(const QString &theme)
{
    qCDebug(lcSettings) << "Saving theme setting:" << theme;
    m_themeSetting = theme;
    updateSetting(QStringLiteral("themeSetting"), theme);
    emit themeSettingChanged(theme);
}

void SettingsManager::save_ui_scale(float scale)
{
    qCDebug(lcSettings) << "Saving UI scale:" << scale;
    m_uiScale = scale;
    updateSetting(QStringLiteral("uiScale"), static_cast<double>(scale));
    emit uiScaleChanged(scale);
}

void SettingsManager::save_color_transition_ms(int ms)
{
    qCDebug(lcSettings) << "Saving color transition ms:" << ms;
    m_colorTransitionMs = ms;
    updateSetting(QStringLiteral("colorTransitionMs"), ms);
    emit colorTransitionMsChanged(ms);
}

void SettingsManager::save_song_length_transition(bool enabled)
{
    qCDebug(lcSettings) << "Saving song length transition:" << enabled;
    m_songLengthTransition = enabled;
    updateSetting(QStringLiteral("songLengthTransition"), enabled);
    emit songLengthTransitionChanged(enabled);
}

void SettingsManager::save_show_clock(bool show)
{
    qCDebug(lcSettings) << "Saving show clock:" << show;
    m_showClock = show;
    updateSetting(QStringLiteral("showClock"), show);
    emit showClockChanged(show);
}

void SettingsManager::save_clock_format(bool is24hour)
{
    qCDebug(lcSettings) << "Saving clock format:" << is24hour;
    m_clockFormat24Hour = is24hour;
    updateSetting(QStringLiteral("clockFormat24Hour"), is24hour);
    emit clockFormatChanged(is24hour);
}

void SettingsManager::save_clock_show_seconds(bool show)
{
    qCDebug(lcSettings) << "Saving clock show seconds:" << show;
    m_clockShowSeconds = show;
    updateSetting(QStringLiteral("clockShowSeconds"), show);
    emit clockShowSecondsChanged(show);
}

void SettingsManager::save_clock_size(int size)
{
    qCDebug(lcSettings) << "Saving clock size:" << size;
    m_clockSize = size;
    updateSetting(QStringLiteral("clockSize"), size);
    emit clockSizeChanged(size);
}

void SettingsManager::save_background_grid(const QString &grid)
{
    qCDebug(lcSettings) << "Saving background grid:" << grid;
    m_backgroundGrid = grid;
    updateSetting(QStringLiteral("backgroundGrid"), grid);
    emit backgroundGridChanged(grid);
}

void SettingsManager::save_screen_width(int width)
{
    m_screenWidth = width;
    updateSetting(QStringLiteral("screenWidth"), width);
    emit screenWidthChanged(width);
}

void SettingsManager::save_screen_height(int height)
{
    m_screenHeight = height;
    updateSetting(QStringLiteral("screenHeight"), height);
    emit screenHeightChanged(height);
}

void SettingsManager::save_background_blur_radius(int radius)
{
    qCDebug(lcSettings) << "Saving background blur radius:" << radius;
    m_backgroundBlurRadius = radius;
    updateSetting(QStringLiteral("backgroundBlurRadius"), radius);
    emit backgroundBlurRadiusChanged(radius);
}

void SettingsManager::save_bottom_bar_orientation(const QString &orientation)
{
    qCDebug(lcSettings) << "Saving bottom bar orientation:" << orientation;
    m_bottomBarOrientation = orientation;
    updateSetting(QStringLiteral("bottomBarOrientation"), orientation);
    emit bottomBarOrientationChanged(orientation);
}

void SettingsManager::save_show_bottom_bar_media_controls(bool show)
{
    qCDebug(lcSettings) << "Saving show bottom bar media controls:" << show;
    m_showBottomBarMediaControls = show;
    updateSetting(QStringLiteral("showBottomBarMediaControls"), show);
    emit showBottomBarMediaControlsChanged(show);
}

void SettingsManager::save_settings_layout_style(const QString &style)
{
    static const QStringList valid = {
        QStringLiteral("Carousel"), QStringLiteral("Sidebar"),
        QStringLiteral("Hub"), QStringLiteral("Dashboard")
    };
    if (!valid.contains(style))
        return;
    qCDebug(lcSettings) << "Saving settings layout style:" << style;
    m_settingsLayoutStyle = style;
    updateSetting(QStringLiteral("settingsLayoutStyle"), style);
    emit settingsLayoutStyleChanged(style);
}

void SettingsManager::set_album_art_colors(const QString &colorsJson)
{
    qCDebug(lcSettings) << "set_album_art_colors called, length:" << colorsJson.length();
    m_albumArtColors = colorsJson;
    updateSetting(QStringLiteral("albumArtColors"), colorsJson);
    emit albumArtColorsChanged(colorsJson);
}

// --- Media ---
void SettingsManager::save_start_volume(float volume)
{
    qCDebug(lcSettings) << "Saving volume setting:" << volume;
    m_startVolume = volume;
    updateSetting(QStringLiteral("startUpVolume"), static_cast<double>(volume));
    emit startUpVolumeChanged(QString::number(static_cast<double>(volume)));
}

void SettingsManager::setCurrentVolume(int volume)
{
    if (m_currentVolume != volume) {
        m_currentVolume = volume;
        emit currentVolumeChanged(volume);
    }
}

void SettingsManager::save_media_folder(const QString &folderPath)
{
    if (folderPath == m_mediaFolder) {
        qCDebug(lcSettings) << "Media folder unchanged, skipping:" << folderPath;
        return;
    }
    qCDebug(lcSettings) << "Saving media folder path:" << folderPath;
    m_mediaFolder = folderPath;
    updateSetting(QStringLiteral("mediaFolder"), folderPath);
    emit mediaFolderChanged(folderPath);
}

void SettingsManager::set_media_source(const QString &source)
{
    if (source != QStringLiteral("local") && source != QStringLiteral("spotify"))
        return;
    if (source != m_mediaSource) {
        m_mediaSource = source;
        QJsonObject settings = loadSettings();
        settings[QStringLiteral("mediaSource")] = source;
        saveSettings(settings);
        emit mediaSourceChanged(source);
    }
}

void SettingsManager::toggle_media_source()
{
    QString newSource = (m_mediaSource == QStringLiteral("local"))
        ? QStringLiteral("spotify") : QStringLiteral("local");
    set_media_source(newSource);
}

void SettingsManager::save_auto_play_on_startup(bool enabled)
{
    qCDebug(lcSettings) << "Saving auto-play on startup:" << enabled;
    m_autoPlayOnStartup = enabled;
    updateSetting(QStringLiteral("autoPlayOnStartup"), enabled);
    emit autoPlayOnStartupChanged(enabled);
}

void SettingsManager::save_persist_shuffle_state(bool enabled)
{
    qCDebug(lcSettings) << "Saving persist shuffle state:" << enabled;
    m_persistShuffleState = enabled;
    updateSetting(QStringLiteral("persistShuffleState"), enabled);
    emit persistShuffleStateChanged(enabled);
}

void SettingsManager::save_last_shuffle_state(bool enabled)
{
    m_lastShuffleState = enabled;
    updateSetting(QStringLiteral("lastShuffleState"), enabled);
    emit lastShuffleStateChanged(enabled);
}

void SettingsManager::save_last_played_song(const QString &filename)
{
    m_lastPlayedSong = filename;
    updateSetting(QStringLiteral("lastPlayedSong"), filename);
    emit lastPlayedSongChanged(filename);
}

void SettingsManager::save_last_played_position(int positionMs)
{
    m_lastPlayedPosition = positionMs;
    updateSetting(QStringLiteral("lastPlayedPosition"), positionMs);
    emit lastPlayedPositionChanged(positionMs);
}

void SettingsManager::save_last_played_playlist(const QString &playlistName)
{
    m_lastPlayedPlaylist = playlistName;
    updateSetting(QStringLiteral("lastPlayedPlaylist"), playlistName);
    emit lastPlayedPlaylistChanged(playlistName);
}

void SettingsManager::save_playback_state(const QString &song, int positionMs, const QString &playlist)
{
    m_lastPlayedSong     = song;
    m_lastPlayedPosition = positionMs;
    m_lastPlayedPlaylist = playlist;

    QJsonObject settings = loadSettings();
    settings[QStringLiteral("lastPlayedSong")]     = song;
    settings[QStringLiteral("lastPlayedPosition")]  = positionMs;
    settings[QStringLiteral("lastPlayedPlaylist")]  = playlist;
    saveSettings(settings);
}

void SettingsManager::save_music_button_default_page(const QString &page)
{
    static const QStringList valid = {
        QStringLiteral("mediaRoom"), QStringLiteral("mediaPlayer")
    };
    if (!valid.contains(page))
        return;
    qCDebug(lcSettings) << "Saving music button default page:" << page;
    m_musicButtonDefaultPage = page;
    updateSetting(QStringLiteral("musicButtonDefaultPage"), page);
    emit musicButtonDefaultPageChanged(page);
}

void SettingsManager::save_return_to_library_after_selection(bool returnToLibrary)
{
    qCDebug(lcSettings) << "Saving return to library after selection:" << returnToLibrary;
    m_returnToLibraryAfterSelection = returnToLibrary;
    updateSetting(QStringLiteral("returnToLibraryAfterSelection"), returnToLibrary);
    emit returnToLibraryAfterSelectionChanged(returnToLibrary);
}

void SettingsManager::save_show_waveform_visualizer(bool enabled)
{
    qCDebug(lcSettings) << "Saving show waveform visualizer:" << enabled;
    m_showWaveformVisualizer = enabled;
    updateSetting(QStringLiteral("showWaveformVisualizer"), enabled);
    emit showWaveformVisualizerChanged(enabled);
}

// --- 3D visual effects ---
void SettingsManager::save_show_3d_button_tilt(bool enabled)
{
    m_show3DButtonTilt = enabled;
    updateSetting(QStringLiteral("show3DButtonTilt"), enabled);
    emit show3DButtonTiltChanged(enabled);
}

void SettingsManager::save_show_3d_album_preview(bool enabled)
{
    m_show3DAlbumPreview = enabled;
    updateSetting(QStringLiteral("show3DAlbumPreview"), enabled);
    emit show3DAlbumPreviewChanged(enabled);
}

void SettingsManager::save_rounded_album_art(bool enabled)
{
    m_roundedAlbumArt = enabled;
    updateSetting(QStringLiteral("roundedAlbumArt"), enabled);
    emit roundedAlbumArtChanged(enabled);
}

void SettingsManager::save_album_art_corner_radius(int radius)
{
    m_albumArtCornerRadius = qBound(2, radius, 32);
    updateSetting(QStringLiteral("albumArtCornerRadius"), m_albumArtCornerRadius);
    emit albumArtCornerRadiusChanged(m_albumArtCornerRadius);
}

void SettingsManager::save_show_album_art_shadow(bool enabled)
{
    m_showAlbumArtShadow = enabled;
    updateSetting(QStringLiteral("showAlbumArtShadow"), enabled);
    emit showAlbumArtShadowChanged(enabled);
}

void SettingsManager::save_vinyl_record_mode(bool enabled)
{
    m_vinylRecordMode = enabled;
    updateSetting(QStringLiteral("vinylRecordMode"), enabled);
    emit vinylRecordModeChanged(enabled);
}

void SettingsManager::save_album_art_transition(const QString &transition)
{
    static const QStringList valid = {
        QStringLiteral("Crossfade"), QStringLiteral("Slide"),
        QStringLiteral("Vinyl Lift"), QStringLiteral("Dissolve"),
        QStringLiteral("Flip"),
        QStringLiteral("Coverflow"), QStringLiteral("Conveyor"),
        QStringLiteral("Stack"), QStringLiteral("Depth"),
        QStringLiteral("Swing")
    };
    if (!valid.contains(transition))
        return;
    m_albumArtTransition = transition;
    updateSetting(QStringLiteral("albumArtTransition"), transition);
    emit albumArtTransitionChanged(transition);
}

void SettingsManager::save_background_overlay_opacity(int opacity)
{
    m_backgroundOverlayOpacity = qBound(0, opacity, 100);
    updateSetting(QStringLiteral("backgroundOverlayOpacity"), m_backgroundOverlayOpacity);
    emit backgroundOverlayOpacityChanged(m_backgroundOverlayOpacity);
}

void SettingsManager::save_side_card_opacity(double opacity)
{
    m_sideCardOpacity = qBound(0.1, opacity, 1.0);
    updateSetting(QStringLiteral("sideCardOpacity"), m_sideCardOpacity);
    emit sideCardOpacityChanged(m_sideCardOpacity);
}

void SettingsManager::save_side_card_angle(int angle)
{
    m_sideCardAngle = qBound(5, angle, 60);
    updateSetting(QStringLiteral("sideCardAngle"), m_sideCardAngle);
    emit sideCardAngleChanged(m_sideCardAngle);
}

void SettingsManager::save_button_tilt_duration(int duration)
{
    m_buttonTiltDuration = qBound(50, duration, 500);
    updateSetting(QStringLiteral("buttonTiltDuration"), m_buttonTiltDuration);
    emit buttonTiltDurationChanged(m_buttonTiltDuration);
}

void SettingsManager::save_text_scroll_speed(int speed)
{
    m_textScrollSpeed = qBound(1000, speed, 10000);
    updateSetting(QStringLiteral("textScrollSpeed"), m_textScrollSpeed);
    emit textScrollSpeedChanged(m_textScrollSpeed);
}

// --- OBD ---
void SettingsManager::save_obd_bluetooth_port(const QString &port)
{
    qCDebug(lcSettings) << "Saving OBD Bluetooth port:" << port;
    m_obdBluetoothPort = port;
    updateSetting(QStringLiteral("obdBluetoothPort"), port);
    emit obdBluetoothPortChanged(port);
}

void SettingsManager::save_obd_saved_adapters(const QString &json)
{
    m_obdSavedAdapters = json.isEmpty() ? QStringLiteral("[]") : json;
    updateSetting(QStringLiteral("obdSavedAdapters"), m_obdSavedAdapters);
    emit obdSavedAdaptersChanged(m_obdSavedAdapters);
}

void SettingsManager::add_obd_saved_adapter(const QString &name, const QString &mac)
{
    const QString trimmed = mac.trimmed().toUpper();
    if (trimmed.isEmpty()) return;

    QJsonArray arr = QJsonDocument::fromJson(m_obdSavedAdapters.toUtf8()).array();
    // Remove any existing entry with the same MAC, then prepend the new one
    // so the most recently used adapter is first.
    for (int i = arr.size() - 1; i >= 0; --i) {
        const QJsonObject o = arr.at(i).toObject();
        if (o.value(QStringLiteral("mac")).toString().toUpper() == trimmed) {
            arr.removeAt(i);
        }
    }
    QJsonObject entry;
    entry[QStringLiteral("name")] = name.isEmpty() ? trimmed : name;
    entry[QStringLiteral("mac")] = trimmed;
    arr.prepend(entry);
    // Cap at 8 to keep the chip row sane.
    while (arr.size() > 8) arr.removeLast();

    save_obd_saved_adapters(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void SettingsManager::remove_obd_saved_adapter(const QString &mac)
{
    const QString trimmed = mac.trimmed().toUpper();
    if (trimmed.isEmpty()) return;

    QJsonArray arr = QJsonDocument::fromJson(m_obdSavedAdapters.toUtf8()).array();
    for (int i = arr.size() - 1; i >= 0; --i) {
        const QJsonObject o = arr.at(i).toObject();
        if (o.value(QStringLiteral("mac")).toString().toUpper() == trimmed) {
            arr.removeAt(i);
        }
    }
    save_obd_saved_adapters(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void SettingsManager::save_obd_fast_mode(bool enabled)
{
    qCDebug(lcSettings) << "Saving OBD fast mode:" << enabled;
    m_obdFastMode = enabled;
    updateSetting(QStringLiteral("obdFastMode"), enabled);
    emit obdFastModeChanged(enabled);
}

void SettingsManager::save_obd_auto_reconnect_attempts(int attempts)
{
    qCDebug(lcSettings) << "Saving OBD auto-reconnect attempts:" << attempts;
    m_obdAutoReconnectAttempts = qBound(0, attempts, 10);
    updateSetting(QStringLiteral("obdAutoReconnectAttempts"), m_obdAutoReconnectAttempts);
    emit obdAutoReconnectAttemptsChanged(m_obdAutoReconnectAttempts);
}

void SettingsManager::save_fuel_tank_capacity(float capacity)
{
    qCDebug(lcSettings) << "Saving fuel tank capacity:" << capacity;
    m_fuelTankCapacity = capacity;
    updateSetting(QStringLiteral("fuelTankCapacity"), static_cast<double>(capacity));
    emit fuelTankCapacityChanged(capacity);
}

void SettingsManager::save_obd_parameter_enabled(const QString &parameter, bool enabled)
{
    m_obdParameters[parameter] = enabled;
    m_obdParamsDirty = true;
    m_obdParamsSaveTimer.start();
}

void SettingsManager::flushObdParameters()
{
    if (!m_obdParamsDirty)
        return;

    qCDebug(lcSettings) << "Flushing OBD parameter changes to disk";

    QJsonObject settings = loadSettings();
    QJsonObject params;
    for (auto it = m_obdParameters.begin(); it != m_obdParameters.end(); ++it)
        params[it.key()] = QJsonValue::fromVariant(it.value());
    settings[QStringLiteral("obdParameters")] = params;
    saveSettings(settings);

    m_obdParamsDirty = false;
    emit obdParametersChanged();
}

void SettingsManager::save_supported_obd_parameters(const QVariantList &parameters)
{
    m_supportedObdParameters = parameters;
    QJsonObject settings = loadSettings();
    QJsonArray arr;
    for (const auto &v : parameters)
        arr.append(v.toString());
    settings[QStringLiteral("supportedOBDParameters")] = arr;
    saveSettings(settings);
    emit supportedOBDParametersChanged();
}

void SettingsManager::save_home_obd_parameters(const QVariantList &parameters)
{
    qCDebug(lcSettings) << "Saving home OBD parameters";
    m_homeObdParameters = parameters;
    QJsonObject settings = loadSettings();
    QJsonArray arr;
    for (const auto &v : parameters)
        arr.append(v.toString());
    settings[QStringLiteral("homeOBDParameters")] = arr;
    saveSettings(settings);
    emit homeOBDParametersChanged();
}

// --- Directory history ---
void SettingsManager::save_to_directory_history(const QString &folderPath)
{
    qCDebug(lcSettings) << "Adding directory to history:" << folderPath;
    if (m_directoryHistory.contains(folderPath))
        return;

    m_directoryHistory.prepend(folderPath);
    if (m_directoryHistory.size() > 10)
        m_directoryHistory = m_directoryHistory.mid(0, 10);

    QJsonObject settings = loadSettings();
    QJsonArray arr;
    for (const auto &v : m_directoryHistory)
        arr.append(v.toString());
    settings[QStringLiteral("directoryHistory")] = arr;
    saveSettings(settings);
    emit directoryHistoryChanged();
}

void SettingsManager::remove_from_directory_history(const QString &folderPath)
{
    qCDebug(lcSettings) << "Removing directory from history:" << folderPath;
    int idx = m_directoryHistory.indexOf(folderPath);
    if (idx < 0)
        return;

    m_directoryHistory.removeAt(idx);
    QJsonObject settings = loadSettings();
    QJsonArray arr;
    for (const auto &v : m_directoryHistory)
        arr.append(v.toString());
    settings[QStringLiteral("directoryHistory")] = arr;
    saveSettings(settings);
    emit directoryHistoryChanged();
}

// --- Custom themes ---
void SettingsManager::save_custom_theme(const QString &name, const QString &themeJson,
                                         const QString &albumArtSource)
{
    qCDebug(lcSettings) << "Saving custom theme:" << name;
    QJsonObject settings = loadSettings();
    if (!settings.contains(QStringLiteral("customThemes")))
        settings[QStringLiteral("customThemes")] = QJsonObject();

    QJsonParseError err;
    QJsonObject theme = QJsonDocument::fromJson(themeJson.toUtf8(), &err).object();

    QString artDir = m_appDataDir + QStringLiteral("/theme_art");
    QString safeName = name;
    safeName.replace(QRegularExpression(QStringLiteral("[^\\w\\-]")), QStringLiteral("_"));

    // Handle local file album art
    if (!albumArtSource.isEmpty()
        && !albumArtSource.startsWith(QStringLiteral("http://"))
        && !albumArtSource.startsWith(QStringLiteral("https://")))
    {
        QDir().mkpath(artDir);
        QString localPath = albumArtSource.startsWith(QStringLiteral("file://"))
            ? QUrl(albumArtSource).toLocalFile()
            : albumArtSource;
        if (!localPath.isEmpty() && QFile::exists(localPath)) {
            QString ext = QFileInfo(localPath).suffix();
            if (ext.isEmpty()) ext = QStringLiteral("jpg");
            QString dest = artDir + QStringLiteral("/theme_") + safeName + QStringLiteral(".") + ext;
            QFile::copy(localPath, dest);
            theme[QStringLiteral("albumArt")] = QUrl::fromLocalFile(dest).toString();
            qCDebug(lcSettings) << "Saved theme art to:" << dest;
        }
    }

    // Save immediately
    QJsonObject themes = settings.value(QStringLiteral("customThemes")).toObject();
    themes[name] = theme;
    settings[QStringLiteral("customThemes")] = themes;
    saveSettings(settings);
    emit customThemesChanged();

    // Download remote art in background thread
    if (!albumArtSource.isEmpty()
        && (albumArtSource.startsWith(QStringLiteral("http://"))
            || albumArtSource.startsWith(QStringLiteral("https://"))))
    {
        downloadThemeArtAsync(name, albumArtSource, artDir, safeName);
    }
}

void SettingsManager::downloadThemeArtAsync(const QString &themeName, const QString &url,
                                              const QString &artDir, const QString &safeName)
{
    // Use a thread to download, then poll on the main thread
    m_pendingThemeArtName.clear();
    m_pendingThemeArtUrl.clear();

    QThread *thread = QThread::create([this, themeName, url, artDir, safeName]() {
        QDir().mkpath(artDir);
        QString dest = artDir + QStringLiteral("/theme_") + safeName + QStringLiteral(".jpg");
        // Use QProcess + curl as a portable download approach
        QProcess proc;
        proc.start(QStringLiteral("curl"), {QStringLiteral("-sL"), QStringLiteral("-o"), dest, url});
        proc.waitForFinished(30000);
        if (proc.exitCode() == 0 && QFile::exists(dest)) {
            m_pendingThemeArtName = themeName;
            m_pendingThemeArtUrl  = QUrl::fromLocalFile(dest).toString();
            qCDebug(lcSettings) << "Downloaded theme art to:" << dest;
        } else {
            qCWarning(lcSettings) << "Failed to download theme album art from:" << url;
        }
    });

    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    // Poll for completion
    QTimer *timer = new QTimer(this);
    timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this, timer]() {
        if (!m_pendingThemeArtName.isEmpty()) {
            timer->stop();
            timer->deleteLater();
            QString name = m_pendingThemeArtName;
            QString localUrl = m_pendingThemeArtUrl;
            m_pendingThemeArtName.clear();
            m_pendingThemeArtUrl.clear();

            QJsonObject settings = loadSettings();
            QJsonObject themes = settings.value(QStringLiteral("customThemes")).toObject();
            if (themes.contains(name)) {
                QJsonObject t = themes.value(name).toObject();
                t[QStringLiteral("albumArt")] = localUrl;
                themes[name] = t;
                settings[QStringLiteral("customThemes")] = themes;
                saveSettings(settings);
                emit customThemesChanged();
            }
        }
    });
    timer->start();
    thread->start();
}

void SettingsManager::delete_custom_theme(const QString &name)
{
    qCDebug(lcSettings) << "Deleting custom theme:" << name;
    QJsonObject settings = loadSettings();
    QJsonObject themes = settings.value(QStringLiteral("customThemes")).toObject();
    if (!themes.contains(name))
        return;

    // Clean up album art
    QJsonObject theme = themes.value(name).toObject();
    if (theme.contains(QStringLiteral("albumArt"))) {
        QString artPath = QUrl(theme.value(QStringLiteral("albumArt")).toString()).toLocalFile();
        if (!artPath.isEmpty() && QFile::exists(artPath)) {
            QFile::remove(artPath);
            qCDebug(lcSettings) << "Removed theme art:" << artPath;
        }
    }

    themes.remove(name);
    settings[QStringLiteral("customThemes")] = themes;
    saveSettings(settings);
    emit customThemesChanged();
}

// --- Spotify credentials ---
void SettingsManager::save_spotify_credentials(const QString &clientId, const QString &clientSecret)
{
    qCDebug(lcSettings) << "Saving Spotify credentials";
    m_spotifyClientId     = clientId;
    m_spotifyClientSecret = clientSecret;

    QJsonObject settings = loadSettings();
    settings[QStringLiteral("spotifyClientId")]     = clientId;
    settings[QStringLiteral("spotifyClientSecret")] = clientSecret;
    saveSettings(settings);
    emit spotifyCredentialsChanged();
}

void SettingsManager::clear_spotify_credentials()
{
    m_spotifyClientId.clear();
    m_spotifyClientSecret.clear();

    QJsonObject settings = loadSettings();
    settings[QStringLiteral("spotifyClientId")]     = QString();
    settings[QStringLiteral("spotifyClientSecret")] = QString();
    saveSettings(settings);
    emit spotifyCredentialsChanged();
}

// --- Window / Settings navigation ---
void SettingsManager::save_window_state(const QString &state)
{
    static const QStringList valid = {
        QStringLiteral("windowed"), QStringLiteral("fullscreen"), QStringLiteral("maximized")
    };
    if (!valid.contains(state))
        return;
    m_windowState = state;
    updateSetting(QStringLiteral("windowState"), state);
    emit windowStateChanged(state);
}

void SettingsManager::set_last_settings_section(const QString &section)
{
    static const QStringList valid = {
        QStringLiteral("deviceSettings"), QStringLiteral("mediaSettings"),
        QStringLiteral("displaySettings"), QStringLiteral("obdSettings"),
        QStringLiteral("androidAutoSettings"), QStringLiteral("phoneMirrorSettings"),
        QStringLiteral("volumeKnobSettings"), QStringLiteral("gestureSensorSettings"),
        QStringLiteral("about")
    };
    if (!valid.contains(section))
        return;
    if (section != m_lastSettingsSection) {
        m_lastSettingsSection = section;
        QJsonObject settings = loadSettings();
        settings[QStringLiteral("lastSettingsSection")] = section;
        saveSettings(settings);
        emit lastSettingsSectionChanged(section);
    }
}

// --- Settings section visibility ---
void SettingsManager::save_settings_section_visibility(const QString &section, bool visible)
{
    qCDebug(lcSettings) << "Saving settings section visibility:" << section << "=" << visible;
    m_settingsMenuVisibility[section] = visible;
    QJsonObject settings = loadSettings();
    QJsonObject vis;
    for (auto it = m_settingsMenuVisibility.begin(); it != m_settingsMenuVisibility.end(); ++it)
        vis[it.key()] = QJsonValue::fromVariant(it.value());
    settings[QStringLiteral("settingsMenuVisibility")] = vis;
    saveSettings(settings);
    emit settingsMenuVisibilityChanged();
}

// --- Android Auto / Phone Mirror ---
void SettingsManager::save_android_auto_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving Android Auto enabled:" << enabled;
    m_androidAutoEnabled = enabled;
    updateSetting(QStringLiteral("androidAutoEnabled"), enabled);
    emit androidAutoEnabledChanged(enabled);
}

void SettingsManager::save_phone_mirror_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving Phone Mirror enabled:" << enabled;
    m_phoneMirrorEnabled = enabled;
    updateSetting(QStringLiteral("phoneMirrorEnabled"), enabled);
    emit phoneMirrorEnabledChanged(enabled);
}

void SettingsManager::save_scrcpy_audio_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving scrcpy audio enabled:" << enabled;
    m_scrcpyAudioEnabled = enabled;
    updateSetting(QStringLiteral("scrcpyAudioEnabled"), enabled);
    emit scrcpyAudioEnabledChanged(enabled);
}

void SettingsManager::save_scrcpy_display_size(const QString &size)
{
    qCDebug(lcSettings) << "Saving scrcpy display size:" << size;
    m_scrcpyDisplaySize = size;
    updateSetting(QStringLiteral("scrcpyDisplaySize"), size);
    emit scrcpyDisplaySizeChanged(size);
}

void SettingsManager::save_scrcpy_audio_gain(double gain)
{
    gain = qBound(0.25, gain, 8.0);
    qCDebug(lcSettings) << "Saving scrcpy audio gain:" << gain;
    m_scrcpyAudioGain = gain;
    updateSetting(QStringLiteral("scrcpyAudioGain"), gain);
    emit scrcpyAudioGainChanged(gain);
}

void SettingsManager::save_scrcpy_audio_duck_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving scrcpy audio duck enabled:" << enabled;
    m_scrcpyAudioDuckEnabled = enabled;
    updateSetting(QStringLiteral("scrcpyAudioDuckEnabled"), enabled);
    emit scrcpyAudioDuckEnabledChanged(enabled);
}

void SettingsManager::save_scrcpy_audio_duck_level(double level)
{
    level = qBound(0.0, level, 1.0);
    qCDebug(lcSettings) << "Saving scrcpy audio duck level:" << level;
    m_scrcpyAudioDuckLevel = level;
    updateSetting(QStringLiteral("scrcpyAudioDuckLevel"), level);
    emit scrcpyAudioDuckLevelChanged(level);
}

// --- ESP32 ---
void SettingsManager::save_esp32_volume_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving ESP32 volume enabled:" << enabled;
    m_esp32VolumeEnabled = enabled;
    updateSetting(QStringLiteral("esp32VolumeEnabled"), enabled);
    emit esp32VolumeEnabledChanged(enabled);
}

void SettingsManager::save_esp32_volume_port(const QString &port)
{
    qCDebug(lcSettings) << "Saving ESP32 volume port:" << port;
    m_esp32VolumePort = port;
    updateSetting(QStringLiteral("esp32VolumePort"), port);
    emit esp32VolumePortChanged(port);
}

void SettingsManager::save_esp32_volume_step_size(double stepSize)
{
    qCDebug(lcSettings) << "Saving ESP32 volume step size:" << stepSize;
    double clamped = qBound(0.25, stepSize, 10.0);
    m_esp32VolumeStepSize = std::round(clamped * 4.0) / 4.0;  // nearest 0.25
    updateSetting(QStringLiteral("esp32VolumeStepSize"), m_esp32VolumeStepSize);
    emit esp32VolumeStepSizeChanged(m_esp32VolumeStepSize);
}

void SettingsManager::save_esp32_auto_reconnect(bool enabled)
{
    qCDebug(lcSettings) << "Saving ESP32 auto-reconnect:" << enabled;
    m_esp32AutoReconnect = enabled;
    updateSetting(QStringLiteral("esp32AutoReconnect"), enabled);
    emit esp32AutoReconnectChanged(enabled);
}

void SettingsManager::save_esp32_led_sleep_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving ESP32 LED sleep enabled:" << enabled;
    m_esp32LedSleepEnabled = enabled;
    updateSetting(QStringLiteral("esp32LedSleepEnabled"), enabled);
    emit esp32LedSleepEnabledChanged(enabled);
}

void SettingsManager::save_esp32_led_color_mode(const QString &mode)
{
    if (mode != QStringLiteral("theme") && mode != QStringLiteral("static"))
        return;
    qCDebug(lcSettings) << "Saving ESP32 LED color mode:" << mode;
    m_esp32LedColorMode = mode;
    updateSetting(QStringLiteral("esp32LedColorMode"), mode);
    emit esp32LedColorModeChanged(mode);
}

void SettingsManager::save_esp32_led_static_color(const QString &color)
{
    qCDebug(lcSettings) << "Saving ESP32 LED static color:" << color;
    m_esp32LedStaticColor = color;
    updateSetting(QStringLiteral("esp32LedStaticColor"), color);
    emit esp32LedStaticColorChanged(color);
}

// --- IMU ---
void SettingsManager::save_imu_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving IMU enabled:" << enabled;
    m_imuEnabled = enabled;
    updateSetting(QStringLiteral("imuEnabled"), enabled);
    emit imuEnabledChanged(enabled);
}

// --- Gesture ---
void SettingsManager::save_gesture_sensor_enabled(bool enabled)
{
    qCDebug(lcSettings) << "Saving gesture sensor enabled:" << enabled;
    m_gestureSensorEnabled = enabled;
    updateSetting(QStringLiteral("gestureSensorEnabled"), enabled);
    emit gestureSensorEnabledChanged(enabled);
}

void SettingsManager::save_gesture_action(const QString &gesture, const QString &action)
{
    qCDebug(lcSettings) << "Saving gesture action:" << gesture << "->" << action;
    m_gestureMapping[gesture] = action;
    QJsonObject settings = loadSettings();
    QJsonObject gm;
    for (auto it = m_gestureMapping.begin(); it != m_gestureMapping.end(); ++it)
        gm[it.key()] = it.value().toString();
    settings[QStringLiteral("gestureMapping")] = gm;
    saveSettings(settings);
    emit gestureMappingChanged();
}

void SettingsManager::save_gesture_mapping(const QVariantMap &mapping)
{
    m_gestureMapping = mapping;
    QJsonObject settings = loadSettings();
    QJsonObject gm;
    for (auto it = mapping.begin(); it != mapping.end(); ++it)
        gm[it.key()] = it.value().toString();
    settings[QStringLiteral("gestureMapping")] = gm;
    saveSettings(settings);
    emit gestureMappingChanged();
}

void SettingsManager::reset_gesture_mapping()
{
    QJsonObject defGm = m_defaultSettings.value(QStringLiteral("gestureMapping")).toObject();
    m_gestureMapping.clear();
    for (auto it = defGm.begin(); it != defGm.end(); ++it)
        m_gestureMapping[it.key()] = it.value().toVariant();
    QJsonObject settings = loadSettings();
    settings[QStringLiteral("gestureMapping")] = defGm;
    saveSettings(settings);
    emit gestureMappingChanged();
}

void SettingsManager::save_gesture_volume_step(int step)
{
    qCDebug(lcSettings) << "Saving gesture volume step:" << step;
    m_gestureVolumeStep = qBound(1, step, 100);
    updateSetting(QStringLiteral("gestureVolumeStep"), m_gestureVolumeStep);
    emit gestureVolumeStepChanged(m_gestureVolumeStep);
}

void SettingsManager::save_gesture_cooldown(int ms)
{
    qCDebug(lcSettings) << "Saving gesture cooldown:" << ms << "ms";
    m_gestureCooldown = qBound(100, ms, 2000);
    updateSetting(QStringLiteral("gestureCooldown"), m_gestureCooldown);
    emit gestureCooldownChanged(m_gestureCooldown);
}

// --- Pinned settings ---
void SettingsManager::toggle_pin_setting(const QString &settingId)
{
    int idx = m_pinnedSettings.indexOf(settingId);
    if (idx >= 0) {
        m_pinnedSettings.removeAt(idx);
    } else {
        if (s_settingsRegistry.contains(settingId))
            m_pinnedSettings.append(settingId);
    }
    QJsonObject settings = loadSettings();
    QJsonArray arr;
    for (const auto &v : m_pinnedSettings)
        arr.append(v.toString());
    settings[QStringLiteral("pinnedSettings")] = arr;
    saveSettings(settings);
    emit pinnedSettingsChanged();
}

// --- Generic setting access ---
void SettingsManager::save_setting(const QString &key, const QVariant &value)
{
    QJsonObject settings = loadSettings();
    settings[key] = QJsonValue::fromVariant(value);
    saveSettings(settings);
    emit genericSettingChanged(key);
}

// =========================================================================
// Q_INVOKABLE getter methods
// =========================================================================

QVariant SettingsManager::get_setting(const QString &key)
{
    QJsonObject settings = loadSettings();
    if (settings.contains(key))
        return settings.value(key).toVariant();
    return QVariant();
}

QVariant SettingsManager::get_setting_with_default(const QString &key, const QVariant &defaultValue)
{
    QJsonObject settings = loadSettings();
    if (settings.contains(key))
        return settings.value(key).toVariant();
    return defaultValue;
}

bool SettingsManager::get_obd_parameter_enabled(const QString &parameter, bool defaultVal)
{
    if (m_obdParameters.contains(parameter))
        return m_obdParameters.value(parameter).toBool();
    m_obdParameters[parameter] = defaultVal;
    return defaultVal;
}

QVariantList SettingsManager::get_supported_obd_parameters()
{
    return m_supportedObdParameters;
}

QVariantList SettingsManager::get_directory_history()
{
    return m_directoryHistory;
}

QVariantList SettingsManager::get_home_obd_parameters()
{
    return m_homeObdParameters;
}

QString SettingsManager::get_custom_theme(const QString &name)
{
    QJsonObject settings = loadSettings();
    QJsonObject themes = settings.value(QStringLiteral("customThemes")).toObject();
    if (themes.contains(name))
        return QString::fromUtf8(QJsonDocument(themes.value(name).toObject()).toJson(QJsonDocument::Compact));
    return QStringLiteral("{}");
}

QString SettingsManager::get_spotify_client_id()       { return m_spotifyClientId; }
QString SettingsManager::get_spotify_client_secret()   { return m_spotifyClientSecret; }
bool    SettingsManager::has_spotify_credentials()     { return !m_spotifyClientId.isEmpty() && !m_spotifyClientSecret.isEmpty(); }
QString SettingsManager::get_media_source()            { return m_mediaSource; }
bool    SettingsManager::get_auto_play_on_startup()    { return m_autoPlayOnStartup; }
bool    SettingsManager::get_persist_shuffle_state()   { return m_persistShuffleState; }
bool    SettingsManager::get_last_shuffle_state()      { return m_lastShuffleState; }
QString SettingsManager::get_last_played_song()        { return m_lastPlayedSong; }
int     SettingsManager::get_last_played_position()    { return m_lastPlayedPosition; }
QString SettingsManager::get_last_played_playlist()    { return m_lastPlayedPlaylist; }
QString SettingsManager::get_last_settings_section()   { return m_lastSettingsSection; }
QString SettingsManager::get_window_state()            { return m_windowState; }
QString SettingsManager::get_music_button_default_page() { return m_musicButtonDefaultPage; }
bool    SettingsManager::get_return_to_library_after_selection() { return m_returnToLibraryAfterSelection; }
bool    SettingsManager::get_android_auto_enabled()    { return m_androidAutoEnabled; }
bool    SettingsManager::get_phone_mirror_enabled()    { return m_phoneMirrorEnabled; }
bool    SettingsManager::get_scrcpy_audio_enabled()    { return m_scrcpyAudioEnabled; }
QString SettingsManager::get_scrcpy_display_size()     { return m_scrcpyDisplaySize; }
double  SettingsManager::get_scrcpy_audio_gain()       { return m_scrcpyAudioGain; }
bool    SettingsManager::get_scrcpy_audio_duck_enabled() { return m_scrcpyAudioDuckEnabled; }
double  SettingsManager::get_scrcpy_audio_duck_level() { return m_scrcpyAudioDuckLevel; }
bool    SettingsManager::get_esp32_volume_enabled()    { return m_esp32VolumeEnabled; }
QString SettingsManager::get_esp32_volume_port()       { return m_esp32VolumePort; }
double  SettingsManager::get_esp32_volume_step_size()  { return m_esp32VolumeStepSize; }
bool    SettingsManager::get_esp32_auto_reconnect()    { return m_esp32AutoReconnect; }
bool    SettingsManager::get_esp32_led_sleep_enabled() { return m_esp32LedSleepEnabled; }
QString SettingsManager::get_esp32_led_color_mode()    { return m_esp32LedColorMode; }
QString SettingsManager::get_esp32_led_static_color()  { return m_esp32LedStaticColor; }
bool    SettingsManager::get_show_waveform_visualizer() { return m_showWaveformVisualizer; }
bool    SettingsManager::get_imu_enabled()             { return m_imuEnabled; }
bool    SettingsManager::get_gesture_sensor_enabled()  { return m_gestureSensorEnabled; }

QString SettingsManager::get_gesture_mapping()
{
    QJsonObject gm;
    for (auto it = m_gestureMapping.begin(); it != m_gestureMapping.end(); ++it)
        gm[it.key()] = it.value().toString();
    return QString::fromUtf8(QJsonDocument(gm).toJson(QJsonDocument::Compact));
}

QVariantMap SettingsManager::get_gesture_mapping_dict()
{
    return m_gestureMapping;
}

bool SettingsManager::is_setting_pinned(const QString &settingId)
{
    return m_pinnedSettings.contains(settingId);
}

QString SettingsManager::get_settings_registry()
{
    return QString::fromUtf8(QJsonDocument(s_settingsRegistry).toJson(QJsonDocument::Compact));
}

QString SettingsManager::get_pinned_settings_metadata()
{
    QJsonArray result;
    for (const auto &sid : m_pinnedSettings) {
        QString id = sid.toString();
        if (!s_settingsRegistry.contains(id))
            continue;
        QJsonObject entry = s_settingsRegistry.value(id).toObject();
        entry[QStringLiteral("id")] = id;

        // Resolve current value
        QString key = entry.value(QStringLiteral("key")).toString();
        QString attr = keyToAttr(key);
        QVariant val;
        // Use a map for known keys to their member variables
        if (attr == QStringLiteral("start_volume"))              val = QVariant(m_startVolume);
        else if (attr == QStringLiteral("auto_play_on_startup")) val = QVariant(m_autoPlayOnStartup);
        else if (attr == QStringLiteral("show_waveform_visualizer")) val = QVariant(m_showWaveformVisualizer);
        else if (attr == QStringLiteral("ui_scale"))             val = QVariant(m_uiScale);
        else if (attr == QStringLiteral("theme_setting"))        val = QVariant(m_themeSetting);
        else if (attr == QStringLiteral("show_clock"))           val = QVariant(m_showClock);
        else if (attr == QStringLiteral("show_bottom_bar_media_controls")) val = QVariant(m_showBottomBarMediaControls);
        else if (attr == QStringLiteral("background_blur_radius")) val = QVariant(m_backgroundBlurRadius);
        else if (attr == QStringLiteral("color_transition_ms"))  val = QVariant(m_colorTransitionMs);
        else if (attr == QStringLiteral("obd_fast_mode"))        val = QVariant(m_obdFastMode);
        else if (attr == QStringLiteral("imu_enabled"))          val = QVariant(m_imuEnabled);
        else if (attr == QStringLiteral("gesture_sensor_enabled")) val = QVariant(m_gestureSensorEnabled);
        else if (attr == QStringLiteral("settings_layout_style")) val = QVariant(m_settingsLayoutStyle);
        else {
            // Fallback: read from settings file
            val = m_settings.value(key).toVariant();
        }
        entry[QStringLiteral("currentValue")] = QJsonValue::fromVariant(val);
        result.append(entry);
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString SettingsManager::get_settings_menu_visibility()
{
    return settingsMenuVisibility();
}

bool SettingsManager::is_settings_section_visible(const QString &section)
{
    if (m_settingsMenuVisibility.contains(section))
        return m_settingsMenuVisibility.value(section).toBool();
    return true;
}

// =========================================================================
// keyToAttr — maps camelCase JSON key to snake_case attr name
// =========================================================================
QString SettingsManager::keyToAttr(const QString &key) const
{
    static const QHash<QString, QString> map = {
        {QStringLiteral("startUpVolume"),               QStringLiteral("start_volume")},
        {QStringLiteral("autoPlayOnStartup"),           QStringLiteral("auto_play_on_startup")},
        {QStringLiteral("showWaveformVisualizer"),      QStringLiteral("show_waveform_visualizer")},
        {QStringLiteral("uiScale"),                     QStringLiteral("ui_scale")},
        {QStringLiteral("themeSetting"),                QStringLiteral("theme_setting")},
        {QStringLiteral("showClock"),                   QStringLiteral("show_clock")},
        {QStringLiteral("showBottomBarMediaControls"),  QStringLiteral("show_bottom_bar_media_controls")},
        {QStringLiteral("backgroundBlurRadius"),        QStringLiteral("background_blur_radius")},
        {QStringLiteral("colorTransitionMs"),           QStringLiteral("color_transition_ms")},
        {QStringLiteral("obdFastMode"),                 QStringLiteral("obd_fast_mode")},
        {QStringLiteral("imuEnabled"),                  QStringLiteral("imu_enabled")},
        {QStringLiteral("gestureSensorEnabled"),        QStringLiteral("gesture_sensor_enabled")},
        {QStringLiteral("settingsLayoutStyle"),         QStringLiteral("settings_layout_style")},
    };
    return map.value(key, key);
}

// =========================================================================
// reset_to_defaults
// =========================================================================
void SettingsManager::reset_to_defaults()
{
    saveSettings(m_defaultSettings);

    m_deviceName = m_defaultSettings.value(QStringLiteral("deviceName")).toString();
    emit deviceNameChanged(m_deviceName);

    m_themeSetting = m_defaultSettings.value(QStringLiteral("themeSetting")).toString();
    emit themeSettingChanged(m_themeSetting);

    m_fontSetting = m_defaultSettings.value(QStringLiteral("fontSetting")).toString();
    emit fontSettingChanged(m_fontSetting);

    m_startVolume = static_cast<float>(m_defaultSettings.value(QStringLiteral("startUpVolume")).toDouble());
    emit startUpVolumeChanged(QString::number(static_cast<double>(m_startVolume)));

    m_showClock = m_defaultSettings.value(QStringLiteral("showClock")).toBool();
    emit showClockChanged(m_showClock);

    m_clockFormat24Hour = m_defaultSettings.value(QStringLiteral("clockFormat24Hour")).toBool();
    emit clockFormatChanged(m_clockFormat24Hour);

    m_clockShowSeconds = m_defaultSettings.value(QStringLiteral("clockShowSeconds")).toBool();
    emit clockShowSecondsChanged(m_clockShowSeconds);

    m_clockSize = m_defaultSettings.value(QStringLiteral("clockSize")).toInt();
    emit clockSizeChanged(m_clockSize);

    m_backgroundGrid = m_defaultSettings.value(QStringLiteral("backgroundGrid")).toString();
    emit backgroundGridChanged(m_backgroundGrid);

    m_screenWidth = m_defaultSettings.value(QStringLiteral("screenWidth")).toInt();
    emit screenWidthChanged(m_screenWidth);

    m_screenHeight = m_defaultSettings.value(QStringLiteral("screenHeight")).toInt();
    emit screenHeightChanged(m_screenHeight);

    m_backgroundBlurRadius = m_defaultSettings.value(QStringLiteral("backgroundBlurRadius")).toInt();
    emit backgroundBlurRadiusChanged(m_backgroundBlurRadius);

    m_uiScale = static_cast<float>(m_defaultSettings.value(QStringLiteral("uiScale")).toDouble());
    emit uiScaleChanged(m_uiScale);

    m_colorTransitionMs = m_defaultSettings.value(QStringLiteral("colorTransitionMs")).toInt();
    emit colorTransitionMsChanged(m_colorTransitionMs);

    m_songLengthTransition = m_defaultSettings.value(QStringLiteral("songLengthTransition")).toBool();
    emit songLengthTransitionChanged(m_songLengthTransition);

    m_obdBluetoothPort = m_defaultSettings.value(QStringLiteral("obdBluetoothPort")).toString();
    emit obdBluetoothPortChanged(m_obdBluetoothPort);

    m_obdSavedAdapters = m_defaultSettings.value(QStringLiteral("obdSavedAdapters")).toString();
    if (m_obdSavedAdapters.isEmpty()) m_obdSavedAdapters = QStringLiteral("[]");
    emit obdSavedAdaptersChanged(m_obdSavedAdapters);

    m_obdFastMode = m_defaultSettings.value(QStringLiteral("obdFastMode")).toBool();
    emit obdFastModeChanged(m_obdFastMode);

    m_obdAutoReconnectAttempts = m_defaultSettings.value(QStringLiteral("obdAutoReconnectAttempts")).toInt();
    emit obdAutoReconnectAttemptsChanged(m_obdAutoReconnectAttempts);

    // Reset OBD parameters
    m_obdParameters.clear();
    QJsonObject defObd = m_defaultSettings.value(QStringLiteral("obdParameters")).toObject();
    for (auto it = defObd.begin(); it != defObd.end(); ++it)
        m_obdParameters[it.key()] = it.value().toVariant();
    emit obdParametersChanged();

    // Media folder: reset to OS-standard music location
    QString defaultMusic = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (defaultMusic.isEmpty())
        defaultMusic = QDir::homePath() + QStringLiteral("/Music");
    m_mediaFolder = m_defaultSettings.value(QStringLiteral("mediaFolder")).toString();
    if (m_mediaFolder.isEmpty())
        m_mediaFolder = defaultMusic;
    emit mediaFolderChanged(m_mediaFolder);

    m_fuelTankCapacity = static_cast<float>(m_defaultSettings.value(QStringLiteral("fuelTankCapacity")).toDouble());
    emit fuelTankCapacityChanged(m_fuelTankCapacity);

    m_bottomBarOrientation = m_defaultSettings.value(QStringLiteral("bottomBarOrientation")).toString();
    emit bottomBarOrientationChanged(m_bottomBarOrientation);

    m_showBottomBarMediaControls = m_defaultSettings.value(QStringLiteral("showBottomBarMediaControls")).toBool();
    emit showBottomBarMediaControlsChanged(m_showBottomBarMediaControls);

    m_autoPlayOnStartup = m_defaultSettings.value(QStringLiteral("autoPlayOnStartup")).toBool();
    emit autoPlayOnStartupChanged(m_autoPlayOnStartup);

    m_lastPlayedSong = m_defaultSettings.value(QStringLiteral("lastPlayedSong")).toString();
    emit lastPlayedSongChanged(m_lastPlayedSong);

    m_lastPlayedPosition = m_defaultSettings.value(QStringLiteral("lastPlayedPosition")).toInt();
    emit lastPlayedPositionChanged(m_lastPlayedPosition);

    m_lastPlayedPlaylist = m_defaultSettings.value(QStringLiteral("lastPlayedPlaylist")).toString();
    emit lastPlayedPlaylistChanged(m_lastPlayedPlaylist);

    m_musicButtonDefaultPage = m_defaultSettings.value(QStringLiteral("musicButtonDefaultPage")).toString();
    emit musicButtonDefaultPageChanged(m_musicButtonDefaultPage);

    m_returnToLibraryAfterSelection = m_defaultSettings.value(QStringLiteral("returnToLibraryAfterSelection")).toBool();
    emit returnToLibraryAfterSelectionChanged(m_returnToLibraryAfterSelection);

    m_androidAutoEnabled = m_defaultSettings.value(QStringLiteral("androidAutoEnabled")).toBool();
    emit androidAutoEnabledChanged(m_androidAutoEnabled);

    m_phoneMirrorEnabled = m_defaultSettings.value(QStringLiteral("phoneMirrorEnabled")).toBool();
    emit phoneMirrorEnabledChanged(m_phoneMirrorEnabled);

    m_scrcpyAudioEnabled = m_defaultSettings.value(QStringLiteral("scrcpyAudioEnabled")).toBool();
    emit scrcpyAudioEnabledChanged(m_scrcpyAudioEnabled);

    m_scrcpyDisplaySize = m_defaultSettings.value(QStringLiteral("scrcpyDisplaySize")).toString();
    emit scrcpyDisplaySizeChanged(m_scrcpyDisplaySize);

    m_scrcpyAudioGain = m_defaultSettings.value(QStringLiteral("scrcpyAudioGain")).toDouble();
    emit scrcpyAudioGainChanged(m_scrcpyAudioGain);

    m_scrcpyAudioDuckEnabled = m_defaultSettings.value(QStringLiteral("scrcpyAudioDuckEnabled")).toBool();
    emit scrcpyAudioDuckEnabledChanged(m_scrcpyAudioDuckEnabled);

    m_scrcpyAudioDuckLevel = m_defaultSettings.value(QStringLiteral("scrcpyAudioDuckLevel")).toDouble();
    emit scrcpyAudioDuckLevelChanged(m_scrcpyAudioDuckLevel);

    // Settings menu visibility
    m_settingsMenuVisibility.clear();
    QJsonObject defVis = m_defaultSettings.value(QStringLiteral("settingsMenuVisibility")).toObject();
    for (auto it = defVis.begin(); it != defVis.end(); ++it)
        m_settingsMenuVisibility[it.key()] = it.value().toVariant();
    emit settingsMenuVisibilityChanged();

    m_esp32VolumeEnabled = m_defaultSettings.value(QStringLiteral("esp32VolumeEnabled")).toBool();
    emit esp32VolumeEnabledChanged(m_esp32VolumeEnabled);

    m_esp32VolumePort = m_defaultSettings.value(QStringLiteral("esp32VolumePort")).toString();
    emit esp32VolumePortChanged(m_esp32VolumePort);

    m_esp32VolumeStepSize = m_defaultSettings.value(QStringLiteral("esp32VolumeStepSize")).toDouble();
    emit esp32VolumeStepSizeChanged(m_esp32VolumeStepSize);

    m_esp32AutoReconnect = m_defaultSettings.value(QStringLiteral("esp32AutoReconnect")).toBool();
    emit esp32AutoReconnectChanged(m_esp32AutoReconnect);

    m_esp32LedSleepEnabled = m_defaultSettings.value(QStringLiteral("esp32LedSleepEnabled")).toBool();
    emit esp32LedSleepEnabledChanged(m_esp32LedSleepEnabled);

    m_esp32LedColorMode = m_defaultSettings.value(QStringLiteral("esp32LedColorMode")).toString();
    emit esp32LedColorModeChanged(m_esp32LedColorMode);

    m_esp32LedStaticColor = m_defaultSettings.value(QStringLiteral("esp32LedStaticColor")).toString();
    emit esp32LedStaticColorChanged(m_esp32LedStaticColor);

    m_showWaveformVisualizer = m_defaultSettings.value(QStringLiteral("showWaveformVisualizer")).toBool();
    emit showWaveformVisualizerChanged(m_showWaveformVisualizer);

    m_imuEnabled = m_defaultSettings.value(QStringLiteral("imuEnabled")).toBool();
    emit imuEnabledChanged(m_imuEnabled);

    m_gestureSensorEnabled = m_defaultSettings.value(QStringLiteral("gestureSensorEnabled")).toBool();
    emit gestureSensorEnabledChanged(m_gestureSensorEnabled);

    // Reset gesture mapping
    m_gestureMapping.clear();
    QJsonObject defGm = m_defaultSettings.value(QStringLiteral("gestureMapping")).toObject();
    for (auto it = defGm.begin(); it != defGm.end(); ++it)
        m_gestureMapping[it.key()] = it.value().toVariant();
    emit gestureMappingChanged();

    m_gestureVolumeStep = m_defaultSettings.value(QStringLiteral("gestureVolumeStep")).toInt();
    emit gestureVolumeStepChanged(m_gestureVolumeStep);

    m_gestureCooldown = m_defaultSettings.value(QStringLiteral("gestureCooldown")).toInt();
    emit gestureCooldownChanged(m_gestureCooldown);

    m_roundedAlbumArt = m_defaultSettings.value(QStringLiteral("roundedAlbumArt")).toBool();
    emit roundedAlbumArtChanged(m_roundedAlbumArt);

    m_albumArtCornerRadius = m_defaultSettings.value(QStringLiteral("albumArtCornerRadius")).toInt();
    emit albumArtCornerRadiusChanged(m_albumArtCornerRadius);

    m_showAlbumArtShadow = m_defaultSettings.value(QStringLiteral("showAlbumArtShadow")).toBool();
    emit showAlbumArtShadowChanged(m_showAlbumArtShadow);

    m_vinylRecordMode = m_defaultSettings.value(QStringLiteral("vinylRecordMode")).toBool();
    emit vinylRecordModeChanged(m_vinylRecordMode);

    m_albumArtTransition = m_defaultSettings.value(QStringLiteral("albumArtTransition")).toString();
    emit albumArtTransitionChanged(m_albumArtTransition);

    m_backgroundOverlayOpacity = m_defaultSettings.value(QStringLiteral("backgroundOverlayOpacity")).toInt();
    emit backgroundOverlayOpacityChanged(m_backgroundOverlayOpacity);

    m_sideCardOpacity = m_defaultSettings.value(QStringLiteral("sideCardOpacity")).toDouble();
    emit sideCardOpacityChanged(m_sideCardOpacity);

    m_sideCardAngle = m_defaultSettings.value(QStringLiteral("sideCardAngle")).toInt();
    emit sideCardAngleChanged(m_sideCardAngle);

    m_buttonTiltDuration = m_defaultSettings.value(QStringLiteral("buttonTiltDuration")).toInt();
    emit buttonTiltDurationChanged(m_buttonTiltDuration);

    m_textScrollSpeed = m_defaultSettings.value(QStringLiteral("textScrollSpeed")).toInt();
    emit textScrollSpeedChanged(m_textScrollSpeed);

    m_pinnedSettings.clear();
    emit pinnedSettingsChanged();
}

// =========================================================================
// Git info methods
// =========================================================================
QString SettingsManager::getGitRepoDir() const
{
    // Go up one level from backend dir to get repo root
    return QFileInfo(m_backendDir).absolutePath();
}

QString SettingsManager::get_git_commit_hash()
{
    QProcess proc;
    proc.setWorkingDirectory(getGitRepoDir());
    proc.start(QStringLiteral("git"), {QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")});
    if (proc.waitForFinished(5000) && proc.exitCode() == 0)
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return QStringLiteral("unknown");
}

QString SettingsManager::get_git_commit_date()
{
    QProcess proc;
    proc.setWorkingDirectory(getGitRepoDir());
    proc.start(QStringLiteral("git"), {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%ci")});
    if (proc.waitForFinished(5000) && proc.exitCode() == 0) {
        QString dateStr = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (!dateStr.isEmpty()) {
            QStringList parts = dateStr.split(QLatin1Char(' '));
            if (parts.size() >= 2)
                return parts[0] + QStringLiteral(" at ") + parts[1].left(5);
        }
        return dateStr;
    }
    return QStringLiteral("unknown");
}

QString SettingsManager::get_git_commit_message()
{
    QProcess proc;
    proc.setWorkingDirectory(getGitRepoDir());
    proc.start(QStringLiteral("git"), {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%s")});
    if (proc.waitForFinished(5000) && proc.exitCode() == 0)
        return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    return QString();
}
