from PySide6.QtCore import QObject, Signal, Slot, Property, QTimer, QUrl, Qt
from PySide6.QtGui import QImage
from PySide6.QtMultimedia import QMediaPlayer, QAudioOutput
from mutagen.mp3 import MP3
from mutagen.id3 import ID3, TIT2, TPE1, TALB
import os
import json
import random
import re
import hashlib
import colorsys
import shutil
import threading

from backend.logging_config import get_logger
from backend.settings_manager import get_app_data_dir

AUDIO_EXTENSIONS = ('.mp3', '.m4a', '.flac', '.ogg', '.opus', '.wav')
logger = get_logger(__name__)


def is_safe_path(base_path, target_path):
    """
    Validate that target_path is within base_path (prevents path traversal attacks).
    Returns True if the path is safe, False otherwise.
    """
    # Normalize and resolve both paths to absolute paths
    base = os.path.normpath(os.path.realpath(base_path))
    target = os.path.normpath(os.path.realpath(target_path))
    # Check if target starts with base (is contained within)
    return target.startswith(base + os.sep) or target == base


def sanitize_metadata(text, max_length=200):
    """
    Sanitize metadata strings from ID3 tags to prevent display issues.
    - Limits length to prevent UI overflow
    - Removes control characters
    - Strips leading/trailing whitespace
    """
    if not isinstance(text, str):
        text = str(text) if text else ""

    # Remove control characters (except newline/tab which we'll replace with space)
    sanitized = ''.join(
        char if (char.isprintable() or char in ' ') else ' '
        for char in text
    )

    # Collapse multiple spaces and strip
    sanitized = ' '.join(sanitized.split())

    # Truncate to max length
    if len(sanitized) > max_length:
        sanitized = sanitized[:max_length - 3] + "..."

    return sanitized


