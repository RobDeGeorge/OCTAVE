"""
DownloadManager — QObject manager for music downloads in OCTAVE.
YouTube-only: YouTube Music search (ytmusicapi) + yt-dlp download via music_dl
(our fork of spotdl), matching the C++ DownloadManager.
Follows the standard OCTAVE manager pattern with ThreadPoolExecutor + QTimer polling.
"""

import json
import os
import re
import threading
import traceback
from concurrent.futures import Future, ThreadPoolExecutor
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

from PySide6.QtCore import QObject, QStandardPaths, QTimer, Signal, Slot, Property

from backend.logging_config import get_logger

logger = get_logger(__name__)


class DownloadManager(QObject):
    """
    Manages music search and download operations.
    Exposes search/download to QML via signals and slots.
    """

    # Search signals
    searchResults = Signal(str)        # JSON array of song results
    searchInProgress = Signal(bool)    # Loading state
    searchError = Signal(str)          # Error message

    # Download signals (keyed by song_id for uniqueness)
    downloadStarted = Signal(str, str)         # song_id, display name
    downloadProgress = Signal(str, float)      # song_id, progress 0.0-1.0
    downloadComplete = Signal(str, str)        # song_id, file path
    downloadError = Signal(str, str, str)      # song_id, display name, error message
    # Python has no explicit queue (the ThreadPoolExecutor serialises work);
    # this fires whenever a download starts or finishes so QML bindings that
    # listen for it refresh exactly as they do against the C++ queue.
    downloadQueueChanged = Signal()            # Queue updated
    downloadStatusChanged = Signal(str, str, str)  # song_id, field, value_json (patch without full re-emit)

    # Status
    statusMessage = Signal(str)        # General status messages

    # Properties
    _is_searching = False
    _active_downloads = 0
    _download_queue: List[Dict[str, Any]] = []

    isSearchingChanged = Signal(bool)
    activeDownloadsChanged = Signal(int)

    def __init__(self):
        super().__init__()
        self._settings_manager = None
        self._music_dl = None
        self._executor = ThreadPoolExecutor(max_workers=2)
        self._pending_futures: Dict[str, Future] = {}  # song_name -> future
        self._download_queue = []

        # Search pagination state
        self._last_query = ""
        self._search_offset = 0
        self._all_results: List[Dict[str, Any]] = []  # accumulated results across pages

        # Thread-safe progress queue: worker threads push updates here,
        # main thread poll timer reads and emits Qt signals
        self._progress_lock = threading.Lock()
        self._progress_updates: List[Tuple[str, float]] = []

        # Target playlist override (None = use downloadSubfolder setting)
        self._target_subfolder = None

        # QTimer for polling futures AND progress (Windows-safe signal emission)
        self._poll_timer = QTimer()
        self._poll_timer.setInterval(200)
        self._poll_timer.timeout.connect(self._poll_futures)

    def connect_settings_manager(self, settings_manager):
        """Wire up settings manager for download paths and format settings."""
        self._settings_manager = settings_manager
        logger.info("DownloadManager connected to SettingsManager")

    def _get_download_path(self) -> str:
        """Get the download destination folder."""
        if not self._settings_manager:
            return os.path.expanduser("~/Music/Downloads")

        media_folder = self._settings_manager.mediaFolder
        if not media_folder:
            media_folder = os.path.expanduser("~/Music")

        # Use target subfolder override if set, otherwise fall back to setting
        subfolder = (
            self._target_subfolder
            if self._target_subfolder is not None
            else self._settings_manager.get_setting("downloadSubfolder", "Downloads")
        )

        # Validate subfolder to prevent path traversal
        from backend.media_manager import is_safe_path
        download_path = os.path.join(media_folder, subfolder)
        if not is_safe_path(media_folder, download_path):
            logger.warning(
                "Download subfolder '%s' escapes media folder, using 'Downloads'",
                subfolder,
            )
            download_path = os.path.join(media_folder, "Downloads")

        os.makedirs(download_path, exist_ok=True)
        return download_path

    def _get_download_format(self) -> str:
        """Get the configured download format."""
        if not self._settings_manager:
            return "mp3"
        return self._settings_manager.get_setting("downloadFormat", "mp3")

    def _get_download_bitrate(self) -> str:
        """Get the configured download bitrate."""
        if not self._settings_manager:
            return "auto"
        return self._settings_manager.get_setting("downloadBitrate", "auto")

    def _youtube_cookies_default_path(self) -> str:
        """Canonical location for the user's exported YouTube cookies file."""
        return os.path.join(
            QStandardPaths.writableLocation(QStandardPaths.DownloadLocation),
            "youtube_cookies.txt",
        )

    def _get_youtube_cookies_path(self) -> Optional[str]:
        """Return the absolute path to youtube_cookies.txt if present, else None.

        Mirrors C++ DownloadManager::_getYoutubeCookiesPath. The user exports
        cookies.txt from a logged-in YouTube session via a browser extension
        (e.g. 'Get cookies.txt LOCALLY') and drops it in their Downloads
        folder; yt-dlp uses it via --cookies / cookiefile to bypass the 2026
        'Sign in to confirm you're not a bot' wall.
        """
        candidate = self._youtube_cookies_default_path()
        return candidate if os.path.exists(candidate) else None

    def _refresh_cookies_file(self):
        """Re-check the cookies file before every download (C++ checks per
        download; the engine is created once, so push the current path into
        its live downloader settings)."""
        if self._music_dl is None:
            return
        cookies_path = self._get_youtube_cookies_path()
        downloader = getattr(self._music_dl, "downloader", None)
        if downloader is None:
            return
        if downloader.settings.get("cookie_file") == cookies_path:
            return
        downloader.settings["cookie_file"] = cookies_path
        for provider in getattr(downloader, "audio_providers", []):
            provider.cookie_file = cookies_path
        if cookies_path:
            logger.info("Using YouTube cookies from %s", cookies_path)
        else:
            logger.info(
                "No YouTube cookies file found; downloads may hit bot wall. "
                "Drop a cookies.txt at %s",
                self._youtube_cookies_default_path(),
            )

    def _ensure_music_dl(self) -> bool:
        """
        Lazily initialize the music_dl engine.
        Returns True if ready, False if settings manager is missing.
        """
        if self._music_dl is not None:
            return True

        if not self._settings_manager:
            logger.error("DownloadManager: no settings manager connected")
            self.statusMessage.emit("Download engine not configured")
            return False

        try:
            from backend.music_dl import MusicDL

            download_path = self._get_download_path()
            dl_format = self._get_download_format()
            bitrate = self._get_download_bitrate()

            # YouTube cookies bypass — mirrors C++ DownloadManager. Drop a
            # Netscape cookies.txt at <DownloadLocation>/youtube_cookies.txt
            # exported from a logged-in YouTube session and the downloader
            # will pass it to yt-dlp via cookiefile.
            cookies_path = self._get_youtube_cookies_path()

            downloader_settings = {
                "output": os.path.join(download_path, "{artists} - {title}.{output-ext}"),
                "format": dl_format,
                "bitrate": bitrate,
                "threads": 2,
                "overwrite": "skip",
                "_progress_callback": self._on_download_progress,
            }
            if cookies_path:
                downloader_settings["cookie_file"] = cookies_path
                logger.info("Using YouTube cookies from %s", cookies_path)
            else:
                logger.info(
                    "No YouTube cookies file found; downloads may hit bot wall. "
                    "Drop a cookies.txt at %s",
                    self._youtube_cookies_default_path(),
                )

            self._music_dl = MusicDL(downloader_settings=downloader_settings)
            logger.info("MusicDL engine initialized (format=%s, path=%s)", dl_format, download_path)
            self.statusMessage.emit("Download engine ready")
            return True

        except Exception as exc:
            logger.error("Failed to initialize MusicDL: %s", exc)
            logger.debug(traceback.format_exc())
            self.statusMessage.emit(f"Download engine error: {exc}")
            self._music_dl = None
            return False

    def _on_download_progress(self, tracker, message):
        """Callback from music_dl progress handler (runs on WORKER thread).
        Queues progress for the main-thread poll timer to emit as Qt signals."""
        try:
            song_id = tracker.song.song_id if tracker.song else ""
            progress = tracker.progress / 100.0
            with self._progress_lock:
                self._progress_updates.append((song_id, progress))
        except Exception:
            pass

    # ─── QML Properties ───────────────────────────────────────────────

    @Property(bool, notify=isSearchingChanged)
    def isSearching(self):
        return self._is_searching

    @Property(int, notify=activeDownloadsChanged)
    def activeDownloads(self):
        return self._active_downloads

    @Property(str, constant=True)
    def downloadPath(self):
        return self._get_download_path()

    # ─── QML Slots ────────────────────────────────────────────────────

    @Slot(str)
    def set_download_playlist(self, name: str):
        """Set the target playlist (subfolder) for downloads."""
        if not name or not name.strip():
            self._target_subfolder = None
            logger.info("Download playlist reset to default")
        else:
            self._target_subfolder = name.strip()
            logger.info("Download playlist set to: %s", self._target_subfolder)

        # If engine is already initialized, update its output path
        if self._music_dl is not None:
            download_path = self._get_download_path()
            dl_format = self._get_download_format()
            output_template = os.path.join(
                download_path, "{artists} - {title}.{output-ext}"
            )
            self._music_dl.downloader.settings["output"] = output_template
            logger.info("Updated engine output path to: %s", download_path)

    @Slot(result=str)
    def get_download_playlist(self) -> str:
        """Get the current target playlist name for downloads."""
        if self._target_subfolder is not None:
            return self._target_subfolder
        if self._settings_manager:
            return self._settings_manager.get_setting("downloadSubfolder", "Downloads")
        return "Downloads"

    @Slot(str)
    def search(self, query: str):
        """
        Search YouTube Music for songs matching a text query.
        Results emitted via searchResults signal as JSON.
        """
        if not query.strip():
            return

        if not self._ensure_music_dl():
            return

        # Reset pagination for new search
        self._last_query = query.strip()
        self._search_offset = 0
        self._all_results = []

        self._is_searching = True
        self.isSearchingChanged.emit(True)
        self.searchInProgress.emit(True)
        self.statusMessage.emit(f"Searching: {query}")

        future = self._executor.submit(self._do_search, query.strip(), 0)
        self._pending_futures["__search__"] = future

        if not self._poll_timer.isActive():
            self._poll_timer.start()

    @Slot()
    def search_more(self):
        """Indicate that all results are already shown (ytmusicapi doesn't support offset pagination)."""
        if not self._last_query:
            return
        self.statusMessage.emit("All results shown")

    @Slot(str)
    def download_song(self, song_json: str):
        """
        Download a single song from its JSON representation.
        Called from QML when user clicks download on a search result.
        """
        if not self._ensure_music_dl():
            return
        self._refresh_cookies_file()

        try:
            song_data = json.loads(song_json)
        except json.JSONDecodeError as exc:
            logger.error("Invalid song JSON: %s", exc)
            self.downloadError.emit("", "Unknown", f"Invalid song data: {exc}")
            return

        song_id = song_data.get("song_id", "")
        song_name = f"{song_data.get('artist', 'Unknown')} - {song_data.get('name', 'Unknown')}"

        # Prevent duplicate download of the same song by song_id
        if song_id:
            for key in self._pending_futures:
                if key.startswith("dl_") and key.endswith(f"_{song_id}"):
                    self.statusMessage.emit(f"Already downloading: {song_name}")
                    return
            # Clear any prior failed state so the UI reflects the retry immediately
            self._mark_result_status_by_id(song_id, "is_failed", False)
            self._mark_result_status_by_id(song_id, "error_message", "")

        self.statusMessage.emit(f"Queuing download: {song_name}")
        self.downloadStarted.emit(song_id, song_name)
        self.downloadQueueChanged.emit()

        self._active_downloads += 1
        self.activeDownloadsChanged.emit(self._active_downloads)

        # Use song_id as the unique key suffix
        self._dl_counter = getattr(self, '_dl_counter', 0) + 1
        future_key = f"dl_{self._dl_counter}_{song_id}"

        future = self._executor.submit(self._do_download, song_data)
        self._pending_futures[future_key] = future

        if not self._poll_timer.isActive():
            self._poll_timer.start()

    # ─── Background work (runs on ThreadPoolExecutor) ─────────────────

    _AUDIO_EXTENSIONS = ('.mp3', '.flac', '.ogg', '.opus', '.m4a')

    def _get_downloaded_files(self) -> Dict[str, str]:
        """Get a dict mapping lowercase filename (no ext) -> folder name across the library."""
        names: Dict[str, str] = {}
        if self._settings_manager:
            media_folder = self._settings_manager.mediaFolder
            if media_folder and os.path.isdir(media_folder):
                try:
                    for root, dirs, files in os.walk(media_folder):
                        # Skip hidden dirs, e.g. the C++ backend's
                        # ".octave-partial" download staging area
                        dirs[:] = [d for d in dirs if not d.startswith('.')]
                        folder = os.path.basename(root)
                        # Root-level files belong to "Unsorted"
                        if os.path.normpath(root) == os.path.normpath(media_folder):
                            folder = "Unsorted"
                        for f in files:
                            if f.lower().endswith(self._AUDIO_EXTENSIONS):
                                names[os.path.splitext(f)[0].lower()] = folder
                except OSError:
                    pass
        # Fallback: also check the download subfolder specifically
        if not names:
            download_path = self._get_download_path()
            folder = os.path.basename(download_path)
            try:
                for f in os.listdir(download_path):
                    if f.lower().endswith(self._AUDIO_EXTENSIONS):
                        names[os.path.splitext(f)[0].lower()] = folder
            except OSError:
                pass
        return names

    @staticmethod
    def _sanitize_for_match(text: str) -> str:
        """Apply the same sanitization music_dl uses for filenames, for matching."""
        # Mirror sanitize_string from music_dl/utils/formatter.py
        text = "".join(c for c in text if c not in "/?\\*|<>")
        text = text.replace('"', "'").replace(":", "-")
        return text.lower()

    def _mark_downloaded(self, results: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """Tag each result with is_downloaded and downloaded_playlist."""
        downloaded = self._get_downloaded_files()
        for r in results:
            artist = r.get('artist', '')
            name = r.get('name', '')
            # Apply the same sanitization that music_dl applies when saving files
            expected = self._sanitize_for_match(f"{artist} - {name}")
            if expected in downloaded:
                r["is_downloaded"] = True
                r["downloaded_playlist"] = downloaded[expected]
            else:
                r["is_downloaded"] = False
                r["downloaded_playlist"] = ""
        return results

    def _do_search(self, query: str, offset: int = 0) -> List[Dict[str, Any]]:
        """Perform search in background thread using YouTube Music (no credentials needed)."""
        from backend.music_dl.utils.search import get_ytm_client

        ytm = get_ytm_client()
        raw = ytm.search(query, filter="songs", limit=20)
        return self._mark_downloaded(self._parse_ytm_results(raw or []))

    @staticmethod
    def _parse_ytm_results(raw: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
        """Convert ytmusicapi song search items into QML search-result dicts."""
        results = []
        for item in raw:
            if item.get("resultType") != "song":
                continue

            video_id = item.get("videoId", "")
            artists = [a.get("name", "") for a in item.get("artists", [])]
            album = item.get("album") or {}
            thumbnails = item.get("thumbnails", [])
            cover_url = thumbnails[-1].get("url", "") if thumbnails else ""
            # YTMusic thumbnails max out at 120x120; request high-res from Google's CDN
            if cover_url and re.search(r"(lh3|yt3)\.googleusercontent\.com", cover_url):
                cover_url = re.sub(r"=w\d+-h\d+.*$", "=w800-h800-l90-rj", cover_url)

            # Parse duration string "M:SS" or "H:MM:SS" to seconds
            duration_str = item.get("duration", "")
            duration_secs = 0
            if duration_str:
                parts = duration_str.split(":")
                try:
                    if len(parts) == 2:
                        duration_secs = int(parts[0]) * 60 + int(parts[1])
                    elif len(parts) == 3:
                        duration_secs = int(parts[0]) * 3600 + int(parts[1]) * 60 + int(parts[2])
                except (ValueError, IndexError):
                    pass

            year = 0
            if item.get("year"):
                try:
                    year = int(item["year"])
                except (ValueError, TypeError):
                    pass

            result = {
                "name": item.get("title", ""),
                "artist": artists[0] if artists else "",
                "artists": artists,
                "album_name": album.get("name", ""),
                "duration": duration_secs,
                "year": year,
                "cover_url": cover_url,
                "url": f"https://music.youtube.com/watch?v={video_id}" if video_id else "",
                "song_id": video_id,
                "explicit": item.get("isExplicit", False),
                "track_number": 0,
                "disc_number": 0,
            }
            results.append(result)

        return results

    def _do_download(self, song_data: Dict[str, Any]) -> Dict[str, Any]:
        """Perform single song download in background thread."""
        from backend.music_dl.types.song import Song

        song_id = song_data.get("song_id", "")

        url = song_data.get("url", "")
        if not all(k in song_data for k in ["name", "artists", "artist"]):
            raise ValueError(f"Cannot build song from data: {song_data}")
        song = Song.from_missing_data(**song_data)
        # A YouTube Music URL goes straight to yt-dlp instead of searching
        if url and ("music.youtube.com" in url or "youtube.com" in url):
            song.download_url = url

        result_song, path = self._music_dl.download(song)
        return {
            "song_id": song_id,
            "song_name": result_song.display_name,
            "path": str(path) if path else None,
            "success": path is not None,
        }

    # ─── QTimer polling (main thread, safe for Qt signals) ────────────

    def _poll_futures(self):
        """Check pending futures AND drain progress queue from main thread."""

        # 1) Drain progress updates (thread-safe)
        with self._progress_lock:
            updates = self._progress_updates[:]
            self._progress_updates.clear()

        for song_name, progress in updates:
            self.downloadProgress.emit(song_name, progress)

        # 2) Check completed futures
        completed_keys = []

        for key, future in self._pending_futures.items():
            if not future.done():
                continue

            completed_keys.append(key)

            try:
                result = future.result()

                if key == "__search__":
                    self._handle_search_result(result)
                elif key.startswith("dl_"):
                    self._handle_download_result(result)

            except Exception as exc:
                logger.error("Future %s failed: %s", key, exc)
                logger.debug(traceback.format_exc())

                if key == "__search__":
                    self._is_searching = False
                    self.isSearchingChanged.emit(False)
                    self.searchInProgress.emit(False)
                    self.searchError.emit(str(exc))
                    self.statusMessage.emit(f"Search failed: {exc}")
                elif key.startswith("dl_"):
                    # Key format: "dl_{counter}_{song_id}"
                    song_id = key.split("_", 2)[2] if key.count("_") >= 2 else ""
                    error_msg = str(exc)
                    self.downloadError.emit(song_id, "", error_msg)
                    self._active_downloads = max(0, self._active_downloads - 1)
                    self.activeDownloadsChanged.emit(self._active_downloads)
                    self.statusMessage.emit(f"Download failed")
                    self._mark_result_status_by_id(song_id, "is_failed", True)
                    self._mark_result_status_by_id(song_id, "error_message", error_msg)

        for key in completed_keys:
            del self._pending_futures[key]

        # Stop timer when no more pending work AND no progress to drain
        if not self._pending_futures and not self._progress_updates:
            self._poll_timer.stop()

    def _handle_search_result(self, results: List[Dict[str, Any]]):
        """Handle completed search — emit results to QML."""
        self._is_searching = False
        self.isSearchingChanged.emit(False)
        self.searchInProgress.emit(False)

        # Accumulate results (append for "load more", replace for new search)
        if self._search_offset == 0:
            self._all_results = results
        else:
            # Deduplicate by song_id
            existing_ids = {r.get("song_id") for r in self._all_results}
            for r in results:
                if r.get("song_id") not in existing_ids:
                    self._all_results.append(r)
                    existing_ids.add(r.get("song_id"))

        # Advance offset for next "load more"
        self._search_offset += len(results)

        self.searchResults.emit(json.dumps(self._all_results))
        self.statusMessage.emit(f"Showing {len(self._all_results)} result(s)")

        # Store results for download_song lookup
        self._last_search_results = self._all_results

    def _mark_result_status_by_id(self, song_id: str, field: str, value: Any):
        """Set a field on the matching search result by song_id and notify QML."""
        if not self._all_results or not song_id:
            return
        for r in self._all_results:
            if r.get("song_id") == song_id:
                r[field] = value
                break
        # Emit targeted patch so QML can update in-place without rebuilding the ListView
        self.downloadStatusChanged.emit(song_id, field, json.dumps(value))

    def _mark_result_status(self, song_name: str, field: str, value: Any):
        """Set a field on the matching search result by name and notify QML."""
        if not self._all_results:
            return
        sanitized_name = self._sanitize_for_match(song_name)
        for r in self._all_results:
            expected = self._sanitize_for_match(
                f"{r.get('artist', '')} - {r.get('name', '')}"
            )
            if expected == sanitized_name:
                r[field] = value
                song_id = r.get("song_id", "")
                if song_id:
                    self.downloadStatusChanged.emit(song_id, field, json.dumps(value))
                break

    def _handle_download_result(self, result: Dict[str, Any]):
        """Handle completed download."""
        song_id = result.get("song_id", "")
        song_name = result.get("song_name", "Unknown")
        path = result.get("path")
        success = result.get("success", False)

        self._active_downloads = max(0, self._active_downloads - 1)
        self.activeDownloadsChanged.emit(self._active_downloads)

        if success and path:
            self.downloadComplete.emit(song_id, path)
            self.statusMessage.emit(f"Downloaded: {song_name}")
            logger.info("Download complete: %s -> %s", song_name, path)
            self._mark_result_status_by_id(song_id, "is_downloaded", True)
        else:
            error_msg = "Download failed — no audio source found"
            self.downloadError.emit(song_id, song_name, error_msg)
            self.statusMessage.emit(f"Failed: {song_name}")
            logger.warning("Download failed: %s", song_name)
            self._mark_result_status_by_id(song_id, "is_failed", True)
            self._mark_result_status_by_id(song_id, "error_message", error_msg)
        self.downloadQueueChanged.emit()

    # ─── Utility Slots for QML ────────────────────────────────────────

    @Slot(str, result=str)
    def get_song_data_for_download(self, song_url: str) -> str:
        """
        Get the full song data JSON for a song from the last search results.
        Used by QML to pass full data to download_song().
        """
        if not hasattr(self, "_last_search_results"):
            return ""

        for result in self._last_search_results:
            if result.get("url") == song_url:
                full_data = result.get("_full_data", result)
                return json.dumps(full_data)

        return ""

    # ─── Cleanup ──────────────────────────────────────────────────────

    def cleanup(self):
        """Clean up resources on app exit."""
        self._poll_timer.stop()
        self._executor.shutdown(wait=False)
        if self._music_dl:
            try:
                self._music_dl.cleanup()
            except Exception:
                pass
        logger.info("DownloadManager cleaned up")
