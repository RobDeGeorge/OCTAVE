import json
from PySide6.QtCore import QObject, Property, Signal, Slot, QTimer, QUrl
import os
import sys
import re
import shutil
import tempfile
import subprocess
from typing import List

from backend.logging_config import get_logger
logger = get_logger(__name__)

# Settings registry — maps setting IDs to metadata for dynamic Quick Panel rendering
SETTINGS_REGISTRY = {
    "startup_volume": {
        "key": "startUpVolume", "label": "Startup Volume", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_start_volume",
        "params": {"from": 0, "to": 1, "stepSize": 0.01, "logarithmic": True}
    },
    "auto_play_on_startup": {
        "key": "autoPlayOnStartup", "label": "Autoplay on Startup", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_auto_play_on_startup"
    },
    "persist_shuffle_state": {
        "key": "persistShuffleState", "label": "Remember Shuffle State", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_persist_shuffle_state"
    },
    "show_waveform_visualizer": {
        "key": "showWaveformVisualizer", "label": "Waveform Visualizer", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_show_waveform_visualizer"
    },
    "show_3d_button_tilt": {
        "key": "show3DButtonTilt", "label": "3D Button Tilt", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_show_3d_button_tilt"
    },
    "show_3d_album_preview": {
        "key": "show3DAlbumPreview", "label": "3D Album Preview", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_show_3d_album_preview"
    },
    "rounded_album_art": {
        "key": "roundedAlbumArt", "label": "Rounded Album Art", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_rounded_album_art"
    },
    "album_art_corner_radius": {
        "key": "albumArtCornerRadius", "label": "Corner Radius", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_album_art_corner_radius",
        "params": {"from": 2, "to": 32, "stepSize": 1}
    },
    "show_album_art_shadow": {
        "key": "showAlbumArtShadow", "label": "Album Art Shadow", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_show_album_art_shadow"
    },
    "vinyl_record_mode": {
        "key": "vinylRecordMode", "label": "Vinyl Record Mode", "category": "mediaSettings",
        "controlType": "toggle", "saveSlot": "save_vinyl_record_mode"
    },
    "album_art_transition": {
        "key": "albumArtTransition", "label": "Track Transition", "category": "mediaSettings",
        "controlType": "chips", "saveSlot": "save_album_art_transition",
        "params": {"options": ["Crossfade", "Slide", "Vinyl Lift", "Dissolve", "Flip",
                                "Coverflow", "Conveyor", "Stack", "Depth", "Swing"]}
    },
    "background_overlay_opacity": {
        "key": "backgroundOverlayOpacity", "label": "Overlay Opacity", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_background_overlay_opacity",
        "params": {"from": 0, "to": 100, "stepSize": 1}
    },
    "side_card_opacity": {
        "key": "sideCardOpacity", "label": "Side Card Opacity", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_side_card_opacity",
        "params": {"from": 0.1, "to": 1.0, "stepSize": 0.05}
    },
    "side_card_angle": {
        "key": "sideCardAngle", "label": "Side Card Angle", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_side_card_angle",
        "params": {"from": 5, "to": 60, "stepSize": 1}
    },
    "button_tilt_duration": {
        "key": "buttonTiltDuration", "label": "Tilt Duration", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_button_tilt_duration",
        "params": {"from": 50, "to": 500, "stepSize": 10}
    },
    "text_scroll_speed": {
        "key": "textScrollSpeed", "label": "Text Scroll Speed", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_text_scroll_speed",
        "params": {"from": 1000, "to": 10000, "stepSize": 500}
    },
    "ui_scale": {
        "key": "uiScale", "label": "UI Scaling", "category": "displaySettings",
        "controlType": "slider", "saveSlot": "save_ui_scale",
        "params": {"from": 0.5, "to": 2.0, "stepSize": 0.05}
    },
    "theme_setting": {
        "key": "themeSetting", "label": "Theme", "category": "displaySettings",
        "controlType": "chips", "saveSlot": "save_theme_setting",
        "params": {"optionsSource": "themeList"}
    },
    "show_clock": {
        "key": "showClock", "label": "Show Clock", "category": "displaySettings",
        "controlType": "toggle", "saveSlot": "save_show_clock"
    },
    "bottom_bar_media_controls": {
        "key": "showBottomBarMediaControls", "label": "Nav Bar Media Controls", "category": "displaySettings",
        "controlType": "toggle", "saveSlot": "save_show_bottom_bar_media_controls"
    },
    "background_blur_radius": {
        "key": "backgroundBlurRadius", "label": "Album Art Blur", "category": "mediaSettings",
        "controlType": "slider", "saveSlot": "save_background_blur_radius",
        "params": {"from": 0, "to": 100, "stepSize": 1}
    },
    "color_transition_ms": {
        "key": "colorTransitionMs", "label": "Theme Transition Speed", "category": "displaySettings",
        "controlType": "slider", "saveSlot": "save_color_transition_ms",
        "params": {"from": 0, "to": 5000, "stepSize": 100}
    },
    "obd_fast_mode": {
        "key": "obdFastMode", "label": "OBD Fast Mode", "category": "obdSettings",
        "controlType": "toggle", "saveSlot": "save_obd_fast_mode"
    },
    "gesture_sensor_enabled": {
        "key": "gestureSensorEnabled", "label": "Gesture Sensor", "category": "gestureSensorSettings",
        "controlType": "toggle", "saveSlot": "save_gesture_sensor_enabled"
    },
    "imu_enabled": {
        "key": "imuEnabled", "label": "IMU Sensor", "category": "imuSettings",
        "controlType": "toggle", "saveSlot": "save_imu_enabled"
    },
}

# Platform-specific imports for file locking
if sys.platform == 'win32':
    import msvcrt
else:
    import fcntl


def get_app_data_dir():
    """Get the appropriate directory for storing app data based on platform."""
    app_name = "OCTAVE"

    if sys.platform == 'win32':
        # Windows: Use %APPDATA%/OCTAVE
        base = os.environ.get('APPDATA', os.path.expanduser('~'))
        data_dir = os.path.join(base, app_name)
    elif sys.platform == 'darwin':
        # macOS: Use ~/Library/Application Support/OCTAVE
        data_dir = os.path.join(os.path.expanduser('~'), 'Library', 'Application Support', app_name)
    else:
        # Linux: Use ~/.config/OCTAVE
        base = os.environ.get('XDG_CONFIG_HOME', os.path.join(os.path.expanduser('~'), '.config'))
        data_dir = os.path.join(base, app_name)

    # Create directory if it doesn't exist
    os.makedirs(data_dir, exist_ok=True)
    return data_dir


