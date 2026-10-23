"""The console's panes. Each one names the API objects it follows (OBJECTS)
and gets their events in octave(); it reads current values from app.state
and acts through app.invoke() / app.req(), which run on worker threads."""

import json
import shlex
from collections import deque
from datetime import datetime

from rich.text import Text
from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.widgets import Button, DataTable, Input, Label, RichLog, Sparkline, Static, Tree

from .link import CONSUMER
from .screens import Confirm, EditValue

HISTORY = 120     # sparkline points per PID
MAX_PINS = 4


def fmt_number(value, decimals=1):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return "" if value is None else str(value)
    return f"{value:.{max(0, int(decimals))}f}"


def fmt_duration(seconds):
    seconds = int(seconds or 0)
    h, rest = divmod(seconds, 3600)
    m, s = divmod(rest, 60)
    return f"{h}:{m:02d}:{s:02d}" if h else f"{m}:{s:02d}"


def short(value, width=60):
    text = value if isinstance(value, str) else json.dumps(value)
    return text if len(text) <= width else text[: width - 1] + "…"


class Pane(Vertical):
    OBJECTS: set = set()

    def octave(self, event):
        """An event from one of OBJECTS (the state is already updated)."""

    def linked(self):
        """Connected (again): read anything subscriptions don't bring."""

    def shown(self, visible):
        """This pane became the active tab, or stopped being it."""

    def focus_main(self):
        """Put the keyboard in the pane, so its bindings work after a tab switch."""
        for widget in self.query("DataTable, Tree"):
            if widget.focusable:
                widget.focus()
                return


# ── Live data ────────────────────────────────────────────────────────────


