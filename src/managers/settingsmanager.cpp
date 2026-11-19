// OCTAVE Settings Manager

#include "settingsmanager.h"
#include "gesturevocabulary.h"
#include "../util/units.h"
#include "../util/pathurl.h"
#include "volumecontroller.h"
#include "../util/perfscope.h"

#include "buildinfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QMetaMethod>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QLoggingCategory>

#include <cmath>

Q_LOGGING_CATEGORY(lcSettings, "octave.settings")

namespace {
// The Sensors page readouts; each is saved as sensorShow<card>
const QStringList kSensorCards = {
    QStringLiteral("Pitch"), QStringLiteral("Roll"), QStringLiteral("Heading"),
    QStringLiteral("GForce"), QStringLiteral("Altitude"), QStringLiteral("Temperature")};
}

// ===========================================================================
// The settings table
//
// One row per plain setting: a single bool, number or string under one key
// of settingsConfigure.json. A row gives the key, its type, its default,
// what values it takes, the signal that says it changed and the save slot
// QML has always called. Everything else is driven from here: the defaults
// (buildDefaultSettings), loading (populateMembers), every way of setting a
// value (setRow: the save_* slots, the writable properties, save_setting,
// apply_settings), get_setting, reset_to_defaults and the pinned-settings
// values.
//
// A new setting: one row here; in the header its Q_PROPERTY (READ / WRITE /
// NOTIFY), getter, signal and save slot; and their two one-line bodies
// below. tst_settingsmanager checks every row against the meta-object.
//
// Rules: any() takes every value of the type; clampTo() brings a number
// into range, as the slot it replaces did; within() refuses a number
// outside the range the UI offers (the slots had no check: the range is the
// QML slider's from / to, named beside the row); oneOf() refuses anything
// else; custom() is a function that may change the value or refuse it.
// Loading is more lenient than saving: a clampTo() value is clamped, a
// oneOf() or custom() value that fails becomes the default, and a within()
// value is kept as the file has it.
// ===========================================================================
namespace {

struct Rule {
    enum Kind { Any, Clamp, Within, OneOf, Custom } kind = Any;
    double lo = 0;
    double hi = 0;
    QStringList allowed;
    bool (*fix)(QVariant &) = nullptr;
};
Rule any() { return {}; }
Rule clampTo(double lo, double hi) { Rule r; r.kind = Rule::Clamp; r.lo = lo; r.hi = hi; return r; }
Rule within(double lo, double hi) { Rule r; r.kind = Rule::Within; r.lo = lo; r.hi = hi; return r; }
Rule oneOf(const QStringList &allowed) { Rule r; r.kind = Rule::OneOf; r.allowed = allowed; return r; }
Rule custom(bool (*fix)(QVariant &)) { Rule r; r.kind = Rule::Custom; r.fix = fix; return r; }

// Plain: the row has a writable property of its own name and type.
// Derived: stored as a string, shown to QML as a list or a map by a property
// of the same name. Part: one of several rows behind one map property.
enum Shape { Plain, Derived, Part };

struct SettingRow {
    const char *key;
    QMetaType::Type type;   // Bool, Int, Double or QString
    QVariant def;
    Rule rule;
    void (*notify)(SettingsManager *, const QVariant &);
    const char *slot;       // the save slot, nullptr if none is public
    Shape shape;
};

// Current SettingsMenu.qml sections, plus the per-feature ids an older
// settingsConfigure.json may still carry
const QStringList kSettingsSections = {
    QStringLiteral("displaySettings"), QStringLiteral("mediaSettings"),
    QStringLiteral("obdSettings"), QStringLiteral("accessoriesSettings"),
    QStringLiteral("deviceSettings"), QStringLiteral("about"),
    QStringLiteral("androidAutoSettings"), QStringLiteral("phoneMirrorSettings"),
    QStringLiteral("volumeKnobSettings"), QStringLiteral("gestureSensorSettings")};

const QStringList kAlbumArtTransitions = {
    QStringLiteral("Crossfade"), QStringLiteral("Slide"), QStringLiteral("Vinyl Lift"),
    QStringLiteral("Dissolve"), QStringLiteral("Flip"), QStringLiteral("Coverflow"),
    QStringLiteral("Conveyor"), QStringLiteral("Stack"), QStringLiteral("Depth"),
    QStringLiteral("Swing")};

// The adapters list is a compact JSON array; nothing saved is "[]"
bool fixSavedAdapters(QVariant &v)
{
    if (v.toString().isEmpty())
        v = QStringLiteral("[]");
    return true;
}

// Virtual display density: 0 (or less) = scaled from the phone's own
bool fixDisplayDpi(QVariant &v)
{
    const int dpi = v.toInt();
    v = dpi <= 0 ? 0 : qBound(80, dpi, 640);
    return true;
}

bool fixTrimmed(QVariant &v)
{
    v = v.toString().trimmed();
    return true;
}

// % per encoder tick: 0.25 to 10, to the nearest 0.25
bool fixKnobStep(QVariant &v)
{
    v = std::round(qBound(0.25, v.toDouble(), 10.0) * 4.0) / 4.0;
    return true;
}

// Any id is kept, even one not plugged in now: the earbuds or the car's
// output come back later, and playback falls back to the default meanwhile.
// Only something that cannot be a device id is refused.
bool fixDeviceId(QVariant &v)
{
    return v.toString().size() <= 512;
}

#define N_BOOL(sig)   [](SettingsManager *s, const QVariant &v) { emit s->sig(v.toBool()); }
#define N_INT(sig)    [](SettingsManager *s, const QVariant &v) { emit s->sig(v.toInt()); }
#define N_DOUBLE(sig) [](SettingsManager *s, const QVariant &v) { emit s->sig(v.toDouble()); }
#define N_FLOAT(sig)  [](SettingsManager *s, const QVariant &v) { emit s->sig(v.toFloat()); }
#define N_STR(sig)    [](SettingsManager *s, const QVariant &v) { emit s->sig(v.toString()); }
#define N_VOID(sig)   [](SettingsManager *s, const QVariant &) { emit s->sig(); }
// startUpVolumeChanged carries the number as text, as it always has
#define N_NUMBER_AS_STR(sig) [](SettingsManager *s, const QVariant &v) { emit s->sig(QString::number(v.toDouble())); }

// key, type, default, rule, notify, save slot, shape
// Ranges given with within() are the sliders': startUpVolume is the linear
// 0-1 the volume slider maps to (MediaSettingsPage.qml), clockSize 10-85,
// uiScale 0.5-2.0, colorTransitionMs 0-2000 (DisplaySettingsPage.qml),
// backgroundBlurRadius 0-100 (MediaSettingsPage.qml, NowPlayingStudio.qml).
#define OCTAVE_SETTINGS(X) \
    X(deviceName, QMetaType::QString, QStringLiteral("Default Device"), any(), N_STR(deviceNameChanged), "save_device_name", Plain) \
    X(themeSetting, QMetaType::QString, QStringLiteral("CosmicVoyager"), any(), N_STR(themeSettingChanged), "save_theme_setting", Plain) \
    X(fontSetting, QMetaType::QString, QStringLiteral("GoogleSans Regular"), any(), N_STR(fontSettingChanged), "save_font_setting", Plain) \
    X(startUpVolume, QMetaType::Double, 0.1, within(0.0, 1.0), N_NUMBER_AS_STR(startUpVolumeChanged), "save_start_volume", Plain) \
    X(showClock, QMetaType::Bool, true, any(), N_BOOL(showClockChanged), "save_show_clock", Plain) \
    X(clockFormat24Hour, QMetaType::Bool, true, any(), N_BOOL(clockFormatChanged), "save_clock_format", Plain) \
    X(clockShowSeconds, QMetaType::Bool, false, any(), N_BOOL(clockShowSecondsChanged), "save_clock_show_seconds", Plain) \
    X(clockSize, QMetaType::Int, 18, within(10, 85), N_INT(clockSizeChanged), "save_clock_size", Plain) \
    X(clockSizeAuto, QMetaType::Bool, true, any(), N_BOOL(clockSizeAutoChanged), "save_clock_size_auto", Plain) \
    X(backgroundGrid, QMetaType::QString, QStringLiteral("4x4"), any(), N_STR(backgroundGridChanged), "save_background_grid", Plain) \
    X(screenWidth, QMetaType::Int, 1280, clampTo(SettingsManager::kMinScreenWidth, SettingsManager::kMaxScreenWidth), N_INT(screenWidthChanged), "save_screen_width", Plain) \
    X(screenHeight, QMetaType::Int, 720, clampTo(SettingsManager::kMinScreenHeight, SettingsManager::kMaxScreenHeight), N_INT(screenHeightChanged), "save_screen_height", Plain) \
    X(backgroundBlurRadius, QMetaType::Int, 40, within(0, 100), N_INT(backgroundBlurRadiusChanged), "save_background_blur_radius", Plain) \
    X(uiScale, QMetaType::Double, 1.0, within(0.5, 2.0), N_FLOAT(uiScaleChanged), "save_ui_scale", Plain) \
    X(colorTransitionMs, QMetaType::Int, 1000, within(0, 2000), N_INT(colorTransitionMsChanged), "save_color_transition_ms", Plain) \
    X(songLengthTransition, QMetaType::Bool, false, any(), N_BOOL(songLengthTransitionChanged), "save_song_length_transition", Plain) \
    X(obdBluetoothPort, QMetaType::QString, QString(), any(), N_STR(obdBluetoothPortChanged), "save_obd_bluetooth_port", Plain) \
    X(obdSavedAdapters, QMetaType::QString, QStringLiteral("[]"), custom(fixSavedAdapters), N_VOID(obdSavedAdaptersChanged), nullptr, Derived) \
    X(obdFastMode, QMetaType::Bool, true, any(), N_BOOL(obdFastModeChanged), "save_obd_fast_mode", Plain) \
    X(obdCardStyleCircular, QMetaType::Bool, false, any(), N_BOOL(obdCardStyleCircularChanged), "save_obd_card_style_circular", Plain) \
    X(gaugeTempUnit, QMetaType::QString, QStringLiteral("C"), oneOf({QStringLiteral("C"), QStringLiteral("F")}), N_VOID(gaugeTempUnitChanged), "save_gauge_temp_unit", Plain) \
    X(gaugeSpeedUnit, QMetaType::QString, QStringLiteral("mph"), oneOf({QStringLiteral("mph"), QStringLiteral("km/h")}), N_VOID(gaugeSpeedUnitChanged), "save_gauge_speed_unit", Plain) \
    X(obdAutoReconnectAttempts, QMetaType::Int, 0, clampTo(0, 10), N_INT(obdAutoReconnectAttemptsChanged), "save_obd_auto_reconnect_attempts", Plain) \
    X(bottomBarOrientation, QMetaType::QString, QStringLiteral("bottom"), any(), N_STR(bottomBarOrientationChanged), "save_bottom_bar_orientation", Plain) \
    X(showBottomBarMediaControls, QMetaType::Bool, false, any(), N_BOOL(showBottomBarMediaControlsChanged), "save_show_bottom_bar_media_controls", Plain) \
    X(fuelTankCapacity, QMetaType::Double, 15.0, any(), N_FLOAT(fuelTankCapacityChanged), "save_fuel_tank_capacity", Plain) \
    X(blackBoxEnabled, QMetaType::Bool, true, any(), N_BOOL(blackBoxEnabledChanged), "save_black_box_enabled", Plain) \
    X(blackBoxSweepAll, QMetaType::Bool, true, any(), N_BOOL(blackBoxSweepAllChanged), "save_black_box_sweep_all", Plain) \
    X(homeVehicleCard, QMetaType::Bool, false, any(), N_BOOL(homeVehicleCardChanged), "setHomeVehicleCard", Plain) \
    X(blackBoxMaxMB, QMetaType::Int, 500, clampTo(50, 5000), N_INT(blackBoxMaxMBChanged), "save_black_box_max_mb", Plain) \
    X(lastSettingsSection, QMetaType::QString, QStringLiteral("deviceSettings"), oneOf(kSettingsSections), N_STR(lastSettingsSectionChanged), "set_last_settings_section", Plain) \
    X(autoPlayOnStartup, QMetaType::Bool, false, any(), N_BOOL(autoPlayOnStartupChanged), "save_auto_play_on_startup", Plain) \
    X(persistShuffleState, QMetaType::Bool, false, any(), N_BOOL(persistShuffleStateChanged), "save_persist_shuffle_state", Plain) \
    X(lastShuffleState, QMetaType::Bool, false, any(), N_BOOL(lastShuffleStateChanged), "save_last_shuffle_state", Plain) \
    X(lastPlayedSong, QMetaType::QString, QString(), any(), N_STR(lastPlayedSongChanged), "save_last_played_song", Plain) \
    X(lastPlayedPosition, QMetaType::Int, 0, any(), N_INT(lastPlayedPositionChanged), "save_last_played_position", Plain) \
    X(lastPlayedPlaylist, QMetaType::QString, QString(), any(), N_STR(lastPlayedPlaylistChanged), "save_last_played_playlist", Plain) \
    X(windowState, QMetaType::QString, QStringLiteral("windowed"), oneOf({QStringLiteral("windowed"), QStringLiteral("fullscreen"), QStringLiteral("maximized")}), N_STR(windowStateChanged), "save_window_state", Plain) \
    X(musicButtonDefaultPage, QMetaType::QString, QStringLiteral("mediaRoom"), oneOf({QStringLiteral("mediaRoom"), QStringLiteral("mediaPlayer")}), N_STR(musicButtonDefaultPageChanged), "save_music_button_default_page", Plain) \
    X(returnToLibraryAfterSelection, QMetaType::Bool, false, any(), N_BOOL(returnToLibraryAfterSelectionChanged), "save_return_to_library_after_selection", Plain) \
    X(androidAutoEnabled, QMetaType::Bool, false, any(), N_BOOL(androidAutoEnabledChanged), "save_android_auto_enabled", Plain) \
    X(phoneMirrorEnabled, QMetaType::Bool, false, any(), N_BOOL(phoneMirrorEnabledChanged), "save_phone_mirror_enabled", Plain) \
    X(scrcpyDisplaySize, QMetaType::QString, QStringLiteral("auto"), any(), N_STR(scrcpyDisplaySizeChanged), "save_scrcpy_display_size", Plain) \
    X(scrcpyAudioGain, QMetaType::Double, 2.0, clampTo(0.25, 8.0), N_DOUBLE(scrcpyAudioGainChanged), "save_scrcpy_audio_gain", Plain) \
    X(scrcpyAudioDuckEnabled, QMetaType::Bool, true, any(), N_BOOL(scrcpyAudioDuckEnabledChanged), "save_scrcpy_audio_duck_enabled", Plain) \
    X(scrcpyAudioDuckLevel, QMetaType::Double, 0.1, clampTo(0.0, 1.0), N_DOUBLE(scrcpyAudioDuckLevelChanged), "save_scrcpy_audio_duck_level", Plain) \
    X(scrcpyPhoneScreenOff, QMetaType::Bool, false, any(), N_BOOL(scrcpyPhoneScreenOffChanged), "save_scrcpy_phone_screen_off", Plain) \
    X(scrcpyDisplayDpi, QMetaType::Int, 0, custom(fixDisplayDpi), N_INT(scrcpyDisplayDpiChanged), "save_scrcpy_display_dpi", Plain) \
    X(scrcpyBitRateUsb, QMetaType::Int, 16, clampTo(2, 40), N_INT(scrcpyBitRateUsbChanged), "save_scrcpy_bit_rate_usb", Plain) \
    X(scrcpyBitRateWifi, QMetaType::Int, 8, clampTo(2, 40), N_INT(scrcpyBitRateWifiChanged), "save_scrcpy_bit_rate_wifi", Plain) \
    X(scrcpyAutoWifi, QMetaType::Bool, false, any(), N_BOOL(scrcpyAutoWifiChanged), "save_scrcpy_auto_wifi", Plain) \
    X(scrcpyBitRateAuto, QMetaType::Bool, true, any(), N_BOOL(scrcpyBitRateAutoChanged), "save_scrcpy_bit_rate_auto", Plain) \
    X(scrcpyWirelessAddress, QMetaType::QString, QString(), custom(fixTrimmed), N_STR(scrcpyWirelessAddressChanged), "save_scrcpy_wireless_address", Plain) \
    X(esp32VolumeEnabled, QMetaType::Bool, true, any(), N_BOOL(esp32VolumeEnabledChanged), "save_esp32_volume_enabled", Plain) \
    X(esp32VolumePort, QMetaType::QString, QString(), any(), N_STR(esp32VolumePortChanged), "save_esp32_volume_port", Plain) \
    X(esp32VolumeStepSize, QMetaType::Double, 1.0, custom(fixKnobStep), N_DOUBLE(esp32VolumeStepSizeChanged), "save_esp32_volume_step_size", Plain) \
    X(esp32AutoReconnect, QMetaType::Bool, true, any(), N_BOOL(esp32AutoReconnectChanged), "save_esp32_auto_reconnect", Plain) \
    X(esp32LedSleepEnabled, QMetaType::Bool, true, any(), N_BOOL(esp32LedSleepEnabledChanged), "save_esp32_led_sleep_enabled", Plain) \
    X(esp32LedColorMode, QMetaType::QString, QStringLiteral("theme"), oneOf({QStringLiteral("theme"), QStringLiteral("static")}), N_STR(esp32LedColorModeChanged), "save_esp32_led_color_mode", Plain) \
    X(esp32LedStaticColor, QMetaType::QString, QStringLiteral("#00FFFF"), any(), N_STR(esp32LedStaticColorChanged), "save_esp32_led_static_color", Plain) \
    X(showWaveformVisualizer, QMetaType::Bool, true, any(), N_BOOL(showWaveformVisualizerChanged), "save_show_waveform_visualizer", Plain) \
    X(show3DButtonTilt, QMetaType::Bool, false, any(), N_BOOL(show3DButtonTiltChanged), "save_show_3d_button_tilt", Plain) \
    X(show3DAlbumPreview, QMetaType::Bool, false, any(), N_BOOL(show3DAlbumPreviewChanged), "save_show_3d_album_preview", Plain) \
    X(roundedAlbumArt, QMetaType::Bool, true, any(), N_BOOL(roundedAlbumArtChanged), "save_rounded_album_art", Plain) \
    X(albumArtCornerRadius, QMetaType::Int, 16, clampTo(2, 32), N_INT(albumArtCornerRadiusChanged), "save_album_art_corner_radius", Plain) \
    X(showAlbumArtShadow, QMetaType::Bool, true, any(), N_BOOL(showAlbumArtShadowChanged), "save_show_album_art_shadow", Plain) \
    X(vinylRecordMode, QMetaType::Bool, false, any(), N_BOOL(vinylRecordModeChanged), "save_vinyl_record_mode", Plain) \
    X(albumArtTransition, QMetaType::QString, QStringLiteral("Crossfade"), oneOf(kAlbumArtTransitions), N_STR(albumArtTransitionChanged), "save_album_art_transition", Plain) \
    X(audioOutputDevice, QMetaType::QString, QString(), custom(fixDeviceId), N_STR(audioOutputDeviceChanged), "save_audio_output_device", Plain) \
    X(backgroundOverlayOpacity, QMetaType::Int, 80, clampTo(0, 100), N_INT(backgroundOverlayOpacityChanged), "save_background_overlay_opacity", Plain) \
    X(sideCardOpacity, QMetaType::Double, 0.4, clampTo(0.1, 1.0), N_DOUBLE(sideCardOpacityChanged), "save_side_card_opacity", Plain) \
    X(sideCardAngle, QMetaType::Int, 30, clampTo(5, 60), N_INT(sideCardAngleChanged), "save_side_card_angle", Plain) \
    X(buttonTiltDuration, QMetaType::Int, 200, clampTo(50, 500), N_INT(buttonTiltDurationChanged), "save_button_tilt_duration", Plain) \
    X(textScrollSpeed, QMetaType::Int, 5000, clampTo(1000, 10000), N_INT(textScrollSpeedChanged), "save_text_scroll_speed", Plain) \
    X(settingsLayoutStyle, QMetaType::QString, QStringLiteral("Sidebar"), oneOf({QStringLiteral("Carousel"), QStringLiteral("Sidebar"), QStringLiteral("Hub"), QStringLiteral("Dashboard")}), N_STR(settingsLayoutStyleChanged), "save_settings_layout_style", Plain) \
    X(albumArtColors, QMetaType::QString, QString(), any(), N_VOID(albumArtColorsChanged), "set_album_art_colors", Derived) \
    X(imuEnabled, QMetaType::Bool, true, any(), N_BOOL(imuEnabledChanged), "save_imu_enabled", Plain) \
    X(sensorEmitRate, QMetaType::Int, 60, clampTo(10, 120), N_INT(sensorEmitRateChanged), "save_sensor_emit_rate", Plain) \
    X(sensorDecimalPlaces, QMetaType::Int, 1, clampTo(0, 4), N_INT(sensorDecimalPlacesChanged), "save_sensor_decimal_places", Plain) \
    X(sensorTempUnit, QMetaType::QString, QStringLiteral("F"), oneOf({QStringLiteral("F"), QStringLiteral("C")}), N_STR(sensorTempUnitChanged), "save_sensor_temp_unit", Plain) \
    X(sensorAltitudeUnit, QMetaType::QString, QStringLiteral("m"), oneOf({QStringLiteral("m"), QStringLiteral("ft")}), N_STR(sensorAltitudeUnitChanged), "save_sensor_altitude_unit", Plain) \
    X(sensorShowPitch, QMetaType::Bool, true, any(), N_VOID(sensorCardsChanged), nullptr, Part) \
    X(sensorShowRoll, QMetaType::Bool, true, any(), N_VOID(sensorCardsChanged), nullptr, Part) \
    X(sensorShowHeading, QMetaType::Bool, true, any(), N_VOID(sensorCardsChanged), nullptr, Part) \
    X(sensorShowGForce, QMetaType::Bool, true, any(), N_VOID(sensorCardsChanged), nullptr, Part) \
    X(sensorShowAltitude, QMetaType::Bool, true, any(), N_VOID(sensorCardsChanged), nullptr, Part) \
    X(sensorShowTemperature, QMetaType::Bool, true, any(), N_VOID(sensorCardsChanged), nullptr, Part) \
    X(gestureSensorEnabled, QMetaType::Bool, true, any(), N_BOOL(gestureSensorEnabledChanged), "save_gesture_sensor_enabled", Plain) \
    X(gestureVolumeStep, QMetaType::Int, 5, clampTo(1, 100), N_INT(gestureVolumeStepChanged), "save_gesture_volume_step", Plain) \
    X(gestureCooldown, QMetaType::Int, 300, clampTo(100, 2000), N_INT(gestureCooldownChanged), "save_gesture_cooldown", Plain)

#define X(key, type, def, rule, notify, slot, shape) R_##key,
enum RowIndex { OCTAVE_SETTINGS(X) kRowCount };
#undef X

const QList<SettingRow> &rows()
{
#define X(key, type, def, rule, notify, slot, shape) SettingRow{#key, type, QVariant(def), rule, notify, slot, shape},
    static const QList<SettingRow> table = {OCTAVE_SETTINGS(X)};
#undef X
    return table;
}

// The row of a key, -1 if the key is not a plain setting
int rowIndex(const QString &key)
{
    static const QHash<QString, int> index = [] {
        QHash<QString, int> h;
        for (int i = 0; i < rows().size(); ++i)
            h.insert(QString::fromLatin1(rows().at(i).key), i);
        return h;
    }();
    return index.value(key, -1);
}

bool isNumber(const QVariant &v)
{
    const int id = v.typeId();
    return id == QMetaType::Int || id == QMetaType::LongLong || id == QMetaType::Double || id == QMetaType::UInt
           || id == QMetaType::ULongLong || id == QMetaType::Float;
}

// `v` as a value of the row's type. Strict: only when it already is one
// ("true" is not a bool, "5" is not a number); an int row rounds.
bool coerce(QVariant &v, QMetaType::Type type, bool strict)
{
    switch (type) {
    case QMetaType::Bool:
        if (strict && v.typeId() != QMetaType::Bool)
            return false;
        v = v.toBool();
        return true;
    case QMetaType::Int:
        if (strict && !isNumber(v))
            return false;
        v = int(qRound(v.toDouble()));
        return true;
    case QMetaType::Double:
        if (strict && !isNumber(v))
            return false;
        v = v.toDouble();
        return std::isfinite(v.toDouble());
    case QMetaType::QString:
        if (strict && v.typeId() != QMetaType::QString)
            return false;
        v = v.toString();
        return true;
    default:
        return false;
    }
}

// The row's rule on a value of the row's type. False = refused.
bool applyRule(const SettingRow &r, QVariant &v, bool loading)
{
    switch (r.rule.kind) {
    case Rule::Any:
        return true;
    case Rule::Clamp:
        if (r.type == QMetaType::Int)
            v = qBound(int(r.rule.lo), v.toInt(), int(r.rule.hi));
        else
            v = qBound(r.rule.lo, v.toDouble(), r.rule.hi);
        return true;
    case Rule::Within: {
        // A float that went through a double is a hair off its limit
        const double d = v.toDouble();
        return loading || (d >= r.rule.lo - 1e-6 && d <= r.rule.hi + 1e-6);
    }
    case Rule::OneOf:
        return r.rule.allowed.contains(v.toString());
    case Rule::Custom:
        return r.rule.fix(v);
    }
    return true;
}

// A row's value out of the file; invalid if the file has another type there
QVariant fromJson(const QJsonValue &jv, QMetaType::Type type)
{
    switch (type) {
    case QMetaType::Bool:    return jv.isBool() ? QVariant(jv.toBool()) : QVariant();
    case QMetaType::Int:     return jv.isDouble() ? QVariant(int(qRound(jv.toDouble()))) : QVariant();
    case QMetaType::Double:  return jv.isDouble() ? QVariant(jv.toDouble()) : QVariant();
    case QMetaType::QString: return jv.isString() ? QVariant(jv.toString()) : QVariant();
    default:                 return QVariant();
    }
}

// The settings that are a list or a map: each has its own savers and its
// own signal; save_setting() and reset_to_defaults() use this to keep the
// members and the listeners in step
struct StructuredSetting {
    const char *key;
    void (*notify)(SettingsManager *);
};
const QList<StructuredSetting> &structuredSettings()
{
    static const QList<StructuredSetting> list = {
        {"obdParameters",          [](SettingsManager *s) { emit s->obdParametersChanged(); }},
        {"homeOBDParameters",      [](SettingsManager *s) { emit s->homeOBDParametersChanged(); }},
        {"supportedOBDParameters", [](SettingsManager *s) { emit s->supportedOBDParametersChanged(); }},
        {"directoryHistory",       [](SettingsManager *s) { emit s->directoryHistoryChanged(); }},
        {"settingsMenuVisibility", [](SettingsManager *s) { emit s->settingsMenuVisibilityChanged(); }},
        {"gestureMapping",         [](SettingsManager *s) { emit s->gestureMappingChanged(); }},
        {"pinnedSettings",         [](SettingsManager *s) { emit s->pinnedSettingsChanged(); }},
        {"customThemes",           [](SettingsManager *s) { emit s->customThemesChanged(); }},
    };
    return list;
}
const StructuredSetting *structuredSetting(const QString &key)
{
    for (const StructuredSetting &s : structuredSettings())
        if (key == QLatin1String(s.key))
            return &s;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// setRow: the one place a plain setting changes
// ---------------------------------------------------------------------------
SettingsManager::SetResult SettingsManager::setRow(int index, QVariant value, bool strictType)
{
    const SettingRow &r = rows().at(index);
    if (!coerce(value, r.type, strictType) || !applyRule(r, value, false)) {
        qCWarning(lcSettings).noquote() << "Setting" << r.key << "refused:" << value.toString().left(80)
                                        << "is not a value it takes";
        return SetResult::Refused;
    }
    if (value == m_values.at(index))
        return SetResult::Unchanged;   // no write, no signal
    qCDebug(lcSettings).noquote() << "Saving" << r.key << "=" << value.toString().left(80);
    m_values[index] = value;
    m_settings.insert(QString::fromLatin1(r.key), QJsonValue::fromVariant(value));
    saveSettings(m_settings);
    r.notify(this, value);
    return SetResult::Changed;
}

// For the tests: the table as data
QVariantList SettingsManager::describeTable()
{
    QVariantList out;
    for (const SettingRow &r : rows()) {
        static const char *const kinds[] = {"any", "clamp", "within", "oneOf", "custom"};
        static const char *const shapes[] = {"plain", "derived", "part"};
        out.append(QVariantMap{{QStringLiteral("key"), QString::fromLatin1(r.key)},
                               {QStringLiteral("type"), QString::fromLatin1(QMetaType(r.type).name())},
                               {QStringLiteral("default"), r.def},
                               {QStringLiteral("rule"), QString::fromLatin1(kinds[r.rule.kind])},
                               {QStringLiteral("lo"), r.rule.lo},
                               {QStringLiteral("hi"), r.rule.hi},
                               {QStringLiteral("allowed"), r.rule.allowed},
                               {QStringLiteral("slot"), QString::fromLatin1(r.slot ? r.slot : "")},
                               {QStringLiteral("shape"), QString::fromLatin1(shapes[r.shape])}});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Static registry (populated once)
// ---------------------------------------------------------------------------
QJsonObject SettingsManager::s_settingsRegistry;

// ---------------------------------------------------------------------------
// buildSettingsRegistry
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
    reg[QStringLiteral("color_transition_ms")]        = entry("colorTransitionMs", "Theme Transition Speed", "displaySettings", "slider", "save_color_transition_ms", sliderParams(0, 2000, 50));
    reg[QStringLiteral("obd_fast_mode")]              = entry("obdFastMode", "OBD Fast Mode", "obdSettings", "toggle", "save_obd_fast_mode");
    reg[QStringLiteral("black_box_enabled")]          = entry("blackBoxEnabled", "Black Box Recording", "obdSettings", "toggle", "save_black_box_enabled");
    reg[QStringLiteral("black_box_sweep_all")]        = entry("blackBoxSweepAll", "Black Box Records All PIDs", "obdSettings", "toggle", "save_black_box_sweep_all");
    reg[QStringLiteral("home_vehicle_card")]          = entry("homeVehicleCard", "Vehicle on Home", "obdSettings", "toggle", "setHomeVehicleCard");
    reg[QStringLiteral("black_box_max_mb")]           = entry("blackBoxMaxMB", "Black Box Storage Cap", "obdSettings", "slider", "save_black_box_max_mb", sliderParams(50, 5000, 50));
    reg[QStringLiteral("gesture_sensor_enabled")]     = entry("gestureSensorEnabled", "Gesture Sensor", "gestureSensorSettings", "toggle", "save_gesture_sensor_enabled");
    reg[QStringLiteral("imu_enabled")]                = entry("imuEnabled", "IMU Sensor", "imuSettings", "toggle", "save_imu_enabled");
    reg[QStringLiteral("sensor_emit_rate")]           = entry("sensorEmitRate", "IMU Update Rate", "imuSettings", "slider", "save_sensor_emit_rate", sliderParams(10, 120, 5));

    s_settingsRegistry = reg;
    return reg;
}

// ---------------------------------------------------------------------------
// buildDefaultSettings
// ---------------------------------------------------------------------------
QJsonObject SettingsManager::buildDefaultSettings() const
{
    QJsonObject d;
    // The plain settings: from the table
    for (const SettingRow &r : rows())
        d[QString::fromLatin1(r.key)] = QJsonValue::fromVariant(r.def);

    // What follows has no row: keys read and written by name only
    // (get_setting / save_setting), the file-only service keys, and the
    // settings that are a list or a map.
    d[QStringLiteral("serviceEnabled")]        = false;   // the service at every start (src/service/serviceoptions.h)
    d[QStringLiteral("serviceWebRoot")]        = QString();   // a frontend served at GET / ("" = the reference client)
    d[QStringLiteral("serviceLoopbackOpen")]   = true;    // programs on this device need no pairing (ServicePairing)
    d[QStringLiteral("serviceTokens")]         = QJsonArray();   // paired devices: [{id, name, hash, created, lastUsed}]

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
        "ELM_VOLTAGE",
        // Multi-value PIDs 0x61+ (ELM327Protocol::extendedPidTable)
        "DEMAND_ENGINE_TORQUE", "ACTUAL_ENGINE_TORQUE", "ENGINE_FRICTION_TORQUE",
        "REFERENCE_TORQUE", "BOOST_PRESSURE_A", "BOOST_PRESSURE_A_COMMANDED", "WASTEGATE_A",
        "WASTEGATE_A_COMMANDED", "MAF_SENSOR_A", "MAF_SENSOR_B", "INTAKE_TEMP_B1S1",
        "INTAKE_TEMP_B1S2", "INTAKE_TEMP_B2S1", "MANIFOLD_SURFACE_TEMP", "COOLANT_TEMP_SENSOR_1",
        "COOLANT_TEMP_SENSOR_2", "THROTTLE_POS_G", "THROTTLE_ACTUATOR_A_COMMANDED",
        "RELATIVE_THROTTLE_A", "EGR_A_COMMANDED", "EGR_A_ACTUAL", "EGR_A_ERROR",
        "FUEL_RAIL_PRESSURE_A", "FUEL_RAIL_PRESSURE_A_COMMANDED", "FUEL_RAIL_TEMP_A",
        "ENGINE_FUEL_RATE_GS", "VEHICLE_FUEL_RATE_GS", "CYLINDER_FUEL_RATE", "EXHAUST_FLOW_RATE",
        "TRANSMISSION_GEAR_RATIO", "RECOMMENDED_GEAR", "ODOMETER", "ENGINE_RUN_TIME_TOTAL",
        "ENGINE_IDLE_TIME_TOTAL", "HYBRID_BATTERY_VOLTAGE", "HYBRID_BATTERY_CURRENT",
        "EV_BATTERY_HEALTH"})
    {
        obdParams[QString::fromLatin1(p)] = false;
    }
    d[QStringLiteral("obdParameters")] = obdParams;

    d[QStringLiteral("homeOBDParameters")] = QJsonArray({
        QStringLiteral("SPEED"), QStringLiteral("RPM"),
        QStringLiteral("COOLANT_TEMP"), QStringLiteral("CONTROL_MODULE_VOLTAGE")
    });
    d[QStringLiteral("supportedOBDParameters")] = QJsonArray();
    d[QStringLiteral("mediaFolder")]             = QString();
    d[QStringLiteral("customThemes")]            = QJsonObject();
    d[QStringLiteral("mediaLastSource")]         = QStringLiteral("local");  // who owned the music: local | phone | androidauto (NowPlaying)
    d[QStringLiteral("mediaLastPlaying")]        = false;    // and whether it was playing
    d[QStringLiteral("androidAutoBackend")]      = QStringLiteral("dhu");      // "dhu" | "native" (OCTAVE is the head unit)
    d[QStringLiteral("androidAutoCertPath")]     = QString();  // head-unit certificate (PEM); "" = <config>/android-auto/headunit.crt + .key
    d[QStringLiteral("androidAutoResolution")]   = QStringLiteral("1280x720"); // 800x480 | 1280x720 | 1920x1080
    d[QStringLiteral("androidAutoFps")]          = 30;         // 30 | 60
    d[QStringLiteral("androidAutoDpi")]          = 160;        // Android Auto UI density
    d[QStringLiteral("airPlayEnabled")]          = false;      // advertise OCTAVE as an AirPlay receiver (AirPlayManager)
    d[QStringLiteral("airPlayName")]             = QStringLiteral("OCTAVE");   // what an iPhone lists under Screen Mirroring
    d[QStringLiteral("airPlayRequirePin")]       = true;       // a new iPhone types the on-screen PIN once
    d[QStringLiteral("airPlayResolution")]       = QStringLiteral("1280x720"); // size offered to the iPhone: 1280x720 | 1920x1080
    d[QStringLiteral("airPlayTouchEnabled")]     = true;       // OCTAVE is also a Bluetooth mouse for the mirrored iPhone
    d[QStringLiteral("airPlayTouchScale")]       = 600.0;      // mouse counts across the iPhone screen's short side (calibration)

    // Settings menu visibility
    QJsonObject menuVis;
    menuVis[QStringLiteral("displaySettings")]      = true;
    menuVis[QStringLiteral("mediaSettings")]        = true;
    menuVis[QStringLiteral("obdSettings")]          = true;
    menuVis[QStringLiteral("accessoriesSettings")]  = true;
    menuVis[QStringLiteral("deviceSettings")]       = true;
    menuVis[QStringLiteral("about")]                = true;
    d[QStringLiteral("settingsMenuVisibility")]     = menuVis;

    d[QStringLiteral("musicDeviceModel")] = QStringLiteral("Album art");

    // Gesture
    d[QStringLiteral("gestureMapping")]          = QJsonObject::fromVariantMap(GestureVocabulary::defaultMapping());
    d[QStringLiteral("pinnedSettings")]          = QJsonArray();

    // Expanded cards
    d[QStringLiteral("expandedCards")] = QJsonObject();

    // Directory history
    d[QStringLiteral("directoryHistory")] = QJsonArray();

    return d;
}

// ---------------------------------------------------------------------------
// getAppDataDir  — OS-specific config directory (static)
// ---------------------------------------------------------------------------
namespace {
QString &appDataDirOverride()
{
    static QString dir;
    return dir;
}
} // namespace

void SettingsManager::setAppDataDirOverride(const QString &dir)
{
    appDataDirOverride() = dir;
}

QString SettingsManager::getAppDataDir()
{
    if (!appDataDirOverride().isEmpty()) {
        QDir().mkpath(appDataDirOverride());
        return appDataDirOverride();
    }
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

    // Where the executable is: its parent is asked for the git information
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

    // Migrate legacy "Google Sans" -> "GoogleSans Regular". Main.qml derives the
    // display name from the bundled GoogleSans-Regular.ttf and Style.setFont()
    // rejects unknown names, so the old value silently left the app in the
    // system font.
    if (m_settings.value(QStringLiteral("fontSetting")).toString() == QStringLiteral("Google Sans")) {
        qCInfo(lcSettings) << "Migrating fontSetting from legacy 'Google Sans' to 'GoogleSans Regular'";
        m_settings[QStringLiteral("fontSetting")] = QStringLiteral("GoogleSans Regular");
        saveSettings(m_settings);
    }

    // Drop keys of removed features (the Spotify integration, the phone
    // audio toggle, music downloads); the Spotify client secret should not
    // linger on disk
    bool dropped = false;
    for (const QString &key : {QStringLiteral("spotifyClientId"), QStringLiteral("spotifyClientSecret"),
                               QStringLiteral("mediaSource"), QStringLiteral("scrcpyAudioEnabled"),
                               QStringLiteral("downloadSubfolder"), QStringLiteral("downloadFormat"),
                               QStringLiteral("downloadBitrate")}) {
        if (m_settings.contains(key)) {
            m_settings.remove(key);
            dropped = true;
        }
    }
    if (dropped) {
        qCInfo(lcSettings) << "Removed settings of retired features (Spotify, phone audio toggle, downloads)";
        saveSettings(m_settings);
        QFile::remove(getAppDataDir() + QStringLiteral("/.spotify_token_cache"));   // its login token
    }

    // Legacy "/dev/rfcomm0": it was the desktop default, means nothing on
    // Android (where the field holds a MAC address) and showed in the UI as
    // if the user had typed it
    if (m_settings.value(QStringLiteral("obdBluetoothPort")).toString() == QLatin1String("/dev/rfcomm0"))
        m_settings[QStringLiteral("obdBluetoothPort")] = QString();
    // The clock's own settings section became part of Display
    if (m_settings.value(QStringLiteral("lastSettingsSection")).toString() == QLatin1String("clockSettings"))
        m_settings[QStringLiteral("lastSettingsSection")] = QStringLiteral("displaySettings");

    m_settingsLoaded = true;
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &SettingsManager::flushPendingSave);

