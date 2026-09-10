"""
Spotify Connect Manager for OCTAVE
Controls Spotify playback on connected devices (phone, desktop, etc.)
"""

from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer, QMetaObject, Qt, Q_ARG
from PySide6.QtGui import QImage
import os
import json
import webbrowser
import secrets
import time
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import threading
from concurrent.futures import ThreadPoolExecutor

from backend.logging_config import get_logger
logger = get_logger(__name__)

from backend.lazy_import import available, lazy_module

# spotipy (which imports redis) and keyring together cost ~0.4 s at import on
# the Pi and are only needed once the user connects; import them on first use.
SPOTIPY_AVAILABLE = available("spotipy")
if not SPOTIPY_AVAILABLE:
    logger.warning("spotipy not installed. Run: pip install spotipy")
spotipy = lazy_module("spotipy")

KEYRING_AVAILABLE = available("keyring")
if not KEYRING_AVAILABLE:
    logger.warning("keyring not installed. Token storage will be less secure.")
keyring = lazy_module("keyring")


# spotipy duck-types its cache handler (get_cached_token / save_token_to_cache),
# so no base class import is needed here.
_CacheHandlerBase = object


class KeyringCacheHandler(_CacheHandlerBase):
    """
    Secure token cache using OS keychain (Windows Credential Manager, macOS Keychain, etc.)
    Falls back to file-based cache if keyring is not available.
    """
    SERVICE_NAME = "OCTAVE_Spotify"
    USERNAME = "spotify_token"

    def __init__(self):
        super().__init__()
        # Fallback file cache location (in backend directory)
        self._cache_file = os.path.join(
            os.path.dirname(os.path.abspath(__file__)),
            '.spotify_token_cache'
        )

    def get_cached_token(self):
        """Retrieve token from OS keychain or fallback file"""
        if KEYRING_AVAILABLE:
            try:
                token_string = keyring.get_password(self.SERVICE_NAME, self.USERNAME)
                if token_string:
                    return json.loads(token_string)
            except Exception as e:
                logger.debug(f"Keyring read error: {e}")

        # Fallback to file cache
        if os.path.exists(self._cache_file):
            try:
                with open(self._cache_file, 'r') as f:
                    return json.load(f)
            except Exception as e:
                logger.debug(f"File cache read error: {e}")
        return None

    def save_token_to_cache(self, token_info):
        """Save token to OS keychain or fallback file"""
        if KEYRING_AVAILABLE:
            try:
                token_string = json.dumps(token_info)
                keyring.set_password(self.SERVICE_NAME, self.USERNAME, token_string)
                return
            except Exception as e:
                logger.debug(f"Keyring write error: {e}")

        # Fallback to file cache
        try:
            with open(self._cache_file, 'w') as f:
                json.dump(token_info, f)
        except Exception as e:
            logger.debug(f"File cache write error: {e}")

    def delete_cached_token(self):
        """Remove token from OS keychain and fallback file"""
        if KEYRING_AVAILABLE:
            try:
                keyring.delete_password(self.SERVICE_NAME, self.USERNAME)
            except keyring.errors.PasswordDeleteError:
                pass  # Token didn't exist
            except Exception as e:
                logger.debug(f"Keyring delete error: {e}")

        # Also delete file cache
        if os.path.exists(self._cache_file):
            try:
                os.remove(self._cache_file)
            except Exception as e:
                logger.debug(f"File cache delete error: {e}")


