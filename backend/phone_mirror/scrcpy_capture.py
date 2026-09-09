"""
Scrcpy Screen Capture for OCTAVE.

Captures the scrcpy window content and provides frames for QML display.
This approach works around SDL's inability to handle resize when embedded
as a child window. The scrcpy window is kept off-screen and its content
is captured and displayed via QML Image element.
"""

import platform
import re
import shutil
import subprocess
import threading
import time
from typing import Optional

from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer, QPointF, Qt
from PySide6.QtGui import QImage
from PySide6.QtQuick import QQuickImageProvider, QQuickItem

from backend.logging_config import get_logger
from backend.phone_mirror.manager import die_with_parent_kill

logger = get_logger(__name__)

if platform.system() == "Windows":
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32

    # Windows API constants
    SRCCOPY = 0x00CC0020
    DIB_RGB_COLORS = 0
    BI_RGB = 0
    PW_CLIENTONLY = 0x1
    PW_RENDERFULLCONTENT = 0x2

    # Window style constants
    GWL_EXSTYLE = -20
    WS_EX_LAYERED = 0x00080000
    WS_EX_TOOLWINDOW = 0x00000080
    WS_EX_APPWINDOW = 0x00040000
    LWA_ALPHA = 0x00000002
    SW_HIDE = 0
    SW_SHOW = 5
    SW_SHOWNOACTIVATE = 4
    SWP_NOSIZE = 0x0001
    SWP_NOZORDER = 0x0004
    SWP_NOACTIVATE = 0x0010
    SWP_SHOWWINDOW = 0x0040

    # Mouse event constants
    WM_LBUTTONDOWN = 0x0201
    WM_LBUTTONUP = 0x0202
    WM_MOUSEMOVE = 0x0200
    MK_LBUTTON = 0x0001
    MOUSEEVENTF_LEFTDOWN = 0x0002
    MOUSEEVENTF_LEFTUP = 0x0004

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ('biSize', wintypes.DWORD),
            ('biWidth', wintypes.LONG),
            ('biHeight', wintypes.LONG),
            ('biPlanes', wintypes.WORD),
            ('biBitCount', wintypes.WORD),
            ('biCompression', wintypes.DWORD),
            ('biSizeImage', wintypes.DWORD),
            ('biXPelsPerMeter', wintypes.LONG),
            ('biYPelsPerMeter', wintypes.LONG),
            ('biClrUsed', wintypes.DWORD),
            ('biClrImportant', wintypes.DWORD),
        ]

    class BITMAPINFO(ctypes.Structure):
        _fields_ = [
            ('bmiHeader', BITMAPINFOHEADER),
            ('bmiColors', wintypes.DWORD * 3),
        ]

    class RECT(ctypes.Structure):
        _fields_ = [
            ('left', wintypes.LONG),
            ('top', wintypes.LONG),
            ('right', wintypes.LONG),
            ('bottom', wintypes.LONG),
        ]

    class POINT(ctypes.Structure):
        _fields_ = [
            ('x', wintypes.LONG),
            ('y', wintypes.LONG),
        ]

    # Function signatures
    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(RECT)]
    user32.GetClientRect.restype = wintypes.BOOL
    user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(RECT)]
    user32.GetWindowRect.restype = wintypes.BOOL
    user32.GetDC.argtypes = [wintypes.HWND]
    user32.GetDC.restype = wintypes.HDC
    user32.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    user32.ReleaseDC.restype = ctypes.c_int
    user32.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
    user32.PrintWindow.restype = wintypes.BOOL
    user32.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user32.SetWindowPos.restype = wintypes.BOOL
    user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user32.ShowWindow.restype = wintypes.BOOL
    user32.IsWindow.argtypes = [wintypes.HWND]
    user32.IsWindow.restype = wintypes.BOOL
    user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
    user32.GetWindowLongW.restype = wintypes.LONG
    user32.SetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int, wintypes.LONG]
    user32.SetWindowLongW.restype = wintypes.LONG
    user32.SetLayeredWindowAttributes.argtypes = [wintypes.HWND, wintypes.DWORD, wintypes.BYTE, wintypes.DWORD]
    user32.SetLayeredWindowAttributes.restype = wintypes.BOOL
    user32.GetForegroundWindow.argtypes = []
    user32.GetForegroundWindow.restype = wintypes.HWND
    user32.SetForegroundWindow.argtypes = [wintypes.HWND]
    user32.SetForegroundWindow.restype = wintypes.BOOL
    user32.GetCursorPos.argtypes = [ctypes.POINTER(POINT)]
    user32.GetCursorPos.restype = wintypes.BOOL
    user32.SetCursorPos.argtypes = [ctypes.c_int, ctypes.c_int]
    user32.SetCursorPos.restype = wintypes.BOOL
    user32.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(POINT)]
    user32.ClientToScreen.restype = wintypes.BOOL
    user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.SendMessageW.restype = wintypes.LPARAM
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.PostMessageW.restype = wintypes.BOOL
    user32.mouse_event.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, ctypes.POINTER(wintypes.ULONG)]
    user32.GetSystemMetrics.argtypes = [ctypes.c_int]
    user32.GetSystemMetrics.restype = ctypes.c_int

    gdi32.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi32.CreateCompatibleDC.restype = wintypes.HDC
    gdi32.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
    gdi32.CreateCompatibleBitmap.restype = wintypes.HBITMAP
    gdi32.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
    gdi32.SelectObject.restype = wintypes.HGDIOBJ
    gdi32.DeleteObject.argtypes = [wintypes.HGDIOBJ]
    gdi32.DeleteObject.restype = wintypes.BOOL
    gdi32.DeleteDC.argtypes = [wintypes.HDC]
    gdi32.DeleteDC.restype = wintypes.BOOL
    gdi32.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT, ctypes.c_void_p, ctypes.POINTER(BITMAPINFO), wintypes.UINT]
    gdi32.GetDIBits.restype = ctypes.c_int
    gdi32.BitBlt.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.DWORD]
    gdi32.BitBlt.restype = wintypes.BOOL