    populateMembers();

    // --- OBD parameter debounce timer ---
    m_obdParamsSaveTimer.setSingleShot(true);
    m_obdParamsSaveTimer.setInterval(800);
    connect(&m_obdParamsSaveTimer, &QTimer::timeout, this, &SettingsManager::flushObdParameters);
}

// ---------------------------------------------------------------------------
// populateMembers: everything derived from m_settings. Runs from the
// constructor and again from reset_to_defaults().
// ---------------------------------------------------------------------------
void SettingsManager::populateMembers()
{
    // The plain settings: each row's value in its own type. What the file
    // has is brought within the row's rule (see the table), and m_settings
    // is corrected with it, so the file and the properties agree.
    m_values.clear();
    m_values.reserve(rows().size());
    for (const SettingRow &r : rows()) {
        const QString key = QString::fromLatin1(r.key);
        QVariant v = fromJson(m_settings.value(key), r.type);
        if (!v.isValid() || !applyRule(r, v, true))
            v = r.def;
        m_values.append(v);
        const QJsonValue stored = QJsonValue::fromVariant(v);
        if (m_settings.value(key) != stored)
            m_settings.insert(key, stored);
    }

    populateStructured();

    // Current volume: from startUpVolume (linear), converted to 0-100 scale
    m_currentVolume = VolumeController::toPercentStatic(startUpVolume());
}

