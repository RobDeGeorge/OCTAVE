"""
Phone Mirror Manager for OCTAVE.

Handles scrcpy-based phone screen mirroring setup and configuration.
Owns the singleton scrcpy process to prevent multiple instances.
"""

import re
import subprocess
import shutil
import platform
import os
import sys
import time
from pathlib import Path
from threading import Thread
from typing import Optional

from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer

from backend.logging_config import get_logger
from backend.phone_mirror.scrcpy_client import (
    ScrcpyClient, HAVE_AV, bundled_server_jar, SERVER_VERSION,
    ACTION_DOWN, ACTION_UP, ACTION_MOVE, KEYCODE_HOME, KEYCODE_BACK, KEYCODE_APP_SWITCH,
)

logger = get_logger(__name__)

# Oldest scrcpy whose CLI we drive (--video-codec, --no-audio, --no-window,
# --v4l2-sink all landed in 2.0). Anything older is rejected with a clear
# error instead of a cryptic "unrecognized option" from the process.
MIN_SCRCPY_VERSION = (2, 0)

# Default V4L2 loopback node scrcpy streams into on Linux
# (modprobe v4l2loopback exclusive_caps=0 card_label=OCTAVE video_nr=10).
DEFAULT_VIDEO_DEVICE = "/dev/video10"

# Default size of the virtual display scrcpy creates on the phone
# (--new-display, Android 11+). Landscape suits a dash screen. scrcpy rounds
# each dimension down to a multiple of 8 (1080x2316 becomes 1080x2312), so
# the setting is snapped the same way to keep the UI honest; the reader
# still probes the node for the real size before parsing frames.
DEFAULT_DISPLAY_SIZE = "1280x800"
NEW_DISPLAY_MIN_SDK = 30  # Android 11


