"""Service load harness: many clients at once against one OCTAVE.

Starts one OCTAVE with the simulated car (`--headless --sim --service PORT`,
or with --ui the QML UI offscreen; any build, release too), with its config
(`--config`), HOME and XDG dirs under WORKDIR, then runs each scenario in
turn for SECONDS:

  idle       no load clients: the reference row
  fanout20   20 clients, each with its own setParameterDemand on every PID
             and a paramStore subscription to every id, unthrottled
  nonreader  one client subscribed to paramStore, nowPlaying and the audio
             levels that never reads a byte
  callflood  one client pipelining `call volumeController.toLinear` (a pure
             function, but it runs on the GUI thread like every call)
  churn      two clients connecting, subscribing, reading one event and
             disconnecting in a loop
  wsmix      8 WebSocket clients on paramStore (throttleMs 40) + nowPlaying,
             2 WebSocket and 1 TCP client on the phone video (sim.setVideo)
  all        every scenario above at once
  media      the 2026-10-22 baseline: paramStore (40 ms), nowPlaying,
             audio levels, the video over WebSocket and over TCP

A probe runs through every scenario so the rows compare:
  - a demand on every PID (so the poll set is the same in every row) and a
    paramStore subscription: values a second reaching a healthy client, and
    event latency as the excess over the fastest delivery seen
  - a `get` every 100 ms: the GUI thread's round trip (where the QML UI
    waits too)
  - sim.status() once a second: Mode 01 replies the simulated adapter sent
    (the OBD poll rate) and the longest gap between two
  - the backend's CPU % (of one core) and RSS from /proc

    python3 tools/stream/service_load.py BUILD/octave PORT WORKDIR MUSIC_DIR \\
        [--ui] [--seconds 30] [--scenario NAME ...]

Prints a Markdown table (plus WORKDIR/results-<mode>.json). WORKDIR is
wiped; it must not be under ~/.config. Results: wiki/utilities.html
"Many clients".
"""
import argparse
import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time

SCENARIOS = ["idle", "fanout20", "nonreader", "callflood", "churn", "wsmix", "all", "media", "quick"]

ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
ap.add_argument("bin")
ap.add_argument("port", type=int, help="0: any free one")
ap.add_argument("workdir")
ap.add_argument("media", nargs="?", help="tracks to play during the load (default: none)")
ap.add_argument("--ui", action="store_true", help="the QML UI offscreen instead of --headless")
ap.add_argument("--seconds", type=int, default=30)
ap.add_argument("--scenario", nargs="+", choices=SCENARIOS, default=SCENARIOS[:-2])
ap.add_argument("--check", action="store_true",
                help="the ctest (service_load_check): idle and quick for a few seconds each, "
                     "exit 1 unless the limits hold")
opt = ap.parse_args()
WARMUP_S, SETTLE_S = 2, 3
if opt.check:
    opt.scenario, opt.seconds, WARMUP_S, SETTLE_S = ["idle", "quick"], 4, 1, 0.5
if opt.port == 0:
    with socket.socket() as probe_port:
        probe_port.bind(("127.0.0.1", 0))
        opt.port = probe_port.getsockname()[1]

PORT = opt.port
WORK = os.path.abspath(opt.workdir)
if PORT == 9876 or WORK.startswith(os.path.expanduser("~/.config")) or WORK == os.path.expanduser("~"):
    raise SystemExit("refusing: port 9876 and the real config are someone's running app")
HZ = os.sysconf("SC_CLK_TCK")
MODE = "ui" if opt.ui else "headless"

# Everything the app could read or write lives under WORK: config, HOME
# (Downloads, ~/Android/Sdk, which is also why no adb is found), data, cache
shutil.rmtree(WORK, ignore_errors=True)
for d in ("cfg/OCTAVE", "home/.local/share", "home/.cache"):
    os.makedirs(f"{WORK}/{d}")
if not opt.media:   # nothing to play: an empty folder, never the user's ~/Music
    opt.media = f"{WORK}/music"
    os.makedirs(opt.media)
json.dump({"mediaFolder": os.path.abspath(opt.media), "esp32VolumeEnabled": False},
          open(f"{WORK}/cfg/OCTAVE/settingsConfigure.json", "w"))
