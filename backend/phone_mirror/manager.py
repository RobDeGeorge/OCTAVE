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

import os
import platform
import re
import shutil
import subprocess
import threading
from pathlib import Path
from typing import Optional

from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer

from backend.logging_config import get_logger

from backend.phone_mirror.scrcpy_client import (
    ScrcpyClient, HAVE_AV, bundled_server_jar, SERVER_VERSION, SERVER_PROCESS_PATTERN,
    KEYCODE_HOME, KEYCODE_BACK, KEYCODE_APP_SWITCH, KEYCODE_WAKEUP,
)

logger = get_logger(__name__)

# Default size of the virtual display scrcpy creates on the phone
# (--new-display, Android 11+). Landscape suits a dash screen. scrcpy rounds
# each dimension down to a multiple of 8, so the setting is snapped the same
# way to keep the UI honest.
DEFAULT_DISPLAY_SIZE = "1280x800"
NEW_DISPLAY_MIN_SDK = 30  # Android 11
# How often the phone's wakefulness is polled while a session is up. A
# locked (dozing) phone keeps the stream open but stops compositing the
# virtual display and drops injected touch, with no signal on the wire, so
# the only way to notice is to ask (`dumpsys power`).
WAKE_POLL_INTERVAL_S = 1.0
# After a power press the phone is woken but its panel is left lit for this
# long. If the user unlocks it in that window they want their phone, not the
# mirror's screen-off; otherwise the panel is blanked again.
WAKE_GRACE_MS = 5000