def normalize_display_size(value: str) -> str:
    """Validate "WxH"; snap both dimensions down to multiples of 8 (min 8),
    which is what scrcpy does to a --new-display size anyway.
    Returns "" for empty/invalid input (= mirror the phone's own screen)."""
    m = re.fullmatch(r"\s*(\d+)\s*[xX]\s*(\d+)\s*", value or "")
    if not m:
        return ""
    w, h = int(m.group(1)), int(m.group(2))
    w = max(8, (w // 8) * 8)
    h = max(8, (h // 8) * 8)
    return f"{w}x{h}"


def die_with_parent(sig: int = 15):
    """Popen preexec_fn (Linux): have the kernel signal the child if OCTAVE
    dies without running cleanup (SIGKILL, crash), so scrcpy/ffmpeg never
    outlive the app and hold /dev/videoN or a virtual display on the phone.

    scrcpy gets SIGTERM (15) so it tears down its virtual display; ffmpeg
    gets SIGKILL (9) because, blocked in the v4l2 driver on a starved
    device, it ignores SIGTERM entirely.
    """
    if platform.system() != "Linux":
        return
    try:
        import ctypes
        libc = ctypes.CDLL("libc.so.6", use_errno=True)
        PR_SET_PDEATHSIG = 1
        libc.prctl(PR_SET_PDEATHSIG, sig)
    except Exception:
        pass


def die_with_parent_kill():
    die_with_parent(9)


def _default_capture_mode() -> str:
    """How the mirrored video reaches QML on this platform.

    window  - scrcpy opens a window that ScrcpyCapture screen-grabs (Windows)
    v4l2    - scrcpy runs headless into a v4l2loopback device that QML shows
              through QtMultimedia Camera/VideoOutput (Linux)
    unsupported - neither path is available (macOS)
    """
    system = platform.system()
    if system == "Windows":
        return "window"
    if system == "Linux":
        return "v4l2"
    return "unsupported"


def _get_bundled_tools_dir() -> Path:
    """Get the path to bundled tools directory.

    Works both in development and when packaged with PyInstaller.
    """
    if getattr(sys, 'frozen', False):
        # Running as compiled executable (PyInstaller)
        base_path = Path(sys._MEIPASS)
    else:
        # Running in development
        base_path = Path(__file__).parent.parent.parent

    return base_path / "tools" / "scrcpy"

if platform.system() == "Windows":
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32

    # Function signatures for window enumeration
    user32.EnumWindows.argtypes = [ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM), wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL


class PhoneMirrorManager(QObject):
    """
    Manager for phone screen mirroring configuration in OCTAVE.

    Handles scrcpy path detection, ADB device checks, and OWNS the scrcpy process.
    Only one scrcpy instance can run at a time (singleton pattern).
    """

    # Signals
    scrcpyPathChanged = Signal()
    error = Signal(str)

    # Signals for scrcpy process state
    scrcpyStarted = Signal(int)  # Emits the window handle (or PID on Linux) when scrcpy is ready
    scrcpyStopped = Signal()
    scrcpyError = Signal(str)
    isRunningChanged = Signal()
    videoDeviceChanged = Signal()
    displaySizeChanged = Signal()
    displayIdChanged = Signal()
    nativeModeChanged = Signal()
    frameSizeChanged = Signal(int, int)  # native mode: decoded frame size
    frameReady = Signal()                # native mode: a frame reached the video sink

    def __init__(self, parent=None):
        super().__init__(parent)

        self._scrcpy_path: Optional[str] = None
        self._custom_scrcpy_path: str = ""
        self._adb_path: Optional[str] = None
        self._audio_enabled: bool = False
        self._scrcpy_version: str = ""
        self._capture_mode: str = _default_capture_mode()
        self._video_device: str = DEFAULT_VIDEO_DEVICE
        self._display_size: str = DEFAULT_DISPLAY_SIZE
        self._display_id: int = -1          # Android display id scrcpy is mirroring (-1 = default)
        self._active_display_size: str = ""  # "" when mirroring the phone's own screen

        # Built-in scrcpy-protocol client (docs/PHONE_MIRROR_NATIVE_PLAN.md)
        self._native_mode: bool = False
        self._client: Optional[ScrcpyClient] = None
        self._video_sink = None
        self._frame_width: int = 0
        self._frame_height: int = 0

        # Singleton scrcpy process
        self._process: Optional[subprocess.Popen] = None
        self._scrcpy_hwnd: Optional[int] = None
        self._is_starting: bool = False
        self._is_stopping: bool = False
        self._ready: bool = False
        self._stderr_tail: list = []

        # Find executables (auto-detect)
        self._scrcpy_path = self._find_scrcpy()
        self._adb_path = self._find_adb()

    @Property(bool, notify=scrcpyPathChanged)
    def isScrcpyInstalled(self) -> bool:
        return self._get_effective_scrcpy_path() is not None

    @Property(str, notify=scrcpyPathChanged)
    def scrcpyPath(self) -> str:
        return self._get_effective_scrcpy_path() or ""

    @Property(str, notify=scrcpyPathChanged)
    def scrcpyVersion(self) -> str:
        """Version string of the effective scrcpy binary ("3.3.1"), or "" if unknown."""
        return self._scrcpy_version

    @Property(str, constant=True)
    def captureMode(self) -> str:
        """'window', 'v4l2' or 'unsupported' — see _default_capture_mode()."""
        return self._capture_mode

    @Property(str, notify=videoDeviceChanged)
    def videoDevice(self) -> str:
        """V4L2 loopback node used in 'v4l2' capture mode."""
        return self._video_device

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

    @Property(int, notify=displayIdChanged)
    def displayId(self) -> int:
        """Android display id of the running mirror (-1 = the phone's main display).
        Touch injection must target this display (`adb shell input -d ID ...`)."""
        return self._display_id

    @Property(str, notify=displayIdChanged)
    def activeDisplaySize(self) -> str:
        """"WxH" of the virtual display actually in use, "" if mirroring the phone screen."""
        return self._active_display_size

    @Slot(result=bool)
    def environmentOk(self) -> bool:
        """True when scrcpy is installed, new enough and (on Linux) the video
        node exists — i.e. a failure is about the phone, not the setup, so
        the view should not show install instructions."""
        if self._native_mode:
            return self.nativeAvailable
        if not self._get_effective_scrcpy_path() or self._version_too_old():
            return False
        if self._capture_mode == "v4l2" and not self.videoDeviceExists():
            return False
        return True

    @Slot(result=bool)
    def videoDeviceExists(self) -> bool:
        """True if the v4l2loopback node exists on disk (module loaded)."""
        return os.path.exists(self._video_device)

    def _kill_stale_server(self):
        """scrcpy's device-side server can outlive the client (seen after a
        crashed 1.21 run); a stale one conflicts with the next session."""
        Thread(target=self._run_adb, args=(["shell", "pkill", "-f", "com.genymobile.scrcpy"],),
               daemon=True).start()

    @Slot(result=int)
    def getDeviceSdk(self) -> int:
        out = self._run_adb(["shell", "getprop", "ro.build.version.sdk"])
        try:
            return int(out.strip())
        except (TypeError, ValueError):
            return 0

    def _run_adb(self, args, timeout: int = 10) -> str:
        if not self._adb_path:
            return ""
        try:
            creationflags = 0
            if platform.system() == "Windows":
                creationflags = subprocess.CREATE_NO_WINDOW
            result = subprocess.run([self._adb_path] + list(args), capture_output=True,
                                    text=True, timeout=timeout, creationflags=creationflags)
            return result.stdout if result.returncode == 0 else ""
        except (subprocess.SubprocessError, OSError):
            return ""

    def _find_scrcpy_display_id(self) -> int:
        """Fallback when scrcpy's stderr didn't reveal the new display id:
        look for the display named "scrcpy" in dumpsys and return its id."""
        out = self._run_adb(["shell", "dumpsys", "display"])
        if not out:
            return -1
        # Logical display blocks look like:  Display 5:\n ... mDisplayInfo=... "scrcpy" ...
        for block in re.split(r"\n(?=\s*Display \d+:)", out):
            m = re.match(r"\s*Display (\d+):", block)
            if m and "scrcpy" in block:
                return int(m.group(1))
        # Older format: DisplayDeviceInfo{"scrcpy": ... } followed by mDisplayId=N
        m = re.search(r'"scrcpy".{0,2000}?mDisplayId=(\d+)', out, re.S)
        return int(m.group(1)) if m else -1

    # ── Built-in client (native mode) ────────────────────────────────

    @Property(bool, notify=nativeModeChanged)
    def nativeMode(self) -> bool:
        """Use OCTAVE's own scrcpy-protocol client (no scrcpy binary, no v4l2)."""
        return self._native_mode

    @Property(bool, constant=True)
    def nativeAvailable(self) -> bool:
        """PyAV importable, bundled server jar found, adb found."""
        return bool(HAVE_AV and bundled_server_jar() and self._adb_path)

    @Property(str, constant=True)
    def serverVersion(self) -> str:
        return SERVER_VERSION

    @Slot(bool)
    def setNativeMode(self, enabled: bool):
        enabled = bool(enabled)
        if enabled == self._native_mode:
            return
        if enabled and not self.nativeAvailable:
            logger.warning("Native phone mirror requested but unavailable "
                           f"(av={HAVE_AV}, jar={bundled_server_jar()}, adb={self._adb_path})")
        was_running = self.isRunning
        if was_running:
            self.stopScrcpy()
        self._native_mode = enabled
        self.nativeModeChanged.emit()
        if was_running:
            QTimer.singleShot(300, self.startScrcpy)

    @Property(QObject, notify=nativeModeChanged)
    def videoSink(self):
        return self._video_sink

    @videoSink.setter
    def videoSink(self, sink):
        """QML binds VideoOutput.videoSink here; decoded frames go straight to it."""
        self._video_sink = sink
        if self._client is not None:
            self._client.set_video_sink(sink)

    @Property(int, notify=frameSizeChanged)
    def frameWidth(self) -> int:
        return self._frame_width

    @Property(int, notify=frameSizeChanged)
    def frameHeight(self) -> int:
        return self._frame_height

    @Slot(int, int, float, float)
    def injectTouch(self, pointer_id: int, action: int, rel_x: float, rel_y: float):
        """Touch on the mirrored display. action: 0 down, 1 up, 2 move; rel_x/rel_y 0..1."""
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
        self.injectKey(KEYCODE_HOME)

    @Slot()
    def pressBack(self):
        self.injectKey(KEYCODE_BACK)

    @Slot()
    def pressAppSwitch(self):
        self.injectKey(KEYCODE_APP_SWITCH)

    def _start_native(self, serial: str):
        jar = bundled_server_jar()
        if not HAVE_AV:
            self.scrcpyError.emit("Built-in phone mirror needs the Python package 'av' (pip install av)")
            return
        if not jar:
            self.scrcpyError.emit("Bundled scrcpy server not found (tools/scrcpy-server/)")
            return
        self._is_starting = True
        self._is_stopping = False
        self._ready = False
        self._display_id = -1
        self._active_display_size = self._display_size
        if self._client is not None:
            self._client.stop()
        client = ScrcpyClient(self._adb_path, jar, self)
        client.set_video_sink(self._video_sink)
        client.connected.connect(self._on_native_connected)
        client.disconnected.connect(self._on_native_disconnected)
        client.frameSizeChanged.connect(self._on_native_frame_size)
        client.frameReady.connect(self.frameReady)
        self._client = client
        # --new-display needs Android 11+; older phones mirror their screen
        display_size = self._display_size
        if display_size:
            sdk = self.getDeviceSdk()
            if 0 < sdk < NEW_DISPLAY_MIN_SDK:
                logger.warning(f"Device SDK {sdk} < {NEW_DISPLAY_MIN_SDK}: virtual display unavailable")
                display_size = ""
                self._active_display_size = ""
        logger.info(f"Starting built-in scrcpy client {SERVER_VERSION} for {serial} "
                    f"(display {display_size or 'phone screen'})")
        # Audio forwarding is not implemented in the built-in client yet
        # (the socket would be connected and drained, but nothing plays it).
        if self._audio_enabled:
            logger.info("Built-in client: audio forwarding not implemented yet, mirroring video only")
        client.start(serial, display_size=display_size, audio=False)
        self.isRunningChanged.emit()

    def _on_native_connected(self, w: int, h: int):
        self._ready = True
        self._is_starting = False
        self._scrcpy_hwnd = 1  # non-zero "handle" keeps the QML contract
        self._frame_width, self._frame_height = w, h
        self.frameSizeChanged.emit(w, h)
        if self._active_display_size:
            self._active_display_size = f"{w}x{h}"
        logger.info(f"Built-in client connected: {w}x{h}")
        self.scrcpyStarted.emit(self._scrcpy_hwnd)

    def _on_native_frame_size(self, w: int, h: int):
        if (w, h) != (self._frame_width, self._frame_height):
            self._frame_width, self._frame_height = w, h
            self.frameSizeChanged.emit(w, h)

    def _on_native_disconnected(self, reason: str):
        if self._is_stopping:
            return
        self._ready = False
        self._is_starting = False
        self._scrcpy_hwnd = None
        self.isRunningChanged.emit()
        if reason:
            self.scrcpyError.emit(reason)
        else:
            self.scrcpyStopped.emit()

    @Slot(str)
    def setVideoDevice(self, path: str):
        path = (path or "").strip() or DEFAULT_VIDEO_DEVICE
        if path == self._video_device:
            return
        logger.debug(f" Video device: {path}")
        self._video_device = path
        self.videoDeviceChanged.emit()
        if self.isRunning and self._capture_mode == "v4l2":
            self.stopScrcpy()
            QTimer.singleShot(300, self.startScrcpy)

    def _get_effective_scrcpy_path(self) -> Optional[str]:
        """Get the scrcpy path to use - custom if set, otherwise auto-detected."""
        if self._custom_scrcpy_path and self._check_scrcpy(self._custom_scrcpy_path):
            return self._custom_scrcpy_path
        return self._scrcpy_path

    @Slot(str)
    def setScrcpyPath(self, path: str):
        """Set a custom scrcpy path."""
        logger.debug(f" Setting custom scrcpy path: {path}")
        old_effective = self._get_effective_scrcpy_path()
        self._custom_scrcpy_path = path.strip()

        if not self._custom_scrcpy_path:
            self._scrcpy_path = self._find_scrcpy()

        new_effective = self._get_effective_scrcpy_path()
        if old_effective != new_effective:
            self.scrcpyPathChanged.emit()
            logger.debug(f" Effective scrcpy path: {new_effective}")

    @Slot(bool)
    def setAudioEnabled(self, enabled: bool):
        """Set whether audio forwarding is enabled. Restarts scrcpy if running."""
        if self._audio_enabled == enabled:
            return

        logger.debug(f" Audio forwarding: {enabled}")
        self._audio_enabled = enabled

        # If scrcpy is currently running, restart it with new audio setting
        if self.isRunning:
            logger.debug("Restarting scrcpy to apply audio setting change...")
            self.stopScrcpy()
            # Small delay to ensure clean shutdown before restart
            QTimer.singleShot(300, self.startScrcpy)

    @Slot(float)
    def setVolume(self, volume: float):
        """Placeholder for volume control - not implemented."""
        # Volume control removed for simplicity
        pass

    def _find_scrcpy(self) -> Optional[str]:
        """Find scrcpy executable.

        Checks bundled location first, then PATH, then common install locations.
        """
        # Check bundled location first
        bundled_dir = _get_bundled_tools_dir()
        if platform.system() == "Windows":
            bundled_scrcpy = bundled_dir / "scrcpy.exe"
        else:
            bundled_scrcpy = bundled_dir / "scrcpy"

        if bundled_scrcpy.exists() and self._check_scrcpy(str(bundled_scrcpy)):
            logger.debug(f" Using bundled scrcpy: {bundled_scrcpy}")
            return str(bundled_scrcpy)

        # Check PATH — validated like every other candidate so the version is
        # known (and a broken/too-old binary on PATH doesn't shadow a good one)
        scrcpy_in_path = shutil.which("scrcpy")
        if scrcpy_in_path and self._check_scrcpy(scrcpy_in_path):
            return scrcpy_in_path

        if platform.system() == "Windows":
            common_paths = [
                r"C:\scrcpy\scrcpy.exe",
                r"C:\Program Files\scrcpy\scrcpy.exe",
                r"C:\Program Files (x86)\scrcpy\scrcpy.exe",
                str(Path.home() / "scrcpy" / "scrcpy.exe"),
                str(Path.home() / "Downloads" / "scrcpy-win64-v3.1" / "scrcpy.exe"),
                str(Path.home() / "Downloads" / "scrcpy" / "scrcpy.exe"),
            ]
        else:
            common_paths = [
                "/usr/bin/scrcpy",
                "/usr/local/bin/scrcpy",
                "/snap/bin/scrcpy",
                str(Path.home() / ".local" / "bin" / "scrcpy"),
            ]

        for path in common_paths:
            if self._check_scrcpy(path):
                return path

        return None

    def _probe_scrcpy_version(self, path: str) -> Optional[str]:
        """Run `scrcpy --version`; return "X.Y[.Z]" or "" if it runs but prints
        nothing parseable, or None if it cannot be executed at all."""
        try:
            creationflags = 0
            if platform.system() == "Windows":
                creationflags = subprocess.CREATE_NO_WINDOW

            result = subprocess.run(
                [path, "--version"],
                capture_output=True,
                text=True,
                timeout=5,
                creationflags=creationflags,
            )
            if result.returncode != 0:
                return None
            # First line: "scrcpy 3.3.1 <https://github.com/Genymobile/scrcpy>"
            m = re.search(r"scrcpy\s+v?(\d+(?:\.\d+)*)", (result.stdout or "") + (result.stderr or ""))
            return m.group(1) if m else ""
        except (FileNotFoundError, subprocess.SubprocessError, OSError):
            return None

    def _check_scrcpy(self, path: str) -> bool:
        """Check if scrcpy executable exists and works (any version)."""
        if not path:
            return False
        version = self._probe_scrcpy_version(path)
        if version is None:
            return False
        self._scrcpy_version = version
        return True

    @staticmethod
    def _version_tuple(version: str) -> tuple:
        try:
            return tuple(int(x) for x in version.split("."))
        except ValueError:
            return ()

    def _version_too_old(self) -> bool:
        """True only when we positively know the version is below the minimum."""
        v = self._version_tuple(self._scrcpy_version)
        return bool(v) and v < MIN_SCRCPY_VERSION

    def _find_adb(self) -> Optional[str]:
        """Find ADB executable.

        Checks bundled location first, then PATH, then common install locations.
        """
        # Check bundled location first (scrcpy package includes adb)
        bundled_dir = _get_bundled_tools_dir()
        if platform.system() == "Windows":
            bundled_adb = bundled_dir / "adb.exe"
        else:
            bundled_adb = bundled_dir / "adb"

        if bundled_adb.exists():
            logger.debug(f" Using bundled adb: {bundled_adb}")
            return str(bundled_adb)

        # Check PATH
        adb_in_path = shutil.which("adb")
        if adb_in_path:
            return adb_in_path

        if platform.system() == "Windows":
            common_paths = [
                r"C:\scrcpy\adb.exe",
                r"C:\Program Files\Android\platform-tools\adb.exe",
                r"C:\Program Files (x86)\Android\platform-tools\adb.exe",
                str(Path.home() / "scrcpy" / "adb.exe"),
                str(Path.home() / "AppData" / "Local" / "Android" / "Sdk" / "platform-tools" / "adb.exe"),
            ]
        else:
            common_paths = [
                "/usr/bin/adb",
                "/usr/local/bin/adb",
                str(Path.home() / "Android" / "Sdk" / "platform-tools" / "adb"),
            ]

        for path in common_paths:
            if os.path.exists(path):
                return path

        return None

    @Slot(result=str)
    def getDeviceState(self) -> str:
        """State of the first USB device adb knows about.

        Returns "device" (ready), "unauthorized" (USB-debugging prompt not
        accepted on the phone), "offline", "none" (nothing attached) or
        "no-adb" (adb binary missing). Lets QML explain *why* mirroring
        can't start instead of a generic "no device".
        """
        if not self._adb_path:
            return "no-adb"
        try:
            creationflags = 0
            if platform.system() == "Windows":
                creationflags = subprocess.CREATE_NO_WINDOW
            result = subprocess.run(
                [self._adb_path, "devices"],
                capture_output=True, text=True, timeout=10,
                creationflags=creationflags,
            )
            if result.returncode != 0:
                return "none"
            states = []
            for line in result.stdout.strip().split('\n')[1:]:
                parts = line.split()
                if len(parts) >= 2:
                    states.append(parts[1])
            if "device" in states:
                return "device"
            for st in ("unauthorized", "offline"):
                if st in states:
                    return st
            return "none"
        except (subprocess.SubprocessError, OSError):
            return "none"

    @staticmethod
    def describeDeviceState(state: str) -> str:
        return {
            "no-adb": "adb not found. Install android-tools (adb) or scrcpy.",
            "unauthorized": "Phone is connected but USB debugging is not authorized. "
                            "Unlock the phone and tap 'Allow' on the USB debugging prompt.",
            "offline": "Phone is connected but adb reports it offline. Unplug and replug the cable.",
            "none": "No Android device connected. Connect via USB and enable USB debugging.",
        }.get(state, "")

    @Slot(result=bool)
    def hasConnectedDevice(self) -> bool:
        """Check if an Android device is connected via ADB."""
        if not self._adb_path:
            return False

        try:
            creationflags = 0
            if platform.system() == "Windows":
                creationflags = subprocess.CREATE_NO_WINDOW

            result = subprocess.run(
                [self._adb_path, "devices"],
                capture_output=True,
                text=True,
                timeout=10,
                creationflags=creationflags,
            )

            if result.returncode == 0:
                lines = result.stdout.strip().split('\n')[1:]
                for line in lines:
                    if line.strip() and 'device' in line and 'offline' not in line:
                        return True
            return False
        except (subprocess.SubprocessError, OSError):
            return False

    @Slot(result=str)
    def getDeviceSerial(self) -> str:
        """Get the first connected device serial."""
        if not self._adb_path:
            return ""

        try:
            creationflags = 0
            if platform.system() == "Windows":
                creationflags = subprocess.CREATE_NO_WINDOW

            result = subprocess.run(
                [self._adb_path, "devices"],
                capture_output=True,
                text=True,
                timeout=10,
                creationflags=creationflags,
            )

            if result.returncode == 0:
                lines = result.stdout.strip().split('\n')[1:]
                for line in lines:
                    if line.strip() and 'device' in line and 'offline' not in line:
                        parts = line.split()
                        if parts:
                            return parts[0]
            return ""
        except (subprocess.SubprocessError, OSError):
            return ""

    @Slot(result=str)
    def getDeviceName(self) -> str:
        """Get the connected device name."""
        if not self._adb_path:
            return ""

        try:
            creationflags = 0
            if platform.system() == "Windows":
                creationflags = subprocess.CREATE_NO_WINDOW

            result = subprocess.run(
                [self._adb_path, "shell", "getprop", "ro.product.model"],
                capture_output=True,
                text=True,
                timeout=10,
                creationflags=creationflags,
            )

            if result.returncode == 0:
                return result.stdout.strip()
            return ""
        except (subprocess.SubprocessError, OSError):
            return ""

    @Slot(result=str)
    def getDeviceResolution(self) -> str:
        """Get the device screen resolution via ADB (e.g., '1080x2400')."""
        if not self._adb_path:
            return ""

        try:
            creationflags = 0
            if platform.system() == "Windows":
                creationflags = subprocess.CREATE_NO_WINDOW

            result = subprocess.run(
                [self._adb_path, "shell", "wm", "size"],
                capture_output=True,
                text=True,
                timeout=10,
                creationflags=creationflags,
            )

            if result.returncode == 0:
                # Output format: "Physical size: 1080x2400"
                output = result.stdout.strip()
                for line in output.split('\n'):
                    if 'Physical size:' in line:
                        # Extract "1080x2400" from "Physical size: 1080x2400"
                        parts = line.split(':')
                        if len(parts) >= 2:
                            return parts[1].strip()
            return ""
        except (subprocess.SubprocessError, OSError):
            return ""

    @Property(str)
    def adbPath(self) -> str:
        """Get the ADB executable path."""
        return self._adb_path or ""

    @Property(bool, notify=isRunningChanged)
    def isRunning(self) -> bool:
        """Check if scrcpy is currently running."""
        if self._client is not None and self._client.is_running:
            return True
        return self._process is not None and self._process.poll() is None

    @Property(int, notify=scrcpyStarted)
    def scrcpyWindowHandle(self) -> int:
        """Get the scrcpy window handle if available."""
        return self._scrcpy_hwnd or 0

    @Slot()
    def startScrcpy(self):
        """Start scrcpy process (singleton - only one instance allowed)."""
        # Already running?
        if self._process and self._process.poll() is None:
            logger.debug("scrcpy already running, emitting existing handle")
            if self._ready:
                self.scrcpyStarted.emit(self._scrcpy_hwnd or self._process.pid)
            return

        # Already starting?
        if self._is_starting:
            logger.debug("scrcpy already starting, ignoring duplicate request")
            return

        if self._native_mode:
            state = self.getDeviceState()
            if state != "device":
                self.scrcpyError.emit(self.describeDeviceState(state))
                return
            self._start_native(self.getDeviceSerial())
            return

        if self._capture_mode == "unsupported":
            self.scrcpyError.emit(f"Phone mirroring is not supported on {platform.system()} yet")
            return

        scrcpy_path = self._get_effective_scrcpy_path()
        if not scrcpy_path:
            self.scrcpyError.emit("scrcpy not installed")
            return

        if self._version_too_old():
            need = ".".join(str(x) for x in MIN_SCRCPY_VERSION)
            self.scrcpyError.emit(
                f"scrcpy {self._scrcpy_version} at {scrcpy_path} is too old (need >= {need}). "
                f"Install a current release from https://github.com/Genymobile/scrcpy/releases")
            return

        state = self.getDeviceState()
        if state != "device":
            self.scrcpyError.emit(self.describeDeviceState(state))
            return

        device_serial = self.getDeviceSerial()
        self._run_adb(["shell", "pkill", "-f", "com.genymobile.scrcpy"])  # clear stale server first
        self._is_starting = True
        self._is_stopping = False
        self._ready = False
        self._stderr_tail = []
        self._display_id = -1
        self._active_display_size = ""

        # Build command
        cmd = [
            scrcpy_path,
            "--video-codec=h264",
            "--video-bit-rate=8M",
            "--max-fps=60",
            "--stay-awake",
        ]

        # Virtual display: landscape, 64-aligned width, leaves the phone's own
        # screen alone. Needs Android 11+; older phones mirror their screen.
        if self._display_size:
            sdk = self.getDeviceSdk()
            if sdk >= NEW_DISPLAY_MIN_SDK or sdk == 0:
                cmd.append(f"--new-display={self._display_size}")
                self._active_display_size = self._display_size
            else:
                logger.warning(f"Device SDK {sdk} < {NEW_DISPLAY_MIN_SDK}: --new-display unavailable, mirroring phone screen")

        if self._capture_mode == "v4l2":
            # Headless: no SDL window, frames go to the loopback device and
            # QML displays them through QtMultimedia. Touch is injected via adb.
            cmd += ["--no-window", f"--v4l2-sink={self._video_device}"]
        else:
            cmd.append("--window-borderless")

        # Audio forwarding - controlled by settings
        if not self._audio_enabled:
            cmd.append("--no-audio")

        if device_serial:
            cmd.extend(["-s", device_serial])

        logger.info(f"Starting scrcpy {self._scrcpy_version or '?'} [{self._capture_mode}]: {' '.join(cmd)}")

        try:
            scrcpy_dir = str(Path(scrcpy_path).parent)
            self._process = subprocess.Popen(
                cmd,
                cwd=scrcpy_dir,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                preexec_fn=die_with_parent if platform.system() != "Windows" else None,
            )
            self.isRunningChanged.emit()

            # Always drain stderr: scrcpy is chatty and blocks once the pipe fills.
            Thread(target=self._drain_stderr, args=(self._process,), daemon=True).start()

            if self._capture_mode == "window":
                Thread(target=self._find_scrcpy_window, daemon=True).start()
            else:
                Thread(target=self._wait_for_v4l2_ready, args=(self._process,), daemon=True).start()

        except Exception as e:
            logger.debug(f" Failed to start scrcpy: {e}")
            self._is_starting = False
            self.scrcpyError.emit(str(e))

    def _drain_stderr(self, proc: subprocess.Popen):
        """Log scrcpy's stderr line by line and keep a short tail for error reports."""
        try:
            for raw in iter(proc.stderr.readline, b""):
                line = raw.decode(errors="replace").rstrip()
                if not line:
                    continue
                self._stderr_tail = (self._stderr_tail + [line])[-20:]
                if "ERROR" in line or "WARN" in line:
                    logger.warning(f"scrcpy: {line}")
                else:
                    logger.debug(f"scrcpy: {line}")
                # scrcpy 3.x: "[server] INFO: New display id: 5" (wording varies)
                m = re.search(r"[Nn]ew display.*?\bid\D{0,3}(\d+)", line)
                if m and proc is self._process:
                    self._display_id = int(m.group(1))
                    logger.info(f"scrcpy virtual display id {self._display_id}")
                    self.displayIdChanged.emit()
                # scrcpy >= 2.0 prints this once the loopback sink is live
                if "v4l2 sink started" in line and proc is self._process and not self._ready:
                    self._mark_ready(proc)
        except Exception as e:
            logger.debug(f" stderr drain ended: {e}")

    def _wait_for_v4l2_ready(self, proc: subprocess.Popen):
        """Fallback readiness for v4l2 mode: if scrcpy is still alive after a
        few seconds but never printed the sink line, assume it is streaming.
        Also turns an early exit into a scrcpyError with scrcpy's own message."""
        for i in range(100):  # up to 10 s
            if proc is not self._process or self._is_stopping:
                return
            if proc.poll() is not None:
                self._on_process_exit(proc)
                return
            if self._ready:
                break
            if i >= 80:  # 8 s alive without an error: good enough
                self._mark_ready(proc)
                break
            time.sleep(0.1)
        # Keep watching so an external exit is reported
        proc.wait()
        self._on_process_exit(proc)

    def _mark_ready(self, proc: subprocess.Popen):
        if self._ready or proc is not self._process:
            return
        self._ready = True
        self._is_starting = False
        self._scrcpy_hwnd = proc.pid
        if self._active_display_size and self._display_id < 0:
            self._display_id = self._find_scrcpy_display_id()
            logger.info(f"scrcpy virtual display id (dumpsys): {self._display_id}")
            self.displayIdChanged.emit()
        logger.info(f"scrcpy ready ({self._capture_mode}, pid {proc.pid}, display {self._display_id})")
        self.scrcpyStarted.emit(proc.pid)

    def _on_process_exit(self, proc: subprocess.Popen):
        if proc is not self._process:
            return  # already replaced or stopped deliberately
        code = proc.returncode
        was_ready = self._ready
        self._process = None
        self._ready = False
        self._is_starting = False
        self._scrcpy_hwnd = None
        self._display_id = -1
        self._active_display_size = ""
        tail = "\n".join(self._stderr_tail[-5:])
        if self._is_stopping:
            return
        logger.warning(f"scrcpy exited with code {code}{' before becoming ready' if not was_ready else ''}: {tail}")
        self.isRunningChanged.emit()
        if was_ready and code == 0:
            self.scrcpyStopped.emit()
        else:
            # A yanked cable is the common vehicle case: say so plainly so the
            # view can offer reconnection instead of setup instructions.
            if any("Device disconnected" in ln for ln in self._stderr_tail):
                self.scrcpyError.emit("Phone disconnected. Reconnect the USB cable.")
                return
            # scrcpy's INFO lines (e.g. "No video mirroring, SDK mouse disabled")
            # are normal headless-mode chatter, not causes.
            errs = ([ln for ln in self._stderr_tail if "ERROR" in ln]
                    or [ln for ln in self._stderr_tail if "WARN" in ln]
                    or [ln for ln in self._stderr_tail if "INFO" not in ln][-2:])
            self.scrcpyError.emit("scrcpy failed: " + (" | ".join(errs)[:300] or f"exit code {code}"))

    def _find_scrcpy_window(self):
        """Find the scrcpy window after it starts (runs in background thread)."""
        if not self._process or platform.system() != "Windows":
            self._is_starting = False
            return

        pid = self._process.pid
        hwnd = None

        # Wait for window to appear (up to 10 seconds)
        for _ in range(100):
            # Check if process was terminated externally
            if not self._process:
                self._is_starting = False
                return

            if self._process.poll() is not None:
                tail = " | ".join(self._stderr_tail[-3:])
                logger.debug(f" scrcpy exited: {tail}")
                self._is_starting = False
                self.scrcpyError.emit(f"scrcpy failed: {tail[:200]}")
                return

            hwnd = self._find_window_by_pid(pid)
            if hwnd:
                break
            time.sleep(0.1)

        if not hwnd:
            logger.debug("Could not find scrcpy window")
            self._is_starting = False
            self.scrcpyError.emit("Could not find scrcpy window")
            return

        logger.debug(f" Found scrcpy window: {hwnd}")
        self._scrcpy_hwnd = hwnd
        self._is_starting = False
        self._ready = True

        # Emit signal with the window handle
        self.scrcpyStarted.emit(hwnd)

    def _find_window_by_pid(self, target_pid: int) -> Optional[int]:
        """Find a window by process ID."""
        result = [None]

        @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        def enum_callback(hwnd, lparam):
            pid = wintypes.DWORD()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == target_pid and user32.IsWindowVisible(hwnd):
                result[0] = hwnd
                return False
            return True

        user32.EnumWindows(enum_callback, 0)
        return result[0]

    @Slot()
    def stopScrcpy(self):
        """Stop the scrcpy process."""
        logger.debug("Stopping scrcpy...")

        self._scrcpy_hwnd = None
        self._is_starting = False
        self._is_stopping = True
        self._ready = False
        self._display_id = -1
        self._active_display_size = ""

        if self._client is not None:
            client, self._client = self._client, None
            client.stop()
            client.deleteLater()
            self._kill_stale_server()

        if self._process:
            try:
                self._process.terminate()
                self._process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                logger.warning("scrcpy did not terminate within 2s, killing")
                try:
                    self._process.kill()
                except Exception as e:
                    logger.warning(f"scrcpy kill failed: {e}")
            except Exception as e:
                logger.warning(f"scrcpy terminate failed: {e}")
                try:
                    self._process.kill()
                except Exception as kill_err:
                    logger.warning(f"scrcpy kill failed: {kill_err}")
            self._process = None
            self._kill_stale_server()

        self.scrcpyStopped.emit()
        self.isRunningChanged.emit()

    @Slot()
    def cleanup(self):
        """Cleanup when OCTAVE is closing."""
        self.stopScrcpy()

    @Slot(result=str)
    def getInstallInstructions(self) -> str:
        """Get instructions for installing scrcpy."""
        if platform.system() == "Windows":
            return """To install scrcpy:

1. Download from: https://github.com/Genymobile/scrcpy/releases
2. Extract to C:\\scrcpy or your preferred location
3. Set the path in Settings > Phone Mirror
4. Restart OCTAVE

Note: scrcpy includes ADB. Enable USB debugging on your phone."""
        else:
            need = ".".join(str(x) for x in MIN_SCRCPY_VERSION)
            return f"""To install scrcpy (version {need} or newer):

Distro packages are often too old (Ubuntu 22.04 ships 1.21).
Build the current release: https://github.com/Genymobile/scrcpy/blob/master/doc/linux.md
Arch Linux: sudo pacman -S scrcpy
macOS: brew install scrcpy

Linux also needs the v4l2loopback kernel module:
  sudo modprobe v4l2loopback exclusive_caps=0 card_label=OCTAVE video_nr=10

Make sure USB debugging is enabled and authorized on your phone."""
