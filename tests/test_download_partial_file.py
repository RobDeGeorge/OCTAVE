"""music_dl must never leave a half-written song at its final library name:
it converts and tags a hidden ".<name>.part" staging file, then renames it
into place. A power cut mid-download used to leave a truncated song that the
"skip" overwrite mode then treated as already downloaded."""

from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock

from backend.media_manager import AUDIO_EXTENSIONS
from backend.music_dl.download import downloader as dl_mod
from backend.music_dl.download.downloader import Downloader, partial_path_for
from backend.music_dl.utils import ffmpeg as ffmpeg_mod


def _song():
    return SimpleNamespace(
        name="Song", artists=["Artist"], artist="Artist", url="https://x/1",
        song_id="1", explicit=False, download_url="https://yt/1",
        display_name="Artist - Song",
    )


def _downloader(monkeypatch, tmp_path, *, convert, embed):
    output = tmp_path / "lib" / "Artist - Song.mp3"
    temp_dir = tmp_path / "temp"
    temp_dir.mkdir()

    class FakeProvider:
        def __init__(self, **_kw):
            self.audio_handler = MagicMock()

        def get_download_metadata(self, url, download=False):
            (temp_dir / "abc.webm").write_bytes(b"webm")
            return {"id": "abc", "ext": "webm", "abr": 160}

    monkeypatch.setattr(dl_mod, "create_file_name", lambda **_kw: output)
    monkeypatch.setattr(dl_mod, "get_temp_path", lambda: temp_dir)
    monkeypatch.setattr(dl_mod, "AudioProvider", FakeProvider)
    monkeypatch.setattr(dl_mod, "convert", convert)
    monkeypatch.setattr(dl_mod, "embed_metadata", embed)

    d = Downloader.__new__(Downloader)
    d.settings = {
        "output": str(output), "format": "mp3", "restrict": None,
        "max_filename_length": None, "skip_explicit": False,
        "scan_for_songs": True, "overwrite": "skip", "cookie_file": None,
        "search_query": None, "filter_results": True, "yt_dlp_args": None,
        "bitrate": "auto", "ffmpeg_args": None, "id3_separator": "/",
        "skip_album_art": False, "fetch_albums": False,
    }
    d.ffmpeg = "ffmpeg"
    d.progress_handler = MagicMock()
    d.scan_formats = ["mp3"]
    d.known_songs = {}
    d.errors = []
    return d, output


def test_partial_name_is_hidden_and_not_audio():
    p = partial_path_for(Path("/music/Artist - Song.mp3"))
    assert p.parent == Path("/music")
    assert p.name.startswith(".")
    assert not p.name.lower().endswith(AUDIO_EXTENSIONS)


def test_song_appears_only_after_convert_and_tag(monkeypatch, tmp_path):
    seen = {}

    def convert(input_file, output_file, **kw):
        seen["convert_to"] = output_file
        seen["force_container"] = kw.get("force_container")
        seen["final_during_convert"] = final.exists()
        output_file.write_bytes(b"mp3 audio")
        return True, None

    def embed(path, song, **kw):
        seen["tagged"] = path
        seen["file_format"] = kw.get("file_format")
        seen["final_during_tag"] = final.exists()

    d, final = _downloader(monkeypatch, tmp_path, convert=convert, embed=embed)
    _song_out, path = d.search_and_download(_song())

    assert path == final
    assert final.read_bytes() == b"mp3 audio"
    assert seen["convert_to"] == partial_path_for(final)
    assert seen["tagged"] == partial_path_for(final)
    assert seen["force_container"] is True
    assert seen["file_format"] == "mp3"
    assert not seen["final_during_convert"] and not seen["final_during_tag"]
    assert not partial_path_for(final).exists()


def test_failed_tagging_leaves_nothing_behind(monkeypatch, tmp_path):
    def convert(input_file, output_file, **kw):
        output_file.write_bytes(b"mp3 audio")
        return True, None

    def embed(path, song, **kw):
        raise RuntimeError("mutagen blew up")

    d, final = _downloader(monkeypatch, tmp_path, convert=convert, embed=embed)
    _song_out, path = d.search_and_download(_song())

    assert path is None
    assert not final.exists()
    assert not partial_path_for(final).exists()


def test_stale_partial_does_not_count_as_downloaded(monkeypatch, tmp_path):
    """A leftover staging file from a power cut must not trigger "skip"."""
    calls = []

    def convert(input_file, output_file, **kw):
        calls.append(output_file)
        output_file.write_bytes(b"full song")
        return True, None

    d, final = _downloader(monkeypatch, tmp_path, convert=convert,
                           embed=lambda *a, **k: None)
    final.parent.mkdir(parents=True)
    partial_path_for(final).write_bytes(b"trunc")

    _song_out, path = d.search_and_download(_song())
    assert calls, "download was skipped"
    assert final.read_bytes() == b"full song"


def test_convert_names_the_container(monkeypatch, tmp_path):
    captured = {}

    class FakePopen:
        def __init__(self, args, **_kw):
            captured["args"] = args
            self.returncode = 0

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            return False

        def communicate(self):
            return b"", b""

    monkeypatch.setattr(ffmpeg_mod.subprocess, "Popen", FakePopen)
    src = tmp_path / "in.webm"
    src.write_bytes(b"x")
    out = tmp_path / ".Song.mp3.part"
    ok, _ = ffmpeg_mod.convert(src, out, output_format="mp3", force_container=True)
    assert ok
    args = captured["args"]
    assert args[-3:] == ["-f", "mp3", str(out.resolve())]
