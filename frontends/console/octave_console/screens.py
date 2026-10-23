"""Modal dialogs: a yes / no confirmation and a one-line value editor."""

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import Button, Input, Label, Static


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