// The settings that are a list or a map, and the media folder
void SettingsManager::populateStructured()
{
    m_homeObdParameters.clear();
    m_settingsMenuVisibility.clear();
    m_gestureMapping.clear();
    m_pinnedSettings.clear();
    m_obdParameters.clear();
    m_supportedObdParameters.clear();
    m_directoryHistory.clear();

    // Read a value from m_settings falling back to defaults
    auto s = [&](const QString &key) -> QJsonValue {
        if (m_settings.contains(key))
            return m_settings.value(key);
        return m_defaultSettings.value(key);
    };

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

    // Home OBD parameters (JSON array -> QVariantList)
    {
        QJsonArray arr = s(QStringLiteral("homeOBDParameters")).toArray();
        for (const auto &v : arr)
            m_homeObdParameters.append(v.toString());
    }

    // Settings menu visibility
    {
        QJsonObject vis = s(QStringLiteral("settingsMenuVisibility")).toObject();
        // Ensure all default sections exist; a section missing from the stored
        // file is visible (matches is_settings_section_visible / SettingsMenu.qml).
        QJsonObject defVis = m_defaultSettings.value(QStringLiteral("settingsMenuVisibility")).toObject();
        for (auto it = defVis.begin(); it != defVis.end(); ++it) {
            if (!vis.contains(it.key()))
                vis[it.key()] = true;
        }
        for (auto it = vis.begin(); it != vis.end(); ++it)
            m_settingsMenuVisibility[it.key()] = it.value().toVariant();
    }

    // Gesture mapping
    {
        QJsonObject gm = s(QStringLiteral("gestureMapping")).toObject();
        for (auto it = gm.begin(); it != gm.end(); ++it)
            m_gestureMapping[it.key()] = it.value().toVariant();
    }

    // Pinned settings
    {
        QJsonArray arr = s(QStringLiteral("pinnedSettings")).toArray();
        for (const auto &v : arr)
            m_pinnedSettings.append(v.toString());
    }

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
// loadSettings — the settings in memory (the file, the first time)
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
    if (!QFile::exists(m_settingsFile)) {
        writeSettingsToDisk(m_defaultSettings);
        return m_defaultSettings;
    }

    QString error;
    QJsonObject settings;
    if (parseSettingsFile(m_settingsFile, settings, error)) {
        backupSettingsFile();
        return settings;
    }

    // A power cut mid-write can leave a truncated file. Keep it for
    // inspection and fall back to the last known-good copy, so the next
    // save doesn't overwrite the user's settings with defaults.
    qCWarning(lcSettings) << "Settings file corrupted:" << error;
    const QString corruptPath = m_settingsFile + QStringLiteral(".corrupt");
    QFile::remove(corruptPath);
    if (QFile::rename(m_settingsFile, corruptPath))
        qCWarning(lcSettings) << "Corrupted settings moved to" << corruptPath;

    if (parseSettingsFile(backupFilePath(), settings, error)) {
        qCWarning(lcSettings) << "Restored settings from backup";
        writeSettingsToDisk(settings);
        return settings;
    }
    qCWarning(lcSettings) << "No usable settings backup (" << error << "), using defaults";
    return m_defaultSettings;
}

