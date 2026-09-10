"""Field diagnostics: lets the user read and get the logs out of the head
unit without a laptop (Settings > About > Diagnostics). Mirrors
src/managers/diagnosticsmanager.{h,cpp}. The log files themselves come from
backend/logging_config.py (Python) / src/util/logger.cpp (C++)."""

import os
import platform
import shutil
from datetime import datetime

from PySide6.QtCore import QObject, Property, Signal, Slot, QStandardPaths, QUrl
from PySide6.QtGui import QDesktopServices, QGuiApplication

from backend.logging_config import get_logger
from backend.settings_manager import get_app_data_dir
from backend.version import __version__

logger = get_logger(__name__)


def _tail(path: str, max_lines: int) -> str:
    try:
        with open(path, "rb") as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            window = 256 * 1024   # enough for a few hundred lines
            f.seek(max(0, size - window))
            text = f.read().decode("utf-8", errors="replace")
    except OSError:
        return ""
    lines = [ln for ln in text.split("\n") if ln]
    return "\n".join(lines[-max_lines:])


class DiagnosticsManager(QObject):
    lastExportPathChanged = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._last_export_path = ""

    @Property(str, constant=True)
    def logDir(self) -> str:
        return os.path.join(get_app_data_dir(), "logs")

    @Property(str, constant=True)
    def appVersion(self) -> str:
        return __version__

    @Property(str, constant=True)
    def backendName(self) -> str:
        return "Python"

    @Property(str, constant=True)
    def deviceInfo(self) -> str:
        return f"{platform.system()} {platform.release()} {platform.machine()} ({platform.node()}, Python {platform.python_version()})"

    @Property(str, notify=lastExportPathChanged)
    def lastExportPath(self) -> str:
        """Where exportLogs() last copied the files ("" until it has run)"""
        return self._last_export_path

    @Property(bool, constant=True)
    def canOpenFolder(self) -> bool:
        """Desktop only: the OS file manager can open the log folder"""
        return True

    def _log_files(self) -> list:
        d = self.logDir
        try:
            names = sorted(n for n in os.listdir(d) if n.startswith("octave") and ".log" in n)
        except OSError:
            return []
        return [os.path.join(d, n) for n in names]

    @Slot(int, result=str)
    def recentLogLines(self, max_lines: int = 200) -> str:
        """Last max_lines lines of the main log followed by the error log"""
        d = self.logDir
        main = _tail(os.path.join(d, "octave.log"), max_lines)
        errors = _tail(os.path.join(d, "octave-error.log"), max(1, max_lines // 4))
        out = main or "(no log yet)"
        if errors:
            out += "\n\n---- errors ----\n" + errors
        return out

    @Slot(int, result=bool)
    def copyLogsToClipboard(self, max_lines: int = 400) -> bool:
        """recentLogLines() plus a header (version, device) onto the clipboard"""
        cb = QGuiApplication.clipboard()
        if cb is None:
            return False
        cb.setText(f"OCTAVE {self.appVersion} ({self.backendName} backend) on {self.deviceInfo}\n"
                   f"{datetime.now().isoformat(timespec='seconds')}\n\n{self.recentLogLines(max_lines)}")
        logger.info("Logs copied to clipboard")
        return True

    @Slot(result=str)
    def exportLogs(self) -> str:
        """Copy every log file (current + rotated) into
        <Downloads>/OCTAVE-logs-<timestamp>/ where any app can attach them.
        Returns the folder, "" on failure."""
        base = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.DownloadLocation) \
            or QStandardPaths.writableLocation(QStandardPaths.StandardLocation.DocumentsLocation)
        target = os.path.join(base, "OCTAVE-logs-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
        try:
            os.makedirs(target, exist_ok=True)
            copied = 0
            for src in self._log_files():
                shutil.copy2(src, target)
                copied += 1
            with open(os.path.join(target, "device-info.txt"), "w", encoding="utf-8") as f:
                f.write(f"OCTAVE {self.appVersion} ({self.backendName} backend)\n{self.deviceInfo}\n"
                        f"exported {datetime.now().isoformat(timespec='seconds')}\n")
        except OSError as e:
            logger.warning(f"Could not export logs to {target}: {e}")
            return ""
        logger.info(f"Exported {copied} log files to {target}")
        self._last_export_path = target
        self.lastExportPathChanged.emit()
        return target

    @Slot(result=bool)
    def openLogFolder(self) -> bool:
        """Desktop: open the log folder in the file manager"""
        return bool(QDesktopServices.openUrl(QUrl.fromLocalFile(self.logDir)))
