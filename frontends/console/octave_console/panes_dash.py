"""Dashboards: list, duplicate, delete, restore the built-ins, export, import,
and edit a spec as JSON (checked with dashboardManager.normalizedSpec)."""

import json

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal
from textual.widgets import DataTable, Static

from .panes import Pane
from .screens import Confirm, EditText, EditValue


def summarize(spec):
    """A text preview of a spec: grid, then one line per cell."""
    if not isinstance(spec, dict) or not spec:
        return "no spec"
    lines = [f"[b]{spec.get('label', '?')}[/b]  id {spec.get('id', '?')}  schema {spec.get('schema', 1)}",
             f"grid {spec.get('gridColumns', 12)} x {spec.get('gridRows', 6)}, {len(spec.get('cells', []))} cells", ""]
    for c in spec.get("cells", []):
        where = f"{c.get('col', 0)},{c.get('row', 0)} {c.get('colSpan', 1)}x{c.get('rowSpan', 1)}"
        props = ", ".join(f"{k}={v}" for k, v in (c.get("props") or {}).items())
        lines.append(f"{where:<12} {c.get('type', '?'):<20} {c.get('paramId', '') or '':<22} [dim]{props}[/dim]")
    return "\n".join(lines)


def changes(before, after):
    """What normalizedSpec changed, in words (cells moved, shrunk or dropped)."""
    notes = []
    b_cells, a_cells = before.get("cells", []), after.get("cells", [])
    if len(a_cells) < len(b_cells):
        notes.append(f"{len(b_cells) - len(a_cells)} cell(s) dropped (overlap, off the grid, or bad)")
    for key in ("gridColumns", "gridRows", "label"):
        if key in before and before.get(key) != after.get(key):
            notes.append(f"{key}: {before.get(key)!r} -> {after.get(key)!r}")
    return notes


