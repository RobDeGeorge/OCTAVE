"""
Downloader module for OCTAVE music_dl (forked from spotdl).
Stripped of: SponsorBlock, lyrics providers, SoundCloud/BandCamp/Piped,
archive, m3u, LRC generation, and web UI features.
Fixed for Python 3.14 asyncio compatibility.
"""

import asyncio
import datetime
import json
import logging
import os
import shutil
import sys
import traceback
from argparse import Namespace
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Type, Union

from backend.music_dl.download.progress_handler import ProgressHandler
from backend.music_dl.providers.audio import (
    AudioProvider,
    YouTubeMusic,
)
from backend.music_dl.types.options import DownloaderOptionalOptions, DownloaderOptions
from backend.music_dl.types.song import Song
from backend.music_dl.utils.config import (
    DOWNLOADER_OPTIONS,
    GlobalConfig,
    create_settings_type,
    get_errors_path,
    get_temp_path,
    modernize_settings,
)
from backend.music_dl.utils.ffmpeg import FFmpegError, convert, get_ffmpeg_path
from backend.music_dl.utils.formatter import create_file_name
from backend.music_dl.utils.metadata import MetadataError, embed_metadata
from backend.music_dl.utils.search import gather_known_songs, reinit_song, songs_from_albums

__all__ = [
    "AUDIO_PROVIDERS",
    "Downloader",
    "DownloaderError",
]

AUDIO_PROVIDERS: Dict[str, Type[AudioProvider]] = {
    "youtube-music": YouTubeMusic,
}

logger = logging.getLogger(__name__)


def partial_path_for(output_file: Path) -> Path:
    """Staging path for ``output_file``: same directory (so the final
    os.replace is a rename, never a copy), hidden, and ending in ".part" so
    library scans that match audio extensions skip it."""
    return output_file.with_name(f".{output_file.name}.part")


def publish_partial(partial_file: Path, output_file: Path) -> None:
    """fsync ``partial_file``, then atomically rename it to ``output_file``."""
    with open(partial_file, "rb+") as handle:
        os.fsync(handle.fileno())
    os.replace(partial_file, output_file)
    if sys.platform != "win32":
        # Persist the rename itself (the directory entry)
        try:
            dir_fd = os.open(output_file.parent, os.O_RDONLY)
            try:
                os.fsync(dir_fd)
            finally:
                os.close(dir_fd)
        except OSError:
            pass


class DownloaderError(Exception):
    """
    Base class for all exceptions related to downloaders.
    """


