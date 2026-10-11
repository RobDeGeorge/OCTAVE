"""
MusicDL — OCTAVE's music download engine (forked from spotdl).
YouTube-only: songs are found on YouTube Music (ytmusicapi), audio is
downloaded with yt-dlp, then converted and tagged locally.
"""

import concurrent.futures
import logging
from pathlib import Path
from typing import List, Optional, Tuple, Union

from backend.music_dl._version import __version__
from backend.music_dl.download.downloader import Downloader
from backend.music_dl.types.options import DownloaderOptionalOptions, DownloaderOptions
from backend.music_dl.types.song import Song

__all__ = ["MusicDL", "__version__"]

logger = logging.getLogger(__name__)


class MusicDL:
    """
    Main entry point for OCTAVE's music download engine.

    Usage:
        from backend.music_dl import MusicDL
        from backend.music_dl.types.song import Song

        dl = MusicDL()
        song = Song.from_missing_data(name='Test Drive', artist='Joji',
                                      artists=['Joji'],
                                      download_url='https://music.youtube.com/watch?v=...')
        song, path = dl.download(song)
    """

    def __init__(
        self,
        downloader_settings: Optional[
            Union[DownloaderOptionalOptions, DownloaderOptions]
        ] = None,
    ):
        if downloader_settings is None:
            downloader_settings = {}

        self.downloader = Downloader(
            settings=downloader_settings,
        )

    def get_download_urls(self, songs: List[Song]) -> List[Optional[str]]:
        """
        Get the YouTube Music download urls for a list of songs (multi-threaded).
        """

        urls: List[Optional[str]] = []
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=self.downloader.settings["threads"]
        ) as executor:
            future_to_song = {
                executor.submit(self.downloader.search, song): song for song in songs
            }
            for future in concurrent.futures.as_completed(future_to_song):
                song = future_to_song[future]
                try:
                    data = future.result()
                    urls.append(data)
                except Exception as exc:
                    logger.error("%s generated an exception: %s", song, exc)

        return urls

    def download(self, song: Song) -> Tuple[Song, Optional[Path]]:
        """Download and convert a single song."""
        return self.downloader.download_song(song)

    def download_songs(self, songs: List[Song]) -> List[Tuple[Song, Optional[Path]]]:
        """Download and convert multiple songs."""
        return self.downloader.download_multiple_songs(songs)

    def cleanup(self):
        """Clean up resources (nothing to release; kept for the manager's
        shutdown path)."""
