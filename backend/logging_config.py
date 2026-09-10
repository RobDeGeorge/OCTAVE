"""
Centralized logging configuration for OCTAVE.

Usage:
    from backend.logging_config import setup_logging, get_logger

    # In main.py (once at startup):
    setup_logging(debug=False)

    # In any module:
    logger = get_logger(__name__)
    logger.info("Application started")
"""

import logging
import logging.handlers
import os
import sys

# Module-level logger cache
_loggers = {}
_initialized = False


def _get_log_dir():
    """Get the logs directory, creating it if needed."""
    app_name = "OCTAVE"

    if sys.platform == 'win32':
        base = os.environ.get('APPDATA', os.path.expanduser('~'))
        data_dir = os.path.join(base, app_name)
    elif sys.platform == 'darwin':
        data_dir = os.path.join(os.path.expanduser('~'), 'Library', 'Application Support', app_name)
    else:
        base = os.environ.get('XDG_CONFIG_HOME', os.path.join(os.path.expanduser('~'), '.config'))
        data_dir = os.path.join(base, app_name)

    log_dir = os.path.join(data_dir, 'logs')
    os.makedirs(log_dir, exist_ok=True)
    return log_dir


def setup_logging(debug: bool = False, console: bool = True, file: bool = True):
    """
    Initialize the logging system. Call once at application startup.

    Logs are persisted across sessions with automatic rotation to prevent
    disk space issues. Good for embedded/vehicle use where you need to
    debug issues after the fact.

    Args:
        debug: Enable DEBUG level logging (verbose)
        console: Enable console output
        file: Enable file output with rotation

    Log rotation policy:
        - octave.log: 5 MB max, keeps 3 backups (~20 MB total)
        - octave-error.log: 2 MB max, keeps 5 backups (~12 MB total)
        - octave-debug.log: 10 MB max, keeps 2 backups (~30 MB total, only with --debug)

    Total max disk usage: ~62 MB (with debug), ~32 MB (without debug)
    """
    global _initialized
    if _initialized:
        return

    root_logger = logging.getLogger('octave')
    root_logger.setLevel(logging.DEBUG if debug else logging.INFO)

    # Clear any existing handlers
    root_logger.handlers.clear()

    # Formatters
    console_formatter = logging.Formatter(
        '%(asctime)s | %(levelname)-7s | %(name)-25s | %(message)s',
        datefmt='%H:%M:%S'
    )

    file_formatter = logging.Formatter(
        '%(asctime)s | %(levelname)-7s | %(name)s | %(filename)s:%(lineno)d | %(funcName)s() | %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    )

    # Console handler
    if console:
        console_handler = logging.StreamHandler(sys.stdout)
        console_handler.setLevel(logging.DEBUG if debug else logging.INFO)
        console_handler.setFormatter(console_formatter)
        root_logger.addHandler(console_handler)

    # File handlers
    if file:
        log_dir = _get_log_dir()

        # Main log file (INFO and above, rotating)
        # Sized for embedded use - keeps several sessions of logs
        main_log = os.path.join(log_dir, 'octave.log')
        main_handler = logging.handlers.RotatingFileHandler(
            main_log,
            maxBytes=5 * 1024 * 1024,  # 5 MB per file
            backupCount=3,             # Keep 3 backups (~20 MB total)
            encoding='utf-8'
        )
        main_handler.setLevel(logging.INFO)
        main_handler.setFormatter(file_formatter)
        root_logger.addHandler(main_handler)

        # Error log file (ERROR and above only)
        # Kept longer since errors are rare but important for debugging
        error_log = os.path.join(log_dir, 'octave-error.log')
        error_handler = logging.handlers.RotatingFileHandler(
            error_log,
            maxBytes=2 * 1024 * 1024,  # 2 MB per file
            backupCount=5,             # Keep 5 backups (~12 MB total)
            encoding='utf-8'
        )
        error_handler.setLevel(logging.ERROR)
        error_handler.setFormatter(file_formatter)
        root_logger.addHandler(error_handler)

        # Debug log (only when debug mode enabled)
        # Larger since debug output is verbose
        if debug:
            debug_log = os.path.join(log_dir, 'octave-debug.log')
            debug_handler = logging.handlers.RotatingFileHandler(
                debug_log,
                maxBytes=10 * 1024 * 1024,  # 10 MB per file
                backupCount=2,              # Keep 2 backups (~30 MB total)
                encoding='utf-8'
            )
            debug_handler.setLevel(logging.DEBUG)
            debug_handler.setFormatter(file_formatter)
            root_logger.addHandler(debug_handler)

    # python-obd does the actual ELM327 I/O for the OBD manager and logs every
    # command/response at DEBUG ("write: ..." / "read: ...") on its own
    # logger tree; give it our handlers so a failed OBD session leaves a
    # protocol transcript in octave-debug.log (with --debug) and its errors
    # reach octave-error.log.
    obd_logger = logging.getLogger('obd')
    obd_logger.setLevel(logging.DEBUG if debug else logging.INFO)
    obd_logger.handlers.clear()
    obd_logger.propagate = False
    for h in root_logger.handlers:
        obd_logger.addHandler(h)

    _initialized = True
    if file:
        _install_crash_handlers(root_logger, os.path.join(_get_log_dir(), 'octave-error.log'))
    root_logger.info(f"Logging initialized (debug={debug}, log_dir={_get_log_dir()})")


def _install_crash_handlers(root_logger: logging.Logger, error_log_path: str):
    """A crash must leave a trace in the error log (mirrors src/util/logger.cpp):
    unhandled exceptions on the main thread and on worker threads (OBD, IMU,
    gesture, phone mirror ...) are logged with their traceback, and
    faulthandler dumps the Python stacks on a native fault (SIGSEGV, SIGABRT,
    SIGFPE, SIGBUS, SIGILL) straight to the error log's descriptor."""
    import faulthandler
    import threading

    def _excepthook(exc_type, exc, tb):
        if issubclass(exc_type, KeyboardInterrupt):
            sys.__excepthook__(exc_type, exc, tb)
            return
        root_logger.critical("Unhandled exception on the main thread", exc_info=(exc_type, exc, tb))
        sys.__excepthook__(exc_type, exc, tb)

    def _threading_excepthook(args):
        root_logger.critical(f"Unhandled exception in thread '{args.thread.name if args.thread else '?'}'",
                             exc_info=(args.exc_type, args.exc_value, args.exc_traceback))

    sys.excepthook = _excepthook
    threading.excepthook = _threading_excepthook
    try:
        fh = open(error_log_path, 'a', encoding='utf-8')   # kept open for the process lifetime
        faulthandler.enable(file=fh, all_threads=True)
    except OSError as e:
        root_logger.warning(f"faulthandler not enabled: {e}")


def get_logger(name: str) -> logging.Logger:
    """
    Get a logger for a module. Converts module names to hierarchical octave.* names.

    Args:
        name: Usually __name__ from the calling module

    Returns:
        A configured logger instance

    Examples:
        # In backend/obd_manager.py:
        logger = get_logger(__name__)  # Returns logger named 'octave.obd'

        # In backend/phone_mirror/scrcpy_host.py:
        logger = get_logger(__name__)  # Returns logger named 'octave.phone_mirror.host'
    """
    # Map module paths to clean logger names
    name_map = {
        'backend.obd_manager': 'octave.obd',
        'backend.media_manager': 'octave.media',
        'backend.spotify_manager': 'octave.spotify',
        'backend.settings_manager': 'octave.settings',
        'backend.clock': 'octave.clock',
        'backend.android_auto.manager': 'octave.android_auto',
        'backend.android_auto.dhu_capture': 'octave.android_auto.capture',
        'backend.android_auto.embedded_dhu': 'octave.android_auto.embedded',
        'backend.phone_mirror.manager': 'octave.phone_mirror',
        'backend.phone_mirror.scrcpy_capture': 'octave.phone_mirror.capture',
        'backend.phone_mirror.scrcpy_host': 'octave.phone_mirror.host',
        'backend.phone_mirror.scrcpy_container': 'octave.phone_mirror.container',
        'backend.phone_mirror.embedded_widget': 'octave.phone_mirror.embedded',
        '__main__': 'octave.app',
        'main': 'octave.app',
    }

    logger_name = name_map.get(name, f'octave.{name.replace("backend.", "")}')

    if logger_name not in _loggers:
        _loggers[logger_name] = logging.getLogger(logger_name)

    return _loggers[logger_name]
