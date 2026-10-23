#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>

class SettingsManager : public QObject
{
    Q_OBJECT
    friend class TstSettingsManager;

    // ======================================================================
    // Q_PROPERTY declarations  (type, QML name, getter, notify)
    // Properties that use simple set-and-save use OCTAVE_PROPERTY macro.
    // Properties with custom logic are declared manually.
    // ======================================================================

    // --- Device ---
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString fontSetting READ fontSetting NOTIFY fontSettingChanged)

    // --- Theme / Display ---
    Q_PROPERTY(QString themeSetting READ themeSetting NOTIFY themeSettingChanged)
    Q_PROPERTY(float uiScale READ uiScale NOTIFY uiScaleChanged)
    Q_PROPERTY(int colorTransitionMs READ colorTransitionMs NOTIFY colorTransitionMsChanged)
    Q_PROPERTY(bool songLengthTransition READ songLengthTransition NOTIFY songLengthTransitionChanged)
    Q_PROPERTY(bool showClock READ showClock NOTIFY showClockChanged)
    Q_PROPERTY(bool clockFormat24Hour READ clockFormat24Hour NOTIFY clockFormatChanged)
    Q_PROPERTY(bool clockShowSeconds READ clockShowSeconds NOTIFY clockShowSecondsChanged)
    Q_PROPERTY(int clockSize READ clockSize NOTIFY clockSizeChanged)
    Q_PROPERTY(bool clockSizeAuto READ clockSizeAuto NOTIFY clockSizeAutoChanged)
    Q_PROPERTY(QString backgroundGrid READ backgroundGrid NOTIFY backgroundGridChanged)
    Q_PROPERTY(int screenWidth READ screenWidth NOTIFY screenWidthChanged)
    Q_PROPERTY(int screenHeight READ screenHeight NOTIFY screenHeightChanged)
    Q_PROPERTY(int backgroundBlurRadius READ backgroundBlurRadius NOTIFY backgroundBlurRadiusChanged)
    Q_PROPERTY(QString bottomBarOrientation READ bottomBarOrientation NOTIFY bottomBarOrientationChanged)
    Q_PROPERTY(bool showBottomBarMediaControls READ showBottomBarMediaControls NOTIFY showBottomBarMediaControlsChanged)
    Q_PROPERTY(QString settingsLayoutStyle READ settingsLayoutStyle NOTIFY settingsLayoutStyleChanged)
    // The Album Art Capture palette extracted from the playing art, as a theme
    // object ({base, baseAlt, accent, text, ...}; empty before the first
    // extraction). Stored as a JSON string under the same key.
    Q_PROPERTY(QVariantMap albumArtColors READ albumArtColors NOTIFY albumArtColorsChanged)
    Q_PROPERTY(QString settingsMenuVisibility READ settingsMenuVisibility NOTIFY settingsMenuVisibilityChanged)

    // --- Media ---
    Q_PROPERTY(float startUpVolume READ startUpVolume NOTIFY startUpVolumeChanged)
    Q_PROPERTY(int currentVolume READ currentVolume NOTIFY currentVolumeChanged)
    Q_PROPERTY(QString mediaFolder READ mediaFolder NOTIFY mediaFolderChanged)
    Q_PROPERTY(bool autoPlayOnStartup READ autoPlayOnStartup NOTIFY autoPlayOnStartupChanged)
    Q_PROPERTY(bool persistShuffleState READ persistShuffleState NOTIFY persistShuffleStateChanged)
    Q_PROPERTY(bool lastShuffleState READ lastShuffleState NOTIFY lastShuffleStateChanged)
    Q_PROPERTY(QString lastPlayedSong READ lastPlayedSong NOTIFY lastPlayedSongChanged)
    Q_PROPERTY(int lastPlayedPosition READ lastPlayedPosition NOTIFY lastPlayedPositionChanged)
    Q_PROPERTY(QString lastPlayedPlaylist READ lastPlayedPlaylist NOTIFY lastPlayedPlaylistChanged)
    Q_PROPERTY(QString musicButtonDefaultPage READ musicButtonDefaultPage NOTIFY musicButtonDefaultPageChanged)
    Q_PROPERTY(bool returnToLibraryAfterSelection READ returnToLibraryAfterSelection NOTIFY returnToLibraryAfterSelectionChanged)
    Q_PROPERTY(bool showWaveformVisualizer READ showWaveformVisualizer NOTIFY showWaveformVisualizerChanged)

    // --- 3D visual effects ---
    Q_PROPERTY(bool show3DButtonTilt READ show3DButtonTilt NOTIFY show3DButtonTiltChanged)
    Q_PROPERTY(bool show3DAlbumPreview READ show3DAlbumPreview NOTIFY show3DAlbumPreviewChanged)
    Q_PROPERTY(bool roundedAlbumArt READ roundedAlbumArt NOTIFY roundedAlbumArtChanged)
    Q_PROPERTY(int albumArtCornerRadius READ albumArtCornerRadius NOTIFY albumArtCornerRadiusChanged)
    Q_PROPERTY(bool showAlbumArtShadow READ showAlbumArtShadow NOTIFY showAlbumArtShadowChanged)
    Q_PROPERTY(bool vinylRecordMode READ vinylRecordMode NOTIFY vinylRecordModeChanged)
    Q_PROPERTY(QString albumArtTransition READ albumArtTransition NOTIFY albumArtTransitionChanged)
    Q_PROPERTY(int backgroundOverlayOpacity READ backgroundOverlayOpacity NOTIFY backgroundOverlayOpacityChanged)
    Q_PROPERTY(double sideCardOpacity READ sideCardOpacity NOTIFY sideCardOpacityChanged)
    Q_PROPERTY(int sideCardAngle READ sideCardAngle NOTIFY sideCardAngleChanged)
    Q_PROPERTY(int buttonTiltDuration READ buttonTiltDuration NOTIFY buttonTiltDurationChanged)
    Q_PROPERTY(int textScrollSpeed READ textScrollSpeed NOTIFY textScrollSpeedChanged)

    // --- OBD ---
    Q_PROPERTY(QString obdBluetoothPort READ obdBluetoothPort NOTIFY obdBluetoothPortChanged)
    // Recently used OBD adapters, newest first, at most 8: {name, mac} maps
    // (stored as a JSON string under the same key)
    Q_PROPERTY(QVariantList obdSavedAdapters READ obdSavedAdapters NOTIFY obdSavedAdaptersChanged)
    Q_PROPERTY(bool obdFastMode READ obdFastMode NOTIFY obdFastModeChanged)
    Q_PROPERTY(bool obdCardStyleCircular READ obdCardStyleCircular NOTIFY obdCardStyleCircularChanged)
    // Units the OBD gauges and dashboards show (ParameterCatalog converts):
    // temperatures "C" | "F", speed "mph" | "km/h". Altitude follows
    // sensorAltitudeUnit. The Sensors page keeps sensorTempUnit.
    Q_PROPERTY(QString gaugeTempUnit READ gaugeTempUnit NOTIFY gaugeTempUnitChanged)
    Q_PROPERTY(QString gaugeSpeedUnit READ gaugeSpeedUnit NOTIFY gaugeSpeedUnitChanged)
    Q_PROPERTY(int obdAutoReconnectAttempts READ obdAutoReconnectAttempts NOTIFY obdAutoReconnectAttemptsChanged)
    Q_PROPERTY(float fuelTankCapacity READ fuelTankCapacity NOTIFY fuelTankCapacityChanged)
    // Black box (TripRecorder): record every drive, also sweep PIDs that are
    // not on screen, and the on-disk cap for the history (database + files)
    Q_PROPERTY(bool blackBoxEnabled READ blackBoxEnabled NOTIFY blackBoxEnabledChanged)
    Q_PROPERTY(bool blackBoxSweepAll READ blackBoxSweepAll NOTIFY blackBoxSweepAllChanged)
    Q_PROPERTY(int blackBoxMaxMB READ blackBoxMaxMB NOTIFY blackBoxMaxMBChanged)
    Q_PROPERTY(QVariantList homeOBDParameters READ homeOBDParameters NOTIFY homeOBDParametersChanged)
    // The home screen holds at most this many OBD cards
    Q_PROPERTY(int maxHomeObdParameters READ maxHomeObdParameters CONSTANT)
    Q_PROPERTY(QVariantList customThemes READ customThemes NOTIFY customThemesChanged)   // names
    // Every saved custom theme, name -> theme object (as on disk)
    Q_PROPERTY(QVariantMap customThemeData READ customThemeData NOTIFY customThemesChanged)
    // The Now Playing look presets, [{label, settings: {key: value}}]
    Q_PROPERTY(QVariantList nowPlayingPresets READ nowPlayingPresets CONSTANT)
    Q_PROPERTY(QVariantList directoryHistory READ directoryHistory NOTIFY directoryHistoryChanged)

    // --- Window ---
    Q_PROPERTY(QString windowState READ windowState NOTIFY windowStateChanged)
    Q_PROPERTY(QString lastSettingsSection READ lastSettingsSection NOTIFY lastSettingsSectionChanged)

    // --- Android Auto / Phone Mirror ---
    Q_PROPERTY(bool androidAutoEnabled READ androidAutoEnabled NOTIFY androidAutoEnabledChanged)
    Q_PROPERTY(bool phoneMirrorEnabled READ phoneMirrorEnabled NOTIFY phoneMirrorEnabledChanged)
    Q_PROPERTY(QString scrcpyDisplaySize READ scrcpyDisplaySize NOTIFY scrcpyDisplaySizeChanged)
    Q_PROPERTY(double scrcpyAudioGain READ scrcpyAudioGain NOTIFY scrcpyAudioGainChanged)
    // The same two levels in dB, as the Phone Audio sliders show them (the
    // file and the audio path keep the linear factors above)
    Q_PROPERTY(double scrcpyAudioGainDb READ scrcpyAudioGainDb NOTIFY scrcpyAudioGainChanged)
    Q_PROPERTY(double scrcpyAudioDuckLevelDb READ scrcpyAudioDuckLevelDb NOTIFY scrcpyAudioDuckLevelChanged)
    Q_PROPERTY(bool scrcpyAudioDuckEnabled READ scrcpyAudioDuckEnabled NOTIFY scrcpyAudioDuckEnabledChanged)
    Q_PROPERTY(double scrcpyAudioDuckLevel READ scrcpyAudioDuckLevel NOTIFY scrcpyAudioDuckLevelChanged)
    Q_PROPERTY(bool scrcpyPhoneScreenOff READ scrcpyPhoneScreenOff NOTIFY scrcpyPhoneScreenOffChanged)
    // Virtual display density override, 0 = scale the phone's own density
    Q_PROPERTY(int scrcpyDisplayDpi READ scrcpyDisplayDpi NOTIFY scrcpyDisplayDpiChanged)
    // Video bit rate in Mbps, per link
    Q_PROPERTY(int scrcpyBitRateUsb READ scrcpyBitRateUsb NOTIFY scrcpyBitRateUsbChanged)
    Q_PROPERTY(int scrcpyBitRateWifi READ scrcpyBitRateWifi NOTIFY scrcpyBitRateWifiChanged)
    // Switch a USB phone to adb over TCP (port 5555) so it can be reached over Wi-Fi later
    Q_PROPERTY(bool scrcpyAutoWifi READ scrcpyAutoWifi NOTIFY scrcpyAutoWifiChanged)
    // Adapt the mirror bit rate to the link; the per-link rates become the ceiling
    Q_PROPERTY(bool scrcpyBitRateAuto READ scrcpyBitRateAuto NOTIFY scrcpyBitRateAutoChanged)
    // Last phone connected over Wi-Fi ("ip:port"); reconnected automatically, "" = none
    Q_PROPERTY(QString scrcpyWirelessAddress READ scrcpyWirelessAddress NOTIFY scrcpyWirelessAddressChanged)

    // --- ESP32 Volume Knob ---
    Q_PROPERTY(bool esp32VolumeEnabled READ esp32VolumeEnabled NOTIFY esp32VolumeEnabledChanged)
    Q_PROPERTY(QString esp32VolumePort READ esp32VolumePort NOTIFY esp32VolumePortChanged)
    Q_PROPERTY(double esp32VolumeStepSize READ esp32VolumeStepSize NOTIFY esp32VolumeStepSizeChanged)
    Q_PROPERTY(bool esp32AutoReconnect READ esp32AutoReconnect NOTIFY esp32AutoReconnectChanged)
    Q_PROPERTY(bool esp32LedSleepEnabled READ esp32LedSleepEnabled NOTIFY esp32LedSleepEnabledChanged)
    Q_PROPERTY(QString esp32LedColorMode READ esp32LedColorMode NOTIFY esp32LedColorModeChanged)
    Q_PROPERTY(QString esp32LedStaticColor READ esp32LedStaticColor NOTIFY esp32LedStaticColorChanged)

    // --- IMU ---
    Q_PROPERTY(bool imuEnabled READ imuEnabled NOTIFY imuEnabledChanged)
    Q_PROPERTY(int sensorEmitRate READ sensorEmitRate NOTIFY sensorEmitRateChanged)   // Hz, 10-120
    // Sensors page display (SensorMenu binds to these)
    Q_PROPERTY(int sensorDecimalPlaces READ sensorDecimalPlaces NOTIFY sensorDecimalPlacesChanged)     // 0-4
    Q_PROPERTY(QString sensorTempUnit READ sensorTempUnit NOTIFY sensorTempUnitChanged)               // "F" | "C"
    Q_PROPERTY(QString sensorAltitudeUnit READ sensorAltitudeUnit NOTIFY sensorAltitudeUnitChanged)   // "m" | "ft"
    // Which readouts show: {Pitch, Roll, Heading, GForce, Altitude, Temperature} -> bool
    Q_PROPERTY(QVariantMap sensorCards READ sensorCards NOTIFY sensorCardsChanged)

    // --- Gesture Sensor ---
    Q_PROPERTY(bool gestureSensorEnabled READ gestureSensorEnabled NOTIFY gestureSensorEnabledChanged)
    Q_PROPERTY(int gestureVolumeStep READ gestureVolumeStep NOTIFY gestureVolumeStepChanged)
    Q_PROPERTY(int gestureCooldown READ gestureCooldown NOTIFY gestureCooldownChanged)

    // --- Pinned Settings ---
    Q_PROPERTY(QVariantList pinnedSettings READ pinnedSettings NOTIFY pinnedSettingsChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr);

    // Returns the OS-specific app data directory path
    static QString getAppDataDir();

    // ======================================================================
    // Property getters
    // ======================================================================

    // Device
    QString deviceName() const;
    QString fontSetting() const;

    // Theme / Display
    QString themeSetting() const;
    float uiScale() const;
    int colorTransitionMs() const;
    bool songLengthTransition() const;
    bool showClock() const;
    bool clockFormat24Hour() const;
    bool clockShowSeconds() const;
    int clockSize() const;
    bool clockSizeAuto() const;
    QString backgroundGrid() const;
    int screenWidth() const;
    int screenHeight() const;
    int backgroundBlurRadius() const;
    QString bottomBarOrientation() const;
    bool showBottomBarMediaControls() const;
    QString settingsLayoutStyle() const;
    QVariantMap albumArtColors() const;
    QString settingsMenuVisibility() const;

    // Media
    float startUpVolume() const;
    int currentVolume() const;
    QString mediaFolder() const;
    bool autoPlayOnStartup() const;
    bool persistShuffleState() const;
    bool lastShuffleState() const;
    QString lastPlayedSong() const;
    int lastPlayedPosition() const;
    QString lastPlayedPlaylist() const;
    QString musicButtonDefaultPage() const;
    bool returnToLibraryAfterSelection() const;
    bool showWaveformVisualizer() const;

    // 3D visual effects
    bool show3DButtonTilt() const;
    bool show3DAlbumPreview() const;
    bool roundedAlbumArt() const;
    int albumArtCornerRadius() const;
    bool showAlbumArtShadow() const;
    bool vinylRecordMode() const;
    QString albumArtTransition() const;
    int backgroundOverlayOpacity() const;
    double sideCardOpacity() const;
    int sideCardAngle() const;
    int buttonTiltDuration() const;
    int textScrollSpeed() const;

    // OBD
    QString obdBluetoothPort() const;
    QVariantList obdSavedAdapters() const;
    bool obdFastMode() const;
    bool obdCardStyleCircular() const;
    QString gaugeTempUnit() const { return m_gaugeTempUnit; }
    QString gaugeSpeedUnit() const { return m_gaugeSpeedUnit; }
    int obdAutoReconnectAttempts() const;
    float fuelTankCapacity() const;
    bool blackBoxEnabled() const;
    bool blackBoxSweepAll() const;
    int blackBoxMaxMB() const;
    QVariantList homeOBDParameters() const;
    static constexpr int kMaxHomeObdParameters = 8;
    // Saved window size range: the window's own minimum up to 8K (a bigger
    // value would be clamped and then pushed back onto a real window)
    static constexpr int kMinScreenWidth = 400;
    static constexpr int kMaxScreenWidth = 7680;
    static constexpr int kMinScreenHeight = 300;
    static constexpr int kMaxScreenHeight = 4320;
    int maxHomeObdParameters() const { return kMaxHomeObdParameters; }
    QVariantList customThemes() const;
    QVariantMap customThemeData() const;
    static QVariantList nowPlayingPresets();
    // A file-only key written by its owner in C++ (ServicePairing: the
    // service switch and the paired devices); never reachable from QML or
    // the service. Other keys are ignored.
    void saveFileOnlySetting(const QString &key, const QVariant &value);
    // Keys only an edit of the file may set: save_setting and
    // apply_settings refuse them (what turns the service on, what it serves)
    static const QStringList &fileOnlyKeys();
    QVariantList directoryHistory() const;

    // Window
    QString windowState() const;
    QString lastSettingsSection() const;

    // Android Auto / Phone Mirror
    bool androidAutoEnabled() const;
    bool phoneMirrorEnabled() const;
    QString scrcpyDisplaySize() const;
    double scrcpyAudioGain() const;
    double scrcpyAudioGainDb() const;
    double scrcpyAudioDuckLevelDb() const;
    // Slider ranges in dB (the savers clamp to them)
    static constexpr double kAudioGainMinDb = -12.0;
    static constexpr double kAudioGainMaxDb = 18.0;
    static constexpr double kAudioDuckMinDb = -40.0;
    static constexpr double kAudioDuckMaxDb = -6.0;
    bool scrcpyAudioDuckEnabled() const;
    double scrcpyAudioDuckLevel() const;
    bool scrcpyPhoneScreenOff() const;
    int scrcpyDisplayDpi() const;
    int scrcpyBitRateUsb() const;
    int scrcpyBitRateWifi() const;
    bool scrcpyAutoWifi() const;
    bool scrcpyBitRateAuto() const;
    QString scrcpyWirelessAddress() const;

    // ESP32
    bool esp32VolumeEnabled() const;
    QString esp32VolumePort() const;
    double esp32VolumeStepSize() const;
    bool esp32AutoReconnect() const;
    bool esp32LedSleepEnabled() const;
    QString esp32LedColorMode() const;
    QString esp32LedStaticColor() const;

    // IMU
    bool imuEnabled() const;
    int sensorEmitRate() const;
    int sensorDecimalPlaces() const;
    QString sensorTempUnit() const;
    QString sensorAltitudeUnit() const;
    QVariantMap sensorCards() const;

    // Gesture
    bool gestureSensorEnabled() const;
    int gestureVolumeStep() const;
    int gestureCooldown() const;

    // Pinned
    QVariantList pinnedSettings() const;

