# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Game Info Tab Mixin for BuilderApp.

Handles:
  - Game Title entry with live UTF-8 byte counter and PSP truncation warning
  - ROM PSP Path entry with ms0:/ef0:/ prefix selector
  - Emulator PSP Path entry (optional, with auto-detect fallback)
  - Output Folder entry with auto-naming from title and manual browse
  - Info box showing build output description
"""

import os
import sys
from pathlib import Path

import customtkinter as ctk
import constants as C

from gui_utils import _sanitize, _strip_quotes


class InfoTabMixin:
    """Mixin providing the Game Info tab and its validation logic."""

    # =====================================================================
    # Info Tab UI Construction
    # =====================================================================

    def _setup_info_tab(self):
        """Build all widgets for the 'Game Info' tab."""
        frame = ctk.CTkFrame(self.tab_info, fg_color="transparent")
        frame.pack(fill="both", expand=True, padx=10, pady=10)

        # --- Game Title ---
        ctk.CTkLabel(frame, text="Game Title", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(10, 2))
        self.title_entry = ctk.CTkEntry(frame, placeholder_text="e.g. Sample Game")
        self.title_entry.pack(fill="x", pady=(0, 2))
        self.title_entry.bind("<KeyRelease>", self._on_title_change)

        # Byte counter + truncation warning frame
        counter_frame = ctk.CTkFrame(frame, fg_color="transparent")
        counter_frame.pack(fill="x", pady=(0, 10))

        self.title_counter = ctk.CTkLabel(counter_frame, text="0 / 127",
                                          font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED)
        self.title_counter.pack(side="left")

        self.title_warning = ctk.CTkLabel(counter_frame, text="", font=ctk.CTkFont(size=10))
        self.title_warning.pack(side="left", padx=(10, 0))

        # --- ROM PSP Path ---
        ctk.CTkLabel(frame, text="ROM PSP Path", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(10, 2))
        rom_frame = ctk.CTkFrame(frame, fg_color="transparent")
        rom_frame.pack(fill="x", pady=(0, 2))

        # Prefix selector: ms0:/ (Memory Stick) or ef0:/ (PSP Go internal)
        self.rom_prefix = ctk.CTkSegmentedButton(rom_frame, values=["ms0:/", "ef0:/"],
                                                  width=75, command=self._on_any_change)
        self.rom_prefix.pack(side="left", padx=(0, 5))
        self.rom_prefix.set("ms0:/")

        self.rom_path = ctk.CTkEntry(rom_frame,
                                      placeholder_text="PSP/GAME/tempgba4psp-mod/roms/SampleGame.gba")
        self.rom_path.pack(side="left", padx=(5, 0), fill="x", expand=True)
        self.rom_path.bind("<KeyRelease>", self._on_any_change)

        # Hint label below ROM path
        ctk.CTkLabel(frame, text="Use ms0:/ for Memory Stick, ef0:/ for PSP Go internal storage",
                    font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED).pack(anchor="w", pady=(0, 0))

        # --- Emulator PSP Path ---
        ctk.CTkLabel(frame, text="Emulator PSP Path", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(0, 2))
        emu_frame = ctk.CTkFrame(frame, fg_color="transparent")
        emu_frame.pack(fill="x", pady=(0, 2))

        self.emu_prefix = ctk.CTkSegmentedButton(emu_frame, values=["ms0:/", "ef0:/"],
                                                  width=75, command=self._on_any_change)
        self.emu_prefix.pack(side="left", padx=(0, 5))
        self.emu_prefix.set("ms0:/")

        self.emu_path = ctk.CTkEntry(emu_frame,
                                      placeholder_text="PSP/GAME/tempgba4psp-mod/")
        self.emu_path.pack(side="left", padx=(5, 0), fill="x", expand=True)
        self.emu_path.bind("<KeyRelease>", self._on_any_change)

        ctk.CTkLabel(frame, text="Use ms0:/ for Memory Stick, ef0:/ for PSP Go internal storage",
                    font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED).pack(anchor="w", pady=(0, 10))

        # --- Output Folder ---
        out_label_frame = ctk.CTkFrame(frame, fg_color="transparent")
        out_label_frame.pack(fill="x", pady=(10, 5))
        ctk.CTkLabel(out_label_frame, text="Output Folder", font=ctk.CTkFont(weight="bold")).pack(side="left")
        ctk.CTkButton(out_label_frame, text="Browse", width=80, command=self._browse_output).pack(side="left", padx=(10, 0))

        self.output_entry = ctk.CTkEntry(frame, width=500)
        self.output_entry.pack(fill="x", pady=(0, 10))
        self.output_entry.bind("<KeyRelease>", self._on_output_entry_change)

        # --- Info Box ---
        self.info_box = ctk.CTkTextbox(frame, height=170, state="disabled", fg_color=C.COLOR_BG_DARK)
        self.info_box.pack(fill="x", pady=10)
        self._update_info_box()

    def _update_title_counter(self, event=None):
        """
        Update the live byte counter and truncation warning for the title.

        PSP reads the TITLE field as UTF-8 but has a hard limit of 127 bytes.
        If the user exceeds this, we truncate at the last valid character boundary.
        """
        text = self.title_entry.get()
        byte_len = len(text.encode("utf-8"))

        # Hard cap at 127 UTF-8 bytes — truncate if exceeded
        if byte_len > C.MAX_TITLE_UTF8_BYTES:
            # Slice to 127 bytes, then decode ignoring any partial multi-byte char
            text = text.encode("utf-8")[:C.MAX_TITLE_UTF8_BYTES].decode("utf-8", errors="ignore")
            self.title_entry.delete(0, "end")
            self.title_entry.insert(0, text)
            byte_len = len(text.encode("utf-8"))

        self.title_counter.configure(text=f"{byte_len} / {C.MAX_TITLE_UTF8_BYTES}")

        # Color-coded warnings based on length
        if byte_len >= C.MAX_TITLE_UTF8_BYTES:
            self.title_counter.configure(text_color=C.COLOR_ERROR)
            self.title_warning.configure(text="PSP limit — May be truncated on PSP XMB", text_color=C.COLOR_ERROR)
        elif byte_len >= 19:
            # 19 chars is roughly where PSP XMB starts truncating visually
            self.title_counter.configure(text_color=C.COLOR_WARNING)
            self.title_warning.configure(text="May be truncated on PSP XMB", text_color=C.COLOR_WARNING)
        else:
            self.title_counter.configure(text_color=C.COLOR_TEXT_MUTED)
            self.title_warning.configure(text="")

    def _on_title_change(self, event=None):
        """
        Handle title entry changes.

        Auto-updates the output folder path based on the sanitized title,
        unless the user has manually edited the output folder (tracked by
        _user_picked_output flag).
        """
        self._update_title_counter()
        title = self.title_entry.get().strip()
        if not title:
            self._on_any_change()
            return

        safe = _sanitize(title)
        current = _strip_quotes(self.output_entry.get().strip())

        # Respect user's manual output folder choice
        if getattr(self, '_user_picked_output', False):
            self._on_any_change()
            return

        if not current:
            # No output set yet — create default
            base = self._get_output_base_dir()
            result = str(Path(base) / safe)
            if sys.platform == "win32" and len(result) >= 2 and result[1] == ':':
                result = result[0].upper() + result[1:]
            self.output_entry.delete(0, "end")
            self.output_entry.insert(0, result)
            self._on_any_change()
            return

        # Auto-generated path: replace only the leaf folder name, preserving parent
        path = Path(current)
        new_path = path.parent / safe

        result = str(new_path)
        if sys.platform == "win32" and len(result) >= 2 and result[1] == ':':
            result = result[0].upper() + result[1:]
        self.output_entry.delete(0, "end")
        self.output_entry.insert(0, result)
        self._on_any_change()

    def _on_output_entry_change(self, event=None):
        """
        Track when the user manually edits the output folder.

        Once edited, the title will no longer auto-update the output path.
        """
        text = _strip_quotes(self.output_entry.get().strip())
        if text:
            self._user_picked_output = True
        self._on_any_change(event)

    def _browse_output(self):
        """Open a directory chooser dialog for the output folder."""
        from tkinter import filedialog
        path = filedialog.askdirectory()
        if path:
            clean = os.path.normpath(path)
            if sys.platform == "win32" and len(clean) >= 2 and clean[1] == ':':
                clean = clean[0].upper() + clean[1:]
            self.output_entry.delete(0, "end")
            self.output_entry.insert(0, clean)
            self._user_picked_output = True
            self._on_any_change()

    def _update_info_box(self):
        """Populate the read-only info box with build output description."""
        self.info_box.configure(state="normal")
        self.info_box.delete("1.0", "end")
        self.info_box.insert("1.0", "Build Summary:\n")
        self.info_box.insert("end", "Fill in the fields above and switch to the Build tab to generate your package.\n\n")
        self.info_box.insert("end", "The output folder will contain:\n")
        self.info_box.insert("end", "  - EBOOT.PBP (launcher with custom assets)\n")
        self.info_box.insert("end", "  - rom_path.txt (points to your ROM)\n")
        self.info_box.insert("end", "  - emulator_path.txt (points to TempGBA4PSP-mod)\n")
        self.info_box.insert("end", "  - assets/ (converted PNG/AT3 files)\n")
        self.info_box.insert("end", "  - readme.txt (installation instructions)\n")
        self.info_box.configure(state="disabled")
