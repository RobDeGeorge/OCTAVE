"""
Type definitions for music_dl options.
Options types have all the fields marked as required.
Settings types have all the fields marked as optional.
"""

from typing import List, Optional, TypedDict, Union

__all__ = [
    "DownloaderOptions",
    "DownloaderOptionalOptions",
]


class DownloaderOptions(TypedDict):
    """Options used for initializing the Downloader."""

    audio_providers: List[str]
    lyrics_providers: List[str]
    genius_token: str
    scan_for_songs: bool
    m3u: Optional[str]
    output: str
    overwrite: str
    search_query: Optional[str]
    ffmpeg: str
    bitrate: Optional[Union[str, int]]
    ffmpeg_args: Optional[str]
    format: str
    save_file: Optional[str]
    filter_results: bool
    threads: int
    cookie_file: Optional[str]
    restrict: Optional[str]
    print_errors: bool
    sponsor_block: bool
    preload: bool
    archive: Optional[str]
    load_config: bool
    log_level: str
    simple_tui: bool
    id3_separator: str
    add_unavailable: bool
    generate_lrc: bool
    force_update_metadata: bool
    only_verified_results: bool
    sync_without_deleting: bool
    max_filename_length: Optional[int]
    yt_dlp_args: Optional[str]
    detect_formats: Optional[List[str]]
    save_errors: Optional[str]
    ignore_albums: Optional[List[str]]
    proxy: Optional[str]
    skip_explicit: Optional[bool]
    log_format: Optional[str]
    redownload: Optional[bool]
    skip_album_art: Optional[bool]
    create_skip_file: Optional[bool]
    respect_skip_file: Optional[bool]
    sync_remove_lrc: Optional[bool]


class DownloaderOptionalOptions(TypedDict, total=False):
    """Optional version of DownloaderOptions."""

    audio_providers: List[str]
    lyrics_providers: List[str]
    genius_token: str
    scan_for_songs: bool
    m3u: Optional[str]
    output: str
    overwrite: str
    search_query: Optional[str]
    ffmpeg: str
    bitrate: Optional[Union[str, int]]
    ffmpeg_args: Optional[str]
    format: str
    save_file: Optional[str]
    filter_results: bool
    threads: int
    cookie_file: Optional[str]
    restrict: Optional[str]
    print_errors: bool
    sponsor_block: bool
    preload: bool
    archive: Optional[str]
    load_config: bool
    log_level: str
    simple_tui: bool
    id3_separator: str
    add_unavailable: bool
    generate_lrc: bool
    force_update_metadata: bool
    only_verified_results: bool
    sync_without_deleting: bool
    max_filename_length: Optional[int]
    yt_dlp_args: Optional[str]
    detect_formats: Optional[List[str]]
    save_errors: Optional[str]
    proxy: Optional[str]
    skip_explicit: Optional[bool]
    log_format: Optional[str]
    redownload: Optional[bool]
    skip_album_art: Optional[bool]
    create_skip_file: Optional[bool]
    respect_skip_file: Optional[bool]
    sync_remove_lrc: Optional[bool]
