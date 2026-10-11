"""
Song module that holds the Song class.
"""

import json
from dataclasses import asdict, dataclass
from typing import Any, Dict, List, Optional

__all__ = ["Song", "SongError"]


class SongError(Exception):
    """
    Base class for all exceptions related to songs.
    """


@dataclass
class Song:
    """
    Song class. Contains all the information about a song.
    """

    name: str
    artists: List[str]
    artist: str
    genres: List[str]
    disc_number: int
    disc_count: int
    album_name: str
    album_artist: str
    duration: int
    year: int
    date: str
    track_number: int
    tracks_count: int
    song_id: str
    explicit: bool
    publisher: str
    url: str
    isrc: Optional[str]
    cover_url: Optional[str]
    copyright_text: Optional[str]
    download_url: Optional[str] = None
    lyrics: Optional[str] = None
    album_id: Optional[str] = None
    list_name: Optional[str] = None
    list_url: Optional[str] = None
    list_position: Optional[int] = None
    list_length: Optional[int] = None
    artist_id: Optional[str] = None
    album_type: Optional[str] = None

    @classmethod
    def from_data_dump(cls, data: str) -> "Song":
        """
        Create a Song object from a data dump.

        ### Arguments
        - data: The data dump.

        ### Returns
        - The Song object.
        """

        # Create dict from json string
        data_dict = json.loads(data)

        # Return product object
        return cls(**data_dict)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "Song":
        """
        Create a Song object from a dictionary.

        ### Arguments
        - data: The dictionary.

        ### Returns
        - The Song object.
        """

        # Return product object
        return cls(**data)

    @classmethod
    def from_missing_data(cls, **kwargs) -> "Song":
        """
        Create a Song object from a dictionary with missing data.
        For example, data dict doesn't contain all the required
        attributes for the Song class.

        ### Arguments
        - data: The dictionary.

        ### Returns
        - The Song object.
        """

        song_data: Dict[str, Any] = {}
        for key in cls.__dataclass_fields__:  # pylint: disable=E1101
            song_data.setdefault(key, kwargs.get(key))

        return cls(**song_data)

    @property
    def display_name(self) -> str:
        """
        Returns a display name for the song.

        ### Returns
        - The display name.
        """

        return f"{self.artist} - {self.name}"

    @property
    def json(self) -> Dict[str, Any]:
        """
        Returns a dictionary of the song's data.

        ### Returns
        - The dictionary.
        """

        return asdict(self)