QString SettingsManager::backupFilePath() const
{
    return m_settingsFile + QStringLiteral(".bak");
}

bool SettingsManager::parseSettingsFile(const QString &path, QJsonObject &out, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = f.errorString();
        return false;
    }
    QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        error = err.error != QJsonParseError::NoError ? err.errorString()
                                                      : QStringLiteral("not a JSON object");
        return false;
    }
    out = validateSettings(doc.object());
    return true;
}

// Copy the settings file that just parsed cleanly to .bak, once per session.
// That is the copy a corrupted file is restored from.
void SettingsManager::backupSettingsFile()
{
    if (m_backupDone)
        return;
    m_backupDone = true;
    QFile f(m_settingsFile);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcSettings) << "Could not back up settings:" << f.errorString();
        return;
    }
    const QByteArray data = f.readAll();
    f.close();
    atomicWrite(backupFilePath(), data);
}

// Write via QSaveFile (temp file, fsync, rename over the target) so a power
// cut leaves either the old file or the new one, never a truncated one.
bool SettingsManager::atomicWrite(const QString &path, const QByteArray &data)
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        qCWarning(lcSettings) << "Cannot open" << path << "for writing:" << out.errorString();
        return false;
    }
    if (out.write(data) != data.size() || !out.commit()) {
        qCWarning(lcSettings) << "Failed to write" << path << ":" << out.errorString();
        return false;
    }
    setFilePermissions(path);
    return true;
}

