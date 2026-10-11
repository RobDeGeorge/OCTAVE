"""DownloadManager turns ytmusicapi song search items into the result dicts
the QML download page binds to (same fields the C++ backend emits)."""

from backend.download_manager import DownloadManager


def test_parse_ytm_results_maps_fields_and_skips_non_songs():
    raw = [
        {
            "resultType": "song",
            "videoId": "abc123",
            "title": "Title",
            "artists": [{"name": "Artist"}, {"name": "Guest"}],
            "album": {"name": "Album"},
            "thumbnails": [{"url": "https://lh3.googleusercontent.com/x=w60-h60-l90-rj"}],
            "duration": "1:02:03",
            "year": "2020",
            "isExplicit": True,
        },
        {"resultType": "video", "videoId": "zzz"},
    ]

    results = DownloadManager._parse_ytm_results(raw)

    assert len(results) == 1
    r = results[0]
    assert r["name"] == "Title"
    assert r["artist"] == "Artist"
    assert r["artists"] == ["Artist", "Guest"]
    assert r["album_name"] == "Album"
    assert r["duration"] == 3723
    assert r["year"] == 2020
    assert r["explicit"] is True
    assert r["song_id"] == "abc123"
    assert r["url"] == "https://music.youtube.com/watch?v=abc123"
    # 60px YTM thumbnails are upgraded to the CDN's high-res variant
    assert r["cover_url"].endswith("=w800-h800-l90-rj")


def test_parse_ytm_results_tolerates_missing_fields():
    results = DownloadManager._parse_ytm_results(
        [{"resultType": "song", "duration": "bad", "year": "n/a"}]
    )
    assert results[0]["duration"] == 0
    assert results[0]["year"] == 0
    assert results[0]["url"] == ""
    assert results[0]["cover_url"] == ""