signals:
    // ======================================================================
    // Signals — the names are the QML binding API; don't rename them
    // ======================================================================

    // Device
    void deviceNameChanged(const QString &value);
    void fontSettingChanged(const QString &value);

    // Theme / Display
    void themeSettingChanged(const QString &value);
    void uiScaleChanged(float value);
    void colorTransitionMsChanged(int value);
    void songLengthTransitionChanged(bool value);
    void showClockChanged(bool value);
    void clockFormatChanged(bool value);
    void clockShowSecondsChanged(bool value);
    void clockSizeChanged(int value);
    void clockSizeAutoChanged(bool value);
    void backgroundGridChanged(const QString &value);
    void screenWidthChanged(int value);
    void screenHeightChanged(int value);
    void backgroundBlurRadiusChanged(int value);
    void bottomBarOrientationChanged(const QString &value);
    void showBottomBarMediaControlsChanged(bool value);
    void settingsLayoutStyleChanged(const QString &value);
    void albumArtColorsChanged();
    void settingsMenuVisibilityChanged();

    // Media
    void startUpVolumeChanged(const QString &value);   // emitted as a string
    void currentVolumeChanged(int value);
    void mediaFolderChanged(const QString &value);
    void autoPlayOnStartupChanged(bool value);
    void persistShuffleStateChanged(bool value);
    void lastShuffleStateChanged(bool value);
    void lastPlayedSongChanged(const QString &value);
    void lastPlayedPositionChanged(int value);
    void lastPlayedPlaylistChanged(const QString &value);
    void musicButtonDefaultPageChanged(const QString &value);
    void returnToLibraryAfterSelectionChanged(bool value);
    void showWaveformVisualizerChanged(bool value);

    // 3D visual effects
    void show3DButtonTiltChanged(bool value);
    void show3DAlbumPreviewChanged(bool value);
    void roundedAlbumArtChanged(bool value);
    void albumArtCornerRadiusChanged(int value);
    void showAlbumArtShadowChanged(bool value);
    void vinylRecordModeChanged(bool value);
    void albumArtTransitionChanged(const QString &value);
    void backgroundOverlayOpacityChanged(int value);
    void sideCardOpacityChanged(double value);
    void sideCardAngleChanged(int value);
    void buttonTiltDurationChanged(int value);
    void textScrollSpeedChanged(int value);

    // OBD
    void obdBluetoothPortChanged(const QString &value);
    void obdSavedAdaptersChanged();
    void obdFastModeChanged(bool value);
    void obdCardStyleCircularChanged(bool value);
    void gaugeTempUnitChanged();
    void gaugeSpeedUnitChanged();
    void obdAutoReconnectAttemptsChanged(int value);
    void fuelTankCapacityChanged(float value);
    void blackBoxEnabledChanged(bool value);
    void blackBoxSweepAllChanged(bool value);
    void blackBoxMaxMBChanged(int value);
    void obdParametersChanged();
    void homeOBDParametersChanged();
    void supportedOBDParametersChanged();

    // Directory history
    void directoryHistoryChanged();

    // Custom themes
    void customThemesChanged();

    // Window / Settings navigation
    void windowStateChanged(const QString &value);
    void lastSettingsSectionChanged(const QString &value);

    // Android Auto / Phone Mirror
    void androidAutoEnabledChanged(bool value);
    void phoneMirrorEnabledChanged(bool value);
    void scrcpyDisplaySizeChanged(const QString &value);
    void scrcpyAudioGainChanged(double value);
    void scrcpyAudioDuckEnabledChanged(bool value);
    void scrcpyAudioDuckLevelChanged(double value);
    void scrcpyPhoneScreenOffChanged(bool value);
    void scrcpyDisplayDpiChanged(int value);
    void scrcpyBitRateUsbChanged(int value);
    void scrcpyBitRateWifiChanged(int value);
    void scrcpyAutoWifiChanged(bool value);
    void scrcpyBitRateAutoChanged(bool value);
    void scrcpyWirelessAddressChanged(const QString &value);

    // ESP32
    void esp32VolumeEnabledChanged(bool value);
    void esp32VolumePortChanged(const QString &value);
    void esp32VolumeStepSizeChanged(double value);
    void esp32AutoReconnectChanged(bool value);
    void esp32LedSleepEnabledChanged(bool value);
    void esp32LedColorModeChanged(const QString &value);
    void esp32LedStaticColorChanged(const QString &value);

    // IMU
    void imuEnabledChanged(bool value);
    void sensorEmitRateChanged(int value);
    void sensorDecimalPlacesChanged(int value);
    void sensorTempUnitChanged(const QString &value);
    void sensorAltitudeUnitChanged(const QString &value);
    void sensorCardsChanged();

    // Gesture
    void gestureSensorEnabledChanged(bool value);
    void gestureMappingChanged();
    void gestureVolumeStepChanged(int value);
    void gestureCooldownChanged(int value);

    // Pinned
    void pinnedSettingsChanged();

    // Generic
    void genericSettingChanged(const QString &key);