env = dict(os.environ, HOME=f"{WORK}/home", XDG_CONFIG_HOME=f"{WORK}/cfg",
           XDG_DATA_HOME=f"{WORK}/home/.local/share", XDG_CACHE_HOME=f"{WORK}/home/.cache",
           QT_QPA_PLATFORM="offscreen")
# The simulated car is the `sim` service object (--sim, every build); with
# --config it uses our folder instead of seeding a throwaway one
args = [opt.bin, "--sim", "--config", f"{WORK}/cfg/OCTAVE", "--service", str(PORT)]
if not opt.ui:
    args.insert(1, "--headless")
app = subprocess.Popen(args, env=env, stdout=open(f"{WORK}/app.log", "w"), stderr=subprocess.STDOUT)


def stop_app():
    if app.poll() is None:
        app.terminate()
        try:
            app.wait(20)
        except subprocess.TimeoutExpired:
            app.kill()


def cpu_ticks():
    f = open(f"/proc/{app.pid}/stat").read().rsplit(")", 1)[1].split()
    return int(f[11]) + int(f[12])


def rss_mb():
    for line in open(f"/proc/{app.pid}/status"):
        if line.startswith("VmRSS"):
            return int(line.split()[1]) / 1024
    return 0.0


def connect(timeout=5):
    for _ in range(150):
        if app.poll() is not None:
            raise SystemExit(f"the app exited ({app.returncode}); see {WORK}/app.log")
        try:
            return socket.create_connection(("127.0.0.1", PORT), timeout=timeout)
        except OSError:
            time.sleep(0.2)
    raise SystemExit("the app did not come up")


class Line:
    """One JSON-lines connection, used from one thread."""

    def __init__(self):
        self.s = connect()
        self.f = self.s.makefile("rb")
        self.next = 0

    def rpc(self, msg):
        self.next += 1
        msg = dict(msg, id=self.next) if "op" in msg else msg
        self.s.sendall((json.dumps(msg) + "\n").encode())
        while True:
            line = self.f.readline()
            if not line:
                raise OSError("closed")
            if line[:1] == b"\xff":
                continue
            r = json.loads(line)
            if "ok" in r and (r.get("id") == msg.get("id")):
                return r

    def close(self):
        try:
            self.s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.s.close()


class Ws:
    """One WebSocket connection (text frames out, any frames in)."""

    def __init__(self):
        self.s = connect()
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall(f"GET / HTTP/1.1\r\nHost: 127.0.0.1:{PORT}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                       f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n".encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.s.recv(4096)
            if not chunk:
                raise OSError("closed during the upgrade")
            buf += chunk
        if b" 101 " not in buf.split(b"\r\n", 1)[0]:
            raise OSError("upgrade refused: " + buf.split(b"\r\n", 1)[0].decode(errors="replace"))
        self.buf = buf.split(b"\r\n\r\n", 1)[1]

    def send(self, msg):
        p = json.dumps(msg).encode()
        m = os.urandom(4)
        n = len(p)
        head = bytes([0x81, 0x80 | n]) if n < 126 else bytes([0x81, 0x80 | 126]) + struct.pack(">H", n)
        self.s.sendall(head + m + bytes(b ^ m[i % 4] for i, b in enumerate(p)))

    def frames(self, stop):
        """Yield (opcode, payload) until the socket closes or `stop` is set."""
        self.s.settimeout(0.5)
        while not stop.is_set():
            while len(self.buf) >= 2:
                n, off = self.buf[1] & 0x7F, 2
                if n == 126:
                    if len(self.buf) < 4:
                        break
                    n, off = struct.unpack(">H", self.buf[2:4])[0], 4
                elif n == 127:
                    if len(self.buf) < 10:
                        break
                    n, off = struct.unpack(">Q", self.buf[2:10])[0], 10
                if len(self.buf) < off + n:
                    break
                op, payload = self.buf[0] & 0x0F, self.buf[off:off + n]
                self.buf = self.buf[off + n:]
                yield op, payload
            try:
                chunk = self.s.recv(1 << 20)
            except TimeoutError:
                continue
            except OSError:
                return
            if not chunk:
                return
            self.buf += chunk

    def close(self):
        try:
            self.s.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.s.close()


def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(p / 100 * len(xs)))] if xs else float("nan")


