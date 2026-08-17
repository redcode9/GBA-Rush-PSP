# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""Shared utility functions for the GUI layer."""

import re


def parse_credits_md(md_path):
    """
    Parse CREDITS.md into structured data for the popup.

    Handles multi-line paragraphs, bullet items, inline markdown links, and
    section headers.

    Returns dict with:
      - 'title': str (project name line)
      - 'sections': list of {'name': str, 'items': list of dict}
        where each item is {'type': 'text'|'bullet', 'text': str}
      - 'footer': list of str
    """
    if not md_path.exists():
        return {"title": "", "sections": [], "footer": []}

    with open(md_path, "r", encoding="utf-8") as f:
        lines = f.readlines()

    result = {"title": "", "sections": [], "footer": []}
    current_section = None
    pending_bullet = None
    pending_text = None

    def _flush_bullet():
        """Write any pending bullet text to the current section."""
        nonlocal pending_bullet
        if pending_bullet is not None and current_section is not None:
            current_section["items"].append({"type": "bullet", "text": pending_bullet})
            pending_bullet = None

    def _flush_text():
        """Write any pending paragraph text to the current section."""
        nonlocal pending_text
        if pending_text is not None and current_section is not None:
            current_section["items"].append({"type": "text", "text": pending_text})
            pending_text = None

    for raw_line in lines:
        line = raw_line.rstrip("\n")
        stripped = line.strip()

        # Empty lines / horizontal rules flush any pending paragraph or bullet
        if not stripped or stripped == "---":
            _flush_bullet()
            _flush_text()
            continue

        # Title line (first # line)
        if line.startswith("# ") and not result["title"]:
            result["title"] = stripped[2:]
            continue

        # Section headers (## Section Name)
        if line.startswith("## "):
            _flush_bullet()
            _flush_text()
            name = stripped[3:]
            current_section = {"name": name, "items": []}
            result["sections"].append(current_section)
            continue

        # Bullet continuation (indented, part of current bullet)
        if pending_bullet is not None and line.startswith("  ") and not line.startswith("  - "):
            pending_bullet += " " + stripped
            continue

        # Bullet items: start with "- " (possibly indented)
        if line.startswith("- ") or line.startswith("  - "):
            _flush_text()
            if pending_bullet is not None:
                _flush_bullet()
            pending_bullet = stripped[2:].strip()
            continue

        # Plain text in a section — accumulate into paragraphs
        if current_section is not None and not line.startswith("#"):
            _flush_bullet()
            if pending_text is None:
                pending_text = stripped
            else:
                pending_text += " " + stripped
            continue

        # Footer lines (after all sections, not in any section)
        if current_section is None and not line.startswith("#") and stripped:
            result["footer"].append(stripped)

    _flush_bullet()
    _flush_text()

    return result


def _sanitize(name):
    """
    Sanitize a string for use as a folder name.

    Replaces Windows-forbidden characters with underscores.
    Falls back to "Untitled" if the result is empty.
    """
    return re.sub(r'[<>"/\\|?*]', '_', name).strip() or "Untitled"


def _strip_quotes(text: str) -> str:
    """
    Remove surrounding quotes from paths copied from Windows Explorer.

    Windows copies paths with quotes when you shift-right-click "Copy as path".
    """
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] and text[0] in ('"', "'"):
        return text[1:-1]
    return text


def _format_time(sec: float) -> str:
    """Format seconds to HH:MM:SS or MM:SS (omits hours if zero)."""
    total = int(sec)
    h = total // 3600
    m = (total % 3600) // 60
    s = total % 60
    if h > 0:
        return f"{h}:{m:02d}:{s:02d}"
    return f"{m}:{s:02d}"


def _format_time_hhmmss(sec: float) -> str:
    """Format seconds to HH:MM:SS always (always includes hours)."""
    total = int(sec)
    h = total // 3600
    m = (total % 3600) // 60
    s = total % 60
    return f"{h}:{m:02d}:{s:02d}"