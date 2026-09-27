#!/usr/bin/env python3
"""YouTube Music search helper for the C++ backend.

Usage: python ytmusic_search.py <query> [limit]

Outputs one JSON object per line, matching the format expected by
DownloadManager::_parseSearchOutput().
"""

import json
import re
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(1)

    query = sys.argv[1]
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 25

    from ytmusicapi import YTMusic

    ytm = YTMusic()
    raw = ytm.search(query, filter="songs", limit=limit)

    if not raw:
        sys.exit(0)

    for item in raw:
        if item.get("resultType") != "song":
            continue

        video_id = item.get("videoId", "")
        if not video_id:
            continue

        artists = [a.get("name", "") for a in item.get("artists", [])]
        album = item.get("album") or {}
        thumbnails = item.get("thumbnails", [])
        cover_url = thumbnails[-1].get("url", "") if thumbnails else ""

        if cover_url and re.search(r"(lh3|yt3)\.googleusercontent\.com", cover_url):
            cover_url = re.sub(r"=w\d+-h\d+.*$", "=w800-h800-l90-rj", cover_url)

        duration_secs = item.get("duration_seconds", 0)
        if not duration_secs:
            duration_str = item.get("duration", "")
            if duration_str:
                parts = duration_str.split(":")
                try:
                    if len(parts) == 2:
                        duration_secs = int(parts[0]) * 60 + int(parts[1])
                    elif len(parts) == 3:
                        duration_secs = int(parts[0]) * 3600 + int(parts[1]) * 60 + int(parts[2])
                except (ValueError, IndexError):
                    pass

        year = 0
        if item.get("year"):
            try:
                year = int(item["year"])
            except (ValueError, TypeError):
                pass

        result = {
            "id": video_id,
            "title": item.get("title", ""),
            "channel": artists[0] if artists else "",
            "uploader": artists[0] if artists else "",
            "album": album.get("name", ""),
            "duration": duration_secs,
            "release_year": year,
            "thumbnails": [{"url": cover_url}] if cover_url else [],
            "thumbnail": cover_url,
        }

        print(json.dumps(result), flush=True)


if __name__ == "__main__":
    main()