class SpotifyManager(QObject):
    # Connection state signals
    connectionStateChanged = Signal(bool)  # True when authenticated
    errorOccurred = Signal(str)  # Error messages

    # Playback state signals (mirror MediaManager's signals)
    playStateChanged = Signal(bool)
    currentTrackChanged = Signal(str, str, str, str)  # title, artist, album, art_url
    durationChanged = Signal(int)
    positionChanged = Signal(int)
    shuffleStateChanged = Signal(bool)
    volumeChanged = Signal(int)  # Volume 0-100

    # Device signals
    devicesChanged = Signal(list)  # List of available Spotify Connect devices
    activeDeviceChanged = Signal(str)  # Currently active device name

    # Playlist/library signals
    playlistsChanged = Signal(list)  # List of user playlists
    spotifyTracksChanged = Signal(list)  # Tracks from selected playlist
    currentSpotifyPlaylistChanged = Signal(str)  # Current playlist name

    # Auth flow signal
    authUrlReady = Signal(str)  # URL for user to visit for OAuth

    # Status progress signal for terminal-style feedback
    statusProgress = Signal(str)  # Status messages with prefixes like [INFO], [ERROR], etc.

    # Album Art Capture signal (matches MediaManager)
    albumColorsExtracted = Signal(str)  # JSON string with extracted album art colors

    # Internal signal for thread-safe initialization after auth
    _authCompleted = Signal()

    # Queue neighbor signal — emitted when queue-based prev/next art is ready
    queueUpdated = Signal()

    # Internal signals for async API results (thread-safe)
    _playbackStateReady = Signal(object)  # Playback data from API
    _queueReady = Signal(list)            # Queue tracks from API
    _colorsReady = Signal(str)            # Extracted theme JSON from worker
    _devicesReady = Signal(list)  # Devices from API
    _playlistsReady = Signal(list)  # Playlists from API

    def __init__(self):
        super().__init__()

        self._is_connected = False
        self._sp = None  # Spotipy client

        # Thread pool for async API calls (max 2 concurrent to avoid rate limits)
        self._executor = ThreadPoolExecutor(max_workers=2)
        self._current_track = {}
        self._devices = []
        self._active_device_id = None
        self._playlists = []
        self._is_playing = False
        self._current_position_ms = 0
        self._is_shuffled = False
        self._current_volume = 100  # Volume 0-100

        # Spotify playlist tracks state
        self._spotify_tracks = []
        self._current_spotify_playlist_id = ""
        self._current_spotify_playlist_name = ""

        # Cached queue for side-card art when no playlist is loaded
        self._cached_queue = []  # list of {name, artist, album, image}
        self._previous_track = {}  # last played track for "behind" side card

        # Credentials (loaded from settings)
        self._client_id = ""
        self._client_secret = ""
        self._redirect_uri = "http://127.0.0.1:8888/callback"

        # Secure token cache using OS keychain
        self.backend_dir = os.path.dirname(os.path.abspath(__file__))
        self._cache_handler = KeyringCacheHandler()

        # OAuth state for CSRF protection
        self._oauth_state = None

        # Polling timer for playback state (API calls)
        self._poll_timer = QTimer()
        self._poll_timer.setInterval(3000)  # Poll every 3 seconds (avoid rate limits)
        self._poll_timer.timeout.connect(self._poll_playback_state)

        # Fast local timer for smooth position interpolation (no API calls)
        self._interpolation_timer = QTimer()
        self._interpolation_timer.setInterval(250)  # Update UI 4x per second
        self._interpolation_timer.timeout.connect(self._interpolate_position)

        # Track last known position and time for interpolation
        self._last_known_position_ms = 0
        self._last_poll_timestamp = 0

        # Store auth_manager for use after thread callback
        self._pending_auth_manager = None

        # Connect internal signals for thread-safe callbacks
        self._authCompleted.connect(self._on_auth_completed)
        self._playbackStateReady.connect(self._handle_playback_state)
        self._queueReady.connect(self._handle_queue_result)
        self._colorsReady.connect(self._handle_colors_result)
        self._devicesReady.connect(self._handle_devices_result)
        self._playlistsReady.connect(self._handle_playlists_result)

        # Track pending async operations to avoid duplicates
        self._poll_in_progress = False

        # Rate-limited background poll error surfacing. Playback polls run on a
        # ~1–10s timer, so raw failures would spam the UI on a dropped network.
        # Only surface to the user after several consecutive failures and at
        # most once per POLL_ERROR_EMIT_INTERVAL seconds.
        self._consecutive_playback_errors = 0
        self._last_playback_error_ts = 0.0
        self.POLL_ERROR_THRESHOLD = 5
        self.POLL_ERROR_EMIT_INTERVAL = 30.0

        self._settings_manager = None

        # Album Art Capture theme state
        self._album_art_capture_active = False
        self._last_extracted_image_url = ""  # Avoid re-extracting same image

    @Slot(QObject)
    def connect_settings_manager(self, settings_manager):
        """Connect to settings manager to load/save credentials"""
        self._settings_manager = settings_manager
        self._load_credentials()

        # Connect theme change signal for Album Art Capture
        if hasattr(settings_manager, 'themeSettingChanged'):
            settings_manager.themeSettingChanged.connect(self._on_theme_changed)

        # Check if Album Art Capture is already active
        current_theme = settings_manager.themeSetting
        self._album_art_capture_active = (current_theme == "Album Art Capture")

    def _load_credentials(self):
        """Load Spotify credentials from settings"""
        if self._settings_manager:
            self._client_id = self._settings_manager.get_spotify_client_id()
            self._client_secret = self._settings_manager.get_spotify_client_secret()

    @Slot()
    def _on_auth_completed(self):
        """Called on main thread after successful OAuth callback"""
        if self._pending_auth_manager:
            self.statusProgress.emit("[SUCCESS] OAuth completed, setting up connection...")
            self._sp = spotipy.Spotify(auth_manager=self._pending_auth_manager)
            self._is_connected = True
            self.connectionStateChanged.emit(True)
            self._poll_timer.start()
            self._interpolation_timer.start()  # Start smooth position updates
            self._refresh_devices()
            self._refresh_playlists()
            self._pending_auth_manager = None
            self.statusProgress.emit("[DONE] Successfully connected to Spotify!")
            logger.info("Post-auth setup completed on main thread")

    @Slot(result=bool)
    def is_available(self):
        """Check if spotipy library is installed"""
        return SPOTIPY_AVAILABLE

    @Slot(result=bool)
    def is_connected(self):
        """Check if we're authenticated with Spotify"""
        return self._is_connected

    @Slot(result=bool)
    def has_credentials(self):
        """Check if client ID and secret are configured"""
        return bool(self._client_id and self._client_secret)

    @Slot(str, str)
    def set_credentials(self, client_id, client_secret):
        """Set Spotify API credentials"""
        self._client_id = client_id
        self._client_secret = client_secret

        # Save to settings
        if self._settings_manager:
            self._settings_manager.save_spotify_credentials(client_id, client_secret)

    @Slot()
    def authenticate(self):
        """Start OAuth authentication flow"""
        self.statusProgress.emit("[INFO] Starting Spotify authentication...")

        if not SPOTIPY_AVAILABLE:
            self.statusProgress.emit("[ERROR] Spotipy library not installed")
            self.errorOccurred.emit("Spotipy library not installed")
            return

        if not self.has_credentials():
            self.statusProgress.emit("[ERROR] Spotify credentials not configured")
            self.errorOccurred.emit("Spotify credentials not configured")
            return

        try:
            # Required scopes for playback control
            scope = (
                "user-read-playback-state "
                "user-modify-playback-state "
                "user-read-currently-playing "
                "playlist-read-private "
                "playlist-read-collaborative "
                "user-library-read"
            )

            # Generate secure state for CSRF protection
            self._oauth_state = secrets.token_urlsafe(32)

            auth_manager = spotipy.SpotifyOAuth(
                client_id=self._client_id,
                client_secret=self._client_secret,
                redirect_uri=self._redirect_uri,
                scope=scope,
                cache_handler=self._cache_handler,
                open_browser=False,
                state=self._oauth_state
            )

            # Check if we have a cached token
            token_info = auth_manager.get_cached_token()

            if token_info:
                # We have a valid token, create client
                self.statusProgress.emit("[INFO] Found cached token, connecting...")
                self._sp = spotipy.Spotify(auth_manager=auth_manager)
                self._is_connected = True
                self.connectionStateChanged.emit(True)
                self._poll_timer.start()
                self._interpolation_timer.start()  # Start smooth position updates
                self._refresh_devices()
                self._refresh_playlists()
                self.statusProgress.emit("[SUCCESS] Connected with cached token")
                logger.info("Connected with cached token")
            else:
                # Need to authenticate - get auth URL
                self.statusProgress.emit("[INFO] No cached token, starting OAuth flow...")
                auth_url = auth_manager.get_authorize_url()
                self.authUrlReady.emit(auth_url)

                # Start local server to catch the callback
                self._start_auth_server(auth_manager)
                self.statusProgress.emit("[INFO] Waiting for browser authentication...")

        except Exception as e:
            self.statusProgress.emit(f"[ERROR] Authentication failed: {str(e)}")
            self.errorOccurred.emit(f"Authentication failed: {str(e)}")
            logger.error(f"Auth error: {e}")

    def _start_auth_server(self, auth_manager):
        """Start a local HTTP server to catch OAuth callback"""
        manager = self

        class CallbackHandler(BaseHTTPRequestHandler):
            def do_GET(self):
                query = urlparse(self.path).query
                params = parse_qs(query)

                # Validate state parameter to prevent CSRF attacks
                received_state = params.get('state', [None])[0]
                if received_state != manager._oauth_state:
                    self.send_response(403)
                    self.send_header('Content-type', 'text/html')
                    self.end_headers()
                    self.wfile.write(b"""
                        <html><body style="font-family: sans-serif; text-align: center; padding-top: 50px;">
                        <h1>Authentication Failed</h1>
                        <p>Invalid state parameter. Please try again.</p>
                        </body></html>
                    """)
                    manager.errorOccurred.emit("OAuth state mismatch - possible CSRF attack")
                    return

                if 'code' in params:
                    code = params['code'][0]
                    try:
                        # Exchange code for token
                        auth_manager.get_access_token(code)

                        # Clear the state after successful use
                        manager._oauth_state = None

                        # Store auth_manager and emit signal to complete setup on main thread
                        manager._pending_auth_manager = auth_manager
                        manager._authCompleted.emit()

                        # Send success response
                        self.send_response(200)
                        self.send_header('Content-type', 'text/html')
                        self.end_headers()
                        self.wfile.write(b"""
                            <html><body style="font-family: sans-serif; text-align: center; padding-top: 50px;">
                            <h1>Success!</h1>
                            <p>Spotify connected. You can close this window.</p>
                            </body></html>
                        """)
                        logger.info("Successfully authenticated")
                    except Exception as e:
                        manager.errorOccurred.emit(f"Token exchange failed: {str(e)}")
                        self.send_response(500)
                        self.end_headers()
                else:
                    self.send_response(400)
                    self.end_headers()

            def log_message(self, format, *args):
                pass  # Suppress HTTP server logs

        def run_server():
            server = HTTPServer(('127.0.0.1', 8888), CallbackHandler)
            server.timeout = 120  # 2 minute timeout
            server.handle_request()  # Handle single request then stop

        # Run server in background thread
        thread = threading.Thread(target=run_server, daemon=True)
        thread.start()

        # Open browser for user to authenticate
        auth_url = auth_manager.get_authorize_url()
        logger.debug(f"Auth URL: {auth_url}")

        # Open browser for authentication
        import platform

        opened = False
        if platform.system() == 'Windows':
            try:
                # Use os.startfile on Windows (safe, no shell injection risk)
                os.startfile(auth_url)
                opened = True
                logger.debug("Opened browser via os.startfile")
            except Exception as e:
                logger.debug(f"os.startfile failed: {e}")

        if not opened:
            try:
                # Fallback: standard webbrowser module (cross-platform)
                webbrowser.open(auth_url)
                opened = True
                logger.debug("Opened browser via webbrowser module")
            except Exception as e:
                logger.debug(f"webbrowser.open failed: {e}")

        if not opened:
            # Last resort: emit URL for user to copy manually
            self.authUrlReady.emit(auth_url)
            self.errorOccurred.emit(f"Could not open browser. Please visit: {auth_url}")

    @Slot()
    def disconnect(self):
        """Disconnect from Spotify"""
        self.statusProgress.emit("[INFO] Disconnecting from Spotify...")
        self._poll_timer.stop()
        self._interpolation_timer.stop()
        self._sp = None
        self._is_connected = False
        self._devices = []
        self._current_track = {}
        self._cached_queue = []
        self._previous_track = {}

        # Remove cached token from OS keychain
        self._cache_handler.delete_cached_token()
        self.statusProgress.emit("[INFO] Cleared cached token")

        self.connectionStateChanged.emit(False)
        self.statusProgress.emit("[DONE] Disconnected from Spotify")
        logger.info("Disconnected")

    @Slot()
    def cleanup(self):
        """Stop all timers and threads - call before app exit"""
        logger.debug("Cleaning up...")

        # Stop timers first to prevent new tasks
        self._poll_timer.stop()
        self._interpolation_timer.stop()

        # Mark as not connected to prevent callbacks from doing work
        self._is_connected = False

        # Clear the client to make API calls fail fast
        self._sp = None

        # Shutdown executor - cancel pending tasks
        self._executor.shutdown(wait=False, cancel_futures=True)

        logger.debug("Cleanup complete")

    # ==================== Playback Control ====================

    def _run_async(self, func, success_callback=None, error_msg="Operation failed"):
        """Run a Spotify API call asynchronously"""
        def on_done(future):
            # Skip if shutting down
            if not self._sp:
                return
            try:
                result = future.result()
                if success_callback:
                    # Use QMetaObject to call callback on main thread
                    QMetaObject.invokeMethod(self, success_callback, Qt.QueuedConnection)
            except Exception as e:
                if self._sp:  # Only emit error if not shutting down
                    self.errorOccurred.emit(f"{error_msg}: {str(e)}")

        self._executor.submit(func).add_done_callback(on_done)

    @Slot()
    def play(self):
        """Resume playback"""
        if not self._sp:
            return
        # Optimistic UI update
        self._is_playing = True
        self.playStateChanged.emit(True)

        def do_play():
            self._sp.start_playback(device_id=self._active_device_id)

        self._run_async(do_play, error_msg="Play failed")

    @Slot()
    def pause(self):
        """Pause playback"""
        if not self._sp:
            return
        # Optimistic UI update
        self._is_playing = False
        self.playStateChanged.emit(False)

        def do_pause():
            self._sp.pause_playback(device_id=self._active_device_id)

        self._run_async(do_pause, error_msg="Pause failed")

    @Slot()
    def toggle_play(self):
        """Toggle play/pause"""
        if not self._sp:
            return
        if self._is_playing:
            self.pause()
        else:
            self.play()

    @Slot()
    def next_track(self):
        """Skip to next track"""
        if not self._sp:
            return

        def do_next():
            self._sp.next_track(device_id=self._active_device_id)

        self._run_async(do_next, error_msg="Next track failed")

    @Slot()
    def previous_track(self):
        """Skip to previous track"""
        if not self._sp:
            return

        def do_prev():
            self._sp.previous_track(device_id=self._active_device_id)

        self._run_async(do_prev, error_msg="Previous track failed")

    @Slot(int)
    def set_position(self, position_ms):
        """Seek to position in current track"""
        if not self._sp:
            return
        # Optimistic UI update
        self._current_position_ms = position_ms
        self._last_known_position_ms = position_ms
        self._last_poll_timestamp = time.time()

        def do_seek():
            self._sp.seek_track(position_ms, device_id=self._active_device_id)

        self._run_async(do_seek, error_msg="Seek failed")

    @Slot(int)
    def set_volume(self, volume_percent):
        """Set playback volume (0-100)"""
        if not self._sp:
            return
        volume = max(0, min(100, int(volume_percent)))
        # Optimistic UI update
        self._current_volume = volume
        self.volumeChanged.emit(volume)

        def do_volume():
            self._sp.volume(volume, device_id=self._active_device_id)

        self._run_async(do_volume, error_msg="Volume failed")

    @Slot()
    def toggle_shuffle(self):
        """Toggle shuffle mode"""
        if not self._sp:
            return
        new_state = not self._is_shuffled
        # Optimistic UI update
        self._is_shuffled = new_state
        self.shuffleStateChanged.emit(new_state)

        def do_shuffle():
            self._sp.shuffle(new_state, device_id=self._active_device_id)

        self._run_async(do_shuffle, error_msg="Shuffle toggle failed")

    @Slot(result=bool)
    def is_shuffled(self):
        """Check if shuffle is enabled"""
        return self._is_shuffled

    @Slot(str)
    def play_uri(self, uri):
        """Play a specific Spotify URI (track, album, playlist)"""
        if not self._sp:
            return

        # Optimistic UI update
        self._is_playing = True
        self.playStateChanged.emit(True)

        # Prepare the API call parameters
        device_id = self._active_device_id
        playlist_id = self._current_spotify_playlist_id
        tracks = self._spotify_tracks

        def do_play():
            if uri.startswith('spotify:track:'):
                # If we have a current playlist context, play within that context
                if playlist_id:
                    playlist_uri = f"spotify:playlist:{playlist_id}"
                    # Find the offset of this track in the playlist
                    offset = None
                    for i, track in enumerate(tracks):
                        if track.get('uri') == uri:
                            offset = i
                            break

                    if offset is not None:
                        self._sp.start_playback(
                            device_id=device_id,
                            context_uri=playlist_uri,
                            offset={"position": offset}
                        )
                    else:
                        # Track not found in playlist, play standalone
                        self._sp.start_playback(device_id=device_id, uris=[uri])
                else:
                    self._sp.start_playback(device_id=device_id, uris=[uri])
            else:
                # Album or playlist
                self._sp.start_playback(device_id=device_id, context_uri=uri)

        self._run_async(do_play, error_msg="Play URI failed")

    # ==================== Device Management ====================

    @Slot()
    def refresh_devices(self):
        """Refresh list of available devices (public slot)"""
        self._refresh_devices()

    def _refresh_devices(self):
        """Schedule async refresh of available Spotify Connect devices"""
        if not self._sp:
            return

        self.statusProgress.emit("[INFO] Scanning for Spotify devices...")

        def fetch():
            try:
                result = self._sp.devices()
                return result.get('devices', [])
            except Exception as e:
                logger.error(f"Device refresh error: {e}")
                self.errorOccurred.emit(f"Could not load Spotify devices: {e}")
                return []

        def on_done(future):
            if not self._sp:  # Skip if shutting down
                return
            try:
                devices = future.result()
                self._devicesReady.emit(devices)
            except Exception as e:
                logger.error(f"Device refresh callback error: {e}", exc_info=True)

        self._executor.submit(fetch).add_done_callback(on_done)

    def _handle_devices_result(self, devices):
        """Handle devices result on main thread"""
        self._devices = [{
            'id': d['id'],
            'name': d['name'],
            'type': d['type'],
            'is_active': d['is_active'],
            'volume': d.get('volume_percent', 0)
        } for d in devices]

        # Find active device
        for d in self._devices:
            if d['is_active']:
                self._active_device_id = d['id']
                self.activeDeviceChanged.emit(d['name'])
                break

        self.devicesChanged.emit(self._devices)

        if len(self._devices) == 0:
            self.statusProgress.emit("[WARN] No devices found - open Spotify on a device")
        else:
            self.statusProgress.emit(f"[FOUND] {len(self._devices)} device(s) available")

    @Slot(str)
    def set_active_device(self, device_id):
        """Transfer playback to a specific device"""
        if not self._sp:
            return
        try:
            self._sp.transfer_playback(device_id, force_play=False)
            self._active_device_id = device_id

            # Find device name
            for d in self._devices:
                if d['id'] == device_id:
                    self.activeDeviceChanged.emit(d['name'])
                    break
        except Exception as e:
            self.errorOccurred.emit(f"Device transfer failed: {str(e)}")

    @Slot(result=list)
    def get_devices(self):
        """Get list of available devices"""
        return self._devices

    # ==================== Playlist Management ====================

    def _refresh_playlists(self):
        """Schedule async refresh of user playlists"""
        if not self._sp:
            return

        self.statusProgress.emit("[INFO] Loading Spotify playlists...")

        def fetch():
            try:
                results = self._sp.current_user_playlists(limit=50)
                return [{
                    'id': p['id'],
                    'name': p['name'],
                    'uri': p['uri'],
                    'track_count': p['tracks']['total'],
                    'image': p['images'][0]['url'] if p['images'] else ''
                } for p in results.get('items', [])]
            except Exception as e:
                logger.error(f"Playlist refresh error: {e}")
                self.errorOccurred.emit(f"Could not load Spotify playlists: {e}")
                return []

        def on_done(future):
            if not self._sp:  # Skip if shutting down
                return
            try:
                playlists = future.result()
                self._playlistsReady.emit(playlists)
            except Exception as e:
                logger.error(f"Playlist refresh callback error: {e}", exc_info=True)

        self._executor.submit(fetch).add_done_callback(on_done)

    def _handle_playlists_result(self, playlists):
        """Handle playlists result on main thread"""
        self._playlists = playlists
        self.playlistsChanged.emit(self._playlists)
        self.statusProgress.emit(f"[FOUND] {len(self._playlists)} playlist(s) loaded")

    @Slot(result=list)
    def get_playlists(self):
        """Get list of user playlists"""
        return self._playlists

    @Slot(str, result=list)
    def get_playlist_tracks(self, playlist_id):
        """Get tracks from a playlist"""
        if not self._sp:
            return []
        try:
            results = self._sp.playlist_tracks(playlist_id, limit=100)

            tracks = []
            for item in results.get('items', []):
                track = item.get('track')
                if track:
                    tracks.append({
                        'id': track['id'],
                        'name': track['name'],
                        'uri': track['uri'],
                        'artist': ', '.join([a['name'] for a in track['artists']]),
                        'album': track['album']['name'],
                        'duration_ms': track['duration_ms'],
                        'image': track['album']['images'][0]['url'] if track['album']['images'] else ''
                    })
            return tracks

        except Exception as e:
            logger.error(f"Get playlist tracks error: {e}")
            return []

    @Slot(str)
    def select_spotify_playlist(self, playlist_id):
        """Select a Spotify playlist and load its tracks"""
        logger.debug(f"select_spotify_playlist called with ID: {playlist_id}")
        if not self._sp:
            logger.warning("Spotify client not connected")
            return

        # Find playlist name from stored playlists
        playlist_name = ""
        for p in self._playlists:
            if p['id'] == playlist_id:
                playlist_name = p['name']
                break

        logger.debug(f"Found playlist name: {playlist_name}")

        # Load tracks
        self._spotify_tracks = self.get_playlist_tracks(playlist_id)
        self._current_spotify_playlist_id = playlist_id
        self._current_spotify_playlist_name = playlist_name

        logger.debug(f"Loaded {len(self._spotify_tracks)} tracks")
        if self._spotify_tracks:
            logger.debug(f"First track: {self._spotify_tracks[0]}")

        # Emit signals
        self.spotifyTracksChanged.emit(self._spotify_tracks)
        self.currentSpotifyPlaylistChanged.emit(playlist_name)

    @Slot(result=list)
    def get_spotify_playlist_names(self):
        """Return list of playlist names"""
        return [p['name'] for p in self._playlists]

    @Slot(result=list)
    def get_spotify_tracks(self):
        """Return currently loaded Spotify playlist tracks"""
        return self._spotify_tracks

    @Slot(result=str)
    def get_current_spotify_playlist_name(self):
        """Return current Spotify playlist name"""
        return self._current_spotify_playlist_name

    @Slot(str, result=str)
    def get_spotify_track_artist(self, track_name):
        """Get artist for a track by name"""
        for track in self._spotify_tracks:
            if track['name'] == track_name:
                return track.get('artist', 'Unknown Artist')
        return 'Unknown Artist'

    @Slot(str, result=str)
    def get_spotify_track_album(self, track_name):
        """Get album for a track by name"""
        for track in self._spotify_tracks:
            if track['name'] == track_name:
                return track.get('album', 'Unknown Album')
        return 'Unknown Album'

    @Slot(str, result=str)
    def get_spotify_track_image(self, track_name):
        """Get album art URL for a track by name"""
        for track in self._spotify_tracks:
            if track['name'] == track_name:
                return track.get('image', '')
        return ''

    @Slot(str, result=str)
    def get_spotify_track_duration_formatted(self, track_name):
        """Get formatted duration (MM:SS) for a track by name"""
        for track in self._spotify_tracks:
            if track['name'] == track_name:
                duration_ms = track.get('duration_ms', 0)
                minutes = duration_ms // 60000
                seconds = (duration_ms % 60000) // 1000
                return f"{minutes}:{seconds:02d}"
        return "0:00"

    @Slot(str, result=str)
    def get_spotify_track_uri(self, track_name):
        """Get Spotify URI for a track by name"""
        for track in self._spotify_tracks:
            if track['name'] == track_name:
                return track.get('uri', '')
        return ''

    @Slot(str, result=str)
    def get_spotify_playlist_id(self, playlist_name):
        """Get playlist ID from playlist name"""
        for p in self._playlists:
            if p['name'] == playlist_name:
                return p['id']
        return ''

    @Slot(result=str)
    def get_current_spotify_playlist_id(self):
        """Get the currently selected Spotify playlist ID"""
        return self._current_spotify_playlist_id

    @Slot(result=str)
    def get_current_spotify_playlist_name(self):
        """Get the currently selected Spotify playlist name"""
        return self._current_spotify_playlist_name

    @Slot(result=bool)
    def has_spotify_playlist_loaded(self):
        """Check if a Spotify playlist is currently loaded"""
        return bool(self._current_spotify_playlist_id and self._spotify_tracks)

    # ==================== Playback State Polling ====================

    def _poll_playback_state(self):
        """Schedule async poll of playback state from Spotify"""
        if not self._sp or self._poll_in_progress:
            return

        self._poll_in_progress = True

        def fetch():
            try:
                result = self._sp.current_playback()
                self._consecutive_playback_errors = 0
                return result
            except Exception as e:
                # Background poll: log at debug to avoid filling logs on dropped
                # network, and rate-limit user-facing error emission.
                self._consecutive_playback_errors += 1
                logger.debug(
                    f"Playback poll error ({self._consecutive_playback_errors} consecutive): {e}"
                )
                if self._consecutive_playback_errors >= self.POLL_ERROR_THRESHOLD:
                    now = time.time()
                    if now - self._last_playback_error_ts >= self.POLL_ERROR_EMIT_INTERVAL:
                        self._last_playback_error_ts = now
                        logger.warning(
                            f"Spotify playback poll failed {self._consecutive_playback_errors} "
                            f"times in a row: {e}"
                        )
                        self.errorOccurred.emit(f"Spotify connection lost: {e}")
                return None

        def on_done(future):
            # Skip if shutting down
            if not self._sp:
                self._poll_in_progress = False
                return
            try:
                playback = future.result()
                # Emit signal to handle result on main thread
                self._playbackStateReady.emit(playback)
            except Exception as e:
                logger.error(f"Playback poll callback error: {e}", exc_info=True)
            finally:
                self._poll_in_progress = False

        future = self._executor.submit(fetch)
        future.add_done_callback(on_done)

    def _handle_playback_state(self, playback):
        """Handle playback state on main thread (called via signal)"""
        if not playback:
            # No active playback - slow down polling
            if self._poll_timer.interval() != 10000:
                self._poll_timer.setInterval(10000)  # 10 seconds when idle
            return

        # Get play state
        is_playing = playback.get('is_playing', False)

        # Adjust poll rate based on playback state
        if is_playing:
            if self._poll_timer.interval() != 3000:
                self._poll_timer.setInterval(3000)  # 3 seconds when playing
        else:
            if self._poll_timer.interval() != 5000:
                self._poll_timer.setInterval(5000)  # 5 seconds when paused

        # Only emit play state if changed
        if is_playing != self._is_playing:
            self._is_playing = is_playing
            self.playStateChanged.emit(is_playing)

        # Check shuffle state - only emit if changed
        shuffle_state = playback.get('shuffle_state', False)
        if shuffle_state != self._is_shuffled:
            self._is_shuffled = shuffle_state
            self.shuffleStateChanged.emit(shuffle_state)

        # Check volume from device - only emit if changed
        device = playback.get('device')
        if device:
            volume = device.get('volume_percent', 100)
            if volume != self._current_volume:
                self._current_volume = volume
                self.volumeChanged.emit(volume)

        # Update position and timestamp for interpolation
        progress_ms = playback.get('progress_ms', 0)
        self._last_known_position_ms = progress_ms
        self._last_poll_timestamp = time.time()
        self._current_position_ms = progress_ms
        self.positionChanged.emit(progress_ms)

        # Check if track changed
        track = playback.get('item')
        if track:
            track_id = track.get('id')
            if track_id != self._current_track.get('id'):
                # Save current as previous before overwriting
                if self._current_track.get('name'):
                    self._previous_track = {
                        'name': self._current_track.get('name', ''),
                        'artist': self._current_track.get('artist', ''),
                        'album': self._current_track.get('album', ''),
                        'image': self._current_track.get('image', '')
                    }
                # Track changed
                self._current_track = {
                    'id': track_id,
                    'name': track.get('name', ''),
                    'artist': ', '.join([a['name'] for a in track.get('artists', [])]),
                    'album': track.get('album', {}).get('name', ''),
                    'duration_ms': track.get('duration_ms', 0),
                    'image': ''
                }

                # Get album art
                album = track.get('album', {})
                images = album.get('images', [])
                if images:
                    self._current_track['image'] = images[0]['url']

                # Emit signals
                self.currentTrackChanged.emit(
                    self._current_track['name'],
                    self._current_track['artist'],
                    self._current_track['album'],
                    self._current_track['image']
                )
                self.durationChanged.emit(self._current_track['duration_ms'])

                # Extract album art colors if Album Art Capture is active
                if self._current_track['image']:
                    self._on_track_changed_for_theme(self._current_track['image'])

                # Fetch queue for side-card art when no playlist is loaded
                if not self._spotify_tracks:
                    self._fetch_queue()

    def _fetch_queue(self):
        """Async fetch Spotify playback queue for side-card neighbor art."""
        def fetch():
            try:
                return self._sp.queue()
            except Exception as e:
                logger.debug(f"Spotify queue fetch failed: {e}")
                return None

        def on_done(future):
            if not self._sp:
                return
            try:
                result = future.result()
                if result:
                    self._queueReady.emit(result.get('queue', []))
            except Exception as e:
                logger.debug(f"Spotify queue callback error: {e}")

        self._executor.submit(fetch).add_done_callback(on_done)

    def _handle_queue_result(self, queue_tracks):
        """Cache queue tracks on main thread for side-card art lookups."""
        self._cached_queue = []
        for track in queue_tracks[:5]:  # Only need a few
            images = track.get('album', {}).get('images', [])
            self._cached_queue.append({
                'name': track.get('name', ''),
                'artist': ', '.join([a['name'] for a in track.get('artists', [])]),
                'album': track.get('album', {}).get('name', ''),
                'image': images[0]['url'] if images else ''
            })
        self.queueUpdated.emit()

    def _interpolate_position(self):
        """Interpolate position between API polls for smooth UI updates"""
        if not self._is_playing or self._last_poll_timestamp == 0:
            return

        # Calculate elapsed time since last poll
        elapsed_ms = int((time.time() - self._last_poll_timestamp) * 1000)

        # Estimate current position
        estimated_position = self._last_known_position_ms + elapsed_ms

        # Don't exceed track duration
        duration = self._current_track.get('duration_ms', 0)
        if duration > 0 and estimated_position > duration:
            estimated_position = duration

        # Only emit if position changed significantly (avoid spam)
        if abs(estimated_position - self._current_position_ms) >= 200:
            self._current_position_ms = estimated_position
            self.positionChanged.emit(estimated_position)

    @Slot(result=str)
    def get_current_track_name(self):
        """Get current track name"""
        return self._current_track.get('name', '')

    @Slot(result=str)
    def get_current_artist(self):
        """Get current artist"""
        return self._current_track.get('artist', '')

    @Slot(result=str)
    def get_current_album(self):
        """Get current album"""
        return self._current_track.get('album', '')

    @Slot(result=str)
    def get_current_album_art(self):
        """Get current album art URL"""
        return self._current_track.get('image', '')

    @Slot(result=str)
    def get_next_track_info(self):
        """Peek at the next track. Uses loaded playlist or Spotify queue as fallback."""
        if self._spotify_tracks and self._current_track.get('name'):
            current_name = self._current_track['name']
            for i, track in enumerate(self._spotify_tracks):
                if track.get('name') == current_name:
                    next_index = (i + 1) % len(self._spotify_tracks)
                    t = self._spotify_tracks[next_index]
                    return json.dumps({
                        "name": t.get('name', ''),
                        "artist": t.get('artist', ''),
                        "album": t.get('album', ''),
                        "image": t.get('image', '')
                    })
        # Fallback: use cached queue (index 0 = next up)
        if self._cached_queue:
            return json.dumps(self._cached_queue[0])
        return ""

    @Slot(result=str)
    def get_previous_track_info(self):
        """Peek at the previous track. Uses loaded playlist or last played track."""
        if self._spotify_tracks and self._current_track.get('name'):
            current_name = self._current_track['name']
            for i, track in enumerate(self._spotify_tracks):
                if track.get('name') == current_name:
                    prev_index = (i - 1) % len(self._spotify_tracks)
                    t = self._spotify_tracks[prev_index]
                    return json.dumps({
                        "name": t.get('name', ''),
                        "artist": t.get('artist', ''),
                        "album": t.get('album', ''),
                        "image": t.get('image', '')
                    })
        # Fallback: use the last played track
        if self._previous_track.get('name'):
            return json.dumps(self._previous_track)
        return ""

    # ==================== Compatibility Methods (match MediaManager API) ====================

    @Slot(result=bool)
    def is_playing(self):
        """Check if currently playing"""
        return self._is_playing

    @Slot(result=int)
    def get_duration(self):
        """Get current track duration in milliseconds"""
        return self._current_track.get('duration_ms', 0)

    @Slot(result=int)
    def get_position(self):
        """Get current playback position in milliseconds"""
        return self._current_position_ms

    @Slot(result=int)
    def get_volume(self):
        """Get current volume (0-100)"""
        return self._current_volume

    @Slot(result=str)
    def get_current_file(self):
        """Get current track name (for compatibility with MediaManager)"""
        return self._current_track.get('name', '')

    # ==================== Album Art Capture Methods ====================

    def _on_theme_changed(self, theme):
        """Handle theme change - track if Album Art Capture is active"""
        self._album_art_capture_active = (theme == "Album Art Capture")
        logger.debug(f"Theme changed to: {theme}, Album Art Capture active: {self._album_art_capture_active}")

        if self._album_art_capture_active:
            # Extract colors from current album art
            image_url = self.get_current_album_art()
            if image_url:
                self._extract_colors_from_url(image_url)

    def _on_track_changed_for_theme(self, image_url):
        """Called when track changes - extract colors if Album Art Capture is active"""
        if self._album_art_capture_active and image_url and image_url != self._last_extracted_image_url:
            logger.debug(f"Track changed, extracting colors for Album Art Capture")
            self._extract_colors_from_url(image_url)

    def _extract_colors_from_url(self, image_url):
        """Extract colors from a Spotify album art URL (async, off main thread)"""
        if not image_url:
            return

        # Avoid re-extracting same image
        if image_url == self._last_extracted_image_url:
            return
        self._last_extracted_image_url = image_url

        logger.debug(f"Extracting colors from: {image_url}")

        def do_extract():
            import urllib.request
            import colorsys
            import numpy as np

            # Download image to temp file
            temp_dir = os.path.join(self.backend_dir, 'temp')
            os.makedirs(temp_dir, exist_ok=True)
            temp_path = os.path.join(temp_dir, 'spotify_album_art.jpg')

            urllib.request.urlretrieve(image_url, temp_path)

            img = QImage(temp_path)
            if img.isNull():
                return None
            img = img.convertToFormat(QImage.Format.Format_RGBA8888)
            img = img.scaled(100, 100, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)

            # Format_RGBA8888: stride = width*4, no row padding
            raw = bytes(img.constBits())
            pixels = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 4)[:, :3]

            colors = self._kmeans_colors(pixels, k=5)

            def color_vibrancy(rgb):
                r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
                h, s, v = colorsys.rgb_to_hsv(r, g, b)
                return s * v

            colors = sorted(colors, key=color_vibrancy, reverse=True)

            def luminance(rgb):
                r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
                return 0.299 * r + 0.587 * g + 0.114 * b

            avg_luminance = np.mean([luminance(p) for p in pixels[:1000]])
            is_dark_image = avg_luminance < 0.5

            return self._generate_theme_from_colors(colors, is_dark_image)

        def on_done(future):
            if not self._sp:
                return
            try:
                theme_json = future.result()
                if theme_json:
                    self._colorsReady.emit(theme_json)
            except Exception as e:
                logger.error(f"Error extracting colors: {e}", exc_info=True)

        self._executor.submit(do_extract).add_done_callback(on_done)

    def _handle_colors_result(self, theme_json):
        """Apply extracted album art colors on the main thread."""
        logger.debug(f"Generated theme, length: {len(theme_json)}")
        if self._settings_manager:
            self._settings_manager.set_album_art_colors(theme_json)
        self.albumColorsExtracted.emit(theme_json)

    def _kmeans_colors(self, pixels, k=5, max_iterations=10):
        """Simple k-means clustering to find dominant colors"""
        import numpy as np

        rng = np.random.default_rng(42)  # local generator, do not seed the global numpy RNG
        indices = rng.choice(len(pixels), k, replace=False)
        centroids = pixels[indices].astype(float)

        for _ in range(max_iterations):
            distances = np.sqrt(((pixels[:, np.newaxis] - centroids) ** 2).sum(axis=2))
            labels = np.argmin(distances, axis=1)
            new_centroids = np.array([
                pixels[labels == i].mean(axis=0) if np.sum(labels == i) > 0 else centroids[i]
                for i in range(k)
            ])
            if np.allclose(centroids, new_centroids):
                break
            centroids = new_centroids

        return centroids.astype(int).tolist()

    def _generate_theme_from_colors(self, colors, is_dark):
        """Generate a complete theme palette from extracted colors"""
        import json
        import colorsys

        def rgb_to_hex(rgb):
            return "#{:02x}{:02x}{:02x}".format(int(rgb[0]), int(rgb[1]), int(rgb[2]))

        def adjust_brightness(rgb, factor):
            r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            v = max(0, min(1, v * factor))
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            return [int(r * 255), int(g * 255), int(b * 255)]

        def adjust_saturation(rgb, factor):
            r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            s = max(0, min(1, s * factor))
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            return [int(r * 255), int(g * 255), int(b * 255)]

        def get_luminance(rgb):
            def channel_luminance(c):
                c = c / 255.0
                return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
            return 0.2126 * channel_luminance(rgb[0]) + 0.7152 * channel_luminance(rgb[1]) + 0.0722 * channel_luminance(rgb[2])

        def get_contrast_ratio(rgb1, rgb2):
            l1, l2 = get_luminance(rgb1), get_luminance(rgb2)
            lighter, darker = max(l1, l2), min(l1, l2)
            return (lighter + 0.05) / (darker + 0.05)

        def get_readable_text_color(background_rgb):
            lum = get_luminance(background_rgb)
            return [245, 245, 245] if lum < 0.3 else [25, 25, 25]

        def get_secondary_text_color(background_rgb):
            lum = get_luminance(background_rgb)
            return [210, 210, 210] if lum < 0.3 else [60, 60, 60]

        def cap_brightness(rgb, max_brightness=0.85):
            """Cap the brightness of a color to prevent overly bright/washed out colors"""
            r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            if v > max_brightness:
                v = max_brightness
                s = min(1.0, s * 1.1)  # Boost saturation when capping brightness
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            return [int(r * 255), int(g * 255), int(b * 255)]

        def ensure_icon_contrast(icon_rgb, background_rgb, min_contrast=3.0):
            contrast = get_contrast_ratio(icon_rgb, background_rgb)
            if contrast >= min_contrast:
                return cap_brightness(icon_rgb)
            bg_lum = get_luminance(background_rgb)
            if bg_lum < 0.5:
                for factor in [1.3, 1.5, 1.8, 2.0, 2.5, 3.0]:
                    adjusted = adjust_brightness(icon_rgb, factor)
                    if get_contrast_ratio(adjusted, background_rgb) >= min_contrast:
                        return cap_brightness(adjusted)
                return [200, 180, 130]  # Fallback muted gold
            else:
                for factor in [0.7, 0.5, 0.4, 0.3, 0.2]:
                    adjusted = adjust_brightness(icon_rgb, factor)
                    if get_contrast_ratio(adjusted, background_rgb) >= min_contrast:
                        return adjusted
                return [50, 40, 30]

        # Get colors from palette
        primary_rgb = colors[0]
        secondary_rgb = colors[1] if len(colors) > 1 else colors[0]
        tertiary_rgb = colors[2] if len(colors) > 2 else secondary_rgb
        quaternary_rgb = colors[3] if len(colors) > 3 else tertiary_rgb

        # Create accent colors
        accent = adjust_brightness(adjust_saturation(primary_rgb, 1.4), 1.2)
        accent2 = adjust_brightness(adjust_saturation(secondary_rgb, 1.3), 1.15)
        accent3 = adjust_brightness(adjust_saturation(tertiary_rgb, 1.25), 1.1)
        accent4 = adjust_brightness(adjust_saturation(quaternary_rgb, 1.2), 1.05)
        nav_color = adjust_brightness(accent, 0.7)
        nav_color2 = adjust_brightness(accent2, 0.75)

        if is_dark:
            base = adjust_brightness(primary_rgb, 0.15)
            base_alt = adjust_brightness(primary_rgb, 0.22)
            hover = adjust_brightness(primary_rgb, 0.30)
            paused = adjust_brightness(primary_rgb, 0.25)
            playing = adjust_brightness(primary_rgb, 0.35)
        else:
            base = adjust_saturation(adjust_brightness(primary_rgb, 2.5), 0.3)
            base_alt = adjust_brightness(base, 0.92)
            hover = adjust_brightness(base, 0.88)
            paused = adjust_brightness(base, 0.85)
            playing = adjust_brightness(base, 0.80)

        text_primary = get_readable_text_color(base)
        text_secondary = get_secondary_text_color(base)

        # Ensure icon visibility
        accent_visible = ensure_icon_contrast(accent, base)
        accent2_visible = ensure_icon_contrast(accent2, base)
        accent3_visible = ensure_icon_contrast(accent3, base)
        accent4_visible = ensure_icon_contrast(accent4, base)
        nav_color_visible = ensure_icon_contrast(nav_color, base)
        nav_color2_visible = ensure_icon_contrast(nav_color2, base)

        theme = {
            "base": rgb_to_hex(base),
            "baseAlt": rgb_to_hex(base_alt),
            "accent": rgb_to_hex(accent_visible),
            "text": {
                "primary": rgb_to_hex(text_primary),
                "secondary": rgb_to_hex(text_secondary)
            },
            "states": {
                "hover": rgb_to_hex(hover),
                "paused": rgb_to_hex(paused),
                "playing": rgb_to_hex(playing)
            },
            "sliders": {
                "volume": rgb_to_hex(accent_visible),
                "media": rgb_to_hex(accent2_visible),
                "settings": rgb_to_hex(accent3_visible)
            },
            "bottombar": {
                "previous": rgb_to_hex(nav_color_visible),
                "play": rgb_to_hex(accent_visible),
                "pause": rgb_to_hex(accent_visible),
                "next": rgb_to_hex(nav_color_visible),
                "volume": rgb_to_hex(accent2_visible),
                "shuffle": rgb_to_hex(accent3_visible),
                "toggleShade": rgb_to_hex(hover),
                "homeButton": rgb_to_hex(accent_visible),
                "obdButton": rgb_to_hex(accent4_visible),
                "mediaButton": rgb_to_hex(accent2_visible),
                "settingsButton": rgb_to_hex(accent3_visible),
                "sensorButton": rgb_to_hex(accent_visible),
                "androidAutoButton": rgb_to_hex(nav_color2_visible),
                "phoneMirrorButton": rgb_to_hex(accent4_visible)
            },
            "mediaroom": {
                "previous": rgb_to_hex(nav_color_visible),
                "play": rgb_to_hex(ensure_icon_contrast(adjust_brightness(accent, 1.1), base)),
                "pause": rgb_to_hex(ensure_icon_contrast(adjust_brightness(accent, 1.1), base)),
                "next": rgb_to_hex(nav_color_visible),
                "left": rgb_to_hex(accent4_visible),
                "right": rgb_to_hex(accent4_visible),
                "shuffle": rgb_to_hex(accent3_visible),
                "toggleShade": rgb_to_hex(paused)
            },
            "mainmenu": {
                "mediaContainer": rgb_to_hex(adjust_brightness(accent2_visible, 0.6))
            },
            "obd": {
                "boxBackground": rgb_to_hex(base_alt),
                "barColor": rgb_to_hex(ensure_icon_contrast(accent4, base_alt)),
                "labelColor": rgb_to_hex(get_secondary_text_color(base_alt)),
                "valueColor": rgb_to_hex(get_readable_text_color(base_alt))
            }
        }

        return json.dumps(theme)
