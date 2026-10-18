"""Fake phone server: speaks the scrcpy 3.x tunnel_forward protocol on a local
TCP port, so the phone-mirror clients can be driven without a phone or adb.

Layout (phone_server/.../device/DesktopConnection.java, device/Streamer.java;
docs/PHONE_MIRROR_NATIVE_PLAN.md):
  1st connection = video socket: one dummy byte as soon as it is accepted
  2nd connection = control socket (no audio here)
  then on the video socket: 64-byte NUL-padded device name,
  codec meta [u32 'h264'][u32 width][u32 height],
  packets [u64 pts|flags][u32 size][Annex-B bytes], bit 63 config, bit 62 key frame.

Point a client at it with OCTAVE_SCRCPY_PORT=<port>. The asyncio loop runs on
a plain thread (not a QThread: PySide 6.11 segfaults at exit on QThread
wrappers made from Python threads).

Usage:
    with FakeScrcpyServer(script) as srv:   # script: list of steps, see below
        os.environ["OCTAVE_SCRCPY_PORT"] = str(srv.port)
        ...
Steps:
    ("config", bytes)      config packet (SPS/PPS)
    ("frame", bytes, key)  media packet
    ("raw", bytes)         arbitrary bytes on the video socket
    ("sleep", seconds)
    ("close",)             server exits: both sockets closed
After the script the server holds the sockets open until stop().
"""

import asyncio
import socket
import struct
import threading
from fractions import Fraction

FLAG_CONFIG = 1 << 63
FLAG_KEY_FRAME = 1 << 62
CODEC_H264 = 0x68323634   # "h264"


def packet(data: bytes, pts: int = 0, config: bool = False, key: bool = False) -> bytes:
    flags = FLAG_CONFIG if config else (pts | (FLAG_KEY_FRAME if key else 0))
    return struct.pack(">QI", flags, len(data)) + data


def free_port() -> int:
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class FakeScrcpyServer:
    def __init__(self, script, width: int, height: int, device_name: str = "Fake Phone",
                 handshake_close: bool = False):
        """handshake_close: exit after the dummy byte, before any meta."""
        self.script = list(script)
        self.width, self.height = width, height
        self.device_name = device_name
        self.handshake_close = handshake_close
        self.port = free_port()
        self.control_bytes = bytearray()   # everything the client sent on the control socket
        self.script_done = threading.Event()
        self._ready = threading.Event()
        self._loop = None
        self._thread = None
        self._stop = None

    # ── lifecycle ────────────────────────────────────────────────────

    def __enter__(self):
        self._thread = threading.Thread(target=self._run, daemon=True, name="fake-scrcpy-server")
        self._thread.start()
        assert self._ready.wait(5), "fake server did not start"
        return self

    def __exit__(self, *exc):
        self.stop()

    def stop(self):
        if self._loop and self._stop and not self._loop.is_closed():
            self._loop.call_soon_threadsafe(self._stop.set)
        if self._thread:
            self._thread.join(timeout=5)

    def _run(self):
        self._loop = asyncio.new_event_loop()
        try:
            self._loop.run_until_complete(self._main())
        finally:
            self._loop.close()

    # ── protocol ─────────────────────────────────────────────────────

    async def _main(self):
        self._stop = asyncio.Event()
        conns = asyncio.Queue()

        async def on_accept(reader, writer):
            await conns.put((reader, writer))

        server = await asyncio.start_server(on_accept, "127.0.0.1", self.port)
        self._ready.set()
        writers = []
        try:
            video_r, video_w = await conns.get()
            writers.append(video_w)
            video_w.write(b"\x00")                       # dummy byte
            await video_w.drain()
            if self.handshake_close:
                return
            control_r, control_w = await conns.get()
            writers.append(control_w)
            reader = asyncio.ensure_future(self._read_control(control_r))
            name = self.device_name.encode()[:63]
            video_w.write(name + b"\x00" * (64 - len(name)))
            video_w.write(struct.pack(">III", CODEC_H264, self.width, self.height))
            await video_w.drain()
            pts = 0
            for step in self.script:
                kind = step[0]
                if kind == "config":
                    video_w.write(packet(step[1], config=True))
                elif kind == "frame":
                    pts += 16_666
                    video_w.write(packet(step[1], pts=pts, key=step[2]))
                elif kind == "raw":
                    video_w.write(step[1])
                elif kind == "sleep":
                    await asyncio.sleep(step[1])
                elif kind == "close":
                    break
                await video_w.drain()
            else:
                self.script_done.set()
                await self._stop.wait()
            self.script_done.set()
            reader.cancel()
        except (ConnectionError, OSError):
            self.script_done.set()
        finally:
            for w in writers:
                w.close()
            server.close()
            await server.wait_closed()

    async def _read_control(self, reader):
        try:
            while True:
                data = await reader.read(4096)
                if not data:
                    return
                self.control_bytes += data
        except (ConnectionError, OSError):
            pass


# ── H.264 test streams ────────────────────────────────────────────────

def split_annexb(data: bytes):
    """Annex-B access unit -> (nal_type, nal bytes incl. start code) list."""
    starts = []
    i = 0
    while True:
        j = data.find(b"\x00\x00\x01", i)
        if j < 0:
            break
        s = j - 1 if j > 0 and data[j - 1] == 0 else j
        starts.append((s, j + 3))
        i = j + 3
    nals = []
    for n, (s, body) in enumerate(starts):
        end = starts[n + 1][0] if n + 1 < len(starts) else len(data)
        nals.append((data[body] & 0x1F, data[s:end]))
    return nals


def encode_h264(width: int, height: int, frames: int = 5):
    """Encode a mid-grey test clip like the phone's encoder sends it:
    returns (config, [(packet, is_key), ...]), SPS/PPS split out of the
    first access unit into the config packet."""
    import av
    codec = av.CodecContext.create("libx264", "w")
    codec.width, codec.height = width, height
    codec.pix_fmt = "yuv420p"
    codec.time_base = Fraction(1, 60)
    codec.options = {"preset": "ultrafast", "tune": "zerolatency", "bframes": "0", "g": "60"}
    out = []
    for n in range(frames):
        f = av.VideoFrame(width, height, "yuv420p")
        for i, plane in enumerate(f.planes):
            value = (100 + 8 * n) if i == 0 else 128   # bright enough not to count as black
            plane.update(bytes([value]) * plane.buffer_size)
        f.pts = n
        out += [bytes(p) for p in codec.encode(f)]
    out += [bytes(p) for p in codec.encode(None)]
    config, packets = b"", []
    for n, au in enumerate(out):
        nals = split_annexb(au)
        if n == 0:
            config = b"".join(b for t, b in nals if t in (7, 8))
            au = b"".join(b for t, b in nals if t not in (7, 8))
        packets.append((au, any(t == 5 for t, _ in nals)))
    return config, packets
