"""Service load baseline: one `octave --headless --dev` (a developer build),
60 s with no clients, then five clients for 120 s:
  - paramStore, every parameter id, with setParameterDemand on all, throttleMs 40
  - nowPlaying, six properties
  - audioAnalyzer.levels, with setDemand
  - phoneMirrorManager.video from sim_video over a WebSocket
  - the same over TCP
Prints the backend's CPU % (of one core) and RSS from /proc, events or
records per second, and event latency as the excess over the fastest
delivery seen (the server's `t` is its own monotonic clock).

    python3 tools/stream/service_load.py build/octave PORT CONFIG_DIR MUSIC_DIR [IDLE_S LOAD_S]

CONFIG_DIR is created and used as XDG_CONFIG_HOME (never the real config);
MUSIC_DIR holds at least one track, played during the load. Baseline
numbers: TODO/open-backend.md, "Service load baseline".
"""
import base64
import json
import os
import socket
import struct
import subprocess
import sys
import threading
import time

BIN, PORT, CFG, MEDIA = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
IDLE_S = int(sys.argv[5]) if len(sys.argv) > 5 else 60
LOAD_S = int(sys.argv[6]) if len(sys.argv) > 6 else 120
HZ = os.sysconf("SC_CLK_TCK")

os.makedirs(f"{CFG}/OCTAVE", exist_ok=True)
json.dump({"mediaFolder": MEDIA}, open(f"{CFG}/OCTAVE/settingsConfigure.json", "w"))
env = dict(os.environ, XDG_CONFIG_HOME=CFG, HOME=os.path.dirname(CFG), QT_QPA_PLATFORM="offscreen")
app = subprocess.Popen([BIN, "--headless", "--dev", "--service", str(PORT)], env=env,
                       stdout=open(f"{CFG}/app.log", "w"), stderr=subprocess.STDOUT)


def cpu_ticks():
    f = open(f"/proc/{app.pid}/stat").read().rsplit(")", 1)[1].split()
    return int(f[11]) + int(f[12])


def rss_mb():
    for line in open(f"/proc/{app.pid}/status"):
        if line.startswith("VmRSS"):
            return int(line.split()[1]) / 1024


def measure(seconds):
    t0, c0 = time.time(), cpu_ticks()
    rss = []
    while time.time() - t0 < seconds:
        time.sleep(1)
        rss.append(rss_mb())
    return 100.0 * (cpu_ticks() - c0) / HZ / (time.time() - t0), min(rss), max(rss), rss[-1]


def line_client():
    for _ in range(100):
        try:
            return socket.create_connection(("127.0.0.1", PORT), timeout=5)
        except OSError:
            time.sleep(0.2)
    raise SystemExit("backend did not come up")


def rpc(s, f, msg):
    s.sendall((json.dumps(msg) + "\n").encode())
    while True:
        r = json.loads(f.readline())
        if "ok" in r:
            return r


stop = threading.Event()
sockets = []
stats = {}


def json_reader(name, s, setup):
    f = s.makefile("rb")
    for m in setup:
        rpc(s, f, m)
    st = stats[name] = {"events": 0, "excess": []}
    best = None
    s.settimeout(None)
    sockets.append(s)
    while not stop.is_set():
        try:
            line = f.readline()
        except OSError:
            break
        if not line:
            break
        if line[:1] == b"\xff":
            continue
        e = json.loads(line)
        if "event" not in e:
            continue
        st["events"] += 1
        if "t" in e:
            d = time.monotonic() * 1000 - e["t"]
            best = d if best is None else min(best, d)
            st["excess"].append(d - best)


def tcp_video(name):
    s = line_client()
    s.sendall(b'{"op":"stream","object":"phoneMirrorManager","name":"video","id":1}\n')
    st = stats[name] = {"records": 0, "bytes": 0, "dropped": 0}
    buf = b""
    s.settimeout(1)
    while not stop.is_set():
        try:
            chunk = s.recv(1 << 20)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf += chunk
        while buf:
            if buf[0] == 0xFF:
                if len(buf) < 5:
                    break
                n = struct.unpack(">I", buf[1:5])[0]
                if len(buf) < 5 + n:
                    break
                st["records"] += 1
                st["bytes"] += n
                buf = buf[5 + n:]
            else:
                i = buf.find(b"\n")
                if i < 0:
                    break
                if b"streamDropped" in buf[:i]:
                    st["dropped"] += 1
                buf = buf[i + 1:]