class ScrcpyFrameProvider(QQuickImageProvider):
    """Provides captured scrcpy frames to QML."""

    def __init__(self):
        super().__init__(QQuickImageProvider.Image)
        self._current_image: Optional[QImage] = None
        self._lock = threading.Lock()

    def requestImage(self, id: str, size, requestedSize):
        """Return the current frame image."""
        with self._lock:
            if self._current_image and not self._current_image.isNull():
                return self._current_image
            # Return placeholder if no frame yet
            return QImage(720, 1280, QImage.Format_RGB32)

    def update_frame(self, image: QImage):
        """Update the current frame."""
        with self._lock:
            self._current_image = image

    @property
    def has_frame(self) -> bool:
        with self._lock:
            return self._current_image is not None and not self._current_image.isNull()


class ScrcpyCapture(QObject):
    """
    Captures the scrcpy window and provides frames for QML display.
    Also handles input forwarding to the scrcpy window.

    This class keeps the scrcpy window off-screen and captures its content
    at a high frame rate, providing the frames via QQuickImageProvider.
    """

    frameReady = Signal()
    captureStarted = Signal()
    captureStopped = Signal()
    error = Signal(str)
    frameSizeChanged = Signal(int, int)  # width, height

    def __init__(self, parent=None):
        super().__init__(parent)
        self._hwnd: int = 0
        self._capturing = False
        self._capture_timer: Optional[QTimer] = None
        self._frame_provider = ScrcpyFrameProvider()
        self._target_fps = 60  # Higher FPS for phone mirror
        self._last_width = 0
        self._last_height = 0
        self._frame_count = 0
        self._input_state = None  # Track window state during touch

        # ADB-based input (set via setPhoneMirrorManager)
        self._adb_path: str = ""
        self._device_serial: str = ""
        self._device_width: int = 0
        self._device_height: int = 0
        self._display_id: int = -1        # target display for `input -d`
        self._fixed_display: bool = False  # virtual display: geometry never rotates
        self._manager = None
        self._v4l2_proc: Optional[subprocess.Popen] = None
        self._seed_proc: Optional[subprocess.Popen] = None
        self._v4l2_token = None
        self._v4l2_thread: Optional[threading.Thread] = None
        self._warned_no_geometry = False

    @property
    def frame_provider(self) -> ScrcpyFrameProvider:
        """Get the image provider for QML."""
        return self._frame_provider

    @Property(bool, notify=captureStarted)
    def isCapturing(self) -> bool:
        return self._capturing

    @Property(int)
    def windowHandle(self) -> int:
        return self._hwnd

    @Property(int)
    def frameWidth(self) -> int:
        return self._last_width

    @Property(int)
    def frameHeight(self) -> int:
        return self._last_height

    @Slot(int, int)
    def setFrameSize(self, width: int, height: int):
        """Tell the input mapper the size of the frames QML is showing.

        Used in v4l2 mode where no frames pass through this object; the
        orientation (landscape vs portrait) drives touch coordinate mapping.
        """
        if width == self._last_width and height == self._last_height:
            return
        self._last_width = width
        self._last_height = height
        logger.debug(f" Frame size (external): {width}x{height}")
        self.frameSizeChanged.emit(width, height)

    @Slot(int)
    def setWindowHandle(self, hwnd: int):
        """Set the scrcpy window handle to capture."""
        self._hwnd = hwnd
        logger.debug(f" Window handle set to: {hwnd}")

        if platform.system() == "Windows" and hwnd:
            # Move window off-screen immediately
            self._position_window_offscreen(hwnd)

    def _position_window_offscreen(self, hwnd: int):
        """Position the scrcpy window off-screen for capture."""
        if not user32.IsWindow(hwnd):
            return

        # Hide from taskbar
        ex_style = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
        ex_style = (ex_style | WS_EX_TOOLWINDOW) & ~WS_EX_APPWINDOW
        user32.SetWindowLongW(hwnd, GWL_EXSTYLE, ex_style)

        # Move off-screen
        user32.SetWindowPos(hwnd, 0, -3000, -3000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)

        # Show window (needed for capture to work)
        user32.ShowWindow(hwnd, SW_SHOWNOACTIVATE)

        logger.debug(f" Window positioned off-screen")

    @Slot()
    def startCapture(self):
        """Start capturing the scrcpy window."""
        if self._capturing:
            return

        if not self._hwnd:
            self.error.emit("No window handle set")
            return

        if platform.system() != "Windows":
            mode = getattr(self._manager, "captureMode", "unsupported") if self._manager else "unsupported"
            if mode != "v4l2":
                self.error.emit("Screen capture only supported on Windows")
                return
            # v4l2 mode: read the loopback node ourselves with an ffmpeg
            # subprocess and push RGB frames into the image provider — the
            # same QML path the Windows grab uses. QtMultimedia's camera
            # enumeration is deliberately avoided: it snapshots the device
            # list at first use per process and never refreshes, so a node
            # that only becomes visible once scrcpy streams is never found.
            device = str(getattr(self._manager, "videoDevice", "") or "")
            ffmpeg = shutil.which("ffmpeg")
            if not ffmpeg:
                self.error.emit("ffmpeg not found — install it (apt install ffmpeg) for Linux phone mirroring")
                return
            logger.info(f"v4l2 capture: {device} via {ffmpeg}")
            self._capturing = True
            self._frame_count = 0
            self._fetchDeviceInfo()
            self._v4l2_thread = threading.Thread(target=self._v4l2_reader, args=(device, ffmpeg),
                                                 daemon=True, name="scrcpy-v4l2")
            self._v4l2_thread.start()
            self.captureStarted.emit()
            return

        logger.debug(f" Starting capture of window {self._hwnd} at {self._target_fps} FPS")
        self._capturing = True
        self._frame_count = 0

        # Fetch device info for ADB-based touch input
        self._fetchDeviceInfo()

        # Ensure window is positioned correctly
        self._position_window_offscreen(self._hwnd)

        # Start capture timer
        self._capture_timer = QTimer()
        self._capture_timer.timeout.connect(self._capture_frame)
        self._capture_timer.start(1000 // self._target_fps)

        self.captureStarted.emit()

    # ── v4l2 reader (Linux) ─────────────────────────────────────────────

    def _probe_v4l2_size(self, device: str) -> tuple:
        """Ask the node for its current WxH (0,0 if unknown).

        The node is authoritative even for a virtual display: scrcpy rounds
        the requested --new-display size (e.g. 1080x2316 -> 1080x2312), and
        parsing frames with the wrong size makes the picture roll.
        """
        v4l2ctl = shutil.which("v4l2-ctl")
        if v4l2ctl:
            try:
                out = subprocess.run([v4l2ctl, "-d", device, "--get-fmt-video"],
                                     capture_output=True, text=True, timeout=5).stdout
                m = re.search(r"Width/Height\s*:\s*(\d+)/(\d+)", out)
                if m:
                    return int(m.group(1)), int(m.group(2))
            except (subprocess.SubprocessError, OSError):
                pass
        ffprobe = shutil.which("ffprobe")
        if ffprobe:
            try:
                out = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0",
                                      "-show_entries", "stream=width,height", "-of", "csv=p=0", device],
                                     capture_output=True, text=True, timeout=10).stdout
                m = re.search(r"(\d+),(\d+)", out)
                if m:
                    return int(m.group(1)), int(m.group(2))
            except (subprocess.SubprocessError, OSError):
                pass
        return 0, 0

    # A bare Android home screen only repaints when the status-bar clock
    # ticks, i.e. once a minute, and a cold reader of the loopback node only
    # gets a frame when the producer writes one. So the first frame can take
    # up to ~60 s to arrive no matter how it is read. The seed grab therefore
    # waits well past a minute boundary, runs concurrently with the stream
    # (whichever delivers first wins), and "no frame yet" is never an error.
    SEED_TIMEOUT_S = 90.0

    def _seed_frame(self, device: str, ffmpeg: str, w: int, h: int, proc_token) -> bool:
        """Grab the frame the loopback node currently holds (or the next one
        written) with a one-shot ffmpeg run and push it to the provider."""
        if device.startswith("/dev/"):
            input_args = ["-f", "v4l2", "-i", device]
        else:
            input_args = ["-i", device]
        cmd = [ffmpeg, "-loglevel", "error", "-nostdin", *input_args,
               "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "bgra", "-"]
        t0 = time.monotonic()
        try:
            self._seed_proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                               preexec_fn=die_with_parent_kill)
            out, err = self._seed_proc.communicate(timeout=self.SEED_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            try:
                self._seed_proc.kill()
            except OSError:
                pass
            if self._capturing and proc_token is self._v4l2_token:
                logger.warning(f"v4l2 capture: seed grab produced no frame in {self.SEED_TIMEOUT_S:.0f} s")
            return False
        except (subprocess.SubprocessError, OSError) as e:
            logger.warning(f"v4l2 capture: seed grab failed: {e}")
            return False
        finally:
            self._seed_proc = None
        if not self._capturing or proc_token is not self._v4l2_token:
            return False  # capture stopped/restarted meanwhile
        if len(out) < w * h * 4:
            logger.warning(f"v4l2 capture: seed grab returned {len(out)} bytes: "
                           f"{err.decode(errors='replace').strip()[-200:]}")
            return False
        if self._frame_count == 0:
            image = QImage(bytes(out[:w * h * 4]), w, h, w * 4, QImage.Format_ARGB32)
            self._frame_provider.update_frame(image)
            self._frame_count += 1
            self.frameReady.emit()
            logger.info(f"v4l2 capture: seeded first frame from the held buffer after {time.monotonic() - t0:.1f} s")
        return True

    def _v4l2_reader(self, device: str, ffmpeg: str):
        """Background loop: ffmpeg decodes the node to raw BGRA on stdout; we
        wrap each frame in a QImage and hand it to the provider. Restarts
        ffmpeg if it exits (e.g. the stream format changed) while capturing."""
        deadline = time.monotonic() + 20.0
        seed_thread = None
        while self._capturing:
            w, h = self._probe_v4l2_size(device)
            if w <= 0 or h <= 0:
                if time.monotonic() > deadline:
                    self._capturing = False
                    self.error.emit(f"No video stream on {device} after 20 s. Is v4l2loopback loaded "
                                    f"(exclusive_caps=0) and scrcpy streaming into it?")
                    return
                time.sleep(0.5)
                continue
            if self._fixed_display and (w, h) != (self._device_width, self._device_height):
                logger.warning(f"v4l2 capture: requested virtual display {self._device_width}x{self._device_height} "
                               f"but the stream is {w}x{h}; using the stream size for frames and touch")
                self._device_width, self._device_height = w, h
            frame_bytes = w * h * 4
            if device.startswith("/dev/"):
                input_args = ["-f", "v4l2", "-i", device]
            else:
                # Developer convenience: a media file loops as a fake phone feed
                input_args = ["-re", "-stream_loop", "-1", "-i", device]
            cmd = [ffmpeg, "-loglevel", "error", "-nostdin",
                   "-fflags", "nobuffer", "-flags", "low_delay",
                   *input_args,
                   "-f", "rawvideo", "-pix_fmt", "bgra", "-"]
            token = object()
            self._v4l2_token = token
            if self._frame_count == 0 and (seed_thread is None or not seed_thread.is_alive()):
                seed_thread = threading.Thread(target=self._seed_frame, args=(device, ffmpeg, w, h, token),
                                               daemon=True, name="scrcpy-v4l2-seed")
                seed_thread.start()
                # Give the readers a frame to catch (see nudgeDisplay)
                threading.Timer(0.5, self.nudgeDisplay).start()
            try:
                proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        bufsize=frame_bytes * 2, preexec_fn=die_with_parent_kill)
            except OSError as e:
                self._capturing = False
                self.error.emit(f"ffmpeg failed to start: {e}")
                return
            self._v4l2_proc = proc
            logger.info(f"v4l2 capture: ffmpeg pid {proc.pid}, {w}x{h}")
            if (w, h) != (self._last_width, self._last_height):
                self._last_width, self._last_height = w, h
                self.frameSizeChanged.emit(w, h)
            buf = bytearray(frame_bytes)
            view = memoryview(buf)
            streamed = 0
            while self._capturing:
                filled = 0
                while filled < frame_bytes and self._capturing:
                    n = proc.stdout.readinto(view[filled:])
                    if not n:
                        break
                    filled += n
                if filled < frame_bytes:
                    break  # ffmpeg exited / EOF
                image = QImage(bytes(buf), w, h, w * 4, QImage.Format_ARGB32)
                self._frame_provider.update_frame(image)
                self._frame_count += 1
                streamed += 1
                self.frameReady.emit()
            # ffmpeg ended
            try:
                proc.kill()
                err = proc.stderr.read().decode(errors="replace").strip()
            except Exception:
                err = ""
            self._v4l2_proc = None
            if not self._capturing:
                return
            # Give up only if ffmpeg keeps dying without ever streaming AND the
            # seed has also finished without producing a frame. An idle phone
            # legitimately yields nothing for up to a minute.
            seed_pending = seed_thread is not None and seed_thread.is_alive()
            give_up = (streamed == 0 and self._frame_count == 0 and not seed_pending
                       and time.monotonic() > deadline)
            logger.warning(f"v4l2 capture: ffmpeg exited ({err[-200:] or 'no output'}), "
                           f"{'giving up' if give_up else 'restarting'}")
            if give_up:
                self._capturing = False
                self.error.emit(f"Could not read video from {device}: {err[-200:] or 'ffmpeg produced no frames'}")
                return
            time.sleep(0.5)

    @Slot()
    def stopCapture(self):
        """Stop capturing."""
        if not self._capturing:
            return

        logger.debug(f" Stopping capture (captured {self._frame_count} frames)")
        self._capturing = False

        for attr in ("_v4l2_proc", "_seed_proc"):
            proc = getattr(self, attr, None)
            if proc is not None:
                try:
                    proc.kill()
                except OSError:
                    pass
                setattr(self, attr, None)
        self._v4l2_token = None

        if self._capture_timer:
            self._capture_timer.stop()
            self._capture_timer = None

        self.captureStopped.emit()

    def _capture_frame(self):
        """Capture a single frame from the scrcpy window."""
        if not self._capturing or not self._hwnd:
            return

        if platform.system() != "Windows":
            return

        if not user32.IsWindow(self._hwnd):
            logger.debug("Window no longer exists")
            self.stopCapture()
            self.error.emit("Scrcpy window closed")
            return

        try:
            # Get client area dimensions
            rect = RECT()
            if not user32.GetClientRect(self._hwnd, ctypes.byref(rect)):
                return

            width = rect.right - rect.left
            height = rect.bottom - rect.top

            if width <= 0 or height <= 0:
                return

            # Check if size changed
            if width != self._last_width or height != self._last_height:
                self._last_width = width
                self._last_height = height
                self.frameSizeChanged.emit(width, height)
                logger.debug(f" Frame size: {width}x{height}")

            # Get window DC
            hwnd_dc = user32.GetDC(self._hwnd)
            if not hwnd_dc:
                return

            try:
                # Create compatible DC and bitmap
                mem_dc = gdi32.CreateCompatibleDC(hwnd_dc)
                if not mem_dc:
                    return

                try:
                    bitmap = gdi32.CreateCompatibleBitmap(hwnd_dc, width, height)
                    if not bitmap:
                        return

                    try:
                        # Select bitmap into memory DC
                        old_bitmap = gdi32.SelectObject(mem_dc, bitmap)

                        # Capture using PrintWindow for best results with hardware-accelerated content
                        # PW_RENDERFULLCONTENT works better for DirectX/OpenGL windows
                        result = user32.PrintWindow(self._hwnd, mem_dc, PW_RENDERFULLCONTENT)

                        if not result:
                            # Fallback to BitBlt if PrintWindow fails
                            gdi32.BitBlt(mem_dc, 0, 0, width, height, hwnd_dc, 0, 0, SRCCOPY)

                        # Get bitmap data
                        bmi = BITMAPINFO()
                        bmi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
                        bmi.bmiHeader.biWidth = width
                        bmi.bmiHeader.biHeight = -height  # Negative for top-down
                        bmi.bmiHeader.biPlanes = 1
                        bmi.bmiHeader.biBitCount = 32
                        bmi.bmiHeader.biCompression = BI_RGB

                        # Allocate buffer for pixel data
                        buffer_size = width * height * 4
                        buffer = (ctypes.c_ubyte * buffer_size)()

                        # Get bitmap bits
                        gdi32.GetDIBits(
                            mem_dc, bitmap, 0, height,
                            buffer, ctypes.byref(bmi), DIB_RGB_COLORS
                        )

                        # Create QImage from buffer (BGRA format)
                        image = QImage(
                            bytes(buffer), width, height,
                            width * 4, QImage.Format_ARGB32
                        ).copy()  # Copy to own the data

                        # Update the frame provider
                        self._frame_provider.update_frame(image)
                        self._frame_count += 1
                        self.frameReady.emit()

                        # Restore old bitmap
                        gdi32.SelectObject(mem_dc, old_bitmap)

                    finally:
                        gdi32.DeleteObject(bitmap)
                finally:
                    gdi32.DeleteDC(mem_dc)
            finally:
                user32.ReleaseDC(self._hwnd, hwnd_dc)

        except Exception as e:
            logger.debug(f" Capture error: {e}")

    def setPhoneMirrorManager(self, manager):
        """
        Set the PhoneMirrorManager reference for ADB access.
        This allows ScrcpyCapture to automatically get ADB path, device serial,
        and device resolution for touch input.
        """
        self._manager = manager
        logger.debug(f" PhoneMirrorManager linked")

    def _fetchDeviceInfo(self):
        """Fetch ADB path, device serial, and resolution from manager."""
        if not self._manager:
            logger.debug("No manager set, cannot fetch device info")
            return

        # Get ADB path
        self._adb_path = self._manager.adbPath
        logger.debug(f" ADB path: {self._adb_path}")

        # Get device serial
        self._device_serial = self._manager.getDeviceSerial()
        logger.debug(f" Device serial: {self._device_serial}")

        # Virtual display (--new-display): its geometry IS the frame geometry,
        # never rotates, and input must be addressed to its display id.
        self._display_id = int(getattr(self._manager, "displayId", -1) or -1)
        active = str(getattr(self._manager, "activeDisplaySize", "") or "")
        if active and 'x' in active:
            w, h = active.split('x')
            self._device_width, self._device_height = int(w), int(h)
            self._fixed_display = True
            logger.debug(f" Virtual display {self._device_width}x{self._device_height}, id {self._display_id}")
            return
        self._fixed_display = False

        # Get device resolution
        resolution_str = self._manager.getDeviceResolution()
        if resolution_str and 'x' in resolution_str:
            try:
                width_str, height_str = resolution_str.split('x')
                self._device_width = int(width_str)
                self._device_height = int(height_str)
                logger.debug(f" Device resolution: {self._device_width}x{self._device_height}")
            except ValueError:
                logger.debug(f" Failed to parse resolution: {resolution_str}")
        else:
            logger.debug(f" Could not get device resolution")

    def setAdbPath(self, adb_path: str):
        """Set the ADB path for input commands."""
        self._adb_path = adb_path

    def setDeviceSerial(self, serial: str):
        """Set the device serial for ADB commands."""
        self._device_serial = serial

    def setDeviceResolution(self, width: int, height: int):
        """Set the device screen resolution for coordinate mapping."""
        self._device_width = width
        self._device_height = height
        logger.debug(f" Device resolution set to {width}x{height}")

    def _is_landscape(self) -> bool:
        """Check if the current capture is in landscape mode."""
        # Compare frame dimensions - landscape if wider than tall
        if self._fixed_display:
            return False  # virtual display geometry already matches the frame
        return self._last_width > self._last_height

    def _convert_to_device_coords(self, rel_x: float, rel_y: float) -> tuple:
        """
        Convert relative coordinates (0-1) to device pixel coordinates.
        Handles orientation by checking if captured frame is landscape or portrait.

        The device always reports its physical resolution (portrait, e.g. 1080x2316).
        When in landscape mode, we need to transform the screen coordinates to match
        the device's coordinate system.
        """
        is_landscape = self._is_landscape()

        if is_landscape:
            # Phone is in landscape mode
            # The screen shows landscape content (wide), but ADB input coordinates
            # are ALWAYS in the current screen orientation, not physical portrait.
            # So we just need to map rel coords to the CURRENT screen dimensions.
            # In landscape, the effective resolution is height x width (e.g., 2316x1080)
            device_x = int(rel_x * self._device_height)  # landscape width = portrait height
            device_y = int(rel_y * self._device_width)   # landscape height = portrait width
        else:
            # Portrait mode - direct mapping
            device_x = int(rel_x * self._device_width)
            device_y = int(rel_y * self._device_height)

        # Clamp to valid range
        device_x = max(0, min(device_x, self._device_width - 1))
        device_y = max(0, min(device_y, self._device_height - 1))

        return device_x, device_y

    def nudgeDisplay(self):
        """Make the virtual display draw a frame right now.

        A freshly created --new-display is static from birth (only its clock
        ticks, once a minute), and the loopback node only yields a frame when
        one is written, so the first picture could take up to a minute. An
        8 px horizontal swipe in the middle of the launcher makes it repaint
        (the page snaps straight back) without activating anything.
        """
        if not self._adb_path or not self._fixed_display or self._device_width <= 0:
            return
        cx, cy = self._device_width // 2, self._device_height // 2
        cmd = [self._adb_path]
        if self._device_serial:
            cmd.extend(['-s', self._device_serial])
        cmd.extend(['shell', 'input'] + self._display_args()
                   + ['swipe', str(cx), str(cy), str(cx + 8), str(cy), '60'])
        logger.info("v4l2 capture: nudging the virtual display to draw its first frame")

        def run():
            try:
                subprocess.run(cmd, capture_output=True, timeout=5)
            except (subprocess.SubprocessError, OSError) as e:
                logger.warning(f"v4l2 capture: nudge failed: {e}")
        threading.Thread(target=run, daemon=True, name="scrcpy-nudge").start()

    def _display_args(self) -> list:
        """`input -d <id>` routes the event to scrcpy's virtual display."""
        return ['-d', str(self._display_id)] if self._display_id >= 0 else []

    @Slot(float, float)
    def sendTap(self, rel_x: float, rel_y: float):
        """
        Send a tap to the Android device using ADB.
        This bypasses all window manipulation issues.

        Args:
            rel_x, rel_y: Relative position (0.0 to 1.0) within the display
        """
        if not hasattr(self, '_adb_path') or not self._adb_path:
            logger.debug("No ADB path set")
            return

        if not hasattr(self, '_device_width') or self._device_width <= 0:
            if not self._warned_no_geometry:
                self._warned_no_geometry = True
                logger.warning("Touch dropped: device geometry unknown (adb 'wm size' failed?) — "
                               "further drops logged at debug level")
            logger.debug("No device resolution set")
            return

        # Convert relative position to device pixels (handles orientation)
        device_x, device_y = self._convert_to_device_coords(rel_x, rel_y)

        # Build ADB command
        cmd = [self._adb_path]
        if hasattr(self, '_device_serial') and self._device_serial:
            cmd.extend(['-s', self._device_serial])
        cmd.extend(['shell', 'input'] + self._display_args() + ['tap', str(device_x), str(device_y)])

        is_landscape = self._is_landscape()
        logger.debug(f" ADB tap: rel({rel_x:.3f},{rel_y:.3f}) -> device({device_x},{device_y}) [{'landscape' if is_landscape else 'portrait'}]")

        # Run ADB command in background thread to avoid blocking
        import subprocess
        from threading import Thread

        def run_tap():
            try:
                creationflags = 0
                if platform.system() == "Windows":
                    creationflags = subprocess.CREATE_NO_WINDOW
                subprocess.run(cmd, capture_output=True, timeout=2, creationflags=creationflags)
            except Exception as e:
                logger.debug(f" ADB tap error: {e}")

        Thread(target=run_tap, daemon=True).start()

    @Slot(float, float, float, float, int)
    def sendSwipe(self, rel_x1: float, rel_y1: float, rel_x2: float, rel_y2: float, duration_ms: int = 300):
        """
        Send a swipe to the Android device using ADB.

        Args:
            rel_x1, rel_y1: Start position (0.0 to 1.0)
            rel_x2, rel_y2: End position (0.0 to 1.0)
            duration_ms: Swipe duration in milliseconds
        """
        if not hasattr(self, '_adb_path') or not self._adb_path:
            return

        if not hasattr(self, '_device_width') or self._device_width <= 0:
            if not self._warned_no_geometry:
                self._warned_no_geometry = True
                logger.warning("Touch dropped: device geometry unknown (adb 'wm size' failed?)")
            return

        # Convert relative positions to device pixels (handles orientation)
        x1, y1 = self._convert_to_device_coords(rel_x1, rel_y1)
        x2, y2 = self._convert_to_device_coords(rel_x2, rel_y2)

        # Build ADB command
        cmd = [self._adb_path]
        if hasattr(self, '_device_serial') and self._device_serial:
            cmd.extend(['-s', self._device_serial])
        cmd.extend(['shell', 'input'] + self._display_args() + ['swipe', str(x1), str(y1), str(x2), str(y2), str(duration_ms)])

        is_landscape = self._is_landscape()
        logger.debug(f" ADB swipe: ({x1},{y1}) -> ({x2},{y2}) duration={duration_ms}ms [{'landscape' if is_landscape else 'portrait'}]")

        import subprocess
        from threading import Thread

        def run_swipe():
            try:
                creationflags = 0
                if platform.system() == "Windows":
                    creationflags = subprocess.CREATE_NO_WINDOW
                subprocess.run(cmd, capture_output=True, timeout=5, creationflags=creationflags)
            except Exception as e:
                logger.debug(f" ADB swipe error: {e}")

        Thread(target=run_swipe, daemon=True).start()

    @Slot(float, float, bool)
    def sendTouchEvent(self, rel_x: float, rel_y: float, pressed: bool):
        """
        Handle touch events - tap on release, track for swipe.
        Uses ADB shell input commands for reliable touch forwarding.
        """
        if pressed:
            # Store start position for potential swipe
            self._touch_start_x = rel_x
            self._touch_start_y = rel_y
            self._touch_moved = False
            logger.debug(f" Touch START at rel({rel_x:.3f}, {rel_y:.3f})")
        else:
            # On release, check if it was a tap or swipe
            logger.debug(f" Touch END at rel({rel_x:.3f}, {rel_y:.3f})")
            if hasattr(self, '_touch_start_x'):
                dx = abs(rel_x - self._touch_start_x)
                dy = abs(rel_y - self._touch_start_y)

                if self._touch_moved and (dx > 0.05 or dy > 0.05):
                    # It was a swipe
                    logger.debug(f" Detected SWIPE (moved={self._touch_moved}, dx={dx:.3f}, dy={dy:.3f})")
                    self.sendSwipe(self._touch_start_x, self._touch_start_y, rel_x, rel_y, 200)
                else:
                    # It was a tap
                    logger.debug(f" Detected TAP")
                    self.sendTap(self._touch_start_x, self._touch_start_y)

    @Slot(float, float)
    def sendTouchMove(self, rel_x: float, rel_y: float):
        """Track touch movement for swipe detection."""
        self._touch_moved = True


