"""
Network Manager for OCTAVE
Handles WiFi/internet connectivity status and checks for updates via GitHub.
"""

import os
import sys
import subprocess
import platform
import urllib.request
import json
import threading
from concurrent.futures import ThreadPoolExecutor

from PySide6.QtCore import QObject, Signal, Slot, QTimer, Property

from backend.logging_config import get_logger
logger = get_logger(__name__)


class NetworkManager(QObject):
    """
    Manager for network connectivity status and update checking.

    Provides:
    - Internet connectivity check
    - WiFi/network name detection
    - GitHub-based update checking (compares local commit to remote HEAD)

    All blocking I/O (subprocess, HTTP) runs on a background thread to avoid
    freezing the Qt event loop.
    """

    # Network status signals
    networkNameChanged = Signal(str)
    isConnectedChanged = Signal(bool)

    # Update check signals
    updateStatusChanged = Signal(str)       # "checking", "up-to-date", "update-available", "error"
    updateMessageChanged = Signal(str)      # Human-readable status message
    remoteCommitChanged = Signal(str)       # Latest remote commit hash
    remoteCommitMsgChanged = Signal(str)    # Latest remote commit message
    workingTreeDirtyChanged = Signal(bool)  # Checkout has uncommitted changes

    # Self-update signals
    selfUpdateStatusChanged = Signal(str)   # "idle", "fetching", "error", "restart-required"
    selfUpdateMessageChanged = Signal(str)  # Human-readable progress message
    canSelfUpdateChanged = Signal(bool)     # Whether git-based self-update is possible

    GITHUB_REPO = "WayBetterSolutions/OCTAVE"

    def __init__(self):
        super().__init__()
        self._network_name = ""
        self._is_connected = False
        self._update_status = ""
        self._working_tree_dirty = False
        self._update_message = ""
        self._remote_commit = ""
        self._remote_commit_msg = ""

        # Self-update state
        self._self_update_status = "idle"
        self._self_update_message = ""
        self._can_self_update = False   # probed 2 s after startup (spawns git), see below
        self._progress_lock = threading.Lock()
        self._progress_message = None
        self._updating = False  # Prevents double-tap
        self._auto_update_checked = False  # One-shot auto-check on first connectivity

        self._executor = ThreadPoolExecutor(max_workers=1)
        self._pending_future = None

        # Poll for completed background work (avoids emitting signals from worker thread)
        self._result_poll_timer = QTimer()
        self._result_poll_timer.setInterval(250)
        self._result_poll_timer.timeout.connect(self._check_pending_result)

        # Poll network status periodically: every 30 s while online, every
        # 2 min after a few consecutive misses (a car with no internet should
        # not pay for an HTTPS timeout twice a minute forever).
        self._offline_streak = 0
        self._poll_timer = QTimer()
        self._poll_timer.timeout.connect(self._refresh_network_status)
        self._poll_timer.start(self.POLL_ONLINE_MS)

        # Check once on startup (slight delay to let the UI load)
        QTimer.singleShot(2000, self._startup_probe)

    POLL_ONLINE_MS = 30000
    POLL_OFFLINE_MS = 120000
    OFFLINE_STREAK_FOR_BACKOFF = 3

    def _startup_probe(self):
        can = self._check_can_self_update()
        if can != self._can_self_update:
            self._can_self_update = can
            self.canSelfUpdateChanged.emit(can)
        self._refresh_network_status()

    # ==================== Properties ====================

    @Property(str, notify=networkNameChanged)
    def networkName(self):
        return self._network_name

    @Property(bool, notify=isConnectedChanged)
    def isConnected(self):
        return self._is_connected

    @Property(str, notify=updateStatusChanged)
    def updateStatus(self):
        return self._update_status

    @Property(str, notify=updateMessageChanged)
    def updateMessage(self):
        return self._update_message

    @Property(str, notify=remoteCommitChanged)
    def remoteCommit(self):
        return self._remote_commit

    @Property(bool, notify=workingTreeDirtyChanged)
    def workingTreeDirty(self):
        # Self-update hard-resets the tree, so the UI must be able to hide that
        # button before it eats real work.
        return self._working_tree_dirty

    @Property(str, notify=remoteCommitMsgChanged)
    def remoteCommitMsg(self):
        return self._remote_commit_msg

    @Property(str, notify=selfUpdateStatusChanged)
    def selfUpdateStatus(self):
        return self._self_update_status

    @Property(str, notify=selfUpdateMessageChanged)
    def selfUpdateMessage(self):
        return self._self_update_message

    @Property(bool, notify=canSelfUpdateChanged)
    def canSelfUpdate(self):
        return self._can_self_update

    # ==================== Background work polling ====================

    def _submit_work(self, fn, result_type):
        """Submit blocking work to the thread pool. Only one job at a time."""
        if self._pending_future and not self._pending_future.done():
            return  # Already busy
        self._pending_future = self._executor.submit(fn)
        self._pending_future._result_type = result_type
        self._result_poll_timer.start()

    def _check_pending_result(self):
        """Poll for completed background work and emit signals on the main thread."""
        # Relay intermediate progress messages from worker thread
        with self._progress_lock:
            msg = self._progress_message
            self._progress_message = None
        if msg is not None:
            self._self_update_message = msg
            self.selfUpdateMessageChanged.emit(msg)

        if not self._pending_future or not self._pending_future.done():
            return
        self._result_poll_timer.stop()
        result_type = getattr(self._pending_future, '_result_type', None)
        try:
            result = self._pending_future.result()
        except Exception as e:
            logger.warning(f"Background network task failed: {e}")
            if result_type == "update_check":
                self._update_status = "error"
                self.updateStatusChanged.emit("error")
                self._update_message = "Could not reach GitHub"
                self.updateMessageChanged.emit(self._update_message)
            elif result_type == "self_update":
                self._self_update_status = "error"
                self.selfUpdateStatusChanged.emit("error")
                self._self_update_message = "Update failed unexpectedly"
                self.selfUpdateMessageChanged.emit(self._self_update_message)
            self._pending_future = None
            return

        self._pending_future = None

        if result_type == "network_status":
            name, connected = result
            if name != self._network_name:
                self._network_name = name
                self.networkNameChanged.emit(name)
            if connected != self._is_connected:
                self._is_connected = connected
                self.isConnectedChanged.emit(connected)
            self._offline_streak = 0 if connected else self._offline_streak + 1
            wanted = self.POLL_OFFLINE_MS if self._offline_streak >= self.OFFLINE_STREAK_FOR_BACKOFF else self.POLL_ONLINE_MS
            if self._poll_timer.interval() != wanted:
                self._poll_timer.setInterval(wanted)
            # Auto-check for updates once on first successful connectivity
            if connected and not self._auto_update_checked:
                self._auto_update_checked = True
                QTimer.singleShot(500, self.checkForUpdates)

        elif result_type == "update_check":
            self._apply_update_result(result)

        elif result_type == "self_update":
            self._apply_self_update_result(result)

    # ==================== Network Status ====================

    def _refresh_network_status(self):
        """Kick off a background network status check."""
        self._submit_work(self._do_network_check, "network_status")

    @staticmethod
    def _do_network_check():
        """Blocking network check — runs on worker thread."""
        name = NetworkManager._get_network_name_sync()
        connected = NetworkManager._check_internet_sync()
        return (name, connected)

    @staticmethod
    def _get_network_name_sync():
        """Get the name of the connected WiFi network (or wired indicator)."""
        system = platform.system()
        try:
            if system == "Linux":
                result = subprocess.run(
                    ['nmcli', '-t', '-f', 'ACTIVE,SSID', 'dev', 'wifi'],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0:
                    for line in result.stdout.strip().split('\n'):
                        if line.startswith('yes:'):
                            ssid = line.split(':', 1)[1]
                            if ssid:
                                return ssid
                # Check for wired connection
                result = subprocess.run(
                    ['nmcli', '-t', '-f', 'TYPE,STATE', 'dev'],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0:
                    for line in result.stdout.strip().split('\n'):
                        if 'ethernet' in line and 'connected' in line:
                            return "Ethernet"

            elif system == "Darwin":
                result = subprocess.run(
                    ['networksetup', '-getairportnetwork', 'en0'],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0 and 'Current Wi-Fi Network' in result.stdout:
                    return result.stdout.split(': ', 1)[1].strip()
                result = subprocess.run(
                    ['ifconfig', 'en0'],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0 and 'status: active' in result.stdout:
                    return "Ethernet"

            elif system == "Windows":
                result = subprocess.run(
                    ['netsh', 'wlan', 'show', 'interfaces'],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0:
                    for line in result.stdout.split('\n'):
                        line = line.strip()
                        if line.startswith('SSID') and 'BSSID' not in line:
                            ssid = line.split(':', 1)[1].strip()
                            if ssid:
                                return ssid
                result = subprocess.run(
                    ['netsh', 'interface', 'show', 'interface'],
                    capture_output=True, text=True, timeout=5
                )
                if result.returncode == 0:
                    for line in result.stdout.split('\n'):
                        if 'Connected' in line and 'Ethernet' in line:
                            return "Ethernet"

        except Exception as e:
            logger.debug(f"Error getting network name: {e}")

        return ""

    @staticmethod
    def _check_internet_sync():
        """Check if the device has internet connectivity."""
        try:
            urllib.request.urlopen("https://clients3.google.com/generate_204", timeout=5)
            return True
        except Exception:
            pass
        return False

    @Slot()
    def refreshNetwork(self):
        """Manually refresh network status (callable from QML)."""
        self._refresh_network_status()

    # ==================== Update Checking ====================

    @Slot()
    def checkForUpdates(self):
        """Start an update check (callable from QML)."""
        self._update_status = "checking"
        self.updateStatusChanged.emit("checking")
        self._update_message = "Checking for updates..."
        self.updateMessageChanged.emit(self._update_message)
        self._submit_work(self._do_update_check_sync, "update_check")

    def _do_update_check_sync(self):
        """Blocking update check — runs on worker thread. Returns a result dict."""
        # Check connectivity first
        connected = self._check_internet_sync()
        if not connected:
            return {"status": "error", "message": "No internet connection"}

        try:
            local_hash = self._get_local_commit_hash_sync()
            if not local_hash:
                return {"status": "error", "message": "Could not determine local version"}

            url = f"https://api.github.com/repos/{self.GITHUB_REPO}/commits/main"
            req = urllib.request.Request(url)
            req.add_header('User-Agent', 'OCTAVE-Updater')
            resp = urllib.request.urlopen(req, timeout=10)
            data = json.loads(resp.read().decode())

            remote_hash = data.get("sha", "")[:7]
            remote_msg = data.get("commit", {}).get("message", "").split('\n')[0]

            relation = self._local_vs_remote_sync(remote_hash, local_hash)
            dirty = self._working_tree_dirty_sync()

            if relation == "same":
                status = "up-to-date"
                message = "You're running the latest version"
            elif relation == "ahead":
                status = "ahead"
                message = "Dev build — ahead of origin/main"
            elif relation == "diverged":
                status = "diverged"
                message = "Local branch has diverged from origin/main"
            else:
                status = "update-available"
                message = f"Update available (latest: {remote_hash})"

            logger.info(
                f"Update check: local={local_hash}, remote={remote_hash}, "
                f"relation={relation}, dirty={dirty}, status={status}"
            )
            return {
                "status": status,
                "message": message,
                "remote_hash": remote_hash,
                "remote_msg": remote_msg,
                "dirty": dirty,
            }

        except Exception as e:
            logger.warning(f"Update check failed: {e}")
            return {"status": "error", "message": "Could not reach GitHub"}

    def _apply_update_result(self, result):
        """Apply update check result on main thread."""
        status = result.get("status", "error")
        message = result.get("message", "")

        self._update_status = status
        self.updateStatusChanged.emit(status)
        self._update_message = message
        self.updateMessageChanged.emit(message)

        remote_hash = result.get("remote_hash")
        if remote_hash is not None:
            self._remote_commit = remote_hash
            self.remoteCommitChanged.emit(remote_hash)

        remote_msg = result.get("remote_msg")
        if remote_msg is not None:
            self._remote_commit_msg = remote_msg
            self.remoteCommitMsgChanged.emit(remote_msg)

        dirty = result.get("dirty")
        if dirty is not None and dirty != self._working_tree_dirty:
            self._working_tree_dirty = dirty
            self.workingTreeDirtyChanged.emit(dirty)

    @staticmethod
    def _repo_dir():
        return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    @classmethod
    def _run_git_sync(cls, args, timeout=10):
        """Run a short git query. Returns (ok, stdout-stripped)."""
        try:
            result = subprocess.run(
                ['git'] + args,
                cwd=cls._repo_dir(),
                capture_output=True, text=True, timeout=timeout
            )
            return result.returncode == 0, result.stdout.strip()
        except Exception as e:
            logger.debug(f"git {' '.join(args)} failed: {e}")
            return False, ""

    @classmethod
    def _working_tree_dirty_sync(cls):
        """True when the checkout has uncommitted changes.

        On failure assume dirty: this gates a `git reset --hard`, so the safe
        default when we cannot tell is the one that refuses to destroy anything.
        """
        ok, out = cls._run_git_sync(['status', '--porcelain'])
        return bool(out) if ok else True

    @classmethod
    def _local_vs_remote_sync(cls, remote_short, local_short):
        """Classify local HEAD against the remote commit: same/behind/ahead/diverged.

        A plain `remote != local` test cannot tell "behind" from "ahead": running
        from a source checkout with local commits reported an update to a commit
        *older* than HEAD, and self-update would then hard-reset onto it.
        """
        if not remote_short:
            return "behind"
        if remote_short == local_short:
            return "same"

        # Do we even have the remote commit locally? If the object is missing we
        # cannot be a descendant of it, so this is a normal "remote moved ahead".
        have_remote, _ = cls._run_git_sync(['cat-file', '-e', f'{remote_short}^{{commit}}'], timeout=5)
        if have_remote:
            is_ancestor, _ = cls._run_git_sync(
                ['merge-base', '--is-ancestor', remote_short, 'HEAD'], timeout=5)
            if is_ancestor:
                return "ahead"  # remote commit is already in our history

        # Remote commit is unknown to us. If we also carry commits the last-fetched
        # origin/main lacks, both sides moved — that is a divergence, not an update.
        ok, count = cls._run_git_sync(['rev-list', '--count', 'origin/main..HEAD'], timeout=5)
        if ok and count.isdigit() and int(count) > 0:
            return "diverged"

        return "behind"

    @staticmethod
    def _get_local_commit_hash_sync():
        """Get the short hash of the local HEAD commit."""
        try:
            backend_dir = os.path.dirname(os.path.abspath(__file__))
            repo_dir = os.path.dirname(backend_dir)
            result = subprocess.run(
                ['git', 'rev-parse', '--short', 'HEAD'],
                cwd=repo_dir,
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                return result.stdout.strip()
        except Exception as e:
            logger.debug(f"Error getting local commit hash: {e}")
        return ""

    # ==================== Self-Update ====================

    @staticmethod
    def _check_can_self_update():
        """Check if self-update is possible (requires git, not a frozen PyInstaller build)."""
        if getattr(sys, 'frozen', False):
            return False
        try:
            result = subprocess.run(
                ['git', '--version'],
                capture_output=True, text=True, timeout=5
            )
            return result.returncode == 0
        except Exception:
            return False

    @Slot()
    def applySelfUpdate(self):
        """Start the self-update process (callable from QML)."""
        if not self._can_self_update:
            return
        # Hard lock — prevents double-tap even if UI is slow to update
        if self._updating:
            return
        if self._pending_future and not self._pending_future.done():
            return

        # Step 2 of this process is `git reset --hard origin/main`. Re-verify the
        # preconditions right here rather than trusting the cached update status:
        # the tree can be dirtied, or commits made, long after the last check ran.
        dirty = self._working_tree_dirty_sync()
        if dirty != self._working_tree_dirty:
            self._working_tree_dirty = dirty
            self.workingTreeDirtyChanged.emit(dirty)
        if dirty:
            logger.warning("Self-update refused: working tree has uncommitted changes")
            self._self_update_status = "error"
            self.selfUpdateStatusChanged.emit("error")
            self._self_update_message = "Uncommitted changes — commit or stash them first"
            self.selfUpdateMessageChanged.emit(self._self_update_message)
            return
        if self._update_status in ("ahead", "diverged"):
            logger.warning(
                f"Self-update refused: local branch is {self._update_status} "
                f"relative to origin/main"
            )
            self._self_update_status = "error"
            self.selfUpdateStatusChanged.emit("error")
            self._self_update_message = "Local commits would be lost — refusing to reset"
            self.selfUpdateMessageChanged.emit(self._self_update_message)
            return

        self._updating = True

        self._self_update_status = "fetching"
        self.selfUpdateStatusChanged.emit("fetching")
        self._self_update_message = "Fetching latest changes..."
        self.selfUpdateMessageChanged.emit(self._self_update_message)
        self._submit_work(self._do_self_update_sync, "self_update")

    def _set_progress(self, msg):
        """Thread-safe progress message relay."""
        with self._progress_lock:
            self._progress_message = msg

    def _do_self_update_sync(self):
        """Blocking self-update — runs on worker thread. Returns a result dict."""
        backend_dir = os.path.dirname(os.path.abspath(__file__))
        repo_dir = os.path.dirname(backend_dir)
        old_commit = None

        try:
            # Step 0: Save current commit hash for rollback
            self._set_progress("Saving rollback point...")
            result = subprocess.run(
                ['git', 'rev-parse', 'HEAD'],
                cwd=repo_dir,
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                old_commit = result.stdout.strip()
                logger.info(f"Rollback point saved: {old_commit[:7]}")

            # Step 1: git fetch origin main
            self._set_progress("Fetching latest changes...")
            result = subprocess.run(
                ['git', 'fetch', 'origin', 'main'],
                cwd=repo_dir,
                capture_output=True, text=True, timeout=60
            )
            if result.returncode != 0:
                return {
                    "status": "error",
                    "message": f"Fetch failed: {result.stderr.strip() or 'unknown error'}"
                }

            # Step 2: git reset --hard origin/main
            self._set_progress("Applying update...")
            result = subprocess.run(
                ['git', 'reset', '--hard', 'origin/main'],
                cwd=repo_dir,
                capture_output=True, text=True, timeout=30
            )
            if result.returncode != 0:
                return {
                    "status": "error",
                    "message": f"Apply failed: {result.stderr.strip() or 'unknown error'}"
                }

            # Step 3: Pre-flight check — verify the new code can at least be parsed
            self._set_progress("Verifying new code...")
            main_py = os.path.join(repo_dir, "main.py")
            if os.path.exists(main_py):
                check = subprocess.run(
                    [sys.executable, '-m', 'py_compile', main_py],
                    cwd=repo_dir,
                    capture_output=True, text=True, timeout=15
                )
                if check.returncode != 0:
                    logger.error(f"Pre-flight check failed: {check.stderr.strip()}")
                    # Roll back — new code has syntax errors
                    if old_commit:
                        self._set_progress("New code has errors, rolling back...")
                        subprocess.run(
                            ['git', 'reset', '--hard', old_commit],
                            cwd=repo_dir,
                            capture_output=True, text=True, timeout=30
                        )
                        logger.info(f"Rolled back to {old_commit[:7]}")
                    return {
                        "status": "error",
                        "message": "Update rolled back — new code failed syntax check"
                    }

            # Step 4: Install/update dependencies from the new requirements.txt
            self._set_progress("Installing dependencies...")
            pip_path = self._get_venv_pip_path(repo_dir)
            requirements_path = os.path.join(repo_dir, "requirements.txt")
            dep_warning = None
            if pip_path and os.path.exists(requirements_path):
                logger.info("Installing dependencies after update...")
                result = subprocess.run(
                    [pip_path, "install", "-r", requirements_path],
                    cwd=repo_dir,
                    capture_output=True, text=True, timeout=600
                )
                if result.returncode != 0:
                    # Log but don't block — the code update already succeeded,
                    # and most deps are likely already installed from the prior version.
                    logger.warning(f"Dependency install had issues: {result.stderr.strip()}")
                    dep_warning = result.stderr.strip()[:120]
                else:
                    logger.info("Dependencies installed successfully")
            else:
                logger.warning("Could not locate venv pip or requirements.txt — skipping dependency install")

            # Step 5: Get new commit info for confirmation
            result = subprocess.run(
                ['git', 'log', '-1', '--format=%h %s'],
                cwd=repo_dir,
                capture_output=True, text=True, timeout=5
            )
            new_info = result.stdout.strip() if result.returncode == 0 else ""

            logger.info(f"Self-update applied successfully: {new_info}")
            msg = f"Update applied. Now on: {new_info}"
            if dep_warning:
                msg += "\n(Some dependencies may need manual install)"
            return {
                "status": "restart-required",
                "message": msg
            }

        except subprocess.TimeoutExpired:
            # Roll back on timeout — don't leave in a half-updated state
            if old_commit:
                try:
                    subprocess.run(
                        ['git', 'reset', '--hard', old_commit],
                        cwd=repo_dir,
                        capture_output=True, text=True, timeout=30
                    )
                    logger.info(f"Rolled back to {old_commit[:7]} after timeout")
                except Exception:
                    pass
            return {"status": "error", "message": "Update timed out and was rolled back"}
        except Exception as e:
            logger.warning(f"Self-update failed: {e}")
            # Roll back on any unexpected error
            if old_commit:
                try:
                    subprocess.run(
                        ['git', 'reset', '--hard', old_commit],
                        cwd=repo_dir,
                        capture_output=True, text=True, timeout=30
                    )
                    logger.info(f"Rolled back to {old_commit[:7]} after error")
                except Exception:
                    pass
            return {"status": "error", "message": f"Update failed and was rolled back: {str(e)}"}

    @staticmethod
    def _get_venv_pip_path(repo_dir):
        """Locate the pip executable inside the project's venv."""
        venv_dir = os.path.join(repo_dir, "venv")
        if platform.system() == "Windows":
            pip_path = os.path.join(venv_dir, "Scripts", "pip.exe")
        else:
            pip_path = os.path.join(venv_dir, "bin", "pip")
        if os.path.exists(pip_path):
            return pip_path
        return None

    def _apply_self_update_result(self, result):
        """Apply self-update result on main thread."""
        status = result.get("status", "error")
        message = result.get("message", "")

        self._self_update_status = status
        self.selfUpdateStatusChanged.emit(status)
        self._self_update_message = message
        self.selfUpdateMessageChanged.emit(message)

        # Clear the update lock
        self._updating = False

        # If successful, also update the main update status
        if status == "restart-required":
            self._update_status = "up-to-date"
            self.updateStatusChanged.emit("up-to-date")
            self._update_message = "Update applied — restart to complete"
            self.updateMessageChanged.emit(self._update_message)

    @Slot()
    def restartApp(self):
        """Restart the OCTAVE application (callable from QML)."""
        logger.info("App restart requested")

        from PySide6.QtWidgets import QApplication
        qapp = QApplication.instance()

        python_path = sys.executable
        repo_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        main_py = os.path.join(repo_dir, "main.py")
        args = [python_path, main_py] + sys.argv[1:]

        # Pre-flight: verify main.py can be parsed before restarting into it
        try:
            check = subprocess.run(
                [python_path, '-m', 'py_compile', main_py],
                capture_output=True, text=True, timeout=15
            )
            if check.returncode != 0:
                logger.error(f"Restart aborted — main.py has errors: {check.stderr.strip()}")
                self._self_update_status = "error"
                self.selfUpdateStatusChanged.emit("error")
                self._self_update_message = "Restart aborted — code has errors"
                self.selfUpdateMessageChanged.emit(self._self_update_message)
                return
        except Exception as e:
            logger.warning(f"Pre-flight check skipped: {e}")

        system = platform.system()
        logger.info(f"Restarting on {system}: {args}")

        if system == "Windows":
            subprocess.Popen(args)
            qapp.quit()
        else:
            # On Unix, os.execv replaces the process after cleanup runs
            qapp.aboutToQuit.connect(lambda: os.execv(python_path, args))
            qapp.quit()

    # ==================== Device Shutdown ====================

    @Slot()
    def shutdown_device(self):
        """Shut down the device cleanly: quit OCTAVE, then power off the OS."""
        system = platform.system()
        logger.info(f"Device shutdown requested on {system}")

        # Schedule OS shutdown with a brief delay so app cleanup can finish
        try:
            if system == "Linux":
                subprocess.Popen(['systemctl', '--no-block', 'poweroff'])
            elif system == "Darwin":
                subprocess.Popen(['sudo', 'shutdown', '-h', '+0'])
            elif system == "Windows":
                subprocess.Popen(['shutdown', '/s', '/t', '5'])
        except Exception as e:
            logger.error(f"Failed to schedule OS shutdown: {e}")

        # Quit the app (triggers cleanup_on_quit via aboutToQuit signal)
        from PySide6.QtWidgets import QApplication
        QApplication.quit()

    def cleanup(self):
        """Stop timers and thread pool on shutdown."""
        self._poll_timer.stop()
        self._result_poll_timer.stop()
        self._executor.shutdown(wait=False)