# ---------------------------------------------------------------------------
# Load clients. Each takes (stop, st): runs until `stop`, counts into `st`

def read_lines(c, stop, st):
    """Count events (and `dropped` events) on a line connection without parsing them."""
    c.s.settimeout(0.5)
    buf = b""
    while not stop.is_set():
        try:
            chunk = c.s.recv(1 << 18)
        except TimeoutError:
            continue
        except OSError:
            st["closed"] = st.get("closed", 0) + 1
            return
        if not chunk:
            st["closed"] = st.get("closed", 0) + 1
            return
        buf += chunk
        lines = buf.split(b"\n")
        buf = lines.pop()
        for line in lines:
            if line.startswith(b'{"event"') or b'"event":' in line[:40]:
                st["events"] = st.get("events", 0) + 1
                if b'"dropped"' in line[:60]:
                    st["dropped"] = st.get("dropped", 0) + 1
        st["bytes"] = st.get("bytes", 0) + len(chunk)


def param_client(name, ids, throttle=0, demand=True):
    def run(stop, st):
        c = Line()
        if demand:
            c.rpc({"op": "call", "object": "obdManager", "method": "setParameterDemand", "args": [name, ids]})
        sub = {"op": "subscribe", "object": "paramStore"}
        if throttle:
            sub["throttleMs"] = throttle
        r = c.rpc(sub)
        if not r["ok"]:
            st["errors"] = st.get("errors", 0) + 1
        read_lines(c, stop, st)
        c.close()
    return run


def nonreader(stop, st):
    c = Line()
    c.rpc({"op": "call", "object": "audioAnalyzer", "method": "setDemand", "args": ["load-nonreader", True]})
    c.rpc({"op": "subscribe", "object": "paramStore"})
    c.rpc({"op": "subscribe", "object": "nowPlaying", "properties": ["title", "playing", "positionMs"]})
    c.rpc({"op": "subscribe", "object": "audioAnalyzer", "properties": ["levels"]})
    c.s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    stop.wait()
    # Still open? A write the server has closed fails (at the latest, the second)
    try:
        for _ in range(2):
            c.s.sendall(b'{"op":"hello","id":99}\n')
            time.sleep(0.2)
        st["openAtEnd"] = 1
    except OSError:
        st["openAtEnd"] = 0
    c.close()


def callflood(stop, st, window=64):
    c = Line()
    msg = b'{"op":"call","object":"volumeController","method":"toLinear","args":[50]}\n'
    inflight = 0
    c.s.settimeout(0.5)
    buf = b""
    st.update(replies=0, errors=0, codes={})
    while not stop.is_set():
        if inflight < window:
            try:
                c.s.sendall(msg * (window - inflight))
            except OSError:
                st["closed"] = 1
                return
            inflight = window
        try:
            chunk = c.s.recv(1 << 18)
        except TimeoutError:
            continue
        except OSError:
            st["closed"] = 1
            return
        if not chunk:
            st["closed"] = 1
            return
        buf += chunk
        lines = buf.split(b"\n")
        buf = lines.pop()
        for line in lines:
            if b'"ok"' not in line:
                continue
            inflight -= 1
            if b'"ok":true' in line:
                st["replies"] += 1
            else:
                st["errors"] += 1
                code = json.loads(line).get("code", "-")
                st["codes"][code] = st["codes"].get(code, 0) + 1
    c.close()


def churn(stop, st):
    st.setdefault("cycles", 0)
    while not stop.is_set():
        try:
            c = Line()
            c.rpc({"op": "hello"})
            c.rpc({"op": "subscribe", "object": "paramStore"})
            c.s.settimeout(1)
            while True:
                line = c.f.readline()
                if not line or line.startswith(b'{"event"'):
                    break
            c.close()
            st["cycles"] += 1
        except OSError:
            st["errors"] = st.get("errors", 0) + 1


