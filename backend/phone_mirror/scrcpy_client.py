"""
Built-in scrcpy-protocol client for OCTAVE.

Replaces the scrcpy desktop binary (and, on Linux, v4l2loopback + ffmpeg):
OCTAVE pushes the bundled scrcpy *server* jar to the phone over adb, starts it,
and talks the scrcpy protocol itself — an H.264 elementary stream on one
socket, control messages (touch, keys) on another. Frames are decoded with
PyAV and handed to a QVideoSink for QML's VideoOutput.

Protocol details (server 3.x) are documented in docs/PHONE_MIRROR_NATIVE_PLAN.md
and were verified against the scrcpy v3.3.4 source.
"""

import os
import platform
import secrets
import socket
import struct
import subprocess
import threading
import time
from pathlib import Path
from typing import Optional

from collections import deque

from backend.lazy_import import available, lazy_module

# numpy is only touched once phone audio flows; defer the import (~0.2 s on the Pi).
np = lazy_module("numpy") if available("numpy") else None

from PySide6.QtCore import QObject, Signal, Slot, QSize, QTimer
from PySide6.QtMultimedia import QVideoFrame, QVideoFrameFormat, QAudioFormat, QAudioSink, QMediaDevices

from backend.logging_config import get_logger

try:
    import av  # PyAV — already in requirements.txt
    HAVE_AV = True
except Exception:  # pragma: no cover - optional at runtime
    av = None
    HAVE_AV = False

logger = get_logger(__name__)

# The bundled phone server: OCTAVE's fork of the scrcpy server (phone_server/,
# Java package org.octave.phoneserver). The version string must match the
# jar's BuildConfig exactly or the server refuses to start.
SERVER_VERSION = "3.3.4-octave"
SERVER_JAR_NAME = "octave-phone-server"
SERVER_CLASS = "org.octave.phoneserver.Server"
SERVER_PROCESS_PATTERN = "org.octave.phoneserver"
DEVICE_JAR_PATH = "/data/local/tmp/octave-phone-server.jar"

# Control message types (app/src/control_msg.h)
MSG_INJECT_KEYCODE = 0
MSG_INJECT_TEXT = 1
MSG_INJECT_TOUCH_EVENT = 2
MSG_BACK_OR_SCREEN_ON = 3
MSG_SET_DISPLAY_POWER = 10

# Android MotionEvent / KeyEvent actions
ACTION_DOWN = 0
ACTION_UP = 1
ACTION_MOVE = 2

# Audio stream (phone_server/.../audio/AudioConfig.java, device/Streamer.java)
AUDIO_SAMPLE_RATE = 48000
AUDIO_CHANNELS = 2
AUDIO_BYTES_PER_FRAME = AUDIO_CHANNELS * 2          # s16le
AUDIO_CODEC_RAW = 0x00726177                       # "raw"
AUDIO_STREAM_DISABLED = 0                          # device could not capture; video continues
AUDIO_STREAM_ERROR = 1                             # configuration error; session must stop
AUDIO_QUEUE_MAX_BYTES = AUDIO_SAMPLE_RATE * AUDIO_BYTES_PER_FRAME // 4   # ~250 ms of backlog before dropping
# Sound detection (for ducking local media): a chunk counts as sound when its
# peak, before gain, exceeds about -46 dBFS; a silent remote submix is exact
# zeros, so this only has to reject dither-level noise.
AUDIO_SIGNAL_THRESHOLD = 164
# How long the phone must stay silent before audio_playing drops. Long enough
# to bridge the gaps between navigation sentences, short enough that music
# comes back promptly after a prompt.
AUDIO_HOLD_MS = 1500

# Frame header flags (app/src/demuxer.c)
FLAG_CONFIG = 1 << 63
FLAG_KEY_FRAME = 1 << 62
PTS_MASK = (1 << 62) - 1

# Android keycodes we expose
KEYCODE_HOME = 3
KEYCODE_BACK = 4
KEYCODE_APP_SWITCH = 187
KEYCODE_POWER = 26