// ---------------------------------------------------------------------------
// saveSettings — update the in-memory copy, write to disk once things settle
// ---------------------------------------------------------------------------
void SettingsManager::saveSettings(const QJsonObject &settings)
{
    OCTAVE_PERF_SCOPE("settings.save_settings");
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
// writeSettingsToDisk — atomic write (QSaveFile: temp + fsync + rename)
// ---------------------------------------------------------------------------
void SettingsManager::writeSettingsToDisk(const QJsonObject &settings)
{
    // Callers pass an already validated object (saveSettings / the defaults).
    // On failure the file on disk is left untouched; the next save retries.
    atomicWrite(m_settingsFile, QJsonDocument(settings).toJson(QJsonDocument::Indented));
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
// Property getters (a plain setting reads its table row)
// =========================================================================

// --- Device ---
QString SettingsManager::deviceName() const { return row(R_deviceName).toString(); }
QString SettingsManager::fontSetting() const { return row(R_fontSetting).toString(); }

// --- Theme / Display ---
QString SettingsManager::themeSetting() const { return row(R_themeSetting).toString(); }
float SettingsManager::uiScale() const { return static_cast<float>(row(R_uiScale).toDouble()); }
int SettingsManager::colorTransitionMs() const { return row(R_colorTransitionMs).toInt(); }
bool SettingsManager::songLengthTransition() const { return row(R_songLengthTransition).toBool(); }
bool SettingsManager::showClock() const { return row(R_showClock).toBool(); }
bool SettingsManager::clockFormat24Hour() const { return row(R_clockFormat24Hour).toBool(); }
bool SettingsManager::clockShowSeconds() const { return row(R_clockShowSeconds).toBool(); }
int SettingsManager::clockSize() const { return row(R_clockSize).toInt(); }
bool SettingsManager::clockSizeAuto() const { return row(R_clockSizeAuto).toBool(); }
QString SettingsManager::backgroundGrid() const { return row(R_backgroundGrid).toString(); }
int SettingsManager::screenWidth() const { return row(R_screenWidth).toInt(); }
int SettingsManager::screenHeight() const { return row(R_screenHeight).toInt(); }
int SettingsManager::backgroundBlurRadius() const { return row(R_backgroundBlurRadius).toInt(); }
QString SettingsManager::bottomBarOrientation() const { return row(R_bottomBarOrientation).toString(); }
bool SettingsManager::showBottomBarMediaControls() const { return row(R_showBottomBarMediaControls).toBool(); }
QString SettingsManager::settingsLayoutStyle() const { return row(R_settingsLayoutStyle).toString(); }
QVariantMap SettingsManager::albumArtColors() const
{
    return QJsonDocument::fromJson(row(R_albumArtColors).toString().toUtf8()).object().toVariantMap();
}

QString SettingsManager::settingsMenuVisibility() const
{
    QJsonObject obj;
    for (auto it = m_settingsMenuVisibility.begin(); it != m_settingsMenuVisibility.end(); ++it)
        obj[it.key()] = QJsonValue::fromVariant(it.value());
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

// --- Media ---
float SettingsManager::startUpVolume() const { return static_cast<float>(row(R_startUpVolume).toDouble()); }
int     SettingsManager::currentVolume() const     { return m_currentVolume; }
QString SettingsManager::mediaFolder() const       { return m_mediaFolder; }
bool SettingsManager::autoPlayOnStartup() const { return row(R_autoPlayOnStartup).toBool(); }
bool SettingsManager::persistShuffleState() const { return row(R_persistShuffleState).toBool(); }
bool SettingsManager::lastShuffleState() const { return row(R_lastShuffleState).toBool(); }
QString SettingsManager::lastPlayedSong() const { return row(R_lastPlayedSong).toString(); }
int SettingsManager::lastPlayedPosition() const { return row(R_lastPlayedPosition).toInt(); }
QString SettingsManager::lastPlayedPlaylist() const { return row(R_lastPlayedPlaylist).toString(); }
QString SettingsManager::musicButtonDefaultPage() const { return row(R_musicButtonDefaultPage).toString(); }
bool SettingsManager::returnToLibraryAfterSelection() const { return row(R_returnToLibraryAfterSelection).toBool(); }
bool SettingsManager::showWaveformVisualizer() const { return row(R_showWaveformVisualizer).toBool(); }

// --- 3D effects ---
bool SettingsManager::show3DButtonTilt() const { return row(R_show3DButtonTilt).toBool(); }
bool SettingsManager::show3DAlbumPreview() const { return row(R_show3DAlbumPreview).toBool(); }
bool SettingsManager::roundedAlbumArt() const { return row(R_roundedAlbumArt).toBool(); }
int SettingsManager::albumArtCornerRadius() const { return row(R_albumArtCornerRadius).toInt(); }
bool SettingsManager::showAlbumArtShadow() const { return row(R_showAlbumArtShadow).toBool(); }
bool SettingsManager::vinylRecordMode() const { return row(R_vinylRecordMode).toBool(); }
QString SettingsManager::albumArtTransition() const { return row(R_albumArtTransition).toString(); }
QString SettingsManager::audioOutputDevice() const { return row(R_audioOutputDevice).toString(); }
int SettingsManager::backgroundOverlayOpacity() const { return row(R_backgroundOverlayOpacity).toInt(); }
double SettingsManager::sideCardOpacity() const { return row(R_sideCardOpacity).toDouble(); }
int SettingsManager::sideCardAngle() const { return row(R_sideCardAngle).toInt(); }
int SettingsManager::buttonTiltDuration() const { return row(R_buttonTiltDuration).toInt(); }
int SettingsManager::textScrollSpeed() const { return row(R_textScrollSpeed).toInt(); }

// --- OBD ---
QString SettingsManager::obdBluetoothPort() const { return row(R_obdBluetoothPort).toString(); }
QVariantList SettingsManager::obdSavedAdapters() const
{
    return QJsonDocument::fromJson(row(R_obdSavedAdapters).toString().toUtf8()).array().toVariantList();
}
bool SettingsManager::obdFastMode() const { return row(R_obdFastMode).toBool(); }
bool SettingsManager::obdCardStyleCircular() const { return row(R_obdCardStyleCircular).toBool(); }
QString SettingsManager::gaugeTempUnit() const { return row(R_gaugeTempUnit).toString(); }
QString SettingsManager::gaugeSpeedUnit() const { return row(R_gaugeSpeedUnit).toString(); }
int SettingsManager::obdAutoReconnectAttempts() const { return row(R_obdAutoReconnectAttempts).toInt(); }
float SettingsManager::fuelTankCapacity() const { return static_cast<float>(row(R_fuelTankCapacity).toDouble()); }
bool SettingsManager::blackBoxEnabled() const { return row(R_blackBoxEnabled).toBool(); }
bool SettingsManager::blackBoxSweepAll() const { return row(R_blackBoxSweepAll).toBool(); }
bool SettingsManager::homeVehicleCard() const { return row(R_homeVehicleCard).toBool(); }
int SettingsManager::blackBoxMaxMB() const { return row(R_blackBoxMaxMB).toInt(); }
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

QVariantMap SettingsManager::customThemeData() const
{
    QJsonObject settings = const_cast<SettingsManager *>(this)->loadSettings();
    return settings.value(QStringLiteral("customThemes")).toObject().toVariantMap();
}

QVariantList SettingsManager::directoryHistory() const { return m_directoryHistory; }

// --- Window ---
QString SettingsManager::windowState() const { return row(R_windowState).toString(); }
QString SettingsManager::lastSettingsSection() const { return row(R_lastSettingsSection).toString(); }

// --- Android Auto / Phone Mirror ---
bool SettingsManager::androidAutoEnabled() const { return row(R_androidAutoEnabled).toBool(); }
bool SettingsManager::phoneMirrorEnabled() const { return row(R_phoneMirrorEnabled).toBool(); }
QString SettingsManager::scrcpyDisplaySize() const { return row(R_scrcpyDisplaySize).toString(); }
double SettingsManager::scrcpyAudioGain() const { return row(R_scrcpyAudioGain).toDouble(); }
bool SettingsManager::scrcpyAudioDuckEnabled() const { return row(R_scrcpyAudioDuckEnabled).toBool(); }
double SettingsManager::scrcpyAudioDuckLevel() const { return row(R_scrcpyAudioDuckLevel).toDouble(); }
bool SettingsManager::scrcpyPhoneScreenOff() const { return row(R_scrcpyPhoneScreenOff).toBool(); }
int SettingsManager::scrcpyDisplayDpi() const { return row(R_scrcpyDisplayDpi).toInt(); }
int SettingsManager::scrcpyBitRateUsb() const { return row(R_scrcpyBitRateUsb).toInt(); }
int SettingsManager::scrcpyBitRateWifi() const { return row(R_scrcpyBitRateWifi).toInt(); }
bool SettingsManager::scrcpyAutoWifi() const { return row(R_scrcpyAutoWifi).toBool(); }
bool SettingsManager::scrcpyBitRateAuto() const { return row(R_scrcpyBitRateAuto).toBool(); }
QString SettingsManager::scrcpyWirelessAddress() const { return row(R_scrcpyWirelessAddress).toString(); }

// --- ESP32 ---
bool SettingsManager::esp32VolumeEnabled() const { return row(R_esp32VolumeEnabled).toBool(); }
QString SettingsManager::esp32VolumePort() const { return row(R_esp32VolumePort).toString(); }
double SettingsManager::esp32VolumeStepSize() const { return row(R_esp32VolumeStepSize).toDouble(); }
bool SettingsManager::esp32AutoReconnect() const { return row(R_esp32AutoReconnect).toBool(); }
bool SettingsManager::esp32LedSleepEnabled() const { return row(R_esp32LedSleepEnabled).toBool(); }
QString SettingsManager::esp32LedColorMode() const { return row(R_esp32LedColorMode).toString(); }
QString SettingsManager::esp32LedStaticColor() const { return row(R_esp32LedStaticColor).toString(); }

// --- IMU ---
bool SettingsManager::imuEnabled() const { return row(R_imuEnabled).toBool(); }
int SettingsManager::sensorEmitRate() const { return row(R_sensorEmitRate).toInt(); }
int SettingsManager::sensorDecimalPlaces() const { return row(R_sensorDecimalPlaces).toInt(); }
QString SettingsManager::sensorTempUnit() const { return row(R_sensorTempUnit).toString(); }
QString SettingsManager::sensorAltitudeUnit() const { return row(R_sensorAltitudeUnit).toString(); }
QVariantMap SettingsManager::sensorCards() const
{
    QVariantMap cards;
    for (const QString &card : kSensorCards)
        cards.insert(card, row(rowIndex(QStringLiteral("sensorShow") + card)).toBool());
    return cards;
}

// --- Gesture ---
bool SettingsManager::gestureSensorEnabled() const { return row(R_gestureSensorEnabled).toBool(); }
int SettingsManager::gestureVolumeStep() const { return row(R_gestureVolumeStep).toInt(); }
int SettingsManager::gestureCooldown() const { return row(R_gestureCooldown).toInt(); }

// --- Pinned ---
QVariantList SettingsManager::pinnedSettings() const   { return m_pinnedSettings; }

// =========================================================================
// Save / setter slots (a plain setting goes through setRow())
// =========================================================================

// --- Device ---
void SettingsManager::save_device_name(const QString &name) { setRow(R_deviceName, name); }
void SettingsManager::save_font_setting(const QString &font) { setRow(R_fontSetting, font); }

// --- Theme / Display ---
void SettingsManager::save_theme_setting(const QString &theme) { setRow(R_themeSetting, theme); }
void SettingsManager::save_ui_scale(float scale) { setRow(R_uiScale, static_cast<double>(scale)); }
void SettingsManager::save_color_transition_ms(int ms) { setRow(R_colorTransitionMs, ms); }
void SettingsManager::save_song_length_transition(bool enabled) { setRow(R_songLengthTransition, enabled); }
void SettingsManager::save_show_clock(bool show) { setRow(R_showClock, show); }
void SettingsManager::save_clock_format(bool is24hour) { setRow(R_clockFormat24Hour, is24hour); }
void SettingsManager::save_clock_show_seconds(bool show) { setRow(R_clockShowSeconds, show); }
void SettingsManager::save_clock_size(int size) { setRow(R_clockSize, size); }
void SettingsManager::save_clock_size_auto(bool autoSize) { setRow(R_clockSizeAuto, autoSize); }
void SettingsManager::save_background_grid(const QString &grid) { setRow(R_backgroundGrid, grid); }
void SettingsManager::save_screen_width(int width) { setRow(R_screenWidth, width); }
void SettingsManager::save_screen_height(int height) { setRow(R_screenHeight, height); }
void SettingsManager::save_background_blur_radius(int radius) { setRow(R_backgroundBlurRadius, radius); }
void SettingsManager::save_bottom_bar_orientation(const QString &orientation) { setRow(R_bottomBarOrientation, orientation); }
void SettingsManager::save_show_bottom_bar_media_controls(bool show) { setRow(R_showBottomBarMediaControls, show); }
void SettingsManager::save_settings_layout_style(const QString &style) { setRow(R_settingsLayoutStyle, style); }
void SettingsManager::set_album_art_colors(const QString &colorsJson)
{
    // The palette reaches QML through this signal and nothing else (Style
    // holds its own copy, which Album Art Capture being switched off and on
    // clears), so the same palette again is still announced. Unchanged, it
    // is not written again.
    if (setRow(R_albumArtColors, colorsJson) == SetResult::Unchanged)
        emit albumArtColorsChanged();
}

// --- Media ---
void SettingsManager::save_start_volume(float volume) { setRow(R_startUpVolume, static_cast<double>(volume)); }

void SettingsManager::setCurrentVolume(int volume)
{
    if (m_currentVolume != volume) {
        m_currentVolume = volume;
        emit currentVolumeChanged(volume);
    }
}

bool SettingsManager::save_media_folder_url(const QUrl &url)
{
    const QString path = PathUrl::folderPath(url);
    if (path.isEmpty()) {
        qCWarning(lcSettings) << "Media folder not usable as a path:" << url.toString();
        return false;
    }
    return save_media_folder(path);
}

bool SettingsManager::save_media_folder(const QString &folderPathIn)
{
    // The rules save_media_folder_url applies, for a typed path too: a URL
    // becomes its path; it must be an absolute path and never the
    // filesystem root (it may be absent now: a card not inserted yet)
    const QUrl asUrl(folderPathIn);
    const QString folderPath = QDir::cleanPath(asUrl.isLocalFile() ? PathUrl::folderPath(asUrl) : folderPathIn.trimmed());
    if (folderPathIn.trimmed().isEmpty() || !QDir::isAbsolutePath(folderPath) || QDir(folderPath).isRoot()) {
        qCWarning(lcSettings) << "Media folder refused (not a folder this can use):" << folderPathIn;
        return false;
    }
    if (folderPath == m_mediaFolder) {
        qCDebug(lcSettings) << "Media folder unchanged, skipping:" << folderPath;
        return true;
    }
    qCDebug(lcSettings) << "Saving media folder path:" << folderPath;
    m_mediaFolder = folderPath;
    updateSetting(QStringLiteral("mediaFolder"), folderPath);
    emit mediaFolderChanged(folderPath);
    return true;
}

void SettingsManager::save_auto_play_on_startup(bool enabled) { setRow(R_autoPlayOnStartup, enabled); }
void SettingsManager::save_persist_shuffle_state(bool enabled) { setRow(R_persistShuffleState, enabled); }
void SettingsManager::save_last_shuffle_state(bool enabled) { setRow(R_lastShuffleState, enabled); }
void SettingsManager::save_last_played_song(const QString &filename) { setRow(R_lastPlayedSong, filename); }
void SettingsManager::save_last_played_position(int positionMs) { setRow(R_lastPlayedPosition, positionMs); }
void SettingsManager::save_last_played_playlist(const QString &playlistName) { setRow(R_lastPlayedPlaylist, playlistName); }

void SettingsManager::save_playback_state(const QString &song, int positionMs, const QString &playlist)
{
    setRow(R_lastPlayedSong, song);
    setRow(R_lastPlayedPosition, positionMs);
    setRow(R_lastPlayedPlaylist, playlist);
}

void SettingsManager::save_music_button_default_page(const QString &page) { setRow(R_musicButtonDefaultPage, page); }
void SettingsManager::save_return_to_library_after_selection(bool returnToLibrary) { setRow(R_returnToLibraryAfterSelection, returnToLibrary); }
void SettingsManager::save_show_waveform_visualizer(bool enabled) { setRow(R_showWaveformVisualizer, enabled); }

// --- 3D visual effects ---
void SettingsManager::save_show_3d_button_tilt(bool enabled) { setRow(R_show3DButtonTilt, enabled); }
void SettingsManager::save_show_3d_album_preview(bool enabled) { setRow(R_show3DAlbumPreview, enabled); }
void SettingsManager::save_rounded_album_art(bool enabled) { setRow(R_roundedAlbumArt, enabled); }
void SettingsManager::save_album_art_corner_radius(int radius) { setRow(R_albumArtCornerRadius, radius); }
void SettingsManager::save_show_album_art_shadow(bool enabled) { setRow(R_showAlbumArtShadow, enabled); }
void SettingsManager::save_vinyl_record_mode(bool enabled) { setRow(R_vinylRecordMode, enabled); }
void SettingsManager::save_audio_output_device(const QString &id) { setRow(R_audioOutputDevice, id); }
void SettingsManager::save_album_art_transition(const QString &transition) { setRow(R_albumArtTransition, transition); }
void SettingsManager::save_background_overlay_opacity(int opacity) { setRow(R_backgroundOverlayOpacity, opacity); }
void SettingsManager::save_side_card_opacity(double opacity) { setRow(R_sideCardOpacity, opacity); }
void SettingsManager::save_side_card_angle(int angle) { setRow(R_sideCardAngle, angle); }
void SettingsManager::save_button_tilt_duration(int duration) { setRow(R_buttonTiltDuration, duration); }
void SettingsManager::save_text_scroll_speed(int speed) { setRow(R_textScrollSpeed, speed); }

// --- OBD ---
void SettingsManager::save_obd_bluetooth_port(const QString &port) { setRow(R_obdBluetoothPort, port); }
void SettingsManager::save_obd_saved_adapters(const QString &json) { setRow(R_obdSavedAdapters, json); }

void SettingsManager::add_obd_saved_adapter(const QString &name, const QString &mac)
{
    QString trimmed = mac.trimmed().toUpper();
    if (trimmed.isEmpty()) return;
    // Upper-case the MAC, not the scheme of a direct-Bluetooth URL
    if (trimmed.startsWith(QStringLiteral("RFCOMM://")))
        trimmed.replace(0, 9, QStringLiteral("rfcomm://"));

    QJsonArray arr = QJsonDocument::fromJson(row(R_obdSavedAdapters).toString().toUtf8()).array();
    // Remove any existing entry with the same MAC, then prepend the new one
    // so the most recently used adapter is first.
    for (int i = arr.size() - 1; i >= 0; --i) {
        const QJsonObject o = arr.at(i).toObject();
        if (o.value(QStringLiteral("mac")).toString().compare(trimmed, Qt::CaseInsensitive) == 0) {
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

    QJsonArray arr = QJsonDocument::fromJson(row(R_obdSavedAdapters).toString().toUtf8()).array();
    for (int i = arr.size() - 1; i >= 0; --i) {
        const QJsonObject o = arr.at(i).toObject();
        if (o.value(QStringLiteral("mac")).toString().toUpper() == trimmed) {
            arr.removeAt(i);
        }
    }
    save_obd_saved_adapters(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void SettingsManager::save_obd_fast_mode(bool enabled) { setRow(R_obdFastMode, enabled); }
void SettingsManager::save_obd_card_style_circular(bool circular) { setRow(R_obdCardStyleCircular, circular); }
void SettingsManager::save_gauge_temp_unit(const QString &unit) { setRow(R_gaugeTempUnit, unit); }
void SettingsManager::save_gauge_speed_unit(const QString &unit) { setRow(R_gaugeSpeedUnit, unit); }
void SettingsManager::save_obd_auto_reconnect_attempts(int attempts) { setRow(R_obdAutoReconnectAttempts, attempts); }
void SettingsManager::save_fuel_tank_capacity(float capacity) { setRow(R_fuelTankCapacity, static_cast<double>(capacity)); }
void SettingsManager::save_black_box_enabled(bool enabled) { setRow(R_blackBoxEnabled, enabled); }
void SettingsManager::save_black_box_sweep_all(bool enabled) { setRow(R_blackBoxSweepAll, enabled); }
void SettingsManager::setHomeVehicleCard(bool enabled) { setRow(R_homeVehicleCard, enabled); }
void SettingsManager::save_black_box_max_mb(int megabytes) { setRow(R_blackBoxMaxMB, megabytes); }

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
    QVariantList clean;
    for (const QVariant &v : parameters) {
        const QString id = v.toString();
        if (!id.isEmpty() && !clean.contains(id) && clean.size() < kMaxHomeObdParameters)
            clean.append(id);
    }
    m_homeObdParameters = clean;
    QJsonObject settings = loadSettings();
    QJsonArray arr;
    for (const auto &v : clean)
        arr.append(v.toString());
    settings[QStringLiteral("homeOBDParameters")] = arr;
    saveSettings(settings);
    emit homeOBDParametersChanged();
}

bool SettingsManager::add_home_obd_parameter(const QString &id, int index)
{
    if (id.isEmpty() || m_homeObdParameters.contains(id) || m_homeObdParameters.size() >= kMaxHomeObdParameters)
        return false;
    QVariantList list = m_homeObdParameters;
    list.insert(index < 0 || index > list.size() ? list.size() : index, id);
    save_home_obd_parameters(list);
    return true;
}

bool SettingsManager::remove_home_obd_parameter(const QString &id)
{
    QVariantList list = m_homeObdParameters;
    if (!list.removeOne(id))
        return false;
    save_home_obd_parameters(list);
    return true;
}

bool SettingsManager::move_home_obd_parameter(int from, int to)
{
    const int n = int(m_homeObdParameters.size());
    to = qMin(to, n - 1);   // dropped past the last card: it goes last
    if (from < 0 || from >= n || to < 0 || from == to)
        return false;
    QVariantList list = m_homeObdParameters;
    list.move(from, to);
    save_home_obd_parameters(list);
    return true;
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
void SettingsManager::save_custom_theme(const QString &name, const QVariantMap &themeData,
                                         const QString &albumArtSource)
{
    qCDebug(lcSettings) << "Saving custom theme:" << name;
    QJsonObject settings = loadSettings();
    if (!settings.contains(QStringLiteral("customThemes")))
        settings[QStringLiteral("customThemes")] = QJsonObject();

    QJsonObject theme = QJsonObject::fromVariantMap(themeData);

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
    // Download on a thread; the result comes back to this (GUI) thread as a
    // queued call. It used to be left in two members the GUI polled every
    // 100 ms with no lock, and a failed download left that timer running
    // for the life of the process.
    QThread *thread = QThread::create([this, themeName, url, artDir, safeName]() {
        QDir().mkpath(artDir);
        const QString dest = artDir + QStringLiteral("/theme_") + safeName + QStringLiteral(".jpg");
        // Use QProcess + curl as a portable download approach
#if QT_CONFIG(process)
        QProcess proc;
        proc.start(QStringLiteral("curl"), {QStringLiteral("-sL"), QStringLiteral("-o"), dest, url});
        const bool downloaded = proc.waitForFinished(30000) && proc.exitStatus() == QProcess::NormalExit
                                && proc.exitCode() == 0;
#else
        const bool downloaded = false;   // no processes in the browser (wasm)
#endif
        if (!downloaded || !QFile::exists(dest)) {
            qCWarning(lcSettings) << "Failed to download theme album art from:" << url;
            return;
        }
        qCDebug(lcSettings) << "Downloaded theme art to:" << dest;
        const QString localUrl = QUrl::fromLocalFile(dest).toString();
        QMetaObject::invokeMethod(this, [this, themeName, localUrl]() {
            QJsonObject settings = loadSettings();
            QJsonObject themes = settings.value(QStringLiteral("customThemes")).toObject();
            if (!themes.contains(themeName))
                return;   // deleted while the picture was on its way
            QJsonObject t = themes.value(themeName).toObject();
            t[QStringLiteral("albumArt")] = localUrl;
            themes[themeName] = t;
            settings[QStringLiteral("customThemes")] = themes;
            saveSettings(settings);
            emit customThemesChanged();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
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

// --- Window / Settings navigation ---
void SettingsManager::save_window_state(const QString &state) { setRow(R_windowState, state); }
void SettingsManager::set_last_settings_section(const QString &section) { setRow(R_lastSettingsSection, section); }

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
void SettingsManager::save_android_auto_enabled(bool enabled) { setRow(R_androidAutoEnabled, enabled); }
void SettingsManager::save_phone_mirror_enabled(bool enabled) { setRow(R_phoneMirrorEnabled, enabled); }
void SettingsManager::save_scrcpy_display_size(const QString &size) { setRow(R_scrcpyDisplaySize, size); }
void SettingsManager::save_scrcpy_audio_gain(double gain) { setRow(R_scrcpyAudioGain, gain); }
void SettingsManager::save_scrcpy_audio_duck_enabled(bool enabled) { setRow(R_scrcpyAudioDuckEnabled, enabled); }

double SettingsManager::scrcpyAudioGainDb() const { return UnitsFn::linearToDb(scrcpyAudioGain()); }
double SettingsManager::scrcpyAudioDuckLevelDb() const { return UnitsFn::linearToDb(scrcpyAudioDuckLevel()); }

void SettingsManager::save_scrcpy_audio_gain_db(double db)
{
    save_scrcpy_audio_gain(UnitsFn::dbToLinear(qBound(kAudioGainMinDb, db, kAudioGainMaxDb)));
}

void SettingsManager::save_scrcpy_audio_duck_level_db(double db)
{
    save_scrcpy_audio_duck_level(UnitsFn::dbToLinear(qBound(kAudioDuckMinDb, db, kAudioDuckMaxDb)));
}

void SettingsManager::save_scrcpy_audio_duck_level(double level) { setRow(R_scrcpyAudioDuckLevel, level); }
void SettingsManager::save_scrcpy_phone_screen_off(bool off) { setRow(R_scrcpyPhoneScreenOff, off); }
void SettingsManager::save_scrcpy_display_dpi(int dpi) { setRow(R_scrcpyDisplayDpi, dpi); }
void SettingsManager::save_scrcpy_bit_rate_usb(int mbps) { setRow(R_scrcpyBitRateUsb, mbps); }
void SettingsManager::save_scrcpy_bit_rate_wifi(int mbps) { setRow(R_scrcpyBitRateWifi, mbps); }
void SettingsManager::save_scrcpy_auto_wifi(bool enabled) { setRow(R_scrcpyAutoWifi, enabled); }
void SettingsManager::save_scrcpy_bit_rate_auto(bool enabled) { setRow(R_scrcpyBitRateAuto, enabled); }
void SettingsManager::save_scrcpy_wireless_address(const QString &address) { setRow(R_scrcpyWirelessAddress, address); }

// --- ESP32 ---
void SettingsManager::save_esp32_volume_enabled(bool enabled) { setRow(R_esp32VolumeEnabled, enabled); }
void SettingsManager::save_esp32_volume_port(const QString &port) { setRow(R_esp32VolumePort, port); }
void SettingsManager::save_esp32_volume_step_size(double stepSize) { setRow(R_esp32VolumeStepSize, stepSize); }
void SettingsManager::save_esp32_auto_reconnect(bool enabled) { setRow(R_esp32AutoReconnect, enabled); }
void SettingsManager::save_esp32_led_sleep_enabled(bool enabled) { setRow(R_esp32LedSleepEnabled, enabled); }
void SettingsManager::save_esp32_led_color_mode(const QString &mode) { setRow(R_esp32LedColorMode, mode); }
void SettingsManager::save_esp32_led_static_color(const QString &color) { setRow(R_esp32LedStaticColor, color); }

// --- IMU ---
void SettingsManager::save_imu_enabled(bool enabled) { setRow(R_imuEnabled, enabled); }
void SettingsManager::save_sensor_emit_rate(int hz) { setRow(R_sensorEmitRate, hz); }
void SettingsManager::save_sensor_decimal_places(int places) { setRow(R_sensorDecimalPlaces, places); }
void SettingsManager::save_sensor_temp_unit(const QString &unit) { setRow(R_sensorTempUnit, unit); }
void SettingsManager::save_sensor_altitude_unit(const QString &unit) { setRow(R_sensorAltitudeUnit, unit); }

void SettingsManager::save_sensor_card_visible(const QString &card, bool visible)
{
    if (!kSensorCards.contains(card))
        return;
    setRow(rowIndex(QStringLiteral("sensorShow") + card), visible);
}

// --- Gesture ---
void SettingsManager::save_gesture_sensor_enabled(bool enabled) { setRow(R_gestureSensorEnabled, enabled); }

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

void SettingsManager::save_gesture_volume_step(int step) { setRow(R_gestureVolumeStep, step); }
void SettingsManager::save_gesture_cooldown(int ms) { setRow(R_gestureCooldown, ms); }

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
const QStringList &SettingsManager::fileOnlyKeys()
{
    static const QStringList keys = {QStringLiteral("serviceEnabled"), QStringLiteral("serviceWebRoot"),
                                     QStringLiteral("serviceLoopbackOpen"), QStringLiteral("serviceTokens")};
    return keys;
}

QVariant SettingsManager::fileOnlySetting(const QString &key)
{
    if (!fileOnlyKeys().contains(key))
        return QVariant();
    const QJsonObject settings = loadSettings();
    return settings.contains(key) ? settings.value(key).toVariant() : QVariant();
}

void SettingsManager::saveFileOnlySetting(const QString &key, const QVariant &value)
{
    if (!fileOnlyKeys().contains(key))
        return;
    updateSetting(key, value);
}

QVariantList SettingsManager::nowPlayingPresets()
{
    auto preset = [](const char *label, const QVariantMap &settings) {
        return QVariantMap{{QStringLiteral("label"), QString::fromUtf8(label)}, {QStringLiteral("settings"), settings}};
    };
    return {
        preset("Minimal", {{"roundedAlbumArt", true}, {"showAlbumArtShadow", false}, {"vinylRecordMode", false},
                           {"showWaveformVisualizer", false}, {"show3DButtonTilt", false}, {"show3DAlbumPreview", false},
                           {"backgroundBlurRadius", 60}, {"backgroundOverlayOpacity", 80},
                           {"albumArtCornerRadius", 16}, {"albumArtTransition", "Crossfade"}, {"backgroundGrid", "Normal"}}),
        preset("Vinyl", {{"roundedAlbumArt", false}, {"showAlbumArtShadow", true}, {"vinylRecordMode", true},
                         {"showWaveformVisualizer", true}, {"show3DButtonTilt", false}, {"show3DAlbumPreview", false},
                         {"backgroundBlurRadius", 30}, {"backgroundOverlayOpacity", 60},
                         {"albumArtTransition", "Vinyl Lift"}, {"backgroundGrid", "Normal"}}),
        preset("Showcase 3D", {{"roundedAlbumArt", true}, {"showAlbumArtShadow", true}, {"vinylRecordMode", false},
                               {"showWaveformVisualizer", false}, {"show3DButtonTilt", true}, {"show3DAlbumPreview", true},
                               {"backgroundBlurRadius", 50}, {"backgroundOverlayOpacity", 70},
                               {"albumArtCornerRadius", 16}, {"albumArtTransition", "Coverflow"},
                               {"backgroundGrid", "4x4"},
                               {"sideCardOpacity", 0.4}, {"sideCardAngle", 30}, {"buttonTiltDuration", 200}}),
        preset("Cinema", {{"roundedAlbumArt", true}, {"showAlbumArtShadow", true}, {"vinylRecordMode", false},
                          {"showWaveformVisualizer", true}, {"show3DButtonTilt", false}, {"show3DAlbumPreview", false},
                          {"backgroundBlurRadius", 80}, {"backgroundOverlayOpacity", 90},
                          {"albumArtCornerRadius", 24}, {"albumArtTransition", "Dissolve"}, {"backgroundGrid", "4x4"}}),
    };
}

bool SettingsManager::applyNowPlayingPreset(const QString &label)
{
    for (const QVariant &p : nowPlayingPresets()) {
        const QVariantMap preset = p.toMap();
        if (preset.value(QStringLiteral("label")).toString() == label) {
            apply_settings(preset.value(QStringLiteral("settings")).toMap());
            return true;
        }
    }
    return false;
}

namespace {
// showWaveformVisualizer -> show_waveform_visualizer, show3DButtonTilt ->
// show_3d_button_tilt
QString snakeCase(const QString &camel)
{
    QString s = camel;
    static const QRegularExpression lowerThenUpper(QStringLiteral("([a-z])([A-Z0-9])"));
    static const QRegularExpression upperThenWord(QStringLiteral("([A-Z0-9])([A-Z][a-z])"));
    s.replace(lowerThenUpper, QStringLiteral("\\1_\\2"));
    s.replace(upperThenWord, QStringLiteral("\\1_\\2"));
    return s.toLower();
}

// `v` as a value of `type`, only when it already is one ("true" is not a bool)
bool strictArg(QVariant &v, QMetaType type)
{
    const int id = v.typeId();
    const bool number = id == QMetaType::Int || id == QMetaType::LongLong || id == QMetaType::Double
                        || id == QMetaType::UInt || id == QMetaType::ULongLong;
    switch (type.id()) {
    case QMetaType::Bool:
        return id == QMetaType::Bool;
    case QMetaType::Int:
        if (!number) return false;
        v = QVariant(int(qRound(v.toDouble())));
        return true;
    case QMetaType::Double:
    case QMetaType::Float:
        if (!number) return false;
        return v.convert(type);
    case QMetaType::QString:
        return id == QMetaType::QString;
    case QMetaType::QStringList:
    case QMetaType::QVariantList:
        return (id == QMetaType::QVariantList || id == QMetaType::QStringList) && v.convert(type);
    case QMetaType::QVariantMap:
        return id == QMetaType::QVariantMap;
    default:
        return false;
    }
}
} // namespace

QVariantMap SettingsManager::apply_settings(const QVariantMap &values)
{
    QHash<QString, QString> registrySlots;
    const QJsonObject reg = buildSettingsRegistry();
    for (auto it = reg.constBegin(); it != reg.constEnd(); ++it) {
        const QJsonObject e = it.value().toObject();
        registrySlots.insert(e.value(QStringLiteral("key")).toString(), e.value(QStringLiteral("saveSlot")).toString());
    }

    QStringList applied;
    QVariantMap refused;
    const QMetaObject *mo = metaObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QString &key = it.key();
        if (fileOnlyKeys().contains(key)) {
            refused.insert(key, QStringLiteral("set in settingsConfigure.json only"));
            continue;
        }
        // The row's own slot first (clockFormat24Hour's is save_clock_format),
        // then the registry's, then save_<snake_case of the key>
        const int index = rowIndex(key);
        const QString slotName = index >= 0 && rows().at(index).slot
                                     ? QString::fromLatin1(rows().at(index).slot)
                                     : registrySlots.value(key, QStringLiteral("save_") + snakeCase(key));
        QMetaMethod slot;
        for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
            const QMetaMethod m = mo->method(i);
            if (m.methodType() != QMetaMethod::Signal && m.access() == QMetaMethod::Public && m.parameterCount() == 1
                && m.name() == slotName.toLatin1()) {
                slot = m;
                break;
            }
        }
        if (!slot.isValid() || !slotName.startsWith(QLatin1String("save_"))) {
            refused.insert(key, QStringLiteral("no setter for this key"));
            continue;
        }
        QVariant arg = it.value();
        if (!strictArg(arg, slot.parameterMetaType(0))) {
            refused.insert(key, QStringLiteral("takes a %1").arg(QString::fromLatin1(slot.parameterMetaType(0).name())));
            continue;
        }
        slot.invoke(this, Qt::DirectConnection, QGenericArgument(slot.parameterMetaType(0).name(), arg.constData()));
        applied.append(key);
    }
    if (!refused.isEmpty())
        qCWarning(lcSettings) << "apply_settings refused" << refused;
    return {{QStringLiteral("applied"), applied}, {QStringLiteral("refused"), refused}};
}

void SettingsManager::save_setting(const QString &key, const QVariant &value)
{
    // What turns the service on and what it serves are set in the file,
    // never by a client of the service itself (one could otherwise point
    // the web root at any directory for the next start)
    if (fileOnlyKeys().contains(key)) {
        qCWarning(lcSettings) << "save_setting: refused" << key << "(edit settingsConfigure.json)";
        return;
    }
    // A plain setting: the same path as its save slot (type, limits, the
    // live value, its signal). It used to be written to the file only: the
    // property kept the old value until the next start.
    const int index = rowIndex(key);
    // The Album Art palette is the player's to set (set_album_art_colors,
    // scope full), as apply_settings has it: not a settings-scope client's
    if (index == R_albumArtColors) {
        qCWarning(lcSettings) << "save_setting: refused" << key << "(set by the player from the album art)";
        return;
    }
    if (index >= 0) {
        if (setRow(index, value, true) == SetResult::Changed)
            emit genericSettingChanged(key);
        return;
    }
    // The media folder has rules of its own (an absolute path, not the root)
    if (key == QLatin1String("mediaFolder")) {
        if (value.typeId() == QMetaType::QString && save_media_folder(value.toString()))
            emit genericSettingChanged(key);
        return;
    }
    const StructuredSetting *structured = structuredSetting(key);
    if (structured)
        flushObdParameters();   // toggles waiting for their debounce are in m_obdParameters only
    QJsonObject settings = loadSettings();
    settings[key] = QJsonValue::fromVariant(value);
    saveSettings(settings);
    if (structured) {
        // A list or a map kept in a member too: follow what was just saved
        populateStructured();
        structured->notify(this);
    }
    emit genericSettingChanged(key);
}

// =========================================================================
// Q_INVOKABLE getter methods
// =========================================================================

QVariant SettingsManager::get_setting(const QString &key)
{
    // Not the service keys: the paired devices' hashes are nobody's business
    if (fileOnlyKeys().contains(key))
        return QVariant();
    const int index = rowIndex(key);
    if (index >= 0)
        return row(index);
    QJsonObject settings = loadSettings();
    if (settings.contains(key))
        return settings.value(key).toVariant();
    return QVariant();
}

QVariant SettingsManager::get_setting_with_default(const QString &key, const QVariant &defaultValue)
{
    if (fileOnlyKeys().contains(key))
        return defaultValue;
    const int index = rowIndex(key);
    if (index >= 0)
        return row(index);
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

bool    SettingsManager::get_auto_play_on_startup()    { return autoPlayOnStartup(); }
bool    SettingsManager::get_persist_shuffle_state()   { return persistShuffleState(); }
bool    SettingsManager::get_last_shuffle_state()      { return lastShuffleState(); }
QString SettingsManager::get_last_played_song()        { return lastPlayedSong(); }
int     SettingsManager::get_last_played_position()    { return lastPlayedPosition(); }
QString SettingsManager::get_last_played_playlist()    { return lastPlayedPlaylist(); }
QString SettingsManager::get_last_settings_section()   { return lastSettingsSection(); }
QString SettingsManager::get_window_state()            { return windowState(); }
QString SettingsManager::get_music_button_default_page() { return musicButtonDefaultPage(); }
bool    SettingsManager::get_return_to_library_after_selection() { return returnToLibraryAfterSelection(); }
bool    SettingsManager::get_android_auto_enabled()    { return androidAutoEnabled(); }
bool    SettingsManager::get_phone_mirror_enabled()    { return phoneMirrorEnabled(); }
QString SettingsManager::get_scrcpy_display_size()     { return scrcpyDisplaySize(); }
double  SettingsManager::get_scrcpy_audio_gain()       { return scrcpyAudioGain(); }
bool    SettingsManager::get_scrcpy_audio_duck_enabled() { return scrcpyAudioDuckEnabled(); }
double  SettingsManager::get_scrcpy_audio_duck_level() { return scrcpyAudioDuckLevel(); }
bool    SettingsManager::get_scrcpy_phone_screen_off() { return scrcpyPhoneScreenOff(); }
int     SettingsManager::get_scrcpy_display_dpi()      { return scrcpyDisplayDpi(); }
int     SettingsManager::get_scrcpy_bit_rate_usb()     { return scrcpyBitRateUsb(); }
int     SettingsManager::get_scrcpy_bit_rate_wifi()    { return scrcpyBitRateWifi(); }
bool    SettingsManager::get_scrcpy_auto_wifi()        { return scrcpyAutoWifi(); }
bool    SettingsManager::get_scrcpy_bit_rate_auto()    { return scrcpyBitRateAuto(); }
QString SettingsManager::get_scrcpy_wireless_address() { return scrcpyWirelessAddress(); }
bool    SettingsManager::get_esp32_volume_enabled()    { return esp32VolumeEnabled(); }
QString SettingsManager::get_esp32_volume_port()       { return esp32VolumePort(); }
double  SettingsManager::get_esp32_volume_step_size()  { return esp32VolumeStepSize(); }
bool    SettingsManager::get_esp32_auto_reconnect()    { return esp32AutoReconnect(); }
bool    SettingsManager::get_esp32_led_sleep_enabled() { return esp32LedSleepEnabled(); }
QString SettingsManager::get_esp32_led_color_mode()    { return esp32LedColorMode(); }
QString SettingsManager::get_esp32_led_static_color()  { return esp32LedStaticColor(); }
bool    SettingsManager::get_show_waveform_visualizer() { return showWaveformVisualizer(); }
bool    SettingsManager::get_imu_enabled()             { return imuEnabled(); }
bool    SettingsManager::get_gesture_sensor_enabled()  { return gestureSensorEnabled(); }

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
        const int index = rowIndex(key);
        const QVariant val = index >= 0 ? row(index) : m_settings.value(key).toVariant();
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
// reset_to_defaults
// =========================================================================
void SettingsManager::reset_to_defaults()
{
    // Persist the defaults, reload everything exactly as the constructor
    // does, then tell QML about what changed and nothing else. The
    // file-only keys keep what the file says: a reset from a client must
    // not change what the service does at the next start.
    flushObdParameters();
    const QVariantList valuesBefore = m_values;
    const QJsonObject before = m_settings;
    const QString folderBefore = m_mediaFolder;
    const int volumeBefore = m_currentVolume;

    QJsonObject defaults = m_defaultSettings;
    for (const QString &key : fileOnlyKeys())
        if (before.contains(key))
            defaults.insert(key, before.value(key));
    saveSettings(defaults);
    populateMembers();

    // The plain settings
    bool cards = false;
    for (int i = 0; i < rows().size(); ++i) {
        if (m_values.at(i) == valuesBefore.at(i))
            continue;
        if (rows().at(i).shape == Part)
            cards = true;   // six rows, one signal
        else
            rows().at(i).notify(this, m_values.at(i));
    }
    if (cards)
        emit sensorCardsChanged();

    // The lists and maps, then the keys known by name only (save_setting's)
    QStringList keys = before.keys();
    for (const QString &key : m_settings.keys())
        if (!before.contains(key))
            keys.append(key);
    for (const QString &key : std::as_const(keys)) {
        if (before.value(key) == m_settings.value(key) || rowIndex(key) >= 0 || fileOnlyKeys().contains(key)
            || key == QLatin1String("mediaFolder"))
            continue;
        if (const StructuredSetting *structured = structuredSetting(key))
            structured->notify(this);
        else
            emit genericSettingChanged(key);
    }
    if (m_mediaFolder != folderBefore)
        emit mediaFolderChanged(m_mediaFolder);
    if (m_currentVolume != volumeBefore)
        emit currentVolumeChanged(m_currentVolume);
}

// =========================================================================
// Git info methods (not settings: they belong with the About page's
// DiagnosticsManager one day, as custom-theme art and the pinned-settings
// registry belong in classes of their own)
// =========================================================================
QString SettingsManager::getGitRepoDir() const
{
    // Go up one level from backend dir to get repo root
    return QFileInfo(m_backendDir).absolutePath();
}

// One git question about OCTAVE's own checkout, asked once a run. The About
// page binds to three of these: each used to start git on the GUI thread
// with a 5 s wait every time the binding was evaluated, and with no .git
// of its own, git answered for whatever repository the binary sat inside.
namespace {
QString gitAnswer(const QString &repoDir, const QStringList &args)
{
#if QT_CONFIG(process)
    static QHash<QString, QString> answers;
    const QString key = args.join(QLatin1Char(' '));
    const auto known = answers.constFind(key);
    if (known != answers.constEnd())
        return *known;
    QString out;
    if (QFileInfo::exists(repoDir + QStringLiteral("/.git"))) {
        QProcess proc;
        proc.setWorkingDirectory(repoDir);
        proc.start(QStringLiteral("git"), args);
        if (proc.waitForFinished(2000) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0)
            out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        else
            proc.kill();
    }
    answers.insert(key, out);
    return out;
#else
    Q_UNUSED(repoDir)
    Q_UNUSED(args)
    return QString();
#endif
}
} // namespace

QString SettingsManager::get_git_commit_hash()
{
    const QString hash = gitAnswer(getGitRepoDir(), {QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")});
    if (!hash.isEmpty())
        return hash;
    // Packaged builds (APK, installers) have neither .git nor git: use the
    // value CMake baked in at configure time.
#ifdef OCTAVE_GIT_COMMIT
    return QStringLiteral(OCTAVE_GIT_COMMIT);
#else
    return QStringLiteral("unknown");
#endif
}

QString SettingsManager::get_git_commit_date()
{
    QString dateStr = gitAnswer(getGitRepoDir(), {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%ci")});
#ifdef OCTAVE_GIT_DATE
    if (dateStr.isEmpty())
        dateStr = QStringLiteral(OCTAVE_GIT_DATE);   // baked in by CMake for packaged builds
#endif
    if (dateStr.isEmpty() || dateStr == QStringLiteral("unknown"))
        return QStringLiteral("unknown");
    const QStringList parts = dateStr.split(QLatin1Char(' '));
    if (parts.size() >= 2)
        return parts[0] + QStringLiteral(" at ") + parts[1].left(5);
    return dateStr;
}

QString SettingsManager::get_git_commit_message()
{
    return gitAnswer(getGitRepoDir(), {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%s")});
}
