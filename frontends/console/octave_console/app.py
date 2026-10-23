"""OCTAVE console: an operator's terminal UI over the service API."""

import os
import time
import urllib.request
from functools import partial

from rich.text import Text
from textual import work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.command import DiscoveryHit, Hit, Provider
from textual.css.query import NoMatches
from textual.widgets import Footer, Static, TabbedContent, TabPane

from .link import Link, Linked, OctaveError, OctaveEvent, Unlinked
from .screens import EditValue
from .panes import DownloadsPane, ExplorerPane, LivePane, ObdPane, SettingsPane, TripsPane, fmt_duration
from .panes_car import CarPane
from .panes_dash import DashboardsPane
from .panes_media import LibraryPane
from .panes_system import PhonePane, SystemPane

PANES = [  # (id, title, class); the title's first character is its key
    ("live", "1 Live", LivePane),
    ("obd", "2 OBD", ObdPane),
    ("trips", "3 Trips", TripsPane),
    ("library", "4 Library", LibraryPane),
    ("downloads", "5 Downloads", DownloadsPane),
    ("dash", "6 Dashboards", DashboardsPane),
    ("car", "7 Car", CarPane),
    ("phone", "8 Phone", PhonePane),
    ("system", "9 System", SystemPane),
    ("settings", "0 Settings", SettingsPane),
    ("api", "` API", ExplorerPane),
]
TAB_KEYS = {"`": "grave_accent"}


class StatusLine(Static):
    """Top line: link, car, black box, clock."""

    def show(self, app):
        t = Text()
        if app.connected:
            info = app.server.get("app", {})
            t.append(" OCTAVE ", style="bold black on #e0a030")
            t.append(f" {info.get('version', '?')} {info.get('build', '')}  proto {app.server.get('protocolVersion')}"
                     f"  {app.link.address}{'  headless' if info.get('headless') else ''} ")
        else:
            t.append(" NO LINK ", style="bold white on red")
            t.append(f" {app.link_reason} ")
        obd = app.state.get("obdManager", {})
        if app.connected:
            state = obd.get("connectionState", "?")
            t.append("  OBD ", style="bold")
            t.append(state, style="green" if obd.get("connected") else "yellow")
            car = " ".join(str(x) for x in (obd.get("vehicleMake"), obd.get("vehicleModelYear")) if x)
            if car:
                t.append(f"  {car}")
            trip = app.state.get("tripRecorder", {})
            if trip.get("recording"):
                t.append("  ● REC ", style="bold red")
                t.append("")
                t.append(fmt_duration(trip.get("currentTripElapsedS")))
            if app.state.get("_mil"):
                t.append("  MIL", style="bold red")
        if app.note:
            t.append(f"   {app.note}", style="italic dim")
        clock = app.state.get("clock", {}).get("time")
        if clock:
            t.append(f"   {clock}", style="bold")
        self.update(t)