class Downloader:
    """
    Downloader class — handles downloading, converting, and tagging songs.
    """

    def __init__(
        self,
        settings: Optional[Union[DownloaderOptionalOptions, DownloaderOptions]] = None,
    ):
        if settings is None:
            settings = {}

        # Grab progress callback before settings get filtered to known keys only
        progress_callback = settings.get("_progress_callback") if isinstance(settings, dict) else None

        # Create settings dictionary, fill in missing values with defaults
        self.settings: DownloaderOptions = DownloaderOptions(
            **create_settings_type(
                Namespace(config=False), dict(settings), DOWNLOADER_OPTIONS
            )  # type: ignore
        )

        # Handle deprecated values
        modernize_settings(self.settings)
        logger.debug("Downloader settings: %s", self.settings)

        if len(self.settings["audio_providers"]) == 0:
            raise DownloaderError(
                "No audio providers specified. Please specify at least one."
            )

        # FFmpeg setup
        self.ffmpeg = self.settings["ffmpeg"]
        if self.ffmpeg == "ffmpeg" and shutil.which("ffmpeg") is None:
            ffmpeg_exec = get_ffmpeg_path()
            if ffmpeg_exec is None:
                raise DownloaderError("ffmpeg is not installed")
            else:
                self.ffmpeg = str(ffmpeg_exec.absolute())

        logger.debug("FFmpeg path: %s", self.ffmpeg)

        # Semaphore limit for concurrent downloads (applied per-call)
        self._thread_limit = self.settings["threads"]

        self.progress_handler = ProgressHandler(
            simple_tui=True,
            update_callback=progress_callback,
        )

        # Gather already present songs
        self.scan_formats = self.settings["detect_formats"] or [self.settings["format"]]
        self.known_songs: Dict[str, List[Path]] = {}
        if self.settings["scan_for_songs"]:
            logger.info("Scanning for known songs, this might take a while...")
            for scan_format in self.scan_formats:
                found_files = gather_known_songs(self.settings["output"], scan_format)
                for song_url, song_paths in found_files.items():
                    known_paths = self.known_songs.get(song_url)
                    if known_paths is None:
                        self.known_songs[song_url] = song_paths
                    else:
                        self.known_songs[song_url].extend(song_paths)

        logger.debug("Found %s known songs", len(self.known_songs))

        # Initialize audio providers
        self.audio_providers: List[AudioProvider] = []
        for audio_provider in self.settings["audio_providers"]:
            audio_class = AUDIO_PROVIDERS.get(audio_provider)
            if audio_class is None:
                raise DownloaderError(f"Invalid audio provider: {audio_provider}")

            self.audio_providers.append(
                audio_class(
                    output_format=self.settings["format"],
                    cookie_file=self.settings["cookie_file"],
                    search_query=self.settings["search_query"],
                    filter_results=self.settings["filter_results"],
                    yt_dlp_args=self.settings["yt_dlp_args"],
                )
            )

        # Initialize list of errors
        self.errors: List[str] = []

        # Initialize proxy server
        proxy = self.settings["proxy"]
        proxies = None
        if proxy:
            proxies = {"http": proxy, "https": proxy}
            logger.info("Setting proxy server: %s", proxy)

        GlobalConfig.set_parameter("proxies", proxies)

        logger.debug("Downloader initialized")

    def download_song(self, song: Song) -> Tuple[Song, Optional[Path]]:
        """Download a single song."""
        self.progress_handler.set_song_count(1)
        results = self.download_multiple_songs([song])
        return results[0]

    def download_multiple_songs(
        self, songs: List[Song]
    ) -> List[Tuple[Song, Optional[Path]]]:
        """Download multiple songs."""

        if self.settings["fetch_albums"]:
            albums = set(song.album_id for song in songs if song.album_id is not None)
            songs.extend(songs_from_albums(list(albums)))
            return_obj = {}
            for song in songs:
                return_obj[song.url] = song
            songs = list(return_obj.values())

        logger.debug("Downloading %d songs", len(songs))

        self.progress_handler.set_song_count(len(songs))

        # Create a fresh event loop per call so concurrent calls from
        # different threads don't collide on the same loop
        loop = asyncio.new_event_loop()
        semaphore = asyncio.Semaphore(self._thread_limit)

        async def _pool_download(song: Song) -> Tuple[Song, Optional[Path]]:
            async with semaphore:
                return await loop.run_in_executor(
                    None, self.search_and_download, song
                )

        async def _download_all():
            tasks = [_pool_download(song) for song in songs]
            return await asyncio.gather(*tasks)

        try:
            results = list(loop.run_until_complete(_download_all()))
        finally:
            loop.close()

        if self.settings["print_errors"]:
            for error in self.errors:
                logger.error(error)

        if self.settings["save_errors"]:
            with open(
                self.settings["save_errors"], "a", encoding="utf-8"
            ) as error_file:
                if len(self.errors) > 0:
                    error_file.write(
                        f"{datetime.datetime.now().strftime('%Y-%m-%d-%H-%M-%S')}\n"
                    )
                for error in self.errors:
                    error_file.write(f"{error}\n")

        # Save results to a file
        if self.settings["save_file"]:
            with open(self.settings["save_file"], "w", encoding="utf-8") as save_file:
                json.dump([song.json for song, _ in results], save_file, indent=4)

        return results

    def search(self, song: Song) -> str:
        """Search for a song using all available providers."""
        for audio_provider in self.audio_providers:
            url = audio_provider.search(song, self.settings["only_verified_results"])
            if url:
                return url
            logger.debug("%s failed to find %s", audio_provider.name, song.display_name)

        raise LookupError(f"No results found for song: {song.display_name}")

    def search_and_download(self, song: Song) -> Tuple[Song, Optional[Path]]:
        """Search for the song and download it."""

        # Check if song has required fields
        if not (song.name and (song.artists or song.artist)) and not (
            song.url or song.song_id
        ):
            logger.error("Song is missing required fields: %s", song.display_name)
            self.errors.append(f"Song is missing required fields: {song.display_name}")
            return song, None

        # Reinitialize the song if missing metadata (requires Spotify)
        from backend.music_dl.utils.spotify import SpotifyClient
        if SpotifyClient._initialized:
            if (
                (song.name is None and song.url)
                or self.settings["fetch_albums"]
                or any(
                    x is None
                    for x in [
                        song.genres,
                        song.disc_count,
                        song.tracks_count,
                        song.track_number,
                        song.album_id,
                        song.album_artist,
                    ]
                )
            ):
                song = reinit_song(song)

        # Create the output file path
        output_file = create_file_name(
            song=song,
            template=self.settings["output"],
            file_extension=self.settings["format"],
            restrict=self.settings["restrict"],
            file_name_length=self.settings["max_filename_length"],
        )

        if song.explicit is True and self.settings["skip_explicit"] is True:
            logger.info("Skipping explicit song: %s", song.display_name)
            return song, None

        # Initialize the progress tracker
        display_progress_tracker = self.progress_handler.get_new_tracker(song)

        try:
            temp_folder = get_temp_path()

            # Check for duplicate songs
            dup_song_paths: List[Path] = self.known_songs.get(song.url, [])
            dup_song_paths = [
                p for p in dup_song_paths
                if p.absolute() != output_file.absolute() and p.exists()
            ]

            file_exists = output_file.exists() or dup_song_paths
            if not self.settings["scan_for_songs"]:
                for file_extension in self.scan_formats:
                    ext_path = output_file.with_suffix(f".{file_extension}")
                    if ext_path.exists():
                        dup_song_paths.append(ext_path)

            # Skip if file exists and overwrite mode is "skip"
            if file_exists and self.settings["overwrite"] == "skip":
                logger.info(
                    "Skipping %s (file already exists) %s",
                    song.display_name,
                    "(duplicate)" if dup_song_paths else "",
                )
                display_progress_tracker.notify_download_skip()
                return song, output_file

            # Force overwrite
            if file_exists and self.settings["overwrite"] == "force":
                logger.info("Overwriting %s", song.display_name)
                for dup_song_path in dup_song_paths:
                    try:
                        dup_song_path.unlink()
                    except (PermissionError, OSError) as exc:
                        logger.debug("Could not remove duplicate: %s, %s", dup_song_path, exc)

            # Metadata-only update
            if file_exists and self.settings["overwrite"] == "metadata":
                embed_metadata(
                    output_file=output_file,
                    song=song,
                    skip_album_art=self.settings["skip_album_art"],
                )
                display_progress_tracker.notify_complete()
                return song, output_file

            # Create the output directory
            output_file.parent.mkdir(parents=True, exist_ok=True)

            if song.download_url is None:
                download_url = self.search(song)
            else:
                download_url = song.download_url

            # Initialize audio downloader
            audio_downloader = AudioProvider(
                output_format=self.settings["format"],
                cookie_file=self.settings["cookie_file"],
                search_query=self.settings["search_query"],
                filter_results=self.settings["filter_results"],
                yt_dlp_args=self.settings["yt_dlp_args"],
            )

            logger.debug("Downloading %s using %s", song.display_name, download_url)

            # Add progress hook
            audio_downloader.audio_handler.add_progress_hook(
                display_progress_tracker.yt_dlp_progress_hook
            )

            download_info = audio_downloader.get_download_metadata(
                download_url, download=True
            )

            if download_info is None:
                raise DownloaderError(
                    f"yt-dlp failed to get metadata for: {song.name} - {song.artist}"
                )

            temp_file = Path(
                temp_folder / f"{download_info['id']}.{download_info['ext']}"
            )

            display_progress_tracker.notify_download_complete()

            # Build the song under a staging name and only rename it to
            # output_file once converted, tagged and fsynced. A power cut
            # mid-write then leaves a stray staging file instead of a
            # truncated song that the "skip" overwrite mode would later treat
            # as already downloaded. The name is hidden and ends in ".part",
            # so neither backend's library scan lists it.
            partial_file = partial_path_for(output_file)
            try:
                # Copy or convert
                if (
                    self.settings["bitrate"] in ["auto", "disable", None]
                    and temp_file.suffix == output_file.suffix
                ):
                    shutil.move(str(temp_file), partial_file)
                    success = True
                    result = None
                else:
                    if self.ffmpeg is None:
                        raise DownloaderError(
                            "Conversion needed but ffmpeg is not available"
                        )

                    if self.settings["bitrate"] in ["auto", None]:
                        bitrate = (
                            f"{int(download_info['abr'])}k"
                            if download_info.get("abr")
                            else "128k"
                        )
                    elif self.settings["bitrate"] == "disable":
                        bitrate = None
                    else:
                        bitrate = str(self.settings["bitrate"])

                    success, result = convert(
                        input_file=temp_file,
                        output_file=partial_file,
                        ffmpeg=self.ffmpeg,
                        output_format=self.settings["format"],
                        bitrate=bitrate,
                        ffmpeg_args=self.settings["ffmpeg_args"],
                        progress_handler=display_progress_tracker.ffmpeg_progress_hook,
                        force_container=True,
                    )

                # Remove temp file
                if temp_file.exists():
                    try:
                        temp_file.unlink()
                    except (PermissionError, OSError) as exc:
                        logger.debug("Could not remove temp file: %s, %s", temp_file, exc)
                        raise DownloaderError(
                            f"Could not remove temp file: {temp_file}"
                        ) from exc

                if not success and result:
                    file_name = (
                        get_errors_path()
                        / f"ffmpeg_error_{datetime.datetime.now().strftime('%Y-%m-%d-%H-%M-%S')}.txt"
                    )
                    error_message = ""
                    for key, value in result.items():
                        error_message += f"### {key}:\n{str(value).strip()}\n\n"
                    with open(file_name, "w", encoding="utf-8") as error_path:
                        error_path.write(error_message)
                    raise FFmpegError(
                        f"Failed to convert {song.display_name}, "
                        f"error saved to: {str(file_name.absolute())}"
                    )

                download_info["filepath"] = str(output_file)

                if song.download_url is None:
                    song.download_url = download_url

                display_progress_tracker.notify_conversion_complete()

                # Embed metadata
                try:
                    embed_metadata(
                        partial_file,
                        song,
                        id3_separator=self.settings["id3_separator"],
                        skip_album_art=self.settings["skip_album_art"],
                        file_format=self.settings["format"],
                    )
                except Exception as exception:
                    raise MetadataError(
                        "Failed to embed metadata to the song"
                    ) from exception

                publish_partial(partial_file, output_file)
            except BaseException:
                try:
                    partial_file.unlink()
                except OSError:
                    pass
                raise

            display_progress_tracker.notify_complete()

            self.known_songs.get(song.url, []).append(output_file)

            logger.info('Downloaded "%s": %s', song.display_name, song.download_url)

            return song, output_file
        except (Exception, UnicodeEncodeError) as exception:
            if isinstance(exception, UnicodeEncodeError):
                exception_cause = exception
                exception = DownloaderError(
                    "You may need to add PYTHONIOENCODING=utf-8 to your environment"
                )
                exception.__cause__ = exception_cause

            display_progress_tracker.notify_error(
                traceback.format_exc(), exception, True
            )
            self.errors.append(
                f"{song.url} - {exception.__class__.__name__}: {exception}"
            )
            return song, None