public slots:
    // ======================================================================
    // Save / setter slots — names are called from QML; don't rename them
    // ======================================================================

    // Device
    void save_device_name(const QString &name);
    void save_font_setting(const QString &font);

    // Theme / Display
    void save_theme_setting(const QString &theme);
    void save_ui_scale(float scale);
    void save_color_transition_ms(int ms);
    void save_song_length_transition(bool enabled);
    void save_show_clock(bool show);
    void save_clock_format(bool is24hour);
    void save_clock_show_seconds(bool show);
    void save_clock_size(int size);
    void save_clock_size_auto(bool autoSize);
    void save_background_grid(const QString &grid);
    void save_screen_width(int width);
    void save_screen_height(int height);
    void save_background_blur_radius(int radius);
    void save_bottom_bar_orientation(const QString &orientation);
    void save_show_bottom_bar_media_controls(bool show);
    void save_settings_layout_style(const QString &style);
    void set_album_art_colors(const QString &colorsJson);

    // Media
    void save_start_volume(float volume);
    void setCurrentVolume(int volume);
    void save_media_folder(const QString &folderPath);
    // A folder from a picker (FolderDialog.selectedFolder): a file:// URL, or
    // an Android external-storage content:// tree mapped to its /storage
    // path (src/util/pathurl.h). False, and nothing saved, when it can't be
    // a path the library can scan (another provider, a web URL).
    Q_INVOKABLE bool save_media_folder_url(const QUrl &url);
    void save_auto_play_on_startup(bool enabled);
    void save_persist_shuffle_state(bool enabled);
    void save_last_shuffle_state(bool enabled);
    void save_last_played_song(const QString &filename);
    void save_last_played_position(int positionMs);
    void save_last_played_playlist(const QString &playlistName);
    void save_playback_state(const QString &song, int positionMs, const QString &playlist);
    void save_music_button_default_page(const QString &page);
    void save_return_to_library_after_selection(bool returnToLibrary);
    void save_show_waveform_visualizer(bool enabled);

    // 3D visual effects
    void save_show_3d_button_tilt(bool enabled);
    void save_show_3d_album_preview(bool enabled);
    void save_rounded_album_art(bool enabled);
    void save_album_art_corner_radius(int radius);
    void save_show_album_art_shadow(bool enabled);
    void save_vinyl_record_mode(bool enabled);
    void save_album_art_transition(const QString &transition);
    void save_background_overlay_opacity(int opacity);
    void save_side_card_opacity(double opacity);
    void save_side_card_angle(int angle);
    void save_button_tilt_duration(int duration);
    void save_text_scroll_speed(int speed);

    // OBD
    void save_obd_bluetooth_port(const QString &port);
    void add_obd_saved_adapter(const QString &name, const QString &mac);
    void remove_obd_saved_adapter(const QString &mac);
    void save_obd_fast_mode(bool enabled);
    void save_obd_card_style_circular(bool circular);
    void save_gauge_temp_unit(const QString &unit);    // ignored unless "C" or "F"
    void save_gauge_speed_unit(const QString &unit);   // ignored unless "mph" or "km/h"
    void save_obd_auto_reconnect_attempts(int attempts);
    void save_fuel_tank_capacity(float capacity);
    void save_black_box_enabled(bool enabled);
    void save_black_box_sweep_all(bool enabled);
    void save_black_box_max_mb(int megabytes);
    void save_obd_parameter_enabled(const QString &parameter, bool enabled);
    void save_supported_obd_parameters(const QVariantList &parameters);
    // The whole list (deduplicated, empty ids dropped, capped at
    // kMaxHomeObdParameters); the editor uses the three below
    void save_home_obd_parameters(const QVariantList &parameters);
    // Add at `index` (-1 = the end); false if already there or full
    bool add_home_obd_parameter(const QString &id, int index = -1);
    bool remove_home_obd_parameter(const QString &id);
    // Move the card at `from` to `to` (past the end = last); the cards in
    // between shift by one
    bool move_home_obd_parameter(int from, int to);

    // Directory history
    void save_to_directory_history(const QString &folderPath);
    void remove_from_directory_history(const QString &folderPath);

    // Custom themes
    void save_custom_theme(const QString &name, const QVariantMap &theme, const QString &albumArtSource = QString());
    void delete_custom_theme(const QString &name);

    // Window / Settings navigation
    void save_window_state(const QString &state);
    void set_last_settings_section(const QString &section);

    // Settings section visibility
    void save_settings_section_visibility(const QString &section, bool visible);

    // Android Auto / Phone Mirror
    void save_android_auto_enabled(bool enabled);
    void save_phone_mirror_enabled(bool enabled);
    void save_scrcpy_display_size(const QString &size);
    void save_scrcpy_audio_gain(double gain);
    void save_scrcpy_audio_duck_enabled(bool enabled);
    void save_scrcpy_audio_duck_level(double level);
    // dB in, the linear factor saved (clamped to the k*Db ranges)
    void save_scrcpy_audio_gain_db(double db);
    void save_scrcpy_audio_duck_level_db(double db);
    void save_scrcpy_phone_screen_off(bool off);
    void save_scrcpy_display_dpi(int dpi);
    void save_scrcpy_bit_rate_usb(int mbps);
    void save_scrcpy_bit_rate_wifi(int mbps);
    void save_scrcpy_auto_wifi(bool enabled);
    void save_scrcpy_bit_rate_auto(bool enabled);
    void save_scrcpy_wireless_address(const QString &address);

    // ESP32
    void save_esp32_volume_enabled(bool enabled);
    void save_esp32_volume_port(const QString &port);
    void save_esp32_volume_step_size(double stepSize);
    void save_esp32_auto_reconnect(bool enabled);
    void save_esp32_led_sleep_enabled(bool enabled);
    void save_esp32_led_color_mode(const QString &mode);
    void save_esp32_led_static_color(const QString &color);

    // IMU
    void save_imu_enabled(bool enabled);
    void save_sensor_emit_rate(int hz);
    void save_sensor_decimal_places(int places);
    void save_sensor_temp_unit(const QString &unit);       // ignored unless "F" or "C"
    void save_sensor_altitude_unit(const QString &unit);   // ignored unless "m" or "ft"
    // card: Pitch, Roll, Heading, GForce, Altitude or Temperature (saved as sensorShow<card>)
    void save_sensor_card_visible(const QString &card, bool visible);

    // Gesture
    void save_gesture_sensor_enabled(bool enabled);
    void save_gesture_action(const QString &gesture, const QString &action);
    void save_gesture_mapping(const QVariantMap &mapping);
    void reset_gesture_mapping();
    void save_gesture_volume_step(int step);
    void save_gesture_cooldown(int ms);

    // Pinned settings
    void toggle_pin_setting(const QString &settingId);

    // Reset to defaults
    void reset_to_defaults();

    // Generic setting access
    void save_setting(const QString &key, const QVariant &value);

    // Many settings at once, each through its own save_* slot (the
    // settings registry's saveSlot, else save_<snake_case of the key>), so
    // every rule and notify signal of a single save applies. A value must
    // already be the slot's type (a bool for a bool, a number for a
    // number, a string for a string; an int slot rounds). Keys with no
    // save slot and the file-only keys are refused, never written.
    // Returns {applied: [keys], refused: {key: reason}}.
    QVariantMap apply_settings(const QVariantMap &values);
    // The Now Playing look presets (Minimal, Vinyl, Showcase 3D, Cinema),
    // each {label, settings}; applyNowPlayingPreset(label) applies one
    // through apply_settings. False for an unknown label.
    bool applyNowPlayingPreset(const QString &label);

    // ======================================================================
    // Q_INVOKABLE getter methods
    // ======================================================================

    Q_INVOKABLE QVariant get_setting(const QString &key);
    Q_INVOKABLE QVariant get_setting_with_default(const QString &key, const QVariant &defaultValue);
    Q_INVOKABLE bool get_obd_parameter_enabled(const QString &parameter, bool defaultVal = true);
    Q_INVOKABLE QVariantList get_supported_obd_parameters();
    Q_INVOKABLE QVariantList get_directory_history();
    Q_INVOKABLE QVariantList get_home_obd_parameters();
    Q_INVOKABLE bool get_auto_play_on_startup();
    Q_INVOKABLE bool get_persist_shuffle_state();
    Q_INVOKABLE bool get_last_shuffle_state();
    Q_INVOKABLE QString get_last_played_song();
    Q_INVOKABLE int get_last_played_position();
    Q_INVOKABLE QString get_last_played_playlist();
    Q_INVOKABLE QString get_last_settings_section();
    Q_INVOKABLE QString get_window_state();
    Q_INVOKABLE QString get_music_button_default_page();
    Q_INVOKABLE bool get_return_to_library_after_selection();
    // Write any coalesced settings change to disk now (called at shutdown)
    Q_INVOKABLE void flushPendingSave();
    Q_INVOKABLE bool get_android_auto_enabled();
    Q_INVOKABLE bool get_phone_mirror_enabled();
    Q_INVOKABLE QString get_scrcpy_display_size();
    Q_INVOKABLE double get_scrcpy_audio_gain();
    Q_INVOKABLE bool get_scrcpy_audio_duck_enabled();
    Q_INVOKABLE double get_scrcpy_audio_duck_level();
    Q_INVOKABLE bool get_scrcpy_phone_screen_off();
    Q_INVOKABLE int get_scrcpy_display_dpi();
    Q_INVOKABLE int get_scrcpy_bit_rate_usb();
    Q_INVOKABLE int get_scrcpy_bit_rate_wifi();
    Q_INVOKABLE bool get_scrcpy_auto_wifi();
    Q_INVOKABLE bool get_scrcpy_bit_rate_auto();
    Q_INVOKABLE QString get_scrcpy_wireless_address();
    Q_INVOKABLE bool get_esp32_volume_enabled();
    Q_INVOKABLE QString get_esp32_volume_port();
    Q_INVOKABLE double get_esp32_volume_step_size();
    Q_INVOKABLE bool get_esp32_auto_reconnect();
    Q_INVOKABLE bool get_esp32_led_sleep_enabled();
    Q_INVOKABLE QString get_esp32_led_color_mode();
    Q_INVOKABLE QString get_esp32_led_static_color();
    Q_INVOKABLE bool get_show_waveform_visualizer();
    Q_INVOKABLE bool get_imu_enabled();
    Q_INVOKABLE bool get_gesture_sensor_enabled();
    Q_INVOKABLE QString get_gesture_mapping();
    Q_INVOKABLE QVariantMap get_gesture_mapping_dict();
    Q_INVOKABLE bool is_setting_pinned(const QString &settingId);
    Q_INVOKABLE QString get_settings_registry();
    Q_INVOKABLE QString get_pinned_settings_metadata();
    Q_INVOKABLE QString get_settings_menu_visibility();
    Q_INVOKABLE bool is_settings_section_visible(const QString &section);

    // Git info
    Q_INVOKABLE QString get_git_commit_hash();
    Q_INVOKABLE QString get_git_commit_date();
    Q_INVOKABLE QString get_git_commit_message();

