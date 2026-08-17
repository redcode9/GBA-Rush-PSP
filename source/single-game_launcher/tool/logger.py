# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Invisible logging for TempGBA4PSP-mod Builder.

One log file per session, timestamped, with automatic cleanup.
Logs go to a user-hidden directory so normal users aren't bothered,
but detailed debug info is available for bug reports.
"""

import logging
import sys
from datetime import datetime
from pathlib import Path


def _get_log_dir() -> Path:
    """Return a user-hidden log directory appropriate for the platform."""
    if sys.platform == "win32":
        base = Path.home() / "AppData" / "Local" / "TempGBA4PSP-mod Builder"
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Logs" / "TempGBA4PSP-mod Builder"
    else:
        base = Path.home() / ".cache" / "tempgba4psp-builder"

    log_dir = base / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    return log_dir


def _cleanup_old_logs(log_dir: Path, max_age_days: int = 7, max_files: int = 5):
    """
    Delete log files older than max_age_days, keeping at most max_files.

    Prevents the log directory from growing unbounded.
    """
    import time
    now = time.time()
    cutoff = now - (max_age_days * 86400)

    logs = sorted(log_dir.glob("*.log"), key=lambda p: p.stat().st_mtime)

    for log in logs[:]:
            try:
                should_remove = False

                if log.stat().st_mtime < cutoff:
                    log.unlink()
                    should_remove = True
                elif len(logs) > max_files:
                    log.unlink()
                    should_remove = True

                if should_remove:
                    logs.remove(log)
            except Exception:
                pass


# Module-level storage for the current session's log path
_current_log_path: Path | None = None


def setup_logging() -> logging.Logger:
    """
    Configure invisible file logging + console warnings.

    Creates a new timestamped log file for this session.
    File gets everything DEBUG and up; console only shows WARNING and up.
    """
    global _current_log_path

    logger = logging.getLogger("tempgba_builder")
    logger.setLevel(logging.DEBUG)

    if logger.handlers:
        return logger

    log_dir = _get_log_dir()
    _cleanup_old_logs(log_dir)

    # One file per session: tempgba-builder_YYYY-MM-DD_HH-MM-SS.log
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    log_file = log_dir / f"tempgba-builder_{timestamp}.log"
    _current_log_path = log_file

    # File handler: everything DEBUG and up
    file_handler = logging.FileHandler(log_file, mode='w', encoding='utf-8')
    file_handler.setLevel(logging.DEBUG)
    file_handler.setFormatter(logging.Formatter(
        '%(asctime)s [%(levelname)s] %(name)s.%(funcName)s: %(message)s',
        datefmt='%Y-%m-%d %H:%M:%S'
    ))
    logger.addHandler(file_handler)

    # Console handler: only WARNING and up (normal users see nothing)
    console = logging.StreamHandler(sys.stdout)
    console.setLevel(logging.WARNING)
    console.setFormatter(logging.Formatter('%(levelname)s: %(message)s'))
    logger.addHandler(console)

    return logger


def get_log_path() -> Path | None:
    """Return the path to the current session's log file."""
    return _current_log_path