class DashboardsPane(Pane):
    SUBSCRIBE = [("dashboardManager", ["dashboards", "lastShareError", "lastError", "hiddenBuiltInCount"], None, 0)]
    BINDINGS = [
        Binding("e", "edit", "Edit JSON"),
        Binding("d", "duplicate", "Duplicate"),
        Binding("x", "delete", "Delete"),
        Binding("i", "import_text", "Import"),
        Binding("o", "export", "Export"),
        Binding("R", "restore", "Restore built-ins"),
    ]

    def __init__(self, **kw):
        super().__init__(**kw)
        self.rows = []

    def compose(self) -> ComposeResult:
        with Horizontal():
            yield DataTable(id="dash-list", cursor_type="row", zebra_stripes=True)
            yield Static("Select a dashboard", id="dash-preview", classes="box")

    def on_mount(self):
        self.query_one("#dash-list", DataTable).add_columns("Dashboard", "Id", "Kind")

    def octave(self, event):
        name = event.get("name")
        if name in ("dashboards", "hiddenBuiltInCount"):
            self.show_list()
        elif name in ("lastShareError", "lastError") and event.get("value"):
            self.app.notify(f"Dashboards: {event['value']}", severity="error")

    def show_list(self):
        state = self.app.state.get("dashboardManager", {})
        self.rows = state.get("dashboards") or []
        table = self.query_one("#dash-list", DataTable)
        cursor = table.cursor_row
        table.clear()
        for d in self.rows:
            table.add_row(d.get("label", ""), d.get("id", ""), "built-in" if d.get("builtIn") else "yours", key=d.get("id"))
        if self.rows:
            table.move_cursor(row=min(cursor, len(self.rows) - 1))
        hidden = state.get("hiddenBuiltInCount") or 0
        table.border_title = f"{len(self.rows)} dashboards" + (f", {hidden} built-in hidden (R restores)" if hidden else "")

    def selected(self):
        table = self.query_one("#dash-list", DataTable)
        return self.rows[table.cursor_row] if self.rows and table.cursor_row < len(self.rows) else None

    def on_data_table_row_highlighted(self, event):
        if (d := self.selected()) is not None:
            self.app.invoke("dashboardManager", "loadDashboard", d["id"], quiet=True,
                            done=lambda spec: self.query_one("#dash-preview", Static).update(summarize(spec)))

    def action_edit(self):
        d = self.selected()
        if d is None:
            return

        def got(spec):
            hint = ("A built-in is read-only: saving makes your own copy under a new id."
                    if d.get("builtIn") else "Saved through normalizedSpec: what it can't place it moves, shrinks or drops.")
            self.app.push_screen(EditText(f"Edit {d.get('label')}", json.dumps(spec, indent=2), hint),
                                 lambda text: self.save_text(text, d))
        self.app.invoke("dashboardManager", "loadDashboard", d["id"], done=got)

    def save_text(self, text, original):
        if text is None:
            return
        try:
            spec = json.loads(text)
        except ValueError as e:
            self.app.notify(f"Not JSON: {e}", severity="error")
            return
        if not isinstance(spec, dict):
            self.app.notify("A dashboard is a JSON object", severity="error")
            return

        def normalized(norm):
            if not norm:
                self.app.notify("normalizedSpec refused it (schema or required fields)", severity="error")
                return
            for note in changes(spec, norm):
                self.app.notify(note, severity="warning")
            if original.get("builtIn"):
                def got_id(new_id):
                    norm["id"] = new_id
                    self.save(norm)
                self.app.invoke("dashboardManager", "uniqueIdForLabel", norm.get("label") or "Dashboard", done=got_id)
            else:
                self.save(norm)
        self.app.invoke("dashboardManager", "normalizedSpec", spec, done=normalized)

    def save(self, spec):
        def saved(new_id):
            if new_id:
                self.app.status_note(f"saved {new_id}")
            else:
                why = self.app.state_of("dashboardManager", "lastError")
                self.app.notify(f"saveDashboard refused it{': ' + why if why else ''}", severity="error")
        self.app.invoke("dashboardManager", "saveDashboard", spec, done=saved)

    def action_duplicate(self):
        d = self.selected()
        if d is None:
            return

        def answered(label):
            if label and label.strip():
                self.app.invoke("dashboardManager", "duplicateDashboard", d["id"], label.strip(),
                                done=lambda new_id: self.app.status_note(f"duplicated as {new_id}" if new_id else "duplicate failed"))
        self.app.push_screen(EditValue(f"Duplicate {d.get('label')} as", f"{d.get('label')} copy"), answered)

    def action_delete(self):
        d = self.selected()
        if d is None:
            return
        what = ("Hide the built-in dashboard {}?\n\nR restores the built-ins." if d.get("builtIn")
                else "Delete your dashboard {}?\n\nThe file goes for good.").format(d.get("label"))

        def answered(yes):
            if yes:
                self.app.invoke("dashboardManager", "deleteDashboard", d["id"], note=f"deleted {d['id']}")
        self.app.push_screen(Confirm(what), answered)

    def action_restore(self):
        self.app.invoke("dashboardManager", "restoreBuiltInDashboards", note="built-ins restored")

    def action_export(self):
        d = self.selected()
        if d is None:
            return

        def got(spec):
            def exported(path):
                hint = f"Also written on the head unit: {path}" if path else "The head unit could not write a file."
                self.app.push_screen(EditText(f"{d.get('label')} as JSON (copy it from here)", json.dumps(spec, indent=2),
                                              hint, read_only=True))
            self.app.invoke("dashboardManager", "exportDashboard", d["id"], done=exported)
        self.app.invoke("dashboardManager", "loadDashboard", d["id"], done=got)

    def action_import_text(self):
        def answered(text):
            if text and text.strip():
                self.app.invoke("dashboardManager", "importDashboardFromText", text,
                                done=lambda new_id: self.app.status_note(f"imported as {new_id}") if new_id else
                                self.app.notify("Import refused (see the error)", severity="error"))
        self.app.push_screen(EditText("Import a dashboard: paste its JSON", "", "Its id is always regenerated."), answered)