private:
    // ======================================================================
    // Internal helpers
    // ======================================================================
    QJsonObject loadSettings();
    void saveSettings(const QJsonObject &settings);
    void updateSetting(const QString &key, const QVariant &value);
    // The adapters list as stored (a compact JSON array string)
    void save_obd_saved_adapters(const QString &json);
    QJsonObject validateSettings(const QJsonObject &settings);
    void setFilePermissions(const QString &filepath);
    QJsonObject buildDefaultSettings() const;
    void populateMembers();   // cached members <- m_settings (constructor + reset_to_defaults)
    static QJsonObject buildSettingsRegistry();
    QString keyToAttr(const QString &key) const;
    QString getGitRepoDir() const;

    // OBD parameter debounce
    void flushObdParameters();

    // Theme art download
    void downloadThemeArtAsync(const QString &themeName, const QString &url,
                               const QString &artDir, const QString &safeName);

    // ======================================================================
    // Member variables
    // ======================================================================

    QString m_appDataDir;
    QString m_settingsFile;
    QString m_backendDir;

    QJsonObject m_defaultSettings;
    QJsonObject m_settings;

    // Debounced disk write: saveSettings() updates m_settings and arms this;
    // flushPendingSave() (timeout, or explicit at shutdown) does the write.
    QTimer m_saveTimer;
    bool m_savePending = false;
    bool m_settingsLoaded = false;
    bool m_backupDone = false;
    QJsonObject readSettingsFromDisk();
    void writeSettingsToDisk(const QJsonObject &settings);
    QString backupFilePath() const;
    bool parseSettingsFile(const QString &path, QJsonObject &out, QString &error);
    void backupSettingsFile();
    bool atomicWrite(const QString &path, const QByteArray &data);

    // OBD parameter debounce timer (800ms)
    QTimer m_obdParamsSaveTimer;
    bool m_obdParamsDirty = false;

    // Pending theme art download result
    QString m_pendingThemeArtName;
    QString m_pendingThemeArtUrl;

    // --- Setting member variables ---
    QString m_deviceName;
    QString m_themeSetting;
    QString m_fontSetting;
    float m_startVolume = 0.1f;
    bool m_showClock = true;
    bool m_clockFormat24Hour = true;
    bool m_clockShowSeconds = false;
    int m_clockSize = 18;
    bool m_clockSizeAuto = true;
    QString m_backgroundGrid;
    int m_screenWidth = 1280;
    int m_screenHeight = 720;
    int m_backgroundBlurRadius = 40;
    float m_uiScale = 1.0f;
    int m_colorTransitionMs = 1000;
    bool m_songLengthTransition = false;
    QString m_obdBluetoothPort;
    QString m_obdSavedAdapters = QStringLiteral("[]");
    bool m_obdFastMode = true;
    bool m_obdCardStyleCircular = false;
    QString m_gaugeTempUnit = QStringLiteral("C");
    QString m_gaugeSpeedUnit = QStringLiteral("mph");
    int m_obdAutoReconnectAttempts = 0;
    QString m_mediaFolder;
    float m_fuelTankCapacity = 15.0f;
    bool m_blackBoxEnabled = true;
    bool m_blackBoxSweepAll = true;
    int m_blackBoxMaxMB = 500;
    QVariantList m_homeObdParameters;
    QString m_bottomBarOrientation;
    bool m_showBottomBarMediaControls = true;

    // Auto-play / resume
    bool m_autoPlayOnStartup = false;
    bool m_persistShuffleState = false;
    bool m_lastShuffleState = false;
    QString m_lastPlayedSong;
    int m_lastPlayedPosition = 0;
    QString m_lastPlayedPlaylist;

    // Window
    QString m_windowState;
    QString m_musicButtonDefaultPage;
    bool m_returnToLibraryAfterSelection = false;

    // Android Auto / Phone Mirror
    bool m_androidAutoEnabled = false;
    bool m_phoneMirrorEnabled = false;
    QString m_scrcpyDisplaySize;
    double m_scrcpyAudioGain = 2.0;
    bool m_scrcpyAudioDuckEnabled = true;
    double m_scrcpyAudioDuckLevel = 0.1;
    bool m_scrcpyPhoneScreenOff = false;
    int m_scrcpyDisplayDpi = 0;
    int m_scrcpyBitRateUsb = 16;
    int m_scrcpyBitRateWifi = 8;
    bool m_scrcpyAutoWifi = false;
    bool m_scrcpyBitRateAuto = true;
    QString m_scrcpyWirelessAddress;

    // Settings menu visibility
    QVariantMap m_settingsMenuVisibility;

    // ESP32
    bool m_esp32VolumeEnabled = true;
    QString m_esp32VolumePort;
    double m_esp32VolumeStepSize = 1.0;
    bool m_esp32AutoReconnect = true;
    bool m_esp32LedSleepEnabled = true;
    QString m_esp32LedColorMode;
    QString m_esp32LedStaticColor;

    // Waveform
    bool m_showWaveformVisualizer = true;

    // 3D effects
    bool m_show3DButtonTilt = false;
    bool m_show3DAlbumPreview = false;
    bool m_roundedAlbumArt = true;
    int m_albumArtCornerRadius = 16;
    bool m_showAlbumArtShadow = true;
    bool m_vinylRecordMode = false;
    QString m_albumArtTransition;
    int m_backgroundOverlayOpacity = 80;
    double m_sideCardOpacity = 0.4;
    int m_sideCardAngle = 30;
    int m_buttonTiltDuration = 200;
    int m_textScrollSpeed = 5000;

    // Settings layout
    QString m_settingsLayoutStyle;

    // IMU
    bool m_imuEnabled = true;
    int m_sensorEmitRate = 60;
    int m_sensorDecimalPlaces = 1;
    QString m_sensorTempUnit = QStringLiteral("F");
    QString m_sensorAltitudeUnit = QStringLiteral("m");
    QVariantMap m_sensorCards;

    // Gesture
    bool m_gestureSensorEnabled = true;
    QVariantMap m_gestureMapping;
    int m_gestureVolumeStep = 5;
    int m_gestureCooldown = 300;

    // Pinned settings
    QVariantList m_pinnedSettings;

    // Current volume (0-100)
    int m_currentVolume = 0;

    // Last settings section
    QString m_lastSettingsSection;

    // OBD parameters (parameter name -> enabled bool)
    QVariantMap m_obdParameters;

    // Supported OBD parameters from scan
    QVariantList m_supportedObdParameters;

    // Directory history
    QVariantList m_directoryHistory;

    // Album art colors JSON
    QString m_albumArtColors;

    // Static registry
    static QJsonObject s_settingsRegistry;
};

#endif // SETTINGSMANAGER_H
