"""Modal dialogs: a yes / no confirmation, a one-line value editor, a text editor and a picker."""

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import Button, Input, Label, OptionList, Static, TextArea
from textual.widgets.option_list import Option


class Confirm(ModalScreen[bool]):
    BINDINGS = [Binding("y", "answer(True)", "Yes"), Binding("n,escape", "answer(False)", "No")]

    def __init__(self, question):
        super().__init__()
        self.question = question

    def compose(self) -> ComposeResult:
        with Vertical(classes="dialog"):
            yield Static(self.question)
            with Horizontal(classes="buttons"):
                yield Button("Yes (y)", id="yes", variant="error", compact=True)
                yield Button("No (n)", id="no", variant="primary", compact=True)

    def on_mount(self):
        self.query_one("#no", Button).focus()

    def on_button_pressed(self, event):
        self.dismiss(event.button.id == "yes")

    def action_answer(self, yes):
        self.dismiss(yes)


class EditValue(ModalScreen):
    """Returns the text entered, or None on Escape."""

    BINDINGS = [Binding("escape", "cancel", "Cancel")]

    def __init__(self, title, value, hint=""):
        super().__init__()
        self.title_text = title
        self.value = value
        self.hint = hint

    def compose(self) -> ComposeResult:
        with Vertical(classes="dialog"):
            yield Label(f"[b]{self.title_text}[/b]")
            yield Input(self.value, id="edit-value")
            if self.hint:
                yield Static(f"[dim]{self.hint}[/dim]")
            yield Static("[dim]Enter saves, Escape cancels[/dim]")

    def on_input_submitted(self, event):
        self.dismiss(event.value)

    def action_cancel(self):
        self.dismiss(None)


class EditText(ModalScreen):
    """A multi-line editor (JSON specs, flag lists). Returns the text, or None."""

    BINDINGS = [Binding("ctrl+s", "save", "Save"), Binding("escape", "cancel", "Cancel")]

    def __init__(self, title, text, hint="", read_only=False):
        super().__init__()
        self.title_text = title
        self.text = text
        self.hint = hint
        self.read_only = read_only

    def compose(self) -> ComposeResult:
        with Vertical(classes="dialog wide"):
            yield Label(f"[b]{self.title_text}[/b]")
            yield TextArea(self.text, id="edit-text", read_only=self.read_only, show_line_numbers=True)
            if self.hint:
                yield Static(f"[dim]{self.hint}[/dim]")
            yield Static("[dim]Escape closes[/dim]" if self.read_only else "[dim]Ctrl+S saves, Escape cancels[/dim]")

    def action_save(self):
        if not self.read_only:
            self.dismiss(self.query_one(TextArea).text)

    def action_cancel(self):
        self.dismiss(None)


class Pick(ModalScreen):
    """Choose one of [(value, label)]; returns the value, or None."""

    BINDINGS = [Binding("escape", "cancel", "Cancel")]

    def __init__(self, title, choices, current=None):
        super().__init__()
        self.title_text = title
        self.choices = list(choices)
        self.current = current

    def compose(self) -> ComposeResult:
        with Vertical(classes="dialog"):
            yield Label(f"[b]{self.title_text}[/b]")
            yield OptionList(*[Option(("* " if v == self.current else "  ") + str(label), id=str(i))
                               for i, (v, label) in enumerate(self.choices)], id="pick")
            yield Static("[dim]Enter picks, Escape cancels[/dim]")

    def on_mount(self):
        values = [v for v, _ in self.choices]
        if self.current in values:
            self.query_one(OptionList).highlighted = values.index(self.current)

    def on_option_list_option_selected(self, event):
        self.dismiss(self.choices[int(event.option.id)][0])

    def action_cancel(self):
        self.dismiss(None)


class FolderPick(ModalScreen):
    """Browse the head unit's folders with mediaManager.browse_music_folders;
    returns the chosen folder's path, or None."""

    BINDINGS = [Binding("s", "choose", "Use this folder"), Binding("backspace", "up", "Up"),
                Binding("escape", "cancel", "Cancel")]

    def __init__(self, start=""):
        super().__init__()
        self.path = ""
        self.up_path = ""
        self.entries = []   # [(path, label)]
        self.start = start

    def compose(self) -> ComposeResult:
        with Vertical(classes="dialog wide"):
            yield Label("[b]Music folder[/b]", id="folder-title")
            yield OptionList(id="folder-list")
            yield Static(id="folder-info")
            yield Static("[dim]Enter opens, Backspace goes up, S uses this folder, Escape cancels[/dim]")

    def on_mount(self):
        self.open(self.start)

    def open(self, path):
        self.app.invoke("mediaManager", "browse_music_folders", path, done=self.show)

    def show(self, result):
        result = result or {}
        if result.get("error"):
            self.query_one("#folder-info", Static).update(f"[red]{result['error']}[/red]")
            if self.path == "" and self.start:   # a stale start path: begin at the roots
                self.start = ""
                self.open("")
            return
        listing = self.query_one("#folder-list", OptionList)
        listing.clear_options()
        if "roots" in result:
            self.path, self.up_path = "", ""
            self.entries = [(r.get("path"), f"{r.get('name')}  [dim]{r.get('path')}[/dim]") for r in result["roots"]]
            info = "Places on the head unit"
        else:
            self.path, self.up_path = result.get("path", ""), result.get("parent", "")
            self.entries = ([(self.up_path, "..")] if self.up_path else [("", ".. (places)")])
            self.entries += [(d.get("path") if isinstance(d, dict) else f"{self.path}/{d}",
                              (d.get("name") if isinstance(d, dict) else str(d)) + "/") for d in result.get("dirs") or []]
            files = result.get("audioFiles")
            count = len(files) if isinstance(files, list) else (files or 0)
            info = f"{self.path}: {count} audio file(s) here" + ("  (list cut short)" if result.get("truncated") else "")
        for i, (_, label) in enumerate(self.entries):
            listing.add_option(Option(label, id=str(i)))
        self.query_one("#folder-title", Label).update(f"[b]Music folder[/b]  {self.path or ''}")
        self.query_one("#folder-info", Static).update(info)

    def on_option_list_option_selected(self, event):
        self.open(self.entries[int(event.option.id)][0])

    def action_up(self):
        if self.path:
            self.open(self.up_path)

    def action_choose(self):
        if self.path:
            self.dismiss(self.path)

    def action_cancel(self):
        self.dismiss(None)