def ws_params(stop, st):
    w = Ws()
    w.send({"op": "subscribe", "object": "paramStore", "throttleMs": 40, "id": 1})
    w.send({"op": "subscribe", "object": "nowPlaying", "properties": ["title", "playing", "positionMs"], "id": 2})
    for op, payload in w.frames(stop):
        if op == 1 and payload.startswith(b'{"event"'):
            st["events"] = st.get("events", 0) + 1
    w.close()


def ws_video(stop, st):
    w = Ws()
    w.send({"op": "stream", "object": "phoneMirrorManager", "name": "video", "id": 1})
    st.setdefault("records", 0)
    st.setdefault("bytes", 0)
    for op, payload in w.frames(stop):
        if op == 2:
            st["records"] += 1
            st["bytes"] += len(payload)
        elif op == 1 and b"streamDropped" in payload:
            st["dropped"] = st.get("dropped", 0) + 1
    w.close()


def tcp_video(stop, st):
    c = Line()
    c.s.sendall(b'{"op":"stream","object":"phoneMirrorManager","name":"video","id":1}\n')
    st.setdefault("records", 0)
    st.setdefault("bytes", 0)
    buf = b""
    c.s.settimeout(0.5)
    while not stop.is_set():
        try:
            chunk = c.s.recv(1 << 20)
        except TimeoutError:
            continue
        except OSError:
            break
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
                    st["dropped"] = st.get("dropped", 0) + 1
                buf = buf[i + 1:]
    c.close()


def levels_client(stop, st):
    c = Line()
    c.rpc({"op": "call", "object": "audioAnalyzer", "method": "setDemand", "args": ["load-levels", True]})
    c.rpc({"op": "subscribe", "object": "audioAnalyzer", "properties": ["levels"]})
    read_lines(c, stop, st)
    c.close()


def now_playing_client(stop, st):
    c = Line()
    c.rpc({"op": "subscribe", "object": "nowPlaying",
           "properties": ["title", "artist", "playing", "positionMs", "durationMs", "artRevision"]})
    read_lines(c, stop, st)
    c.close()


def scenario_clients(name, ids):
    """[(group, fn)] for one scenario; `group` names the stats row."""
    out = []
    if name in ("fanout20", "all", "quick"):
        out += [("fanout", param_client(f"load-{i}", ids)) for i in range(20)]
    if name in ("nonreader", "all"):
        out.append(("nonreader", nonreader))
    if name in ("callflood", "all", "quick"):
        out.append(("callflood", callflood))
    if name in ("churn", "all"):
        out += [("churn", churn)] * 2
    if name in ("wsmix", "all"):
        out += [("ws-params", ws_params)] * 8 + [("ws-video", ws_video)] * 2 + [("tcp-video", tcp_video)]
    if name == "media":
        out += [("params40", param_client("load-test", ids, throttle=40)), ("nowPlaying", now_playing_client),
                ("levels", levels_client), ("ws-video", ws_video), ("tcp-video", tcp_video)]
    return out


# ---------------------------------------------------------------------------
# The probe

def probe_params(stop, st, ids):
    c = Line()
    c.rpc({"op": "call", "object": "obdManager", "method": "setParameterDemand", "args": ["load-probe", ids]})
    c.rpc({"op": "subscribe", "object": "paramStore"})
    c.s.settimeout(0.5)
    best = None
    while not stop.is_set():
        try:
            line = c.f.readline()
        except TimeoutError:
            continue
        except OSError:
            break
        if not line:
            break
        if not line.startswith(b'{"event":"params"'):
            continue
        e = json.loads(line)
        st["values"] += len(e.get("values", {}))
        if "t" in e:
            d = time.monotonic() * 1000 - e["t"]
            best = d if best is None or d < best else best
            st["excess"].append(d - best)
    c.close()


def probe_rtt(stop, st):
    c = Line()
    while not stop.is_set():
        t0 = time.monotonic()
        c.rpc({"op": "get", "object": "volumeController", "property": "percent"})
        st["rtt"].append((time.monotonic() - t0) * 1000)
        stop.wait(0.1)
    c.close()


def sim_status(c):
    r = c.rpc({"op": "call", "object": "sim", "method": "status"})
    if not r.get("ok") or not r["result"]:   # not up yet (or not a simulated run)
        raise LookupError(r.get("error", "sim.status() is empty"))
    return r["result"]


