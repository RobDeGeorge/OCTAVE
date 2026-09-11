import sys
import os
import platform
import argparse
import json
from datetime import datetime

# Force Qt Quick Controls to use Basic style (allows Slider customization)
os.environ["QT_QUICK_CONTROLS_STYLE"] = "Basic"

parser = argparse.ArgumentParser(description='OCTAVE Infotainment System')
parser.add_argument('--debug', action='store_true', help='Enable debug logging')
parser.add_argument('--profile', action='store_true', help='Enable performance monitor')
args, _ = parser.parse_known_args()

# Initialize logging FIRST (before other imports that might log)
from backend.logging_config import setup_logging, get_logger
setup_logging(debug=args.debug)
logger = get_logger(__name__)

# Check system type
system_name = platform.system()
logger.info(f"Detected operating system: {system_name}")

from PySide6.QtCore import QUrl, QTimer
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide6.QtCore import QResource
from PySide6.QtWidgets import QApplication

# backend imports — universal
from backend.clock import Clock
from backend.settings_manager import SettingsManager
from backend.media_manager import MediaManager
from backend.audio_analyzer import AudioAnalyzer
from backend.network_manager import NetworkManager
from backend.download_manager import DownloadManager
from backend.volume_utils import VolumeController
from backend.dashboard_manager import DashboardManager
from backend.settings_manager import get_app_data_dir

from backend.obd_manager import OBDManager
from backend.spotify_manager import SpotifyManager
from backend.android_auto import AndroidAutoManager, EmbeddedDhuItem
from backend.phone_mirror import PhoneMirrorManager
from backend.esp32_volume_manager import ESP32VolumeManager
from backend.berryimu_manager import BerryIMUManager
from backend.gesture_manager import GestureManager

app = QApplication(sys.argv)

# Auto-detect screen size and calculate scale factor
screen = app.primaryScreen()
if screen:
    available = screen.availableSize()
    # Scale off the SHORTER axis so the factor is the same whether the display
    # reports landscape or portrait dimensions (matches src/main.cpp).
    auto_scale = min(available.width(), available.height()) / 720.0
    auto_scale = max(0.4, min(3.0, auto_scale))  # Clamp to [0.4, 3.0]
    logger.info(f"Screen auto-detection: {available.width()}x{available.height()}, autoScale={auto_scale:.2f}")
else:
    auto_scale = 1.0
    logger.warning("No primary screen detected, using autoScale=1.0")

engine = QQmlApplicationEngine()

# Expose auto-scale to QML
engine.rootContext().setContextProperty("screenAutoScale", auto_scale)

# Register custom QML types
qmlRegisterType(EmbeddedDhuItem, "OCTAVE.AndroidAuto", 1, 0, "EmbeddedDhuItem")

engine.addImportPath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "frontend"))

# Expose platform flag to QML — always false on the Python (desktop) build.
engine.rootContext().setContextProperty("isAndroid", False)

# Settings Manager
settings_manager = SettingsManager()
engine.rootContext().setContextProperty("settingsManager", settings_manager)

# A hung event loop gets logged (octave.ui_watchdog) instead of vanishing
from backend.ui_watchdog import UiWatchdog
ui_watchdog = UiWatchdog()
ui_watchdog.start()

# Diagnostics — read / export the logs from the head unit (Settings > About)
from backend.diagnostics_manager import DiagnosticsManager
diagnostics_manager = DiagnosticsManager()
engine.rootContext().setContextProperty("diagnosticsManager", diagnostics_manager)

# Clock
clock = Clock(settings_manager)
engine.rootContext().setContextProperty("clock", clock)

# Media Manager
media_manager = MediaManager()
media_manager.connect_settings_manager(settings_manager)
engine.rootContext().setContextProperty("mediaManager", media_manager)

# Audio Analyzer for waveform visualization
audio_analyzer = AudioAnalyzer()
engine.rootContext().setContextProperty("audioAnalyzer", audio_analyzer)

# OBD Manager — registered via install_obd_manager() so dev mode can swap in
# a MockOBDManager before QML loads. Filled in either by the __main__ block
# at the bottom of this file or by dev/dev_runtime.install_dev_runtime().
obd_manager = None


def install_obd_manager(manager):
    """Register an OBD manager (real or mock) as the QML context property.
    Must be called before load_main_qml()."""
    global obd_manager
    obd_manager = manager
    engine.rootContext().setContextProperty("obdManager", manager)


# Spotify Manager
spotify_manager = SpotifyManager()
spotify_manager.connect_settings_manager(settings_manager)
engine.rootContext().setContextProperty("spotifyManager", spotify_manager)