class LivePane(Pane):
    """Every PID the vehicle reports, sortable, with sparklines for pinned ones.
    Only the visible rows (and the pins) are registered as demand."""

    OBJECTS = {"paramStore", "parameterCatalog", "obdManager"}
    COLUMNS = [("id", "ID"), ("title", "Parameter"), ("value", "Value"), ("unit", "Unit"), ("min", "Min"),
               ("max", "Max"), ("pin", "")]
    BINDINGS = [
        Binding("p", "pin", "Pin sparkline"),
        Binding("s", "sort_next", "Sort"),
        Binding("r", "sort_reverse", "Reverse"),
    ]

    def __init__(self, **kw):
        super().__init__(**kw)
        self.catalog = {}          # id -> displayParameters row
        self.values = {}           # id -> native value
        self.history = {}          # id -> deque of display values
        self.order = []            # ids as shown
        self.pins = []
        self.sort_key = "title"
        self.reverse = False
        self.visible = False
        self.sent_demand = None

    def compose(self) -> ComposeResult:
        self.table = DataTable(id="live-table", cursor_type="row", zebra_stripes=True)
        yield self.table
        self.sparks = Vertical(id="sparks")
        yield self.sparks

    def on_mount(self):
        for key, label in self.COLUMNS:
            self.table.add_column(label, key=key)
        self.sparks.display = False
        self.set_interval(0.5, self.update_demand)
        self.set_interval(1.0, self.resort)

    def octave(self, event):
        obj = event.get("object")
        if obj == "paramStore" or event.get("event") == "params":
            self.take_values(event.get("values", {}))
        elif event.get("name") in ("displayParameters", "vehicleSupportedParameters"):
            self.catalog = {row["id"]: row for row in self.app.state_of("parameterCatalog", "displayParameters", [])}
            self.rebuild()

    def to_display(self, pid, value):
        row = self.catalog.get(pid)
        if row is None or isinstance(value, bool) or not isinstance(value, (int, float)):
            return value
        return value * row.get("displayScale", 1) + row.get("displayOffset", 0)

    def take_values(self, values):
        new_ids = False
        for pid, value in values.items():
            self.values[pid] = value
            shown = self.to_display(pid, value)
            if isinstance(shown, (int, float)) and not isinstance(shown, bool):
                self.history.setdefault(pid, deque(maxlen=HISTORY)).append(shown)
            if pid in self.order:
                self.table.update_cell(pid, "value", self.value_cell(pid), update_width=True)
            elif pid in self.catalog:
                new_ids = True
        if new_ids:
            self.rebuild()
        self.refresh_sparks()

    def value_cell(self, pid):
        row = self.catalog.get(pid, {})
        return Text(fmt_number(self.to_display(pid, self.values.get(pid)), row.get("decimals", 1)), justify="right")

    def wanted_ids(self):
        supported = set(self.app.state_of("obdManager", "vehicleSupportedParameters", []) or [])
        return [pid for pid in self.catalog if pid in supported or pid in self.values]

    def sort_value(self, pid):
        row = self.catalog.get(pid, {})
        if self.sort_key == "value":
            v = self.to_display(pid, self.values.get(pid))
            return (0, v) if isinstance(v, (int, float)) else (1, 0)
        if self.sort_key == "pin":
            return (pid not in self.pins, row.get("title", pid))
        v = row.get(self.sort_key, pid if self.sort_key == "id" else "")
        return (0, v) if isinstance(v, (int, float)) else (1, str(v).lower())

    def rebuild(self):
        ids = sorted(self.wanted_ids(), key=self.sort_value, reverse=self.reverse)
        cursor = self.order[self.table.cursor_row] if self.order and self.table.cursor_row < len(self.order) else None
        self.table.clear()
        for pid in ids:
            row = self.catalog[pid]
            self.table.add_row(pid, row.get("title", pid), self.value_cell(pid), row.get("unit", ""),
                               fmt_number(row.get("min"), 0), fmt_number(row.get("max"), 0),
                               "*" if pid in self.pins else "", key=pid)
        self.order = ids
        if cursor in ids:
            self.table.move_cursor(row=ids.index(cursor))
        sort_label = dict(self.COLUMNS)[self.sort_key] or "pinned"
        self.table.border_title = f"Live data: {len(ids)} PIDs, sorted by {sort_label}{' (desc)' if self.reverse else ''}"

    def on_data_table_header_selected(self, event):
        key = event.column_key.value
        if key == self.sort_key:
            self.reverse = not self.reverse
        else:
            self.sort_key, self.reverse = key, False
        self.rebuild()

    def resort(self):
        """Values move, so a sort by value is redone each second while shown."""
        if self.visible and self.sort_key == "value" and self.order:
            self.rebuild()

    def action_sort_next(self):
        keys = [k for k, _ in self.COLUMNS]
        self.sort_key = keys[(keys.index(self.sort_key) + 1) % len(keys)]
        self.rebuild()

    def action_sort_reverse(self):
        self.reverse = not self.reverse
        self.rebuild()

    def action_pin(self):
        if not self.order:
            return
        pid = self.order[self.table.cursor_row]
        if pid in self.pins:
            self.pins.remove(pid)
        else:
            self.pins = (self.pins + [pid])[-MAX_PINS:]
        for p in self.order:
            self.table.update_cell(p, "pin", "*" if p in self.pins else "")
        self.sparks.remove_children()
        self.sparks.display = bool(self.pins)
        for p in self.pins:
            self.sparks.mount(Vertical(Label(id=f"spark-label-{p}", classes="spark-label"),
                                       Sparkline([], id=f"spark-{p}", summary_function=max), classes="spark"))
        self.refresh_sparks()
        self.update_demand()

    def refresh_sparks(self):
        for pid in self.pins:
            row = self.catalog.get(pid, {})
            data = list(self.history.get(pid, []))
            try:
                self.query_one(f"#spark-{pid}", Sparkline).data = data or [0]
                lo, hi = (min(data), max(data)) if data else (0, 0)
                self.query_one(f"#spark-label-{pid}", Label).update(
                    f"[b]{row.get('title', pid)}[/b]  {self.value_cell(pid).plain} {row.get('unit', '')}"
                    f"   [dim]min {fmt_number(lo, row.get('decimals', 1))}  max {fmt_number(hi, row.get('decimals', 1))}[/dim]")
            except Exception:   # mounted on the next refresh
                pass

    def visible_ids(self):
        if not self.order:
            return []
        top = int(self.table.scroll_y)
        rows = max(1, self.table.size.height - 1)
        return self.order[top: top + rows]

    def shown(self, visible):
        self.visible = visible
        self.update_demand()

    def update_demand(self):
        ids = sorted(set(self.visible_ids()) | set(self.pins)) if self.visible else []
        if ids != self.sent_demand and self.app.connected:
            self.sent_demand = ids
            self.app.invoke("obdManager", "setParameterDemand", CONSUMER, ids, quiet=True)

    def linked(self):
        self.sent_demand = None


# ── OBD ──────────────────────────────────────────────────────────────────