# ---------------------------------------------------------------------------

def run_scenario(name, ids, seconds, sim):
    probe = {"values": 0, "excess": [], "rtt": []}
    stop = threading.Event()
    threads = [threading.Thread(target=probe_params, args=(stop, probe, ids)),
               threading.Thread(target=probe_rtt, args=(stop, probe))]
    groups = {}
    for group, fn in scenario_clients(name, ids):
        st = {}
        groups.setdefault(group, []).append(st)
        threads.append(threading.Thread(target=fn, args=(stop, st), daemon=True))
    for t in threads:
        t.start()
    time.sleep(WARMUP_S)   # subscriptions in place
    probe.update(values=0, excess=[], rtt=[])
    for g in groups.values():
        for st in g:
            for k, v in list(st.items()):
                if isinstance(v, int) and k not in ("openAtEnd", "closed"):
                    st[k] = 0
                elif isinstance(v, dict):
                    st[k] = {}
    polls0 = sim_status(sim)["polls"]
    gaps, rss = [], []
    t0, c0, h0 = time.monotonic(), cpu_ticks(), os.times()
    while time.monotonic() - t0 < seconds:
        time.sleep(1)
        s = sim_status(sim)
        gaps.append(s["poll_max_gap_ms"])
        rss.append(rss_mb())
    elapsed = time.monotonic() - t0
    polls = sim_status(sim)["polls"] - polls0
    cpu = 100.0 * (cpu_ticks() - c0) / HZ / elapsed
    h1 = os.times()
    harness_cpu = 100.0 * ((h1.user - h0.user) + (h1.system - h0.system)) / elapsed
    stop.set()
    for t in threads:
        t.join(5)
    row = {
        "scenario": name, "seconds": round(elapsed, 1), "cpu": round(cpu, 1),
        "rssEnd": round(rss[-1]), "rssMax": round(max(rss)), "harnessCpu": round(harness_cpu),
        "pollsPerS": round(polls / elapsed, 1), "pollGapMaxMs": max(gaps),
        "probeValuesPerS": round(probe["values"] / elapsed), "probeP99Ms": round(pct(probe["excess"], 99), 1),
        "rttP50Ms": round(pct(probe["rtt"], 50), 1), "rttP99Ms": round(pct(probe["rtt"], 99), 1),
        "rttMaxMs": round(max(probe["rtt"]) if probe["rtt"] else float("nan"), 1),
        "clients": {},
    }
    for group, sts in groups.items():
        agg = {}
        for st in sts:
            for k, v in st.items():
                if isinstance(v, int):
                    agg[k] = agg.get(k, 0) + v
                elif isinstance(v, dict):
                    for kk, vv in v.items():
                        agg.setdefault(k, {})[kk] = agg.setdefault(k, {}).get(kk, 0) + vv
        agg["n"] = len(sts)
        if group == "fanout":
            rates = sorted(st.get("events", 0) / elapsed for st in sts)
            agg["eventsPerSMin"], agg["eventsPerSMax"] = round(rates[0], 1), round(rates[-1], 1)
        row["clients"][group] = agg
    time.sleep(SETTLE_S)   # let the closed connections go before the next row
    return row


def describe(row):
    parts = []
    for group, a in row["clients"].items():
        s = row["seconds"]
        if group == "fanout":
            parts.append(f"fanout {a['eventsPerSMin']}-{a['eventsPerSMax']} ev/s each, {a.get('dropped', 0)} dropped")
        elif group == "nonreader":
            parts.append("non-reader " + ("still open" if a.get("openAtEnd") else "closed by the server"))
        elif group == "callflood":
            codes = ", ".join(f"{k} {v}" for k, v in a.get("codes", {}).items())
            parts.append(f"calls {a.get('replies', 0) / s:.0f}/s ok" + (f", refused: {codes}" if codes else "")
                         + (", closed" if a.get("closed") else ""))
        elif group == "churn":
            parts.append(f"churn {a.get('cycles', 0) / s:.1f} cycles/s" + (f", {a['errors']} errors" if a.get("errors") else ""))
        elif "video" in group:
            parts.append(f"{group} {a.get('records', 0) / s / a['n']:.0f} rec/s each" + (f", {a['dropped']} dropped" if a.get("dropped") else ""))
        else:
            parts.append(f"{group} {a.get('events', 0) / s / a['n']:.1f} ev/s each")
    return "; ".join(parts)


