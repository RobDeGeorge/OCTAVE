"""MediaManager playback robustness: a missing track must not silently stop
playback, and shuffle must survive a playlist re-select (library rescan).

A real MediaManager opens a QAudioOutput, which can hang headless, so these
tests build a bare instance with only the state the code paths under test
touch, and a fake player.
"""

import os

from PySide6.QtCore import QObject, QUrl


class _FakePlayer:
    def __init__(self):
        self.source = None
        self.playing = False
        self.stopped = 0

    def setSource(self, url):
        self.source = url

    def play(self):
        self.playing = True

    def stop(self):
        self.playing = False
        self.stopped += 1


class _FakeTimer:
    def start(self, *args):
        pass

    def stop(self):
        pass


def _make_manager(tmp_path, files, existing):
    from backend.media_manager import MediaManager

    mm = MediaManager.__new__(MediaManager)
    QObject.__init__(mm)
    lib = tmp_path / "lib"
    folder = lib / "Road"
    folder.mkdir(parents=True)
    for name in existing:
        (folder / name).write_bytes(b"\x00")

    mm._library_root = str(lib)
    mm.media_dir = str(folder)
    mm._all_music_file_paths = {}
    mm._playlists = {"Road": {"path": str(folder), "files": list(files), "is_combined": False}}
    mm._current_playlist_name = ""
    mm._is_all_music_active = False
    mm._current_playlist = []
    mm._current_index = 0
    mm._original_files = []
    mm._metadata_cache = {}
    mm._shuffle = False
    mm._is_playing = False
    mm._is_paused = True
    mm._is_muted = False
    mm._recovery_attempts = 0
    mm._recovery_file = ""
    mm._consecutive_bad_tracks = 0
    mm._player = _FakePlayer()
    mm._save_state_timer = _FakeTimer()
    mm._precache_timer = _FakeTimer()
    # Metadata/stats paths read tags; irrelevant here
    mm.invalidate_stats_cache = lambda: None
    mm._calculate_all_stats = lambda: None
    mm._emit_metadata = lambda filename: None
    mm.get_formatted_duration = lambda filename: ""
    return mm


def test_missing_track_skips_to_next_playable(qapp, tmp_path):
    files = ["a.mp3", "b.mp3", "c.mp3"]
    mm = _make_manager(tmp_path, files, existing=["b.mp3"])
    mm.select_playlist("Road")
    started = []
    mm.currentMediaChanged.connect(started.append)

    mm.play_file("a.mp3")   # deleted since the scan

    assert started == ["b.mp3"]
    assert mm._is_playing
    assert mm._current_playlist[mm._current_index] == "b.mp3"
    assert os.path.basename(mm._player.source.toLocalFile()) == "b.mp3"


def test_all_tracks_missing_stops_cleanly(qapp, tmp_path):
    files = ["a.mp3", "b.mp3", "c.mp3"]
    mm = _make_manager(tmp_path, files, existing=[])
    mm.select_playlist("Road")
    states = []
    mm.playStateChanged.connect(states.append)

    mm.play_file("a.mp3")

    assert not mm._is_playing
    assert mm._player.stopped == 1
    assert states and states[-1] is False
    assert mm._consecutive_bad_tracks == 0


def test_shuffle_survives_reselect_with_current_song_first(qapp, tmp_path):
    files = [f"{i:02d}.mp3" for i in range(40)]
    mm = _make_manager(tmp_path, files, existing=files)
    mm.select_playlist("Road")
    mm._shuffle = True
    mm._current_index = mm._current_playlist.index("17.mp3")

    mm.select_playlist("Road")   # what a library rescan does

    assert mm._current_playlist[0] == "17.mp3"
    assert sorted(mm._current_playlist) == sorted(files)
    # 39! orderings: an alphabetical result would mean shuffle was dropped
    assert mm._current_playlist[1:] != sorted(mm._current_playlist[1:])


def test_select_playlist_without_shuffle_is_alphabetical(qapp, tmp_path):
    files = ["c.mp3", "a.mp3", "b.mp3"]
    mm = _make_manager(tmp_path, files, existing=files)
    mm.select_playlist("Road")
    assert mm._current_playlist == ["a.mp3", "b.mp3", "c.mp3"]


def test_zero_byte_cover_is_rewritten(qapp, tmp_path):
    from backend.media_manager import MediaManager

    mm = MediaManager.__new__(MediaManager)
    QObject.__init__(mm)
    mm.temp_dir = str(tmp_path)
    album_id = "Album_Artist"

    # Truncated by an older build's in-place write
    url = mm._write_cover(album_id, b"", "image/jpeg")
    path = QUrl(url).toLocalFile()
    assert os.path.getsize(path) == 0
    assert mm._cached_art_url_for(album_id) == ""

    mm._write_cover(album_id, b"\xff\xd8jpeg", "image/jpeg")
    with open(path, "rb") as f:
        assert f.read() == b"\xff\xd8jpeg"
    assert mm._cached_art_url_for(album_id) == url
