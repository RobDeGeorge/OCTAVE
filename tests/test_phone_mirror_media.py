"""Phone media (OCTAVE controls the phone's media session): device message
parsing in the client, and the follow / hand-back rules in the manager.

No phone or adb needed: device messages go through a socket pair, and the
manager is fed parsed states directly.
"""

import socket
import struct

from backend.phone_mirror import scrcpy_client as sc
from backend.phone_mirror.manager import PhoneMirrorManager


def _short_str(s):
    raw = s.encode("utf-8")
    return struct.pack(">H", len(raw)) + raw


def _media_state_msg(playing, title, position=1000, duration=200000, package="com.spotify.music"):
    flags = 1 | (2 if playing else 0) | 4
    return (bytes([sc.DEVMSG_OCTAVE_MEDIA_STATE, flags]) + struct.pack(">qq", position, duration)
            + _short_str(package) + _short_str(title) + _short_str("Artist") + _short_str("Album"))


def _state(playing, title="Song", has_session=True):
    return {"has_session": has_session, "playing": playing, "can_seek": True, "position_ms": 1000,
            "duration_ms": 200000, "package": "com.example.music", "title": title, "artist": "A", "album": "B"}


def test_client_parses_media_messages():
    client = sc.ScrcpyClient("adb", "jar")
    states, arts = [], []
    client.mediaStateChanged.connect(states.append)
    client.mediaArtChanged.connect(arts.append)
    a, b = socket.socketpair()
    try:
        a.sendall(_media_state_msg(True, "Dummy ✓")
                  + bytes([sc.DEVMSG_OCTAVE_MEDIA_ART]) + struct.pack(">I", 3) + b"\xff\xd8\xff"
                  + bytes([sc.DEVMSG_OCTAVE_MEDIA_ART]) + struct.pack(">I", 0))
        a.close()
        client._drain_device_messages(b)
    finally:
        b.close()
    assert states == [{"has_session": True, "playing": True, "can_seek": True, "position_ms": 1000,
                       "duration_ms": 200000, "package": "com.spotify.music", "title": "Dummy ✓",
                       "artist": "Artist", "album": "Album"}]
    assert arts == [b"\xff\xd8\xff", b""]


def test_media_command_wire_format():
    client = sc.ScrcpyClient("adb", "jar")
    sent = []
    client._send = sent.append
    client.media_command(sc.MEDIA_SEEK, 61000)
    assert sent == [struct.pack(">BBq", sc.MSG_OCTAVE_MEDIA, sc.MEDIA_SEEK, 61000)]
    assert len(sent[0]) == 10


def test_phone_has_controls_for_the_whole_session():
    m = PhoneMirrorManager()
    changes = []
    m.mediaActiveChanged.connect(changes.append)
    m._on_media_state(_state(False, has_session=False))
    assert m.mediaActive and not m.mediaAvailable    # first report: the phone is the source
    m._on_media_state(_state(True))
    assert m.mediaActive and m.mediaPlaying and m.mediaTitle == "Song"
    m._on_media_state(_state(False))
    m._on_media_state(_state(False, has_session=False))
    assert m.mediaActive                             # paused, or no media app: still the phone
    m.releaseMedia(False)                            # a local track started
    m._on_media_state(_state(False))
    assert not m.mediaActive                         # a paused phone does not take them back
    m._on_media_state(_state(True))
    assert m.mediaActive                             # playing again does
    m._reset_media()
    assert changes == [True, False, True, False]


def test_mute_silences_phone_audio():
    m = PhoneMirrorManager()
    volumes = []

    class Client:
        def set_volume(self, v):
            volumes.append(v)

    m._client = Client()
    m.setVolume(0.5)
    m.setMuted(True)
    m.setVolume(0.7)          # the slider moves while muted: stays silent
    m.setMuted(False)
    m._client = None
    assert volumes == [0.5, 0.0, 0.0, 0.7]


def test_release_hands_controls_back_and_pauses_phone():
    m = PhoneMirrorManager()
    sent = []
    m._media_command = lambda action, argument=0: sent.append(action)
    m._on_media_state(_state(True))
    m.releaseMedia(False)
    assert not m.mediaActive and sent == []
    m._on_media_state(_state(False))
    m._on_media_state(_state(True))
    m.releaseMedia(True)
    assert not m.mediaActive and sent == [sc.MEDIA_PAUSE]


def test_reset_clears_media():
    m = PhoneMirrorManager()
    m._on_media_state(_state(True))
    m._reset_media()
    assert not m.mediaActive and not m.mediaAvailable and m.mediaTitle == "" and m.mediaArtUrl == ""
