"""
Android Auto Manager (DHU Mode Only)

This manager controls Android Auto integration in OCTAVE using Google's
Desktop Head Unit (DHU). The DHU handles all protocol communication with
the phone - we just launch it, capture its window, and forward clicks.

Usage:
    manager = AndroidAutoManager()
    manager.launchDhuSeamless()  # Launch and capture DHU
    manager.sendDhuClick(x, y)   # Forward touch events
    manager.closeDhu()           # Close DHU
"""

import subprocess
import os
import platform
import ctypes
import time
from typing import Optional
from pathlib import Path

from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer

from backend.logging_config import get_logger
from backend.phone_mirror.manager import _find_adb

logger = get_logger(__name__)


class AndroidAutoManager(QObject):
    """
    Android Auto manager for OCTAVE (DHU mode only).

    This class handles:
    - Finding and launching Google's Desktop Head Unit (DHU)
    - ADB port forwarding setup
    - Head unit server management on the phone
    - DHU window capture and display
    - Input forwarding to DHU
    """

    # Signals
    connectionProgress = Signal(str)  # Human-readable status
    error = Signal(str)
    dhuWindowReady = Signal(int)  # Emits window handle when DHU window is found
    dhuEmbeddedChanged = Signal(bool)  # Emits when DHU embedding state changes

    def __init__(self, parent=None):
        super().__init__(parent)

        # DHU process and window
        self._dhu_process: Optional[subprocess.Popen] = None
        self._dhu_hwnd: int = 0
        self._dhu_embedded = False

        # DHU capture for seamless integration
        from .dhu_capture import DhuCapture
        self._dhu_capture = DhuCapture(self)
        self._dhu_capture.frameReady.connect(self._on_dhu_frame_ready)
        self._dhu_capture.error.connect(self._on_dhu_capture_error)

    # Google DHU methods
    @Property(bool, constant=True)
    def isDhuInstalled(self) -> bool:
        """Check if Google's Desktop Head Unit is installed."""
        return self._find_dhu_path() is not None

    @Property(str, constant=True)
    def dhuPath(self) -> str:
        """Get the path to Google's Desktop Head Unit."""
        path = self._find_dhu_path()
        return str(path) if path else ""

    def _find_dhu_path(self) -> Optional[Path]:
        """Find the Google DHU executable path."""
        system = platform.system()

        # Common SDK locations
        if system == "Windows":
            sdk_locations = [
                Path(os.environ.get("LOCALAPPDATA", "")) / "Android" / "Sdk",
                Path(os.environ.get("USERPROFILE", "")) / "AppData" / "Local" / "Android" / "Sdk",
                Path("C:/Android/Sdk"),
                Path("C:/Users") / os.environ.get("USERNAME", "") / "Android" / "Sdk",
            ]
            dhu_name = "desktop-head-unit.exe"
        else:
            sdk_locations = [
                Path.home() / "Android" / "Sdk",
                Path.home() / "Library" / "Android" / "sdk",
                Path("/opt/android-sdk"),
                Path("/usr/local/android-sdk"),
            ]
            dhu_name = "desktop-head-unit"

        # Check each SDK location
        for sdk_path in sdk_locations:
            dhu_path = sdk_path / "extras" / "google" / "auto" / dhu_name
            if dhu_path.exists():
                return dhu_path

        return None

    def _find_adb_path(self) -> Optional[Path]:
        """Find ADB executable path.

        Same discovery as PhoneMirrorManager (bundled platform-tools, PATH,
        common SDK locations) so both features drive the same adb binary.
        """
        found = _find_adb()
        return Path(found) if found else None

    def _is_headunit_server_running(self, adb_path) -> bool:
        """Check if the head unit server is already running on the phone."""
        try:
            result = subprocess.run(
                [str(adb_path), "shell", "dumpsys", "activity", "services",
                 "com.google.android.projection.gearhead/.companion.DeveloperHeadUnitNetworkService"],
                capture_output=True,
                text=True,
                timeout=5
            )
            return "ServiceRecord" in result.stdout
        except Exception:
            return False

    def _start_headunit_server(self, adb_path) -> bool:
        """Start the head unit server on the phone via ADB."""
        try:
            logger.info("Starting head unit server on phone...")
            self.connectionProgress.emit("Starting head unit server on phone...")

            # Try starting via activity
            subprocess.run(
                [str(adb_path), "shell", "am", "start",
                 "-n", "com.google.android.projection.gearhead/.companion.MainActivity",
                 "-a", "com.google.android.gms.car.action.START_HEAD_UNIT_SERVER"],
                capture_output=True,
                text=True,
                timeout=10
            )

            time.sleep(2)

            if self._is_headunit_server_running(adb_path):
                logger.info("Head unit server started successfully")
                return True

            # Try direct service start
            subprocess.run(
                [str(adb_path), "shell", "am", "startservice",
                 "-n", "com.google.android.projection.gearhead/.companion.DeveloperHeadUnitNetworkService"],
                capture_output=True,
                text=True,
                timeout=10
            )

            time.sleep(1)

            if self._is_headunit_server_running(adb_path):
                logger.info("Head unit server started successfully")
                return True

            logger.warning("Could not auto-start head unit server")
            return False

        except Exception as e:
            logger.error(f"Error starting head unit server: {e}")
            return False

    def _setup_adb_forward(self) -> bool:
        """Setup ADB port forwarding for DHU."""
        adb_path = self._find_adb_path()

        if not adb_path:
            logger.warning("ADB not found")
            return False

        try:
            # Remove a stale DHU forward only — `--remove-all` would also
            # tear down a live phone-mirror (localabstract:scrcpy_*) tunnel.
            subprocess.run(
                [str(adb_path), "forward", "--remove", "tcp:5277"],
                capture_output=True,
                text=True,
                timeout=10
            )

            logger.debug(f"Running ADB forward: {adb_path}")
            result = subprocess.run(
                [str(adb_path), "forward", "tcp:5277", "tcp:5277"],
                capture_output=True,
                text=True,
                timeout=10
            )

            if result.returncode == 0:
                logger.debug("ADB forward successful")
                return True
            else:
                logger.error(f"ADB forward failed: {result.stderr.strip()}")
                return False

        except subprocess.TimeoutExpired:
            logger.error("ADB forward timed out")
            return False
        except Exception as e:
            logger.error(f"ADB forward error: {e}")
            return False

    def _prepare_adb_connection(self) -> bool:
        """
        Full ADB preparation: check device, setup forwarding, start head unit server.
        Returns True if successful.
        """
        adb_path = self._find_adb_path()
        if not adb_path:
            logger.warning("ADB not found - cannot prepare connection")
            self.error.emit("ADB not found. Install Android SDK platform-tools.")
            return False

        # Check if phone is connected
        try:
            result = subprocess.run(
                [str(adb_path), "devices"],
                capture_output=True,
                text=True,
                timeout=10
            )
            lines = result.stdout.strip().split('\n')
            devices = [l for l in lines[1:] if l.strip() and 'device' in l]
            if not devices:
                logger.warning("No Android device connected")
                self.error.emit("No Android device connected. Connect your phone via USB.")
                return False
            logger.info(f"Found {len(devices)} connected device(s)")
        except Exception as e:
            logger.error(f"Failed to check devices: {e}")
            return False

        # Setup fresh port forwarding
        self.connectionProgress.emit("Setting up ADB port forwarding...")
        if not self._setup_adb_forward():
            self.error.emit("Failed to setup ADB port forwarding")
            return False

        # Check if head unit server is running
        if not self._is_headunit_server_running(adb_path):
            if not self._start_headunit_server(adb_path):
                logger.info("Please start 'Head unit server' manually on your phone")
                self.connectionProgress.emit("Please start 'Head unit server' in Android Auto developer settings...")

                # Wait for user to start the server
                for i in range(60):
                    if self._is_headunit_server_running(adb_path):
                        logger.info("Head unit server detected!")
                        self.connectionProgress.emit("Head unit server started!")
                        time.sleep(0.5)
                        break
                    time.sleep(1)
                    if i > 0 and i % 5 == 0:
                        remaining = 60 - i
                        self.connectionProgress.emit(f"Waiting for head unit server... ({remaining}s)")
                else:
                    logger.warning("Timed out waiting for head unit server")
                    self.error.emit("Head unit server not started. Please start it and try again.")
                    return False
        else:
            logger.debug("Head unit server already running")

        return True

    @Slot(result=str)
    def getDhuInstallInstructions(self) -> str:
        """Get instructions for installing Google DHU."""
        return """To install Google's Desktop Head Unit:

1. Open Android Studio
2. Go to Tools → SDK Manager
3. Select the "SDK Tools" tab
4. Check "Android Auto Desktop Head Unit Emulator"
5. Click "Apply" to install

After installation, click "Launch Android Auto" button."""

    # DHU Window Properties
    @Property(bool, notify=dhuEmbeddedChanged)
    def isDhuEmbedded(self) -> bool:
        """Check if DHU window is currently embedded."""
        return self._dhu_embedded

    @Property(int, notify=dhuWindowReady)
    def dhuWindowHandle(self) -> int:
        """Get the DHU window handle."""
        return self._dhu_hwnd

    def _find_dhu_window(self) -> int:
        """Find the DHU window handle using Windows API."""
        if platform.system() != "Windows":
            return 0

        try:
            from ctypes import wintypes
            user32 = ctypes.windll.user32

            dhu_pid = self._dhu_process.pid if self._dhu_process else None
            found_hwnd = [0]

            # Proper callback type for EnumWindows
            WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

            def enum_callback(hwnd, lParam):
                if dhu_pid:
                    window_pid = wintypes.DWORD()
                    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(window_pid))
                    if window_pid.value != dhu_pid:
                        return True

                length = user32.GetWindowTextLengthW(hwnd)
                if length > 0:
                    buffer = ctypes.create_unicode_buffer(length + 1)
                    user32.GetWindowTextW(hwnd, buffer, length + 1)
                    title = buffer.value
                    if "Desktop Head Unit" in title or "Android Auto" in title:
                        found_hwnd[0] = hwnd
                        logger.debug(f"Found DHU window: '{title}' (hwnd={hwnd})")
                        return False
                return True

            callback = WNDENUMPROC(enum_callback)
            user32.EnumWindows(callback, 0)
            return found_hwnd[0]

        except Exception as e:
            logger.error(f"Error finding DHU window: {e}")
            return 0

    @Slot(result=bool)
    def launchDhuSeamless(self) -> bool:
        """
        Launch DHU and capture its output seamlessly into OCTAVE.
        The DHU window is hidden and its content is captured and displayed in QML.
        """
        # Close any existing DHU first
        if self._dhu_process or self._dhu_embedded:
            self.closeDhu()

        dhu_path = self._find_dhu_path()

        if not dhu_path:
            logger.warning("Google DHU not found")
            self.error.emit("Google DHU not installed.")
            return False

        try:
            # Full ADB preparation
            if not self._prepare_adb_connection():
                return False

            self.connectionProgress.emit("Please start 'Head unit server' on your phone...")

            logger.info(f"Launching DHU for seamless capture: {dhu_path}")

            # Create DHU config file for higher resolution
            android_dir = Path.home() / ".android"
            android_dir.mkdir(exist_ok=True)
            config_path = android_dir / "headunit.ini"
            try:
                with open(config_path, 'w') as f:
                    f.write("[general]\n")
                    f.write("resolution = 1920 1080\n")
                    f.write("dpi = 160\n")
                logger.debug(f"Created DHU config at {config_path}")
            except Exception as e:
                logger.warning(f"Could not create DHU config: {e}")

            # Launch DHU - start hidden, we'll move it off-screen immediately
            if platform.system() == "Windows":
                startupinfo = subprocess.STARTUPINFO()
                startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startupinfo.wShowWindow = 6  # SW_MINIMIZE - start minimized

                self._dhu_process = subprocess.Popen(
                    [str(dhu_path)],
                    cwd=str(dhu_path.parent),
                    startupinfo=startupinfo
                )

                # Start aggressive polling to hide window ASAP
                self._hide_poll_count = 0
                QTimer.singleShot(100, self._poll_and_hide_dhu)
            else:
                self._dhu_process = subprocess.Popen(
                    [str(dhu_path)],
                    cwd=str(dhu_path.parent)
                )
                QTimer.singleShot(3000, self._setup_seamless_capture)

            self.connectionProgress.emit("Starting Android Auto...")
            return True

        except Exception as e:
            logger.error(f"Failed to launch DHU: {e}")
            self.error.emit(f"Failed to launch DHU: {e}")
            return False

    @Slot()
    def _poll_and_hide_dhu(self):
        """Aggressively poll for DHU window and hide it immediately."""
        if not self._dhu_process or self._dhu_process.poll() is not None:
            logger.warning("DHU process ended")
            self.error.emit("DHU closed unexpectedly")
            return

        hwnd = self._find_dhu_window()
        if hwnd:
            user32 = ctypes.windll.user32

            # Hide immediately - move off-screen and remove from taskbar
            GWL_EXSTYLE = -20
            WS_EX_TOOLWINDOW = 0x00000080  # Hide from taskbar
            SWP_NOSIZE = 0x0001
            SWP_NOZORDER = 0x0004
            SWP_NOACTIVATE = 0x0010
            SW_RESTORE = 9

            # Make it a tool window (hides from taskbar)
            old_style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
            user32.SetWindowLongW(hwnd, GWL_EXSTYLE, old_style | WS_EX_TOOLWINDOW)

            # Move off-screen immediately
            user32.SetWindowPos(hwnd, 0, -3000, -3000, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)

            # Restore from minimized (needed for PrintWindow to work) but keep off-screen
            user32.ShowWindow(hwnd, SW_RESTORE)

            # Ensure it stays off-screen after restore
            user32.SetWindowPos(hwnd, 0, -3000, -3000, 0, 0,
                               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)

            logger.debug(f"DHU window {hwnd} hidden off-screen")

            # Wait a bit for window to fully initialize, then start capture
            self._dhu_hwnd = hwnd
            QTimer.singleShot(2000, self._start_dhu_capture)
        else:
            # Keep polling aggressively
            self._hide_poll_count += 1
            if self._hide_poll_count < 50:  # Try for up to 5 seconds
                QTimer.singleShot(100, self._poll_and_hide_dhu)
            else:
                logger.error("Timeout waiting for DHU window")
                self.error.emit("Could not find DHU window")

    @Slot()
    def _start_dhu_capture(self):
        """Signal that DHU window is ready for embedding."""
        if not self._dhu_hwnd:
            return

        # The EmbeddedDhuItem will handle the actual embedding via SetParent
        # We just need to signal that the window is ready
        self._dhu_embedded = True
        self.dhuEmbeddedChanged.emit(True)
        self.dhuWindowReady.emit(self._dhu_hwnd)
        self.connectionProgress.emit("Android Auto running")
        logger.info(f"DHU window ready for embedding: {self._dhu_hwnd}")

    @Slot()
    def _setup_seamless_capture(self):
        """Find DHU window and start seamless capture (non-Windows fallback)."""
        hwnd = self._find_dhu_window()
        if hwnd:
            self._dhu_hwnd = hwnd
            logger.debug(f"DHU window found for capture: {hwnd}")

            # Start capturing
            self._dhu_capture.setWindowHandle(hwnd)
            self._dhu_capture.startCapture()

            self._dhu_embedded = True
            self.dhuEmbeddedChanged.emit(True)
            self.dhuWindowReady.emit(hwnd)
            self.connectionProgress.emit("Android Auto running")

        else:
            # Retry
            if self._dhu_process and self._dhu_process.poll() is None:
                logger.debug("DHU window not found yet, retrying...")
                QTimer.singleShot(1000, self._setup_seamless_capture)
            else:
                logger.warning("DHU process ended")
                self.error.emit("DHU closed unexpectedly")

    @Slot()
    def _on_dhu_frame_ready(self):
        """Called when a new DHU frame is captured."""
        pass

    @Slot(str)
    def _on_dhu_capture_error(self, error_msg: str):
        """Handle capture errors."""
        logger.error(f"Capture error: {error_msg}")
        self.error.emit(error_msg)

    @Property(QObject, constant=True)
    def dhuCapture(self):
        """Get the DHU capture object for QML."""
        return self._dhu_capture

    @Slot(int, int)
    def sendDhuClick(self, x: int, y: int):
        """Send a click to the DHU at the given coordinates."""
        self._dhu_capture.sendMouseClick(x, y)

    @Slot()
    def closeDhu(self):
        """Close the embedded DHU."""
        # Stop capture first
        self._dhu_capture.stopCapture()

        if self._dhu_process:
            try:
                self._dhu_process.terminate()
                self._dhu_process.wait(timeout=5)
            except Exception as e:
                logger.error(f"Error closing DHU: {e}")
                try:
                    self._dhu_process.kill()
                except Exception as kill_err:
                    logger.warning(f"DHU kill failed: {kill_err}")
            self._dhu_process = None

        self._dhu_hwnd = 0
        self._dhu_embedded = False
        self.dhuEmbeddedChanged.emit(False)

    @Slot()
    def cleanup(self):
        """
        Full cleanup when OCTAVE is closing.
        Stops DHU and cleans up ADB connections.
        """
        logger.info("Cleaning up Android Auto...")

        self.closeDhu()

        # Clean up the DHU port forward (only ours — phone mirror owns its own)
        adb_path = self._find_adb_path()
        if adb_path:
            subprocess.run(
                [str(adb_path), "forward", "--remove", "tcp:5277"],
                capture_output=True,
                text=True,
                timeout=10
            )
            logger.info("Cleanup complete")