# Android Auto Manager
android_auto_manager = AndroidAutoManager()
engine.rootContext().setContextProperty("androidAutoManager", android_auto_manager)

# Register DHU frame provider for seamless Android Auto display
engine.addImageProvider("dhuframe", android_auto_manager._dhu_capture.frame_provider)

# Phone Mirror Manager
phone_mirror_manager = PhoneMirrorManager()
engine.rootContext().setContextProperty("phoneMirrorManager", phone_mirror_manager)

# Phone mirror settings
phone_mirror_manager.setAudioEnabled(settings_manager.get_scrcpy_audio_enabled())
phone_mirror_manager.setDisplaySize(settings_manager.get_scrcpy_display_size())
phone_mirror_manager.setAudioGain(settings_manager.get_scrcpy_audio_gain())
settings_manager.scrcpyAudioGainChanged.connect(phone_mirror_manager.setAudioGain)
# Duck local media while the phone produces sound (nav prompts, video, calls)
phone_mirror_manager.setAudioDuckEnabled(settings_manager.get_scrcpy_audio_duck_enabled())
phone_mirror_manager.setAudioDuckLevel(settings_manager.get_scrcpy_audio_duck_level())
settings_manager.scrcpyAudioDuckEnabledChanged.connect(phone_mirror_manager.setAudioDuckEnabled)
settings_manager.scrcpyAudioDuckLevelChanged.connect(phone_mirror_manager.setAudioDuckLevel)
phone_mirror_manager.duckingChanged.connect(media_manager.setDucking)
# Keep the phone's own panel dark while mirroring; a locked phone is woken automatically
phone_mirror_manager.setPhoneScreenOff(settings_manager.get_scrcpy_phone_screen_off())
settings_manager.scrcpyPhoneScreenOffChanged.connect(phone_mirror_manager.setPhoneScreenOff)
# Startup volume is applied to all outputs by VolumeController below,
# after every manager is constructed.
settings_manager.scrcpyAudioEnabledChanged.connect(
    lambda enabled: phone_mirror_manager.setAudioEnabled(enabled)
)

# ESP32 Volume Manager - wireless rotary encoder volume control
esp32_volume_manager = ESP32VolumeManager()
esp32_volume_manager.connect_settings_manager(settings_manager)
engine.rootContext().setContextProperty("esp32VolumeManager", esp32_volume_manager)

# Volume Controller — single entry point for routing a 0–100 percent to
# every audio output (local media, Spotify, phone mirror, ESP32). Both
# Python handlers (gesture, knob) and QML sliders funnel through this to
# keep outputs in sync and the quadratic curve in one place.
volume_controller = VolumeController(
    settings_manager=settings_manager,
    media_manager=media_manager,
    spotify_manager=spotify_manager,
    phone_mirror_manager=phone_mirror_manager,
    esp32_manager=esp32_volume_manager,
)
engine.rootContext().setContextProperty("volumeController", volume_controller)
# Apply saved volume to every output at startup.
volume_controller.applyVolume(settings_manager.currentVolume)

# BerryIMU v3 Manager - live accelerometer/gyro/mag/baro for CarMenu
berryimu_manager = BerryIMUManager()
berryimu_manager.connect_settings_manager(settings_manager)
engine.rootContext().setContextProperty("berryIMU", berryimu_manager)

# Gesture Sensor Manager - PAJ7620U2 gesture recognition
gesture_manager = GestureManager()
gesture_manager.connect_settings_manager(settings_manager)
engine.rootContext().setContextProperty("gestureSensor", gesture_manager)

# Network Manager — WiFi status and update checking
network_manager = NetworkManager()
engine.rootContext().setContextProperty("networkManager", network_manager)

# Download Manager — music search and download via music_dl
download_manager = DownloadManager()
download_manager.connect_settings_manager(settings_manager)
engine.rootContext().setContextProperty("downloadManager", download_manager)

# Auto-rescan library when a download completes
download_manager.downloadComplete.connect(
    lambda name, path: media_manager.scan_library(reset_display_names=False)
)

# Dashboard Manager — built-in presets + user-authored dashboards
# (mirror of src/managers/dashboardmanager.{h,cpp}). Must register before
# engine.load so QML sees the context property at first binding eval.
dashboard_manager = DashboardManager()
dashboard_manager.setPresetsDir(
    os.path.join(os.path.dirname(os.path.abspath(__file__)),
                 "frontend", "dashboards", "presets")
)
dashboard_manager.setUserDir(os.path.join(get_app_data_dir(), "dashboards"))
engine.rootContext().setContextProperty("dashboardManager", dashboard_manager)

