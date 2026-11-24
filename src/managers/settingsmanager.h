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
    // Service permissions (src/service/servicescopes.h)
    Q_CLASSINFO("octave.scope", "settings")
    Q_CLASSINFO("octave.scope.read",
                "get_android_auto_enabled get_auto_play_on_startup get_directory_history "
                "get_esp32_auto_reconnect get_esp32_led_color_mode get_esp32_led_sleep_enabled "
                "get_esp32_led_static_color get_esp32_volume_enabled get_esp32_volume_port "
                "get_esp32_volume_step_size get_gesture_mapping get_gesture_mapping_dict "
                "get_gesture_sensor_enabled get_git_commit_date get_git_commit_hash get_git_commit_message "
                "get_home_obd_parameters get_imu_enabled get_last_played_playlist get_last_played_position "
                "get_last_played_song get_last_settings_section get_last_shuffle_state "
                "get_music_button_default_page get_obd_parameter_enabled get_persist_shuffle_state "
                "get_phone_mirror_enabled get_pinned_settings_metadata "
                "get_return_to_library_after_selection get_scrcpy_audio_duck_enabled "
                "get_scrcpy_audio_duck_level get_scrcpy_audio_gain get_scrcpy_auto_wifi "
                "get_scrcpy_bit_rate_auto get_scrcpy_bit_rate_usb get_scrcpy_bit_rate_wifi "
                "get_scrcpy_display_dpi get_scrcpy_display_size get_scrcpy_phone_screen_off "
                "get_scrcpy_wireless_address get_setting get_setting_with_default "
                "get_settings_menu_visibility get_settings_registry get_show_waveform_visualizer "
                "get_supported_obd_parameters get_window_state is_setting_pinned "
                "is_settings_section_visible")
    Q_CLASSINFO("octave.scope.full", "setCurrentVolume set_album_art_colors")
    // The plain settings are writable properties (the service's `set`, a QML
    // assignment), through the same checks as their save_* slots. The slots
    // and the get_* methods that only repeat a property stay until protocol 5.
    Q_CLASSINFO("octave.deprecated.save_device_name", "use the deviceName property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_font_setting", "use the fontSetting property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_theme_setting", "use the themeSetting property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_ui_scale", "use the uiScale property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_color_transition_ms", "use the colorTransitionMs property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_song_length_transition", "use the songLengthTransition property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_show_clock", "use the showClock property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_clock_format", "use the clockFormat24Hour property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_clock_show_seconds", "use the clockShowSeconds property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_clock_size", "use the clockSize property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_clock_size_auto", "use the clockSizeAuto property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_background_grid", "use the backgroundGrid property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_screen_width", "use the screenWidth property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_screen_height", "use the screenHeight property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_background_blur_radius", "use the backgroundBlurRadius property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_bottom_bar_orientation", "use the bottomBarOrientation property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_show_bottom_bar_media_controls", "use the showBottomBarMediaControls property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_settings_layout_style", "use the settingsLayoutStyle property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_start_volume", "use the startUpVolume property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_media_folder", "use the mediaFolder property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_auto_play_on_startup", "use the autoPlayOnStartup property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_persist_shuffle_state", "use the persistShuffleState property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_last_shuffle_state", "use the lastShuffleState property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_last_played_song", "use the lastPlayedSong property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_last_played_position", "use the lastPlayedPosition property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_last_played_playlist", "use the lastPlayedPlaylist property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_music_button_default_page", "use the musicButtonDefaultPage property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_return_to_library_after_selection", "use the returnToLibraryAfterSelection property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_show_waveform_visualizer", "use the showWaveformVisualizer property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_show_3d_button_tilt", "use the show3DButtonTilt property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_show_3d_album_preview", "use the show3DAlbumPreview property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_rounded_album_art", "use the roundedAlbumArt property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_album_art_corner_radius", "use the albumArtCornerRadius property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_show_album_art_shadow", "use the showAlbumArtShadow property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_vinyl_record_mode", "use the vinylRecordMode property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_album_art_transition", "use the albumArtTransition property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_audio_output_device", "use the audioOutputDevice property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_background_overlay_opacity", "use the backgroundOverlayOpacity property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_side_card_opacity", "use the sideCardOpacity property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_side_card_angle", "use the sideCardAngle property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_button_tilt_duration", "use the buttonTiltDuration property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_text_scroll_speed", "use the textScrollSpeed property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_obd_bluetooth_port", "use the obdBluetoothPort property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_obd_fast_mode", "use the obdFastMode property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_obd_card_style_circular", "use the obdCardStyleCircular property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_gauge_temp_unit", "use the gaugeTempUnit property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_gauge_speed_unit", "use the gaugeSpeedUnit property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_obd_auto_reconnect_attempts", "use the obdAutoReconnectAttempts property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_fuel_tank_capacity", "use the fuelTankCapacity property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_black_box_enabled", "use the blackBoxEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_black_box_sweep_all", "use the blackBoxSweepAll property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_black_box_max_mb", "use the blackBoxMaxMB property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_home_obd_parameters", "use the homeOBDParameters property; goes in protocol 5")
    // Home is a dashboard (dashboardManager.homeDashboardId): the classic Home's card list and vehicle switch go with it
    Q_CLASSINFO("octave.deprecated.homeOBDParameters", "Home is a dashboard: dashboardManager.homeDashboardId; goes with the classic Home")
    Q_CLASSINFO("octave.deprecated.homeOBDParametersChanged", "Home is a dashboard: dashboardManager.homeDashboardChanged; goes with the classic Home")
    Q_CLASSINFO("octave.deprecated.maxHomeObdParameters", "Home is a dashboard: dashboardManager.homeDashboardId; goes with the classic Home")
    Q_CLASSINFO("octave.deprecated.homeVehicleCard", "Home is a dashboard: place a VehicleWidget; goes with the classic Home")
    Q_CLASSINFO("octave.deprecated.homeVehicleCardChanged", "Home is a dashboard: place a VehicleWidget; goes with the classic Home")
    Q_CLASSINFO("octave.deprecated.save_window_state", "use the windowState property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.set_last_settings_section", "use the lastSettingsSection property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_android_auto_enabled", "use the androidAutoEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_phone_mirror_enabled", "use the phoneMirrorEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_display_size", "use the scrcpyDisplaySize property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_audio_gain", "use the scrcpyAudioGain property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_audio_gain_db", "use the scrcpyAudioGainDb property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_audio_duck_level_db", "use the scrcpyAudioDuckLevelDb property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_audio_duck_enabled", "use the scrcpyAudioDuckEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_audio_duck_level", "use the scrcpyAudioDuckLevel property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_phone_screen_off", "use the scrcpyPhoneScreenOff property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_display_dpi", "use the scrcpyDisplayDpi property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_bit_rate_usb", "use the scrcpyBitRateUsb property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_bit_rate_wifi", "use the scrcpyBitRateWifi property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_auto_wifi", "use the scrcpyAutoWifi property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_bit_rate_auto", "use the scrcpyBitRateAuto property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_scrcpy_wireless_address", "use the scrcpyWirelessAddress property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_esp32_volume_enabled", "use the esp32VolumeEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_esp32_volume_port", "use the esp32VolumePort property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_esp32_volume_step_size", "use the esp32VolumeStepSize property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_esp32_auto_reconnect", "use the esp32AutoReconnect property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_esp32_led_sleep_enabled", "use the esp32LedSleepEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_esp32_led_color_mode", "use the esp32LedColorMode property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_esp32_led_static_color", "use the esp32LedStaticColor property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_imu_enabled", "use the imuEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_sensor_emit_rate", "use the sensorEmitRate property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_sensor_decimal_places", "use the sensorDecimalPlaces property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_sensor_temp_unit", "use the sensorTempUnit property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_sensor_altitude_unit", "use the sensorAltitudeUnit property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_gesture_sensor_enabled", "use the gestureSensorEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_gesture_volume_step", "use the gestureVolumeStep property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.save_gesture_cooldown", "use the gestureCooldown property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_directory_history", "use the directoryHistory property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_home_obd_parameters", "use the homeOBDParameters property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_auto_play_on_startup", "use the autoPlayOnStartup property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_persist_shuffle_state", "use the persistShuffleState property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_last_shuffle_state", "use the lastShuffleState property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_last_played_song", "use the lastPlayedSong property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_last_played_position", "use the lastPlayedPosition property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_last_played_playlist", "use the lastPlayedPlaylist property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_last_settings_section", "use the lastSettingsSection property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_window_state", "use the windowState property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_music_button_default_page", "use the musicButtonDefaultPage property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_return_to_library_after_selection", "use the returnToLibraryAfterSelection property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_android_auto_enabled", "use the androidAutoEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_phone_mirror_enabled", "use the phoneMirrorEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_display_size", "use the scrcpyDisplaySize property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_audio_gain", "use the scrcpyAudioGain property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_audio_duck_enabled", "use the scrcpyAudioDuckEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_audio_duck_level", "use the scrcpyAudioDuckLevel property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_phone_screen_off", "use the scrcpyPhoneScreenOff property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_display_dpi", "use the scrcpyDisplayDpi property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_bit_rate_usb", "use the scrcpyBitRateUsb property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_bit_rate_wifi", "use the scrcpyBitRateWifi property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_auto_wifi", "use the scrcpyAutoWifi property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_bit_rate_auto", "use the scrcpyBitRateAuto property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_scrcpy_wireless_address", "use the scrcpyWirelessAddress property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_esp32_volume_enabled", "use the esp32VolumeEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_esp32_volume_port", "use the esp32VolumePort property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_esp32_volume_step_size", "use the esp32VolumeStepSize property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_esp32_auto_reconnect", "use the esp32AutoReconnect property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_esp32_led_sleep_enabled", "use the esp32LedSleepEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_esp32_led_color_mode", "use the esp32LedColorMode property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_esp32_led_static_color", "use the esp32LedStaticColor property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_show_waveform_visualizer", "use the showWaveformVisualizer property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_imu_enabled", "use the imuEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_gesture_sensor_enabled", "use the gestureSensorEnabled property; goes in protocol 5")
    Q_CLASSINFO("octave.deprecated.get_settings_menu_visibility", "use the settingsMenuVisibility property; goes in protocol 5")
    friend class TstSettingsManager;

    // ======================================================================
    // Q_PROPERTY declarations. A plain setting (one value under one key of
    // settingsConfigure.json) is a row of the table in settingsmanager.cpp:
    // its default, type, limits and notify signal live there, and its
    // property is writable through its save_* slot.
    // ======================================================================

    // --- Device ---
    Q_PROPERTY(QString deviceName READ deviceName WRITE save_device_name NOTIFY deviceNameChanged)
    Q_PROPERTY(QString fontSetting READ fontSetting WRITE save_font_setting NOTIFY fontSettingChanged)

    // --- Theme / Display ---
    Q_PROPERTY(QString themeSetting READ themeSetting WRITE save_theme_setting NOTIFY themeSettingChanged)
    Q_PROPERTY(float uiScale READ uiScale WRITE save_ui_scale NOTIFY uiScaleChanged)
    Q_PROPERTY(int colorTransitionMs READ colorTransitionMs WRITE save_color_transition_ms NOTIFY colorTransitionMsChanged)
    Q_PROPERTY(bool songLengthTransition READ songLengthTransition WRITE save_song_length_transition NOTIFY songLengthTransitionChanged)
    Q_PROPERTY(bool showClock READ showClock WRITE save_show_clock NOTIFY showClockChanged)
    Q_PROPERTY(bool clockFormat24Hour READ clockFormat24Hour WRITE save_clock_format NOTIFY clockFormatChanged)
    Q_PROPERTY(bool clockShowSeconds READ clockShowSeconds WRITE save_clock_show_seconds NOTIFY clockShowSecondsChanged)
    Q_PROPERTY(int clockSize READ clockSize WRITE save_clock_size NOTIFY clockSizeChanged)
    Q_PROPERTY(bool clockSizeAuto READ clockSizeAuto WRITE save_clock_size_auto NOTIFY clockSizeAutoChanged)
    Q_PROPERTY(QString backgroundGrid READ backgroundGrid WRITE save_background_grid NOTIFY backgroundGridChanged)
    Q_PROPERTY(int screenWidth READ screenWidth WRITE save_screen_width NOTIFY screenWidthChanged)
    Q_PROPERTY(int screenHeight READ screenHeight WRITE save_screen_height NOTIFY screenHeightChanged)
    Q_PROPERTY(int backgroundBlurRadius READ backgroundBlurRadius WRITE save_background_blur_radius NOTIFY backgroundBlurRadiusChanged)
    Q_PROPERTY(QString bottomBarOrientation READ bottomBarOrientation WRITE save_bottom_bar_orientation NOTIFY bottomBarOrientationChanged)
    Q_PROPERTY(bool showBottomBarMediaControls READ showBottomBarMediaControls WRITE save_show_bottom_bar_media_controls NOTIFY showBottomBarMediaControlsChanged)
    Q_PROPERTY(QString settingsLayoutStyle READ settingsLayoutStyle WRITE save_settings_layout_style NOTIFY settingsLayoutStyleChanged)
    // The Album Art Capture palette extracted from the playing art, as a theme
    // object ({base, baseAlt, accent, text, ...}; empty before the first
    // extraction). Stored as a JSON string under the same key.
    Q_PROPERTY(QVariantMap albumArtColors READ albumArtColors NOTIFY albumArtColorsChanged)
    Q_PROPERTY(QString settingsMenuVisibility READ settingsMenuVisibility NOTIFY settingsMenuVisibilityChanged)

    // --- Media ---
    Q_PROPERTY(float startUpVolume READ startUpVolume WRITE save_start_volume NOTIFY startUpVolumeChanged)
    Q_PROPERTY(int currentVolume READ currentVolume NOTIFY currentVolumeChanged)
    Q_PROPERTY(QString mediaFolder READ mediaFolder WRITE save_media_folder NOTIFY mediaFolderChanged)
    Q_PROPERTY(bool autoPlayOnStartup READ autoPlayOnStartup WRITE save_auto_play_on_startup NOTIFY autoPlayOnStartupChanged)
    Q_PROPERTY(bool persistShuffleState READ persistShuffleState WRITE save_persist_shuffle_state NOTIFY persistShuffleStateChanged)
    Q_PROPERTY(bool lastShuffleState READ lastShuffleState WRITE save_last_shuffle_state NOTIFY lastShuffleStateChanged)
    Q_PROPERTY(QString lastPlayedSong READ lastPlayedSong WRITE save_last_played_song NOTIFY lastPlayedSongChanged)
    Q_PROPERTY(int lastPlayedPosition READ lastPlayedPosition WRITE save_last_played_position NOTIFY lastPlayedPositionChanged)
    Q_PROPERTY(QString lastPlayedPlaylist READ lastPlayedPlaylist WRITE save_last_played_playlist NOTIFY lastPlayedPlaylistChanged)
    Q_PROPERTY(QString musicButtonDefaultPage READ musicButtonDefaultPage WRITE save_music_button_default_page NOTIFY musicButtonDefaultPageChanged)
    Q_PROPERTY(bool returnToLibraryAfterSelection READ returnToLibraryAfterSelection WRITE save_return_to_library_after_selection NOTIFY returnToLibraryAfterSelectionChanged)
    Q_PROPERTY(bool showWaveformVisualizer READ showWaveformVisualizer WRITE save_show_waveform_visualizer NOTIFY showWaveformVisualizerChanged)

    // --- 3D visual effects ---
    Q_PROPERTY(bool show3DButtonTilt READ show3DButtonTilt WRITE save_show_3d_button_tilt NOTIFY show3DButtonTiltChanged)
    Q_PROPERTY(bool show3DAlbumPreview READ show3DAlbumPreview WRITE save_show_3d_album_preview NOTIFY show3DAlbumPreviewChanged)
    Q_PROPERTY(bool roundedAlbumArt READ roundedAlbumArt WRITE save_rounded_album_art NOTIFY roundedAlbumArtChanged)
    Q_PROPERTY(int albumArtCornerRadius READ albumArtCornerRadius WRITE save_album_art_corner_radius NOTIFY albumArtCornerRadiusChanged)
    Q_PROPERTY(bool showAlbumArtShadow READ showAlbumArtShadow WRITE save_show_album_art_shadow NOTIFY showAlbumArtShadowChanged)
    Q_PROPERTY(bool vinylRecordMode READ vinylRecordMode WRITE save_vinyl_record_mode NOTIFY vinylRecordModeChanged)
    Q_PROPERTY(QString albumArtTransition READ albumArtTransition WRITE save_album_art_transition NOTIFY albumArtTransitionChanged)
    // The audio output to play to (a QAudioDevice id); empty = the system default
    Q_PROPERTY(QString audioOutputDevice READ audioOutputDevice WRITE save_audio_output_device NOTIFY audioOutputDeviceChanged)
    Q_PROPERTY(int backgroundOverlayOpacity READ backgroundOverlayOpacity WRITE save_background_overlay_opacity NOTIFY backgroundOverlayOpacityChanged)
    Q_PROPERTY(double sideCardOpacity READ sideCardOpacity WRITE save_side_card_opacity NOTIFY sideCardOpacityChanged)
    Q_PROPERTY(int sideCardAngle READ sideCardAngle WRITE save_side_card_angle NOTIFY sideCardAngleChanged)
    Q_PROPERTY(int buttonTiltDuration READ buttonTiltDuration WRITE save_button_tilt_duration NOTIFY buttonTiltDurationChanged)
    Q_PROPERTY(int textScrollSpeed READ textScrollSpeed WRITE save_text_scroll_speed NOTIFY textScrollSpeedChanged)

    // --- OBD ---
    Q_PROPERTY(QString obdBluetoothPort READ obdBluetoothPort WRITE save_obd_bluetooth_port NOTIFY obdBluetoothPortChanged)
    // Recently used OBD adapters, newest first, at most 8: {name, mac} maps
    // (stored as a JSON string under the same key)
    Q_PROPERTY(QVariantList obdSavedAdapters READ obdSavedAdapters NOTIFY obdSavedAdaptersChanged)
    Q_PROPERTY(bool obdFastMode READ obdFastMode WRITE save_obd_fast_mode NOTIFY obdFastModeChanged)
    Q_PROPERTY(bool obdCardStyleCircular READ obdCardStyleCircular WRITE save_obd_card_style_circular NOTIFY obdCardStyleCircularChanged)
    // Units the OBD gauges and dashboards show (ParameterCatalog converts):
    // temperatures "C" | "F", speed "mph" | "km/h". Altitude follows
    // sensorAltitudeUnit. The Sensors page keeps sensorTempUnit.
    Q_PROPERTY(QString gaugeTempUnit READ gaugeTempUnit WRITE save_gauge_temp_unit NOTIFY gaugeTempUnitChanged)
    Q_PROPERTY(QString gaugeSpeedUnit READ gaugeSpeedUnit WRITE save_gauge_speed_unit NOTIFY gaugeSpeedUnitChanged)
    Q_PROPERTY(int obdAutoReconnectAttempts READ obdAutoReconnectAttempts WRITE save_obd_auto_reconnect_attempts NOTIFY obdAutoReconnectAttemptsChanged)
    Q_PROPERTY(float fuelTankCapacity READ fuelTankCapacity WRITE save_fuel_tank_capacity NOTIFY fuelTankCapacityChanged)
    // Black box (TripRecorder): record every drive, also sweep PIDs that are
    // not on screen, and the on-disk cap for the history (database + files)
    Q_PROPERTY(bool blackBoxEnabled READ blackBoxEnabled WRITE save_black_box_enabled NOTIFY blackBoxEnabledChanged)
    Q_PROPERTY(bool blackBoxSweepAll READ blackBoxSweepAll WRITE save_black_box_sweep_all NOTIFY blackBoxSweepAllChanged)
    Q_PROPERTY(int blackBoxMaxMB READ blackBoxMaxMB WRITE save_black_box_max_mb NOTIFY blackBoxMaxMBChanged)
    Q_PROPERTY(QVariantList homeOBDParameters READ homeOBDParameters WRITE save_home_obd_parameters NOTIFY homeOBDParametersChanged)
    // The home screen holds at most this many OBD cards
    Q_PROPERTY(int maxHomeObdParameters READ maxHomeObdParameters CONSTANT)
    // The vehicle's picture as a card with the Home readings
    Q_PROPERTY(bool homeVehicleCard READ homeVehicleCard WRITE setHomeVehicleCard NOTIFY homeVehicleCardChanged)
    Q_PROPERTY(QVariantList customThemes READ customThemes NOTIFY customThemesChanged)   // names
    // Every saved custom theme, name -> theme object (as on disk)
    Q_PROPERTY(QVariantMap customThemeData READ customThemeData NOTIFY customThemesChanged)
    // The Now Playing look presets, [{label, settings: {key: value}}]
    Q_PROPERTY(QVariantList nowPlayingPresets READ nowPlayingPresets CONSTANT)
    Q_PROPERTY(QVariantList directoryHistory READ directoryHistory NOTIFY directoryHistoryChanged)

    // --- Window ---
    Q_PROPERTY(QString windowState READ windowState WRITE save_window_state NOTIFY windowStateChanged)
    Q_PROPERTY(QString lastSettingsSection READ lastSettingsSection WRITE set_last_settings_section NOTIFY lastSettingsSectionChanged)

    // --- Android Auto / Phone Mirror ---
    Q_PROPERTY(bool androidAutoEnabled READ androidAutoEnabled WRITE save_android_auto_enabled NOTIFY androidAutoEnabledChanged)
    Q_PROPERTY(bool phoneMirrorEnabled READ phoneMirrorEnabled WRITE save_phone_mirror_enabled NOTIFY phoneMirrorEnabledChanged)
    Q_PROPERTY(QString scrcpyDisplaySize READ scrcpyDisplaySize WRITE save_scrcpy_display_size NOTIFY scrcpyDisplaySizeChanged)
    Q_PROPERTY(double scrcpyAudioGain READ scrcpyAudioGain WRITE save_scrcpy_audio_gain NOTIFY scrcpyAudioGainChanged)
    // The same two levels in dB, as the Phone Audio sliders show them (the
    // file and the audio path keep the linear factors above)
    Q_PROPERTY(double scrcpyAudioGainDb READ scrcpyAudioGainDb WRITE save_scrcpy_audio_gain_db NOTIFY scrcpyAudioGainChanged)
    Q_PROPERTY(double scrcpyAudioDuckLevelDb READ scrcpyAudioDuckLevelDb WRITE save_scrcpy_audio_duck_level_db NOTIFY scrcpyAudioDuckLevelChanged)
    Q_PROPERTY(bool scrcpyAudioDuckEnabled READ scrcpyAudioDuckEnabled WRITE save_scrcpy_audio_duck_enabled NOTIFY scrcpyAudioDuckEnabledChanged)
    Q_PROPERTY(double scrcpyAudioDuckLevel READ scrcpyAudioDuckLevel WRITE save_scrcpy_audio_duck_level NOTIFY scrcpyAudioDuckLevelChanged)
    Q_PROPERTY(bool scrcpyPhoneScreenOff READ scrcpyPhoneScreenOff WRITE save_scrcpy_phone_screen_off NOTIFY scrcpyPhoneScreenOffChanged)
    // Virtual display density override, 0 = scale the phone's own density
    Q_PROPERTY(int scrcpyDisplayDpi READ scrcpyDisplayDpi WRITE save_scrcpy_display_dpi NOTIFY scrcpyDisplayDpiChanged)
    // Video bit rate in Mbps, per link
    Q_PROPERTY(int scrcpyBitRateUsb READ scrcpyBitRateUsb WRITE save_scrcpy_bit_rate_usb NOTIFY scrcpyBitRateUsbChanged)
    Q_PROPERTY(int scrcpyBitRateWifi READ scrcpyBitRateWifi WRITE save_scrcpy_bit_rate_wifi NOTIFY scrcpyBitRateWifiChanged)
    // Switch a USB phone to adb over TCP (port 5555) so it can be reached over Wi-Fi later
    Q_PROPERTY(bool scrcpyAutoWifi READ scrcpyAutoWifi WRITE save_scrcpy_auto_wifi NOTIFY scrcpyAutoWifiChanged)
    // Adapt the mirror bit rate to the link; the per-link rates become the ceiling
    Q_PROPERTY(bool scrcpyBitRateAuto READ scrcpyBitRateAuto WRITE save_scrcpy_bit_rate_auto NOTIFY scrcpyBitRateAutoChanged)
    // Last phone connected over Wi-Fi ("ip:port"); reconnected automatically, "" = none
    Q_PROPERTY(QString scrcpyWirelessAddress READ scrcpyWirelessAddress WRITE save_scrcpy_wireless_address NOTIFY scrcpyWirelessAddressChanged)

    // --- ESP32 Volume Knob ---
    Q_PROPERTY(bool esp32VolumeEnabled READ esp32VolumeEnabled WRITE save_esp32_volume_enabled NOTIFY esp32VolumeEnabledChanged)
    Q_PROPERTY(QString esp32VolumePort READ esp32VolumePort WRITE save_esp32_volume_port NOTIFY esp32VolumePortChanged)
    Q_PROPERTY(double esp32VolumeStepSize READ esp32VolumeStepSize WRITE save_esp32_volume_step_size NOTIFY esp32VolumeStepSizeChanged)
    Q_PROPERTY(bool esp32AutoReconnect READ esp32AutoReconnect WRITE save_esp32_auto_reconnect NOTIFY esp32AutoReconnectChanged)
    Q_PROPERTY(bool esp32LedSleepEnabled READ esp32LedSleepEnabled WRITE save_esp32_led_sleep_enabled NOTIFY esp32LedSleepEnabledChanged)
    Q_PROPERTY(QString esp32LedColorMode READ esp32LedColorMode WRITE save_esp32_led_color_mode NOTIFY esp32LedColorModeChanged)
    Q_PROPERTY(QString esp32LedStaticColor READ esp32LedStaticColor WRITE save_esp32_led_static_color NOTIFY esp32LedStaticColorChanged)

    // --- IMU ---
    Q_PROPERTY(bool imuEnabled READ imuEnabled WRITE save_imu_enabled NOTIFY imuEnabledChanged)
    Q_PROPERTY(int sensorEmitRate READ sensorEmitRate WRITE save_sensor_emit_rate NOTIFY sensorEmitRateChanged)   // Hz, 10-120
    // Sensors page display (SensorMenu binds to these)
    Q_PROPERTY(int sensorDecimalPlaces READ sensorDecimalPlaces WRITE save_sensor_decimal_places NOTIFY sensorDecimalPlacesChanged)     // 0-4
    Q_PROPERTY(QString sensorTempUnit READ sensorTempUnit WRITE save_sensor_temp_unit NOTIFY sensorTempUnitChanged)               // "F" | "C"
    Q_PROPERTY(QString sensorAltitudeUnit READ sensorAltitudeUnit WRITE save_sensor_altitude_unit NOTIFY sensorAltitudeUnitChanged)   // "m" | "ft"
    // Which readouts show: {Pitch, Roll, Heading, GForce, Altitude, Temperature} -> bool
    Q_PROPERTY(QVariantMap sensorCards READ sensorCards NOTIFY sensorCardsChanged)

    // --- Gesture Sensor ---
    Q_PROPERTY(bool gestureSensorEnabled READ gestureSensorEnabled WRITE save_gesture_sensor_enabled NOTIFY gestureSensorEnabledChanged)
    Q_PROPERTY(int gestureVolumeStep READ gestureVolumeStep WRITE save_gesture_volume_step NOTIFY gestureVolumeStepChanged)
    Q_PROPERTY(int gestureCooldown READ gestureCooldown WRITE save_gesture_cooldown NOTIFY gestureCooldownChanged)

    // --- Pinned Settings ---
    Q_PROPERTY(QVariantList pinnedSettings READ pinnedSettings NOTIFY pinnedSettingsChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr);

    // Returns the OS-specific app data directory path (or the override)
    static QString getAppDataDir();
    // Use `dir` instead of the OS folder for settings, trips, dashboards and
    // logs (`--config DIR`, the --sim throwaway). Before anything reads it
    static void setAppDataDirOverride(const QString &dir);

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
    QString audioOutputDevice() const;
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
    QString gaugeTempUnit() const;
    QString gaugeSpeedUnit() const;
    int obdAutoReconnectAttempts() const;
    float fuelTankCapacity() const;
    bool blackBoxEnabled() const;
    bool blackBoxSweepAll() const;
    int blackBoxMaxMB() const;
    QVariantList homeOBDParameters() const;
    // No settings file was here at start: a fresh install (not for QML or the service)
    bool createdThisRun() const { return m_createdThisRun; }
    bool homeVehicleCard() const;
    static constexpr int kMaxHomeObdParameters = 8;
    // Saved window size range: the window's own minimum up to 8K (a bigger
    // value would be clamped and then pushed back onto a real window)
    static constexpr int kMinScreenWidth = 320;   // = Main.qml minimumWidth
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
    // ...and read by them (get_setting refuses these keys)
    QVariant fileOnlySetting(const QString &key);
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
    void audioOutputDeviceChanged(const QString &value);
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
    void homeVehicleCardChanged(bool value);
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
    // An absolute path (or a file:// URL), never the filesystem root;
    // false and nothing saved otherwise
    bool save_media_folder(const QString &folderPath);
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
    void save_audio_output_device(const QString &id);
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
    void setHomeVehicleCard(bool enabled);
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
    void populateMembers();   // everything below m_settings <- m_settings (constructor + reset_to_defaults)
    void populateStructured();   // the list / map settings only
    static QJsonObject buildSettingsRegistry();

    // The table-driven core (settingsmanager.cpp, "The settings table").
    // m_settings is what the file holds; m_values is each table row's value
    // in its own type, written by setRow() and nothing else, so a typed
    // setter, a property write, save_setting() and apply_settings() cannot
    // leave two answers for one key.
    enum class SetResult { Refused, Unchanged, Changed };
    QVariant row(int index) const { return m_values.at(index); }
    // `strictType`: the value came from outside (save_setting, the service)
    // and must already be the row's type
    SetResult setRow(int index, QVariant value, bool strictType = false);
    QVariantList m_values;
    // The table as data, for tst_settingsmanager
    static QVariantList describeTable();
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
    bool m_createdThisRun = false;
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

    // --- Settings that are a list or a map, each kept by its own savers ---
    // (to move out one day: the pinned-settings registry, custom themes and
    // their art download, and the git information are not settings storage)
    QString m_mediaFolder;                 // "" in the file = the OS music folder
    QVariantList m_homeObdParameters;
    QVariantMap m_settingsMenuVisibility;
    QVariantMap m_gestureMapping;
    QVariantList m_pinnedSettings;
    QVariantMap m_obdParameters;           // parameter name -> enabled
    QVariantList m_supportedObdParameters; // from the last vehicle scan
    QVariantList m_directoryHistory;

    // Current volume (0-100): live, never saved; starts from startUpVolume
    int m_currentVolume = 0;

    // Static registry
    static QJsonObject s_settingsRegistry;
};

#endif // SETTINGSMANAGER_H
