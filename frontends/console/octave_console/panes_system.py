"""Phone (mirroring session, pairing, phone keys and media, Android Auto) and
System (network, updates, power, diagnostics, about). Nothing here runs adb
or touches the network until a key or button asks for it."""

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.widgets import Button, DataTable, Label, RichLog, Static

from .panes import Pane
from .screens import Confirm, EditText, EditValue

PHONE_PROPS = ["nativeAvailable", "adbPath", "serverVersion", "isRunning", "sessionState", "deviceState", "deviceHint",
               "lastErrorKind", "mirrorNotice", "devices", "connectionType", "wirelessAddress", "wirelessBusy",
               "qrPairingActive", "qrPairingRows", "phoneAsleep", "phoneInUse", "audioActive", "currentBitRate",
               "frameWidth", "frameHeight", "displaySize", "activeDisplaySize", "mediaAvailable", "mediaPlaying",
               "mediaTitle", "mediaArtist", "mediaApp", "mediaActive"]


class PhonePane(Pane):
    SUBSCRIBE = [
        ("phoneMirrorManager", PHONE_PROPS, ["scrcpyError", "wirelessResult"], 0),
        ("androidAutoManager", ["isDhuInstalled", "dhuPath", "isDhuEmbedded"], ["connectionProgress", "error"], 0),
    ]
    # No single-key start: mirroring talks to a real phone, so it takes a button press
    BINDINGS = [Binding("w", "wireless", "Wireless address")]

    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical(id="phone-left"):
                yield Label("Phone mirroring  [dim](the picture itself needs the QML app)[/dim]", classes="section")
                yield Static(id="phone-state", classes="box")
                yield DataTable(id="phone-devices", cursor_type="row")
                with Horizontal(classes="buttons"):
                    yield Button("Start", id="pm-start", variant="primary", compact=True)
                    yield Button("Stop", id="pm-stop", compact=True)
                    yield Button("Resume", id="pm-resume", compact=True)
                    yield Button("Wake", id="pm-wake", compact=True)
                    yield Button("Check phone", id="pm-refresh", compact=True)
                    yield Button("Dismiss notice", id="pm-dismiss", compact=True)
                with Horizontal(classes="buttons"):
                    yield Button("Home", id="pm-home", compact=True)
                    yield Button("Back", id="pm-back", compact=True)
                    yield Button("Apps", id="pm-apps", compact=True)
                    yield Button("Wireless…", id="pm-wireless", compact=True)
                    yield Button("Forget wireless", id="pm-forget", compact=True)
                    yield Button("QR pairing", id="pm-qr", compact=True)
                    yield Button("Install help", id="pm-help", compact=True)
                yield Static(id="phone-qr")
            with Vertical(id="phone-right"):
                yield Label("Phone media", classes="section")
                yield Static(id="phone-media", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("Prev", id="pm-prev", compact=True)
                    yield Button("Play/Pause", id="pm-play", compact=True)
                    yield Button("Next", id="pm-next", compact=True)
                    yield Button("Hand back to local", id="pm-release", compact=True)
                yield Label("Android Auto (desktop head unit)", classes="section")
                yield Static(id="aa-state", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("Launch", id="aa-launch", compact=True)
                    yield Button("Close", id="aa-close", compact=True)
                    yield Button("Install help", id="aa-help", compact=True)
                yield RichLog(id="phone-log", max_lines=200, wrap=True)

    def on_mount(self):
        self.query_one("#phone-devices", DataTable).add_columns("Serial", "Model", "State")
        self.render_all()

    def octave(self, event):
        name = event.get("name")
        if event.get("event") == "signal":
            text = " ".join(str(a) for a in event.get("args") or [])
            self.query_one("#phone-log", RichLog).write(f"{event.get('object')}.{name}: {text}")
        self.render_all()

    def render_all(self):
        p = self.app.state.get("phoneMirrorManager", {})
        notice = p.get("mirrorNotice")
        lines = [
            f"Session  [b]{p.get('sessionState') or '?'}[/b]{'  running' if p.get('isRunning') else ''}"
            f"{'  phone asleep' if p.get('phoneAsleep') else ''}{'  phone in use' if p.get('phoneInUse') else ''}",
            f"Device   {p.get('deviceState') or '-'}  [dim]{p.get('deviceHint') or ''}[/dim]",
            f"Link     {p.get('connectionType') or '-'}  wireless {p.get('wirelessAddress') or '-'}"
            f"{'  (busy)' if p.get('wirelessBusy') else ''}",
            f"Video    {p.get('frameWidth') or 0}x{p.get('frameHeight') or 0}  size {p.get('activeDisplaySize') or p.get('displaySize') or '-'}"
            f"  {round((p.get('currentBitRate') or 0) / 1e6, 1)} Mb/s  audio {'on' if p.get('audioActive') else 'off'}",
            f"Server   {p.get('serverVersion') or '-'}  adb {p.get('adbPath') or '[red]not found[/red]'}"
            f"{'' if p.get('nativeAvailable', True) else '  [red]native client unavailable[/red]'}",
        ]
        if p.get("lastErrorKind"):
            lines.append(f"[red]Last error: {p['lastErrorKind']}[/red]")
        if notice:
            lines.append(f"[yellow]{notice}[/yellow]")
        self.query_one("#phone-state", Static).update("\n".join(lines))
        table = self.query_one("#phone-devices", DataTable)
        table.clear()
        for d in p.get("devices") or []:
            table.add_row(d.get("serial", ""), d.get("model", ""), d.get("state", ""))
        table.border_title = "Devices (Enter selects)"
        rows = p.get("qrPairingRows") or []
        self.query_one("#phone-qr", Static).update(
            ("[b]Scan with the phone (Wireless debugging > Pair with QR code)[/b]\n" + "\n".join(rows)) if p.get("qrPairingActive") and rows else "")
        self.query_one("#phone-media", Static).update(
            (f"[b]{p.get('mediaTitle') or '-'}[/b]\n{p.get('mediaArtist') or ''}\n"
             f"[dim]{p.get('mediaApp') or ''}  {'playing' if p.get('mediaPlaying') else 'paused'}"
             f"{'  (the active source)' if p.get('mediaActive') else ''}[/dim]")
            if p.get("mediaAvailable") else "[dim]no phone media[/dim]")
        a = self.app.state.get("androidAutoManager", {})
        self.query_one("#aa-state", Static).update(
            f"{'installed' if a.get('isDhuInstalled') else '[yellow]not installed[/yellow]'}  {a.get('dhuPath') or ''}\n"
            f"{'embedded window open' if a.get('isDhuEmbedded') else 'not running'}")

    def on_data_table_row_selected(self, event):
        devices = self.app.state.get("phoneMirrorManager", {}).get("devices") or []
        if event.data_table.id == "phone-devices" and event.cursor_row < len(devices):
            serial = devices[event.cursor_row].get("serial")
            self.app.invoke("phoneMirrorManager", "selectDevice", serial, note=f"selected {serial}")

    def action_start(self):
        self.app.invoke("phoneMirrorManager", "startScrcpy", note="starting mirroring")

    def action_stop(self):
        self.app.invoke("phoneMirrorManager", "stopScrcpy", note="mirroring stopped")

    def action_wireless(self):
        current = self.app.state.get("phoneMirrorManager", {}).get("wirelessAddress") or ""

        def answered(addr):
            if addr is not None:
                self.app.invoke("phoneMirrorManager", "setWirelessAddress", addr.strip(), note=f"wireless {addr.strip()}")
        self.app.push_screen(EditValue("Phone's wireless address (host:port)", current), answered)

    def action_qr(self):
        if self.app.state.get("phoneMirrorManager", {}).get("qrPairingActive"):
            self.app.invoke("phoneMirrorManager", "cancelQrPairing", note="QR pairing cancelled")
        else:
            self.app.invoke("phoneMirrorManager", "startQrPairing", note="QR pairing started")

    def show_text(self, title, text):
        self.app.push_screen(EditText(title, text or "(nothing)", read_only=True))

    def on_button_pressed(self, event):
        calls = {
            "pm-start": self.action_start, "pm-stop": self.action_stop, "pm-wireless": self.action_wireless,
            "pm-qr": self.action_qr,
            "pm-resume": lambda: self.app.invoke("phoneMirrorManager", "resumeMirroring"),
            "pm-wake": lambda: self.app.invoke("phoneMirrorManager", "wakePhone", note="waking the phone"),
            "pm-refresh": lambda: self.app.invoke("phoneMirrorManager", "refreshDeviceState", note="checking the phone"),
            "pm-dismiss": lambda: self.app.invoke("phoneMirrorManager", "dismissNotice"),
            "pm-home": lambda: self.app.invoke("phoneMirrorManager", "pressHome"),
            "pm-back": lambda: self.app.invoke("phoneMirrorManager", "pressBack"),
            "pm-apps": lambda: self.app.invoke("phoneMirrorManager", "pressAppSwitch"),
            "pm-forget": lambda: self.app.invoke("phoneMirrorManager", "forgetWireless", note="wireless forgotten"),
            "pm-help": lambda: self.app.invoke("phoneMirrorManager", "getInstallInstructions",
                                               done=lambda t: self.show_text("Phone mirroring setup", t)),
            "pm-prev": lambda: self.app.invoke("phoneMirrorManager", "mediaPrevious"),
            "pm-play": lambda: self.app.invoke("phoneMirrorManager", "mediaPlayPause"),
            "pm-next": lambda: self.app.invoke("phoneMirrorManager", "mediaNext"),
            "pm-release": lambda: self.app.invoke("phoneMirrorManager", "releaseMedia", True, note="local media again"),
            "aa-launch": lambda: self.app.invoke("androidAutoManager", "launchDhuSeamless", note="launching Android Auto"),
            "aa-close": lambda: self.app.invoke("androidAutoManager", "closeDhu", note="Android Auto closed"),
            "aa-help": lambda: self.app.invoke("androidAutoManager", "getDhuInstallInstructions",
                                               done=lambda t: self.show_text("Android Auto setup", t)),
        }
        if event.button.id in calls:
            calls[event.button.id]()


class SystemPane(Pane):
    SUBSCRIBE = [
        ("networkManager", ["networkName", "isConnected", "updateStatus", "updateMessage", "remoteCommit",
                            "remoteCommitMsg", "workingTreeDirty", "selfUpdateStatus", "selfUpdateMessage",
                            "canSelfUpdate"], None, 0),
        ("diagnosticsManager", ["logDir", "appVersion", "backendName", "deviceInfo", "lastExportPath"], None, 0),
    ]
    BINDINGS = [Binding("l", "logs", "Refresh log"), Binding("x", "export_logs", "Export logs")]

    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical(id="sys-left"):
                yield Label("Network", classes="section")
                yield Static(id="sys-network", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("Refresh", id="net-refresh", compact=True)
                yield Label("Updates", classes="section")
                yield Static(id="sys-update", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("Check", id="upd-check", compact=True)
                    yield Button("Apply update…", id="upd-apply", compact=True)
                    yield Button("Restart app…", id="sys-restart", compact=True)
                    yield Button("Shut down…", id="sys-shutdown", variant="error", compact=True)
                yield Label("About", classes="section")
                yield Static(id="sys-about", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("LICENSE", id="lic-LICENSE", compact=True)
                    yield Button("NOTICE", id="lic-NOTICE", compact=True)
            with Vertical(id="sys-right"):
                yield Label("Diagnostics", classes="section")
                yield Static(id="sys-diag", classes="box")
                with Horizontal(classes="buttons"):
                    yield Button("Refresh log", id="diag-log", compact=True)
                    yield Button("Export logs", id="diag-export", compact=True)
                    yield Button("Download export…", id="diag-download", compact=True)
                yield RichLog(id="sys-log", max_lines=2000, wrap=False)

    def on_mount(self):
        self.render_all()

    def linked(self):
        def fetch(link):
            return [link.call("settingsManager", m) for m in
                    ("get_git_commit_hash", "get_git_commit_date", "get_git_commit_message")]

        def got(info):
            commit, date, msg = info
            self.query_one("#sys-about", Static).update(f"Commit {commit or '?'}  {date or ''}\n[dim]{msg or ''}[/dim]")
        self.app.blocking(fetch, got)
        self.action_logs()

    def octave(self, event):
        self.render_all()

    def render_all(self):
        n = self.app.state.get("networkManager", {})
        self.query_one("#sys-network", Static).update(
            f"{'[green]connected[/green]' if n.get('isConnected') else '[yellow]not connected[/yellow]'}  {n.get('networkName') or ''}")
        self.query_one("#sys-update", Static).update(
            f"{n.get('updateStatus') or 'not checked'}  [dim]{n.get('updateMessage') or ''}[/dim]\n"
            f"Remote   {n.get('remoteCommit') or '-'}  [dim]{n.get('remoteCommitMsg') or ''}[/dim]\n"
            f"Self-update {'available' if n.get('canSelfUpdate') else 'not available'}"
            f"{'  (working tree dirty)' if n.get('workingTreeDirty') else ''}  {n.get('selfUpdateStatus') or ''}"
            f"  [dim]{n.get('selfUpdateMessage') or ''}[/dim]")
        d = self.app.state.get("diagnosticsManager", {})
        self.query_one("#sys-diag", Static).update(
            f"OCTAVE {d.get('appVersion') or '?'}  backend {d.get('backendName') or '?'}\n"
            f"[dim]{d.get('deviceInfo') or ''}[/dim]\n"
            f"Logs  {d.get('logDir') or '-'}\n"
            f"Last export  {d.get('lastExportPath') or '-'}")

    def action_logs(self):
        def got(text):
            log = self.query_one("#sys-log", RichLog)
            log.clear()
            for line in (text or "").splitlines():
                log.write(line)
        self.app.invoke("diagnosticsManager", "recentLogLines", 300, done=got, quiet=True)

    def action_export_logs(self):
        self.app.invoke("diagnosticsManager", "exportLogs",
                        done=lambda path: self.app.notify(f"Logs exported on the head unit: {path}" if path else "Export failed",
                                                          severity="information" if path else "error"))

    def confirm(self, question, method, note):
        def answered(yes):
            if yes:
                self.app.invoke("networkManager", method, note=note)
        self.app.push_screen(Confirm(question), answered)

    def on_button_pressed(self, event):
        bid = event.button.id
        if bid == "net-refresh":
            self.app.invoke("networkManager", "refreshNetwork", note="network refreshed")
        elif bid == "upd-check":
            self.app.invoke("networkManager", "checkForUpdates", note="checking for updates")
        elif bid == "upd-apply":
            self.confirm("Apply the update now?\n\nOCTAVE pulls, rebuilds and restarts.", "applySelfUpdate", "updating")
        elif bid == "sys-restart":
            self.confirm("Restart OCTAVE?\n\nThe console reconnects when it is back.", "restartApp", "restarting")
        elif bid == "sys-shutdown":
            self.confirm("Shut down the head unit?\n\nEverything stops until it is powered on again.", "shutdown_device",
                         "shutting down")
        elif bid == "diag-log":
            self.action_logs()
        elif bid == "diag-export":
            self.action_export_logs()
        elif bid == "diag-download":
            self.app.download_export("logs")
        elif bid and bid.startswith("lic-"):
            name = bid[4:]
            self.app.invoke("diagnosticsManager", "licenseText", name,
                            done=lambda t: self.app.push_screen(EditText(name, t or "(none)", read_only=True)))