class MediaBar(Static):
    """Bottom bar, always visible: now playing, position and volume."""

    def show(self, app):
        n = app.state.get("nowPlaying", {})
        v = app.state.get("volumeController", {})
        t = Text()
        t.append(" ▶ " if n.get("playing") else " ❚❚ ", style="bold black on #e0a030")
        if n.get("hasTrack") or n.get("title"):
            t.append(f" {n.get('title') or '?'}", style="bold")
            if n.get("artist"):
                t.append(f" — {n['artist']}")
        else:
            t.append(" nothing playing", style="dim")
        pos, dur = (n.get("positionMs") or 0), (n.get("durationMs") or 0)
        width = 24
        filled = int(width * pos / dur) if dur else 0
        t.append(f"  {fmt_duration(pos / 1000)} ")
        t.append("━" * filled, style="#e0a030")
        t.append("─" * (width - filled), style="dim")
        t.append(f" {fmt_duration(dur / 1000)}")
        if n.get("shuffled"):
            t.append("  SHUF", style="bold")
        t.append(f"  [{n.get('source', '-')}]", style="dim")
        pct = v.get("percent", 0) or 0
        t.append("   VOL ", style="bold")
        if v.get("effectivelyMuted") or v.get("muted"):
            t.append("MUTE", style="bold red")
        else:
            t.append("▮" * (pct // 10) + "▯" * (10 - pct // 10), style="#e0a030")
        t.append(f" {pct:3d}%")
        self.update(t)


class Commands(Provider):
    """The command palette (ctrl+p)."""

    def commands(self):
        app = self.app
        out = [(f"Go to {title[2:]}", partial(app.action_tab, pid), f"Show the {title[2:]} pane") for pid, title, _ in PANES]
        out += [
            ("Play / pause", app.action_play, "nowPlaying.togglePlay"),
            ("Next track", app.action_next, "nowPlaying.next"),
            ("Previous track", app.action_previous, "nowPlaying.previous"),
            ("Toggle shuffle", app.action_shuffle, "nowPlaying.toggleShuffle"),
            ("Mute / unmute", app.action_mute, "volumeController.muted"),
            ("Volume up", partial(app.action_volume, 5), "+5 %"),
            ("Volume down", partial(app.action_volume, -5), "-5 %"),
            ("OBD: connect", partial(app.invoke, "obdManager", "connect_obd", note="connecting"), "obdManager.connect_obd"),
            ("OBD: reconnect", partial(app.invoke, "obdManager", "reconnect", note="reconnecting"), "obdManager.reconnect"),
            ("OBD: disconnect", partial(app.invoke, "obdManager", "close", note="disconnecting"), "obdManager.close"),
            ("OBD: scan vehicle", partial(app.invoke, "obdManager", "scan_vehicle", note="scanning"), "supported PIDs"),
            ("OBD: read trouble codes", partial(app.invoke, "obdManager", "read_dtc", note="reading codes"), "read_dtc"),
            ("OBD: clear trouble codes…", app.clear_dtcs, "asks first"),
            ("OBD: read freeze frame", partial(app.invoke, "obdManager", "read_freeze_frame"), "read_freeze_frame"),
            ("OBD: Bluetooth adapter scan", partial(app.invoke, "obdAdapterFinder", "startScan"), "obdAdapterFinder"),
            ("Black box: end trip now", partial(app.invoke, "tripRecorder", "endTripNow", note="trip ended"),
             "tripRecorder.endTripNow"),
            ("Black box: refresh trips", partial(app.invoke, "tripRecorder", "refresh"), "tripRecorder.refresh"),
            ("Toggle dark / light", app.action_toggle_dark, "terminal theme"),
            ("Quit", app.exit, "close the console"),
        ]
        return out

    async def discover(self):
        for name, callback, help_text in self.commands():
            yield DiscoveryHit(name, callback, help=help_text)

    async def search(self, query):
        matcher = self.matcher(query)
        for name, callback, help_text in self.commands():
            score = matcher.match(name)
            if score > 0:
                yield Hit(score, matcher.highlight(name), callback, help=help_text)


class OctaveConsole(App):
    TITLE = "OCTAVE console"
    CSS_PATH = "app.tcss"
    COMMANDS = App.COMMANDS | {Commands}
    BINDINGS = [
        Binding("space", "play", "Play/Pause"),
        Binding("less_than_sign", "previous", "Prev", key_display="<"),
        Binding("greater_than_sign", "next", "Next", key_display=">"),
        Binding("comma", "seek(-10000)", "-10s", show=False),
        Binding("full_stop", "seek(10000)", "+10s", show=False),
        Binding("plus,equals_sign", "volume(5)", "Vol+", key_display="+"),
        Binding("minus", "volume(-5)", "Vol-", key_display="-"),
        Binding("m", "mute", "Mute"),
        Binding("z", "shuffle", "Shuffle", show=False),
        *[Binding(TAB_KEYS.get(title[0], title[0]), f"tab('{pid}')", title[2:], show=False) for pid, title, _ in PANES],
        Binding("ctrl+p", "command_palette", "Palette"),
        Binding("q", "quit", "Quit"),
    ]

    def __init__(self, host="127.0.0.1", port=9876, start_tab="live"):
        super().__init__()
        self.link = Link(self, host, port)
        self.state = {}         # object -> {property: value}
        self.describe = {}      # object -> describe entry
        self.server = {}
        self.connected = False
        self.link_reason = f"connecting to {host}:{port}"
        self.note = ""
        self.start_tab = start_tab
        self.panes = []

    # ── layout ────────────────────────────────────────────────────────────

    def compose(self) -> ComposeResult:
        yield StatusLine(id="status")
        with TabbedContent(initial=self.start_tab, id="tabs"):
            for pid, title, cls in PANES:
                with TabPane(title, id=pid):
                    pane = cls(id=f"pane-{pid}")
                    self.panes.append(pane)
                    yield pane
        yield MediaBar(id="media")
        yield Footer()

    def on_mount(self):
        self.refresh_bars()
        self.link.start()

    def on_unmount(self):
        self.link.stop()

    def refresh_bars(self):
        try:
            self.query_one(StatusLine).show(self)
            self.query_one(MediaBar).show(self)
        except NoMatches:   # quitting: the bars are gone already
            pass

    def active_pane(self):
        active = self.query_one(TabbedContent).active
        return next((p for p in self.panes if p.id == f"pane-{active}"), None)

    def on_tabbed_content_tab_activated(self, event):
        for pane in self.panes:
            pane.shown(pane is self.active_pane())
        if (pane := self.active_pane()) is not None:
            self.call_after_refresh(pane.focus_main)

    # ── link events ───────────────────────────────────────────────────────

    def on_linked(self, message: Linked):
        self.server = message.server
        self.describe = message.describe.get("objects", message.describe)
        self.connected = True
        self.state.clear()
        for pane in self.panes:
            for obj, props, sigs, throttle in pane.SUBSCRIBE:
                args = {"object": obj, **({"properties": props} if props else {}), **({"signals": sigs} if sigs else {})}
                self.req("subscribe", quiet=True, **args, **({"throttleMs": throttle} if throttle else {}))
            pane.linked()
            pane.shown(pane is self.active_pane())
        self.refresh_bars()

    def on_unlinked(self, message: Unlinked):
        self.connected = False
        self.link_reason = message.reason
        self.refresh_bars()

    def on_octave_event(self, message: OctaveEvent):
        ev = message.event
        obj, kind, name = ev.get("object"), ev.get("event"), ev.get("name")
        if kind == "property":
            self.state.setdefault(obj, {})[name] = ev.get("value")
        elif kind == "model":
            pass
        elif kind == "signal" and obj == "clock":
            self.state.setdefault("clock", {})["time"] = (ev.get("args") or [""])[0]
        elif kind == "signal" and name == "milStatusChanged":
            self.state["_mil"] = bool((ev.get("args") or [False])[0])
        elif kind == "params":
            obj = "paramStore"
        elif kind == "dropped":
            self.note = f"{ev.get('count')} events dropped"
        for pane in self.panes:
            if not pane.is_mounted:   # still composing at startup; it renders from state when it is
                continue
            if obj in pane.OBJECTS or any(obj == sub[0] for sub in pane.SUBSCRIBE):
                try:
                    pane.octave(ev)
                except NoMatches:   # the pane is being torn down (quitting)
                    pass
        if obj in ("nowPlaying", "volumeController", "obdManager", "tripRecorder", "clock"):
            self.refresh_bars()

    def state_of(self, obj, prop, default=None):
        return self.state.get(obj, {}).get(prop, default)

    # ── requests (worker threads, results back on the UI thread) ──────────

    @work(thread=True, group="octave")
    def req(self, op, done=None, failed=None, note=None, quiet=False, **args):
        try:
            result = self.link.request(op, **args)
        except OctaveError as e:
            if failed:
                self.call_from_thread(failed, str(e))
            elif not quiet:
                self.call_from_thread(self.notify, f"{op} {args.get('object', '')}.{args.get('method', '')}: {e}",
                                      severity="error")
            return
        if note:
            self.call_from_thread(self.status_note, note)
        if done:
            self.call_from_thread(done, result)

    @work(thread=True, group="octave")
    def blocking(self, fn, done=None):
        """Run fn(link) on a worker thread (several calls in a row); done(result) on the UI thread."""
        try:
            result = fn(self.link)
        except OctaveError as e:
            self.call_from_thread(self.notify, str(e), severity="error")
            return
        if done:
            self.call_from_thread(done, result)

    def invoke(self, obj, method, *args, done=None, failed=None, note=None, quiet=False):
        self.req("call", object=obj, method=method, args=list(args), done=done, failed=failed, note=note, quiet=quiet)

    def download_export(self, kind):
        """Fetch the head unit's last export of `kind` (trip, dashboard, logs)
        over GET /export/<kind> and save it where the user says, here."""
        host, port = self.link.host, self.link.port

        def fetch(_link):
            try:
                with urllib.request.urlopen(f"http://{host}:{port}/export/{kind}", timeout=10) as r:
                    return r.read(), r.headers.get("Content-Type", "")
            except Exception as e:
                return None, str(e)

        def got(result):
            data, ctype = result
            if data is None:
                self.notify(f"No {kind} export to download (export one first): {ctype}", severity="warning")
                return
            ext = next((e for t, e in (("json", ".json"), ("csv", ".csv"), ("zip", ".zip"), ("gzip", ".gz"),
                                        ("text", ".txt")) if t in ctype), ".bin")
            default = os.path.join(os.getcwd(), f"OCTAVE-{kind}-{time.strftime('%Y%m%d-%H%M%S')}{ext}")

            def answered(path):
                if not path:
                    return
                path = os.path.expanduser(path.strip())
                try:
                    with open(path, "wb") as f:
                        f.write(data)
                except OSError as e:
                    self.notify(f"Could not write {path}: {e}", severity="error")
                    return
                self.notify(f"Saved {len(data)} bytes to {path}")
            self.push_screen(EditValue(f"Save the {kind} export ({len(data)} bytes, {ctype or 'unknown type'}) to",
                                       default, "a path on this computer"), answered)
        self.blocking(fetch, got)

    def status_note(self, text):
        self.note = text
        self.refresh_bars()

    # ── global actions ────────────────────────────────────────────────────

    def action_tab(self, pid):
        # TabbedContent follows focus: a focused widget left in the old pane would pull its tab back
        self.screen.set_focus(None)
        self.query_one(TabbedContent).active = pid

    def action_play(self):
        self.invoke("nowPlaying", "togglePlay")

    def action_next(self):
        self.invoke("nowPlaying", "next")

    def action_previous(self):
        self.invoke("nowPlaying", "previous")

    def action_shuffle(self):
        self.invoke("nowPlaying", "toggleShuffle")

    def action_seek(self, delta_ms):
        n = self.state.get("nowPlaying", {})
        if n.get("canSeek"):
            target = max(0, min((n.get("durationMs") or 0), (n.get("positionMs") or 0) + int(delta_ms)))
            self.invoke("nowPlaying", "seek", target)

    def action_volume(self, delta):
        pct = self.state_of("volumeController", "percent", 0) or 0
        self.invoke("volumeController", "applyVolume", max(0, min(100, pct + int(delta))))

    def action_mute(self):
        muted = bool(self.state_of("volumeController", "muted", False))
        self.req("set", object="volumeController", property="muted", value=not muted)

    def action_toggle_dark(self):
        self.theme = "textual-light" if self.theme != "textual-light" else "textual-dark"

    def clear_dtcs(self):
        self.action_tab("obd")
        self.query_one(ObdPane).action_clear_dtc()