class ScrcpyCaptureItem(QQuickItem):
    """
    A QQuickItem that displays captured scrcpy frames and handles input.

    This item connects to PhoneMirrorManager and ScrcpyCapture to:
    1. Start capture when scrcpy launches
    2. Refresh the QML Image when new frames are available
    3. Forward touch/mouse input to the scrcpy window
    """

    # Signals
    streamStarted = Signal()
    streamStopped = Signal()
    errorOccurred = Signal(str)
    frameRefresh = Signal()  # Triggers QML image refresh

    def __init__(self, parent=None):
        super().__init__(parent)
        self._manager = None
        self._capture: Optional[ScrcpyCapture] = None
        self._frame_counter = 0  # For triggering QML image refresh

        # Accept mouse events
        self.setAcceptedMouseButtons(Qt.LeftButton)

        logger.debug("Created")

    @Property(int, notify=frameRefresh)
    def frameCounter(self) -> int:
        """Counter that increments on each frame, used to refresh QML Image."""
        return self._frame_counter

    @Property(int, notify=streamStarted)
    def frameWidth(self) -> int:
        if self._capture:
            return self._capture.frameWidth
        return 0

    @Property(int, notify=streamStarted)
    def frameHeight(self) -> int:
        if self._capture:
            return self._capture.frameHeight
        return 0

    @Property(bool, notify=streamStarted)
    def isStreaming(self) -> bool:
        return self._capture is not None and self._capture.isCapturing

    def setCapture(self, capture: ScrcpyCapture):
        """Set the ScrcpyCapture instance to use."""
        self._capture = capture
        if capture:
            capture.frameReady.connect(self._on_frame_ready)
            capture.error.connect(self._on_capture_error)

    @Slot(QObject)
    def connectToManager(self, manager):
        """Connect to PhoneMirrorManager."""
        if self._manager:
            try:
                self._manager.scrcpyStarted.disconnect(self._on_scrcpy_started)
                self._manager.scrcpyStopped.disconnect(self._on_scrcpy_stopped)
                self._manager.scrcpyError.disconnect(self._on_scrcpy_error)
            except (RuntimeError, TypeError):
                pass  # Signals may already be disconnected

        self._manager = manager
        if manager:
            logger.debug("Connecting to manager")
            from PySide6.QtCore import Qt
            manager.scrcpyStarted.connect(self._on_scrcpy_started, Qt.QueuedConnection)
            manager.scrcpyStopped.connect(self._on_scrcpy_stopped, Qt.QueuedConnection)
            manager.scrcpyError.connect(self._on_scrcpy_error, Qt.QueuedConnection)

            # If already running, start capture
            if manager.isRunning and manager.scrcpyWindowHandle:
                logger.debug(f" Manager has running scrcpy")
                QTimer.singleShot(100, lambda: self._on_scrcpy_started(manager.scrcpyWindowHandle))

    @Slot(int)
    def _on_scrcpy_started(self, hwnd: int):
        """Called when scrcpy window is ready."""
        logger.debug(f" Scrcpy started, hwnd={hwnd}")

        if not self._capture:
            logger.debug("No capture instance set!")
            return

        # Set window handle and start capture
        self._capture.setWindowHandle(hwnd)
        QTimer.singleShot(200, self._capture.startCapture)  # Small delay for window to be ready

        self.streamStarted.emit()

    @Slot()
    def _on_scrcpy_stopped(self):
        """Called when scrcpy stops."""
        logger.debug("Scrcpy stopped")
        if self._capture:
            self._capture.stopCapture()
        self.streamStopped.emit()

    @Slot(str)
    def _on_scrcpy_error(self, error: str):
        """Called on scrcpy error."""
        logger.debug(f" Error: {error}")
        if self._capture:
            self._capture.stopCapture()
        self.errorOccurred.emit(error)

    @Slot()
    def _on_frame_ready(self):
        """Called when a new frame is captured."""
        self._frame_counter += 1
        self.frameRefresh.emit()

    @Slot(str)
    def _on_capture_error(self, error: str):
        """Called on capture error."""
        self.errorOccurred.emit(error)

    @Slot()
    def detach(self):
        """Pause capture when view is deactivated."""
        logger.debug("Detaching (pausing capture)")
        if self._capture:
            self._capture.stopCapture()

    @Slot()
    def reattach(self):
        """Resume capture when view is reactivated."""
        logger.debug("Reattaching (resuming capture)")
        if self._manager and self._manager.isRunning and self._capture:
            hwnd = self._manager.scrcpyWindowHandle
            if hwnd:
                self._capture.setWindowHandle(hwnd)
                self._capture.startCapture()
                self.streamStarted.emit()

    # Mouse event handlers for touch input
    def mousePressEvent(self, event):
        """Handle mouse press."""
        if self._capture and self._capture.isCapturing:
            pos = event.position()
            self._capture.sendMouseEvent(
                int(pos.x()), int(pos.y()), True,
                int(self.width()), int(self.height())
            )
        event.accept()

    def mouseReleaseEvent(self, event):
        """Handle mouse release."""
        if self._capture and self._capture.isCapturing:
            pos = event.position()
            self._capture.sendMouseEvent(
                int(pos.x()), int(pos.y()), False,
                int(self.width()), int(self.height())
            )
        event.accept()

    def mouseMoveEvent(self, event):
        """Handle mouse move (for drag)."""
        if self._capture and self._capture.isCapturing:
            pos = event.position()
            self._capture.sendMouseMove(
                int(pos.x()), int(pos.y()),
                int(self.width()), int(self.height())
            )
        event.accept()

    @Slot(float, float)
    def handleClick(self, x: float, y: float):
        """Handle a click from QML."""
        if self._capture and self._capture.isCapturing:
            self._capture.sendClick(
                int(x), int(y),
                int(self.width()), int(self.height())
            )

    def componentComplete(self):
        """Called when QML component is fully loaded."""
        super().componentComplete()
        logger.debug("Component complete")