def bundled_server_jar() -> Optional[str]:
    """Locate the bundled phone server jar (repo: tools/phone-server/)."""
    root = Path(__file__).resolve().parents[2]
    candidates = [
        root / "tools" / "phone-server" / SERVER_JAR_NAME,   # checkout
        root / "phone-server" / SERVER_JAR_NAME,             # next to a deployed binary
    ]
    for c in candidates:
        if c.is_file():
            return str(c)
    return None


def _die_with_parent():
    if platform.system() != "Linux":
        return
    try:
        import ctypes
        libc = ctypes.CDLL("libc.so.6", use_errno=True)
        libc.prctl(1, 15)  # PR_SET_PDEATHSIG, SIGTERM
    except Exception:
        pass


def _recv_exact(sock: socket.socket, n: int) -> bytes:
    buf = bytearray(n)
    view = memoryview(buf)
    got = 0
    while got < n:
        k = sock.recv_into(view[got:], n - got)
        if k == 0:
            raise ConnectionError("socket closed")
        got += k
    return bytes(buf)


class ScrcpyClient(QObject):
    """One mirroring session: server process + video/control sockets + decoder."""

    connected = Signal(int, int)        # frame width, height
    disconnected = Signal(str)          # reason; "" for a requested stop
    frameSizeChanged = Signal(int, int)
    frameReady = Signal()               # a new decoded frame is waiting (GUI thread pulls it)
    audioReady = Signal()               # PCM chunks are queued (GUI thread writes them to the sink)
    audioStateChanged = Signal(bool)    # phone audio is (not) being played through OCTAVE
    audioPlayingChanged = Signal(bool)  # the phone is (not) producing sound (GUI thread)
    serverLog = Signal(str)

    def __init__(self, adb_path: str, server_jar: str, parent=None):
        super().__init__(parent)
        self._adb = adb_path
        self._jar = server_jar
        self._serial = ""
        self._port = 0
        self._scid = ""
        self._proc: Optional[subprocess.Popen] = None
        self._video: Optional[socket.socket] = None
        self._audio: Optional[socket.socket] = None
        self._control: Optional[socket.socket] = None
        self._thread: Optional[threading.Thread] = None
        self._stopping = False
        self._running = False
        self._width = 0
        self._height = 0
        self._sink = None
        self._latest_frame: Optional[QVideoFrame] = None
        self._frame_lock = threading.Lock()
        self._send_lock = threading.Lock()
        self._frame_count = 0
        self.frameReady.connect(self._deliver_frame)
        # Audio: PCM from the audio socket, played through QAudioSink on the
        # GUI thread (QAudioSink is not thread-safe). Volume follows OCTAVE.
        self._audio_queue: deque = deque()
        self._audio_queue_bytes = 0
        self._audio_lock = threading.Lock()
        self._audio_sink: Optional[QAudioSink] = None
        self._audio_io = None
        self._audio_active = False
        self._volume = 1.0
        self._audio_gain = 2.0
        self._gain_warned = False
        self.audioReady.connect(self._deliver_audio)
        # Sound detection for ducking: the audio thread flags a chunk with
        # level, the GUI thread turns that into audio_playing with a release hold.
        self._audio_signal_seen = False
        self._audio_playing = False
        self._audio_hold_timer = QTimer(self)
        self._audio_hold_timer.setSingleShot(True)
        self._audio_hold_timer.setInterval(AUDIO_HOLD_MS)
        self._audio_hold_timer.timeout.connect(lambda: self._set_audio_playing(False))

    # ── public API ────────────────────────────────────────────────────

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def frame_size(self) -> tuple:
        return self._width, self._height

    @property
    def frame_count(self) -> int:
        return self._frame_count

    @property
    def audio_active(self) -> bool:
        return self._audio_active

    @property
    def audio_playing(self) -> bool:
        """The phone is producing sound right now (level above
        AUDIO_SIGNAL_THRESHOLD within the last AUDIO_HOLD_MS)."""
        return self._audio_playing

    def _set_audio_playing(self, playing: bool):
        # GUI thread
        if playing == self._audio_playing:
            return
        self._audio_playing = playing
        self.audioPlayingChanged.emit(playing)

    def set_audio_gain(self, gain: float):
        """Linear gain applied to phone PCM before the sink (soft-limited)."""
        self._audio_gain = max(0.25, min(8.0, float(gain)))

    def set_volume(self, linear: float):
        """0..1 linear, as produced by VolumeController."""
        self._volume = max(0.0, min(1.0, float(linear)))
        if self._audio_sink is not None:
            self._audio_sink.setVolume(self._volume)

    def set_video_sink(self, sink):
        """QVideoSink (from QML VideoOutput.videoSink) that receives frames."""
        self._sink = sink

    def start(self, serial: str, display_size: str = "", max_fps: int = 60,
              bit_rate: int = 8_000_000, audio: bool = False, stay_awake: bool = True) -> bool:
        if self._running or (self._thread and self._thread.is_alive()):
            return False
        if not HAVE_AV:
            self.disconnected.emit("PyAV (python package 'av') is not installed")
            return False
        self._serial = serial
        self._stopping = False
        self._frame_count = 0
        self._thread = threading.Thread(
            target=self._run, args=(display_size, max_fps, bit_rate, audio, stay_awake),
            daemon=True, name="scrcpy-client")
        self._thread.start()
        return True

    def stop(self):
        self._stopping = True
        self._running = False
        for s in (self._video, self._audio, self._control):
            if s is not None:
                try:
                    s.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    s.close()
                except OSError:
                    pass
        self._video = self._audio = self._control = None
        proc, self._proc = self._proc, None
        if proc is not None:
            try:
                proc.terminate()
                proc.wait(timeout=2)
            except Exception:
                try:
                    proc.kill()
                except Exception:
                    pass
        if self._port:
            self._adb_run(["forward", "--remove", f"tcp:{self._port}"], timeout=5)
            self._port = 0
        if self._thread and self._thread.is_alive() and threading.current_thread() is not self._thread:
            self._thread.join(timeout=3)
        self._stop_audio_sink()

    def inject_touch(self, pointer_id: int, action: int, x: int, y: int, pressure: float = 1.0):
        """Touch on the mirrored display, x/y in frame pixels."""
        if not self._running or self._width <= 0:
            return
        x = max(0, min(int(x), self._width - 1))
        y = max(0, min(int(y), self._height - 1))
        p = 0 if action == ACTION_UP else max(0, min(int(pressure * 0xFFFF), 0xFFFF))
        msg = struct.pack(">BBqiiHHHii", MSG_INJECT_TOUCH_EVENT, action, pointer_id,
                          x, y, self._width, self._height, p, 0, 0)
        self._send(msg)

    def inject_key(self, keycode: int, action: int = ACTION_DOWN, meta: int = 0):
        msg = struct.pack(">BBiii", MSG_INJECT_KEYCODE, action, keycode, 0, meta)
        self._send(msg)

    def press_key(self, keycode: int):
        self.inject_key(keycode, ACTION_DOWN)
        self.inject_key(keycode, ACTION_UP)

    def set_display_power(self, on: bool):
        self._send(struct.pack(">BB", MSG_SET_DISPLAY_POWER, 1 if on else 0))

    # ── internals ─────────────────────────────────────────────────────

    def _send(self, msg: bytes):
        sock = self._control
        if sock is None:
            return
        with self._send_lock:
            try:
                sock.sendall(msg)
            except OSError as e:
                logger.debug(f"control send failed: {e}")

    def _adb_run(self, args, timeout: int = 15) -> subprocess.CompletedProcess:
        cmd = [self._adb]
        if self._serial:
            cmd += ["-s", self._serial]
        cmd += args
        creationflags = subprocess.CREATE_NO_WINDOW if platform.system() == "Windows" else 0
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                              creationflags=creationflags)

    def _reap_stale_forwards(self):
        """Remove leftover `localabstract:scrcpy_*` forwards for this device.

        The forward lives in the adb server, so a hard-killed OCTAVE (no
        cleanup, PDEATHSIG cannot reach it) strands a listening port per
        session; they accumulate until the adb server restarts.
        """
        try:
            r = self._adb_run(["forward", "--list"], timeout=5)
        except (subprocess.SubprocessError, OSError):
            return
        for line in (r.stdout or "").splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[2].startswith("localabstract:scrcpy_") \
                    and (not self._serial or parts[0] == self._serial):
                logger.info(f"scrcpy client: removing stale forward {parts[1]} -> {parts[2]}")
                try:
                    self._adb_run(["forward", "--remove", parts[1]], timeout=5)
                except (subprocess.SubprocessError, OSError):
                    pass

    @staticmethod
    def _free_port() -> int:
        forced = os.environ.get("OCTAVE_SCRCPY_PORT")  # test hook (fake-server tests)
        if forced:
            return int(forced)
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
        s.close()
        return port

    def _fail(self, reason: str):
        if self._stopping:
            return
        self._stopping = True
        logger.warning(f"scrcpy client: {reason}")
        self._running = False
        self.disconnected.emit(reason)
        threading.Thread(target=self.stop, daemon=True).start()

    def _run(self, display_size: str, max_fps: int, bit_rate: int, audio: bool, stay_awake: bool):
        try:
            self._session(display_size, max_fps, bit_rate, audio, stay_awake)
        except Exception as e:  # any unexpected error ends the session cleanly
            self._fail(f"scrcpy client error: {e}")

    def _session(self, display_size, max_fps, bit_rate, audio, stay_awake):
        t0 = time.monotonic()
        # 1. push the server (cleanup=true deletes it on exit, so every time)
        r = self._adb_run(["push", self._jar, DEVICE_JAR_PATH], timeout=30)
        if r.returncode != 0:
            self._fail(f"could not push scrcpy server: {(r.stderr or r.stdout).strip()[-200:]}")
            return
        # 2. tunnel. scid must fit a signed 32-bit int on the server side.
        #    secrets, not random: other modules seed the global RNG (media
        #    colour extraction), which made every session's scid identical.
        self._scid = f"{secrets.randbits(31):08x}"
        self._reap_stale_forwards()
        self._port = self._free_port()
        r = self._adb_run(["forward", f"tcp:{self._port}", f"localabstract:scrcpy_{self._scid}"])
        if r.returncode != 0:
            self._fail(f"adb forward failed: {(r.stderr or r.stdout).strip()[-200:]}")
            return
        # 3. start the server
        opts = [
            f"scid={self._scid}", "tunnel_forward=true", "video=true",
            f"audio={'true' if audio else 'false'}", "control=true",
            "video_codec=h264", "max_size=0", f"video_bit_rate={bit_rate}",
            "audio_codec=raw",   # PCM s16le 48 kHz stereo: no decoder needed on our side
            f"max_fps={max_fps}", f"stay_awake={'true' if stay_awake else 'false'}",
            "cleanup=true", "send_device_meta=true", "send_frame_meta=true",
            "send_codec_meta=true", "send_dummy_byte=true", "log_level=info",
        ]
        if display_size:
            opts.append(f"new_display={display_size}")
        cmd = [self._adb]
        if self._serial:
            cmd += ["-s", self._serial]
        cmd += ["shell", f"CLASSPATH={DEVICE_JAR_PATH}", "app_process", "/",
                SERVER_CLASS, SERVER_VERSION, *opts]
        logger.info(f"scrcpy client: starting server: {' '.join(cmd[-(len(opts) + 5):])}")
        popen_kw = {}
        if platform.system() == "Windows":
            popen_kw["creationflags"] = subprocess.CREATE_NO_WINDOW
        else:
            popen_kw["preexec_fn"] = _die_with_parent
        self._proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **popen_kw)
        threading.Thread(target=self._drain_server_log, args=(self._proc,), daemon=True,
                         name="scrcpy-server-log").start()

        # 4. connect: the tunnel accepts before the server listens, so retry
        #    until the dummy byte actually arrives on the first (video) socket.
        #    The server then waits for EVERY socket (video, [audio], control)
        #    to be connected before it sends device/codec meta, so the control
        #    socket must be opened before reading anything else.
        video = self._connect_until_ready(deadline=t0 + 20.0)
        if video is None:
            return
        self._video = video
        try:
            if audio:
                audio_sock = socket.create_connection(("127.0.0.1", self._port), timeout=5)
                audio_sock.settimeout(None)
                self._audio = audio_sock
                threading.Thread(target=self._drain_audio, args=(audio_sock,), daemon=True,
                                 name="scrcpy-audio").start()
            control = socket.create_connection(("127.0.0.1", self._port), timeout=5)
            control.settimeout(None)
            control.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._control = control
        except OSError as e:
            self._fail(f"could not open control socket: {e}")
            return
        try:
            name = _recv_exact(video, 64).split(b"\x00", 1)[0].decode(errors="replace")
            codec_id, w, h = struct.unpack(">III", _recv_exact(video, 12))
        except (ConnectionError, OSError) as e:
            self._fail(f"handshake failed: {e}")
            return
        codec = codec_id.to_bytes(4, "big").decode(errors="replace")
        logger.info(f"scrcpy client: device '{name}', codec {codec}, {w}x{h}, "
                    f"handshake at +{time.monotonic() - t0:.2f} s")
        self._width, self._height = w, h
        threading.Thread(target=self._drain_device_messages, args=(control,), daemon=True,
                         name="scrcpy-devmsg").start()
        self._running = True
        self.frameSizeChanged.emit(w, h)
        self.connected.emit(w, h)

        # 5. demux + decode
        self._video_loop(video, t0)

    def _connect_until_ready(self, deadline: float) -> Optional[socket.socket]:
        while time.monotonic() < deadline and not self._stopping:
            if self._proc is not None and self._proc.poll() is not None:
                self._fail("scrcpy server exited during startup (see log)")
                return None
            s = None
            try:
                s = socket.create_connection(("127.0.0.1", self._port), timeout=2.0)
                s.settimeout(2.0)
                dummy = s.recv(1)
                if dummy:
                    s.settimeout(None)
                    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    return s
            except (OSError, socket.timeout):
                pass
            # Either EOF (tunnel accepted before the server listened) or a
            # timeout: close it, or a leaked connection is later handed out
            # by the server as the audio/control socket.
            if s is not None:
                try:
                    s.close()
                except OSError:
                    pass
            time.sleep(0.1)
        if not self._stopping:
            self._fail("scrcpy server did not answer within 20 s")
        return None

    def _drain_server_log(self, proc: subprocess.Popen):
        try:
            for raw in iter(proc.stdout.readline, b""):
                line = raw.decode(errors="replace").rstrip()
                if not line:
                    continue
                if "ERROR" in line or "WARN" in line:
                    logger.warning(f"scrcpy server: {line}")
                else:
                    # Startup INFO ("Device: ...", "New display ...") is the
                    # only trace of the server in a normal log: keep it visible.
                    logger.info(f"scrcpy server: {line}")
                self.serverLog.emit(line)
        except Exception:
            pass
        code = proc.poll()
        if proc is self._proc and not self._stopping:
            self._fail("Phone disconnected. Reconnect the USB cable." if self._running
                       else f"scrcpy server exited (code {code})")

    def _drain_audio(self, audio_sock: socket.socket):
        """Audio socket: 4-byte codec id, then [u64 pts][u32 size][PCM] packets."""
        try:
            (codec_id,) = struct.unpack(">I", _recv_exact(audio_sock, 4))
            if codec_id == AUDIO_STREAM_DISABLED:
                logger.warning("scrcpy client: phone cannot capture audio (Android 11+ required or "
                               "capture refused); mirroring video only")
                return
            if codec_id == AUDIO_STREAM_ERROR:
                logger.warning("scrcpy client: server reported an audio configuration error; video only")
                return
            if codec_id != AUDIO_CODEC_RAW:
                logger.warning(f"scrcpy client: unexpected audio codec 0x{codec_id:08x}; video only")
                return
            logger.info("scrcpy client: audio stream is PCM 48 kHz stereo, playing through OCTAVE")
            self._audio_active = True
            self.audioStateChanged.emit(True)
            while not self._stopping:
                header = _recv_exact(audio_sock, 12)
                _pts, size = struct.unpack(">QI", header)
                data = self._apply_gain(_recv_exact(audio_sock, size))
                with self._audio_lock:
                    self._audio_queue.append(data)
                    self._audio_queue_bytes += len(data)
                    # Bound latency: if the GUI thread fell behind, drop the oldest
                    while self._audio_queue_bytes > AUDIO_QUEUE_MAX_BYTES and len(self._audio_queue) > 1:
                        self._audio_queue_bytes -= len(self._audio_queue.popleft())
                self.audioReady.emit()
        except (ConnectionError, OSError, struct.error) as e:
            if not self._stopping:
                logger.info(f"scrcpy client: audio stream ended: {e}")
        finally:
            if self._audio_active:
                self._audio_active = False
                self.audioStateChanged.emit(False)

    def _apply_gain(self, pcm: bytes) -> bytes:
        """Multiply s16le samples by the gain with a soft limiter (tanh) so a
        hot source cannot clip harshly, and note whether the chunk carried
        sound (before gain) for ducking. Runs on the audio thread."""
        gain = self._audio_gain
        if np is None or len(pcm) < 4:
            if np is None and not self._gain_warned:
                self._gain_warned = True
                logger.warning("scrcpy client: numpy not available, phone audio gain ignored "
                               "and ducking follows the stream instead of the sound level")
            if np is None:
                self._audio_signal_seen = True   # cannot measure: treat the stream as sound
            return pcm
        samples = np.frombuffer(pcm[:len(pcm) - len(pcm) % 2], dtype="<i2")
        if int(np.abs(samples.astype(np.int32)).max()) >= AUDIO_SIGNAL_THRESHOLD:
            self._audio_signal_seen = True
        if abs(gain - 1.0) < 1e-3:
            return pcm
        x = samples.astype(np.float32) * (gain / 32768.0)
        y = np.tanh(x) * 32767.0
        return y.astype("<i2").tobytes()

    def _ensure_audio_sink(self) -> bool:
        if self._audio_sink is not None:
            return self._audio_io is not None
        fmt = QAudioFormat()
        fmt.setSampleRate(AUDIO_SAMPLE_RATE)
        fmt.setChannelCount(AUDIO_CHANNELS)
        fmt.setSampleFormat(QAudioFormat.SampleFormat.Int16)
        device = QMediaDevices.defaultAudioOutput()
        if device.isNull():
            logger.warning("scrcpy client: no audio output device; phone audio will not play")
            return False
        if not device.isFormatSupported(fmt):
            logger.warning("scrcpy client: default audio output does not accept 48 kHz stereo s16; phone audio will not play")
            return False
        sink = QAudioSink(device, fmt)
        sink.setBufferSize(AUDIO_SAMPLE_RATE * AUDIO_BYTES_PER_FRAME // 5)  # ~200 ms device buffer
        sink.setVolume(self._volume)
        io = sink.start()
        if io is None:
            logger.warning(f"scrcpy client: could not start audio output ({sink.error()})")
            return False
        self._audio_sink, self._audio_io = sink, io
        return True

    def _stop_audio_sink(self):
        self._audio_signal_seen = False
        self._audio_hold_timer.stop()
        self._set_audio_playing(False)
        sink, self._audio_sink, self._audio_io = self._audio_sink, None, None
        if sink is not None:
            try:
                sink.stop()
            except RuntimeError:
                pass
        with self._audio_lock:
            self._audio_queue.clear()
            self._audio_queue_bytes = 0

    @Slot()
    def _deliver_audio(self):
        # GUI thread. Write whatever is queued; never block on a full device
        # buffer — dropping keeps the audio close to the picture.
        if self._audio_signal_seen and not self._stopping:
            self._audio_signal_seen = False
            self._set_audio_playing(True)
            self._audio_hold_timer.start()   # (re)arm the release
        if self._stopping or not self._ensure_audio_sink():
            with self._audio_lock:
                self._audio_queue.clear()
                self._audio_queue_bytes = 0
            return
        while True:
            with self._audio_lock:
                if not self._audio_queue:
                    return
                chunk = self._audio_queue[0]
            free = self._audio_sink.bytesFree()
            if free < len(chunk):
                return  # try again on the next audioReady
            self._audio_io.write(chunk)
            with self._audio_lock:
                self._audio_queue.popleft()
                self._audio_queue_bytes -= len(chunk)

    def _drain_device_messages(self, control: socket.socket):
        # Clipboard notifications etc. — read and discard for now.
        try:
            while not self._stopping:
                if not control.recv(4096):
                    break
        except OSError:
            pass

    def _video_loop(self, video: socket.socket, t0: float):
        ctx = av.CodecContext.create("h264", "r")
        try:
            ctx.thread_type = "AUTO"
        except Exception:
            pass
        pending_config = b""
        first = True
        try:
            while not self._stopping:
                header = _recv_exact(video, 12)
                pts_flags, size = struct.unpack(">QI", header)
                data = _recv_exact(video, size)
                if pts_flags & FLAG_CONFIG:
                    # SPS/PPS: prepend to the next packet, as the scrcpy client does
                    pending_config = data
                    continue
                if pending_config:
                    data = pending_config + data
                    pending_config = b""
                packet = av.Packet(data)
                for frame in ctx.decode(packet):
                    self._push_frame(frame)
                    if first:
                        first = False
                        logger.info(f"scrcpy client: first frame at +{time.monotonic() - t0:.2f} s")
        except (ConnectionError, OSError) as e:
            if not self._stopping:
                self._fail("Phone disconnected. Reconnect the USB cable." if self._running
                           else f"video stream ended: {e}")
        except Exception as e:
            self._fail(f"decode error: {e}")

    # ── frame delivery ───────────────────────────────────────────────

    def _push_frame(self, frame):
        """Convert a PyAV frame to a QVideoFrame (YUV420P, no RGB conversion)."""
        if frame.format.name != "yuv420p":
            frame = frame.reformat(format="yuv420p")
        w, h = frame.width, frame.height
        if (w, h) != (self._width, self._height):
            self._width, self._height = w, h
            self.frameSizeChanged.emit(w, h)
        fmt = QVideoFrameFormat(QSize(w, h), QVideoFrameFormat.PixelFormat.Format_YUV420P)
        qframe = QVideoFrame(fmt)
        if not qframe.map(QVideoFrame.MapMode.WriteOnly):
            return
        try:
            for i, plane in enumerate(frame.planes[:3]):
                rows = h if i == 0 else (h + 1) // 2
                row_bytes = w if i == 0 else (w + 1) // 2
                src = memoryview(plane).cast("B")
                src_stride = plane.line_size
                dst = qframe.bits(i)
                dst_stride = qframe.bytesPerLine(i)
                if src_stride == dst_stride:
                    n = src_stride * rows
                    dst[:n] = src[:n]
                else:
                    for r in range(rows):
                        dst[r * dst_stride:r * dst_stride + row_bytes] = \
                            src[r * src_stride:r * src_stride + row_bytes]
        finally:
            qframe.unmap()
        with self._frame_lock:
            self._latest_frame = qframe
        self._frame_count += 1
        self.frameReady.emit()

    @Slot()
    def _deliver_frame(self):
        # Runs on the GUI thread (queued connection); coalesces if it falls behind.
        with self._frame_lock:
            frame, self._latest_frame = self._latest_frame, None
        if frame is not None and self._sink is not None:
            try:
                self._sink.setVideoFrame(frame)
            except RuntimeError:
                self._sink = None  # sink was destroyed with its VideoOutput
