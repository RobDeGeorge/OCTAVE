"""
YouTube Music lookup helpers and local-library scanning for music_dl.

OCTAVE's downloader is YouTube-only: songs are found with ytmusicapi and
downloaded with yt-dlp (the same path the C++ DownloadManager takes).
"""

import logging
from pathlib import Path
from typing import Dict, List, Optional

from ytmusicapi import YTMusic

from backend.music_dl.types.song import Song
from backend.music_dl.utils.metadata import get_file_metadata

__all__ = [
    "get_ytm_client",
    "get_song_from_file_metadata",
    "gather_known_songs",
]

logger = logging.getLogger(__name__)
client = None  # pylint: disable=invalid-name


def get_ytm_client() -> YTMusic:
    """
    Lazily initialize the YTMusic client.

    ### Returns
    - the YTMusic client
    """

    global client  # pylint: disable=global-statement
    if client is None:
        client = YTMusic()

    return client


def get_song_from_file_metadata(file: Path, id3_separator: str = "/") -> Optional[Song]:
    """
    Get song based on the file metadata

    ### Arguments
    - file: Path to file

    ### Returns
    - Song object
    """

    file_metadata = get_file_metadata(file, id3_separator)

    if file_metadata is None:
        return None

    return Song.from_missing_data(**file_metadata)


def gather_known_songs(output: str, output_format: str) -> Dict[str, List[Path]]:
    """
    Gather all known songs from the output directory, keyed by the source
    URL stored in their tags. Files without a stored URL are skipped.

    ### Arguments
    - output: Output path template
    - output_format: Output format

    ### Returns
    - Dictionary containing all known songs and their paths
    """

    # Get the base directory from the path template
    # Path("/Music/test/{artist}/{artists} - {title}.{output-ext}") -> "/Music/test"
    base_dir = output.split("{", 1)[0]
    paths = Path(base_dir).glob(f"**/*.{output_format}")

    known_songs: Dict[str, List[Path]] = {}
    for path in paths:
        song = get_song_from_file_metadata(path)
        if song is None or song.url is None:
            continue

        known_songs.setdefault(song.url, []).append(path)

    return known_songs