# Hardware signal wiring — gesture, ESP32, phone mirror
def handle_gesture_action(action):
    if action == "next_track":
        media_manager.next_track()
    elif action == "previous_track":
        media_manager.previous_track()
    elif action == "play_pause":
        media_manager.toggle_play()
    elif action == "mute_toggle":
        media_manager.toggle_mute()
    elif action == "volume_up":
        step = settings_manager.gestureVolumeStep
        volume_controller.applyVolume(settings_manager.currentVolume + step)
    elif action == "volume_down":
        step = settings_manager.gestureVolumeStep
        volume_controller.applyVolume(settings_manager.currentVolume - step)

gesture_manager.actionTriggered.connect(handle_gesture_action)

_volume_accumulator = 0.0

def handle_esp32_volume_change(delta):
    global _volume_accumulator
    _volume_accumulator += delta
    if abs(_volume_accumulator) >= 1.0:
        change = int(_volume_accumulator)
        _volume_accumulator -= change
        volume_controller.applyVolume(settings_manager.currentVolume + change)

def handle_esp32_mute_toggle():
    media_manager.toggle_mute()
    esp32_volume_manager.send_mute_state(media_manager.is_muted())

esp32_volume_manager.volumeChangeRequested.connect(handle_esp32_volume_change)
esp32_volume_manager.muteToggleRequested.connect(handle_esp32_mute_toggle)

# ESP32 volume sync is handled by VolumeController.applyVolume() above —
# every volume change goes through it, so no separate listener needed.


def hex_to_rgb(hex_color):
    hex_color = hex_color.lstrip('#')
    r = int(hex_color[0:2], 16)
    g = int(hex_color[2:4], 16)
    b = int(hex_color[4:6], 16)
    return r, g, b

def send_current_led_color():
    color_mode = settings_manager.esp32LedColorMode
    if color_mode == "static":
        static_color = settings_manager.esp32LedStaticColor
        try:
            r, g, b = hex_to_rgb(static_color)
            esp32_volume_manager.send_theme_color(r, g, b)
        except (ValueError, IndexError):
            esp32_volume_manager.send_theme_color(0, 255, 255)
    else:
        colors_json = settings_manager.albumArtColors
        if colors_json:
            try:
                colors = json.loads(colors_json)
                accent_hex = colors.get("accent", "#00FFFF")
                r, g, b = hex_to_rgb(accent_hex)
                esp32_volume_manager.send_theme_color(r, g, b)
            except (json.JSONDecodeError, ValueError, KeyError):
                esp32_volume_manager.send_theme_color(0, 255, 255)
        else:
            esp32_volume_manager.send_theme_color(0, 255, 255)

def sync_theme_to_esp32(colors_json):
    if settings_manager.esp32LedColorMode != "theme" or not colors_json:
        return
    try:
        colors = json.loads(colors_json)
        accent_hex = colors.get("accent", "#00FFFF")
        r, g, b = hex_to_rgb(accent_hex)
        esp32_volume_manager.send_theme_color(r, g, b)
    except (json.JSONDecodeError, ValueError, KeyError):
        pass

settings_manager.albumArtColorsChanged.connect(sync_theme_to_esp32)

def on_static_color_changed(color):
    if settings_manager.esp32LedColorMode == "static":
        try:
            r, g, b = hex_to_rgb(color)
            esp32_volume_manager.send_theme_color(r, g, b)
        except (ValueError, IndexError):
            pass

settings_manager.esp32LedStaticColorChanged.connect(on_static_color_changed)

def on_color_mode_changed(mode):
    send_current_led_color()

settings_manager.esp32LedColorModeChanged.connect(on_color_mode_changed)

def on_esp32_connection_changed(status):
    if status == "Connected":
        from PySide6.QtCore import QTimer
        def send_initial_state():
            esp32_volume_manager.send_volume_update(settings_manager.currentVolume)
            esp32_volume_manager.send_mute_state(media_manager.is_muted())
            send_current_led_color()
        QTimer.singleShot(500, send_initial_state)

esp32_volume_manager.connectionStatusChanged.connect(on_esp32_connection_changed)

