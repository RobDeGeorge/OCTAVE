"""SpotifyManager.authenticate() must not touch the network on the GUI thread,
and the fallback token cache must be written atomically and owner-only."""

import json
import os
import stat
import sys
import threading
import time
import types
from unittest import mock

import pytest

import backend.spotify_manager as sm


def _pump_until(qapp, predicate, timeout=2.0):
    deadline = time.monotonic() + timeout
    while not predicate() and time.monotonic() < deadline:
        qapp.processEvents()
        time.sleep(0.005)
    return predicate()


class _FakeOAuth:
    """Stands in for spotipy.SpotifyOAuth; get_cached_token blocks like a
    token refresh over a slow link would."""

    instances = []

    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.release = threading.Event()
        self.called_on = None
        _FakeOAuth.instances.append(self)

    def get_cached_token(self):
        self.called_on = threading.current_thread()
        self.release.wait(5)
        return {"access_token": "x", "expires_at": time.time() + 3600}


@pytest.fixture
def manager(qapp):
    _FakeOAuth.instances = []
    fake_spotipy = types.SimpleNamespace(
        SpotifyOAuth=_FakeOAuth,
        Spotify=mock.MagicMock(name="Spotify"),
    )
    with mock.patch.object(sm, "spotipy", fake_spotipy), \
            mock.patch.object(sm, "SPOTIPY_AVAILABLE", True):
        m = sm.SpotifyManager()
        m._client_id = "id"
        m._client_secret = "secret"
        # Keep the post-connect fetches off the fake client
        m._refresh_devices = lambda: None
        m._refresh_playlists = lambda: None
        yield m, fake_spotipy
        for inst in _FakeOAuth.instances:
            inst.release.set()
        m.cleanup()


def test_authenticate_returns_without_blocking(qapp, manager):
    m, fake_spotipy = manager
    states = []
    m.connectionStateChanged.connect(states.append)

    start = time.monotonic()
    m.authenticate()
    assert time.monotonic() - start < 1.0, "authenticate() blocked the caller"

    # Double-tap while the check is in flight must not start a second one
    m.authenticate()
    assert _pump_until(qapp, lambda: _FakeOAuth.instances
                       and _FakeOAuth.instances[0].called_on is not None)
    assert len(_FakeOAuth.instances) == 1

    oauth = _FakeOAuth.instances[0]
    assert oauth.called_on is not threading.main_thread()
    assert oauth.kwargs.get("requests_timeout")
    assert states == []

    oauth.release.set()
    assert _pump_until(qapp, lambda: states), "connection never completed"
    assert states == [True]
    assert m.is_connected()
    kwargs = fake_spotipy.Spotify.call_args.kwargs
    assert kwargs["auth_manager"] is oauth
    assert kwargs["requests_timeout"]


def test_disconnect_drops_in_flight_auth(qapp, manager):
    m, _ = manager
    states = []
    m.connectionStateChanged.connect(states.append)

    m.authenticate()
    assert _pump_until(qapp, lambda: _FakeOAuth.instances
                       and _FakeOAuth.instances[0].called_on is not None)
    with mock.patch.object(m._cache_handler, "delete_cached_token"):
        m.disconnect()
    _FakeOAuth.instances[0].release.set()

    # Let the worker finish and the queued result arrive
    _pump_until(qapp, lambda: False, timeout=0.3)
    assert states == [False]
    assert not m.is_connected()


@pytest.mark.skipif(sys.platform == "win32", reason="POSIX permissions")
def test_token_file_fallback_is_atomic_and_private(tmp_path):
    handler = sm.KeyringCacheHandler()
    handler._cache_file = str(tmp_path / ".spotify_token_cache")
    token = {"access_token": "secret", "refresh_token": "r", "expires_at": 1}

    with mock.patch.object(sm, "KEYRING_AVAILABLE", False):
        handler.save_token_to_cache(token)
        assert handler.get_cached_token() == token

    with open(handler._cache_file) as f:
        assert json.load(f) == token
    assert stat.S_IMODE(os.stat(handler._cache_file).st_mode) == 0o600
    assert os.listdir(tmp_path) == [".spotify_token_cache"]