class MediaManager(QObject):
    playbackStateChanged = Signal(int)
    playStateChanged = Signal(bool)
    currentMediaChanged = Signal(str)
    mediaListChanged = Signal(list)
    muteChanged = Signal(bool)
    durationChanged = Signal(int)
    positionChanged = Signal(int)
    metadataChanged = Signal(str, str, str)
    durationFormatChanged = Signal(str)
    volumeChanged = Signal(float)
    shuffleStateChanged = Signal(bool)
    totalDurationChanged = Signal(str)  # Formatted duration string
    albumCountChanged = Signal(int)     # Number of unique albums
    artistCountChanged = Signal(int)    # Number of unique artists
    playlistsChanged = Signal()         # When playlist list updates
    currentPlaylistChanged = Signal(str)  # When active playlist changes
    scanProgress = Signal(str)           # Terminal-style feedback during scan
    albumColorsExtracted = Signal(str)    # JSON string with extracted album art colors
    
    
    def __init__(self):
        super().__init__()
        self._player = QMediaPlayer()
        self._audio_output = QAudioOutput()
        self._player.setAudioOutput(self._audio_output)
        
        # Set default volume
        self._audio_output.setVolume(0.5)
        
        # Set up media directory
        self.backend_dir = os.path.dirname(os.path.abspath(__file__))
        self.default_media_dir = os.path.join(self.backend_dir, 'media')
        self.media_dir = self.default_media_dir
        self.temp_dir = os.path.join(self.backend_dir, 'temp')
        
        # Media player configuration
        self._player.setProperty("probe-size", 10000000)  # 10MB
        self._player.setProperty("analyzeduration", 5000000)  # 5 seconds
        
        # Playback state
        self._current_index = 0
        self._is_muted = False
        self._previous_volume = 0.5
        self._is_playing = False
        self._is_paused = True
        self._shuffle = False
        self._auto_play = False  # Set to False to prevent auto-play
        
        # Playlist management
        self._original_files = []
        self._current_playlist = []
        
        # Caching
        self._album_art_cache = {}  # Album ID to filename mapping
        self._metadata_cache = {}  # Filename to metadata mapping
        self._max_cache_files = 500  # Maximum number of cached files
        self._metadata_cache_max = 1000  # Maximum metadata cache entries
        self._access_count = {}  # Track album art access for LRU caching
        
        # Album Art Capture theme active state
        self._album_art_capture_active = False

        # Statistics cache
        self._stats_cache = {
            "total_duration_ms": 0,
            "total_duration_formatted": "0:00:00",
            "album_count": 0,
            "artist_count": 0,
            "is_valid": False
        }

        # Playlist management
        self._library_root = ""                   # Main source folder
        self._playlists = {}                      # Dict: name -> {path, files, song_count}
        self._playlist_names = []                 # List of playlist names
        self._current_playlist_name = ""          # Active playlist

        # All Music playlist support - maps filename to full path for multi-folder playlist
        self._all_music_file_paths = {}           # Dict: filename -> full directory path
        self._is_all_music_active = False         # True when "All Music" playlist is selected

        # Mute toggle guard - prevents rapid toggling from corrupting state
        self._mute_toggle_locked = False

        # Connect signals
        self._player.durationChanged.connect(self.durationChanged.emit)
        self._player.positionChanged.connect(self.positionChanged.emit)
        self._player.mediaStatusChanged.connect(self._handle_media_status)
        self._player.errorOccurred.connect(self._handle_player_error)

        # Initialize position timer
        self._position_timer = QTimer()
        self._position_timer.setInterval(100)  # Update every 100ms
        self._position_timer.timeout.connect(self._update_position)
        self._position_timer.start()
        
        # Debounce timer for saving playback state — avoids disk writes on every
        # rapid track change (spam next/prev).  Saves once 1s after the last change.
        self._save_state_timer = QTimer()
        self._save_state_timer.setInterval(1000)
        self._save_state_timer.setSingleShot(True)
        self._save_state_timer.timeout.connect(self._save_playback_state_now)

        # Pre-cache timer — after a track change, pre-cache metadata + album art
        # for neighboring tracks in a background thread so the NEXT click is instant.
        self._precache_timer = QTimer()
        self._precache_timer.setInterval(80)
        self._precache_timer.setSingleShot(True)
        self._precache_timer.timeout.connect(self._precache_neighbors_start)

        # Create media and temp directories if they don't exist
        self._ensure_directories()

        # Clear temp files on startup
        self._clear_temp_files()

        self._settings_manager = None

        # Display name mapping (filename -> cleaned display name)
        self._display_names = {}
        self._display_names_path = os.path.join(get_app_data_dir(), 'display_names.json')
        self._load_display_names()

        # Connect our own signal for Album Art Capture theme updates
        self.currentMediaChanged.connect(self._on_media_changed_for_theme)

    def __del__(self):
        """Clean up resources on destruction"""
        try:
            # Flush any pending debounced save immediately on shutdown
            if self._save_state_timer and self._save_state_timer.isActive():
                self._save_state_timer.stop()
                self._save_playback_state_now()
            self._clear_temp_files()
            if self._player:
                self._player.stop()
            if self._position_timer:
                self._position_timer.stop()
            if self._precache_timer:
                self._precache_timer.stop()
        except Exception:
            pass  # Avoid errors during shutdown
    
    def _ensure_directories(self):
        """Ensure required directories exist"""
        try:
            if not os.path.exists(self.media_dir):
                os.makedirs(self.media_dir)
                logger.info(f"Created media directory at: {self.media_dir}")
            
            if not os.path.exists(self.temp_dir):
                os.makedirs(self.temp_dir)
                logger.info(f"Created temp directory at: {self.temp_dir}")
        except Exception as e:
            logger.error(f"Error creating directories: {e}")
            
    def _update_position(self):
        """Update position for UI slider"""
        if self._player.playbackState() == QMediaPlayer.PlayingState:
            self.positionChanged.emit(self._player.position())
            
    def _handle_media_status(self, status):
        """Handle media status changes"""
        try:
            if status == QMediaPlayer.MediaStatus.EndOfMedia:
                logger.info("Song ended, playing next track")
                self.next_track()
            elif status == QMediaPlayer.MediaStatus.StalledMedia:
                logger.warning("Media stalled — attempting recovery")
                self._attempt_playback_recovery()
            elif status == QMediaPlayer.MediaStatus.InvalidMedia:
                logger.error("Invalid media detected — attempting recovery")
                self._attempt_playback_recovery()
        except Exception as e:
            logger.error(f"Media status handling error: {e}")

    def _handle_player_error(self, error):
        """Handle player errors with automatic recovery"""
        error_msg = self._player.errorString()
        logger.error(f"Player error ({error}): {error_msg}")
        self._attempt_playback_recovery()

    def _attempt_playback_recovery(self):
        """Re-seat the current source and resume playback"""
        try:
            current_file = self.get_current_file()
            if not current_file:
                logger.warning("Recovery failed — no current file")
                return

            position = self._player.position()
            was_playing = self._is_playing

            file_path = self._get_file_path(current_file)
            if not os.path.exists(file_path):
                logger.warning(f"Recovery failed — file missing: {file_path}")
                return

            logger.info(f"Recovering playback for: {current_file} at position {position}ms")

            # Re-set source and restore position
            self._player.setSource(QUrl.fromLocalFile(file_path))
            if position > 0:
                self._player.setPosition(position)

            if was_playing:
                self._player.play()
                self._is_playing = True
                self._is_paused = False

            # Restore volume/mute state after recovery
            if self._is_muted:
                self._audio_output.setVolume(0.0)

            self.playStateChanged.emit(self._is_playing)
            logger.info("Playback recovery successful")
        except Exception as e:
            logger.error(f"Playback recovery failed: {e}")

    def _cache_metadata(self, filename):
        """Cache metadata for a file to reduce disk operations.

        Also extracts album art from the same ID3 read so that a subsequent
        get_album_art() call is an instant cache hit (avoids a second file open).
        """
        if filename in self._metadata_cache:
            return

        try:
            # Use helper to get correct file path (handles All Music multi-folder)
            file_path = self._get_file_path(filename)
            display_name = self._get_original_filename(filename)

            # Manage cache size
            if len(self._metadata_cache) >= self._metadata_cache_max:
                # Remove oldest entry
                self._metadata_cache.pop(next(iter(self._metadata_cache)))

            # Read metadata — handle MP3 and M4A/MP4 formats
            ext = os.path.splitext(file_path)[1].lower()
            base_name = os.path.splitext(display_name)[0]

            if ext == '.mp3':
                audio = ID3(file_path)
                mp3 = MP3(file_path)
                self._metadata_cache[filename] = {
                    "artist": self._extract_id3_text(audio.get('TPE1'), "Unknown Artist"),
                    "album": self._extract_id3_text(audio.get('TALB'), "Unknown Album"),
                    "title": self._extract_id3_text(audio.get('TIT2'), base_name),
                    "duration": int(mp3.info.length)
                }
                self._cache_album_art_from_id3(filename, audio)
            elif ext in ('.m4a', '.mp4', '.aac'):
                from mutagen.mp4 import MP4 as M4A, MP4Cover
                m4a = M4A(file_path)
                self._metadata_cache[filename] = {
                    "artist": (m4a.tags.get('\xa9ART', ['Unknown Artist'])[0] if m4a.tags else "Unknown Artist"),
                    "album": (m4a.tags.get('\xa9alb', ['Unknown Album'])[0] if m4a.tags else "Unknown Album"),
                    "title": (m4a.tags.get('\xa9nam', [base_name])[0] if m4a.tags else base_name),
                    "duration": int(m4a.info.length) if m4a.info else 0
                }
                # Cache album art from m4a covr tag
                covers = m4a.tags.get('covr', []) if m4a.tags else []
                if covers:
                    album_id = self._get_album_id(filename)
                    if album_id not in self._album_art_cache:
                        self._manage_cache(album_id)
                        cover = covers[0]
                        art_ext = 'png' if cover.imageformat == MP4Cover.FORMAT_PNG else 'jpg'
                        cache_hash = hashlib.sha256(f"{album_id}_0".encode('utf-8')).hexdigest()[:16]
                        temp_path = os.path.join(self.temp_dir, f'cover_{cache_hash}.{art_ext}')
                        if not os.path.exists(temp_path):
                            with open(temp_path, 'wb') as img_file:
                                img_file.write(bytes(cover))
                        self._album_art_cache[album_id] = QUrl.fromLocalFile(temp_path).toString()
            else:
                # Generic fallback using mutagen.File
                from mutagen import File as MutagenFile
                mf = MutagenFile(file_path)
                self._metadata_cache[filename] = {
                    "artist": "Unknown Artist",
                    "album": "Unknown Album",
                    "title": base_name,
                    "duration": int(mf.info.length) if mf and mf.info else 0
                }
        except Exception as e:
            logger.error(f"Metadata caching error for {filename}: {e}")
            # Set fallback values
            display_name = self._get_original_filename(filename)
            base_name = os.path.splitext(display_name)[0]
            self._metadata_cache[filename] = {
                "artist": "Unknown Artist",
                "album": "Unknown Album",
                "title": base_name,
                "duration": 0
            }
    
    def _extract_id3_text(self, tag, default=""):
        """Helper to safely extract and sanitize text from ID3 tags"""
        if tag is None:
            return sanitize_metadata(default)
        if isinstance(tag, str):
            return sanitize_metadata(tag)
        if hasattr(tag, 'text') and tag.text:
            return sanitize_metadata(tag.text[0])
        return sanitize_metadata(str(tag)) if tag else sanitize_metadata(default)

    def _cache_album_art_from_id3(self, filename, audio):
        """Extract and cache album art from an already-loaded ID3 object.

        Called by _cache_metadata() so a single file read populates both caches.
        get_album_art() then returns instantly from cache.
        """
        try:
            album_id = self._get_album_id(filename)
            if album_id in self._album_art_cache:
                return
            self._manage_cache(album_id)
            self._access_count[album_id] = self._access_count.get(album_id, 0) + 1
            for tag in audio.values():
                if tag.FrameID == 'APIC':
                    mime = tag.mime.lower()
                    if mime in ('image/jpeg', 'image/jpg'):
                        ext = 'jpg'
                    elif mime == 'image/png':
                        ext = 'png'
                    elif mime == 'image/gif':
                        ext = 'gif'
                    else:
                        ext = 'img'
                    cache_key = f"{album_id}_0"
                    cache_hash = hashlib.sha256(cache_key.encode('utf-8')).hexdigest()[:16]
                    temp_path = os.path.join(self.temp_dir, f'cover_{cache_hash}.{ext}')
                    if not os.path.exists(temp_path):
                        with open(temp_path, 'wb') as img_file:
                            img_file.write(tag.data)
                    url = QUrl.fromLocalFile(temp_path).toString()
                    self._album_art_cache[album_id] = url
                    return
        except Exception:
            pass  # non-critical — get_album_art() is the fallback

    def _emit_metadata(self, filename):
        """Emit metadata change signals"""
        if filename not in self._metadata_cache:
            self._cache_metadata(filename)
            
        meta = self._metadata_cache[filename]
        self.metadataChanged.emit(
            meta.get("title", filename.replace('.mp3', '')),
            meta.get("artist", "Unknown Artist"),
            meta.get("album", "Unknown Album")
        )
        
    def _get_album_id(self, filename):
        """Create a unique ID for album art caching"""
        try:
            if filename not in self._metadata_cache:
                self._cache_metadata(filename)

            meta = self._metadata_cache[filename]
            # When album is unknown (common for YouTube downloads), each track
            # likely has its own unique cover art, so include the filename
            # to avoid all "Unknown Album" tracks sharing one cached image.
            if meta['album'] == "Unknown Album":
                return f"{meta['album']}_{meta['artist']}_{filename}"
            return f"{meta['album']}_{meta['artist']}"
        except Exception as e:
            logger.error(f"Error getting album ID: {e}")
            # Use SHA256 for deterministic, collision-resistant fallback
            return hashlib.sha256(filename.encode('utf-8')).hexdigest()[:16]
        
    def _manage_cache(self, new_album_id):
        """More efficient cache management using LRU approach"""
        try:
            if len(self._album_art_cache) >= self._max_cache_files:
                # Find least recently accessed item, excluding the new one
                items = [(k, v) for k, v in self._access_count.items() if k != new_album_id]
                if not items:
                    return
                    
                # Find least used album art
                least_used = min(items, key=lambda x: x[1])[0]
                
                # Remove it from cache
                if least_used in self._album_art_cache:
                    file_path = self._album_art_cache[least_used].replace('file:///', '')
                    try:
                        if os.path.exists(file_path):
                            os.remove(file_path)
                    except Exception as e:
                        logger.warning(f"Warning: Could not remove file {file_path}: {e}")
                    finally:
                        del self._album_art_cache[least_used]
                        del self._access_count[least_used]
                        
                logger.info(f"Cache managed. New size: {len(self._album_art_cache)}")
        except Exception as e:
            logger.error(f"Cache management error")

    @Slot()
    def invalidate_stats_cache(self):
        """Mark the statistics cache as invalid to force recalculation"""
        self._stats_cache["is_valid"] = False  
                
    @Slot()
    def _clear_temp_files(self):
        """Improved temp file management with error handling"""
        if os.path.exists(self.temp_dir):
            for file in os.listdir(self.temp_dir):
                try:
                    file_path = os.path.join(self.temp_dir, file)
                    if os.path.isfile(file_path):
                        os.remove(file_path)
                except Exception as e:
                    logger.error(f"Error removing temp file {file}: {e}")
                    
        # Make sure directory exists
        try:
            if not os.path.exists(self.temp_dir):
                os.makedirs(self.temp_dir)
        except Exception as e:
            logger.error(f"Error creating temp directory: {e}")
                
    def _shuffle_playlist(self):
        """Helper method to create shuffled playlist from current playlist files"""
        files = self._get_current_playlist_files()
        if not files:
            # Fallback to directory scan if no playlist is selected
            files = self.get_media_files()
        if not files:
            return []

        shuffled = files.copy()
        random.shuffle(shuffled)
        return shuffled
                
    @Slot(result=list)
    def get_media_files(self, emit_signal=True):
        """Get list of available MP3 files"""
        mp3_files = []
        try:
            if os.path.exists(self.media_dir):
                for file in os.listdir(self.media_dir):
                    if file.lower().endswith(AUDIO_EXTENSIONS):
                        mp3_files.append(file)
                        
                # Only emit signal if requested
                if emit_signal:
                    self.mediaListChanged.emit(mp3_files)
                
        except Exception as e:
            logger.error(f"Error getting media files: {e}")
                
        return mp3_files
    
    @Slot(str, result=str)
    def get_formatted_duration(self, filename):
        """Get formatted duration string (MM:SS)"""
        try:
            if filename not in self._metadata_cache:
                self._cache_metadata(filename)
                
            duration_seconds = self._metadata_cache[filename]["duration"]
            minutes = duration_seconds // 60
            seconds = duration_seconds % 60
            formatted = f"{minutes}:{seconds:02d}"
            self.durationFormatChanged.emit(formatted)
            return formatted
        except Exception as e:
            logger.error(f"Error getting duration: {e}")
            return "0:00"
    
    @Slot(str, result=str)
    def get_band(self, filename):
        """Get artist name from metadata"""
        if filename not in self._metadata_cache:
            self._cache_metadata(filename)
        return self._metadata_cache[filename]["artist"]

    @Slot(str, result=str)
    def get_album(self, filename):
        """Get album name from metadata"""
        if filename not in self._metadata_cache:
            self._cache_metadata(filename)
        return self._metadata_cache[filename]["album"]

    @Slot(str, result=str)
    def get_album_art(self, filename):
        """Extract and cache album art. Returns file:// URL or empty string."""
        try:
            album_id = self._get_album_id(filename)
            
            # Update access count for LRU cache
            self._access_count[album_id] = self._access_count.get(album_id, 0) + 1

            # Return if already cached
            if album_id in self._album_art_cache:
                logger.info(f"Album art cache hit for {filename}: {self._album_art_cache[album_id][:80]}")
                return self._album_art_cache[album_id]
            
            # Manage cache BEFORE adding new entry
            self._manage_cache(album_id)

            # Extract and cache new album art - use helper for correct path
            file_path = self._get_file_path(filename)
            file_ext = os.path.splitext(file_path)[1].lower()

            found_apic = False
            apic_index = 0

            if file_ext in ('.m4a', '.mp4', '.aac'):
                # M4A/MP4: cover art stored as 'covr' tag
                from mutagen.mp4 import MP4 as M4A, MP4Cover
                m4a = M4A(file_path)
                covers = m4a.tags.get('covr', []) if m4a.tags else []
                for cover in covers:
                    found_apic = True
                    ext = 'png' if cover.imageformat == MP4Cover.FORMAT_PNG else 'jpg'
                    cache_key = f"{album_id}_{apic_index}"
                    cache_hash = hashlib.sha256(cache_key.encode('utf-8')).hexdigest()[:16]
                    temp_path = os.path.join(self.temp_dir, f'cover_{cache_hash}.{ext}')
                    if not os.path.exists(temp_path):
                        with open(temp_path, 'wb') as img_file:
                            img_file.write(bytes(cover))

                    if album_id not in self._album_art_cache:
                        url = QUrl.fromLocalFile(temp_path).toString()
                        self._album_art_cache[album_id] = url
                        return url
                    apic_index += 1
            else:
                # MP3: cover art stored as APIC ID3 frames
                audio = ID3(file_path)
                for tag in audio.values():
                    if tag.FrameID == 'APIC':
                        found_apic = True
                        mime = tag.mime.lower()
                        if mime in ('image/jpeg', 'image/jpg'):
                            ext = 'jpg'
                        elif mime == 'image/png':
                            ext = 'png'
                        else:
                            ext = 'jpg'
                        cache_key = f"{album_id}_{apic_index}"
                        cache_hash = hashlib.sha256(cache_key.encode('utf-8')).hexdigest()[:16]
                        temp_path = os.path.join(self.temp_dir, f'cover_{cache_hash}.{ext}')
                        if not os.path.exists(temp_path):
                            with open(temp_path, 'wb') as img_file:
                                img_file.write(tag.data)

                        if album_id not in self._album_art_cache:
                            url = QUrl.fromLocalFile(temp_path).toString()
                            self._album_art_cache[album_id] = url
                            return url
                        apic_index += 1

            if not found_apic:
                return ""
            # If multiple APICs, only the first is returned/cached for now
            return self._album_art_cache.get(album_id, "")
        except Exception as e:
            logger.error(f"Error getting album art: {e}")
            return ""

    def _extract_album_colors(self, image_path):
        """Extract dominant colors from album art image using k-means clustering.
        Pure Python + QImage — no Pillow/numpy required.
        """
        try:
            img = QImage(image_path)
            if img.isNull():
                return None
            img = img.convertToFormat(QImage.Format.Format_RGBA8888)
            img = img.scaled(50, 50, Qt.IgnoreAspectRatio, Qt.SmoothTransformation)

            # Format_RGBA8888 guarantees 4-byte stride per pixel, no row padding
            ptr = img.constBits()
            raw = bytes(ptr)
            pixels = [
                (raw[i], raw[i + 1], raw[i + 2])
                for i in range(0, len(raw), 4)
            ]

            colors = self._kmeans_colors(pixels, k=5)

            def color_vibrancy(rgb):
                r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
                h, s, v = colorsys.rgb_to_hsv(r, g, b)
                return s * v

            colors = sorted(colors, key=color_vibrancy, reverse=True)

            def luminance(rgb):
                r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
                return 0.299 * r + 0.587 * g + 0.114 * b

            sample = pixels[:1000]
            avg_luminance = sum(luminance(p) for p in sample) / len(sample)
            is_dark_image = avg_luminance < 0.5

            theme_colors = self._generate_theme_from_colors(colors, is_dark_image)
            return theme_colors

        except Exception as e:
            logger.error(f"Error extracting colors: {e}")
            return None

    def _kmeans_colors(self, pixels, k=5, max_iterations=10):
        """Simple k-means clustering to find dominant colors. Pure Python."""
        import random
        # Local generator: seeding the *global* random module here made every
        # later random.shuffle() / getrandbits() in the process deterministic.
        rng = random.Random(42)
        n = len(pixels)
        indices = rng.sample(range(n), min(k, n))
        centroids = [list(pixels[i]) for i in indices]

        labels = [0] * n
        for _ in range(max_iterations):
            # Assign each pixel to nearest centroid
            for i, px in enumerate(pixels):
                min_dist = float('inf')
                for j, c in enumerate(centroids):
                    d = (px[0] - c[0]) ** 2 + (px[1] - c[1]) ** 2 + (px[2] - c[2]) ** 2
                    if d < min_dist:
                        min_dist = d
                        labels[i] = j

            # Update centroids
            new_centroids = []
            converged = True
            for j in range(k):
                members = [pixels[i] for i in range(n) if labels[i] == j]
                if members:
                    nr, ng, nb = 0, 0, 0
                    for m in members:
                        nr += m[0]; ng += m[1]; nb += m[2]
                    cnt = len(members)
                    nc = [nr / cnt, ng / cnt, nb / cnt]
                else:
                    nc = centroids[j]
                if abs(nc[0] - centroids[j][0]) > 1 or abs(nc[1] - centroids[j][1]) > 1 or abs(nc[2] - centroids[j][2]) > 1:
                    converged = False
                new_centroids.append(nc)

            centroids = new_centroids
            if converged:
                break

        return [[int(c[0]), int(c[1]), int(c[2])] for c in centroids]

    def _generate_theme_from_colors(self, colors, is_dark):
        """Generate a complete theme palette from extracted colors with variety"""
        import json

        def rgb_to_hex(rgb):
            return "#{:02x}{:02x}{:02x}".format(int(rgb[0]), int(rgb[1]), int(rgb[2]))

        def adjust_brightness(rgb, factor):
            """Adjust brightness of RGB color"""
            r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            v = max(0, min(1, v * factor))
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            return [int(r * 255), int(g * 255), int(b * 255)]

        def adjust_saturation(rgb, factor):
            """Adjust saturation of RGB color"""
            r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            s = max(0, min(1, s * factor))
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            return [int(r * 255), int(g * 255), int(b * 255)]

        # Get primary colors from the extracted palette (sorted by vibrancy)
        primary_rgb = colors[0]  # Most vibrant
        secondary_rgb = colors[1] if len(colors) > 1 else colors[0]
        tertiary_rgb = colors[2] if len(colors) > 2 else secondary_rgb
        quaternary_rgb = colors[3] if len(colors) > 3 else tertiary_rgb

        # Create accent colors with MORE contrast between them
        # Primary: boost saturation and brightness significantly
        accent = adjust_saturation(primary_rgb, 1.4)
        accent = adjust_brightness(accent, 1.2)

        # Secondary: different treatment - keep saturation, adjust brightness differently
        accent2 = adjust_saturation(secondary_rgb, 1.3)
        accent2 = adjust_brightness(accent2, 1.15)

        # Tertiary: moderate boost
        accent3 = adjust_saturation(tertiary_rgb, 1.25)
        accent3 = adjust_brightness(accent3, 1.1)

        # Quaternary: for additional variety
        accent4 = adjust_saturation(quaternary_rgb, 1.2)
        accent4 = adjust_brightness(accent4, 1.05)

        # Create softer/dimmer versions for nav buttons (more contrast from main accents)
        nav_color = adjust_brightness(accent, 0.7)
        nav_color2 = adjust_brightness(accent2, 0.75)

        def get_luminance(rgb):
            """Calculate relative luminance for WCAG contrast calculations"""
            def channel_luminance(c):
                c = c / 255.0
                return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
            r, g, b = rgb[0], rgb[1], rgb[2]
            return 0.2126 * channel_luminance(r) + 0.7152 * channel_luminance(g) + 0.0722 * channel_luminance(b)

        def get_contrast_ratio(rgb1, rgb2):
            """Calculate contrast ratio between two colors (WCAG formula)"""
            l1 = get_luminance(rgb1)
            l2 = get_luminance(rgb2)
            lighter = max(l1, l2)
            darker = min(l1, l2)
            return (lighter + 0.05) / (darker + 0.05)

        def get_readable_text_color(background_rgb):
            """Return white or black text based on background luminance for best readability"""
            lum = get_luminance(background_rgb)
            # Use white text on dark backgrounds, black text on light backgrounds
            # Threshold of 0.179 is the midpoint for WCAG contrast
            if lum < 0.3:  # More generous threshold for dark backgrounds
                return [245, 245, 245]  # Bright white text
            else:
                return [25, 25, 25]  # Near black text

        def get_secondary_text_color(background_rgb):
            """Return a secondary text color with good contrast but less prominent"""
            lum = get_luminance(background_rgb)
            if lum < 0.3:  # Dark background
                return [210, 210, 210]  # Brighter secondary text (was 180)
            else:
                return [60, 60, 60]  # Darker secondary text for light backgrounds

        def cap_brightness(rgb, max_brightness=0.85):
            """Cap the brightness of a color to prevent overly bright/washed out colors"""
            r, g, b = rgb[0] / 255, rgb[1] / 255, rgb[2] / 255
            h, s, v = colorsys.rgb_to_hsv(r, g, b)
            if v > max_brightness:
                v = max_brightness
                # Also boost saturation slightly when capping brightness to keep color vibrant
                s = min(1.0, s * 1.1)
            r, g, b = colorsys.hsv_to_rgb(h, s, v)
            return [int(r * 255), int(g * 255), int(b * 255)]

        def ensure_icon_contrast(icon_rgb, background_rgb, min_contrast=3.0):
            """Ensure icon color has sufficient contrast against background.
            If not, brighten or darken the icon color until it does."""
            contrast = get_contrast_ratio(icon_rgb, background_rgb)
            if contrast >= min_contrast:
                # Cap brightness to prevent overly bright colors
                return cap_brightness(icon_rgb)

            bg_lum = get_luminance(background_rgb)
            adjusted = icon_rgb[:]

            # Try brightening or darkening based on background
            if bg_lum < 0.5:
                # Dark background - brighten the icon
                for factor in [1.3, 1.5, 1.8, 2.0, 2.5, 3.0]:
                    adjusted = adjust_brightness(icon_rgb, factor)
                    if get_contrast_ratio(adjusted, background_rgb) >= min_contrast:
                        # Cap brightness to prevent overly bright colors
                        return cap_brightness(adjusted)
                # If still not enough, return a light color (but not too bright)
                return [200, 180, 130]  # Fallback muted gold
            else:
                # Light background - darken the icon
                for factor in [0.7, 0.5, 0.4, 0.3, 0.2]:
                    adjusted = adjust_brightness(icon_rgb, factor)
                    if get_contrast_ratio(adjusted, background_rgb) >= min_contrast:
                        return adjusted
                # If still not enough, return a dark color
                return [50, 40, 30]  # Fallback dark

            return adjusted

        if is_dark:
            # Dark theme based on album art
            base = adjust_brightness(primary_rgb, 0.15)  # Very dark version
            base_alt = adjust_brightness(primary_rgb, 0.22)  # Slightly lighter
            hover = adjust_brightness(primary_rgb, 0.30)
            paused = adjust_brightness(primary_rgb, 0.25)
            playing = adjust_brightness(primary_rgb, 0.35)
        else:
            # Light theme based on album art
            base = adjust_brightness(primary_rgb, 2.5)  # Very light version
            base = adjust_saturation(base, 0.3)  # Desaturate for background
            base_alt = adjust_brightness(base, 0.92)
            hover = adjust_brightness(base, 0.88)
            paused = adjust_brightness(base, 0.85)
            playing = adjust_brightness(base, 0.80)

        # Calculate text colors based on actual background luminance for guaranteed readability
        text_primary = get_readable_text_color(base)
        text_secondary = get_secondary_text_color(base)

        # Ensure all icon colors have sufficient contrast against the base background
        accent_visible = ensure_icon_contrast(accent, base)
        accent2_visible = ensure_icon_contrast(accent2, base)
        accent3_visible = ensure_icon_contrast(accent3, base)
        accent4_visible = ensure_icon_contrast(accent4, base)
        nav_color_visible = ensure_icon_contrast(nav_color, base)
        nav_color2_visible = ensure_icon_contrast(nav_color2, base)

        # Build theme object with varied colors like CosmicVoyager
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
                "previous": rgb_to_hex(nav_color_visible),       # Dimmed primary for nav
                "play": rgb_to_hex(accent_visible),              # Bright primary for emphasis
                "pause": rgb_to_hex(accent_visible),             # Match play
                "next": rgb_to_hex(nav_color_visible),           # Match previous
                "volume": rgb_to_hex(accent2_visible),           # Secondary color for volume
                "shuffle": rgb_to_hex(accent3_visible),          # Tertiary for shuffle
                "toggleShade": rgb_to_hex(hover),
                "homeButton": rgb_to_hex(accent_visible),        # Primary accent
                "obdButton": rgb_to_hex(accent4_visible),        # Quaternary for OBD (distinct)
                "mediaButton": rgb_to_hex(accent2_visible),      # Secondary for media
                "settingsButton": rgb_to_hex(accent3_visible),   # Tertiary for settings
                "sensorButton": rgb_to_hex(accent_visible),      # Primary accent (top-level destination, matches home)
                "androidAutoButton": rgb_to_hex(nav_color2_visible),
                "phoneMirrorButton": rgb_to_hex(accent4_visible)  # Quaternary for phone mirror
            },
            "mediaroom": {
                "previous": rgb_to_hex(nav_color_visible),
                "play": rgb_to_hex(ensure_icon_contrast(adjust_brightness(accent, 1.1), base)),  # Brighter than bottombar
                "pause": rgb_to_hex(ensure_icon_contrast(adjust_brightness(accent, 1.1), base)),
                "next": rgb_to_hex(nav_color_visible),
                "left": rgb_to_hex(accent4_visible),             # Quaternary for arrows
                "right": rgb_to_hex(accent4_visible),
                "shuffle": rgb_to_hex(accent3_visible),          # Tertiary for shuffle
                "toggleShade": rgb_to_hex(paused)
            },
            "mainmenu": {
                "mediaContainer": rgb_to_hex(adjust_brightness(accent2_visible, 0.6))
            },
            "obd": {
                "boxBackground": rgb_to_hex(base_alt),
                "barColor": rgb_to_hex(ensure_icon_contrast(accent4, base_alt)),  # Visible against OBD box
                "labelColor": rgb_to_hex(get_secondary_text_color(base_alt)),  # Readable labels
                "valueColor": rgb_to_hex(get_readable_text_color(base_alt))   # Readable values
            }
        }

        return json.dumps(theme)

    @Slot(str)
    def extract_colors_from_album_art(self, filename):
        """Extract colors from album art and emit signal with theme colors"""
        import sys
        logger.debug(f"[AlbumArtCapture] extract_colors_from_album_art called with: {filename}")
        sys.stdout.flush()
        try:
            # First get the album art (this may already be cached)
            art_url = self.get_album_art(filename)
            logger.debug(f"[AlbumArtCapture] Got album art URL: {art_url}")
            if not art_url:
                logger.debug(f"[AlbumArtCapture] No album art found for: {filename}")
                return

            # Convert URL to file path using QUrl for proper cross-platform handling
            qurl = QUrl(art_url)
            image_path = qurl.toLocalFile()
            logger.debug(f"[AlbumArtCapture] Converted to local path: {image_path}")

            if not image_path or not os.path.exists(image_path):
                logger.debug(f"[AlbumArtCapture] Album art file not found: {image_path}")
                return

            logger.debug(f"[AlbumArtCapture] Extracting colors from: {image_path}")

            # Extract colors
            theme_json = self._extract_album_colors(image_path)
            if theme_json:
                # Use slot method on settings_manager (property-based approach for better QML reactivity)
                if self._settings_manager:
                    self._settings_manager.set_album_art_colors(theme_json)
                    logger.debug(f"[AlbumArtCapture] Called set_album_art_colors for: {filename}")
                else:
                    # Fallback to direct signal
                    self.albumColorsExtracted.emit(theme_json)
                    logger.debug(f"[AlbumArtCapture] Emitted via mediaManager for: {filename}")
            else:
                logger.debug(f"[AlbumArtCapture] Failed to extract colors for: {filename}")
        except Exception as e:
            import traceback
            logger.debug(f"[AlbumArtCapture] Error extracting colors from album art: {e}")
            traceback.print_exc()

    @Slot(result=str)
    def get_current_file(self):
        """Get currently playing file without auto-playing"""
        # Initialize playlist if empty
        if not self._current_playlist:
            files = self._get_current_playlist_files()
            if files:
                self._current_playlist = sorted(files, key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower()))
                self._current_index = 0
                # Return the first file from the sorted playlist but don't play it
                return self._current_playlist[0]

        # Return current file if index is valid
        if 0 <= self._current_index < len(self._current_playlist):
            return self._current_playlist[self._current_index]

        # Fallback to first file if index is invalid
        files = self._get_current_playlist_files()
        if files:
            self._current_playlist = sorted(files, key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower()))
            self._current_index = 0
            return self._current_playlist[0]

        return ""

    @Slot(result=str)
    def get_next_track_name(self):
        """Peek at the next track name without triggering playback"""
        if not self._current_playlist:
            return ""
        next_index = (self._current_index + 1) % len(self._current_playlist)
        return self._current_playlist[next_index]

    @Slot(result=str)
    def get_previous_track_name(self):
        """Peek at the previous track name without triggering playback"""
        if not self._current_playlist:
            return ""
        prev_index = (self._current_index - 1) % len(self._current_playlist)
        return self._current_playlist[prev_index]

    @Slot(result=str)
    def get_next_track_album_art(self):
        """Get album art for the next track without triggering playback"""
        next_name = self.get_next_track_name()
        if not next_name:
            return ""
        return self.get_album_art(next_name)

    @Slot(result=str)
    def get_previous_track_album_art(self):
        """Get album art for the previous track without triggering playback"""
        prev_name = self.get_previous_track_name()
        if not prev_name:
            return ""
        return self.get_album_art(prev_name)

    @Slot(result=list)
    def get_neighbor_album_arts(self):
        """Get album art URLs for both neighbors in a single call.

        Returns [prevArtUrl, nextArtUrl] — halves the PySide6 bridge
        crossings compared to two separate slot calls from QML.
        """
        prev_name = self.get_previous_track_name()
        next_name = self.get_next_track_name()
        return [
            self.get_album_art(prev_name) if prev_name else "",
            self.get_album_art(next_name) if next_name else ""
        ]

    @Slot(str)
    def play_file(self, filename):
        """Play specified file"""
        # Initialize current_playlist if needed
        if not self._current_playlist:
            try:
                files = self._get_current_playlist_files()
                self._current_playlist = self._shuffle_playlist() if self._shuffle else sorted(files, key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower()))
            except Exception as e:
                logger.error(f"Error initializing playlist: {e}")
                self._current_playlist = []

        # Only proceed if we have files
        if not self._current_playlist:
            logger.info("No media files available to play")
            return

        # Try to find the file in the playlist
        try:
            if filename in self._current_playlist:
                self._current_index = self._current_playlist.index(filename)
            elif not self._shuffle:
                # If not found and not shuffled, rebuild alphabetical playlist from current playlist
                files = self._get_current_playlist_files()
                self._current_playlist = sorted(files, key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower()))
                if filename in self._current_playlist:
                    self._current_index = self._current_playlist.index(filename)
                else:
                    # File not found in current playlist, use the first file
                    filename = self._current_playlist[0] if self._current_playlist else ""
                    self._current_index = 0
        except Exception as e:
            logger.error(f"Error finding file in playlist: {e}")
            # Fallback to first file
            self._current_index = 0
            if self._current_playlist:
                filename = self._current_playlist[0]
            else:
                return
        
        # Play the file - use helper for correct path (handles All Music)
        file_path = self._get_file_path(filename)
        if os.path.exists(file_path):
            try:
                url = QUrl.fromLocalFile(file_path)
                self._player.setSource(url)
                self._player.play()
                self._is_playing = True
                self._is_paused = False

                # Restore mute state - setSource/play can reset audio output volume
                if self._is_muted:
                    self._audio_output.setVolume(0.0)

                self.playStateChanged.emit(True)
                # Pre-populate caches BEFORE emitting currentMediaChanged — this
                # moves the ID3/disk I/O out of the QML binding evaluation (render
                # phase) so the animation can start without blocking the scene graph.
                # _cache_metadata now extracts album art from the same ID3 read,
                # so get_album_art() in QML bindings is an instant cache hit.
                self._emit_metadata(filename)
                self.currentMediaChanged.emit(filename)
                self.get_formatted_duration(filename)
                # Schedule background pre-cache for neighboring tracks so the
                # NEXT click's get_album_art() is an instant cache hit.
                self._precache_timer.start()
                logger.info(f"Now playing: {filename} from {'shuffled' if self._shuffle else 'alphabetical'} playlist at position {self._current_index}")

            except Exception as e:
                logger.error(f"Playback error")
        else:
            logger.info(f"File not found: {file_path}")
     
    @Slot()
    def next_track(self):
        """Play next track in playlist"""
        try:
            if not self._current_playlist:
                files = self._get_current_playlist_files()
                self._current_playlist = sorted(files, key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower()))

            if not self._current_playlist:
                logger.info("No media files available")
                return

            self._current_index = (self._current_index + 1) % len(self._current_playlist)
            next_song = self._current_playlist[self._current_index]
            self.play_file(next_song)
            # Debounced — only writes to disk 1s after the last track change
            self._save_state_timer.start()
        except Exception as e:
            logger.error(f"Error in next_track: {e}")

    @Slot()
    def previous_track(self):
        """Play previous track in playlist"""
        try:
            if not self._current_playlist:
                files = self._get_current_playlist_files()
                self._current_playlist = sorted(files, key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower()))

            if not self._current_playlist:
                logger.info("No media files available")
                return

            self._current_index = (self._current_index - 1) % len(self._current_playlist)
            prev_song = self._current_playlist[self._current_index]
            self.play_file(prev_song)
            # Debounced — only writes to disk 1s after the last track change
            self._save_state_timer.start()
        except Exception as e:
            logger.error(f"Error in previous_track: {e}")
        
    @Slot()
    def pause(self):
        """Pause playback"""
        self._player.pause()
        self._is_paused = True
        self._is_playing = False
        self.playStateChanged.emit(False)
        self._save_playback_state()
        # Preserve mute volume state during pause
        if not self._is_muted and self._audio_output.volume() > 0.0:
            self._previous_volume = self._audio_output.volume()
        
    @Slot()
    def toggle_play(self):
        # Handle case when no source is set
        if not self._player.source().isValid():
            current_file = self.get_current_file()
            if current_file:
                self.play_file(current_file)
                return

        if self._is_playing:
            self._player.pause()
            self._is_paused = True
            self._is_playing = False
            self._save_playback_state()
        else:
            # Check if the player is in an error or invalid state before resuming
            media_status = self._player.mediaStatus()
            if media_status in (
                QMediaPlayer.MediaStatus.InvalidMedia,
                QMediaPlayer.MediaStatus.NoMedia,
                QMediaPlayer.MediaStatus.StalledMedia
            ):
                logger.warning(f"Player in bad state ({media_status}) on resume — recovering")
                self._attempt_playback_recovery()
                return

            self._player.play()
            self._is_paused = False
            self._is_playing = True

            # Verify the player actually started — if not, recover
            actual_state = self._player.playbackState()
            if actual_state != QMediaPlayer.PlaybackState.PlayingState:
                logger.warning(f"Player did not start (state={actual_state}) — recovering")
                self._attempt_playback_recovery()
                return

            # Restore mute state after successful resume
            if self._is_muted:
                self._audio_output.setVolume(0.0)

        self.playStateChanged.emit(self._is_playing)
            
    @Slot(result=bool)
    def is_playing(self):
        """Return current playing state"""
        return self._is_playing
    
    @Slot(result=bool)
    def is_paused(self):
        """Return current paused state"""
        return self._is_paused
    
    @Slot(result=float)
    def get_duration(self):
        """Get current media duration in ms"""
        return self._player.duration()

    @Slot(result=float)
    def get_position(self):
        """Get current playback position in ms"""
        return self._player.position()

    @Slot(int)
    def set_position(self, position):
        """Set playback position in ms"""
        self._player.setPosition(position)

    @Slot()
    def toggle_mute(self):
        """Toggle mute state with guard against rapid toggling"""
        if self._mute_toggle_locked:
            return
        self._mute_toggle_locked = True

        try:
            if self._is_muted:
                # Unmuting — restore previous volume (ensure it's valid)
                restore_vol = self._previous_volume if self._previous_volume > 0.0 else 0.5
                self._audio_output.setVolume(restore_vol)
            else:
                # Muting — capture current volume only if it's meaningful
                current_vol = self._audio_output.volume()
                if current_vol > 0.0:
                    self._previous_volume = current_vol
                self._audio_output.setVolume(0.0)

            self._is_muted = not self._is_muted
            self.muteChanged.emit(self._is_muted)
            logger.info(f"Mute toggled: {self._is_muted}")
        finally:
            # Unlock after a short delay to debounce rapid presses
            QTimer.singleShot(150, self._unlock_mute_toggle)

    def _unlock_mute_toggle(self):
        """Unlock mute toggle after debounce period"""
        self._mute_toggle_locked = False
    
    @Slot(result=bool)
    def is_muted(self):
        """Return current mute state"""
        return self._is_muted
            
    @Slot(float)
    def setVolume(self, volume):
        """Set output volume (0.0-1.0)"""
        try:
            volume = float(volume)
            # Clamp volume to valid range
            volume = max(0.0, min(1.0, volume))

            # If muted, update the stored volume for when unmuted, but keep audio at 0
            if self._is_muted:
                self._previous_volume = volume
                # Keep actual audio muted
                self._audio_output.setVolume(0.0)
            else:
                self._audio_output.setVolume(volume)

            self.volumeChanged.emit(volume)
        except Exception as e:
            logger.error(f"Error setting volume: {e}")
            
    @Slot(result=float)
    def getVolume(self):
        """Get current volume level (0.0-1.0)"""
        return self._audio_output.volume()

    def _get_current_playlist_files(self):
        """Get the files for the currently selected playlist.
        This returns the original playlist files (not the shuffled version).
        """
        if self._current_playlist_name and self._current_playlist_name in self._playlists:
            return self._playlists[self._current_playlist_name]["files"].copy()
        # Fallback to current playlist if no named playlist is selected
        return self._current_playlist.copy() if self._current_playlist else []

    @Slot()
    def toggle_shuffle(self):
        """Toggle shuffle mode"""
        self._shuffle = not self._shuffle

        # Get current song before changing playlists
        current_song = self.get_current_file()

        # Use the current playlist's files, not a directory scan
        # This ensures we shuffle within the selected playlist (including All Music)
        files = self._get_current_playlist_files()

        if self._shuffle:
            # Store original order if needed
            if not self._original_files:
                self._original_files = files.copy()

            # Create shuffled playlist
            shuffled = files.copy()
            random.shuffle(shuffled)

            # Move current song to start of shuffled list if it exists
            if current_song in shuffled:
                idx = shuffled.index(current_song)
                if idx > 0:  # Only swap if not already at position 0
                    shuffled[0], shuffled[idx] = shuffled[idx], shuffled[0]

            self._current_playlist = shuffled
            self._current_index = 0
            logger.info(f"Shuffle enabled for '{self._current_playlist_name}', starting from: {current_song}")
        else:
            # Get alphabetical list from original playlist files
            alphabetical = sorted(files, key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower()))

            # Find current song in alphabetical order
            if current_song and current_song in alphabetical:
                self._current_index = alphabetical.index(current_song)
                self._current_playlist = alphabetical
                logger.info(f"Shuffle disabled for '{self._current_playlist_name}'. Continuing alphabetically from: {current_song}")
            else:
                self._current_playlist = alphabetical
                self._current_index = 0

        # Clear original files when disabling shuffle
        if not self._shuffle:
            self._original_files = []

        # Emit AFTER the playlist is reordered so QML reads the new order
        self.shuffleStateChanged.emit(self._shuffle)

        # Save shuffle state for persistence
        if self._settings_manager:
            self._settings_manager.save_last_shuffle_state(self._shuffle)

        # Update UI
        self.mediaListChanged.emit(self._current_playlist)
        
    @Slot(result=bool)
    def is_shuffled(self):
        """Return current shuffle state"""
        return self._shuffle

    # ==================== Playlist Management ====================

    @Slot()
    def scan_library(self, reset_display_names=True):
        """Scan the library root for subfolders (playlists) and their MP3s"""
        if getattr(self, '_scan_in_progress', False):
            logger.info("scan_library: already in progress, skipping")
            return

        if not self._library_root or not os.path.exists(self._library_root):
            self.scanProgress.emit(f"[ERROR] Library path not set or doesn't exist")
            logger.info(f"Library root not set or doesn't exist: {self._library_root}")
            return

        self._scan_in_progress = True
        try:
            self._scan_library_inner(reset_display_names)
        finally:
            self._scan_in_progress = False

    def _scan_library_inner(self, reset_display_names):
        self.scanProgress.emit(f"[SCAN] Starting library scan...")
        self.scanProgress.emit(f"[PATH] {self._library_root}")
        logger.info(f"Scanning library at: {self._library_root}")

        # Remember active playlist so we can re-select it after rescan
        previous_playlist = self._current_playlist_name

        # Clear existing caches
        self._playlists = {}
        self._playlist_names = []
        self._metadata_cache = {}
        self._album_art_cache = {}
        self._access_count = {}
        self._all_music_file_paths = {}
        self._is_all_music_active = False
        if reset_display_names:
            self._display_names = {}
            self._save_display_names()
        self.invalidate_stats_cache()
        self.scanProgress.emit(f"[CLEAR] Caches cleared")

        # Collect all MP3s for "All Music" playlist
        all_music_files = []

        # First, check for root-level MP3s (goes to "Unsorted" playlist)
        root_mp3s = []
        self.scanProgress.emit(f"[SCAN] Checking root folder for MP3s...")
        try:
            for item in os.listdir(self._library_root):
                item_path = os.path.join(self._library_root, item)
                if os.path.isfile(item_path) and item.lower().endswith(AUDIO_EXTENSIONS):
                    root_mp3s.append(item)
                    # Track for All Music - store the directory path for this file
                    self._all_music_file_paths[item] = self._library_root
                    all_music_files.append(item)
        except Exception as e:
            self.scanProgress.emit(f"[ERROR] Failed to scan root: {e}")
            logger.error(f"Error scanning root for MP3s: {e}")

        if root_mp3s:
            self._playlists["Unsorted"] = {
                "name": "Unsorted",
                "path": self._library_root,
                "files": root_mp3s,
                "song_count": len(root_mp3s)
            }
            self._playlist_names.append("Unsorted")
            self.scanProgress.emit(f"[FOUND] 'Unsorted' - {len(root_mp3s)} songs")
            logger.info(f"Found {len(root_mp3s)} unsorted MP3s in root")

        # Now scan each immediate subfolder as a playlist
        self.scanProgress.emit(f"[SCAN] Scanning subfolders...")
        try:
            subfolders = [item for item in os.listdir(self._library_root)
                         if os.path.isdir(os.path.join(self._library_root, item))
                         and not item.startswith('.')]  # Skip hidden/system dirs
            self.scanProgress.emit(f"[INFO] Found {len(subfolders)} subfolders to scan")

            for item in subfolders:
                subfolder_path = os.path.join(self._library_root, item)
                mp3_files = []
                try:
                    for f in os.listdir(subfolder_path):
                        if f.lower().endswith(AUDIO_EXTENSIONS):
                            mp3_files.append(f)
                            # Track for All Music - handle duplicate filenames by appending folder
                            unique_name = f
                            if f in self._all_music_file_paths:
                                # Duplicate filename - make it unique by prefixing with folder name
                                unique_name = f"{item} - {f}"
                            self._all_music_file_paths[unique_name] = subfolder_path
                            all_music_files.append(unique_name)
                except Exception as e:
                    self.scanProgress.emit(f"[ERROR] Failed to scan '{item}': {e}")
                    logger.error(f"Error scanning subfolder {item}: {e}")
                    continue

                # Include every subfolder — even empty ones — so user-created
                # playlists survive a rescan (e.g. after moving the last song out).
                self._playlists[item] = {
                    "name": item,
                    "path": subfolder_path,
                    "files": mp3_files,
                    "song_count": len(mp3_files)
                }
                self._playlist_names.append(item)
                if mp3_files:
                    self.scanProgress.emit(f"[FOUND] '{item}' - {len(mp3_files)} songs")
                    logger.info(f"Found playlist '{item}' with {len(mp3_files)} songs")
                else:
                    self.scanProgress.emit(f"[FOUND] '{item}' - empty playlist")
                    logger.info(f"Found empty playlist folder '{item}'")
        except Exception as e:
            self.scanProgress.emit(f"[ERROR] Failed to scan subfolders: {e}")
            logger.error(f"Error scanning library subfolders: {e}")

        # Create "All Music" playlist if we have any songs
        if all_music_files:
            self._playlists["All Music"] = {
                "name": "All Music",
                "path": self._library_root,  # Base path (individual files use _all_music_file_paths)
                "files": all_music_files,
                "song_count": len(all_music_files),
                "is_combined": True  # Flag to indicate this is a combined playlist
            }
            self.scanProgress.emit(f"[FOUND] 'All Music' - {len(all_music_files)} songs (combined)")
            logger.info(f"Created 'All Music' playlist with {len(all_music_files)} total songs")

        # Sort playlist names alphabetically, but keep "All Music" first, then "Unsorted"
        if "Unsorted" in self._playlist_names:
            self._playlist_names.remove("Unsorted")
        self._playlist_names.sort(key=str.lower)

        # Insert special playlists at the beginning
        if "Unsorted" in self._playlists:
            self._playlist_names.insert(0, "Unsorted")
        if "All Music" in self._playlists:
            self._playlist_names.insert(0, "All Music")

        self.scanProgress.emit(f"[DONE] Scan complete: {len(self._playlist_names)} playlists, {len(all_music_files)} total songs")
        logger.info(f"Library scan complete. Found {len(self._playlist_names)} playlists")

        # Auto-strip display names so filenames are always cleaned up
        self._auto_strip_display_names()

        # If a playlist was active before the rescan, re-select it to refresh
        # the song list in QML (emits mediaListChanged). Preserve current
        # playback position so the playing song is not disrupted.
        if previous_playlist and previous_playlist in self._playlists:
            previous_file = None
            if self._current_playlist and 0 <= self._current_index < len(self._current_playlist):
                previous_file = self._current_playlist[self._current_index]
            self.select_playlist(previous_playlist)
            # Restore playback index to the same song if possible
            if previous_file and previous_file in self._current_playlist:
                self._current_index = self._current_playlist.index(previous_file)
            logger.info("Re-selected playlist '%s' after rescan", previous_playlist)

        # Emit AFTER all state is settled — QML sees the final, consistent state
        self.playlistsChanged.emit()

    @Slot(str)
    def set_library_root(self, path):
        """Set the main library folder and scan for playlists"""
        if not path:
            logger.info("Invalid library path: empty path")
            return

        # Normalize and resolve to absolute path (prevents path traversal)
        normalized_path = os.path.normpath(os.path.realpath(path))

        if os.path.exists(normalized_path) and os.path.isdir(normalized_path):
            self._library_root = normalized_path
            logger.info(f"Library root set to: {normalized_path}")
            self.scan_library(reset_display_names=False)

            # Auto-select first playlist if available
            if self._playlist_names:
                self.select_playlist(self._playlist_names[0])
        else:
            logger.info(f"Invalid library path: {path}")

    @Slot(result=list)
    def get_playlist_names(self):
        """Return list of all playlist names"""
        return self._playlist_names

    @Slot(str)
    def select_playlist(self, name):
        """Select a playlist and load its songs"""
        if name not in self._playlists:
            logger.info(f"Playlist not found: {name}")
            return

        logger.info(f"Selecting playlist: {name}")

        self._current_playlist_name = name
        playlist = self._playlists[name]

        # Check if this is the "All Music" combined playlist
        self._is_all_music_active = playlist.get("is_combined", False)

        if self._is_all_music_active:
            # For "All Music", we don't set a single media_dir since files span multiple folders
            # The _all_music_file_paths dict handles finding the correct path for each file
            self.media_dir = self._library_root  # Default to library root
            logger.info(f"All Music playlist active - files span multiple directories")
        else:
            # Update media_dir to playlist path for existing methods
            self.media_dir = playlist["path"]

        self._current_playlist = sorted(
            playlist["files"],
            key=lambda x: re.sub(r'[^\w\s]|_', '', x.lower())
        )
        self._current_index = 0

        # Clear metadata cache if switching playlists (different folder)
        self._metadata_cache = {}

        # Recalculate stats for the new playlist so signals fire
        self.invalidate_stats_cache()
        self._calculate_all_stats()

        # Emit signals
        self.currentPlaylistChanged.emit(name)
        self.mediaListChanged.emit(self._current_playlist)

    @Slot(result=str)
    def get_current_playlist_name(self):
        """Return current playlist name"""
        return self._current_playlist_name

    @Slot(result=list)
    def get_current_song_list(self):
        """Return the file list for the active playlist."""
        return self._current_playlist.copy() if self._current_playlist else []

    def _get_file_path(self, filename):
        """Get the full file path for a filename, handling All Music multi-folder playlist"""
        # Reject filenames with path traversal attempts
        # Check for ".." as a path separator component (not just anywhere in the string,
        # since song titles like "still feel..mp3" contain ".." legitimately)
        if (os.sep + '..' in filename or filename.startswith('..' + os.sep)
                or filename == '..' or filename.startswith('/') or filename.startswith('\\')):
            logger.info(f"Rejected potentially unsafe filename: {filename}")
            return None

        # Check _all_music_file_paths first - this handles All Music playlist
        # regardless of _is_all_music_active flag (needed for album art caching)
        if filename in self._all_music_file_paths:
            # For All Music, look up the directory from our mapping
            directory = self._all_music_file_paths[filename]
            # Handle renamed files (prefixed with folder name for duplicates)
            if " - " in filename and not os.path.exists(os.path.join(directory, filename)):
                # Extract the original filename after the prefix
                original_filename = filename.split(" - ", 1)[1]
                file_path = os.path.join(directory, original_filename)
            else:
                file_path = os.path.join(directory, filename)
        else:
            # Standard case - use media_dir
            file_path = os.path.join(self.media_dir, filename)

        # Validate the constructed path is within the library root
        if self._library_root and not is_safe_path(self._library_root, file_path):
            logger.info(f"Path validation failed - file outside library: {file_path}")
            return None

        return file_path

    def _get_original_filename(self, filename):
        """Get the original filename (without folder prefix for All Music duplicates)"""
        # Check if this is a renamed duplicate (prefixed with folder name)
        # Works regardless of _is_all_music_active state
        if " - " in filename and filename in self._all_music_file_paths:
            # Check if this is a renamed duplicate by seeing if original exists in mapping
            parts = filename.split(" - ", 1)
            if len(parts) == 2:
                potential_original = parts[1]
                # If the original filename also exists in the mapping, this was renamed
                if potential_original in self._all_music_file_paths:
                    return potential_original
        return filename

    # ─── Song deletion ──────────────────────────────────────────────

    songDeleted = Signal(str)  # filename that was deleted

    @Slot(str, result=bool)
    def delete_song(self, filename):
        """Delete a song file from disk and refresh the playlist."""
        if not filename:
            logger.warning("delete_song called with empty filename")
            return False

        file_path = self._get_file_path(filename)
        if not file_path:
            logger.warning("delete_song: could not resolve path for %s", filename)
            return False

        if not os.path.isfile(file_path):
            logger.warning("delete_song: file not found: %s", file_path)
            return False

        # Don't delete the currently playing file
        is_current = (
            self._current_playlist
            and 0 <= self._current_index < len(self._current_playlist)
            and self._current_playlist[self._current_index] == filename
            and self._is_playing
        )
        if is_current:
            self._player.stop()
            self._is_playing = False
            self.playStateChanged.emit(False)

        try:
            os.remove(file_path)
            logger.info("Deleted song: %s", file_path)
        except OSError as e:
            logger.error("Failed to delete %s: %s", file_path, e)
            return False

        # Remove from display names if present
        if filename in self._display_names:
            del self._display_names[filename]
            self._save_display_names()

        # Rescan library to rebuild playlists
        self.scan_library(reset_display_names=False)
        self.songDeleted.emit(filename)
        return True

    # ─── Playlist creation & song move ─────────────────────────────

    @Slot(str, result=bool)
    def create_playlist(self, name):
        """Create a new playlist subfolder in the library root."""
        if not name or not name.strip():
            logger.warning("create_playlist called with empty name")
            return False

        name = name.strip()

        # Reject path separators, traversal attempts, and hidden/system names
        if os.sep in name or '/' in name or '\\' in name or '..' in name:
            logger.warning("create_playlist: rejected unsafe name '%s'", name)
            return False

        if name.startswith('.'):
            logger.warning("create_playlist: rejected hidden folder name '%s'", name)
            return False

        if not self._library_root or not os.path.exists(self._library_root):
            logger.warning("create_playlist: library root not set")
            return False

        folder_path = os.path.join(self._library_root, name)

        if not is_safe_path(self._library_root, folder_path):
            logger.warning("create_playlist: path escapes library root")
            return False

        if os.path.exists(folder_path):
            # If the folder exists but is empty (leftover from a previous session),
            # treat it as a fresh creation rather than rejecting it.
            if os.path.isdir(folder_path) and not os.listdir(folder_path):
                logger.info("create_playlist: reusing empty folder '%s'", name)
            else:
                logger.info("create_playlist: folder '%s' already exists with content", name)
                return False
        else:
            try:
                os.makedirs(folder_path, exist_ok=True)
                logger.info("Created playlist folder: %s", folder_path)
            except OSError as e:
                logger.error("Failed to create playlist folder '%s': %s", name, e)
                return False

        # Rescan — scan_library now includes empty folders, so the new
        # playlist will appear automatically.  Single playlistsChanged emission.
        self.scan_library(reset_display_names=False)
        return True

    @Slot(str, str, result=bool)
    def move_song_to_playlist(self, filename, target_playlist):
        """Move a song file to a different playlist folder."""
        if not filename or not target_playlist:
            logger.warning("move_song_to_playlist: empty argument")
            return False

        source_path = self._get_file_path(filename)
        if not source_path or not os.path.isfile(source_path):
            logger.warning("move_song_to_playlist: source not found for '%s'", filename)
            return False

        if target_playlist not in self._playlists:
            logger.warning("move_song_to_playlist: unknown playlist '%s'", target_playlist)
            return False

        target_dir = self._playlists[target_playlist]["path"]
        if not os.path.isdir(target_dir):
            logger.warning("move_song_to_playlist: target dir missing for '%s'", target_playlist)
            return False

        if not is_safe_path(self._library_root, source_path):
            logger.warning("move_song_to_playlist: source path not safe")
            return False

        # Use the original filename (strip All Music prefix if present)
        original_name = self._get_original_filename(filename)
        dest_path = os.path.join(target_dir, original_name)

        if not is_safe_path(self._library_root, dest_path):
            logger.warning("move_song_to_playlist: dest path not safe")
            return False

        # Handle filename collision
        if os.path.exists(dest_path):
            base, ext = os.path.splitext(original_name)
            counter = 1
            while os.path.exists(dest_path):
                dest_path = os.path.join(target_dir, f"{base} ({counter}){ext}")
                counter += 1

        # Stop playback if this is the current song
        is_current = (
            self._current_playlist
            and 0 <= self._current_index < len(self._current_playlist)
            and self._current_playlist[self._current_index] == filename
            and self._is_playing
        )
        if is_current:
            self._player.stop()
            self._is_playing = False
            self.playStateChanged.emit(False)

        try:
            shutil.move(source_path, dest_path)
            logger.info("Moved '%s' -> '%s'", source_path, dest_path)
        except OSError as e:
            logger.error("Failed to move '%s': %s", filename, e)
            return False

        self.scan_library(reset_display_names=False)
        return True

    @Slot(result=list)
    def get_movable_playlist_names(self):
        """Return playlist names excluding 'All Music' (valid move targets)."""
        return [n for n in self._playlist_names if n != "All Music"]

    @Slot(str, result=bool)
    def delete_playlist(self, name):
        """Delete a playlist folder and all its contents."""
        if not name or not name.strip():
            return False

        name = name.strip()

        # Protect special playlists
        if name in ("All Music", "Unsorted"):
            logger.warning("delete_playlist: cannot delete special playlist '%s'", name)
            return False

        if name not in self._playlists:
            logger.warning("delete_playlist: playlist '%s' not found", name)
            return False

        folder_path = self._playlists[name].get("path", "")
        if not folder_path or not os.path.isdir(folder_path):
            logger.warning("delete_playlist: folder missing for '%s'", name)
            return False

        if not is_safe_path(self._library_root, folder_path):
            logger.warning("delete_playlist: path not safe for '%s'", name)
            return False

        # Stop playback if currently playing from this playlist
        if self._current_playlist_name == name and self._is_playing:
            self._player.stop()
            self._is_playing = False
            self.playStateChanged.emit(False)

        try:
            shutil.rmtree(folder_path)
            logger.info("Deleted playlist folder: %s", folder_path)
        except OSError as e:
            logger.error("Failed to delete playlist '%s': %s", name, e)
            return False

        # Switch away if this was the active playlist
        was_active = self._current_playlist_name == name

        self.scan_library(reset_display_names=False)

        if was_active and self._playlist_names:
            self.select_playlist(self._playlist_names[0])

        return True

    @Slot(str, str, str, result=bool)
    def play_downloaded_song(self, artist, name, playlist):
        """
        Find and play a downloaded song by artist, name, and playlist folder.
        Rescans library first so the file is recognized, then selects the
        playlist and plays the matching file.  Returns True on success.
        """
        if not artist or not name or not playlist:
            return False

        # Rescan so the newly-downloaded file appears in the library
        self.scan_library(reset_display_names=False)

        if playlist not in self._playlists:
            logger.warning("play_downloaded_song: playlist '%s' not found", playlist)
            return False

        # Build the expected filename stem the same way music_dl does
        expected_stem = f"{artist} - {name}".lower()
        # Sanitize like music_dl: remove /?\\*|<>, replace ": with -/'
        expected_stem = "".join(c for c in expected_stem if c not in "/?\\*|<>")
        expected_stem = expected_stem.replace('"', "'").replace(":", "-")

        # Search the playlist for a matching file
        playlist_data = self._playlists[playlist]
        matched_file = None
        for f in playlist_data.get("files", []):
            stem = os.path.splitext(f)[0].lower()
            if stem == expected_stem:
                matched_file = f
                break

        if not matched_file:
            # Fuzzy: check if file stem starts with expected
            for f in playlist_data.get("files", []):
                stem = os.path.splitext(f)[0].lower()
                if expected_stem in stem or stem in expected_stem:
                    matched_file = f
                    break

        if not matched_file:
            logger.warning("play_downloaded_song: no match for '%s - %s' in '%s'", artist, name, playlist)
            return False

        # Select the playlist and play the file
        self.select_playlist(playlist)
        self.play_file(matched_file)
        logger.info("play_downloaded_song: playing '%s' from '%s'", matched_file, playlist)
        return True

    @Slot(str, result=str)
    def get_song_file_path(self, filename):
        """Return the full file path for a song filename (for UI display)."""
        path = self._get_file_path(filename)
        return path if path else ""

    @Slot(str, result=bool)
    def play_file_at_path(self, file_path):
        """
        Play a specific file by absolute path. Rescans the library so freshly
        downloaded files are picked up, finds which playlist holds the file,
        selects that playlist, and plays the file. Returns True on success.
        """
        if not file_path or not os.path.exists(file_path):
            logger.warning("play_file_at_path: path missing or does not exist: %s", file_path)
            return False

        self.scan_library(reset_display_names=False)

        file_dir = os.path.realpath(os.path.dirname(file_path))
        filename = os.path.basename(file_path)

        for playlist_name, playlist_data in self._playlists.items():
            if playlist_data.get("is_combined"):
                continue
            playlist_dir = os.path.realpath(playlist_data.get("path", ""))
            if playlist_dir == file_dir and filename in playlist_data.get("files", []):
                self.select_playlist(playlist_name)
                self.play_file(filename)
                logger.info("play_file_at_path: playing '%s' from playlist '%s'", filename, playlist_name)
                return True

        logger.warning("play_file_at_path: no playlist contains '%s'", file_path)
        return False

    def _load_display_names(self):
        """Load persisted display names from JSON"""
        try:
            if os.path.exists(self._display_names_path):
                with open(self._display_names_path, 'r', encoding='utf-8') as f:
                    self._display_names = json.load(f)
                logger.info(f"Loaded {len(self._display_names)} display names")
        except Exception as e:
            logger.error(f"Error loading display names: {e}")
            self._display_names = {}

    def _save_display_names(self):
        """Persist display names to JSON"""
        try:
            with open(self._display_names_path, 'w', encoding='utf-8') as f:
                json.dump(self._display_names, f, ensure_ascii=False, indent=2)
            logger.info(f"Saved {len(self._display_names)} display names")
        except Exception as e:
            logger.error(f"Error saving display names: {e}")

    # ─── Filename stripping (shared logic) ───────────────────────────

    # Junk patterns to remove from display names
    _JUNK_PATTERNS = [
        r'\(Official\s+Audio\)',
        r'\(Official\s+Video\)',
        r'\(Official\s+Music\s+Video\)',
        r'\(Official\s+Lyric\s+Video\)',
        r'\(Lyrics?\)',
        r'\(Audio\)',
        r'\(Visuali[sz]er\)',
        r'\(Music\s+Video\)',
        r'\(Live\)',
        r'\[Official\]',
        r'\[Official\s+Audio\]',
        r'\[Official\s+Video\]',
        r'\[HD\]',
        r'\[HQ\]',
        r'\[4K\]',
        r'\[Lyrics?\]',
        r'\(feat\.\s*[^)]*\)',
        r'\(ft\.\s*[^)]*\)',
        r'\[feat\.\s*[^\]]*\]',
        r'\[ft\.\s*[^\]]*\]',
    ]
    _JUNK_RE = re.compile('|'.join(_JUNK_PATTERNS), re.IGNORECASE)
    _TRACK_NUM_RE = re.compile(r'^\d{1,3}\s*[-._)\s]\s*')

    def _strip_single_filename(self, filename):
        """Strip a single filename and return the cleaned name, or None if unchanged."""
        name = filename
        if name.lower().endswith(AUDIO_EXTENSIONS):
            name = name[:-4]

        original_name = name

        # Get artist from ID3 tag to remove from filename
        try:
            self._cache_metadata(filename)
            artist = self._metadata_cache.get(filename, {}).get("artist", "")
            if artist and artist != "Unknown Artist":
                artist_names = re.split(r'[/;]', artist)
                for a in artist_names:
                    a = a.strip()
                    if a:
                        name = re.sub(re.escape(a), '', name, flags=re.IGNORECASE)
        except Exception:
            pass

        name = name.replace('_', ' ')
        name = self._TRACK_NUM_RE.sub('', name)
        name = self._JUNK_RE.sub('', name)

        # Clean up leftover separator sequences
        name = re.sub(r'[,\s]+-[,\s]+', ' - ', name)
        name = re.sub(r',\s*,', ',', name)
        name = re.sub(r'\s{2,}', ' ', name)
        name = re.sub(r'-{2,}', '-', name)
        name = re.sub(r'\.{2,}', '.', name)
        name = name.strip(' -.,')
        name = re.sub(r'\(\s*\)', '', name)
        name = re.sub(r'\[\s*\]', '', name)
        name = name.strip()

        if name and name != original_name:
            return name
        return None

    def _auto_strip_display_names(self):
        """Automatically strip display names for all files not yet stripped.
        Called at the end of every library scan."""
        if not self._library_root or not os.path.exists(self._library_root):
            return

        all_files = []
        for playlist_data in self._playlists.values():
            if playlist_data.get("is_combined"):
                continue
            for filename in playlist_data["files"]:
                if filename not in all_files:
                    all_files.append(filename)

        if not all_files:
            return

        changed = 0
        for filename in all_files:
            # Skip files that already have a display name
            if filename in self._display_names:
                continue

            cleaned = self._strip_single_filename(filename)
            if cleaned:
                self._display_names[filename] = cleaned
                changed += 1

        if changed:
            self._save_display_names()
            logger.info("Auto-stripped %d of %d filenames", changed, len(all_files))

    @Slot()
    def strip_filenames(self):
        """Clean up display names for all MP3s in the library"""
        if not self._library_root or not os.path.exists(self._library_root):
            self.scanProgress.emit("[ERROR] Library path not set or doesn't exist")
            return

        self.scanProgress.emit("[STRIP] Starting filename cleanup...")

        changed = 0
        all_files = []

        # Gather all MP3 filenames from all playlists
        for playlist_name, playlist_data in self._playlists.items():
            if playlist_data.get("is_combined"):
                continue  # Skip "All Music" since it duplicates files
            for filename in playlist_data["files"]:
                if filename not in all_files:
                    all_files.append(filename)

        if not all_files:
            self.scanProgress.emit("[STRIP] No MP3 files found in library")
            return

        for filename in all_files:
            cleaned = self._strip_single_filename(filename)
            if cleaned:
                self._display_names[filename] = cleaned
                original = filename[:-4] if filename.lower().endswith(AUDIO_EXTENSIONS) else filename
                self.scanProgress.emit(f'[STRIP] "{original}" → "{cleaned}"')
                changed += 1

        # Save to disk
        self._save_display_names()

        self.scanProgress.emit(f"[DONE] Stripped {changed} of {len(all_files)} filenames")

        # Notify QML to refresh
        self.mediaListChanged.emit(self._current_playlist)

    @Slot(str, result=str)
    def get_display_name(self, filename):
        """Get the cleaned display name for a file, falling back to filename without extension"""
        return self._display_names.get(filename, filename.replace('.mp3', ''))

    @Slot(QObject)
    def connect_settings_manager(self, settings_manager):
        # Guard against double initialization
        if self._settings_manager is not None:
            return
        self._settings_manager = settings_manager
        # Set library root from settings and scan for playlists
        if self._settings_manager:
            self.set_library_root(self._settings_manager.mediaFolder)
            # Connect to future changes
            self._settings_manager.mediaFolderChanged.connect(self.set_library_root)

            # Restore last playback state after library is scanned
            QTimer.singleShot(500, self._restore_playback_state)

            # Connect theme change to trigger album art color extraction
            self._settings_manager.themeSettingChanged.connect(self._on_theme_changed)

            # Check if Album Art Capture is already active on startup
            current_theme = self._settings_manager.themeSetting
            self._album_art_capture_active = (current_theme == "Album Art Capture")

            # If Album Art Capture is active on startup, trigger initial extraction after restore
            if self._album_art_capture_active:
                # Delay extraction to allow playback state restore to complete first
                QTimer.singleShot(1000, self._extract_colors_on_startup)

    def _on_theme_changed(self, theme):
        """Handle theme change - extract colors if Album Art Capture is selected"""
        import sys
        logger.debug(f"[AlbumArtCapture] Theme changed to: {theme}")
        sys.stdout.flush()

        # Track if Album Art Capture is active
        self._album_art_capture_active = (theme == "Album Art Capture")

        if self._album_art_capture_active:
            # Get current file and extract colors
            current_file = self.get_current_file()
            logger.debug(f"[AlbumArtCapture] Current file: {current_file}")
            sys.stdout.flush()
            if current_file:
                self.extract_colors_from_album_art(current_file)

    def _on_media_changed_for_theme(self, filename):
        """Handle media change - extract colors if Album Art Capture is active"""
        import sys
        if self._album_art_capture_active and filename:
            logger.debug(f"[AlbumArtCapture] Media changed, extracting colors for: {filename}")
            sys.stdout.flush()
            self.extract_colors_from_album_art(filename)

    def _extract_colors_on_startup(self):
        """Extract colors on startup if Album Art Capture theme is already active"""
        import sys
        logger.debug(f"[AlbumArtCapture] Startup extraction triggered")
        sys.stdout.flush()
        if self._album_art_capture_active:
            current_file = self.get_current_file()
            logger.debug(f"[AlbumArtCapture] Startup - current file: {current_file}")
            sys.stdout.flush()
            if current_file:
                self.extract_colors_from_album_art(current_file)

    def _restore_playback_state(self):
        """Restore last played song and position from settings"""
        if not self._settings_manager:
            return

        last_song = self._settings_manager.get_last_played_song()
        last_position = self._settings_manager.get_last_played_position()
        last_playlist = self._settings_manager.get_last_played_playlist()
        auto_play = self._settings_manager.get_auto_play_on_startup()

        # Restore shuffle state if persist setting is enabled
        if self._settings_manager.get_persist_shuffle_state():
            restore_shuffle = self._settings_manager.get_last_shuffle_state()
            if restore_shuffle and not self._shuffle:
                self._shuffle = True
                self.shuffleStateChanged.emit(True)
                logger.info("Restored persistent shuffle state: ON")

        logger.info(f"Restoring playback state: song={last_song}, position={last_position}, playlist={last_playlist}, auto_play={auto_play}")

        if not last_song:
            return

        # Select the playlist if it exists
        if last_playlist and last_playlist in self._playlists:
            self.select_playlist(last_playlist)

        # Check if the song exists in the current playlist
        if last_song not in self._current_playlist:
            logger.info(f"Last played song '{last_song}' not found in current playlist")
            return

        # If shuffle was restored, shuffle the playlist with current song at front
        if self._shuffle:
            self._original_files = self._current_playlist.copy()
            shuffled = self._current_playlist.copy()
            random.shuffle(shuffled)
            if last_song in shuffled:
                idx = shuffled.index(last_song)
                if idx > 0:
                    shuffled[0], shuffled[idx] = shuffled[idx], shuffled[0]
            self._current_playlist = shuffled
            self.mediaListChanged.emit(self._current_playlist)

        # Set up the player with the last song - use helper for correct path
        file_path = self._get_file_path(last_song)
        if not os.path.exists(file_path):
            logger.info(f"Last played file not found: {file_path}")
            return

        # Find the song's index in the playlist
        self._current_index = self._current_playlist.index(last_song)

        # Load the song
        url = QUrl.fromLocalFile(file_path)
        self._player.setSource(url)

        # Emit signals to update UI
        self.currentMediaChanged.emit(last_song)
        self._emit_metadata(last_song)
        self.get_formatted_duration(last_song)

        # Set position after a small delay to ensure media is loaded
        if last_position > 0:
            QTimer.singleShot(100, lambda: self._player.setPosition(last_position))

        # Auto-play if enabled (delay allows audio system to fully initialize and avoid crackling)
        if auto_play:
            QTimer.singleShot(500, self._player.play)
            QTimer.singleShot(500, lambda: self._set_playing_state(True))

        logger.info(f"Playback state restored: {last_song} at position {last_position}ms")

    def _set_playing_state(self, is_playing):
        """Helper to set playing state and emit signal"""
        self._is_playing = is_playing
        self._is_paused = not is_playing
        self.playStateChanged.emit(is_playing)

    @Slot()
    def _save_playback_state(self):
        """Debounced entry point — restarts the timer so only the last call wins."""
        self._save_state_timer.start()

    def _save_playback_state_now(self):
        """Actually write playback state to disk (called by debounce timer)."""
        if not self._settings_manager:
            return

        current_song = self.get_current_file()
        current_position = self._player.position()
        current_playlist = self._current_playlist_name

        if current_song:
            self._settings_manager.save_playback_state(
                current_song,
                current_position,
                current_playlist
            )

    def _precache_neighbors_start(self):
        """Kick off background pre-caching of neighboring tracks' metadata + art.

        Runs in a daemon thread so the main thread (and animation) is never blocked.
        CPython's GIL makes dict reads/writes atomic, so accessing the caches from
        the worker thread is safe without a lock.
        """
        if not self._current_playlist:
            return
        idx = self._current_index
        playlist = list(self._current_playlist)  # snapshot
        n = len(playlist)
        # Bail on the "no current track" sentinel (-1) or a stale index that
        # outlived a playlist shrink (mirrors the C++ guard).
        if n == 0 or idx < 0 or idx >= n:
            return
        neighbors = []
        for offset in range(-3, 4):
            if offset == 0:
                continue
            j = (idx + offset) % n
            track = playlist[j]
            # Playlists shorter than the +/-3 window wrap onto themselves; don't
            # re-cache the current track or queue the same file repeatedly.
            if j == idx or track in neighbors:
                continue
            neighbors.append(track)
        if not neighbors:
            return
        threading.Thread(
            target=self._precache_tracks,
            args=(neighbors,),
            daemon=True,
        ).start()

    def _precache_tracks(self, filenames):
        """Background: pre-cache metadata and album art so future clicks are instant."""
        for filename in filenames:
            try:
                self._cache_metadata(filename)
                self.get_album_art(filename)
            except Exception:
                pass  # non-critical — worst case the main thread does the work later

    def update_media_directory(self, directory):
        if os.path.exists(directory) and os.path.isdir(directory):
            old_dir = self.media_dir
            self.media_dir = directory
            
            # Clear caches that depend on the previous directory
            self._metadata_cache = {}
            self._album_art_cache = {}
            self._access_count = {}
            self.invalidate_stats_cache()
            
            # Refresh media files
            self.get_media_files()
            
            # If currently playing, try to continue with same file or reset
            current_file = self.get_current_file()
            if self._is_playing and current_file and os.path.exists(self._get_file_path(current_file)):
                self.play_file(current_file)
            elif self._is_playing:
                # Was playing but file not in new directory - play first available file
                files = self.get_media_files()
                if files:
                    self.play_file(files[0])
                else:
                    self._player.stop()
                    self._is_playing = False
                    self._is_paused = True
                    self.playStateChanged.emit(False)
        else:
            logger.warning(f"Warning: Directory {directory} does not exist or is not a directory")
            
    @Slot(result=str)
    def get_default_media_dir(self):
        """Return the default media directory path"""
        return self.default_media_dir

    @Slot(result=str)
    def get_media_folder_name(self):
        """Return just the folder name of the current media directory"""
        return os.path.basename(self.media_dir)

    @Slot(str, result=str)
    def get_full_file_path(self, filename):
        """Get the full file path for a given filename, handling All Music playlist"""
        path = self._get_file_path(filename)
        return path if path and os.path.exists(path) else ""

    def _calculate_all_stats(self):
        """Calculate all statistics at once and cache the results"""
        if self._stats_cache["is_valid"]:
            return

        try:
            # Use current playlist files for stats calculation
            files = self._get_current_playlist_files()
            
            # Initialize calculation variables
            total_ms = 0
            albums = set()
            artists = set()
            
            # Process all files in a single pass
            for filename in files:
                if filename not in self._metadata_cache:
                    self._cache_metadata(filename)
                    
                # Duration
                duration_seconds = self._metadata_cache[filename]["duration"]
                total_ms += duration_seconds * 1000
                
                # Album
                album = self._metadata_cache[filename]["album"]
                if album and album != "Unknown Album":
                    albums.add(album)
                    
                # Artist
                artist = self._metadata_cache[filename]["artist"]
                if artist and artist != "Unknown Artist":
                    artists.add(artist)
            
            # Update cache
            self._stats_cache["total_duration_ms"] = total_ms
            self._stats_cache["total_duration_formatted"] = self._format_duration(total_ms)
            self._stats_cache["album_count"] = len(albums)
            self._stats_cache["artist_count"] = len(artists)
            self._stats_cache["is_valid"] = True
            
            # Emit signals with new values
            self.totalDurationChanged.emit(self._stats_cache["total_duration_formatted"])
            self.albumCountChanged.emit(self._stats_cache["album_count"])
            self.artistCountChanged.emit(self._stats_cache["artist_count"])
        
        except Exception as e:
            logger.error(f"Error calculating statistics: {e}")
            # Set default values on error
            self._stats_cache["total_duration_ms"] = 0
            self._stats_cache["total_duration_formatted"] = "0:00:00"
            self._stats_cache["album_count"] = 0
            self._stats_cache["artist_count"] = 0

    def _format_duration(self, ms):
        """Format milliseconds to hours:minutes:seconds"""
        try:
            seconds = int(ms / 1000)
            minutes = int(seconds / 60)
            hours = int(minutes / 60)
            minutes = minutes % 60
            seconds = seconds % 60
            return f"{hours}:{minutes:02d}:{seconds:02d}"
        except Exception as e:
            logger.error(f"Error formatting duration: {e}")
            return "0:00:00"

    @Slot(result=str)
    def get_total_duration(self):
        """Get the total duration of all media files as formatted string"""
        if not self._stats_cache["is_valid"]:
            self._calculate_all_stats()
        return self._stats_cache["total_duration_formatted"]

    @Slot(result=int)
    def get_album_count(self):
        """Get the count of unique albums"""
        if not self._stats_cache["is_valid"]:
            self._calculate_all_stats()
        return self._stats_cache["album_count"]

    @Slot(result=int)
    def get_artist_count(self):
        """Get the count of unique artists"""
        if not self._stats_cache["is_valid"]:
            self._calculate_all_stats()
        return self._stats_cache["artist_count"]
    
    def _clean_for_sort(self, filename):
        """Helper function to create consistent sort keys"""
        return re.sub(r'[^\w\s]|_', '', filename.lower())

    @Slot(str, bool, result=list)
    def sort_media_files(self, sort_column, ascending=True):
        """Sort media files based on criteria"""
        try:
            # Use cached files instead of calling get_media_files() again
            # This is the key change to prevent the infinite recursion
            files = self._current_playlist if self._current_playlist else self.get_media_files(emit_signal=False)

            if sort_column == "title":
                sorted_files = sorted(files, 
                                key=lambda x: re.sub(r'[^\w\s]|_', '', 
                                                    x.replace('.mp3', '').lower().strip()), 
                                reverse=not ascending)
            elif sort_column == "album":
                sorted_files = sorted(files, 
                                key=lambda x: re.sub(r'[^\w\s]|_', '', 
                                                    self.get_album(x).lower().strip()), 
                                reverse=not ascending)
            elif sort_column == "artist":
                sorted_files = sorted(files, 
                                key=lambda x: re.sub(r'[^\w\s]|_', '', 
                                                    self.get_band(x).lower().strip()), 
                                reverse=not ascending)
            else:
                sorted_files = files
                
            return sorted_files
        except Exception as e:
            logger.error(f"Error sorting media files: {e}")
            return []  # Return empty list on error instead of calling get_media_files again