class ObdPane(Pane):
    OBJECTS = {"obdManager", "obdAdapterFinder"}
    BINDINGS = [
        Binding("c", "connect", "Connect"),
        Binding("R", "reconnect", "Reconnect"),
        Binding("t", "read_dtc", "Read DTCs"),
        Binding("f", "freeze", "Freeze frame"),
        Binding("X", "clear_dtc", "Clear DTCs"),
    ]

    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical(id="obd-left"):
                yield Static(id="obd-status", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("Connect", id="obd-connect", variant="primary", compact=True)
                    yield Button("Reconnect", id="obd-reconnect", compact=True)
                    yield Button("Disconnect", id="obd-close", compact=True)
                    yield Button("Scan car", id="obd-scan", compact=True)
                yield Label("Adapters  [dim](Enter connects)[/dim]", classes="section")
                yield DataTable(id="adapters", cursor_type="row")
                with Horizontal(classes="buttons"):
                    yield Button("Refresh ports", id="obd-ports", compact=True)
                    yield Button("Bluetooth scan", id="bt-scan", compact=True)
                    yield Button("Stop", id="bt-stop", compact=True)
                yield Static(id="bt-status")
            with Vertical(id="obd-right"):
                yield Label("Trouble codes", classes="section")
                yield DataTable(id="dtcs", cursor_type="row")
                with Horizontal(classes="buttons"):
                    yield Button("Read", id="dtc-read", variant="primary", compact=True)
                    yield Button("Clear…", id="dtc-clear", variant="error", compact=True)
                    yield Button("Freeze frame", id="dtc-freeze", compact=True)
                yield Static(id="freeze", classes="box")
                yield Label("Connection log", classes="section")
                yield RichLog(id="obd-log", max_lines=500, wrap=True)

    def on_mount(self):
        self.query_one("#adapters", DataTable).add_columns("Adapter", "Kind", "Address", "Via")
        self.query_one("#dtcs", DataTable).add_columns("Code", "Description")
        self.adapters = []
        self.render_status()

    def octave(self, event):
        name = event.get("name")
        if event.get("event") == "signal":
            args = event.get("args", [])
            if name == "dtcCodesChanged":
                self.show_dtcs(args[0] if args else [])
            elif name == "freezeFrameChanged":
                self.show_freeze(args[0] if args else [])
            elif name == "dtcClearResult":
                ok, msg = (args + [False, ""])[:2]
                self.app.notify(msg or ("Codes cleared" if ok else "Clear failed"), severity="information" if ok else "error")
                self.app.invoke("obdManager", "read_dtc", quiet=True)
            elif name == "connectionLogLineAppended":
                self.query_one("#obd-log", RichLog).write(args[0] if args else "")
            elif name == "scanProgressChanged":
                self.app.status_note(f"vehicle scan {args[0]}% {args[1] if len(args) > 1 else ''}")
            return
        if name == "connectionLog":
            log = self.query_one("#obd-log", RichLog)
            log.clear()
            for line in event.get("value") or []:
                log.write(line)
        elif name in ("availableAdapters", "devices"):
            self.show_adapters()
        self.render_status()

    def render_status(self):
        s = self.app.state.get("obdManager", {})
        f = self.app.state.get("obdAdapterFinder", {})
        state = s.get("connectionState", "?")
        colour = {"Connected": "green", "Connecting": "yellow", "Failed": "red"}.get(state, "white")
        progress = s.get("connectionProgress")
        lines = [
            f"[{colour}]●[/{colour}] [b]{state}[/b]  {s.get('connectionStatus', '') if s.get('connectionStatus') != state else ''}"
            + (f"  {progress}%" if progress not in (None, 0, 100) else ""),
            f"  [dim]{s.get('connectionDetail', '')}[/dim]",
            f"Vehicle  {s.get('vehicleMake') or '-'} {s.get('vehicleModelYear') or ''}",
            f"VIN      {s.get('vin') or '-'}",
            f"PIDs     {len(s.get('vehicleSupportedParameters') or [])} supported"
            + ("" if s.get("vehicleScanComplete", True) else " (scanning)"),
            f"MIL      {'[red]ON[/red]' if self.app.state.get('_mil') else 'off'}",
        ]
        self.query_one("#obd-status", Static).update("\n".join(lines))
        bt = ("Bluetooth: " + ("on" if f.get("bluetoothOn") else "off") + (", scanning" if f.get("scanning") else "")
              if f.get("supported") else "Bluetooth adapter discovery not supported here")
        status = f.get("status") or ""
        self.query_one("#bt-status", Static).update(
            bt + (f"\n[{'red' if f.get('statusIsError') else 'dim'}]{status}[/]" if status else ""))

    def show_adapters(self):
        table = self.query_one("#adapters", DataTable)
        table.clear()
        self.adapters = []
        for a in self.app.state_of("obdManager", "availableAdapters", []) or []:
            self.adapters.append(("obd", a.get("identifier")))
            table.add_row(a.get("name", ""), a.get("kind", ""), a.get("identifier", ""), "port")
        for d in self.app.state_of("obdAdapterFinder", "devices", []) or []:
            self.adapters.append(("bt", d.get("address")))
            name = d.get("name", "") + (" *" if d.get("likelyObd") else "")
            table.add_row(name, d.get("kind", ""), d.get("address", ""), f"bt {d.get('rssi', '')}")

    def show_dtcs(self, codes):
        table = self.query_one("#dtcs", DataTable)
        table.clear()
        for c in codes or []:
            if isinstance(c, dict):
                table.add_row(c.get("code", ""), c.get("description", "") or "")
            else:
                table.add_row(str(c), "")
        self.query_one("#dtcs").border_title = f"{len(codes or [])} code(s)"

    def show_freeze(self, frame):
        if not frame:
            text = "Freeze frame: none stored"
        else:
            text = "Freeze frame\n" + "\n".join(
                "  " + ("  ".join(str(v) for v in row.values() if v not in ("", None)) if isinstance(row, dict) else str(row))
                for row in frame)
        self.query_one("#freeze", Static).update(text)

    def linked(self):
        def got(codes):
            self.show_dtcs(codes)
        self.app.invoke("obdManager", "get_dtc_codes", done=got, quiet=True)
        self.app.invoke("obdManager", "get_freeze_frame", done=self.show_freeze, quiet=True)

    def on_data_table_row_selected(self, event):
        if event.data_table.id != "adapters" or event.cursor_row >= len(self.adapters):
            return
        via, address = self.adapters[event.cursor_row]
        if via == "obd":
            self.app.invoke("obdManager", "connect_to_adapter", address, note=f"connecting to {address}")
        else:
            self.app.invoke("obdAdapterFinder", "connectAdapter", address, note=f"pairing {address}")

    def on_button_pressed(self, event):
        actions = {
            "obd-connect": self.action_connect, "obd-reconnect": self.action_reconnect,
            "obd-close": lambda: self.app.invoke("obdManager", "close", note="disconnecting"),
            "obd-scan": lambda: self.app.invoke("obdManager", "scan_vehicle", note="scanning the vehicle"),
            "obd-ports": lambda: self.app.invoke("obdManager", "refresh_ports", note="refreshing ports"),
            "bt-scan": lambda: self.app.invoke("obdAdapterFinder", "startScan", note="Bluetooth scan"),
            "bt-stop": lambda: self.app.invoke("obdAdapterFinder", "stopScan"),
            "dtc-read": self.action_read_dtc, "dtc-clear": self.action_clear_dtc, "dtc-freeze": self.action_freeze,
        }
        if event.button.id in actions:
            actions[event.button.id]()

    def action_connect(self):
        self.app.invoke("obdManager", "connect_obd", note="connecting")

    def action_reconnect(self):
        self.app.invoke("obdManager", "reconnect", note="reconnecting")

    def action_read_dtc(self):
        self.app.invoke("obdManager", "read_dtc", note="reading trouble codes")

    def action_freeze(self):
        self.app.invoke("obdManager", "read_freeze_frame", note="reading freeze frame")

    def action_clear_dtc(self):
        def answered(yes):
            if yes:
                self.app.invoke("obdManager", "clear_dtc", note="clearing trouble codes")
        self.app.push_screen(Confirm("Clear all trouble codes?\n\nThis also turns off the check-engine light and "
                                     "resets the car's readiness monitors."), answered)


# ── Trips ────────────────────────────────────────────────────────────────


class TripsPane(Pane):
    OBJECTS = {"tripRecorder"}
    BINDINGS = [
        Binding("enter", "detail", "Details", show=False),
        Binding("e", "export", "Export"),
        Binding("d", "delete", "Delete"),
        Binding("E", "end_trip", "End trip now"),
        Binding("g", "refresh", "Refresh"),
    ]

    def compose(self) -> ComposeResult:
        with Horizontal():
            self.table = DataTable(id="trips", cursor_type="row", zebra_stripes=True)
            yield self.table
            with Vertical(id="trip-side"):
                yield Static(id="trip-live", classes="box")
                yield Static("Select a trip and press Enter", id="trip-detail", classes="box")

    def on_mount(self):
        self.table.add_columns("Started", "Duration", "Samples", "Vehicle", "State")
        self.ids = []

    def octave(self, event):
        name = event.get("name")
        if name == "trips":
            self.show_trips(event.get("value") or [])
        elif name == "detailReady":
            tid, detail = (event.get("args") + [None, None])[:2]
            self.show_detail(tid, detail)
        elif name == "exportFinished":
            tid, path = (event.get("args") + [None, ""])[:2]
            self.app.notify(f"Trip exported to {path}" if path else "Export failed",
                            severity="information" if path else "error")
        elif name == "lastError" and event.get("value"):
            self.app.notify(f"Black box: {event['value']}", severity="error")
        self.show_live()

    def show_live(self):
        s = self.app.state.get("tripRecorder", {})
        rec = "[red]● REC[/red] " if s.get("recording") else "[dim]not recording[/dim]"
        self.query_one("#trip-live", Static).update(
            f"{rec}  {fmt_duration(s.get('currentTripElapsedS'))}  {s.get('currentSampleCount', 0)} samples\n"
            f"Black box {'on' if s.get('enabled', True) else 'off'}, "
            f"{(s.get('storageBytes') or 0) / 1e6:.1f} MB stored\n"
            f"[dim]{s.get('lastExportPath') or ''}[/dim]")

    def show_trips(self, trips):
        cursor = self.table.cursor_row
        self.table.clear()
        self.ids = []
        for t in sorted(trips, key=lambda t: t.get("startMs") or 0, reverse=True):
            self.ids.append(t["id"])
            vehicle = " ".join(str(x) for x in (t.get("make"), t.get("modelYear")) if x)
            state = "open" if t.get("open") else ("clean" if t.get("clean") else "closed")
            live = self.app.state.get("tripRecorder", {})
            duration, samples = t.get("durationS"), t.get("sampleCount", 0)
            if t.get("open") and live.get("currentTripId") == t["id"]:
                duration, samples = live.get("currentTripElapsedS"), live.get("currentSampleCount", 0)
            self.table.add_row((t.get("startIso") or "").replace("T", " "), fmt_duration(duration),
                               str(samples), vehicle, state, key=str(t["id"]))
        if self.ids:
            self.table.move_cursor(row=min(cursor, len(self.ids) - 1))
        self.table.border_title = f"{len(self.ids)} trip(s)"

    def selected(self):
        return self.ids[self.table.cursor_row] if self.ids and self.table.cursor_row < len(self.ids) else None

    def show_detail(self, tid, detail):
        if not isinstance(detail, dict):
            self.query_one("#trip-detail", Static).update(f"Trip {tid}: no detail")
            return
        lines = [f"[b]Trip {tid}[/b]"]
        for key, value in detail.items():
            if isinstance(value, dict):
                lines.append(f"[b]{key}[/b]")
                lines += [f"  {k}: {short(v, 50)}" for k, v in list(value.items())[:30]]
            elif isinstance(value, list):
                lines.append(f"[b]{key}[/b] ({len(value)})")
                lines += [f"  {short(v, 60)}" for v in value[:12]]
            else:
                lines.append(f"{key}: {short(value, 60)}")
        self.query_one("#trip-detail", Static).update("\n".join(lines))

    def on_data_table_row_selected(self, event):
        self.action_detail()

    def action_detail(self):
        tid = self.selected()
        if tid is not None:
            self.app.invoke("tripRecorder", "requestDetail", tid, quiet=True)

    def action_export(self):
        tid = self.selected()
        if tid is not None:
            self.app.invoke("tripRecorder", "exportTrip", tid, note=f"exporting trip {tid}")

    def action_delete(self):
        tid = self.selected()
        if tid is None:
            return

        def answered(yes):
            if yes:
                self.app.invoke("tripRecorder", "deleteTrip", tid, note=f"deleted trip {tid}")
        self.app.push_screen(Confirm(f"Delete trip {tid}?\n\nIts samples, CSV and summary go for good."), answered)

    def action_end_trip(self):
        self.app.invoke("tripRecorder", "endTripNow", note="ending the current trip")

    def action_refresh(self):
        self.app.invoke("tripRecorder", "refresh", quiet=True)


# ── Settings ─────────────────────────────────────────────────────────────


def snake(name):
    return "".join("_" + c.lower() if c.isupper() else c for c in name)


def parse_value(text, qtype):
    text = text.strip()
    if qtype == "bool":
        return text.lower() in ("1", "true", "yes", "on")
    if qtype in ("int", "qlonglong", "uint"):
        return int(float(text))
    if qtype in ("float", "double", "qreal"):
        return float(text)
    if qtype == "QString":
        return text
    return json.loads(text)


class SettingsPane(Pane):
    """Every settingsManager property, from describe. Writes go through the
    property's save slot: the registry's saveSlot, else save_<snake_name>,
    else save_setting(key, value)."""

    OBJECTS = {"settingsManager"}
    BINDINGS = [Binding("slash", "filter", "Filter"), Binding("enter", "edit", "Edit", show=False)]

    def compose(self) -> ComposeResult:
        yield Input(placeholder="filter settings (/ to focus)", id="settings-filter")
        self.table = DataTable(id="settings", cursor_type="row", zebra_stripes=True)
        yield self.table

    def on_mount(self):
        self.table.add_column("Setting", key="name")
        self.table.add_column("Label", key="label")
        self.table.add_column("Type", key="type")
        self.table.add_column("Value", key="value")
        self.props = {}       # name -> describe entry
        self.methods = {}     # name -> param count
        self.registry = {}    # property name -> registry entry
        self.shown_names = []

    def linked(self):
        desc = self.app.describe.get("settingsManager", {})
        self.props = {p["name"]: p for p in desc.get("properties", [])}
        self.methods = {}
        for m in desc.get("methods", []):
            self.methods.setdefault(m["name"], set()).add(len(m.get("params", [])))

        def got_registry(raw):
            try:
                reg = json.loads(raw) if isinstance(raw, str) else (raw or {})
            except ValueError:
                reg = {}
            self.registry = {e.get("key"): e for e in reg.values() if isinstance(e, dict)}
            self.rebuild()
        self.app.invoke("settingsManager", "get_settings_registry", done=got_registry, quiet=True)
        self.app.req("subscribe", object="settingsManager", properties=list(self.props), quiet=True)
        self.rebuild()

    def octave(self, event):
        name = event.get("name")
        if name in self.shown_names:
            self.table.update_cell(name, "value", short(event.get("value"), 70), update_width=True)

    def rebuild(self):
        needle = self.query_one("#settings-filter", Input).value.lower()
        self.table.clear()
        self.shown_names = []
        values = self.app.state.get("settingsManager", {})
        for name in sorted(self.props):
            label = self.registry.get(name, {}).get("label", "")
            if needle and needle not in name.lower() and needle not in label.lower():
                continue
            self.shown_names.append(name)
            self.table.add_row(name, label, self.props[name]["type"], short(values.get(name), 70), key=name)
        self.table.border_title = f"{len(self.shown_names)} of {len(self.props)} settings"

    def on_input_changed(self, event):
        if event.input.id == "settings-filter":
            self.rebuild()

    def on_input_submitted(self, event):
        if event.input.id == "settings-filter":
            self.table.focus()

    def action_filter(self):
        self.query_one("#settings-filter", Input).focus()

    def on_data_table_row_selected(self, event):
        self.action_edit()

    def save_slot(self, name):
        slot = self.registry.get(name, {}).get("saveSlot")
        if slot and 1 in self.methods.get(slot, set()):
            return slot, False
        slot = "save_" + snake(name)
        if 1 in self.methods.get(slot, set()):
            return slot, False
        return "save_setting", True

    def write(self, name, value):
        slot, generic = self.save_slot(name)
        args = (name, value) if generic else (value,)
        self.app.invoke("settingsManager", slot, *args, note=f"{name} = {short(value, 40)}")

    def action_edit(self):
        if not self.shown_names:
            return
        name = self.shown_names[self.table.cursor_row]
        prop = self.props[name]
        current = self.app.state.get("settingsManager", {}).get(name)
        if prop["type"] == "bool":
            self.write(name, not current)
            return
        entry = self.registry.get(name, {})
        params = entry.get("params", {}) or {}
        hint = []
        if "options" in params:
            hint.append("options: " + ", ".join(map(str, params["options"])))
        if "from" in params:
            hint.append(f"range {params.get('from')}..{params.get('to')}")
        slot, generic = self.save_slot(name)
        hint.append(f"saved with {slot}")
        text = current if isinstance(current, str) else json.dumps(current)

        def answered(raw):
            if raw is None:
                return
            try:
                self.write(name, parse_value(raw, prop["type"]))
            except (ValueError, TypeError) as e:
                self.app.notify(f"{name}: {e}", severity="error")
        self.app.push_screen(EditValue(f"{name}  ({prop['type']})", text, "\n".join(hint)), answered)


# ── Downloads ────────────────────────────────────────────────────────────


class ModelTable:
    """A list-model property mirrored from rows / model events."""

    def __init__(self):
        self.rows = []

    def apply(self, event):
        change = event.get("change")
        first, last = event.get("first", 0), event.get("last", -1)
        rows = event.get("rows") or []
        if change == "reset":
            self.rows = list(rows)
        elif change == "insert":
            self.rows[first:first] = rows
        elif change == "remove":
            del self.rows[first:last + 1]
        elif change == "update":
            self.rows[first:first + len(rows)] = rows


class DownloadsPane(Pane):
    OBJECTS = {"downloadManager"}
    BINDINGS = [
        Binding("slash", "search_focus", "Search"),
        Binding("n", "more", "More results"),
        Binding("P", "pause", "Pause"),
        Binding("u", "resume", "Resume"),
        Binding("x", "cancel", "Cancel"),
    ]

    def compose(self) -> ComposeResult:
        yield Input(placeholder="search songs (Enter), / to focus", id="dl-search")
        with Horizontal():
            with Vertical():
                yield Label("Results  [dim](Enter downloads, n more)[/dim]", classes="section")
                yield DataTable(id="dl-results", cursor_type="row", zebra_stripes=True)
            with Vertical():
                yield Label("Queue  [dim](P pause, u resume, x cancel)[/dim]", classes="section")
                yield DataTable(id="dl-queue", cursor_type="row", zebra_stripes=True)
                with Horizontal(classes="buttons"):
                    yield Button("Clear finished", id="dl-clear-finished", compact=True)
                    yield Button("Clear queue", id="dl-clear", compact=True)
        yield Static(id="dl-status")

    def on_mount(self):
        self.results = ModelTable()
        self.queue = ModelTable()
        self.query_one("#dl-results", DataTable).add_columns("Title", "Artist", "Album", "Length", "")
        self.query_one("#dl-queue", DataTable).add_columns("Song", "Artist", "Status", "Progress")
        self.message = ""

    def octave(self, event):
        name = event.get("name")
        if event.get("event") == "model":
            (self.results if name == "searchResults" else self.queue).apply(event)
            self.show_tables()
        elif name == "statusMessage":
            self.message = (event.get("args") or [""])[0]
        elif name in ("searchError", "downloadError"):
            self.message = "[red]" + " ".join(str(a) for a in event.get("args") or []) + "[/red]"
        self.show_status()

    def show_status(self):
        s = self.app.state.get("downloadManager", {})
        if s.get("downloadsAvailable") is False:
            text = "[yellow]Downloads are not available in this build[/yellow]"
        else:
            text = (("[b]searching…[/b]  " if s.get("isSearching") else "")
                    + f"{s.get('activeDownloads', 0)} active  →  {s.get('downloadPath', '')}")
        self.query_one("#dl-status", Static).update(text + (f"\n{self.message}" if self.message else ""))

    def show_tables(self):
        res = self.query_one("#dl-results", DataTable)
        res.clear()
        for r in self.results.rows:
            flag = "done" if r.get("is_downloaded") else ("…" if r.get("is_downloading") else "")
            res.add_row(r.get("name", ""), r.get("artist", ""), r.get("album_name", ""), r.get("duration", ""), flag)
        q = self.query_one("#dl-queue", DataTable)
        q.clear()
        for r in self.queue.rows:
            q.add_row(r.get("songName") or r.get("name", ""), r.get("artist", ""), r.get("status", ""),
                      f"{r.get('progress', 0)}%" if r.get("progress") is not None else "")

    def on_input_submitted(self, event):
        if event.input.id == "dl-search" and event.value.strip():
            self.app.invoke("downloadManager", "search", event.value.strip(), note=f"searching '{event.value}'")
            self.query_one("#dl-results").focus()

    def action_search_focus(self):
        self.query_one("#dl-search", Input).focus()

    def action_more(self):
        self.app.invoke("downloadManager", "search_more", quiet=True)

    def on_data_table_row_selected(self, event):
        if event.data_table.id == "dl-results" and event.cursor_row < len(self.results.rows):
            r = self.results.rows[event.cursor_row]
            self.app.invoke("downloadManager", "download", r.get("song_id"), r.get("name"),
                            note=f"queued {r.get('name')}")

    def queued(self):
        table = self.query_one("#dl-queue", DataTable)
        if table.cursor_row < len(self.queue.rows):
            return self.queue.rows[table.cursor_row].get("songId")
        return None

    def action_pause(self):
        if (sid := self.queued()) is not None:
            self.app.invoke("downloadManager", "pause_download", sid)

    def action_resume(self):
        if (sid := self.queued()) is not None:
            self.app.invoke("downloadManager", "resume_download", sid)

    def action_cancel(self):
        if (sid := self.queued()) is not None:
            self.app.invoke("downloadManager", "cancel_download", sid)

    def on_button_pressed(self, event):
        if event.button.id == "dl-clear-finished":
            self.app.invoke("downloadManager", "clear_finished_downloads")
        elif event.button.id == "dl-clear":
            self.app.invoke("downloadManager", "clear_download_queue")

    def linked(self):
        for prop, model in (("searchResults", self.results), ("downloadQueue", self.queue)):
            def got(rows, model=model):
                model.rows = rows or []
                self.show_tables()
            self.app.req("rows", object="downloadManager", property=prop, done=got, quiet=True)


# ── API explorer ─────────────────────────────────────────────────────────


def parse_command(line):
    """'call volumeController applyVolume 40' -> ("call", {object, method, args})."""
    words = shlex.split(line)
    if not words:
        raise ValueError("empty command")
    op, rest = words[0], words[1:]

    def val(word):
        try:
            return json.loads(word)
        except ValueError:
            return word

    if op == "hello":
        return op, {}
    if op == "describe":
        return op, ({"object": rest[0]} if rest else {})
    if not rest:
        raise ValueError(f"{op} needs an object")
    obj = rest[0]
    if op == "get":
        return op, {"object": obj, **({"property": rest[1]} if len(rest) > 1 else {})}
    if op == "set":
        if len(rest) < 3:
            raise ValueError("set OBJECT PROPERTY VALUE")
        return op, {"object": obj, "property": rest[1], "value": val(" ".join(rest[2:]))}
    if op == "call":
        if len(rest) < 2:
            raise ValueError("call OBJECT METHOD [ARGS...]")
        return op, {"object": obj, "method": rest[1], "args": [val(w) for w in rest[2:]]}
    if op == "rows":
        if len(rest) < 2:
            raise ValueError("rows OBJECT PROPERTY [FROM [COUNT]]")
        extra = {"from": int(rest[2])} if len(rest) > 2 else {}
        if len(rest) > 3:
            extra["count"] = int(rest[3])
        return op, {"object": obj, "property": rest[1], **extra}
    raise ValueError(f"unknown op {op} (hello, describe, get, set, call, rows)")


class ExplorerPane(Pane):
    BINDINGS = [Binding("slash", "command", "Command line")]

    def compose(self) -> ComposeResult:
        with Horizontal():
            self.api_tree = Tree("objects", id="api-tree")
            self.api_tree.show_root = False
            yield self.api_tree
            with Vertical():
                yield RichLog(id="api-out", wrap=True, highlight=True, markup=True, max_lines=2000)
                yield Input(placeholder="get OBJECT PROP | set OBJECT PROP VALUE | call OBJECT METHOD ARGS… | "
                                        "describe OBJECT | rows OBJECT PROP", id="api-cmd")

    def linked(self):
        self.api_tree.clear()
        for name in sorted(self.app.describe):
            desc = self.app.describe[name]
            node = self.api_tree.root.add(f"[b]{name}[/b] [dim]{desc.get('class', '')}[/dim]", data=("object", name))
            props = node.add(f"properties ({len(desc.get('properties', []))})")
            for p in desc.get("properties", []):
                w = " [yellow]rw[/yellow]" if p.get("writable") else ""
                props.add_leaf(f"{p['name']} [dim]{p['type']}[/dim]{w}", data=("property", name, p["name"], p))
            meths = node.add(f"methods ({len(desc.get('methods', []))})")
            for m in desc.get("methods", []):
                sig = ", ".join(f"{a['type']} {a['name']}" for a in m.get("params", []))
                ret = f" → {m['returns']}" if m.get("returns") else ""
                meths.add_leaf(f"{m['name']}([dim]{sig}[/dim]){ret}", data=("method", name, m["name"], m))
            sigs = node.add(f"signals ({len(desc.get('signals', []))})")
            for s in desc.get("signals", []):
                sigs.add_leaf(f"{s['name']}({', '.join(a['name'] for a in s.get('params', []))})")

    def on_tree_node_selected(self, event):
        data = event.node.data
        if not data:
            return
        cmd = self.query_one("#api-cmd", Input)
        if data[0] == "object":
            self.run(f"describe {data[1]}", show_result=False)
        elif data[0] == "property":
            cmd.value = f"get {data[1]} {data[2]}"
            self.run(cmd.value)
            if data[3].get("writable"):
                cmd.value = f"set {data[1]} {data[2]} "
        elif data[0] == "method":
            cmd.value = f"call {data[1]} {data[2]} " + " ".join(a["name"].upper() for a in data[3].get("params", []))
            cmd.focus()

    def on_input_submitted(self, event):
        if event.input.id == "api-cmd" and event.value.strip():
            self.run(event.value.strip())

    def run(self, line, show_result=True):
        out = self.query_one("#api-out", RichLog)
        try:
            op, args = parse_command(line)
        except ValueError as e:
            out.write(f"[red]{e}[/red]")
            return
        out.write(f"[b cyan]> {line}[/b cyan]")

        def done(result):
            out.write(json.dumps(result, indent=2) if show_result or op != "describe" else
                      f"{len(result.get('properties', []))} properties, {len(result.get('methods', []))} methods, "
                      f"{len(result.get('signals', []))} signals (expand the tree)")

        def failed(err):
            out.write(f"[red]error: {err}[/red]")
        self.app.req(op, done=done, failed=failed, quiet=True, **args)

    def action_command(self):
        self.query_one("#api-cmd", Input).focus()


def stamp():
    return datetime.now().strftime("%H:%M:%S")