def ws_video(name):
    s = line_client()
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall(f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
              f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    buf = buf.split(b"\r\n\r\n", 1)[1]
    p = json.dumps({"op": "stream", "object": "phoneMirrorManager", "name": "video", "id": 1}).encode()
    m = os.urandom(4)
    s.sendall(bytes([0x81, 0x80 | len(p)]) + m + bytes(b ^ m[i % 4] for i, b in enumerate(p)))
    st = stats[name] = {"records": 0, "bytes": 0}
    s.settimeout(1)
    while not stop.is_set():
        try:
            chunk = s.recv(1 << 20)
        except socket.timeout:
            continue
        if not chunk:
            break
        buf += chunk
        while len(buf) >= 2:
            n, off = buf[1] & 0x7F, 2
            if n == 126:
                if len(buf) < 4:
                    break
                n, off = struct.unpack(">H", buf[2:4])[0], 4
            elif n == 127:
                if len(buf) < 10:
                    break
                n, off = struct.unpack(">Q", buf[2:10])[0], 10
            if len(buf) < off + n:
                break
            if buf[0] & 0x0F == 2:
                st["records"] += 1
                st["bytes"] += n
            buf = buf[off + n:]


s0 = line_client()
f0 = s0.makefile("rb")
ids = [p["id"] for p in rpc(s0, f0, {"op": "get", "object": "parameterCatalog", "property": "parameters", "id": 1})["result"]]
time.sleep(5)   # the sim car connects
idle = measure(IDLE_S)

clients = [
    threading.Thread(target=json_reader, args=("params", line_client(), [
        {"op": "call", "object": "obdManager", "method": "setParameterDemand", "args": ["load-test", ids], "id": 1},
        {"op": "subscribe", "object": "paramStore", "throttleMs": 40, "id": 2}])),
    threading.Thread(target=json_reader, args=("nowPlaying", line_client(), [
        {"op": "subscribe", "object": "nowPlaying",
         "properties": ["title", "artist", "playing", "positionMs", "durationMs", "artRevision"], "id": 1}])),
    threading.Thread(target=json_reader, args=("levels", line_client(), [
        {"op": "call", "object": "audioAnalyzer", "method": "setDemand", "args": ["load-test", True], "id": 1},
        {"op": "subscribe", "object": "audioAnalyzer", "properties": ["levels"], "id": 2}])),
    threading.Thread(target=ws_video, args=("video-ws",)),
    threading.Thread(target=tcp_video, args=("video-tcp",)),
]
for t in clients:
    t.start()
time.sleep(1)
rpc(s0, f0, {"action": "sim_video", "args": {"on": True}})
rpc(s0, f0, {"action": "sim_throttle", "args": {"value": 40}})
files = sorted(os.listdir(MEDIA))
if files:
    rpc(s0, f0, {"op": "call", "object": "mediaManager", "method": "play_file", "args": [files[0]], "id": 9})
load = measure(LOAD_S)
playing = rpc(s0, f0, {"op": "get", "object": "nowPlaying", "property": "playing", "id": 10})["result"]
stop.set()
for k in sockets:
    try:
        k.shutdown(socket.SHUT_RDWR)
    except OSError:
        pass
for t in clients:
    t.join(5)
app.terminate()
app.wait(20)


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p / 100 * len(xs)))] if xs else float("nan")


print(f"idle  ({IDLE_S} s, no clients): CPU {idle[0]:.1f} %  RSS {idle[1]:.0f}-{idle[2]:.0f} MB")
print(f"load ({LOAD_S} s, 5 clients):  CPU {load[0]:.1f} %  RSS {load[1]:.0f}-{load[2]:.0f} MB (end {load[3]:.0f})")
for name, st in stats.items():
    if "excess" in st:
        ex = st["excess"]
        print(f"  {name:10s} {st['events']:6d} events ({st['events'] / LOAD_S:.1f}/s)  latency over best: "
              f"p50 {pct(ex, 50):.1f} ms  p95 {pct(ex, 95):.1f}  p99 {pct(ex, 99):.1f}  max {max(ex) if ex else 0:.1f}")
    else:
        print(f"  {name:10s} {st['records']:6d} records ({st['records'] / LOAD_S:.1f}/s, {st['bytes'] / LOAD_S / 1024:.0f} KB/s)"
              + (f"  dropped {st.get('dropped', 0)}" if "dropped" in st else ""))
print(f"  ids subscribed: {len(ids)}; local track playing during the run: {playing}")