try:
    ctl = Line()
    ids = [p["id"] for p in ctl.rpc({"op": "get", "object": "parameterCatalog", "property": "parameters"})["result"]]
    log = open(f"{WORK}/app.log").read()
    if "Using adb" in log:
        raise SystemExit("the app found an adb: stopping before it talks to a device")
    for _ in range(60):   # the simulated car connects
        try:
            if sim_status(ctl)["polls"] > 0:
                break
        except LookupError:
            pass
        time.sleep(0.5)
    else:
        raise SystemExit("the simulated car never answered a poll")
    ctl.rpc({"op": "call", "object": "sim", "method": "setThrottle", "args": [40]})
    if any(s in opt.scenario for s in ("wsmix", "all", "media")):
        ctl.rpc({"op": "call", "object": "sim", "method": "setVideo", "args": [True]})
    files = sorted(f for f in os.listdir(opt.media) if not f.startswith(".")) if opt.media else []
    if files:
        ctl.rpc({"op": "call", "object": "mediaManager", "method": "play_file", "args": [files[0]]})
    rows = []
    for name in opt.scenario:
        print(f"[{MODE}] {name} ...", file=sys.stderr, flush=True)
        rows.append(run_scenario(name, ids, opt.seconds, ctl))
    playing = ctl.rpc({"op": "get", "object": "nowPlaying", "property": "playing"})["result"]
    ctl.close()
finally:
    stop_app()

json.dump({"mode": MODE, "ids": len(ids), "seconds": opt.seconds, "playing": playing, "rows": rows},
          open(f"{WORK}/results-{MODE}.json", "w"), indent=1)
print(f"\n{MODE}: {len(ids)} ids, {opt.seconds} s a row, local track playing: {playing}\n")
print("| Scenario | CPU % | RSS MB | Polls/s | Poll gap max ms | Probe values/s | Probe p99 ms | GUI RTT p50 / p99 / max ms | Clients |")
print("|---|---|---|---|---|---|---|---|---|")
for r in rows:
    print(f"| {r['scenario']} | {r['cpu']} | {r['rssEnd']} | {r['pollsPerS']} | {r['pollGapMaxMs']} | {r['probeValuesPerS']} "
          f"| {r['probeP99Ms']} | {r['rttP50Ms']} / {r['rttP99Ms']} / {r['rttMaxMs']} | {describe(r) or '-'} |")
print(f"\n(harness CPU per row: {', '.join(str(r['harnessCpu']) for r in rows)} % of one core)")

if opt.check:
    # Loose bounds, so a slow CI machine passes and a broken limit does not
    idle, quick = rows
    fan, flood = quick["clients"]["fanout"], quick["clients"]["callflood"]
    floodRate = flood.get("replies", 0) / quick["seconds"]
    pollChange = abs(quick["pollsPerS"] - idle["pollsPerS"]) / max(idle["pollsPerS"], 0.1)
    checks = [
        ("the simulated car is polled", idle["pollsPerS"] > 1),
        ("no subscriber dropped events or lost its connection",
         not (fan.get("dropped") or fan.get("closed") or fan.get("errors"))),
        ("every subscriber kept getting events (at least 5 a second)", fan["eventsPerSMin"] >= 5),
        ("the flooder was limited (rateLimited, or closed for ignoring it)",
         bool(flood.get("closed") or flood.get("codes", {}).get("rateLimited"))),
        (f"the flooder got at most 300 answers a second ({floodRate:.0f})", floodRate <= 300),
        (f"the poll rate stayed within 20 % of idle ({pollChange * 100:.0f} %)", pollChange <= 0.2),
    ]
    print()
    for what, ok in checks:
        print(("ok    " if ok else "FAIL  ") + what)
    sys.exit(0 if all(ok for _, ok in checks) else 1)
