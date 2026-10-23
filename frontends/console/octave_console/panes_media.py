"""Library: the songs of the current playlist, the playlists, the phone's queue
while mirroring, and a spectrum readout from audioAnalyzer."""

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.widgets import DataTable, Label, Static

from .link import CONSUMER
from .panes import Pane
from .screens import Confirm, EditValue, Pick

SORTS = ["title", "artist", "album"]
BARS = " ▁▂▃▄▅▆▇█"


def spectrum(levels, width):
    """Fold the analyzer's bars into width columns of block characters."""
    if not levels or width <= 0:
        return ""
    step = max(1, len(levels) // width)
    cols = [max(levels[i:i + step]) for i in range(0, len(levels), step)][:width]
    top = max(8, max(cols) or 1)   # the analyzer's bars run 0..8
    return "".join(BARS[min(len(BARS) - 1, int(v / top * (len(BARS) - 1) + 0.5))] for v in cols)


class LibraryPane(Pane):
    SUBSCRIBE = [
        ("mediaManager", None, ["mediaListChanged", "playlistsChanged", "currentPlaylistChanged", "currentMediaChanged",
                                "songDeleted", "scanProgress"], 0),
        ("audioAnalyzer", ["levels"], None, 100),
        ("phoneMirrorManager", ["mediaQueue", "mediaQueueActiveId", "mediaQueueTitle", "mediaActive"], None, 0),
    ]
    BINDINGS = [
        Binding("s", "sort", "Sort"),
        Binding("n", "new_playlist", "New playlist"),
        Binding("D", "delete_playlist", "Delete playlist"),
        Binding("v", "move", "Move to playlist"),
        Binding("x", "delete_song", "Delete song"),
        Binding("R", "rescan", "Rescan"),
        Binding("S", "strip", "Strip names"),
    ]

    def __init__(self, **kw):
        super().__init__(**kw)
        self.songs = []          # [{file, title, artist, album, length}]
        self.playlists = []
        self.current_playlist = ""
        self.current_file = ""
        self.sort = 0
        self.reverse = False
        self.queue = []
        self.visible = False

    def compose(self) -> ComposeResult:
        with Horizontal():
            with Vertical(id="lib-left"):
                yield Label("Playlists  [dim](Enter selects, n new, D delete)[/dim]", classes="section")
                yield DataTable(id="lib-playlists", cursor_type="row")
                yield Label("Phone queue  [dim](Enter skips to it)[/dim]", classes="section", id="lib-queue-label")
                yield DataTable(id="lib-queue", cursor_type="row")
            with Vertical(id="lib-right"):
                yield DataTable(id="lib-songs", cursor_type="row", zebra_stripes=True)
                yield Static(id="lib-spectrum")
                yield Static(id="lib-status")

    def on_mount(self):
        songs = self.query_one("#lib-songs", DataTable)
        for key, label in (("now", ""), ("title", "Title"), ("artist", "Artist"), ("album", "Album"), ("length", "Length")):
            songs.add_column(label, key=key)
        self.query_one("#lib-playlists", DataTable).add_columns("", "Playlist")
        self.query_one("#lib-queue", DataTable).add_columns("", "Title", "Artist")
        self.show_queue()

    # ── data ──────────────────────────────────────────────────────────────

    def linked(self):
        self.reload()

    def reload(self):
        column, ascending = SORTS[self.sort], not self.reverse

        def fetch(link):
            files = link.call("mediaManager", "sort_media_files", column, ascending) or []
            songs = []
            for f in files:
                songs.append({
                    "file": f,
                    "title": link.call("mediaManager", "get_display_name", f) or f,
                    "artist": link.call("mediaManager", "get_band", f) or "",
                    "album": link.call("mediaManager", "get_album", f) or "",
                    "length": link.call("mediaManager", "get_formatted_duration", f) or "",
                })
            return {
                "songs": songs,
                "playlists": link.call("mediaManager", "get_playlist_names") or [],
                "current": link.call("mediaManager", "get_current_playlist_name") or "",
                "file": link.call("mediaManager", "get_current_file") or "",
                "summary": (link.call("mediaManager", "get_total_duration") or "",
                            link.call("mediaManager", "get_artist_count") or 0,
                            link.call("mediaManager", "get_album_count") or 0),
            }
        self.app.blocking(fetch, self.show)

    def show(self, data):
        self.songs = data["songs"]
        self.playlists = data["playlists"]
        self.current_playlist = data["current"]
        self.current_file = data["file"]
        songs = self.query_one("#lib-songs", DataTable)
        cursor = songs.cursor_row
        songs.clear()
        for s in self.songs:
            songs.add_row("▶" if s["file"] == self.current_file else "", s["title"], s["artist"], s["album"],
                          s["length"], key=s["file"])
        if self.songs:
            songs.move_cursor(row=min(cursor, len(self.songs) - 1))
        total, artists, albums = data["summary"]
        songs.border_title = (f"{self.current_playlist or 'All'}: {len(self.songs)} songs, {artists} artists, "
                              f"{albums} albums, {total}   sorted by {SORTS[self.sort]}{' (desc)' if self.reverse else ''}")
        pls = self.query_one("#lib-playlists", DataTable)
        pls.clear()
        for name in self.playlists:
            pls.add_row("*" if name == self.current_playlist else "", name, key=name)

    def octave(self, event):
        obj, name = event.get("object"), event.get("name")
        if obj == "mediaManager":
            if name == "scanProgress":
                self.query_one("#lib-status", Static).update((event.get("args") or [""])[0])
            elif name == "currentMediaChanged":
                self.current_file = (event.get("args") or [""])[0]
                songs = self.query_one("#lib-songs", DataTable)
                for s in self.songs:
                    songs.update_cell(s["file"], "now", "▶" if s["file"] == self.current_file else "")
            else:
                self.reload()
        elif obj == "audioAnalyzer" and name == "levels":
            width = max(10, self.query_one("#lib-spectrum").size.width - 2)
            self.query_one("#lib-spectrum", Static).update(spectrum(event.get("value") or [], width))
        elif obj == "phoneMirrorManager":
            self.show_queue()

    def show_queue(self):
        s = self.app.state.get("phoneMirrorManager", {})
        self.queue = s.get("mediaQueue") or []
        active = s.get("mediaQueueActiveId")
        table = self.query_one("#lib-queue", DataTable)
        table.clear()
        for item in self.queue:
            table.add_row("▶" if item.get("id") == active else "", item.get("title", ""), item.get("artist", "") or item.get("subtitle", ""))
        visible = bool(self.queue)
        table.display = visible
        self.query_one("#lib-queue-label").display = visible
        if visible:
            table.border_title = s.get("mediaQueueTitle") or "Phone queue"

    def shown(self, visible):
        if visible != self.visible:
            self.visible = visible
            if self.app.connected:
                self.app.invoke("audioAnalyzer", "setDemand", CONSUMER, visible, quiet=True)

    # ── actions ───────────────────────────────────────────────────────────

    def selected_song(self):
        table = self.query_one("#lib-songs", DataTable)
        return self.songs[table.cursor_row]["file"] if self.songs and table.cursor_row < len(self.songs) else None

    def on_data_table_row_selected(self, event):
        tid = event.data_table.id
        if tid == "lib-songs" and (f := self.selected_song()):
            self.app.invoke("mediaManager", "play_file", f, note=f"playing {f}")
        elif tid == "lib-playlists" and event.cursor_row < len(self.playlists):
            name = self.playlists[event.cursor_row]
            self.app.invoke("mediaManager", "select_playlist", name, note=f"playlist {name}", done=lambda _: self.reload())
        elif tid == "lib-queue" and event.cursor_row < len(self.queue):
            self.app.invoke("phoneMirrorManager", "mediaSkipToQueueItem", self.queue[event.cursor_row].get("id"))

    def action_sort(self):
        """title, artist, album ascending, then the same descending."""
        step = self.sort + (len(SORTS) if self.reverse else 0) + 1
        self.sort, self.reverse = step % len(SORTS), (step // len(SORTS)) % 2 == 1
        self.reload()

    def action_new_playlist(self):
        def answered(name):
            if name and name.strip():
                self.app.invoke("mediaManager", "create_playlist", name.strip(), note=f"created {name.strip()}",
                                done=lambda _: self.reload())
        self.app.push_screen(EditValue("New playlist", ""), answered)

    def action_delete_playlist(self):
        table = self.query_one("#lib-playlists", DataTable)
        if not self.playlists or table.cursor_row >= len(self.playlists):
            return
        name = self.playlists[table.cursor_row]

        def answered(yes):
            if yes:
                self.app.invoke("mediaManager", "delete_playlist", name, note=f"deleted playlist {name}",
                                done=lambda _: self.reload())
        self.app.push_screen(Confirm(f"Delete the playlist {name}?\n\nIts songs move back to the library."), answered)

    def action_move(self):
        f = self.selected_song()
        if not f:
            return

        def got(names):
            def picked(target):
                if target:
                    self.app.invoke("mediaManager", "move_song_to_playlist", f, target, note=f"moved to {target}",
                                    done=lambda _: self.reload())
            self.app.push_screen(Pick(f"Move {f} to", [(n, n) for n in names or []]), picked)
        self.app.invoke("mediaManager", "get_movable_playlist_names", done=got)

    def action_delete_song(self):
        f = self.selected_song()
        if not f:
            return

        def answered(yes):
            if yes:
                self.app.invoke("mediaManager", "delete_song", f, note=f"deleted {f}", done=lambda _: self.reload())
        self.app.push_screen(Confirm(f"Delete {f} from the head unit?\n\nThe file is removed for good."), answered)

    def action_rescan(self):
        self.app.invoke("mediaManager", "scan_library", note="rescanning the library", done=lambda _: self.reload())

    def action_strip(self):
        def answered(yes):
            if yes:
                self.app.invoke("mediaManager", "strip_filenames", note="stripping file names", done=lambda _: self.reload())
        self.app.push_screen(Confirm("Strip junk from every file name in the library?\n\nThis renames files."), answered)