def normalize_display_size(value: str) -> str:
    """Validate "WxH"; snap both dimensions down to multiples of 8 (min 8).
    Returns "" for empty/invalid input (= mirror the phone's own screen)."""
    m = re.fullmatch(r"\s*(\d+)\s*[xX]\s*(\d+)\s*", value or "")
    if not m:
        return ""
    w, h = int(m.group(1)), int(m.group(2))
    w = max(8, (w // 8) * 8)
    h = max(8, (h // 8) * 8)
    return f"{w}x{h}"


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


def _find_adb() -> Optional[str]:
    """Bundled platform-tools first, then PATH, then common install locations."""
    bundled = _bundled_adb()
    if bundled:
        logger.debug(f" Using bundled platform-tools adb: {bundled}")
        return bundled
    in_path = shutil.which("adb")
    if in_path:
        return in_path
    if platform.system() == "Windows":
        common = [
            r"C:\Program Files\Android\platform-tools\adb.exe",
            r"C:\Program Files (x86)\Android\platform-tools\adb.exe",
            str(Path.home() / "AppData" / "Local" / "Android" / "Sdk" / "platform-tools" / "adb.exe"),
        ]
    else:
        common = [
            "/usr/bin/adb",
            "/usr/local/bin/adb",
            str(Path.home() / "Android" / "Sdk" / "platform-tools" / "adb"),
        ]
    for path in common:
        if os.path.exists(path):
            return path
    return None


class PhoneMirrorManager(QObject):
    """
    Owns the single mirroring session (one phone at a time), the adb probes
    the UI needs, and the settings that shape the session.
    """

    error = Signal(str)

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
    phoneAsleepChanged = Signal(bool)   # the phone was put to sleep (locked) during a session
    phoneInUseChanged = Signal(bool)    # the user unlocked the phone: OCTAVE stops blanking / waking it
    _wakefulness = Signal(str, int)     # poll thread -> GUI thread: ("Awake"|"Dozing"|"Asleep"|"", deviceLocked 0/1/-1)
    # Emitted whenever duckingFactor changes; main.py routes it to the other
    # audio sources (MediaManager.setDucking).
    duckingChanged = Signal(float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self._adb_path: Optional[str] = _find_adb()
        self._audio_enabled: bool = False
        self._volume: float = 1.0
        self._display_size: str = DEFAULT_DISPLAY_SIZE
        self._active_display_size: str = ""

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
        self._phone_screen_off: bool = True  # blank the phone's panel while mirroring (setting scrcpyPhoneScreenOff)
        self._phone_asleep: bool = False
        self._phone_in_use: bool = False
        self._serial: str = ""
        self._vdisplay_id: int = -1          # id of the --new-display virtual display, from the server log
        self._prev_locked: Optional[int] = None   # deviceLocked at the previous poll; None before the first
        self._blank_on_wake: bool = False        # the doze ended an in-use spell: user is putting the phone down
        self._panel_off: bool = False            # last panel state we asked for this session
        self._grace = QTimer(self)
        self._grace.setSingleShot(True)
        self._grace.setInterval(WAKE_GRACE_MS)
        self._grace.timeout.connect(self._on_grace_expired)
        self._wake_stop = threading.Event()
        self._wake_thread: Optional[threading.Thread] = None
        self._wakefulness.connect(self._on_wakefulness)

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
                "over USB and accept the authorization prompt.")

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

    @Slot(result=bool)
    def hasConnectedDevice(self) -> bool:
        return self.getDeviceState() == "device"

    @Slot(result=str)
    def getDeviceSerial(self) -> str:
        for serial, st in self._devices():
            if st == "device":
                return serial
        return ""

    @Slot(result=str)
    def getDeviceName(self) -> str:
        return self._run_adb(["shell", "getprop", "ro.product.model"]).strip()

    @Slot(result=str)
    def getDeviceResolution(self) -> str:
        for line in self._run_adb(["shell", "wm", "size"]).split("\n"):
            if "Physical size:" in line:
                return line.split(":", 1)[1].strip()
        return ""

    @Slot(result=int)
    def getDeviceSdk(self) -> int:
        try:
            return int(self._run_adb(["shell", "getprop", "ro.build.version.sdk"]).strip())
        except (TypeError, ValueError):
            return 0

    def _kill_stale_server(self):
        """A crashed session can leave the device-side server running."""
        self._run_adb(["shell", "pkill", "-f", SERVER_PROCESS_PATTERN], timeout=5)

    # ── settings ────────────────────────────────────────────────────

    @Slot(float)
    def setVolume(self, volume: float):
        """Called by VolumeController.applyVolume() on every volume change
        (0..1 linear). Phone audio played through OCTAVE follows it. Must
        exist even when audio is off: VolumeController calls it unconditionally."""
        self._volume = volume
        if self._client is not None:
            self._client.set_volume(volume)

    @Slot(bool)
    def setAudioEnabled(self, enabled: bool):
        """Play the phone's audio through OCTAVE (setting scrcpyAudioEnabled).
        Restarts the session if one is running, like the display size."""
        enabled = bool(enabled)
        if enabled == self._audio_enabled:
            return
        self._audio_enabled = enabled
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

    @Property(bool, notify=phoneAsleepChanged)
    def phoneAsleep(self) -> bool:
        """True while the phone is dozing (power button / lock) during a session.
        Its virtual display stops compositing and drops touch; OCTAVE wakes it."""
        return self._phone_asleep

    @Slot(bool)
    def setPhoneScreenOff(self, off: bool):
        """Setting scrcpyPhoneScreenOff: keep the phone's own panel dark while
        mirroring. The stream and touch keep working (SurfaceControl power
        mode, not sleep); the server restores the panel when the session ends."""
        off = bool(off)
        if off == self._phone_screen_off:
            return
        self._phone_screen_off = off
        if self._client is not None and self._client.is_running and self._ready:
            self._set_panel(not off)

    @Property(bool, notify=phoneInUseChanged)
    def phoneInUse(self) -> bool:
        """True after the user unlocked the phone during a session: the panel is
        left alone and a lock is not undone, until they lock it again or tap
        resumeMirroring(). The mirror itself keeps working while it is awake."""
        return self._phone_in_use

    @Slot()
    def resumeMirroring(self):
        """Take the phone back: leave the in-use state, wake it if needed and
        re-apply the screen-off setting."""
        self._set_in_use(False)
        self._grace.stop()
        if self._phone_asleep:
            self.wakePhone()
        else:
            self._apply_screen_off()

    def _set_in_use(self, in_use: bool):
        if in_use == self._phone_in_use:
            return
        self._phone_in_use = in_use
        if in_use and self._ready:
            self._set_panel(True)   # they are holding it: never leave it dark
        logger.info("Phone mirror: phone unlocked by the user; leaving its screen alone" if in_use
                    else "Phone mirror: phone handed back to the mirror")
        self.phoneInUseChanged.emit(in_use)

    def _on_grace_expired(self):
        if not self._phone_in_use and not self._phone_asleep:
            self._apply_screen_off()

    @Slot()
    def wakePhone(self):
        """Wake a sleeping phone (KEYCODE_WAKEUP never toggles it off)."""
        if not self._serial:
            return
        logger.info("Phone mirror: waking phone")
        serial = self._serial
        threading.Thread(
            target=self._run_adb, daemon=True, name="phone-mirror-wake",
            args=(["-s", serial, "shell", "input", "keyevent", str(KEYCODE_WAKEUP)],), kwargs={"timeout": 5},
        ).start()

    def _set_panel(self, on: bool):
        """Send SET_DISPLAY_POWER once per state change; the server logs each one."""
        if self._client is None or not self._client.is_running:
            return
        if self._panel_off == (not on):
            return
        self._panel_off = not on
        self._client.set_display_power(on)

    def _apply_screen_off(self):
        if self._ready and self._phone_screen_off:
            self._set_panel(False)

    def _start_wake_watch(self, serial: str):
        self._stop_wake_watch()
        self._wake_stop = threading.Event()
        t = threading.Thread(target=self._wake_watch, args=(serial, self._wake_stop),
                             daemon=True, name="phone-mirror-wake-watch")
        self._wake_thread = t
        t.start()

    def _stop_wake_watch(self):
        self._wake_stop.set()
        self._wake_thread = None
        self._grace.stop()
        self._set_in_use(False)
        self._prev_locked = None
        self._blank_on_wake = False
        self._panel_off = False
        if self._phone_asleep:
            self._phone_asleep = False
            self.phoneAsleepChanged.emit(False)

    def _wake_watch(self, serial: str, stop: threading.Event):
        # No `grep -m1`: closing the pipe early makes dumpsys log a broken-pipe
        # error on the phone every poll. grep reads it all; re.search takes the first.
        # One adb round trip per poll: wakefulness plus the keyguard state
        # (`dumpsys trust` prints deviceLocked=0 once the user has authenticated).
        cmd = ["-s", serial, "shell",
               "dumpsys power | grep mWakefulness=; dumpsys trust | grep deviceLocked"]
        # First poll after 1 s (the server powers the device on asynchronously
        # at start); it decides the initial panel state. Then every 2 s.
        wait = 1.0
        while not stop.wait(wait):
            wait = WAKE_POLL_INTERVAL_S
            out = self._run_adb(cmd, timeout=5)
            m = re.search(r"mWakefulness=(\w+)", out)
            # The current user's line; a work profile / Secure Folder prints
            # its own deviceLocked, which is not the keyguard we care about.
            lk = re.search(r"\(current\)[^\n]*deviceLocked=(\d)", out) or re.search(r"deviceLocked=(\d)", out)
            if not stop.is_set():
                self._wakefulness.emit(m.group(1) if m else "", int(lk.group(1)) if lk else -1)

    def _on_wakefulness(self, state: str, locked: int):
        if not state or self._client is None or not self._client.is_running:
            return
        asleep = state != "Awake"
        if asleep != self._phone_asleep:
            self._phone_asleep = asleep
            self.phoneAsleepChanged.emit(asleep)
            if asleep:
                # stay_awake means the phone never idles off: this is a power
                # press. Wake it, then let the grace window tell us whether the
                # user wanted the screen off or wanted their phone -- unless
                # they were using it, in which case they are putting it down.
                self._grace.stop()
                self._blank_on_wake = self._phone_in_use and self._phone_screen_off
                self._set_in_use(False)
                if self._client is not None:
                    self._client.set_hold_black(True)   # keep the last good frame on the dash
                self._panel_off = False   # the wake powers the panel on; our mode is reverted
                logger.info(f"Phone mirror: phone went to sleep ({state}); waking it")
                if self._blank_on_wake:
                    self._apply_screen_off()   # may survive the wake; re-applied below if not
                self.wakePhone()
            else:
                logger.info("Phone mirror: phone is awake again")
                if self._client is not None:
                    self._client.set_hold_black(False)
                if self._blank_on_wake:
                    self._blank_on_wake = False
                    self._apply_screen_off()
                elif self._phone_screen_off and not self._phone_in_use:
                    self._grace.start()
            return
        if asleep:
            return
        prev, self._prev_locked = self._prev_locked, (locked if locked >= 0 else self._prev_locked)
        if prev is None:
            # First poll of the session: an unlocked phone is in the user's
            # hand, a locked one is in the cradle and gets the screen-off.
            if locked == 0:
                self._set_in_use(True)
            else:
                self._apply_screen_off()
            return
        # Edge-triggered on the unlock itself (1 -> 0). Blanking the panel does
        # not lock the phone, so a level check would undo resumeMirroring() on
        # the next poll; and phones with a lock-after delay stay deviceLocked=0
        # for a while after a power press, which is not an unlock either.
        if locked == 0 and prev == 1 and not self._phone_in_use:
            self._grace.stop()
            self._set_in_use(True)
        elif locked == 1 and prev == 0 and self._phone_in_use:
            # Locked without sleeping (lock shortcut / lock-after delay elapsed)
            self._set_in_use(False)
            if self._phone_screen_off:
                self._grace.start()

    @Property(str, notify=displaySizeChanged)
    def displaySize(self) -> str:
        """Requested virtual display size "WxH" (--new-display), "" = phone screen."""
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
        if self.isRunning:
            self.stopScrcpy()
            QTimer.singleShot(300, self.startScrcpy)

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
            self.scrcpyError.emit(self.describeDeviceState(state))
            return
        serial = self.getDeviceSerial()

        self._is_starting = True
        self._is_stopping = False
        self._ready = False
        self._active_display_size = self._display_size
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
        client.serverLog.connect(self._on_server_log)
        client.set_audio_gain(self._audio_gain)
        client.set_volume(self._volume)
        self._client = client

        display_size = self._display_size
        if display_size:
            sdk = self.getDeviceSdk()
            if 0 < sdk < NEW_DISPLAY_MIN_SDK:
                logger.warning(f"Device SDK {sdk} < {NEW_DISPLAY_MIN_SDK}: virtual display unavailable, mirroring phone screen")
                display_size = ""
                self._active_display_size = ""
        logger.info(f"Starting phone mirror (server {SERVER_VERSION}) for {serial} "
                    f"(display {display_size or 'phone screen'}, audio {'on' if self._audio_enabled else 'off'})")
        self._serial = serial
        self._vdisplay_id = -1
        client.start(serial, display_size=display_size, audio=self._audio_enabled)
        self.isRunningChanged.emit()

    def _on_connected(self, w: int, h: int):
        self._ready = True
        self._is_starting = False
        self._frame_width, self._frame_height = w, h
        if self._active_display_size:
            self._active_display_size = f"{w}x{h}"
        self.frameSizeChanged.emit(w, h)
        logger.info(f"Phone mirror connected: {w}x{h}")
        self.scrcpyStarted.emit(1)
        # The first wakefulness poll (1 s in) decides the initial panel state.
        self._start_wake_watch(self._serial)

    def _on_frame_size(self, w: int, h: int):
        if (w, h) != (self._frame_width, self._frame_height):
            self._frame_width, self._frame_height = w, h
            self.frameSizeChanged.emit(w, h)

    def _on_disconnected(self, reason: str):
        if self._is_stopping:
            return
        self._stop_wake_watch()
        self._ready = False
        self._is_starting = False
        self.isRunningChanged.emit()
        if reason:
            self.scrcpyError.emit(reason)
        else:
            self.scrcpyStopped.emit()

    @Slot()
    def stopScrcpy(self):
        logger.debug("Stopping phone mirror")
        self._is_stopping = True
        self._is_starting = False
        self._ready = False
        self._active_display_size = ""
        self._stop_wake_watch()
        if self._client is not None:
            client, self._client = self._client, None
            client.stop()
            client.deleteLater()
            self._kill_stale_server()
        self.scrcpyStopped.emit()
        self.isRunningChanged.emit()

    @Slot()
    def cleanup(self):
        """Cleanup when OCTAVE is closing."""
        self.stopScrcpy()
