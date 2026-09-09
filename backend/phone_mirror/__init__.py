"""
Phone Mirror module for OCTAVE.

Mirrors an Android phone using OCTAVE's built-in scrcpy-protocol client.
"""

from .manager import PhoneMirrorManager
from .scrcpy_client import ScrcpyClient

__all__ = ['PhoneMirrorManager', 'ScrcpyClient']