# Add the cleanup connection after creating managers:
def cleanup_on_quit():
    """Save state and cleanup before app exits"""
    ui_watchdog.stop()  # before Qt tears the QObject down under the worker thread
    media_manager._save_playback_state_now()
    media_manager.flush_metadata_store()   # persistent tag store; covers stay cached across runs
    spotify_manager.cleanup()
    android_auto_manager.cleanup()  # Full cleanup: stops DHU, ADB, and head unit server
    phone_mirror_manager.cleanup()  # Stop phone mirror if running
    settings_manager.flush_pending_save()  # coalesced settings writes land before exit
    esp32_volume_manager.cleanup()  # Disconnect ESP32 volume controller
    berryimu_manager.cleanup()  # Stop BerryIMU sensor reading
    gesture_manager.cleanup()  # Stop gesture sensor reading
    network_manager.cleanup()  # Stop network polling
    download_manager.cleanup()  # Clean up download engine

app.aboutToQuit.connect(cleanup_on_quit)

# SIGTERM/SIGINT (launcher scripts, systemd, Ctrl-C) must run cleanup_on_quit
# too, otherwise child processes (scrcpy, ffmpeg, DHU) are orphaned. Python
# only services signal handlers while running bytecode, so a periodic no-op
# timer gives the interpreter a chance to see the signal inside app.exec().
import signal
signal.signal(signal.SIGTERM, lambda *_: app.quit())
signal.signal(signal.SIGINT, lambda *_: app.quit())
_signal_pump = QTimer()
_signal_pump.setInterval(500)
_signal_pump.timeout.connect(lambda: None)
_signal_pump.start()


def load_main_qml():
    """Load frontend/Main.qml and return the root window. install_obd_manager()
    must have been called first so the obdManager context property exists."""
    qml_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), "frontend", "Main.qml")
    engine.load(QUrl.fromLocalFile(qml_file))
    if not engine.rootObjects():
        sys.exit(-1)
    return engine.rootObjects()[0]


cmd_server = None   # set by setup_perf_profiling() (--profile); dev tooling registers actions on it


def setup_perf_profiling():
    """Wire up perf monitor + command server. Called only when --profile is set.

    backend/perf_monitor.py, backend/command_server.py and backend/perf_patches.py
    are developer-only and intentionally gitignored (see .gitignore), so every
    import is guarded — a fresh clone gets a log line, not an ImportError."""
    global cmd_server
    try:
        from backend.perf_monitor import PerfMonitor
    except ImportError:
        logger.warning("--profile: backend/perf_monitor.py not present (gitignored dev tooling), "
                       "skipping performance monitor and command server")
        return
    perf_stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    perf_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dev", "perf", "logs")
    os.makedirs(perf_dir, exist_ok=True)
    csv_path = os.path.join(perf_dir, f"perf_{perf_stamp}.csv")
    log_path = os.path.join(perf_dir, f"perf_{perf_stamp}.log")
    perf_monitor = PerfMonitor(csv_path=csv_path, log_path=log_path)
    engine.rootContext().setContextProperty("perfMonitor", perf_monitor)
    perf_monitor.start()

    # Command server for MCP/remote control
    try:
        from backend.command_server import CommandServer
    except ImportError:
        logger.warning("--profile: backend/command_server.py not present (gitignored dev tooling), "
                       "skipping command server")
    else:
        cmd_server = CommandServer()
        cmd_server.start(
            managers={
                "media": media_manager,
                "spotify": spotify_manager,
                "obd": obd_manager,
                "settings": settings_manager,
            },
            perf_monitor=perf_monitor,
            engine=engine,
        )

        def stop_cmd_server():
            cmd_server.stop()

        app.aboutToQuit.connect(stop_cmd_server)

    # Instrument hot paths
    try:
        # Developer-only instrumentation, intentionally gitignored (see .gitignore)
        from backend.perf_patches import apply_patches
    except ImportError:
        logger.warning("--profile: backend/perf_patches.py not present, skipping method instrumentation")
        return
    apply_patches(
        media_manager=media_manager,
        audio_analyzer=audio_analyzer,
        spotify_manager=spotify_manager,
        obd_manager=obd_manager,
        berryimu_manager=berryimu_manager,
        gesture_manager=gesture_manager,
        esp32_volume_manager=esp32_volume_manager,
        settings_manager=settings_manager,
    )

    def stop_perf_monitor():
        perf_monitor.stop()
        print(f"\n\033[1;36m[PERF]\033[0m  Log saved to {log_path}")
        print(f"\033[1;36m[PERF]\033[0m  CSV saved to {csv_path}")
        print(perf_monitor.getDetailedReport())

    app.aboutToQuit.connect(stop_perf_monitor)


if __name__ == "__main__":
    install_obd_manager(OBDManager(settings_manager))
    load_main_qml()
    if args.profile:
        setup_perf_profiling()
    sys.exit(app.exec())
