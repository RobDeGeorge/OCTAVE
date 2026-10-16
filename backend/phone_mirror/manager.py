"""
Phone Mirror Manager for OCTAVE.

Mirrors an Android phone into the QML view using OCTAVE's built-in
scrcpy-protocol client (backend/phone_mirror/scrcpy_client.py): the bundled
scrcpy server jar is pushed to the phone over adb and OCTAVE decodes the
video stream and injects touch itself. Nothing has to be installed by the
user: adb is bundled on x86_64 (scripts/fetch_platform_tools.py) and found
on PATH otherwise.

Mirrors src/managers/phonemirrormanager.{h,cpp}.
"""

import hashlib
import os
import platform
import queue
import re
import secrets
import shutil
import subprocess
import threading
import time
from pathlib import Path
from typing import Optional

from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer, QUrl

from backend.logging_config import get_logger
from backend.phone_mirror.qrcodegen import QrCode

from backend.phone_mirror.scrcpy_client import (
    ScrcpyClient, HAVE_AV, bundled_server_jar, SERVER_VERSION, SERVER_PROCESS_PATTERN, PERSIST_MS,
    is_network_serial, KEYCODE_HOME, KEYCODE_BACK, KEYCODE_APP_SWITCH, KEYCODE_WAKEUP,
    MEDIA_WATCH, MEDIA_PLAY, MEDIA_PAUSE, MEDIA_PLAY_PAUSE, MEDIA_NEXT, MEDIA_PREVIOUS, MEDIA_SEEK,
    MEDIA_SKIP_TO_QUEUE_ITEM,
)
from backend.settings_manager import get_app_data_dir

logger = get_logger(__name__)

# Phone media state before the server reports one (same keys as ScrcpyClient.mediaStateChanged)
_NO_MEDIA = {"has_session": False, "playing": False, "can_seek": False, "position_ms": 0, "duration_ms": 0,
             "package": "", "title": "", "artist": "", "album": ""}

# Size of the virtual display scrcpy creates on the phone (--new-display,
# Android 11+) when the setting is "auto" but the view has not reported its
# size yet. Landscape suits a dash screen. scrcpy rounds each dimension down
# to a multiple of 8, so sizes are snapped the same way to keep the UI honest.
DEFAULT_DISPLAY_SIZE = "1280x800"
AUTO_DISPLAY_SIZE = "auto"
# "auto" sizes are scaled down to fit this (phone encoders reject very large frames)
AUTO_MAX_SIDE = 1920
# A view resize restarts an "auto" session once the size has settled
RESIZE_RESTART_MS = 1500
NEW_DISPLAY_MIN_SDK = 30  # Android 11
# After a power press from the locked state the phone-side keeper wakes the
# phone but leaves its panel lit for this long. If the keyguard is dismissed
# in that window the user wants their phone, not the mirror's screen-off;
# otherwise the panel is blanked again.
WAKE_GRACE_MS = 8000
# Video bit rate defaults, Mbps (settings scrcpyBitRateUsb / scrcpyBitRateWifi)
DEFAULT_USB_BIT_RATE_MBPS = 16
DEFAULT_WIFI_BIT_RATE_MBPS = 8
# Port a USB phone's adbd is switched to for adb over TCP (setting scrcpyAutoWifi)
TCPIP_PORT = 5555
# Adaptive quality: floor, and where a Wi-Fi session starts (fraction of the
# setting, which is the ceiling). USB starts at the ceiling.
ADAPTIVE_MIN_BPS = 2_000_000
ADAPTIVE_WIFI_START = 0.6
# Minimum gap between background attempts to reach the remembered Wi-Fi phone
AUTO_CONNECT_INTERVAL_S = 5.0
# How long a shown QR code waits for the phone to scan it
QR_PAIRING_TIMEOUT_S = 120.0
_TOKEN_CHARS = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789"


def qr_pairing_payload(service: str, password: str) -> str:
    """"WIFI:T:ADB;S:<service>;P:<password>;;" -- the payload Android's "Pair
    device with QR code" scanner expects (Android Studio's format)."""
    return f"WIFI:T:ADB;S:{service};P:{password};;"


def _random_token(length: int) -> str:
    return "".join(secrets.choice(_TOKEN_CHARS) for _ in range(length))


