"""Car: the shift light (live state and its flags), the IMU (values, tare),
the gesture sensor (status and mapping) and the ESP32 volume knob (ports and
connection). Enables and look settings are in the Settings pane."""

import json

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.widgets import Button, DataTable, Label, Static

from .link import CONSUMER
from .panes import Pane, fmt_number
from .screens import Confirm, EditText, Pick

FLAG_FIELDS = ["rpmLow", "rpmHigh", "color", "flash", "flashSpeed"]
NEW_FLAG = {"rpmLow": 5500, "rpmHigh": 6500, "color": "#FF0000", "flash": True, "flashSpeed": 100}


def check_flags(flags):
    """Raise ValueError unless flags is a list of {rpmLow < rpmHigh, color, ...}."""
    if not isinstance(flags, list):
        raise ValueError("flags are a JSON list")
    for i, f in enumerate(flags):
        if not isinstance(f, dict):
            raise ValueError(f"flag {i + 1} is not an object")
        low, high = f.get("rpmLow", f.get("rpm")), f.get("rpmHigh")
        if not isinstance(low, (int, float)) or not isinstance(high, (int, float)) or low >= high:
            raise ValueError(f"flag {i + 1}: rpmLow must be a number below rpmHigh")
    return flags


class CarPane(Pane):
    OBJECTS = {"paramStore", "parameterCatalog"}
    SUBSCRIBE = [
        ("shiftLightManager", ["enabled", "maxRpm", "flags", "rpm", "active", "activeIndex", "activeFlag", "lit",
                               "suppressed", "lampSize", "glowSize", "glowIntensity", "pulseEnabled",
                               "fullScreenFlashOpacity", "showOnHomeCard"], None, 100),
        ("berryIMU", ["connected", "hasTemperature"], ["connectionStatusChanged"], 0),
        ("gestureSensor", ["connectionStatus", "gestures", "actions"], ["gestureDetected", "actionTriggered"], 0),
        ("esp32VolumeManager", ["connected", "connectionStatus", "connectionDetail", "availablePorts"], None, 0),
    ]
    BINDINGS = [
        Binding("a", "add_flag", "Add flag"),
        Binding("x", "remove_flag", "Remove flag"),
        Binding("E", "edit_flags", "Flags as JSON"),
        Binding("t", "tare", "Tare IMU"),
    ]

    def __init__(self, **kw):
        super().__init__(**kw)
        self.flags = []
        self.mapping = {}
        self.imu = {}
        self.visible = False
        self.last_gesture = ""

    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical(id="car-left"):
                yield Label("Shift light  [dim](Enter edits a flag, a add, x remove, E all as JSON)[/dim]", classes="section")
                yield Static(id="shift-state", classes="box")
                yield DataTable(id="shift-flags", cursor_type="row")
                yield Label("IMU", classes="section")
                yield Static(id="imu-state", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("Tare", id="imu-tare", compact=True)
                    yield Button("Reset tare", id="imu-reset", compact=True)
            with Vertical(id="car-right"):
                yield Label("Gesture sensor  [dim](Enter picks the action)[/dim]", classes="section")
                yield Static(id="gesture-state")
                yield DataTable(id="gestures", cursor_type="row")
                with Horizontal(classes="buttons"):
                    yield Button("Reset mapping…", id="gesture-reset", compact=True)
                yield Label("ESP32 volume knob", classes="section")
                yield Static(id="esp-state", classes="box")
                yield DataTable(id="esp-ports", cursor_type="row")
                with Horizontal(classes="buttons"):
                    yield Button("Refresh ports", id="esp-refresh", compact=True)
                    yield Button("Connect", id="esp-connect", compact=True)
                    yield Button("Disconnect", id="esp-disconnect", compact=True)
                    yield Button("Retry", id="esp-retry", compact=True)

    def on_mount(self):
        self.query_one("#shift-flags", DataTable).add_columns("#", "From RPM", "To RPM", "Colour", "Flash", "Speed ms")
        self.query_one("#gestures", DataTable).add_columns("Gesture", "Action")
        self.query_one("#esp-ports", DataTable).add_columns("Port", "Device", "ESP32-S3")
        self.render_all()

    def shown(self, visible):
        if visible != self.visible:
            self.visible = visible
            if self.app.connected:
                self.app.invoke("berryIMU", "setDemand", CONSUMER, visible, quiet=True)

    def linked(self):
        self.visible = False
        self.load_mapping()

    def load_mapping(self):
        def fetch(link):
            gestures = link.request("get", object="gestureSensor", property="gestures") or []
            return {g: link.call("gestureSensor", "getGestureAction", g) for g in gestures}

        def got(mapping):
            self.mapping = mapping
            self.render_gestures()
        self.app.blocking(fetch, got)

    def octave(self, event):
        obj, name = event.get("object"), event.get("name")
        if event.get("event") == "params":
            ids = {pid for pid, row in self.imu_rows().items()}
            hit = {k: v for k, v in (event.get("values") or {}).items() if k in ids}
            if hit:
                self.imu.update(hit)
                self.render_imu()
            return
        if obj == "shiftLightManager":
            if name == "flags":
                self.flags = event.get("value") or []
                self.render_flags()
            self.render_shift()
        elif obj == "berryIMU":
            self.render_imu()
        elif obj == "gestureSensor":
            if event.get("event") == "signal":
                self.last_gesture = f"{name.replace('Detected', '').replace('Triggered', '')}: {(event.get('args') or [''])[0]}"
            elif name == "gestures":
                self.load_mapping()
            self.render_gestures()
        elif obj == "esp32VolumeManager":
            self.render_esp()

    # ── rendering ─────────────────────────────────────────────────────────

    def render_all(self):
        self.render_shift()
        self.render_flags()
        self.render_imu()
        self.render_gestures()
        self.render_esp()

    def render_shift(self):
        s = self.app.state.get("shiftLightManager", {})
        lit = "[b reverse] LIT [/]" if s.get("lit") else ("on" if s.get("active") else "off")
        flag = s.get("activeFlag") or {}
        colour = flag.get("color", "")
        swatch = f"[{colour}]██[/]" if colour else ""
        self.query_one("#shift-state", Static).update(
            f"{'Enabled' if s.get('enabled') else '[red]Disabled[/red]'}{'  suppressed' if s.get('suppressed') else ''}"
            f"   RPM {fmt_number(s.get('rpm'), 0)} / {s.get('maxRpm', '?')}   light {lit} {swatch}\n"
            f"[dim]lamp {fmt_number(s.get('lampSize'), 2)}  glow {fmt_number(s.get('glowSize'), 2)} x "
            f"{fmt_number(s.get('glowIntensity'), 2)}  pulse {'on' if s.get('pulseEnabled') else 'off'}  "
            f"flash opacity {fmt_number(s.get('fullScreenFlashOpacity'), 2)}  "
            f"home card {'on' if s.get('showOnHomeCard') else 'off'}  (Settings pane edits these)[/dim]")

    def render_flags(self):
        table = self.query_one("#shift-flags", DataTable)
        active = self.app.state.get("shiftLightManager", {}).get("activeIndex", -1)
        table.clear()
        for i, f in enumerate(self.flags):
            colour = f.get("color", "")
            table.add_row(("▶" if i == active else "") + str(i + 1), fmt_number(f.get("rpmLow", f.get("rpm")), 0),
                          fmt_number(f.get("rpmHigh"), 0), f"[{colour}]██[/] {colour}" if colour else "",
                          "yes" if f.get("flash") else "no", str(f.get("flashSpeed", "")))
        table.border_title = f"{len(self.flags)} flag(s)"

    def imu_rows(self):
        return {r["id"]: r for r in self.app.state_of("parameterCatalog", "displayParameters", []) or []
                if r.get("source") not in (None, "obd")}

    def render_imu(self):
        b = self.app.state.get("berryIMU", {})
        rows = self.imu_rows()
        lines = [f"{'[green]connected[/green]' if b.get('connected') else '[yellow]not connected[/yellow]'}"
                 f"{'  (temperature sensor)' if b.get('hasTemperature') else ''}"]
        for pid, row in rows.items():
            if pid in self.imu:
                v = self.imu[pid] * row.get("displayScale", 1) + row.get("displayOffset", 0)
                lines.append(f"{row.get('title', pid):<22} {fmt_number(v, row.get('decimals', 1)):>9} {row.get('unit', '')}")
        if len(lines) == 1:
            lines.append("[dim]no readings (the IMU sends while this pane is shown)[/dim]")
        self.query_one("#imu-state", Static).update("\n".join(lines))

    def render_gestures(self):
        g = self.app.state.get("gestureSensor", {})
        self.query_one("#gesture-state", Static).update(
            f"{g.get('connectionStatus') or 'unknown'}" + (f"   last {self.last_gesture}" if self.last_gesture else ""))
        labels = {a.get("key"): a.get("label") for a in g.get("actions") or []}
        table = self.query_one("#gestures", DataTable)
        cursor = table.cursor_row
        table.clear()
        for gesture in g.get("gestures") or []:
            action = self.mapping.get(gesture, "")
            table.add_row(gesture, labels.get(action, action), key=gesture)
        if table.row_count:
            table.move_cursor(row=min(cursor, table.row_count - 1))

    def render_esp(self):
        e = self.app.state.get("esp32VolumeManager", {})
        self.query_one("#esp-state", Static).update(
            f"{'[green]connected[/green]' if e.get('connected') else 'not connected'}  {e.get('connectionStatus') or ''}\n"
            f"[dim]{e.get('connectionDetail') or ''}[/dim]\n"
            f"[dim]connects to the port in Settings (esp32VolumePort)[/dim]")
        table = self.query_one("#esp-ports", DataTable)
        table.clear()
        for p in e.get("availablePorts") or []:
            table.add_row(p.get("port", ""), p.get("description", ""), "yes" if p.get("isEsp32S3") else "")

    # ── actions ───────────────────────────────────────────────────────────

    def save_flags(self, flags):
        try:
            check_flags(flags)
        except ValueError as e:
            self.app.notify(str(e), severity="error")
            return
        self.app.invoke("shiftLightManager", "setFlags", flags, note=f"{len(flags)} shift flag(s) saved")

    def on_data_table_row_selected(self, event):
        tid = event.data_table.id
        if tid == "shift-flags" and event.cursor_row < len(self.flags):
            i = event.cursor_row

            def answered(text):
                if text is None:
                    return
                try:
                    flag = json.loads(text)
                except ValueError as e:
                    self.app.notify(f"Not JSON: {e}", severity="error")
                    return
                self.save_flags(self.flags[:i] + [flag] + self.flags[i + 1:])
            self.app.push_screen(EditText(f"Flag {i + 1}", json.dumps(self.flags[i], indent=2),
                                          "rpmLow, rpmHigh, color (#RRGGBB), flash (true / false), flashSpeed (ms)"), answered)
        elif tid == "gestures":
            g = self.app.state.get("gestureSensor", {})
            gestures = g.get("gestures") or []
            if event.cursor_row >= len(gestures):
                return
            gesture = gestures[event.cursor_row]

            def picked(action):
                if action is not None:
                    self.app.invoke("gestureSensor", "setGestureAction", gesture, action, note=f"{gesture} -> {action}",
                                    done=lambda _: self.load_mapping())
            self.app.push_screen(Pick(f"Action for {gesture}", [(a.get("key"), a.get("label")) for a in g.get("actions") or []],
                                      self.mapping.get(gesture)), picked)

    def action_add_flag(self):
        self.save_flags(self.flags + [dict(NEW_FLAG)])

    def action_remove_flag(self):
        table = self.query_one("#shift-flags", DataTable)
        if not self.flags or table.cursor_row >= len(self.flags):
            return
        i = table.cursor_row

        def answered(yes):
            if yes:
                self.save_flags(self.flags[:i] + self.flags[i + 1:])
        self.app.push_screen(Confirm(f"Remove shift flag {i + 1}?"), answered)

    def action_edit_flags(self):
        def answered(text):
            if text is None:
                return
            try:
                flags = json.loads(text)
            except ValueError as e:
                self.app.notify(f"Not JSON: {e}", severity="error")
                return
            self.save_flags(flags)
        self.app.push_screen(EditText("Shift light flags", json.dumps(self.flags, indent=2),
                                      "A list of {rpmLow, rpmHigh, color, flash, flashSpeed}; the first matching band wins."),
                             answered)

    def action_tare(self):
        self.app.invoke("berryIMU", "calibrateTare", note="IMU tared")

    def on_button_pressed(self, event):
        bid = event.button.id
        if bid == "imu-tare":
            self.action_tare()
        elif bid == "imu-reset":
            self.app.invoke("berryIMU", "resetTare", note="IMU tare reset")
        elif bid == "gesture-reset":
            def answered(yes):
                if yes:
                    self.app.invoke("gestureSensor", "resetMappingToDefaults", note="gesture mapping reset",
                                    done=lambda _: self.load_mapping())
            self.app.push_screen(Confirm("Reset every gesture to its default action?"), answered)
        elif bid == "esp-refresh":
            self.app.invoke("esp32VolumeManager", "refresh_ports", note="ports refreshed")
        elif bid == "esp-connect":
            self.app.invoke("esp32VolumeManager", "connect_device", note="connecting the knob")
        elif bid == "esp-disconnect":
            self.app.invoke("esp32VolumeManager", "disconnect_device", note="knob disconnected")
        elif bid == "esp-retry":
            self.app.invoke("esp32VolumeManager", "reset_reconnect_attempts", note="knob retry")
