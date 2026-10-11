"""
Configuration module for OCTAVE music_dl (forked from spotdl).
Simplified: removed web UI, hardcoded credentials, and CLI-specific config.
"""

import json
import logging
import os
import platform
from argparse import Namespace
from pathlib import Path
from typing import Any, Dict

from backend.music_dl.types.options import DownloaderOptions

__all__ = [
    "ConfigError",
    "get_music_dl_path",
    "get_config_file",
    "get_temp_path",
    "get_errors_path",
    "get_config",
    "create_settings_type",
    "DOWNLOADER_OPTIONS",
    "DEFAULT_CONFIG",
    # Keep old name as alias for compatibility
    "get_spotdl_path",
]

logger = logging.getLogger(__name__)


class ConfigError(Exception):
    """
    Base class for all exceptions related to config.
    """


def get_music_dl_path() -> Path:
    """
    Get the path to the music_dl config folder.
    Uses OCTAVE's backend/temp/music_dl/ for temp data,
    and ~/.config/music_dl/ for persistent config.
    """
    if platform.system() == "Linux":
        config_path = Path.home() / ".config" / "music_dl"
    elif platform.system() == "Darwin":
        config_path = Path.home() / "Library" / "Application Support" / "music_dl"
    else:
        config_path = Path.home() / ".music_dl"

    os.makedirs(config_path, exist_ok=True)
    return config_path


# Alias for backward compatibility with spotdl code
get_spotdl_path = get_music_dl_path


def get_config_file() -> Path:
    return get_music_dl_path() / "config.json"


def get_temp_path() -> Path:
    temp_path = get_music_dl_path() / "temp"
    if not temp_path.exists():
        os.mkdir(temp_path)
    return temp_path


def get_errors_path() -> Path:
    errors_path = get_music_dl_path() / "errors"
    if not errors_path.exists():
        os.mkdir(errors_path)
    return errors_path


def get_config() -> Dict[str, Any]:
    config_path = get_config_file()
    if not config_path.exists():
        return {}
    with open(config_path, "r", encoding="utf-8") as config_file:
        return json.load(config_file)


def create_settings_type(
    arguments: Namespace,
    config: Dict[str, Any],
    default: DownloaderOptions,
) -> Dict[str, Any]:
    """
    Create settings dict.
    Argument value has priority, then config file value, then default.
    """
    settings = {}
    for key, default_value in default.items():
        argument_val = arguments.__dict__.get(key)
        config_val = config.get(key)

        if argument_val is not None:
            settings[key] = argument_val
        elif config_val is not None:
            settings[key] = config_val
        else:
            settings[key] = default_value

    return settings


def modernize_settings(options: DownloaderOptions):
    """Handle deprecated values in config file."""
    if options["restrict"] is True:
        logger.warning("Deprecated 'True' for 'restrict'. Using 'strict' instead.")
        options["restrict"] = "strict"


class GlobalConfig:
    """Class to store global configuration."""
    parameters: Dict[str, Any] = {}

    @classmethod
    def set_parameter(cls, key, value):
        cls.parameters[key] = value

    @classmethod
    def get_parameter(cls, key):
        return cls.parameters.get(key, None)


DOWNLOADER_OPTIONS: DownloaderOptions = {
    "audio_providers": ["youtube-music"],
    "lyrics_providers": [],
    "genius_token": "",
    "scan_for_songs": False,
    "m3u": None,
    "output": "{artists} - {title}.{output-ext}",
    "overwrite": "skip",
    "search_query": None,
    "ffmpeg": "ffmpeg",
    "bitrate": "auto",
    "ffmpeg_args": None,
    "format": "mp3",
    "save_file": None,
    "filter_results": True,
    "threads": 2,
    "cookie_file": None,
    "restrict": None,
    "print_errors": False,
    "sponsor_block": False,
    "preload": False,
    "archive": None,
    "load_config": False,
    "log_level": "INFO",
    "simple_tui": True,
    "id3_separator": "/",
    "add_unavailable": False,
    "generate_lrc": False,
    "force_update_metadata": False,
    "only_verified_results": False,
    "sync_without_deleting": False,
    "max_filename_length": None,
    "yt_dlp_args": None,
    "detect_formats": None,
    "save_errors": None,
    "ignore_albums": None,
    "proxy": None,
    "skip_explicit": False,
    "log_format": None,
    "redownload": False,
    "skip_album_art": False,
    "create_skip_file": False,
    "respect_skip_file": False,
    "sync_remove_lrc": False,
}

DEFAULT_CONFIG = {
    **DOWNLOADER_OPTIONS,  # type: ignore
}