def normalize_display_size(value: str) -> str:
    """Validate "WxH"; snap both dimensions down to multiples of 8 (min 8).
    "auto" is kept as is. Returns "" for empty/invalid input (= mirror the
    phone's own screen)."""
    if (value or "").strip().lower() == AUTO_DISPLAY_SIZE:
        return AUTO_DISPLAY_SIZE
    m = re.fullmatch(r"\s*(\d+)\s*[xX]\s*(\d+)\s*", value or "")
    if not m:
        return ""
    w, h = int(m.group(1)), int(m.group(2))
    w = max(8, (w // 8) * 8)
    h = max(8, (h // 8) * 8)
    return f"{w}x{h}"


def auto_display_size(view_width: int, view_height: int) -> str:
    """The view's pixel size scaled down to fit AUTO_MAX_SIDE and snapped to
    multiples of 8; DEFAULT_DISPLAY_SIZE before the view has reported a size."""
    if view_width < 64 or view_height < 64:
        return DEFAULT_DISPLAY_SIZE
    scale = min(1.0, AUTO_MAX_SIDE / max(view_width, view_height))
    return normalize_display_size(f"{round(view_width * scale)}x{round(view_height * scale)}")


_IP_ADDR_RE = re.compile(r"^\d+:\s+(\S+)\s+inet\s+([0-9.]+)/")


def wifi_address_from_ip_addr(output: str) -> str:
    """Phone's Wi-Fi IPv4 from `ip -4 -o addr show`. Wi-Fi client interfaces
    first, then a hotspot the phone itself runs; mobile data (rmnet*, ccmni*),
    VPNs and loopback never reach OCTAVE."""
    hotspot = ""
    for line in (output or "").split("\n"):
        m = _IP_ADDR_RE.match(line.strip())
        if not m:
            continue
        iface, ip = m.group(1), m.group(2)
        if iface.startswith("wlan"):
            return ip
        if not hotspot and iface.startswith(("swlan", "ap", "softap")):
            hotspot = ip
    return hotspot


def host_of(address: str) -> str:
    """"192.168.1.5:5555" -> "192.168.1.5", "[fe80::1]:5555" -> "fe80::1"."""
    host = (address or "").strip()
    if host.startswith("[") and "]:" in host:
        return host[1:host.index("]:")]
    if host.count(":") == 1:
        return host.split(":", 1)[0]
    return host


def _bundled_adb() -> Optional[str]:
    """adb from Google's platform-tools fetched by scripts/fetch_platform_tools.py.

    Dev layout: <repo>/tools/platform-tools/<linux|windows|darwin>/adb
    Deployed:   <app dir>/platform-tools/adb (CI copies it next to the binary)
    """
    exe = "adb.exe" if platform.system() == "Windows" else "adb"
    os_name = {"Linux": "linux", "Windows": "windows", "Darwin": "darwin"}.get(platform.system(), "")
    root = Path(__file__).resolve().parents[2]
    for c in (root / "tools" / "platform-tools" / os_name / exe,
              root / "platform-tools" / exe):
        if c.is_file():
            return str(c)
    return None


# `adb pair` / `adb mdns` arrived in platform-tools 30
ADB_WIRELESS_MIN_TOOLS = 30
_adb_tools_cache: dict = {}
_adb_tools_lock = threading.Lock()


def adb_tools_major(adb_path: str) -> int:
    """platform-tools major version of an adb binary, 0 if unknown. `adb
    version` prints "Version 36.0.2-..."; builds too old to have that line are
    far too old to pair. Cached: a box64-run adb is slow to start."""
    with _adb_tools_lock:
        if adb_path in _adb_tools_cache:
            return _adb_tools_cache[adb_path]
    major = 0
    try:
        creationflags = subprocess.CREATE_NO_WINDOW if platform.system() == "Windows" else 0
        out = subprocess.run([adb_path, "version"], capture_output=True, text=True,
                             timeout=5, creationflags=creationflags).stdout
        m = re.search(r"^Version (\d+)\.", out, re.MULTILINE)
        if m:
            major = int(m.group(1))
    except (OSError, subprocess.SubprocessError):
        pass
    with _adb_tools_lock:
        _adb_tools_cache[adb_path] = major
    return major


def _find_adb() -> Optional[str]:
    """Bundled platform-tools first, then PATH, then common install locations,
    preferring the first adb new enough to pair (ADB_WIRELESS_MIN_TOOLS); with
    none, the first one found still serves USB."""
    candidates = [_bundled_adb(), shutil.which("adb")]
    if platform.system() == "Windows":
        candidates += [
            r"C:\Program Files\Android\platform-tools\adb.exe",
            r"C:\Program Files (x86)\Android\platform-tools\adb.exe",
            str(Path.home() / "AppData" / "Local" / "Android" / "Sdk" / "platform-tools" / "adb.exe"),
        ]
    else:
        # The SDK before the distro paths: Ubuntu 22.04's /usr/bin/adb is
        # 28.x, too old to pair, and Google ships no ARM64 Linux build
        candidates += [
            str(Path.home() / "Android" / "Sdk" / "platform-tools" / "adb"),
            "/usr/bin/adb",
            "/usr/local/bin/adb",
        ]
    fallback = None
    for path in candidates:
        if not path or not os.path.exists(path):
            continue
        path = os.path.abspath(path)
        if adb_tools_major(path) >= ADB_WIRELESS_MIN_TOOLS:
            return path
        if fallback is None:
            fallback = path
    return fallback


class PhoneMirrorManager(QObject):
    """
    Owns the single mirroring session (one phone at a time), the adb probes
    the UI needs, and the settings that shape the session.
    """

    # Session state (names kept from the original scrcpy-binary design;
    # PhoneMirrorView.qml depends on them)
    scrcpyStarted = Signal(int)   # non-zero handle when the stream is up
    scrcpyStopped = Signal()
    scrcpyError = Signal(str)
    isRunningChanged = Signal()

    displaySizeChanged = Signal()
    frameSizeChanged = Signal(int, int)  # decoded frame size
    frameReady = Signal()                # a frame reached the video sink
    videoSinkChanged = Signal()
    audioActiveChanged = Signal(bool)   # phone audio playing through OCTAVE (or not)
    audioPlayingChanged = Signal(bool)  # the phone is (not) producing sound
    # Phone PCM as it is handed to the speakers (ScrcpyClient.audioPlayed);
    # main.py routes it to AudioAnalyzer.feed_live_pcm
    audioPlayed = Signal(bytes, int)
    phoneAsleepChanged = Signal(bool)   # the phone was put to sleep (locked) during a session
    phoneInUseChanged = Signal(bool)    # the user unlocked the phone: OCTAVE stops blanking / waking it
    # Emitted whenever duckingFactor changes; main.py routes it to the other
    # audio sources (MediaManager.setDucking).
    duckingChanged = Signal(float)
    # Wi-Fi: main.py saves wirelessAddressChanged to the settings
    wirelessAddressChanged = Signal(str)
    connectionTypeChanged = Signal()
    wirelessBusyChanged = Signal()
    wirelessResult = Signal(bool, str)          # outcome of QR pairing
    qrPairingChanged = Signal()
    currentBitRateChanged = Signal()
    mediaChanged = Signal()
    mediaArtChanged = Signal()
    mediaQueueChanged = Signal()
    mediaActiveChanged = Signal(bool)   # main.py pauses OCTAVE's own sources when the phone takes over
    _post = Signal(object)                      # worker thread -> GUI thread callable

    def __init__(self, parent=None):
        super().__init__(parent)
        self._adb_path: Optional[str] = _find_adb()
        if not self._adb_path:
            logger.warning("Phone mirror: no adb found, phone mirroring is unavailable")
        else:
            tools = adb_tools_major(self._adb_path)
            logger.info(f"Phone mirror: using adb {self._adb_path} (platform-tools {tools or 'unknown'})")
            if tools < ADB_WIRELESS_MIN_TOOLS:
                logger.warning("Phone mirror: this adb is too old for wireless pairing; QR pairing needs "
                               f"platform-tools {ADB_WIRELESS_MIN_TOOLS} or newer")
        self._volume: float = 1.0
        self._display_size: str = AUTO_DISPLAY_SIZE
        self._active_display_size: str = ""
        self._view_width: int = 0
        self._view_height: int = 0
        self._started_auto_size: str = ""        # "auto" size the running session was started with
        self._resize_timer = QTimer(self)        # debounces restarts on view resize
        self._resize_timer.setSingleShot(True)
        self._resize_timer.setInterval(RESIZE_RESTART_MS)
        self._resize_timer.timeout.connect(self._on_resize_settled)
        self._display_dpi: int = 0
        self._bit_rate_usb_mbps: int = DEFAULT_USB_BIT_RATE_MBPS
        self._bit_rate_wifi_mbps: int = DEFAULT_WIFI_BIT_RATE_MBPS
        self._auto_wifi: bool = True
        self._bit_rate_auto: bool = True
        self._current_bit_rate: int = 0
        self._media: dict = dict(_NO_MEDIA)
        self._media_position_at: float = 0.0    # monotonic time of the last position report
        self._media_art_url: str = ""
        self._media_queue: list = []        # as published: items with "art" filled in
        self._media_queue_raw: list = []    # as reported: items with "art_uri"
        self._media_queue_active_id = -1
        self._media_queue_title = ""
        self._queue_art: dict = {}          # art_uri -> file:// URL ("" = unreadable)
        # Art arrives one image at a time right after a queue: publish once per batch
        self._queue_art_timer = QTimer(self)
        self._queue_art_timer.setSingleShot(True)
        self._queue_art_timer.setInterval(150)
        self._queue_art_timer.timeout.connect(self._publish_media_queue)
        self._media_art_seq: int = 0            # alternates the art file so QML reloads it
        self._media_active: bool = False
        self._media_reported: bool = False      # the session's first media report arrived
        self._muted: bool = False
        self._tcpip_checked: bool = False        # the auto-Wi-Fi step ran for the session being started
        self._stale_retried: bool = False        # a failed Wi-Fi start already dropped its stale ip:port and retried
        self._start_generation: int = 0          # bumped by stopScrcpy; a pending start checks it

        self._client: Optional[ScrcpyClient] = None
        self._video_sink = None
        self._frame_width: int = 0
        self._frame_height: int = 0
        self._is_starting: bool = False
        self._is_stopping: bool = False
        self._ready: bool = False
        self._audio_gain: float = 2.0
        self._audio_playing: bool = False
        self._duck_enabled: bool = True
        self._duck_level: float = 0.1        # -20 dB
        self._duck_factor: float = 1.0       # last value emitted through duckingChanged
        self._phone_screen_off: bool = False  # blank the phone's panel while mirroring (setting scrcpyPhoneScreenOff)
        self._phone_asleep: bool = False
        self._phone_in_use: bool = False
        self._serial: str = ""
        self._vdisplay_id: int = -1          # id of the --new-display virtual display, from the server log
        self._panel_dark: bool = False           # as reported by the keeper
        # A session that died from a link drop leaves its server (and the
        # virtual display with the user's apps) alive on the phone for
        # PERSIST_MS; the next start attaches to it instead of starting fresh.
        self._persist_scid: str = ""
        self._persist_until: float = 0.0
        self._attaching: bool = False

        self._wireless_address: str = ""
        self._connection_type: str = ""
        # Wireless jobs run one at a time on one worker thread, so a
        # reconnect never races a pairing
        self._jobs: queue.Queue = queue.Queue()
        self._worker: Optional[threading.Thread] = None
        self._wireless_jobs: int = 0             # UI-started jobs in flight (GUI thread)
        self._auto_connect_running: bool = False
        self._last_auto_connect: float = 0.0
        self._qr_rows: list = []
        self._qr_generation: int = 0             # bumped on start/cancel; a stale wait gives up
        self._post.connect(self._run_posted)

    # ── availability / environment ──────────────────────────────────

    @Property(str, constant=True)
    def adbPath(self) -> str:
        return self._adb_path or ""

    @Property(bool, constant=True)
    def nativeAvailable(self) -> bool:
        """PyAV importable, bundled server jar found, adb found."""
        return bool(HAVE_AV and bundled_server_jar() and self._adb_path)

    @Property(str, constant=True)
    def serverVersion(self) -> str:
        return SERVER_VERSION

    @Slot(result=bool)
    def environmentOk(self) -> bool:
        """True when everything OCTAVE needs is present, i.e. a failure is
        about the phone, not the setup (the view then hides the setup text)."""
        return self.nativeAvailable

    @Slot(result=str)
    def getInstallInstructions(self) -> str:
        missing = []
        if not HAVE_AV:
            missing.append("the Python package 'av' (pip install av)")
        if not bundled_server_jar():
            missing.append("the bundled phone server (tools/phone-server/)")
        if not self._adb_path:
            missing.append("adb (run scripts/fetch_platform_tools.py or install android-tools)")
        if missing:
            return "Phone mirroring needs " + " and ".join(missing) + "."
        return ("Enable USB debugging on the phone (Settings > Developer options), connect it "
                "over USB and accept the authorization prompt. To go wireless, open Phone Mirror and "
                "scan its QR code from the phone's Wireless debugging screen.")

    # ── device probes ───────────────────────────────────────────────

    def _run_adb(self, args, timeout: int = 10) -> str:
        if not self._adb_path:
            return ""
        try:
            creationflags = subprocess.CREATE_NO_WINDOW if platform.system() == "Windows" else 0
            result = subprocess.run([self._adb_path] + list(args), capture_output=True,
                                    text=True, timeout=timeout, creationflags=creationflags)
            return result.stdout if result.returncode == 0 else ""
        except (subprocess.SubprocessError, OSError):
            return ""

    def _run_adb_full(self, args, timeout: float = 10) -> str:
        """stdout+stderr whatever the exit code; safe from any thread."""
        if not self._adb_path:
            return ""
        try:
            creationflags = subprocess.CREATE_NO_WINDOW if platform.system() == "Windows" else 0
            result = subprocess.run([self._adb_path] + list(args), stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=timeout,
                                    creationflags=creationflags)
            return (result.stdout or "").strip()
        except subprocess.TimeoutExpired as e:
            out = e.stdout or ""
            return (out.decode(errors="replace") if isinstance(out, bytes) else out).strip()
        except (subprocess.SubprocessError, OSError):
            return ""

    def _serial_args(self) -> list:
        """["-s", serial] for the phone getDeviceSerial() picks. With a phone on
        both USB and Wi-Fi (or two phones) a bare `adb shell` fails with
        "more than one device", so every probe names its phone."""
        serial = self.getDeviceSerial()
        return ["-s", serial] if serial else []

    def _devices(self) -> list:
        """[(serial, state)] from `adb devices`."""
        out = self._run_adb(["devices"])
        rows = []
        for line in out.strip().split("\n")[1:]:
            parts = line.split()
            if len(parts) >= 2:
                rows.append((parts[0], parts[1]))
        return rows

    @Slot(result=str)
    def getDeviceState(self) -> str:
        """"device" (ready), "unauthorized", "offline", "none" or "no-adb"."""
        if not self._adb_path:
            return "no-adb"
        states = [st for _, st in self._devices()]
        if "device" in states:
            return "device"
        # Nothing usable: if a Wi-Fi phone is remembered, try to reach it in
        # the background (throttled). The view polls this, so it is retried for free.
        self._maybe_reconnect_wireless()
        for st in ("unauthorized", "offline"):
            if st in states:
                return st
        return "none"

    @staticmethod
    def describeDeviceState(state: str) -> str:
        return {
            "no-adb": "adb not found. Run scripts/fetch_platform_tools.py or install android-tools.",
            "unauthorized": "Phone is connected but USB debugging is not authorized. "
                            "Unlock the phone and tap 'Allow' on the USB debugging prompt.",
            "offline": "Phone is connected but adb reports it offline. Unplug and replug the cable.",
            "none": "No Android device connected. Connect via USB and enable USB debugging.",
        }.get(state, "")

    def _describe_problem(self, state: str) -> str:
        """describeDeviceState() with the Wi-Fi case spelled out. The phrases the
        view keys its auto-retry on (No Android device / not authorized /
        offline) are kept."""
        wireless = False
        if state in ("offline", "unauthorized"):
            matching = [serial for serial, st in self._devices() if st == state]
            wireless = bool(matching) and all(is_network_serial(s) for s in matching)
        if wireless and state == "offline":
            return ("The phone's Wi-Fi link is offline. Check that it is on the same Wi-Fi network "
                    "as OCTAVE and that Wireless debugging is on.")
        if wireless and state == "unauthorized":
            return ("Phone is connected over Wi-Fi but debugging is not authorized. "
                    "Unlock the phone and tap 'Allow' on the debugging prompt.")
        if state == "none" and self._wireless_address:
            return (f"No Android device connected. Waiting for {self._wireless_address} over Wi-Fi: turn on "
                    "Wireless debugging on the phone (same Wi-Fi network as OCTAVE), or plug it in over USB.")
        return self.describeDeviceState(state)

    @Slot(result=bool)
    def hasConnectedDevice(self) -> bool:
        return self.getDeviceState() == "device"

    @Slot(result=str)
    def getDeviceSerial(self) -> str:
        """USB first: it is the faster, steadier link when the phone is on both."""
        wireless = ""
        for serial, st in self._devices():
            if st != "device":
                continue
            if not is_network_serial(serial):
                return serial
            wireless = wireless or serial
        return wireless

    @Slot(result=str)
    def getDeviceName(self) -> str:
        return self._run_adb(self._serial_args() + ["shell", "getprop", "ro.product.model"]).strip()

    @Slot(result=str)
    def getDeviceResolution(self) -> str:
        for line in self._run_adb(self._serial_args() + ["shell", "wm", "size"]).split("\n"):
            if "Physical size:" in line:
                return line.split(":", 1)[1].strip()
        return ""

    @Slot(result=int)
    def getDeviceSdk(self) -> int:
        try:
            return int(self._run_adb(self._serial_args() + ["shell", "getprop", "ro.build.version.sdk"]).strip())
        except (TypeError, ValueError):
            return 0

    def _kill_stale_server(self):
        """A crashed session can leave the device-side server running."""
        serial_args = ["-s", self._serial] if self._serial else []
        self._run_adb(serial_args + ["shell", "pkill", "-f", SERVER_PROCESS_PATTERN], timeout=5)

    # ── settings ────────────────────────────────────────────────────

    @Slot(float)
    def setVolume(self, volume: float):
        """Called by VolumeController.applyVolume() on every volume change
        (0..1 linear). Phone audio, always played through OCTAVE while
        mirroring, follows it. Must
        exist even when audio is off: VolumeController calls it unconditionally."""
        self._volume = volume
        if self._client is not None:
            self._client.set_volume(0.0 if self._muted else volume)

    @Slot(bool)
    def setMuted(self, muted: bool):
        """OCTAVE's mute (MediaManager.muteChanged) silences phone audio too."""
        self._muted = bool(muted)
        if self._client is not None:
            self._client.set_volume(0.0 if self._muted else self._volume)

    def _restart_session(self):
        if self.isRunning:
            self.stopScrcpy()
            QTimer.singleShot(300, self.startScrcpy)

    @Slot(float)
    def setAudioGain(self, gain: float):
        """Linear gain applied to phone PCM before OCTAVE's volume (setting
        scrcpyAudioGain). Phones deliver capture at their own media level,
        typically well under loudness-mastered local music; +6 dB (2.0) is a
        sensible default. Clamped 0.25..8, soft-limited in the client."""
        self._audio_gain = max(0.25, min(8.0, float(gain)))
        if self._client is not None:
            self._client.set_audio_gain(self._audio_gain)

    @Property(bool, notify=audioActiveChanged)
    def audioActive(self) -> bool:
        """Phone audio is currently being played through OCTAVE."""
        return self._client is not None and self._client.audio_active

    @Property(bool, notify=audioPlayingChanged)
    def audioPlaying(self) -> bool:
        """The phone is producing sound right now (navigation prompt, video, call)."""
        return self._audio_playing

    @property
    def duckingFactor(self) -> float:
        """Factor other sources should currently apply: duck level while the
        phone is producing sound and ducking is on, 1.0 otherwise."""
        return self._duck_factor

    @Slot(bool)
    def setAudioDuckEnabled(self, enabled: bool):
        """Duck OCTAVE's other sources while the phone produces sound
        (setting scrcpyAudioDuckEnabled)."""
        enabled = bool(enabled)
        if enabled == self._duck_enabled:
            return
        self._duck_enabled = enabled
        self._update_ducking()

    @Slot(float)
    def setAudioDuckLevel(self, level: float):
        """Linear factor applied to the other sources while ducked
        (setting scrcpyAudioDuckLevel, 0.1 = -20 dB)."""
        self._duck_level = max(0.0, min(1.0, float(level)))
        self._update_ducking()

    def _on_audio_playing(self, playing: bool):
        if playing == self._audio_playing:
            return
        self._audio_playing = playing
        self.audioPlayingChanged.emit(playing)
        self._update_ducking()

    def _update_ducking(self):
        factor = self._duck_level if (self._duck_enabled and self._audio_playing) else 1.0
        if abs(factor - self._duck_factor) < 1e-6:
            return
        self._duck_factor = factor
        logger.debug(f"phone mirror: ducking factor {factor:.3f}")
        self.duckingChanged.emit(factor)

    # ── phone screen / sleep ────────────────────────────────────────
    # The phone-side MirrorKeeper (phone_server/.../control/MirrorKeeper.java)
    # does the work: it watches the phone's power state and keyguard from
    # inside the device, wakes it on a power press (the virtual display has no
    # power group of its own and dozes with the phone), blanks the panel per
    # the setting, and decides "in use" from the keyguard being dismissed.
    # OCTAVE only sets the policy, takes the phone back, and shows the state.

    @Property(bool, notify=phoneAsleepChanged)
    def phoneAsleep(self) -> bool:
        """True while the phone is dozing (power button / lock) during a session.
        Its virtual display stops compositing and drops touch; the keeper wakes it."""
        return self._phone_asleep

    @Property(bool, notify=phoneInUseChanged)
    def phoneInUse(self) -> bool:
        """True after the user unlocked the phone during a session: the panel is
        left alone and a lock is not undone, until they lock it again or tap
        resumeMirroring(). The mirror itself keeps working while it is awake."""
        return self._phone_in_use

    @Slot(bool)
    def setPhoneScreenOff(self, off: bool):
        """Setting scrcpyPhoneScreenOff: keep the phone's own panel dark while
        mirroring. The stream and touch keep working (SurfaceControl power
        mode, not sleep); the server restores the panel when the session ends."""
        off = bool(off)
        if off == self._phone_screen_off:
            return
        self._phone_screen_off = off
        self._send_keeper_policy()

    @Slot()
    def resumeMirroring(self):
        """Take the phone back: leave the in-use state, wake it if needed and
        re-apply the screen-off setting."""
        if self._client is not None and self._client.is_running:
            self._client.take_back()

    @Slot()
    def wakePhone(self):
        """Wake a sleeping phone from OCTAVE's side (KEYCODE_WAKEUP over adb);
        the keeper normally does this itself within its 250 ms poll."""
        if not self._serial:
            return
        logger.info("Phone mirror: waking phone")
        serial = self._serial
        threading.Thread(
            target=self._run_adb, daemon=True, name="phone-mirror-wake",
            args=(["-s", serial, "shell", "input", "keyevent", str(KEYCODE_WAKEUP)],), kwargs={"timeout": 5},
        ).start()

    def _send_keeper_policy(self):
        if self._client is not None and self._client.is_running and self._ready:
            self._client.set_keeper(True, self._phone_screen_off, WAKE_GRACE_MS)

    def _on_phone_state(self, asleep: bool, in_use: bool, panel_dark: bool):
        if asleep != self._phone_asleep:
            self._phone_asleep = asleep
            if self._client is not None:
                self._client.set_hold_black(asleep)   # keep the last good frame on the dash
            logger.info("Phone mirror: phone went to sleep; the keeper is waking it" if asleep
                        else "Phone mirror: phone is awake again")
            self.phoneAsleepChanged.emit(asleep)
        if in_use != self._phone_in_use:
            self._phone_in_use = in_use
            logger.info("Phone mirror: phone unlocked by the user; leaving its screen alone" if in_use
                        else "Phone mirror: phone handed back to the mirror")
            self.phoneInUseChanged.emit(in_use)
        self._panel_dark = panel_dark

    def _reset_phone_state(self):
        if self._phone_in_use:
            self._phone_in_use = False
            self.phoneInUseChanged.emit(False)
        if self._phone_asleep:
            self._phone_asleep = False
            self.phoneAsleepChanged.emit(False)
        self._panel_dark = False

    # ── phone media ─────────────────────────────────────────────────
    # The server's PhoneMedia (phone_server/.../control/PhoneMedia.java)
    # follows the phone's active media session and reports it; OCTAVE's media
    # controls drive it through ScrcpyClient.media_command while mediaActive is set.

    @Property(bool, notify=mediaChanged)
    def mediaAvailable(self) -> bool:
        """The phone reports a media session (whatever app plays it)."""
        return self._media["has_session"]

    @Property(bool, notify=mediaChanged)
    def mediaPlaying(self) -> bool:
        return self._media["has_session"] and self._media["playing"]

    @Property(str, notify=mediaChanged)
    def mediaTitle(self) -> str:
        return self._media["title"]

    @Property(str, notify=mediaChanged)
    def mediaArtist(self) -> str:
        return self._media["artist"]

    @Property(str, notify=mediaChanged)
    def mediaAlbum(self) -> str:
        return self._media["album"]

    @Property(str, notify=mediaChanged)
    def mediaApp(self) -> str:
        """Package name of the phone's media app."""
        return self._media["package"]

    @Property(int, notify=mediaChanged)
    def mediaDuration(self) -> int:
        """Track length in ms, 0 = unknown."""
        return int(min(self._media["duration_ms"], 2**31 - 1))

    @Property(bool, notify=mediaChanged)
    def mediaCanSeek(self) -> bool:
        return self._media["can_seek"]

    @Property(str, notify=mediaArtChanged)
    def mediaArtUrl(self) -> str:
        """file:// URL of the album art (a JPEG in the app data temp dir), "" for none."""
        return self._media_art_url

    @Property(bool, notify=mediaActiveChanged)
    def mediaActive(self) -> bool:
        """The phone is the media source OCTAVE's controls drive: set for the
        whole mirroring session (from the first media report); a local
        track starting clears it (releaseMedia) until the phone plays
        again, and the session ending clears it."""
        return self._media_active

    # The phone session's play queue (Spotify: the current track and the
    # next 49): [{id, title, artist, art}] (art: file:// URL of a JPEG in the
    # app data temp dir, "" until it arrives); mediaQueueActiveId is the
    # playing item's id (-1 none). mediaSkipToQueueItem(id) jumps to an item.
    @Property("QVariantList", notify=mediaQueueChanged)
    def mediaQueue(self) -> list:
        return self._media_queue

    @Property("qint64", notify=mediaQueueChanged)
    def mediaQueueActiveId(self) -> int:
        return self._media_queue_active_id

    @Property(str, notify=mediaQueueChanged)
    def mediaQueueTitle(self) -> str:
        return self._media_queue_title

    def _on_media_queue(self, items: list, active_id: int, title: str):
        if (items, active_id, title) == (self._media_queue_raw, self._media_queue_active_id, self._media_queue_title):
            return
        self._media_queue_raw = items
        self._media_queue_active_id = active_id
        self._media_queue_title = title
        self._publish_media_queue()

    @staticmethod
    def _queue_art_dir() -> str:
        return os.path.join(get_app_data_dir(), "temp", "phone_queue_art")

    def _on_media_queue_art(self, key: str, jpeg: bytes):
        url = ""
        if jpeg:
            art_dir = self._queue_art_dir()
            path = os.path.join(art_dir, hashlib.sha1(key.encode("utf-8")).hexdigest() + ".jpg")
            try:
                os.makedirs(art_dir, exist_ok=True)
                with open(path, "wb") as f:
                    f.write(jpeg)
                url = QUrl.fromLocalFile(path).toString()
            except OSError as e:
                logger.warning(f"Phone mirror: could not write phone queue art to {path}: {e}")
        self._queue_art[key] = url
        self._queue_art_timer.start()

    def _publish_media_queue(self):
        self._queue_art_timer.stop()
        self._media_queue = [dict(item, art=self._queue_art.get(item.get("art_uri", ""), ""))
                             for item in self._media_queue_raw]
        self.mediaQueueChanged.emit()

    def _on_media_state(self, state: dict):
        was_playing = self._media["has_session"] and self._media["playing"]
        self._media = dict(state)
        self._media_position_at = time.monotonic()
        self.mediaChanged.emit()
        # The phone is the media source while mirroring: it takes the controls
        # with its first report, and again whenever it starts playing after a
        # local track took them (releaseMedia)
        playing = state["has_session"] and state["playing"]
        first_report = not self._media_reported
        self._media_reported = True
        if not self._media_active and (first_report or (playing and not was_playing)):
            logger.info(f"Phone mirror: OCTAVE's media controls drive the phone {state['package']}".rstrip())
            self._set_media_active(True)

    def _on_media_art(self, jpeg: bytes):
        url = ""
        if jpeg:
            art_dir = os.path.join(get_app_data_dir(), "temp")
            # Alternate between two files: a new URL makes QML reload the image
            self._media_art_seq = (self._media_art_seq + 1) % 2
            path = os.path.join(art_dir, f"phone_media_art_{self._media_art_seq}.jpg")
            try:
                os.makedirs(art_dir, exist_ok=True)
                with open(path, "wb") as f:
                    f.write(jpeg)
                url = QUrl.fromLocalFile(path).toString()
            except OSError as e:
                logger.warning(f"Phone mirror: could not write phone album art to {path}: {e}")
        if url != self._media_art_url:
            self._media_art_url = url
            self.mediaArtChanged.emit()

    def _reset_media(self):
        self._media_reported = False
        self._set_media_active(False)
        if self._media != _NO_MEDIA:
            self._media = dict(_NO_MEDIA)
            self.mediaChanged.emit()
        if self._media_art_url:
            self._media_art_url = ""
            self.mediaArtChanged.emit()
        self._on_media_queue([], -1, "")
        if self._queue_art:
            self._queue_art.clear()
            shutil.rmtree(self._queue_art_dir(), ignore_errors=True)

    def _set_media_active(self, active: bool):
        if active == self._media_active:
            return
        self._media_active = active
        self.mediaActiveChanged.emit(active)

    @Slot(result=int)
    def mediaPosition(self) -> int:
        """Position now in ms, extrapolated from the last report while playing."""
        pos = self._media["position_ms"]
        if self._media["has_session"] and self._media["playing"]:
            pos += int((time.monotonic() - self._media_position_at) * 1000)
        if self._media["duration_ms"] > 0:
            pos = min(pos, self._media["duration_ms"])
        return int(max(0, min(pos, 2**31 - 1)))

    def _media_command(self, action: int, argument: int = 0):
        if self._client is not None and self._ready:
            self._client.media_command(action, argument)

    @Slot()
    def mediaPlayPause(self):
        self._media_command(MEDIA_PLAY_PAUSE)

    @Slot()
    def mediaPlay(self):
        self._media_command(MEDIA_PLAY)

    @Slot()
    def mediaPause(self):
        self._media_command(MEDIA_PAUSE)

    @Slot()
    def mediaNext(self):
        self._media_command(MEDIA_NEXT)

    @Slot()
    def mediaPrevious(self):
        self._media_command(MEDIA_PREVIOUS)

    @Slot(int)
    def mediaSeek(self, position_ms: int):
        if self._client is None or not self._ready:
            return
        position_ms = max(0, int(position_ms))
        self._media_command(MEDIA_SEEK, position_ms)
        # Show the new position before the phone confirms it
        self._media["position_ms"] = position_ms
        self._media_position_at = time.monotonic()

    @Slot("qint64")
    def mediaSkipToQueueItem(self, queue_id: int):
        self._media_command(MEDIA_SKIP_TO_QUEUE_ITEM, int(queue_id))

    @Slot(bool)
    def releaseMedia(self, pause_phone: bool):
        """OCTAVE's local playback took over: the controls leave the
        phone; pause_phone also pauses what the phone is playing."""
        if not self._media_active:
            return
        if pause_phone and self.mediaPlaying:
            self.mediaPause()
        logger.info("Phone mirror: OCTAVE's media controls leave the phone" + (" (phone paused)" if pause_phone else ""))
        self._set_media_active(False)

    @Property(str, notify=displaySizeChanged)
    def displaySize(self) -> str:
        """Requested virtual display size "WxH" (--new-display), "auto" = the
        view's own pixel size (setViewSize), "" = phone screen."""
        return self._display_size

    @Slot(str)
    def setDisplaySize(self, size: str):
        norm = normalize_display_size(size)
        if norm != (size or "").strip():
            logger.info(f"Display size '{size}' normalized to '{norm}' (scrcpy rounds to multiples of 8)")
        if norm == self._display_size:
            return
        self._display_size = norm
        self.displaySizeChanged.emit()
        self._restart_session()

    def _requested_display(self) -> str:
        """Size actually requested from the server: "WxH[/dpi]", "" = phone screen."""
        size = (auto_display_size(self._view_width, self._view_height)
                if self._display_size == AUTO_DISPLAY_SIZE else self._display_size)
        if size and self._display_dpi > 0:
            size += f"/{self._display_dpi}"
        return size

    @Slot(int, int)
    def setViewSize(self, width: int, height: int):
        """The mirror view's size in device pixels; drives the "auto" display
        size. A running auto session is restarted (debounced) when it changes."""
        if (width, height) == (self._view_width, self._view_height):
            return
        self._view_width, self._view_height = int(width), int(height)
        if self.isRunning and self._display_size == AUTO_DISPLAY_SIZE:
            self._resize_timer.start()

    def _on_resize_settled(self):
        size = auto_display_size(self._view_width, self._view_height)
        if (self.isRunning and self._display_size == AUTO_DISPLAY_SIZE and self._started_auto_size
                and size != self._started_auto_size):
            logger.info(f"View resized: restarting the mirror at {size}")
            self._restart_session()

    @Slot(int)
    def setDisplayDpi(self, dpi: int):
        """Virtual display density (setting scrcpyDisplayDpi), 0 = scaled from the phone's."""
        dpi = max(0, int(dpi))
        if dpi == self._display_dpi:
            return
        self._display_dpi = dpi
        if self._display_size:
            self._restart_session()

    @Slot(int)
    def setBitRateUsb(self, mbps: int):
        mbps = max(2, min(40, int(mbps)))
        if mbps == self._bit_rate_usb_mbps:
            return
        self._bit_rate_usb_mbps = mbps
        if self._connection_type == "usb":
            self._restart_session()

    @Slot(int)
    def setBitRateWifi(self, mbps: int):
        mbps = max(2, min(40, int(mbps)))
        if mbps == self._bit_rate_wifi_mbps:
            return
        self._bit_rate_wifi_mbps = mbps
        if self._connection_type == "wifi":
            self._restart_session()

    @Slot(bool)
    def setBitRateAuto(self, enabled: bool):
        """Setting scrcpyBitRateAuto: adapt the rate to the link, with the
        per-link settings as the ceiling (AdaptiveBitRate)."""
        enabled = bool(enabled)
        if enabled == self._bit_rate_auto:
            return
        self._bit_rate_auto = enabled
        self._restart_session()

    @Property(int, notify=currentBitRateChanged)
    def currentBitRate(self) -> int:
        """Video bit rate of the running session in bps (follows adaptive changes), 0 when idle."""
        return self._current_bit_rate

    def _set_current_bit_rate(self, bps: int):
        if bps != self._current_bit_rate:
            self._current_bit_rate = bps
            self.currentBitRateChanged.emit()

    @Slot(bool)
    def setAutoWifi(self, enabled: bool):
        """Setting scrcpyAutoWifi: before a USB session, switch the phone's adbd
        to TCP port TCPIP_PORT and remember its Wi-Fi address, so it can be
        reached without a cable (and without the Wireless debugging toggle)
        until it reboots."""
        self._auto_wifi = bool(enabled)

    @Property(str, notify=frameSizeChanged)
    def activeDisplaySize(self) -> str:
        """"WxH" of the virtual display in use, "" when mirroring the phone screen."""
        return self._active_display_size

    # ── video sink / frames ─────────────────────────────────────────

    @Property(QObject, notify=videoSinkChanged)
    def videoSink(self):
        return self._video_sink

    @videoSink.setter
    def videoSink(self, sink):
        """QML binds VideoOutput.videoSink here; decoded frames go straight to it."""
        self._video_sink = sink
        if self._client is not None:
            self._client.set_video_sink(sink)
        self.videoSinkChanged.emit()

    @Property(int, notify=frameSizeChanged)
    def frameWidth(self) -> int:
        return self._frame_width

    @Property(int, notify=frameSizeChanged)
    def frameHeight(self) -> int:
        return self._frame_height

    # ── input ───────────────────────────────────────────────────────

    @Slot(int, int, float, float)
    def injectTouch(self, pointer_id: int, action: int, rel_x: float, rel_y: float):
        """Touch on the mirrored display. action: 0 down, 1 up, 2 move; rel 0..1."""
        if self._client is None or not self._client.is_running:
            return
        w, h = self._client.frame_size
        self._client.inject_touch(pointer_id, action, rel_x * w, rel_y * h)

    @Slot(int)
    def injectKey(self, keycode: int):
        if self._client is not None and self._client.is_running:
            self._client.press_key(keycode)

    @Slot()
    def pressHome(self):
        # HOME is a system key that Android routes to the default display, so
        # on a virtual display it does nothing; launch the launcher there instead.
        if self._vdisplay_id >= 0 and self._serial:
            threading.Thread(
                target=self._run_adb, daemon=True, name="phone-mirror-home",
                args=(["-s", self._serial, "shell", "am", "start", "--display", str(self._vdisplay_id),
                       "-a", "android.intent.action.MAIN", "-c", "android.intent.category.HOME"],),
                kwargs={"timeout": 5},
            ).start()
            return
        self.injectKey(KEYCODE_HOME)

    def _on_server_log(self, line: str):
        m = re.search(r"New display: .*\(id=(\d+)\)", line)
        if m:
            self._vdisplay_id = int(m.group(1))

    @Slot()
    def pressBack(self):
        self.injectKey(KEYCODE_BACK)

    @Slot()
    def pressAppSwitch(self):
        self.injectKey(KEYCODE_APP_SWITCH)

    # ── session lifecycle ───────────────────────────────────────────

    @Property(bool, notify=isRunningChanged)
    def isRunning(self) -> bool:
        return self._client is not None and self._client.is_running

    @Slot()
    def startScrcpy(self):
        """Start a mirroring session (singleton)."""
        if self.isRunning:
            if self._ready:
                self.scrcpyStarted.emit(1)
            return
        if self._is_starting:
            return
        if not self.nativeAvailable:
            self.scrcpyError.emit(self.getInstallInstructions())
            return
        state = self.getDeviceState()
        if state != "device":
            self.scrcpyError.emit(self._describe_problem(state))
            return
        serial = self.getDeviceSerial()
        wireless = is_network_serial(serial)
        if not wireless and self._auto_wifi and not self._tcpip_checked:
            # Off the GUI thread: `adb tcpip` restarts the phone's adbd, which
            # takes a couple of seconds. The session starts once it is back.
            self._is_starting = True
            self._start_generation += 1
            gen = self._start_generation

            def job():
                address = self._enable_tcpip(serial)

                def done():
                    if address and address != self._wireless_address:
                        logger.info(f"Phone mirror: remembering the phone for Wi-Fi at {address}")
                        self._wireless_address = address
                        self.wirelessAddressChanged.emit(address)
                    if gen != self._start_generation:
                        return   # stopped meanwhile
                    self._is_starting = False
                    self._tcpip_checked = True
                    self.startScrcpy()
                self._post.emit(done)

            self._run_wireless_job(job, False)
            return
        self._tcpip_checked = False

        self._is_starting = True
        self._is_stopping = False
        self._ready = False
        requested = self._requested_display()
        self._active_display_size = requested.split("/", 1)[0]
        self._started_auto_size = self._active_display_size if self._display_size == AUTO_DISPLAY_SIZE else ""
        attach_scid = ""
        if self._persist_scid and time.monotonic() < self._persist_until:
            attach_scid = self._persist_scid
        self._attaching = bool(attach_scid)
        if not attach_scid:
            self._persist_scid = ""
            self._serial = serial
            self._kill_stale_server()

        if self._client is not None:
            self._client.stop()
        client = ScrcpyClient(self._adb_path, bundled_server_jar(), self)
        client.set_video_sink(self._video_sink)
        client.connected.connect(self._on_connected)
        client.disconnected.connect(self._on_disconnected)
        client.frameSizeChanged.connect(self._on_frame_size)
        client.frameReady.connect(self.frameReady)
        client.audioStateChanged.connect(self.audioActiveChanged)
        client.audioPlayingChanged.connect(self._on_audio_playing)
        client.audioPlayed.connect(self.audioPlayed)
        client.serverLog.connect(self._on_server_log)
        client.phoneStateChanged.connect(self._on_phone_state)
        client.mediaStateChanged.connect(self._on_media_state)
        client.mediaArtChanged.connect(self._on_media_art)
        client.mediaQueueChanged.connect(self._on_media_queue)
        client.mediaQueueArt.connect(self._on_media_queue_art)
        client.bitRateChanged.connect(self._set_current_bit_rate)
        client.set_audio_gain(self._audio_gain)
        client.set_volume(0.0 if self._muted else self._volume)
        self._client = client

        display_size = requested
        if display_size:
            sdk = self.getDeviceSdk()
            if 0 < sdk < NEW_DISPLAY_MIN_SDK:
                logger.warning(f"Device SDK {sdk} < {NEW_DISPLAY_MIN_SDK}: virtual display unavailable, mirroring phone screen")
                display_size = ""
                self._active_display_size = ""
                self._started_auto_size = ""
        bit_rate_mbps = self._bit_rate_wifi_mbps if wireless else self._bit_rate_usb_mbps
        start_bps = bit_rate_mbps * 1_000_000
        if self._bit_rate_auto:
            # The setting is the ceiling; Wi-Fi starts below it and climbs while the link keeps up
            client.set_adaptive_bit_rate(ADAPTIVE_MIN_BPS, start_bps)
            if wireless:
                start_bps = max(ADAPTIVE_MIN_BPS, int(start_bps * ADAPTIVE_WIFI_START))
        logger.info(f"Starting phone mirror (server {SERVER_VERSION}) for {serial} "
                    f"(display {display_size or 'phone screen'}, {start_bps / 1e6:g} Mbps "
                    f"{'adaptive, max ' + str(bit_rate_mbps) if self._bit_rate_auto else 'fixed'}, "
                    f"{'Wi-Fi' if wireless else 'USB'})")
        self._serial = serial
        self._set_connection_type("wifi" if wireless else "usb")
        if not attach_scid:
            self._vdisplay_id = -1
        if attach_scid:
            logger.info(f"Reattaching to the phone's running mirror session (scid {attach_scid})")
        # Phone audio always plays through OCTAVE while mirroring
        client.start(serial, display_size=display_size, audio=True, attach_scid=attach_scid,
                     bit_rate=start_bps)
        self.isRunningChanged.emit()

    def _on_connected(self, w: int, h: int):
        self._ready = True
        self._stale_retried = False
        self._is_starting = False
        self._attaching = False
        self._persist_scid = ""
        self._frame_width, self._frame_height = w, h
        if self._active_display_size:
            self._active_display_size = f"{w}x{h}"
        self.frameSizeChanged.emit(w, h)
        logger.info(f"Phone mirror connected: {w}x{h}")
        self.scrcpyStarted.emit(1)
        # Hand the sleep/panel policy to the phone-side keeper. The server
        # powers the device on asynchronously at start; give it a moment.
        QTimer.singleShot(500, self._send_keeper_policy)
        # Follow the phone's media session so OCTAVE's media controls can drive it
        self._client.media_command(MEDIA_WATCH, 1)

    def _on_frame_size(self, w: int, h: int):
        if (w, h) != (self._frame_width, self._frame_height):
            self._frame_width, self._frame_height = w, h
            self.frameSizeChanged.emit(w, h)

    def _on_disconnected(self, reason: str):
        if self._is_stopping:
            return
        self._reset_phone_state()
        self._reset_media()
        was_ready = self._ready
        self._ready = False
        self._is_starting = False
        if self._attaching:
            # The persisted server was gone (or the link is still down): start fresh
            self._attaching = False
            self._persist_scid = ""
            logger.info(f"Reattach failed ({reason}); starting a new session")
            self.isRunningChanged.emit()
            QTimer.singleShot(0, self.startScrcpy)
            return
        if was_ready and self._active_display_size and self._client is not None and self._client.scid:
            # Link drop mid-stream: the server keeps the virtual display for PERSIST_MS
            self._persist_scid = self._client.scid
            self._persist_until = time.monotonic() + PERSIST_MS / 1000.0 - 5.0
        self._set_connection_type("")
        self._set_current_bit_rate(0)
        if not was_ready and reason and not self._stale_retried and ":" in (self._serial or ""):
            # Wireless debugging takes a new port on every Wi-Fi rejoin, but adb
            # keeps listing the old ip:port as a device until it notices it is
            # dead, so the start can pick a stale entry. Drop it, look up the
            # current port over mDNS and start once more.
            self._stale_retried = True
            self._is_starting = True
            stale = self._serial
            self._start_generation += 1
            gen = self._start_generation
            logger.info(f"Phone mirror: start over Wi-Fi failed ({reason}); dropping {stale} and retrying")

            def job():
                self._run_adb_full(["disconnect", stale], timeout=5)
                fresh = self._mdns_connect_address_for(host_of(stale))
                reached = fresh if fresh and self._adb_connect(fresh)[0] else ""

                def done():
                    if gen != self._start_generation:
                        return   # stopped meanwhile
                    self._is_starting = False
                    if reached and reached != stale and self._wireless_address == stale:
                        self._wireless_address = reached
                        self.wirelessAddressChanged.emit(reached)
                    self.startScrcpy()
                self._post.emit(done)

            self._run_wireless_job(job, False)
            return
        self._stale_retried = False
        self.isRunningChanged.emit()
        if reason:
            self.scrcpyError.emit(reason)
        else:
            self.scrcpyStopped.emit()

    @Slot()
    def stopScrcpy(self):
        logger.debug("Stopping phone mirror")
        self._start_generation += 1   # a pending auto-Wi-Fi step does not start a session
        self._is_stopping = True
        self._is_starting = False
        self._ready = False
        self._attaching = False
        self._stale_retried = False
        self._persist_scid = ""
        self._active_display_size = ""
        self._started_auto_size = ""
        self._resize_timer.stop()
        self._set_current_bit_rate(0)
        self._reset_phone_state()
        self._reset_media()
        if self._client is not None:
            client, self._client = self._client, None
            client.stop()
            client.deleteLater()
            self._kill_stale_server()
        self._set_connection_type("")
        self.scrcpyStopped.emit()
        self.isRunningChanged.emit()

    @Slot()
    def cleanup(self):
        """Cleanup when OCTAVE is closing."""
        self._qr_generation += 1   # a QR pairing wait gives up within a second
        self.stopScrcpy()

    # ── Wi-Fi ───────────────────────────────────────────────────────
    # adb itself carries the session over TCP: once the phone is listed by
    # `adb devices` as "ip:port" (adb connect) or as an mDNS name (adb
    # auto-connects phones it has paired with), the client's `adb -s <serial>`
    # push / forward / shell work exactly as over USB. This code only gets the
    # phone into that list and keeps it there.

    @Property(str, notify=wirelessAddressChanged)
    def wirelessAddress(self) -> str:
        """"ip:port" of the phone last connected wirelessly; reconnected automatically."""
        return self._wireless_address

    @Property(str, notify=connectionTypeChanged)
    def connectionType(self) -> str:
        """"usb" | "wifi" for the running session, "" when idle."""
        return self._connection_type

    @Property(bool, notify=wirelessBusyChanged)
    def wirelessBusy(self) -> bool:
        """A pair / connect / scan started from the UI is in progress."""
        return self._wireless_jobs > 0

    def _set_connection_type(self, kind: str):
        if kind != self._connection_type:
            self._connection_type = kind
            self.connectionTypeChanged.emit()

    @Slot(object)
    def _run_posted(self, fn):
        fn()

    def _job_loop(self):
        while True:
            job = self._jobs.get()
            try:
                job()
            except Exception:
                logger.exception("phone mirror: wireless job failed")

    def _run_wireless_job(self, job, ui: bool):
        """Queue job on the wireless worker; ui jobs drive wirelessBusy."""
        if ui:
            self._wireless_jobs += 1
            if self._wireless_jobs == 1:
                self.wirelessBusyChanged.emit()

        def wrapped():
            try:
                job()
            finally:
                if ui:
                    self._post.emit(self._ui_job_done)

        if self._worker is None or not self._worker.is_alive():
            self._worker = threading.Thread(target=self._job_loop, daemon=True, name="phone-mirror-wifi")
            self._worker.start()
        self._jobs.put(wrapped)

    def _ui_job_done(self):
        self._wireless_jobs -= 1
        if self._wireless_jobs == 0:
            self.wirelessBusyChanged.emit()

    def _mdns_services(self) -> list:
        """[(name, type, address)] from `adb mdns services`, e.g.
        ("adb-R5CT…-AbCdEf", "_adb-tls-connect._tcp", "192.168.1.5:37199")."""
        rows = []
        for line in self._run_adb_full(["mdns", "services"], timeout=5).split("\n"):
            parts = line.split()
            if len(parts) >= 3 and parts[1].startswith("_adb"):
                rows.append((parts[0], parts[1], parts[2]))
        return rows or self._avahi_mdns_services()

    @staticmethod
    def _avahi_mdns_services() -> list:
        """Same rows from `avahi-browse`. Debian/Ubuntu's arm64 adb is built
        without working mDNS (`adb mdns services` is always empty) and Google
        ships platform-tools for x86-64 only, so on a Pi the phone is found
        through the system's Avahi. Parsable resolved lines look like
        "=;ap0;IPv4;adb-R3GL…-51sbEx;_adb-tls-connect._tcp;local;Android.local;10.42.0.30;36937;…"""
        avahi = shutil.which("avahi-browse")
        if not avahi:
            return []
        rows = []
        for kind in ("_adb-tls-connect._tcp", "_adb-tls-pairing._tcp"):
            try:
                out = subprocess.run([avahi, "-rpt", kind], stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, timeout=4).stdout
            except subprocess.TimeoutExpired as e:
                out = e.stdout or ""
                out = out.decode(errors="replace") if isinstance(out, bytes) else out
            except (subprocess.SubprocessError, OSError):
                continue
            for line in out.split("\n"):
                f = line.split(";")
                # IPv6 answers are link-local (need a scope id) and repeat the IPv4 ones
                if len(f) < 9 or f[0] != "=" or f[2] != "IPv4":
                    continue
                row = (f[3], f[4], f"{f[7]}:{f[8]}")
                if row not in rows:
                    rows.append(row)
        return rows

    def _mdns_connect_address_for(self, host: str) -> str:
        for _name, kind, address in self._mdns_services():
            if "_adb-tls-connect" in kind and host_of(address) == host:
                return address
        return ""

    def _adb_connect(self, address: str):
        """(ok, message). adb connect exits 0 on failure too; "already
        connected to" counts as success."""
        out = self._run_adb_full(["connect", address], timeout=10)
        ok = "connected to" in out and "failed" not in out and "cannot" not in out
        message = out.split("\n")[-1] if out else f"No answer from {address}"
        return ok, message

    def _enable_tcpip(self, serial: str) -> str:
        """Worker thread: switch a USB phone to adb over TCP. Returns "ip:port",
        "" when the phone has no Wi-Fi address or adb refuses."""
        ip = wifi_address_from_ip_addr(
            self._run_adb_full(["-s", serial, "shell", "ip", "-4", "-o", "addr", "show"], timeout=5))
        if not ip:
            logger.info("Phone mirror: phone has no Wi-Fi address; not enabling adb over Wi-Fi")
            return ""
        port = str(TCPIP_PORT)
        current = self._run_adb_full(["-s", serial, "shell", "getprop", "service.adb.tcp.port"], timeout=5)
        if current.strip() != port:
            # Restarts adbd on the phone: USB drops for a moment and comes back
            logger.info(f"Phone mirror: enabling adb over Wi-Fi on the phone (port {port})")
            out = self._run_adb(["-s", serial, "tcpip", port], timeout=10)
            if not out:
                logger.warning("Phone mirror: adb tcpip failed")
                return ""
            time.sleep(1.0)
            for _ in range(16):
                if self._run_adb_full(["-s", serial, "get-state"], timeout=2).strip() == "device":
                    break
                time.sleep(0.5)
            else:
                logger.warning("Phone mirror: phone did not come back on USB after adb tcpip")
        return f"{ip}:{port}"

    @Slot(str)
    def setWirelessAddress(self, address: str):
        """Remembered phone (from settings at startup); tries to reach it at once."""
        value = (address or "").strip()
        if value == self._wireless_address:
            return
        self._wireless_address = value
        self.wirelessAddressChanged.emit(value)
        if value:
            self._last_auto_connect = 0.0
            self._maybe_reconnect_wireless()

    def _maybe_reconnect_wireless(self):
        if (not self._wireless_address or not self._adb_path or self._wireless_jobs > 0
                or self._auto_connect_running):
            return
        now = time.monotonic()
        if self._last_auto_connect and now - self._last_auto_connect < AUTO_CONNECT_INTERVAL_S:
            return
        self._last_auto_connect = now
        self._auto_connect_running = True
        saved = self._wireless_address

        def job():
            ok, msg = self._adb_connect(saved)
            reached = saved if ok else ""
            if not ok:
                # Wireless debugging picks a new port every time it is switched
                # on; the phone advertises the current one over mDNS.
                fresh = self._mdns_connect_address_for(host_of(saved))
                if fresh and fresh != saved:
                    ok, msg = self._adb_connect(fresh)
                    if ok:
                        reached = fresh
            if reached:
                logger.info(f"Phone mirror: reconnected to the phone over Wi-Fi at {reached}")
            else:
                logger.debug(f"Phone mirror: Wi-Fi phone {saved} not reachable: {msg}")

            def done():
                self._auto_connect_running = False
                # The phone moved to a new port: remember that one
                if reached and reached != saved and self._wireless_address == saved:
                    self._wireless_address = reached
                    self.wirelessAddressChanged.emit(reached)
            self._post.emit(done)

        self._run_wireless_job(job, ui=False)

    def _connect_paired_host(self, host: str) -> str:
        """After a successful adb pair: the connect port differs from the
        pairing port; the phone advertises it over mDNS (polled up to 8 s).
        adb may already have auto-connected ("already connected" then).
        Returns the address reached, "" if none."""
        for _ in range(16):
            connect_addr = self._mdns_connect_address_for(host)
            if connect_addr and self._adb_connect(connect_addr)[0]:
                return connect_addr
            time.sleep(0.5)
        return ""

    @Slot()
    def forgetWireless(self):
        """Disconnect the Wi-Fi phone and stop reconnecting to it."""
        addr = self._wireless_address
        if self._connection_type == "wifi":
            self.stopScrcpy()
        self._wireless_address = ""
        self.wirelessAddressChanged.emit("")
        if not addr:
            return
        logger.info(f"Phone mirror: forgetting the Wi-Fi phone at {addr}")
        self._run_wireless_job(lambda: self._run_adb_full(["disconnect", addr], timeout=5), ui=False)

    # ── QR pairing ──
    # Android Studio's flow: the host shows WIFI:T:ADB;S:<name>;P:<password>;;
    # the phone's "Pair device with QR code" scanner reads it and advertises an
    # _adb-tls-pairing service under <name>; the host then runs
    # `adb pair <ip:port> <password>` against it and connects as usual.

    @Property(list, notify=qrPairingChanged)
    def qrPairingRows(self) -> list:
        """The QR code's modules, one string of '0'/'1' per row (empty when no
        pairing is waiting), for QML to draw."""
        return self._qr_rows

    @Property(bool, notify=qrPairingChanged)
    def qrPairingActive(self) -> bool:
        return bool(self._qr_rows)

    @Slot()
    def startQrPairing(self):
        """Show a QR code for the phone's "Pair device with QR code" scanner,
        wait for the phone to advertise it over mDNS, then pair and connect.
        Outcome via wirelessResult; qrPairingRows clears when it ends."""
        if not self._adb_path:
            self.wirelessResult.emit(False, self.describeDeviceState("no-adb"))
            return
        if adb_tools_major(self._adb_path) < ADB_WIRELESS_MIN_TOOLS:
            logger.warning(f"Phone mirror: QR pairing, adb {self._adb_path} is too old to pair")
            self.wirelessResult.emit(False, f"adb is too old for wireless pairing (needs platform-tools "
                                            f"{ADB_WIRELESS_MIN_TOOLS} or newer). Install a newer adb, or pair over USB.")
            return
        service = "octave-" + _random_token(8)
        password = _random_token(12)
        qr = QrCode.encode_text(qr_pairing_payload(service, password), QrCode.Ecc.MEDIUM)
        size = qr.get_size()
        self._qr_rows = ["".join("1" if qr.get_module(x, y) else "0" for x in range(size)) for y in range(size)]
        self.qrPairingChanged.emit()
        self._qr_generation += 1
        generation = self._qr_generation
        logger.info(f"Phone mirror: QR pairing, waiting for the phone to scan (service {service})")

        def job():
            deadline = time.monotonic() + QR_PAIRING_TIMEOUT_S
            pair_addr = ""
            while not pair_addr and self._qr_generation == generation and time.monotonic() < deadline:
                for name, kind, address in self._mdns_services():
                    if name == service and "_adb-tls-pairing" in kind:
                        pair_addr = address
                if not pair_addr:
                    time.sleep(1.0)
            if self._qr_generation != generation:
                return   # cancelled or restarted: the newer request reports
            ok, reached, detail = False, "", ""
            if not pair_addr:
                detail = f"The phone did not scan the code within {int(QR_PAIRING_TIMEOUT_S)} s."
            else:
                out = self._run_adb_full(["pair", pair_addr, password], timeout=20)
                if "Successfully paired" in out:
                    ok = True
                    reached = self._connect_paired_host(host_of(pair_addr))
                else:
                    detail = out.split("\n")[-1] if out else "no answer"

            def done():
                if self._qr_generation != generation:
                    return
                self._finish_qr_pairing()
                if not ok:
                    logger.warning(f"Phone mirror: QR pairing failed: {detail}")
                    self.wirelessResult.emit(False, detail + " Check that the phone is on the same Wi-Fi network "
                                                             "and try again." if not pair_addr
                                             else f"Pairing failed. Try again. ({detail})")
                elif not reached:
                    self.wirelessResult.emit(False, "Paired, but the phone did not accept a connection. Keep its "
                                                    "Wireless debugging screen open and try again.")
                else:
                    logger.info(f"Phone mirror: QR pairing, paired and connected over Wi-Fi at {reached}")
                    self.setWirelessAddress(reached)
                    self.wirelessResult.emit(True, f"Paired and connected to {reached} over Wi-Fi.")
            self._post.emit(done)

        self._run_wireless_job(job, ui=True)

    @Slot()
    def cancelQrPairing(self):
        if not self._qr_rows:
            return
        self._qr_generation += 1   # the waiting job notices within a second and returns
        self._finish_qr_pairing()

    def _finish_qr_pairing(self):
        if self._qr_rows:
            self._qr_rows = []
            self.qrPairingChanged.emit()