class SettingsManager(QObject):
    # Existing signals
    deviceNameChanged = Signal(str)
    themeSettingChanged = Signal(str)
    fontSettingChanged = Signal(str)
    startUpVolumeChanged = Signal(str)
    showClockChanged = Signal(bool)
    clockFormatChanged = Signal(bool)
    clockShowSecondsChanged = Signal(bool)
    clockSizeChanged = Signal(int)
    backgroundGridChanged = Signal(str)
    screenWidthChanged = Signal(int)
    screenHeightChanged = Signal(int)
    backgroundBlurRadiusChanged = Signal(int)
    uiScaleChanged = Signal(float)
    colorTransitionMsChanged = Signal(int)
    songLengthTransitionChanged = Signal(bool)
    obdBluetoothPortChanged = Signal(str)
    obdSavedAdaptersChanged = Signal(str)
    obdFastModeChanged = Signal(bool)
    obdParametersChanged = Signal()
    obdAutoReconnectAttemptsChanged = Signal(int)
    mediaFolderChanged = Signal(str)
    directoryHistoryChanged = Signal()
    homeOBDParametersChanged = Signal()
    supportedOBDParametersChanged = Signal()
    customThemesChanged = Signal()
    bottomBarOrientationChanged = Signal(str)
    showBottomBarMediaControlsChanged = Signal(bool)
    spotifyCredentialsChanged = Signal()
    mediaSourceChanged = Signal(str)  # "local" or "spotify"
    lastSettingsSectionChanged = Signal(str)
    currentVolumeChanged = Signal(int)  # Unified volume (0-100) for both local and Spotify
    autoPlayOnStartupChanged = Signal(bool)
    persistShuffleStateChanged = Signal(bool)
    lastShuffleStateChanged = Signal(bool)
    lastPlayedSongChanged = Signal(str)
    lastPlayedPositionChanged = Signal(int)
    lastPlayedPlaylistChanged = Signal(str)
    windowStateChanged = Signal(str)
    musicButtonDefaultPageChanged = Signal(str)
    returnToLibraryAfterSelectionChanged = Signal(bool)
    androidAutoEnabledChanged = Signal(bool)
    phoneMirrorEnabledChanged = Signal(bool)
    scrcpyAudioEnabledChanged = Signal(bool)
    scrcpyDisplaySizeChanged = Signal(str)
    scrcpyAudioGainChanged = Signal(float)
    albumArtColorsChanged = Signal(str)  # JSON string with album art theme colors

    # Settings menu visibility signals
    settingsMenuVisibilityChanged = Signal()  # Emitted when any menu visibility changes

    # Generic settings changed signal - emitted when save_setting is called
    genericSettingChanged = Signal(str)  # Emits the key that changed

    # ESP32 Volume Knob signals
    esp32VolumeEnabledChanged = Signal(bool)
    esp32VolumePortChanged = Signal(str)
    esp32VolumeStepSizeChanged = Signal(float)
    esp32AutoReconnectChanged = Signal(bool)
    esp32LedSleepEnabledChanged = Signal(bool)
    esp32LedColorModeChanged = Signal(str)  # "theme" or "static"
    esp32LedStaticColorChanged = Signal(str)  # hex color like "#FF0000"

    # Waveform visualizer signals
    showWaveformVisualizerChanged = Signal(bool)

    # 3D visual effect signals
    show3DButtonTiltChanged = Signal(bool)
    show3DAlbumPreviewChanged = Signal(bool)
    roundedAlbumArtChanged = Signal(bool)
    albumArtCornerRadiusChanged = Signal(int)
    showAlbumArtShadowChanged = Signal(bool)
    vinylRecordModeChanged = Signal(bool)
    albumArtTransitionChanged = Signal(str)
    backgroundOverlayOpacityChanged = Signal(int)
    sideCardOpacityChanged = Signal(float)
    sideCardAngleChanged = Signal(int)
    buttonTiltDurationChanged = Signal(int)
    textScrollSpeedChanged = Signal(int)

    # Settings layout style signal
    settingsLayoutStyleChanged = Signal(str)

    # IMU sensor signals
    imuEnabledChanged = Signal(bool)

    # Gesture sensor signals
    gestureSensorEnabledChanged = Signal(bool)
    gestureMappingChanged = Signal()
    gestureVolumeStepChanged = Signal(int)
    gestureCooldownChanged = Signal(int)

    # Pinned settings signals
    pinnedSettingsChanged = Signal()

    def __init__(self):
        self._album_art_colors = ""  # Loaded from settings below; persisted across restarts
        super().__init__()
        # Use proper app data directory for settings (not bundled app directory)
        self.app_data_dir = get_app_data_dir()
        self.settings_file = os.path.join(self.app_data_dir, 'settingsConfigure.json')
        # Keep backend_dir reference for accessing bundled resources
        self.backend_dir = os.path.dirname(os.path.abspath(__file__))
        
        self._default_settings = {
            "deviceName": "Default Device",
            "themeSetting": "CosmicVoyager",
            "fontSetting": "Google Sans",
            "startUpVolume": 0.1,
            "showClock": True,
            "clockFormat24Hour": True,
            "clockShowSeconds": False,
            "clockSize": 18,
            "backgroundGrid": "4x4",
            "screenWidth": 1280,
            "screenHeight": 720,
            "backgroundBlurRadius": 40,
            "uiScale": 1.0,
            "colorTransitionMs": 1000,
            "songLengthTransition": False,
            "obdBluetoothPort": "",
            "obdSavedAdapters": "[]",
            "obdFastMode": True,
            "obdAutoReconnectAttempts": 0,  # 0 = disabled, 1-10 = number of attempts
            "bottomBarOrientation": "bottom",
            "showBottomBarMediaControls": True,
            "fuelTankCapacity": 15.0,  # Add fuel tank capacity setting in gallons
            "obdParameters": {
                # Original default parameters (enabled by default)
                "COOLANT_TEMP": True,
                "CONTROL_MODULE_VOLTAGE": True,
                "ENGINE_LOAD": True,
                "THROTTLE_POS": True,
                "INTAKE_TEMP": True,
                "TIMING_ADVANCE": True,
                "MAF": True,
                "SPEED": True,
                "RPM": True,
                "COMMANDED_EQUIV_RATIO": True,
                "FUEL_LEVEL": True,
                "INTAKE_PRESSURE": True,
                "SHORT_FUEL_TRIM_1": True,
                "LONG_FUEL_TRIM_1": True,
                "O2_B1S1": True,
                "FUEL_PRESSURE": True,
                "OIL_TEMP": True,
                "FUEL_ECONOMY": True,
                "DISTANCE_TO_EMPTY": True,
                "IGNITION_TIMING": True,
                # Additional parameters (disabled by default, enable via scan or manually)
                "RUN_TIME": False,
                "DISTANCE_W_MIL": False,
                "FUEL_RAIL_PRESSURE_VAC": False,
                "FUEL_RAIL_PRESSURE_DIRECT": False,
                "BAROMETRIC_PRESSURE": False,
                "AMBIANT_AIR_TEMP": False,
                "RELATIVE_THROTTLE_POS": False,
                "THROTTLE_POS_B": False,
                "ACCELERATOR_POS_D": False,
                "CATALYST_TEMP_B1S1": False,
                "CATALYST_TEMP_B1S2": False,
                "EVAP_VAPOR_PRESSURE": False,
                "SHORT_FUEL_TRIM_2": False,
                "LONG_FUEL_TRIM_2": False,
                "O2_B1S2": False,
                "O2_B2S1": False,
                "O2_B2S2": False,
                "DISTANCE_SINCE_DTC_CLEAR": False,
                "WARMUPS_SINCE_DTC_CLEAR": False,
                "ABSOLUTE_LOAD": False,
                "COMMANDED_EGR": False,
                "EGR_ERROR": False,
                "ETHANOL_PERCENT": False,
                # Batch 2 - Additional O2 sensors
                "O2_B1S3": False,
                "O2_B1S4": False,
                "O2_B2S3": False,
                "O2_B2S4": False,
                # Wide-range O2 sensors voltage
                "O2_S1_WR_VOLTAGE": False,
                "O2_S2_WR_VOLTAGE": False,
                "O2_S3_WR_VOLTAGE": False,
                "O2_S4_WR_VOLTAGE": False,
                "O2_S5_WR_VOLTAGE": False,
                "O2_S6_WR_VOLTAGE": False,
                "O2_S7_WR_VOLTAGE": False,
                "O2_S8_WR_VOLTAGE": False,
                # Wide-range O2 sensors current
                "O2_S1_WR_CURRENT": False,
                "O2_S2_WR_CURRENT": False,
                "O2_S3_WR_CURRENT": False,
                "O2_S4_WR_CURRENT": False,
                "O2_S5_WR_CURRENT": False,
                "O2_S6_WR_CURRENT": False,
                "O2_S7_WR_CURRENT": False,
                "O2_S8_WR_CURRENT": False,
                # Bank 2 catalyst temps
                "CATALYST_TEMP_B2S1": False,
                "CATALYST_TEMP_B2S2": False,
                # Additional throttle/accelerator
                "THROTTLE_POS_C": False,
                "ACCELERATOR_POS_E": False,
                "ACCELERATOR_POS_F": False,
                "THROTTLE_ACTUATOR": False,
                # Fuel system
                "EVAPORATIVE_PURGE": False,
                "FUEL_RAIL_PRESSURE_ABS": False,
                "FUEL_INJECT_TIMING": False,
                "FUEL_RATE": False,
                # Time-based
                "RUN_TIME_MIL": False,
                "TIME_SINCE_DTC_CLEARED": False,
                # Other
                "MAX_MAF": False,
                "FUEL_TYPE": False,
                "EVAP_VAPOR_PRESSURE_ABS": False,
                "EVAP_VAPOR_PRESSURE_ALT": False,
                "SHORT_O2_TRIM_B1": False,
                "LONG_O2_TRIM_B1": False,
                "SHORT_O2_TRIM_B2": False,
                "LONG_O2_TRIM_B2": False,
                "RELATIVE_ACCEL_POS": False,
                "HYBRID_BATTERY_REMAINING": False,
                "ELM_VOLTAGE": False,
            },
            "homeOBDParameters": ["SPEED", "RPM", "COOLANT_TEMP", "CONTROL_MODULE_VOLTAGE"],
            "supportedOBDParameters": [],
            "lastSettingsSection": "deviceSettings",
            "spotifyClientId": "",
            "spotifyClientSecret": "",
            "mediaSource": "local",
            "mediaFolder": "",
            "customThemes": {},
            "autoPlayOnStartup": False,
            "persistShuffleState": False,
            "lastShuffleState": False,
            "lastPlayedSong": "",
            "lastPlayedPosition": 0,
            "lastPlayedPlaylist": "",
            "windowState": "windowed",
            "musicButtonDefaultPage": "mediaRoom",  # "mediaRoom" or "mediaPlayer"
            "returnToLibraryAfterSelection": False,  # If True, return to MediaPlayer after song selection
            "androidAutoEnabled": False,  # If True, show Android Auto button in bottom bar
            "phoneMirrorEnabled": False,  # If True, show Phone Mirror button in bottom bar
            "scrcpyAudioEnabled": False,  # If True, forward audio from phone
            "scrcpyDisplaySize": "1280x800",  # Virtual display WxH (--new-display); "" = phone screen
            "scrcpyAudioGain": 2.0,  # Linear gain on phone audio before OCTAVE's volume (+6 dB)
            # Settings menu section visibility (all visible by default, except advanced features)
            "settingsMenuVisibility": {
                "deviceSettings": True,
                "mediaSettings": True,
                "displaySettings": True,
                "obdSettings": True,
                "androidAutoSettings": True,
                "phoneMirrorSettings": True,
                "volumeKnobSettings": False,  # Hidden by default - enable via double-click menu
                "gestureSensorSettings": True,
                "about": True
            },
            # ESP32 Volume Knob settings
            "esp32VolumeEnabled": True,
            "esp32VolumePort": "COM7",
            "esp32VolumeStepSize": 1,  # 1% per encoder tick for fine control
            "esp32AutoReconnect": True,
            "esp32LedSleepEnabled": True,  # LEDs turn off when OCTAVE closes
            "esp32LedColorMode": "theme",  # "theme" (follows album art) or "static"
            "esp32LedStaticColor": "#00FFFF",  # Static color when not using theme
            # Waveform visualizer settings
            "showWaveformVisualizer": True,  # Show audio waveform visualization in MediaRoom
            # 3D visual effect settings
            "show3DButtonTilt": False,   # 3D tilt on prev/play/next press
            "show3DAlbumPreview": False,  # Coverflow-style album art card stack
            "roundedAlbumArt": True,     # Rounded corners on album art
            "albumArtCornerRadius": 16,  # Corner radius in dp (2-32)
            "showAlbumArtShadow": True,  # Drop shadow on album art
            "vinylRecordMode": False,    # Spin album art like a vinyl record
            "albumArtTransition": "Crossfade",  # any of: "Crossfade", "Slide", "Vinyl Lift", "Dissolve", "Flip", "Coverflow", "Conveyor", "Stack", "Depth", "Swing"
            "backgroundOverlayOpacity": 80,  # Dark overlay opacity percentage (0-100)
            "sideCardOpacity": 0.4,      # 3D preview side card opacity (0.1-1.0)
            "sideCardAngle": 30,         # 3D preview side card rotation angle (5-60)
            "buttonTiltDuration": 200,   # Button tilt animation duration in ms (50-500)
            "textScrollSpeed": 5000,     # Text scroll animation duration in ms (1000-10000)
            "albumArtColors": "",            # Last-extracted album art theme JSON (persisted for instant restore on startup)
            "settingsLayoutStyle": "Sidebar",  # Settings menu layout: "Carousel", "Sidebar", "Hub", "Dashboard"
            # IMU sensor settings
            "imuEnabled": True,
            "sensorEmitRate": 60,
            "sensorDecimalPlaces": 1,
            "sensorTempUnit": "F",
            "sensorAltitudeUnit": "m",
            "sensorShowPitch": True,
            "sensorShowRoll": True,
            "sensorShowHeading": True,
            "sensorShowGForce": True,
            "sensorShowAltitude": True,
            "sensorShowTemperature": True,
            # Gesture sensor settings
            "gestureSensorEnabled": True,
            "gestureMapping": {
                "RIGHT": "next_track",
                "LEFT": "previous_track",
                "UP": "volume_up",
                "DOWN": "volume_down",
                "FORWARD": "play_pause",
                "BACKWARD": "mute_toggle",
                "CLOCKWISE": "volume_up",
                "COUNTER-CLOCKWISE": "volume_down",
                "WAVE": "play_pause"
            },
            "gestureVolumeStep": 5,
            "gestureCooldown": 300,
            "pinnedSettings": [],
            # Music download settings
            "downloadSubfolder": "Downloads",  # Subfolder within mediaFolder
            "downloadFormat": "mp3",           # mp3, flac, ogg, opus, m4a
            "downloadBitrate": "auto",         # auto, 128k, 192k, 320k
            # Settings card expanded/collapsed state (default all collapsed)
            "expandedCards": {},
        }
            
    
        
        self._default_settings["directoryHistory"] = []
            
        # Load settings at startup
        self._settings = self.load_settings()

        # Migrate: old default uiScale 0.6 → new default 1.0
        if self._settings.get("uiScale") == 0.6:
            logger.info("Migrating uiScale from old default 0.6 to new default 1.0")
            self._settings["uiScale"] = 1.0
            self.save_settings(self._settings)

        # Initialize directory history
        self._directory_history = self._settings.get("directoryHistory", [])

        # Initialize existing settings
        self._device_name = self._settings.get("deviceName", self._default_settings["deviceName"])
        self._theme_setting = self._settings.get("themeSetting", self._default_settings["themeSetting"])
        self._font_setting = self._settings.get("fontSetting", self._default_settings["fontSetting"])
        self._start_volume = self._settings.get("startUpVolume", self._default_settings["startUpVolume"])
        self._show_clock = self._settings.get("showClock", self._default_settings["showClock"])
        self._clock_format_24hour = self._settings.get("clockFormat24Hour", self._default_settings["clockFormat24Hour"])
        self._clock_show_seconds = self._settings.get("clockShowSeconds", self._default_settings["clockShowSeconds"])
        self._clock_size = self._settings.get("clockSize", self._default_settings["clockSize"])
        self._background_grid = self._settings.get("backgroundGrid", self._default_settings["backgroundGrid"])
        self._screen_width = self._settings.get("screenWidth", self._default_settings["screenWidth"])
        self._screen_height = self._settings.get("screenHeight", self._default_settings["screenHeight"])
        self._background_blur_radius = self._settings.get("backgroundBlurRadius", self._default_settings["backgroundBlurRadius"])
        self._ui_scale = self._settings.get("uiScale", self._default_settings["uiScale"])
        self._color_transition_ms = self._settings.get("colorTransitionMs", self._default_settings["colorTransitionMs"])
        self._song_length_transition = self._settings.get("songLengthTransition", self._default_settings["songLengthTransition"])
        self._obd_bluetooth_port = self._settings.get("obdBluetoothPort", self._default_settings["obdBluetoothPort"])
        # Migrate the legacy "/dev/rfcomm0" placeholder — meaningless now, was leaking into the UI.
        if self._obd_bluetooth_port == "/dev/rfcomm0":
            self._obd_bluetooth_port = ""
        self._obd_saved_adapters = self._settings.get("obdSavedAdapters", self._default_settings["obdSavedAdapters"]) or "[]"
        self._obd_fast_mode = self._settings.get("obdFastMode", self._default_settings["obdFastMode"])
        self._obd_auto_reconnect_attempts = self._settings.get("obdAutoReconnectAttempts", self._default_settings["obdAutoReconnectAttempts"])
        default_music = os.path.expanduser("~/Music")
        self._media_folder = self._settings.get("mediaFolder", "") or default_music
        os.makedirs(self._media_folder, exist_ok=True)
        self._fuel_tank_capacity = self._settings.get("fuelTankCapacity", self._default_settings["fuelTankCapacity"])
        self._home_obd_parameters = self._settings.get("homeOBDParameters", self._default_settings["homeOBDParameters"])
        self._bottom_bar_orientation = self._settings.get("bottomBarOrientation", self._default_settings["bottomBarOrientation"])
        self._show_bottom_bar_media_controls = self._settings.get("showBottomBarMediaControls", self._default_settings["showBottomBarMediaControls"])

        # Spotify credentials (stored separately for security)
        self._spotify_client_id = self._settings.get("spotifyClientId", "")
        self._spotify_client_secret = self._settings.get("spotifyClientSecret", "")

        # Media source preference: "local" or "spotify"
        self._media_source = self._settings.get("mediaSource", "local")

        # Auto-play and resume settings
        self._auto_play_on_startup = self._settings.get("autoPlayOnStartup", False)
        self._persist_shuffle_state = self._settings.get("persistShuffleState", False)
        self._last_shuffle_state = self._settings.get("lastShuffleState", False)
        self._last_played_song = self._settings.get("lastPlayedSong", "")
        self._last_played_position = self._settings.get("lastPlayedPosition", 0)
        self._last_played_playlist = self._settings.get("lastPlayedPlaylist", "")

        # Window state: "windowed", "fullscreen", or "maximized"
        self._window_state = self._settings.get("windowState", "windowed")

        # Music button navigation settings
        self._music_button_default_page = self._settings.get("musicButtonDefaultPage", "mediaRoom")
        self._return_to_library_after_selection = self._settings.get("returnToLibraryAfterSelection", False)

        # Android Auto settings
        self._android_auto_enabled = self._settings.get("androidAutoEnabled", False)

        # Phone Mirror settings
        self._phone_mirror_enabled = self._settings.get("phoneMirrorEnabled", False)
        self._scrcpy_audio_enabled = self._settings.get("scrcpyAudioEnabled", False)
        self._scrcpy_display_size = self._settings.get("scrcpyDisplaySize", "1280x800")
        self._scrcpy_audio_gain = float(self._settings.get("scrcpyAudioGain", 2.0))

        # Settings menu visibility
        self._settings_menu_visibility = self._settings.get(
            "settingsMenuVisibility",
            self._default_settings["settingsMenuVisibility"]
        )
        # Ensure all sections exist (in case new sections were added)
        for section in self._default_settings["settingsMenuVisibility"]:
            if section not in self._settings_menu_visibility:
                self._settings_menu_visibility[section] = True

        # ESP32 Volume Knob settings
        self._esp32_volume_enabled = self._settings.get("esp32VolumeEnabled", self._default_settings["esp32VolumeEnabled"])
        self._esp32_volume_port = self._settings.get("esp32VolumePort", self._default_settings["esp32VolumePort"])
        self._esp32_volume_step_size = self._settings.get("esp32VolumeStepSize", self._default_settings["esp32VolumeStepSize"])
        self._esp32_auto_reconnect = self._settings.get("esp32AutoReconnect", self._default_settings["esp32AutoReconnect"])
        self._esp32_led_sleep_enabled = self._settings.get("esp32LedSleepEnabled", self._default_settings["esp32LedSleepEnabled"])
        self._esp32_led_color_mode = self._settings.get("esp32LedColorMode", self._default_settings["esp32LedColorMode"])
        self._esp32_led_static_color = self._settings.get("esp32LedStaticColor", self._default_settings["esp32LedStaticColor"])

        # Waveform visualizer settings
        self._show_waveform_visualizer = self._settings.get("showWaveformVisualizer", self._default_settings["showWaveformVisualizer"])

        # 3D visual effect settings
        self._show_3d_button_tilt = self._settings.get("show3DButtonTilt", self._default_settings["show3DButtonTilt"])
        self._show_3d_album_preview = self._settings.get("show3DAlbumPreview", self._default_settings["show3DAlbumPreview"])
        self._rounded_album_art = self._settings.get("roundedAlbumArt", self._default_settings["roundedAlbumArt"])
        self._album_art_corner_radius = self._settings.get("albumArtCornerRadius", self._default_settings["albumArtCornerRadius"])
        self._show_album_art_shadow = self._settings.get("showAlbumArtShadow", self._default_settings["showAlbumArtShadow"])
        self._vinyl_record_mode = self._settings.get("vinylRecordMode", self._default_settings["vinylRecordMode"])
        self._album_art_transition = self._settings.get("albumArtTransition", self._default_settings["albumArtTransition"])
        self._background_overlay_opacity = self._settings.get("backgroundOverlayOpacity", self._default_settings["backgroundOverlayOpacity"])
        self._side_card_opacity = self._settings.get("sideCardOpacity", self._default_settings["sideCardOpacity"])
        self._side_card_angle = self._settings.get("sideCardAngle", self._default_settings["sideCardAngle"])
        self._button_tilt_duration = self._settings.get("buttonTiltDuration", self._default_settings["buttonTiltDuration"])
        self._text_scroll_speed = self._settings.get("textScrollSpeed", self._default_settings["textScrollSpeed"])

        # Album art colors — last-extracted palette, restored on startup so the
        # placeholder Album Art theme doesn't flash before fresh extraction runs.
        self._album_art_colors = self._settings.get("albumArtColors", self._default_settings["albumArtColors"])

        # Settings layout style
        self._settings_layout_style = self._settings.get("settingsLayoutStyle", self._default_settings["settingsLayoutStyle"])

        # IMU sensor settings
        self._imu_enabled = self._settings.get("imuEnabled", self._default_settings["imuEnabled"])

        # Gesture sensor settings
        self._gesture_sensor_enabled = self._settings.get("gestureSensorEnabled", self._default_settings["gestureSensorEnabled"])
        self._gesture_mapping = self._settings.get("gestureMapping", self._default_settings["gestureMapping"])
        self._gesture_volume_step = self._settings.get("gestureVolumeStep", self._default_settings["gestureVolumeStep"])
        self._gesture_cooldown = self._settings.get("gestureCooldown", self._default_settings["gestureCooldown"])

        # Pinned settings
        self._pinned_settings = self._settings.get("pinnedSettings", [])

        # Current volume (0-100) - unified volume for both local and Spotify
        # Initialize from startUpVolume, converted to 0-100 scale
        self._current_volume = int(round((self._start_volume ** 0.5) * 100))

        # Last settings section visited (migrate removed clockSettings → displaySettings)
        self._last_settings_section = self._settings.get("lastSettingsSection", self._default_settings["lastSettingsSection"])
        if self._last_settings_section == "clockSettings":
            self._last_settings_section = "displaySettings"

        # Handle OBD parameters with a default if not present
        if "obdParameters" in self._settings:
            self._obd_parameters = self._settings["obdParameters"]

            # Add any missing parameters from defaults
            for param, value in self._default_settings["obdParameters"].items():
                if param not in self._obd_parameters:
                    self._obd_parameters[param] = value
        else:
            self._obd_parameters = self._default_settings["obdParameters"]

        # Debounce timer for OBD parameter changes - batches rapid toggles
        self._obd_params_dirty = False
        self._obd_params_save_timer = QTimer()
        self._obd_params_save_timer.setSingleShot(True)
        self._obd_params_save_timer.setInterval(800)  # 800ms debounce
        self._obd_params_save_timer.timeout.connect(self._flush_obd_parameters)

        # Load persisted vehicle scan results
        self._supported_obd_parameters = self._settings.get("supportedOBDParameters", [])

    def _lock_file(self, f, exclusive=True):
        """Acquire a lock on the file (cross-platform)"""
        if sys.platform == 'win32':
            # Windows file locking
            if exclusive:
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                msvcrt.locking(f.fileno(), msvcrt.LK_NBRLCK, 1)
        else:
            # Unix file locking
            if exclusive:
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            else:
                fcntl.flock(f.fileno(), fcntl.LOCK_SH | fcntl.LOCK_NB)

    def _unlock_file(self, f):
        """Release the lock on the file (cross-platform)"""
        if sys.platform == 'win32':
            try:
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
            except Exception:
                pass
        else:
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)

    def _set_file_permissions(self, filepath):
        """Set restrictive file permissions (owner read/write only)"""
        if sys.platform != 'win32':
            # Unix: chmod 600
            os.chmod(filepath, 0o600)
        # Windows: permissions are handled differently via ACLs
        # The file is already protected by user account on Windows

    def _validate_settings(self, settings):
        """Validate settings structure and types, return sanitized settings"""
        if not isinstance(settings, dict):
            return self._default_settings.copy()

        validated = self._default_settings.copy()

        for key, default_val in self._default_settings.items():
            if key not in settings:
                continue

            val = settings[key]
            default_type = type(default_val)

            # bool must be checked before int (bool is subclass of int)
            if default_type is bool:
                if isinstance(val, bool):
                    validated[key] = val
            elif default_type is int:
                if isinstance(val, bool):
                    pass  # reject bool for int fields
                elif isinstance(val, int):
                    validated[key] = val
                elif isinstance(val, float):
                    validated[key] = int(val)
            elif default_type is float:
                if isinstance(val, bool):
                    pass  # reject bool for float fields
                elif isinstance(val, (int, float)):
                    validated[key] = float(val)
            elif isinstance(val, default_type):
                validated[key] = val

        # Copy any extra keys not in defaults (for forward compatibility)
        for key in settings:
            if key not in self._default_settings:
                validated[key] = settings[key]

        return validated

    def load_settings(self):
        try:
            with open(self.settings_file, 'r') as f:
                try:
                    self._lock_file(f, exclusive=False)
                    settings = json.load(f)
                    return self._validate_settings(settings)
                except (BlockingIOError, OSError):
                    # Could not acquire lock, read anyway but log warning
                    logger.warning("Could not acquire file lock for reading settings")
                    f.seek(0)
                    settings = json.load(f)
                    return self._validate_settings(settings)
                finally:
                    try:
                        self._unlock_file(f)
                    except Exception:
                        pass
        except FileNotFoundError:
            self.save_settings(self._default_settings)
            return self._default_settings.copy()
        except json.JSONDecodeError as e:
            logger.error(f"Settings file corrupted ({e}), using defaults")
            return self._default_settings.copy()

    def save_settings(self, settings):
        """Save settings atomically with file locking"""
        # Validate before saving
        validated_settings = self._validate_settings(settings)

        # Write to temp file first, then rename (atomic on most systems)
        dir_name = os.path.dirname(self.settings_file)
        try:
            # Create temp file in same directory for atomic rename
            fd, temp_path = tempfile.mkstemp(dir=dir_name, suffix='.json')
            try:
                with os.fdopen(fd, 'w') as f:
                    try:
                        self._lock_file(f, exclusive=True)
                    except (BlockingIOError, OSError):
                        logger.warning("Could not acquire file lock for writing settings")
                    json.dump(validated_settings, f, indent=4)

                # Set permissions before rename
                self._set_file_permissions(temp_path)

                # Atomic rename (on Unix) or replace (on Windows)
                if sys.platform == 'win32':
                    # Windows doesn't support atomic rename over existing file
                    if os.path.exists(self.settings_file):
                        os.remove(self.settings_file)
                os.rename(temp_path, self.settings_file)

            except Exception as e:
                # Clean up temp file on error
                if os.path.exists(temp_path):
                    os.remove(temp_path)
                raise e

        except Exception as e:
            logger.error(f"Error saving settings: {e}")
            # Fallback to direct write if atomic fails
            with open(self.settings_file, 'w') as f:
                json.dump(validated_settings, f, indent=4)
            self._set_file_permissions(self.settings_file)

    def update_setting(self, key, value, signal=None):
        settings = self.load_settings()
        settings[key] = value
        self.save_settings(settings)
        if signal:
            signal.emit(value)

    @Property(float, notify=uiScaleChanged)
    def uiScale(self):
        return self._ui_scale

    @Property(int, notify=colorTransitionMsChanged)
    def colorTransitionMs(self):
        return self._color_transition_ms

    @Property(bool, notify=songLengthTransitionChanged)
    def songLengthTransition(self):
        return self._song_length_transition

    @Property(str, notify=bottomBarOrientationChanged)
    def bottomBarOrientation(self):
        return self._bottom_bar_orientation

    @Property(bool, notify=showBottomBarMediaControlsChanged)
    def showBottomBarMediaControls(self):
        return self._show_bottom_bar_media_controls

    @Property(int, notify=backgroundBlurRadiusChanged)
    def backgroundBlurRadius(self):
        return self._background_blur_radius
    
    @Property(str, notify=deviceNameChanged)
    def deviceName(self):
        return self._device_name
    
    @Property(str, notify=themeSettingChanged)
    def themeSetting(self):
        return self._theme_setting

    @Property(str, notify=fontSettingChanged)
    def fontSetting(self):
        return self._font_setting

    @Property(float, notify=startUpVolumeChanged)
    def startUpVolume(self):
        return self._start_volume

    @Property(int, notify=currentVolumeChanged)
    def currentVolume(self):
        return self._current_volume

    @Slot(int)
    def setCurrentVolume(self, volume):
        """Set the unified volume (0-100) for both local and Spotify"""
        if self._current_volume != volume:
            self._current_volume = volume
            self.currentVolumeChanged.emit(volume)

    @Property(bool, notify=showClockChanged)
    def showClock(self):
        return self._show_clock
    
    @Property(bool, notify=clockFormatChanged)
    def clockFormat24Hour(self):
        return self._clock_format_24hour

    @Property(bool, notify=clockShowSecondsChanged)
    def clockShowSeconds(self):
        return self._clock_show_seconds

    @Property(int, notify=clockSizeChanged)
    def clockSize(self):
        return self._clock_size
    
    @Property(str, notify=backgroundGridChanged)
    def backgroundGrid(self):
        return self._background_grid
    
    @Property(int, notify=screenWidthChanged)
    def screenWidth(self):
        return self._screen_width

    @Property(int, notify=screenHeightChanged)
    def screenHeight(self):
        return self._screen_height
    
    @Property(str, notify=obdBluetoothPortChanged)
    def obdBluetoothPort(self):
        return self._obd_bluetooth_port

    @Property(str, notify=obdSavedAdaptersChanged)
    def obdSavedAdapters(self):
        return self._obd_saved_adapters

    @Property(bool, notify=obdFastModeChanged)
    def obdFastMode(self):
        return self._obd_fast_mode

    @Property(int, notify=obdAutoReconnectAttemptsChanged)
    def obdAutoReconnectAttempts(self):
        return self._obd_auto_reconnect_attempts

    @Property(str, notify=mediaFolderChanged)
    def mediaFolder(self):
        return self._media_folder
    
    @Property('QVariantList', notify=directoryHistoryChanged)
    def directoryHistory(self):
        return self._directory_history
    
    @Property('QVariantList', notify=homeOBDParametersChanged)
    def homeOBDParameters(self):
        return self._home_obd_parameters
    
    @Property('QVariantList', notify=customThemesChanged)
    def customThemes(self):
        """Return list of custom theme names"""
        settings = self.load_settings()
        if "customThemes" in settings:
            return list(settings["customThemes"].keys())
        return []

    # Existing save methods
    @Slot(float)
    def save_ui_scale(self, scale):
        logger.debug(f"Saving UI scale: {scale}")
        self._ui_scale = scale
        self.update_setting("uiScale", scale, self.uiScaleChanged)

    @Slot(int)
    def save_color_transition_ms(self, ms):
        logger.debug(f"Saving color transition ms: {ms}")
        self._color_transition_ms = ms
        self.update_setting("colorTransitionMs", ms, self.colorTransitionMsChanged)

    @Slot(bool)
    def save_song_length_transition(self, enabled):
        logger.debug(f"Saving song length transition: {enabled}")
        self._song_length_transition = enabled
        self.update_setting("songLengthTransition", enabled, self.songLengthTransitionChanged)

    @Slot(int)
    def save_background_blur_radius(self, radius):
        logger.debug(f"Saving background blur radius: {radius}")
        self._background_blur_radius = radius
        self.update_setting("backgroundBlurRadius", radius, self.backgroundBlurRadiusChanged)
    
    @Slot(str)
    def save_device_name(self, name):
        logger.debug(f"Saving device name: {name}")
        self._device_name = name
        self.update_setting("deviceName", name, self.deviceNameChanged)
        
    @Slot(str)
    def save_theme_setting(self, theme):
        logger.debug(f"Saving theme setting: {theme}")
        self._theme_setting = theme
        self.update_setting("themeSetting", theme, self.themeSettingChanged)

    @Slot(str)
    def save_font_setting(self, font):
        logger.debug(f"Saving font setting: {font}")
        self._font_setting = font
        self.update_setting("fontSetting", font, self.fontSettingChanged)
        
    @Slot(float)
    def save_start_volume(self, volume):
        logger.debug(f"Saving volume setting: {volume}")
        self._start_volume = volume
        self.update_setting("startUpVolume", volume, self.startUpVolumeChanged)
        
    @Slot(bool)
    def save_show_clock(self, show):
        logger.debug(f"Saving show clock setting: {show}")
        self._show_clock = show
        self.update_setting("showClock", show, self.showClockChanged)

    @Slot(bool)
    def save_clock_format(self, is_24hour):
        logger.debug(f"Saving clock format setting: {is_24hour}")
        self._clock_format_24hour = is_24hour
        self.update_setting("clockFormat24Hour", is_24hour, self.clockFormatChanged)

    @Slot(bool)
    def save_clock_show_seconds(self, show):
        logger.debug(f"Saving clock show seconds setting: {show}")
        self._clock_show_seconds = show
        self.update_setting("clockShowSeconds", show, self.clockShowSecondsChanged)

    @Slot(int)
    def save_clock_size(self, size):
        logger.debug(f"Saving clock size setting: {size}")
        self._clock_size = size
        self.update_setting("clockSize", size, self.clockSizeChanged)
        
    @Slot(str)
    def save_background_grid(self, grid_setting):
        logger.debug(f"Saving background grid setting: {grid_setting}")
        self._background_grid = grid_setting
        self.update_setting("backgroundGrid", grid_setting, self.backgroundGridChanged)
        
        
    @Slot(int)
    def save_screen_width(self, width):
        self._screen_width = width
        self.update_setting("screenWidth", width, self.screenWidthChanged)
        
    @Slot(int)
    def save_screen_height(self, height):
        self._screen_height = height
        self.update_setting("screenHeight", height, self.screenHeightChanged)
    
    # New OBD save methods
    @Slot(str)
    def save_obd_bluetooth_port(self, port):
        logger.debug(f"Saving OBD Bluetooth port: {port}")
        self._obd_bluetooth_port = port
        self.update_setting("obdBluetoothPort", port, self.obdBluetoothPortChanged)

    @Slot(str)
    def save_obd_saved_adapters(self, raw):
        import json as _json
        try:
            parsed = _json.loads(raw) if raw else []
            if not isinstance(parsed, list):
                parsed = []
        except (ValueError, TypeError):
            parsed = []
        self._obd_saved_adapters = _json.dumps(parsed, separators=(",", ":"))
        self.update_setting("obdSavedAdapters", self._obd_saved_adapters, self.obdSavedAdaptersChanged)

    @Slot(str, str)
    def add_obd_saved_adapter(self, name, mac):
        import json as _json
        trimmed = (mac or "").strip().upper()
        if not trimmed:
            return
        try:
            arr = _json.loads(self._obd_saved_adapters) if self._obd_saved_adapters else []
            if not isinstance(arr, list):
                arr = []
        except (ValueError, TypeError):
            arr = []
        # Drop any existing entry for this MAC, then prepend so most-recent is first.
        arr = [e for e in arr if isinstance(e, dict) and (e.get("mac") or "").upper() != trimmed]
        arr.insert(0, {"name": name or trimmed, "mac": trimmed})
        # Cap at 8 entries.
        del arr[8:]
        self.save_obd_saved_adapters(_json.dumps(arr, separators=(",", ":")))

    @Slot(str)
    def remove_obd_saved_adapter(self, mac):
        import json as _json
        trimmed = (mac or "").strip().upper()
        if not trimmed:
            return
        try:
            arr = _json.loads(self._obd_saved_adapters) if self._obd_saved_adapters else []
            if not isinstance(arr, list):
                arr = []
        except (ValueError, TypeError):
            arr = []
        arr = [e for e in arr if isinstance(e, dict) and (e.get("mac") or "").upper() != trimmed]
        self.save_obd_saved_adapters(_json.dumps(arr, separators=(",", ":")))
    
    @Slot(bool)
    def save_obd_fast_mode(self, enabled):
        logger.debug(f"Saving OBD fast mode: {enabled}")
        self._obd_fast_mode = enabled
        self.update_setting("obdFastMode", enabled, self.obdFastModeChanged)

    @Slot(int)
    def save_obd_auto_reconnect_attempts(self, attempts):
        """Save OBD auto-reconnect attempts (0 = disabled, 1-10 = number of attempts)"""
        logger.debug(f"Saving OBD auto-reconnect attempts: {attempts}")
        self._obd_auto_reconnect_attempts = max(0, min(10, attempts))  # Clamp to 0-10
        self.update_setting("obdAutoReconnectAttempts", self._obd_auto_reconnect_attempts, self.obdAutoReconnectAttemptsChanged)

    @Slot(str, bool)
    def save_obd_parameter_enabled(self, parameter, enabled):
        """Save OBD parameter with debouncing to prevent lag from rapid toggles"""
        # Update internal state immediately (for responsive UI)
        self._obd_parameters[parameter] = enabled

        # Mark as dirty and restart debounce timer
        self._obd_params_dirty = True
        self._obd_params_save_timer.start()

    def _flush_obd_parameters(self):
        """Actually save OBD parameters to disk and emit signal (called after debounce)"""
        if not self._obd_params_dirty:
            return

        logger.debug("Flushing OBD parameter changes to disk")

        # Load current settings
        settings = self.load_settings()

        # Make sure obdParameters exists
        if "obdParameters" not in settings:
            settings["obdParameters"] = {}

        # Update all parameters
        settings["obdParameters"] = self._obd_parameters.copy()

        # Save to disk
        self.save_settings(settings)

        # Clear dirty flag
        self._obd_params_dirty = False

        # Emit signal once for all batched changes
        self.obdParametersChanged.emit()
        
    @Slot(str, bool, result=bool)
    def get_obd_parameter_enabled(self, parameter, default=True):
        """Get OBD parameter enabled state from memory (no disk I/O)"""
        if parameter in self._obd_parameters:
            return self._obd_parameters[parameter]

        # Parameter not in memory, use default and cache it
        self._obd_parameters[parameter] = default
        return default

    @Slot('QVariantList')
    def save_supported_obd_parameters(self, parameters):
        """Save vehicle-supported OBD parameters from scan results"""
        self._supported_obd_parameters = list(parameters)
        settings = self.load_settings()
        settings["supportedOBDParameters"] = list(parameters)
        self.save_settings(settings)
        self.supportedOBDParametersChanged.emit()

    @Slot(result='QVariantList')
    def get_supported_obd_parameters(self):
        """Return vehicle-supported OBD parameters from last scan"""
        return self._supported_obd_parameters

    @Slot(str)
    def save_media_folder(self, folder_path):
        # Skip if the folder hasn't actually changed (prevents double-scan)
        if folder_path == self._media_folder:
            logger.debug(f"Media folder unchanged, skipping: {folder_path}")
            return
        logger.debug(f"Saving media folder path: {folder_path}")
        self._media_folder = folder_path
        self.update_setting("mediaFolder", folder_path, self.mediaFolderChanged)
        
    # Add new signal for fuel tank capacity
    fuelTankCapacityChanged = Signal(float)

    # Add property for fuel tank capacity
    @Property(float, notify=fuelTankCapacityChanged)
    def fuelTankCapacity(self):
        return self._fuel_tank_capacity

    # Add a method to save fuel tank capacity
    @Slot(float)
    def save_fuel_tank_capacity(self, capacity):
        logger.debug(f"Saving fuel tank capacity: {capacity}")
        self._fuel_tank_capacity = capacity
        self.update_setting("fuelTankCapacity", capacity, self.fuelTankCapacityChanged)
        
    @Slot(str)
    def save_to_directory_history(self, folder_path):
        logger.debug(f"Adding directory to history: {folder_path}")
        # Don't add duplicates
        if folder_path not in self._directory_history:
            # Add to the beginning of the list
            self._directory_history.insert(0, folder_path)
            
            # Limit the number of saved directories to 10
            if len(self._directory_history) > 10:
                self._directory_history = self._directory_history[:10]
                
            # Save to settings
            settings = self.load_settings()
            settings["directoryHistory"] = self._directory_history
            self.save_settings(settings)
            
            # Emit signal
            self.directoryHistoryChanged.emit()

    @Slot(str)
    def remove_from_directory_history(self, folder_path):
        logger.debug(f"Removing directory from history: {folder_path}")
        if folder_path in self._directory_history:
            self._directory_history.remove(folder_path)
            
            # Save to settings
            settings = self.load_settings()
            settings["directoryHistory"] = self._directory_history
            self.save_settings(settings)
            
            # Emit signal
            self.directoryHistoryChanged.emit()

    @Slot(result='QVariantList')
    def get_directory_history(self):
        return self._directory_history
    
    @Slot('QVariantList')
    def save_home_obd_parameters(self, parameters):
        """Save the list of OBD parameters to display on home screen"""
        logger.debug(f"Saving home OBD parameters: {parameters}")
        self._home_obd_parameters = list(parameters)  # Make a copy

        # Update the settings file
        settings = self.load_settings()
        settings["homeOBDParameters"] = list(parameters)
        self.save_settings(settings)

        # Emit signal to notify UI
        self.homeOBDParametersChanged.emit()
        
    @Slot(str)
    def save_bottom_bar_orientation(self, orientation):
        logger.debug(f"Saving bottom bar orientation: {orientation}")
        self._bottom_bar_orientation = orientation
        self.update_setting("bottomBarOrientation", orientation, self.bottomBarOrientationChanged)

    @Slot(bool)
    def save_show_bottom_bar_media_controls(self, show):
        logger.debug(f"Saving show bottom bar media controls: {show}")
        self._show_bottom_bar_media_controls = show
        self.update_setting("showBottomBarMediaControls", show, self.showBottomBarMediaControlsChanged)

    @Slot(result='QVariantList')
    def get_home_obd_parameters(self):
        """Return the list of OBD parameters to display on home screen"""
        return self._home_obd_parameters
    
    @Slot(str, result=str)
    def get_custom_theme(self, name):
        """Get a custom theme by name as JSON string"""
        settings = self.load_settings()
        if "customThemes" in settings and name in settings["customThemes"]:
            return json.dumps(settings["customThemes"][name])
        return "{}"

    @Slot(str, str, str)
    def save_custom_theme(self, name, theme_json, album_art_source=""):
        """Save a custom theme from album art colors, optionally with album art image"""
        logger.debug(f"Saving custom theme: {name}")
        settings = self.load_settings()
        if "customThemes" not in settings:
            settings["customThemes"] = {}
        theme = json.loads(theme_json)

        art_dir = os.path.join(self.app_data_dir, "theme_art")
        safe_name = re.sub(r'[^\w\-]', '_', name)

        # Handle local files synchronously (fast, no network)
        if album_art_source and not album_art_source.startswith("http://") and not album_art_source.startswith("https://"):
            try:
                os.makedirs(art_dir, exist_ok=True)
                local_path = QUrl(album_art_source).toLocalFile() if album_art_source.startswith("file://") else album_art_source
                if local_path and os.path.isfile(local_path):
                    ext = os.path.splitext(local_path)[1] or ".jpg"
                    dest = os.path.join(art_dir, f"theme_{safe_name}{ext}")
                    shutil.copy2(local_path, dest)
                    theme["albumArt"] = QUrl.fromLocalFile(dest).toString()
                    logger.debug(f"Saved theme art to: {dest}")
            except Exception as e:
                logger.warning(f"Failed to save theme album art: {e}")

        # Save theme immediately (colors are ready, art downloads in background)
        settings["customThemes"][name] = theme
        self.save_settings(settings)
        self.customThemesChanged.emit()

        # Download remote album art in background thread (e.g. Spotify URLs)
        if album_art_source and (album_art_source.startswith("http://") or album_art_source.startswith("https://")):
            self._download_theme_art_async(name, album_art_source, art_dir, safe_name)

    def _download_theme_art_async(self, theme_name, url, art_dir, safe_name):
        """Download remote album art in a background thread, then update the saved theme."""
        import threading

        def do_download():
            try:
                import urllib.request
                os.makedirs(art_dir, exist_ok=True)
                dest = os.path.join(art_dir, f"theme_{safe_name}.jpg")
                urllib.request.urlretrieve(url, dest)
                local_url = QUrl.fromLocalFile(dest).toString()
                logger.debug(f"Downloaded theme art to: {dest}")

                # Store result for main-thread pickup
                self._pending_theme_art = (theme_name, local_url)
            except Exception as e:
                logger.warning(f"Failed to download theme album art: {e}")

        self._pending_theme_art = None
        threading.Thread(target=do_download, daemon=True).start()

        # Poll for completion on main thread (Windows-safe pattern)
        timer = QTimer(self)
        timer.setInterval(100)
        def check_done():
            if self._pending_theme_art is not None:
                timer.stop()
                timer.deleteLater()
                name, local_url = self._pending_theme_art
                self._pending_theme_art = None
                settings = self.load_settings()
                if "customThemes" in settings and name in settings["customThemes"]:
                    settings["customThemes"][name]["albumArt"] = local_url
                    self.save_settings(settings)
                    self.customThemesChanged.emit()
        timer.timeout.connect(check_done)
        timer.start()

    @Slot(str)
    def delete_custom_theme(self, name):
        """Delete a custom theme by name, including its album art file"""
        logger.debug(f"Deleting custom theme: {name}")
        settings = self.load_settings()
        if "customThemes" in settings and name in settings["customThemes"]:
            # Clean up album art file
            theme = settings["customThemes"][name]
            if "albumArt" in theme:
                try:
                    art_path = QUrl(theme["albumArt"]).toLocalFile()
                    if art_path and os.path.isfile(art_path):
                        os.remove(art_path)
                        logger.debug(f"Removed theme art: {art_path}")
                except Exception as e:
                    logger.warning(f"Failed to remove theme art: {e}")
            del settings["customThemes"][name]
            self.save_settings(settings)
            self.customThemesChanged.emit()

    # ==================== Spotify Credentials ====================

    @Slot(result=str)
    def get_spotify_client_id(self):
        """Get Spotify client ID"""
        return self._spotify_client_id

    @Slot(result=str)
    def get_spotify_client_secret(self):
        """Get Spotify client secret"""
        return self._spotify_client_secret

    @Slot(result=bool)
    def has_spotify_credentials(self):
        """Check if Spotify credentials are configured"""
        return bool(self._spotify_client_id and self._spotify_client_secret)

    @Slot(str, str)
    def save_spotify_credentials(self, client_id, client_secret):
        """Save Spotify API credentials"""
        logger.debug(f"Saving Spotify credentials")
        self._spotify_client_id = client_id
        self._spotify_client_secret = client_secret

        settings = self.load_settings()
        settings["spotifyClientId"] = client_id
        settings["spotifyClientSecret"] = client_secret
        self.save_settings(settings)

        self.spotifyCredentialsChanged.emit()

    @Slot()
    def clear_spotify_credentials(self):
        """Clear saved Spotify credentials"""
        self._spotify_client_id = ""
        self._spotify_client_secret = ""

        settings = self.load_settings()
        settings["spotifyClientId"] = ""
        settings["spotifyClientSecret"] = ""
        self.save_settings(settings)

        self.spotifyCredentialsChanged.emit()

    # ==================== Media Source ====================

    @Property(str, notify=mediaSourceChanged)
    def mediaSource(self):
        """Get current media source preference: 'local' or 'spotify'"""
        return self._media_source

    @Slot(result=str)
    def get_media_source(self):
        """Get current media source preference"""
        return self._media_source

    @Slot(str)
    def set_media_source(self, source):
        """Set media source preference: 'local' or 'spotify'"""
        if source not in ("local", "spotify"):
            return

        if source != self._media_source:
            self._media_source = source
            settings = self.load_settings()
            settings["mediaSource"] = source
            self.save_settings(settings)
            self.mediaSourceChanged.emit(source)

    @Slot()
    def toggle_media_source(self):
        """Toggle between local and spotify"""
        new_source = "spotify" if self._media_source == "local" else "local"
        self.set_media_source(new_source)

    # ==================== Auto Play & Resume Settings ====================

    @Property(bool, notify=autoPlayOnStartupChanged)
    def autoPlayOnStartup(self):
        """Get auto-play on startup setting"""
        return self._auto_play_on_startup

    @Slot(result=bool)
    def get_auto_play_on_startup(self):
        """Get auto-play on startup setting"""
        return self._auto_play_on_startup

    @Slot(bool)
    def save_auto_play_on_startup(self, enabled):
        """Save auto-play on startup setting"""
        logger.debug(f"Saving auto-play on startup: {enabled}")
        self._auto_play_on_startup = enabled
        self.update_setting("autoPlayOnStartup", enabled, self.autoPlayOnStartupChanged)

    @Property(bool, notify=persistShuffleStateChanged)
    def persistShuffleState(self):
        return self._persist_shuffle_state

    @Slot(result=bool)
    def get_persist_shuffle_state(self):
        return self._persist_shuffle_state

    @Slot(bool)
    def save_persist_shuffle_state(self, enabled):
        logger.debug(f"Saving persist shuffle state: {enabled}")
        self._persist_shuffle_state = enabled
        self.update_setting("persistShuffleState", enabled, self.persistShuffleStateChanged)

    @Property(bool, notify=lastShuffleStateChanged)
    def lastShuffleState(self):
        return self._last_shuffle_state

    @Slot(result=bool)
    def get_last_shuffle_state(self):
        return self._last_shuffle_state

    @Slot(bool)
    def save_last_shuffle_state(self, enabled):
        self._last_shuffle_state = enabled
        self.update_setting("lastShuffleState", enabled, self.lastShuffleStateChanged)

    @Property(str, notify=lastPlayedSongChanged)
    def lastPlayedSong(self):
        """Get last played song filename"""
        return self._last_played_song

    @Slot(result=str)
    def get_last_played_song(self):
        """Get last played song filename"""
        return self._last_played_song

    @Slot(str)
    def save_last_played_song(self, filename):
        """Save last played song filename"""
        self._last_played_song = filename
        self.update_setting("lastPlayedSong", filename, self.lastPlayedSongChanged)

    @Property(int, notify=lastPlayedPositionChanged)
    def lastPlayedPosition(self):
        """Get last played position in milliseconds"""
        return self._last_played_position

    @Slot(result=int)
    def get_last_played_position(self):
        """Get last played position in milliseconds"""
        return self._last_played_position

    @Slot(int)
    def save_last_played_position(self, position_ms):
        """Save last played position in milliseconds"""
        self._last_played_position = position_ms
        self.update_setting("lastPlayedPosition", position_ms, self.lastPlayedPositionChanged)

    @Property(str, notify=lastPlayedPlaylistChanged)
    def lastPlayedPlaylist(self):
        """Get last played playlist name"""
        return self._last_played_playlist

    @Slot(result=str)
    def get_last_played_playlist(self):
        """Get last played playlist name"""
        return self._last_played_playlist

    @Slot(str)
    def save_last_played_playlist(self, playlist_name):
        """Save last played playlist name"""
        self._last_played_playlist = playlist_name
        self.update_setting("lastPlayedPlaylist", playlist_name, self.lastPlayedPlaylistChanged)

    @Slot(str, int, str)
    def save_playback_state(self, song, position_ms, playlist):
        """Save all playback state at once (used when pausing/stopping)"""
        self._last_played_song = song
        self._last_played_position = position_ms
        self._last_played_playlist = playlist

        settings = self.load_settings()
        settings["lastPlayedSong"] = song
        settings["lastPlayedPosition"] = position_ms
        settings["lastPlayedPlaylist"] = playlist
        self.save_settings(settings)

    # ==================== Last Settings Section ====================

    @Property(str, notify=lastSettingsSectionChanged)
    def lastSettingsSection(self):
        """Get last visited settings section"""
        return self._last_settings_section

    @Slot(result=str)
    def get_last_settings_section(self):
        """Get last visited settings section"""
        return self._last_settings_section

    @Slot(str)
    def set_last_settings_section(self, section):
        """Set last visited settings section"""
        valid_sections = ["deviceSettings", "mediaSettings", "displaySettings", "obdSettings", "androidAutoSettings", "phoneMirrorSettings", "volumeKnobSettings", "gestureSensorSettings", "about"]
        if section not in valid_sections:
            return

        if section != self._last_settings_section:
            self._last_settings_section = section
            settings = self.load_settings()
            settings["lastSettingsSection"] = section
            self.save_settings(settings)
            self.lastSettingsSectionChanged.emit(section)

    # ==================== Window State ====================

    @Property(str, notify=windowStateChanged)
    def windowState(self):
        """Get window state: 'windowed', 'fullscreen', or 'maximized'"""
        return self._window_state

    @Slot(result=str)
    def get_window_state(self):
        """Get window state"""
        return self._window_state

    @Slot(str)
    def save_window_state(self, state):
        """Save window state: 'windowed', 'fullscreen', or 'maximized'"""
        valid_states = ["windowed", "fullscreen", "maximized"]
        if state not in valid_states:
            return

        self._window_state = state
        self.update_setting("windowState", state, self.windowStateChanged)

    # ==================== Music Button Navigation Settings ====================

    @Property(str, notify=musicButtonDefaultPageChanged)
    def musicButtonDefaultPage(self):
        """Get music button default page: 'mediaRoom' or 'mediaPlayer'"""
        return self._music_button_default_page

    @Slot(result=str)
    def get_music_button_default_page(self):
        """Get music button default page"""
        return self._music_button_default_page

    @Slot(str)
    def save_music_button_default_page(self, page):
        """Save music button default page: 'mediaRoom' or 'mediaPlayer'"""
        valid_pages = ["mediaRoom", "mediaPlayer"]
        if page not in valid_pages:
            return

        logger.debug(f"Saving music button default page: {page}")
        self._music_button_default_page = page
        self.update_setting("musicButtonDefaultPage", page, self.musicButtonDefaultPageChanged)

    @Property(bool, notify=returnToLibraryAfterSelectionChanged)
    def returnToLibraryAfterSelection(self):
        """Get whether to return to library after song selection"""
        return self._return_to_library_after_selection

    @Slot(result=bool)
    def get_return_to_library_after_selection(self):
        """Get whether to return to library after song selection"""
        return self._return_to_library_after_selection

    @Slot(bool)
    def save_return_to_library_after_selection(self, return_to_library):
        """Save whether to return to library after song selection"""
        logger.debug(f"Saving return to library after selection: {return_to_library}")
        self._return_to_library_after_selection = return_to_library
        self.update_setting("returnToLibraryAfterSelection", return_to_library, self.returnToLibraryAfterSelectionChanged)

    # ==================== Android Auto Settings ====================

    @Property(bool, notify=androidAutoEnabledChanged)
    def androidAutoEnabled(self):
        """Get whether Android Auto is enabled"""
        return self._android_auto_enabled

    @Slot(result=bool)
    def get_android_auto_enabled(self):
        """Get whether Android Auto is enabled"""
        return self._android_auto_enabled

    @Slot(bool)
    def save_android_auto_enabled(self, enabled):
        """Save whether Android Auto is enabled"""
        logger.debug(f"Saving Android Auto enabled: {enabled}")
        self._android_auto_enabled = enabled
        self.update_setting("androidAutoEnabled", enabled, self.androidAutoEnabledChanged)

    # ==================== Phone Mirror Settings ====================

    @Property(bool, notify=phoneMirrorEnabledChanged)
    def phoneMirrorEnabled(self):
        """Get whether Phone Mirror is enabled"""
        return self._phone_mirror_enabled

    @Slot(result=bool)
    def get_phone_mirror_enabled(self):
        """Get whether Phone Mirror is enabled"""
        return self._phone_mirror_enabled

    @Slot(bool)
    def save_phone_mirror_enabled(self, enabled):
        """Save whether Phone Mirror is enabled"""
        logger.debug(f"Saving Phone Mirror enabled: {enabled}")
        self._phone_mirror_enabled = enabled
        self.update_setting("phoneMirrorEnabled", enabled, self.phoneMirrorEnabledChanged)

    @Property(bool, notify=scrcpyAudioEnabledChanged)
    def scrcpyAudioEnabled(self):
        """Get whether scrcpy audio forwarding is enabled"""
        return self._scrcpy_audio_enabled

    @Slot(result=bool)
    def get_scrcpy_audio_enabled(self):
        """Get whether scrcpy audio forwarding is enabled"""
        return self._scrcpy_audio_enabled

    @Slot(bool)
    def save_scrcpy_audio_enabled(self, enabled):
        """Save whether scrcpy audio forwarding is enabled"""
        logger.debug(f"Saving scrcpy audio enabled: {enabled}")
        self._scrcpy_audio_enabled = enabled
        self.update_setting("scrcpyAudioEnabled", enabled, self.scrcpyAudioEnabledChanged)

    @Property(str, notify=scrcpyDisplaySizeChanged)
    def scrcpyDisplaySize(self):
        """Get the scrcpy virtual display size ("WxH", "" = mirror phone screen)"""
        return self._scrcpy_display_size

    @Slot(result=str)
    def get_scrcpy_display_size(self):
        """Get the scrcpy virtual display size ("WxH", "" = mirror phone screen)"""
        return self._scrcpy_display_size

    @Slot(str)
    def save_scrcpy_display_size(self, size):
        """Save the scrcpy virtual display size"""
        logger.debug(f"Saving scrcpy display size: {size}")
        self._scrcpy_display_size = size
        self.update_setting("scrcpyDisplaySize", size, self.scrcpyDisplaySizeChanged)

    @Property(float, notify=scrcpyAudioGainChanged)
    def scrcpyAudioGain(self):
        """Linear gain applied to phone audio before OCTAVE's volume"""
        return self._scrcpy_audio_gain

    @Slot(result=float)
    def get_scrcpy_audio_gain(self):
        return self._scrcpy_audio_gain

    @Slot(float)
    def save_scrcpy_audio_gain(self, gain):
        gain = max(0.25, min(8.0, float(gain)))
        logger.debug(f"Saving scrcpy audio gain: {gain}")
        self._scrcpy_audio_gain = gain
        self.update_setting("scrcpyAudioGain", gain, self.scrcpyAudioGainChanged)

    @Property(bool, notify=esp32VolumeEnabledChanged)
    def esp32VolumeEnabled(self):
        """Get whether ESP32 volume controller is enabled"""
        return self._esp32_volume_enabled

    @Slot(result=bool)
    def get_esp32_volume_enabled(self):
        """Get whether ESP32 volume controller is enabled"""
        return self._esp32_volume_enabled

    @Slot(bool)
    def save_esp32_volume_enabled(self, enabled):
        """Save whether ESP32 volume controller is enabled"""
        logger.debug(f"Saving ESP32 volume enabled: {enabled}")
        self._esp32_volume_enabled = enabled
        self.update_setting("esp32VolumeEnabled", enabled, self.esp32VolumeEnabledChanged)

    @Property(str, notify=esp32VolumePortChanged)
    def esp32VolumePort(self):
        """Get the ESP32 serial port"""
        return self._esp32_volume_port

    @Slot(result=str)
    def get_esp32_volume_port(self):
        """Get the ESP32 serial port"""
        return self._esp32_volume_port

    @Slot(str)
    def save_esp32_volume_port(self, port):
        """Save the ESP32 serial port"""
        logger.debug(f"Saving ESP32 volume port: {port}")
        self._esp32_volume_port = port
        self.update_setting("esp32VolumePort", port, self.esp32VolumePortChanged)

    @Property(float, notify=esp32VolumeStepSizeChanged)
    def esp32VolumeStepSize(self):
        """Get the volume step size per encoder tick (0.25-10)"""
        return self._esp32_volume_step_size

    @Slot(result=float)
    def get_esp32_volume_step_size(self):
        """Get the volume step size per encoder tick"""
        return self._esp32_volume_step_size

    @Slot(float)
    def save_esp32_volume_step_size(self, step_size):
        """Save the volume step size per encoder tick (0.25-10)"""
        logger.debug(f"Saving ESP32 volume step size: {step_size}")
        # Clamp to 0.25-10 and round to nearest 0.25
        clamped = max(0.25, min(10.0, step_size))
        self._esp32_volume_step_size = round(clamped * 4) / 4  # Round to nearest 0.25
        self.update_setting("esp32VolumeStepSize", self._esp32_volume_step_size, self.esp32VolumeStepSizeChanged)

    @Property(bool, notify=esp32AutoReconnectChanged)
    def esp32AutoReconnect(self):
        """Get whether ESP32 auto-reconnect is enabled"""
        return self._esp32_auto_reconnect

    @Slot(result=bool)
    def get_esp32_auto_reconnect(self):
        """Get whether ESP32 auto-reconnect is enabled"""
        return self._esp32_auto_reconnect

    @Slot(bool)
    def save_esp32_auto_reconnect(self, enabled):
        """Save whether ESP32 auto-reconnect is enabled"""
        logger.debug(f"Saving ESP32 auto-reconnect: {enabled}")
        self._esp32_auto_reconnect = enabled
        self.update_setting("esp32AutoReconnect", enabled, self.esp32AutoReconnectChanged)

    # ESP32 LED Sleep Enable
    @Property(bool, notify=esp32LedSleepEnabledChanged)
    def esp32LedSleepEnabled(self):
        """Get whether ESP32 LEDs sleep when OCTAVE closes"""
        return self._esp32_led_sleep_enabled

    @Slot(result=bool)
    def get_esp32_led_sleep_enabled(self):
        """Get whether ESP32 LEDs sleep when OCTAVE closes"""
        return self._esp32_led_sleep_enabled

    @Slot(bool)
    def save_esp32_led_sleep_enabled(self, enabled):
        """Save whether ESP32 LEDs sleep when OCTAVE closes"""
        logger.debug(f"Saving ESP32 LED sleep enabled: {enabled}")
        self._esp32_led_sleep_enabled = enabled
        self.update_setting("esp32LedSleepEnabled", enabled, self.esp32LedSleepEnabledChanged)

    # ESP32 LED Color Mode
    @Property(str, notify=esp32LedColorModeChanged)
    def esp32LedColorMode(self):
        """Get ESP32 LED color mode ('theme' or 'static')"""
        return self._esp32_led_color_mode

    @Slot(result=str)
    def get_esp32_led_color_mode(self):
        """Get ESP32 LED color mode"""
        return self._esp32_led_color_mode

    @Slot(str)
    def save_esp32_led_color_mode(self, mode):
        """Save ESP32 LED color mode ('theme' or 'static')"""
        if mode not in ["theme", "static"]:
            return
        logger.debug(f"Saving ESP32 LED color mode: {mode}")
        self._esp32_led_color_mode = mode
        self.update_setting("esp32LedColorMode", mode, self.esp32LedColorModeChanged)

    # ESP32 LED Static Color
    @Property(str, notify=esp32LedStaticColorChanged)
    def esp32LedStaticColor(self):
        """Get ESP32 LED static color (hex)"""
        return self._esp32_led_static_color

    @Slot(result=str)
    def get_esp32_led_static_color(self):
        """Get ESP32 LED static color"""
        return self._esp32_led_static_color

    @Slot(str)
    def save_esp32_led_static_color(self, color):
        """Save ESP32 LED static color (hex like '#FF0000')"""
        logger.debug(f"Saving ESP32 LED static color: {color}")
        self._esp32_led_static_color = color
        self.update_setting("esp32LedStaticColor", color, self.esp32LedStaticColorChanged)

    # ==================== Waveform Visualizer Settings ====================

    @Property(bool, notify=showWaveformVisualizerChanged)
    def showWaveformVisualizer(self):
        """Get whether waveform visualizer is enabled"""
        return self._show_waveform_visualizer

    @Slot(result=bool)
    def get_show_waveform_visualizer(self):
        """Get whether waveform visualizer is enabled"""
        return self._show_waveform_visualizer

    @Slot(bool)
    def save_show_waveform_visualizer(self, enabled):
        """Save whether waveform visualizer is enabled"""
        logger.debug(f"Saving show waveform visualizer: {enabled}")
        self._show_waveform_visualizer = enabled
        self.update_setting("showWaveformVisualizer", enabled, self.showWaveformVisualizerChanged)

    # ==================== 3D Visual Effect Settings ====================

    @Property(bool, notify=show3DButtonTiltChanged)
    def show3DButtonTilt(self):
        return self._show_3d_button_tilt

    @Slot(bool)
    def save_show_3d_button_tilt(self, enabled):
        self._show_3d_button_tilt = enabled
        self.update_setting("show3DButtonTilt", enabled, self.show3DButtonTiltChanged)

    @Property(bool, notify=show3DAlbumPreviewChanged)
    def show3DAlbumPreview(self):
        return self._show_3d_album_preview

    @Slot(bool)
    def save_show_3d_album_preview(self, enabled):
        self._show_3d_album_preview = enabled
        self.update_setting("show3DAlbumPreview", enabled, self.show3DAlbumPreviewChanged)

    @Property(bool, notify=roundedAlbumArtChanged)
    def roundedAlbumArt(self):
        return self._rounded_album_art

    @Slot(bool)
    def save_rounded_album_art(self, enabled):
        self._rounded_album_art = enabled
        self.update_setting("roundedAlbumArt", enabled, self.roundedAlbumArtChanged)

    @Property(int, notify=albumArtCornerRadiusChanged)
    def albumArtCornerRadius(self):
        return self._album_art_corner_radius

    @Slot(int)
    def save_album_art_corner_radius(self, radius):
        self._album_art_corner_radius = max(2, min(32, radius))
        self.update_setting("albumArtCornerRadius", self._album_art_corner_radius, self.albumArtCornerRadiusChanged)

    @Property(bool, notify=showAlbumArtShadowChanged)
    def showAlbumArtShadow(self):
        return self._show_album_art_shadow

    @Slot(bool)
    def save_show_album_art_shadow(self, enabled):
        self._show_album_art_shadow = enabled
        self.update_setting("showAlbumArtShadow", enabled, self.showAlbumArtShadowChanged)

    @Property(bool, notify=vinylRecordModeChanged)
    def vinylRecordMode(self):
        return self._vinyl_record_mode

    @Slot(bool)
    def save_vinyl_record_mode(self, enabled):
        self._vinyl_record_mode = enabled
        self.update_setting("vinylRecordMode", enabled, self.vinylRecordModeChanged)

    @Property(str, notify=albumArtTransitionChanged)
    def albumArtTransition(self):
        return self._album_art_transition

    @Slot(str)
    def save_album_art_transition(self, transition):
        if transition not in ("Crossfade", "Slide", "Vinyl Lift", "Dissolve", "Flip",
                              "Coverflow", "Conveyor", "Stack", "Depth", "Swing"):
            return
        self._album_art_transition = transition
        self.update_setting("albumArtTransition", transition, self.albumArtTransitionChanged)

    @Property(int, notify=backgroundOverlayOpacityChanged)
    def backgroundOverlayOpacity(self):
        return self._background_overlay_opacity

    @Slot(int)
    def save_background_overlay_opacity(self, opacity):
        self._background_overlay_opacity = max(0, min(100, opacity))
        self.update_setting("backgroundOverlayOpacity", self._background_overlay_opacity, self.backgroundOverlayOpacityChanged)

    @Property(float, notify=sideCardOpacityChanged)
    def sideCardOpacity(self):
        return self._side_card_opacity

    @Slot(float)
    def save_side_card_opacity(self, opacity):
        self._side_card_opacity = max(0.1, min(1.0, opacity))
        self.update_setting("sideCardOpacity", self._side_card_opacity, self.sideCardOpacityChanged)

    @Property(int, notify=sideCardAngleChanged)
    def sideCardAngle(self):
        return self._side_card_angle

    @Slot(int)
    def save_side_card_angle(self, angle):
        self._side_card_angle = max(5, min(60, angle))
        self.update_setting("sideCardAngle", self._side_card_angle, self.sideCardAngleChanged)

    @Property(int, notify=buttonTiltDurationChanged)
    def buttonTiltDuration(self):
        return self._button_tilt_duration

    @Slot(int)
    def save_button_tilt_duration(self, duration):
        self._button_tilt_duration = max(50, min(500, duration))
        self.update_setting("buttonTiltDuration", self._button_tilt_duration, self.buttonTiltDurationChanged)

    @Property(int, notify=textScrollSpeedChanged)
    def textScrollSpeed(self):
        return self._text_scroll_speed

    @Slot(int)
    def save_text_scroll_speed(self, speed):
        self._text_scroll_speed = max(1000, min(10000, speed))
        self.update_setting("textScrollSpeed", self._text_scroll_speed, self.textScrollSpeedChanged)

    # ==================== Settings Layout Style ====================

    @Property(str, notify=settingsLayoutStyleChanged)
    def settingsLayoutStyle(self):
        return self._settings_layout_style

    @Slot(str)
    def save_settings_layout_style(self, style):
        if style not in ("Carousel", "Sidebar", "Hub", "Dashboard"):
            return
        logger.debug(f"Saving settings layout style: {style}")
        self._settings_layout_style = style
        self.update_setting("settingsLayoutStyle", style, self.settingsLayoutStyleChanged)

    # ==================== IMU Sensor Settings ====================

    @Property(bool, notify=imuEnabledChanged)
    def imuEnabled(self):
        return self._imu_enabled

    @Slot(result=bool)
    def get_imu_enabled(self):
        return self._imu_enabled

    @Slot(bool)
    def save_imu_enabled(self, enabled):
        logger.debug(f"Saving IMU enabled: {enabled}")
        self._imu_enabled = enabled
        self.update_setting("imuEnabled", enabled, self.imuEnabledChanged)

    # ==================== Gesture Sensor Settings ====================

    @Property(bool, notify=gestureSensorEnabledChanged)
    def gestureSensorEnabled(self):
        return self._gesture_sensor_enabled

    @Slot(result=bool)
    def get_gesture_sensor_enabled(self):
        return self._gesture_sensor_enabled

    @Slot(bool)
    def save_gesture_sensor_enabled(self, enabled):
        logger.debug(f"Saving gesture sensor enabled: {enabled}")
        self._gesture_sensor_enabled = enabled
        self.update_setting("gestureSensorEnabled", enabled, self.gestureSensorEnabledChanged)

    @Slot(result=str)
    def get_gesture_mapping(self):
        return json.dumps(self._gesture_mapping)

    @Slot(result="QVariantMap")
    def get_gesture_mapping_dict(self):
        return self._gesture_mapping.copy()

    @Slot(str, str)
    def save_gesture_action(self, gesture, action):
        logger.debug(f"Saving gesture action: {gesture} -> {action}")
        self._gesture_mapping[gesture] = action
        settings = self.load_settings()
        settings["gestureMapping"] = self._gesture_mapping
        self.save_settings(settings)
        self.gestureMappingChanged.emit()

    @Slot("QVariantMap")
    def save_gesture_mapping(self, mapping):
        self._gesture_mapping = dict(mapping)
        settings = self.load_settings()
        settings["gestureMapping"] = self._gesture_mapping
        self.save_settings(settings)
        self.gestureMappingChanged.emit()

    @Slot()
    def reset_gesture_mapping(self):
        self._gesture_mapping = self._default_settings["gestureMapping"].copy()
        settings = self.load_settings()
        settings["gestureMapping"] = self._gesture_mapping
        self.save_settings(settings)
        self.gestureMappingChanged.emit()

    @Property(int, notify=gestureVolumeStepChanged)
    def gestureVolumeStep(self):
        return self._gesture_volume_step

    @Slot(int)
    def save_gesture_volume_step(self, step):
        logger.debug(f"Saving gesture volume step: {step}")
        self._gesture_volume_step = max(1, min(100, step))
        self.update_setting("gestureVolumeStep", self._gesture_volume_step, self.gestureVolumeStepChanged)

    @Property(int, notify=gestureCooldownChanged)
    def gestureCooldown(self):
        return self._gesture_cooldown

    @Slot(int)
    def save_gesture_cooldown(self, ms):
        logger.debug(f"Saving gesture cooldown: {ms}ms")
        self._gesture_cooldown = max(100, min(2000, ms))
        self.update_setting("gestureCooldown", self._gesture_cooldown, self.gestureCooldownChanged)

    # ==================== Pinned Quick Settings ====================

    @Property('QVariantList', notify=pinnedSettingsChanged)
    def pinnedSettings(self):
        return self._pinned_settings

    @Slot(str)
    def toggle_pin_setting(self, setting_id):
        """Toggle a setting's pinned state"""
        if setting_id in self._pinned_settings:
            self._pinned_settings.remove(setting_id)
        else:
            if setting_id in SETTINGS_REGISTRY:
                self._pinned_settings.append(setting_id)
        settings = self.load_settings()
        settings["pinnedSettings"] = self._pinned_settings
        self.save_settings(settings)
        self.pinnedSettingsChanged.emit()

    @Slot(str, result=bool)
    def is_setting_pinned(self, setting_id):
        """Check if a setting is pinned"""
        return setting_id in self._pinned_settings

    @Slot(result=str)
    def get_settings_registry(self):
        """Return the full settings registry as JSON"""
        return json.dumps(SETTINGS_REGISTRY)

    @Slot(result=str)
    def get_pinned_settings_metadata(self):
        """Return list of pinned setting objects with current values as JSON"""
        result = []
        for sid in self._pinned_settings:
            if sid not in SETTINGS_REGISTRY:
                continue
            entry = SETTINGS_REGISTRY[sid].copy()
            entry["id"] = sid
            # Resolve current value from the corresponding property
            key = entry["key"]
            if hasattr(self, '_' + self._key_to_attr(key)):
                entry["currentValue"] = getattr(self, '_' + self._key_to_attr(key))
            else:
                # Fallback: load from settings file
                entry["currentValue"] = self._settings.get(key)
            result.append(entry)
        return json.dumps(result)

    def _key_to_attr(self, key):
        """Convert camelCase settings key to snake_case attribute name"""
        # Map known keys to their internal attribute names
        attr_map = {
            "startUpVolume": "start_volume",
            "autoPlayOnStartup": "auto_play_on_startup",
            "showWaveformVisualizer": "show_waveform_visualizer",
            "uiScale": "ui_scale",
            "themeSetting": "theme_setting",
            "showClock": "show_clock",
            "showBottomBarMediaControls": "show_bottom_bar_media_controls",
            "backgroundBlurRadius": "background_blur_radius",
            "colorTransitionMs": "color_transition_ms",
            "obdFastMode": "obd_fast_mode",
            "imuEnabled": "imu_enabled",
            "gestureSensorEnabled": "gesture_sensor_enabled",
            "settingsLayoutStyle": "settings_layout_style",
        }
        return attr_map.get(key, key)

    # ==================== Settings Menu Visibility ====================

    @Property(str, notify=settingsMenuVisibilityChanged)
    def settingsMenuVisibility(self):
        """Get settings menu visibility as JSON string"""
        return json.dumps(self._settings_menu_visibility)

    @Slot(result=str)
    def get_settings_menu_visibility(self):
        """Get settings menu visibility as JSON string"""
        return json.dumps(self._settings_menu_visibility)

    @Slot(str, result=bool)
    def is_settings_section_visible(self, section):
        """Check if a specific settings section is visible"""
        return self._settings_menu_visibility.get(section, True)

    @Slot(str, bool)
    def save_settings_section_visibility(self, section, visible):
        """Save visibility for a specific settings section"""
        logger.debug(f"Saving settings section visibility: {section} = {visible}")
        self._settings_menu_visibility[section] = visible
        # Save to disk
        settings = self.load_settings()
        settings["settingsMenuVisibility"] = self._settings_menu_visibility
        self.save_settings(settings)
        # Emit signal (no parameters)
        self.settingsMenuVisibilityChanged.emit()

    # ==================== Album Art Colors ====================

    @Property(str, notify=albumArtColorsChanged)
    def albumArtColors(self):
        """Get the current album art theme colors JSON"""
        return self._album_art_colors

    @Slot(str)
    def set_album_art_colors(self, colors_json):
        """Set album art theme colors, persist them, and emit signal"""
        logger.debug(f"set_album_art_colors called, length: {len(colors_json) if colors_json else 0}")
        self._album_art_colors = colors_json
        self.update_setting("albumArtColors", colors_json, self.albumArtColorsChanged)

    # ==================== Generic Setting Access ====================
    # These methods allow QML to get/set arbitrary settings without dedicated properties
    # Useful for per-parameter OBD settings like RPM shift light configuration

    @Slot(str, result='QVariant')
    def get_setting(self, key, default_value=None):
        """Get a setting value by key with optional default"""
        settings = self.load_settings()
        return settings.get(key, default_value)

    @Slot(str, 'QVariant', result='QVariant')
    def get_setting_with_default(self, key, default_value):
        """Get a setting value by key with a specified default value"""
        settings = self.load_settings()
        return settings.get(key, default_value)

    @Slot(str, 'QVariant')
    def save_setting(self, key, value):
        """Save a single setting by key"""
        settings = self.load_settings()
        settings[key] = value
        self.save_settings(settings)
        self.genericSettingChanged.emit(key)

    @Slot()
    def reset_to_defaults(self):
        # Save default settings
        self.save_settings(self._default_settings)
        
        # Reset existing settings
        self._device_name = self._default_settings["deviceName"]
        self.deviceNameChanged.emit(self._device_name)
        
        self._theme_setting = self._default_settings["themeSetting"]
        self.themeSettingChanged.emit(self._theme_setting)

        self._font_setting = self._default_settings["fontSetting"]
        self.fontSettingChanged.emit(self._font_setting)

        self._start_volume = self._default_settings["startUpVolume"]
        self.startUpVolumeChanged.emit(self._start_volume)
        
        self._show_clock = self._default_settings["showClock"]
        self.showClockChanged.emit(self._show_clock)
        
        self._clock_format_24hour = self._default_settings["clockFormat24Hour"]
        self.clockFormatChanged.emit(self._clock_format_24hour)

        self._clock_show_seconds = self._default_settings["clockShowSeconds"]
        self.clockShowSecondsChanged.emit(self._clock_show_seconds)

        self._clock_size = self._default_settings["clockSize"]
        self.clockSizeChanged.emit(self._clock_size)
        
        self._background_grid = self._default_settings["backgroundGrid"]
        self.backgroundGridChanged.emit(self._background_grid)
                
        self._screen_width = self._default_settings["screenWidth"]
        self.screenWidthChanged.emit(self._screen_width)
        
        self._screen_height = self._default_settings["screenHeight"]
        self.screenHeightChanged.emit(self._screen_height)
        
        self._background_blur_radius = self._default_settings["backgroundBlurRadius"]
        self.backgroundBlurRadiusChanged.emit(self._background_blur_radius)
        
        self._ui_scale = self._default_settings["uiScale"]
        self.uiScaleChanged.emit(self._ui_scale)

        self._color_transition_ms = self._default_settings["colorTransitionMs"]
        self.colorTransitionMsChanged.emit(self._color_transition_ms)

        self._song_length_transition = self._default_settings["songLengthTransition"]
        self.songLengthTransitionChanged.emit(self._song_length_transition)

        self._obd_bluetooth_port = self._default_settings["obdBluetoothPort"]
        self.obdBluetoothPortChanged.emit(self._obd_bluetooth_port)

        self._obd_saved_adapters = self._default_settings["obdSavedAdapters"]
        self.obdSavedAdaptersChanged.emit(self._obd_saved_adapters)

        self._obd_fast_mode = self._default_settings["obdFastMode"]
        self.obdFastModeChanged.emit(self._obd_fast_mode)

        self._obd_auto_reconnect_attempts = self._default_settings["obdAutoReconnectAttempts"]
        self.obdAutoReconnectAttemptsChanged.emit(self._obd_auto_reconnect_attempts)

        self._obd_parameters = self._default_settings["obdParameters"]
        self.obdParametersChanged.emit()

        default_music = os.path.expanduser("~/Music")
        self._media_folder = self._default_settings["mediaFolder"] or default_music
        self.mediaFolderChanged.emit(self._media_folder)

        self._fuel_tank_capacity = self._default_settings["fuelTankCapacity"]
        self.fuelTankCapacityChanged.emit(self._fuel_tank_capacity)
            
        self._bottom_bar_orientation = self._default_settings["bottomBarOrientation"]
        self.bottomBarOrientationChanged.emit(self._bottom_bar_orientation)

        self._show_bottom_bar_media_controls = self._default_settings["showBottomBarMediaControls"]
        self.showBottomBarMediaControlsChanged.emit(self._show_bottom_bar_media_controls)

        self._auto_play_on_startup = self._default_settings["autoPlayOnStartup"]
        self.autoPlayOnStartupChanged.emit(self._auto_play_on_startup)

        self._last_played_song = self._default_settings["lastPlayedSong"]
        self.lastPlayedSongChanged.emit(self._last_played_song)

        self._last_played_position = self._default_settings["lastPlayedPosition"]
        self.lastPlayedPositionChanged.emit(self._last_played_position)

        self._last_played_playlist = self._default_settings["lastPlayedPlaylist"]
        self.lastPlayedPlaylistChanged.emit(self._last_played_playlist)

        self._music_button_default_page = self._default_settings["musicButtonDefaultPage"]
        self.musicButtonDefaultPageChanged.emit(self._music_button_default_page)

        self._return_to_library_after_selection = self._default_settings["returnToLibraryAfterSelection"]
        self.returnToLibraryAfterSelectionChanged.emit(self._return_to_library_after_selection)

        self._android_auto_enabled = self._default_settings["androidAutoEnabled"]
        self.androidAutoEnabledChanged.emit(self._android_auto_enabled)

        self._phone_mirror_enabled = self._default_settings["phoneMirrorEnabled"]
        self.phoneMirrorEnabledChanged.emit(self._phone_mirror_enabled)

        self._scrcpy_audio_enabled = self._default_settings["scrcpyAudioEnabled"]
        self.scrcpyAudioEnabledChanged.emit(self._scrcpy_audio_enabled)

        self._scrcpy_display_size = self._default_settings["scrcpyDisplaySize"]
        self.scrcpyDisplaySizeChanged.emit(self._scrcpy_display_size)

        self._scrcpy_audio_gain = self._default_settings["scrcpyAudioGain"]
        self.scrcpyAudioGainChanged.emit(self._scrcpy_audio_gain)

        self._settings_menu_visibility = self._default_settings["settingsMenuVisibility"].copy()
        self.settingsMenuVisibilityChanged.emit()

        self._esp32_volume_enabled = self._default_settings["esp32VolumeEnabled"]
        self.esp32VolumeEnabledChanged.emit(self._esp32_volume_enabled)

        self._esp32_volume_port = self._default_settings["esp32VolumePort"]
        self.esp32VolumePortChanged.emit(self._esp32_volume_port)

        self._esp32_volume_step_size = self._default_settings["esp32VolumeStepSize"]
        self.esp32VolumeStepSizeChanged.emit(self._esp32_volume_step_size)

        self._esp32_auto_reconnect = self._default_settings["esp32AutoReconnect"]
        self.esp32AutoReconnectChanged.emit(self._esp32_auto_reconnect)

        self._esp32_led_sleep_enabled = self._default_settings["esp32LedSleepEnabled"]
        self.esp32LedSleepEnabledChanged.emit(self._esp32_led_sleep_enabled)

        self._esp32_led_color_mode = self._default_settings["esp32LedColorMode"]
        self.esp32LedColorModeChanged.emit(self._esp32_led_color_mode)

        self._esp32_led_static_color = self._default_settings["esp32LedStaticColor"]
        self.esp32LedStaticColorChanged.emit(self._esp32_led_static_color)

        self._show_waveform_visualizer = self._default_settings["showWaveformVisualizer"]
        self.showWaveformVisualizerChanged.emit(self._show_waveform_visualizer)

        self._imu_enabled = self._default_settings["imuEnabled"]
        self.imuEnabledChanged.emit(self._imu_enabled)

        self._gesture_sensor_enabled = self._default_settings["gestureSensorEnabled"]
        self.gestureSensorEnabledChanged.emit(self._gesture_sensor_enabled)

        self._gesture_mapping = self._default_settings["gestureMapping"].copy()
        self.gestureMappingChanged.emit()

        self._gesture_volume_step = self._default_settings["gestureVolumeStep"]
        self.gestureVolumeStepChanged.emit(self._gesture_volume_step)

        self._gesture_cooldown = self._default_settings["gestureCooldown"]
        self.gestureCooldownChanged.emit(self._gesture_cooldown)

        self._rounded_album_art = self._default_settings["roundedAlbumArt"]
        self.roundedAlbumArtChanged.emit(self._rounded_album_art)

        self._album_art_corner_radius = self._default_settings["albumArtCornerRadius"]
        self.albumArtCornerRadiusChanged.emit(self._album_art_corner_radius)

        self._show_album_art_shadow = self._default_settings["showAlbumArtShadow"]
        self.showAlbumArtShadowChanged.emit(self._show_album_art_shadow)

        self._vinyl_record_mode = self._default_settings["vinylRecordMode"]
        self.vinylRecordModeChanged.emit(self._vinyl_record_mode)

        self._album_art_transition = self._default_settings["albumArtTransition"]
        self.albumArtTransitionChanged.emit(self._album_art_transition)

        self._background_overlay_opacity = self._default_settings["backgroundOverlayOpacity"]
        self.backgroundOverlayOpacityChanged.emit(self._background_overlay_opacity)

        self._side_card_opacity = self._default_settings["sideCardOpacity"]
        self.sideCardOpacityChanged.emit(self._side_card_opacity)

        self._side_card_angle = self._default_settings["sideCardAngle"]
        self.sideCardAngleChanged.emit(self._side_card_angle)

        self._button_tilt_duration = self._default_settings["buttonTiltDuration"]
        self.buttonTiltDurationChanged.emit(self._button_tilt_duration)

        self._text_scroll_speed = self._default_settings["textScrollSpeed"]
        self.textScrollSpeedChanged.emit(self._text_scroll_speed)

        self._pinned_settings = []
        self.pinnedSettingsChanged.emit()

    # ==================== Git Commit Info ====================

    def _get_git_repo_dir(self):
        """Get the root directory of the git repository"""
        # Try to find the repo root from the backend directory
        current_dir = self.backend_dir
        # Go up one level from backend/ to get the repo root
        repo_dir = os.path.dirname(current_dir)
        return repo_dir

    @Slot(result=str)
    def get_git_commit_hash(self):
        """Get the short hash of the latest git commit"""
        try:
            repo_dir = self._get_git_repo_dir()
            result = subprocess.run(
                ['git', 'rev-parse', '--short', 'HEAD'],
                cwd=repo_dir,
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except Exception as e:
            logger.debug(f"Error getting git commit hash: {e}")
        return "unknown"

    @Slot(result=str)
    def get_git_commit_date(self):
        """Get the date/time of the latest git commit"""
        try:
            repo_dir = self._get_git_repo_dir()
            result = subprocess.run(
                ['git', 'log', '-1', '--format=%ci'],
                cwd=repo_dir,
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                # Format: "2026-01-18 14:30:45 -0500"
                # Return a more readable format
                date_str = result.stdout.strip()
                if date_str:
                    # Parse and reformat to be more readable
                    parts = date_str.split()
                    if len(parts) >= 2:
                        return f"{parts[0]} at {parts[1][:5]}"  # "2026-01-18 at 14:30"
                return date_str
        except Exception as e:
            logger.debug(f"Error getting git commit date: {e}")
        return "unknown"

    @Slot(result=str)
    def get_git_commit_message(self):
        """Get the subject line of the latest git commit"""
        try:
            repo_dir = self._get_git_repo_dir()
            result = subprocess.run(
                ['git', 'log', '-1', '--format=%s'],
                cwd=repo_dir,
                capture_output=True,
                text=True,
                timeout=5
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except Exception as e:
            logger.debug(f"Error getting git commit message: {e}")
        return ""