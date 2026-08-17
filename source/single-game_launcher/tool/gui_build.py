# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Build Tab Mixin for BuilderApp.

Provides:
  - Build summary review with auto-sizing horizontal scroll
  - Input validation (title, paths, time formats, file existence)
  - One-click package generation in a background thread
  - Results display with colored OK/WARN/ERROR tags
  - Post-build "Open Output Folder" button
"""

import os
import platform
import shutil
import subprocess
import threading
import time
from pathlib import Path

import customtkinter as ctk
import tkinter as tk
import constants as C

import logging

logger = logging.getLogger("tempgba_builder")

from gui_utils import _strip_quotes


class BuildTabMixin:
    """Mixin providing the Build tab, validation, and package generation."""

    # =====================================================================
    # Build Tab UI Construction
    # =====================================================================

    SUMMARY_LABEL_WIDTH = 100  # Fixed width for summary row labels (prevents jitter)

    def _setup_build_tab(self):
        """Build all widgets for the 'Build' tab."""
        frame = ctk.CTkFrame(self.tab_build, fg_color="transparent")
        frame.pack(fill="both", expand=True, padx=10, pady=10)

        # Title + subtitle in one row
        title_row = ctk.CTkFrame(frame, fg_color="transparent")
        title_row.pack(fill="x", pady=(10, 0))
        ctk.CTkLabel(title_row, text="Build Summary", font=ctk.CTkFont(weight="bold")).pack(side="left")
        ctk.CTkLabel(title_row, text="  — Review your settings before building",
                     font=ctk.CTkFont(size=12), text_color=C.COLOR_TEXT_MUTED).pack(side="left", padx=(5, 0))

        # Summary frame inside a canvas for horizontal scrolling
        summary_outer = ctk.CTkFrame(frame, fg_color="transparent")
        summary_outer.pack(fill="x", pady=(5, 10))

        self.summary_canvas = tk.Canvas(summary_outer, bg=C.COLOR_BG_DARK,
                                        highlightthickness=0, height=1)  # Height auto-grows
        self.summary_canvas.pack(side="top", fill="x", expand=True)

        self.summary_h_scroll = ctk.CTkScrollbar(summary_outer, orientation="horizontal",
                                                  command=self.summary_canvas.xview)
        self.summary_h_scroll.pack(side="bottom", fill="x")
        self.summary_canvas.configure(xscrollcommand=self.summary_h_scroll.set)

        self.summary_frame = ctk.CTkFrame(self.summary_canvas, fg_color=C.COLOR_BG_DARK, corner_radius=8)
        self.summary_window = self.summary_canvas.create_window(
            (0, 0), window=self.summary_frame, anchor="nw", tags="frame"
        )

        self._update_summary()

        # Build button + reserved progress bar space (prevents layout shift)
        build_btn_frame = ctk.CTkFrame(frame, fg_color="transparent")
        build_btn_frame.pack(fill="x", pady=0)

        self.build_btn = ctk.CTkButton(build_btn_frame, text="Build Package", height=28,
                                      font=ctk.CTkFont(size=14, weight="bold"),
                                      command=self._build_package)
        self.build_btn.pack()

        # Fixed-height container: reserves space so Results section never shifts
        self.build_progress_frame = ctk.CTkFrame(build_btn_frame, fg_color="transparent", height=20)
        self.build_progress_frame.pack(fill="x", pady=0)
        self.build_progress_frame.pack_propagate(False)  # lock height even when empty

        self.build_progress = ctk.CTkProgressBar(self.build_progress_frame, width=200, mode="indeterminate")
        self.build_progress.pack(pady=(5, 0))
        self.build_progress.set(0)
        self.build_progress.pack_forget()  # hide bar, but frame stays

        # Results
        ctk.CTkLabel(frame, text="Results", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=0)

        # Results textbox with horizontal scrolling only
        results_outer = ctk.CTkFrame(frame, fg_color="transparent")
        results_outer.pack(fill="x", pady=(0, 5))

        self.results_box = tk.Text(results_outer, height=12, wrap="none",
                                   bg=C.COLOR_BG_DARK, fg=C.COLOR_TEXT_MAIN,
                                   insertbackground=C.COLOR_TEXT_MAIN,
                                   font=("Consolas", 10),
                                   relief="flat", borderwidth=0, highlightthickness=0,
                                   selectbackground=C.COLOR_ACCENT, selectforeground="#ffffff",
                                   padx=2, pady=2,
                                   spacing1=0, spacing3=0,
                                   takefocus=False, cursor="arrow")

        # Block all typing while still allowing copy (Ctrl+C) and select-all (Ctrl+A)
        self.results_box.bind("<Key>", self._on_results_key)

        self.results_box.pack(side="top", fill="x")

        # Fixed-height container for scrollbar — keeps layout stable when hidden
        self.results_scroll_frame = ctk.CTkFrame(results_outer, fg_color="transparent", height=16)
        self.results_scroll_frame.pack(side="bottom", fill="x")
        self.results_scroll_frame.pack_propagate(False)  # Lock height

        self.results_h_scroll = ctk.CTkScrollbar(self.results_scroll_frame, orientation="horizontal",
                                                  command=self.results_box.xview)
        self.results_h_scroll.pack(fill="x", expand=True)
        self.results_box.configure(xscrollcommand=self.results_h_scroll.set, yscrollcommand=None)

        # Configure result tags once (colors for OK/WARN/ERROR)
        self.results_box.tag_config("ok", foreground=C.COLOR_SUCCESS)
        self.results_box.tag_config("warn", foreground=C.COLOR_WARNING)
        self.results_box.tag_config("error", foreground=C.COLOR_ERROR)

        self.open_folder_btn = ctk.CTkButton(frame, text="Open Output Folder", command=self._open_output,
                                              state="disabled")
        self.open_folder_btn.pack(pady=0)

    def _on_any_change(self, event=None):
        """Mark summary as dirty — will refresh when Build tab is shown."""
        self._summary_dirty = True

    # Refresh canvas scrollregion when switching to Build tab
    def _refresh_summary_scroll(self):
        """
        Update canvas scrollregion, height, and sync scrollbar.

        Releases any previous width cap so the frame can grow to its true
        natural width based on current content.
        """
        self.summary_canvas.update_idletasks()
        # Release width cap so frame can measure its natural size
        self.summary_canvas.itemconfig(self.summary_window, width=0)
        self.summary_canvas.update_idletasks()

        bbox = self.summary_canvas.bbox("all")
        if not bbox:
            return
        self.summary_canvas.configure(scrollregion=bbox)
        self.summary_canvas.configure(height=bbox[3] - bbox[1])

        canvas_width = self.summary_canvas.winfo_width()
        if canvas_width > 0 and bbox[2] < canvas_width:
            # Content is narrower than canvas: stretch to fill for clean look
            self.summary_canvas.itemconfig(self.summary_window, width=canvas_width)

        # Sync scrollbar thumb to actual content
        self.summary_canvas.update_idletasks()
        self.summary_h_scroll.set(*self.summary_canvas.xview())

    def _update_summary(self):
        """Rebuild all summary rows from current widget values."""
        for widget in self.summary_frame.winfo_children():
            widget.destroy()

        title = self.title_entry.get().strip() or "(not set)"
        rom_path_text = self.rom_path.get().strip()
        rom = f"{self.rom_prefix.get()}{rom_path_text}" if rom_path_text else "(not set)"
        emu_path_text = self.emu_path.get().strip()
        emu = f"{self.emu_prefix.get()}{emu_path_text}" if emu_path_text else "(not set)"
        out = self.output_entry.get().strip() or "(not set)"

        if self.audio_type_var.get() == "local":
            audio = self.audio_entry.get().strip() or "(none)"
        else:
            audio = self.youtube_entry.get().strip() or "(none)"

        start_t = self.start_time_entry.get().strip() or "0:00:00"
        end_t = self.end_time_entry.get().strip() or "auto"

        # Normal rows
        self._summary_row("Game Title", title)
        # ROM Path
        if rom_path_text:
            self._summary_row("ROM Path", rom)
        else:
            self._summary_row("ROM Path", "(not set)", warning="⚠️ REQUIRED — must be set before copying to PSP")
        # Emulator Path
        if emu_path_text:
            self._summary_row("Emulator Path", emu)
        else:
            self._summary_row("Emulator Path", "(not set)", warning="Optional — looks for TempGBA4PSP-mod in parent folder")
        # Output Folder
        self._summary_row("Output Folder", out)
        # ICON0
        self._summary_img_row("ICON0", self._get_asset_path(self.icon_entry, 'icon_path'), self.icon_mode.get())
        # PIC1
        self._summary_img_row("PIC1", self._get_asset_path(self.pic1_entry, 'pic1_path'), self.pic1_mode.get())
        # PIC0
        self._summary_img_row("PIC0", self._get_asset_path(self.pic0_entry, 'pic0_path'), self.pic0_mode.get())
        # SND0 (Audio) - path on first line
        self._summary_row("SND0 (Audio)", audio)
        # Loop / Start / End
        self._summary_audio_meta_row(self.loop_var.get(), start_t, end_t)

    def _summary_row(self, label: str, value: str, warning: str = None):
        """Create a single summary row. Optional warning shown in orange."""
        row = ctk.CTkFrame(self.summary_frame, fg_color="transparent")
        row.pack(fill="x", padx=5, pady=2)
        ctk.CTkLabel(row, text=f"{label}:", font=ctk.CTkFont(weight="bold"),
                     width=self.SUMMARY_LABEL_WIDTH, anchor="w").pack(side="left")
        ctk.CTkLabel(row, text=value, anchor="w").pack(side="left", fill="x", expand=True)
        if warning:
            ctk.CTkLabel(row, text=warning, font=ctk.CTkFont(size=10),
                         text_color=C.COLOR_WARNING, anchor="w").pack(side="left", padx=(10, 0))

    def _summary_img_row(self, label: str, path, mode: str):
        """Image row with bold 'Format:' label."""
        row = ctk.CTkFrame(self.summary_frame, fg_color="transparent")
        row.pack(fill="x", padx=5, pady=2)
        ctk.CTkLabel(row, text=f"{label}:", font=ctk.CTkFont(weight="bold"),
                     width=self.SUMMARY_LABEL_WIDTH, anchor="w").pack(side="left")
        if not path:
            ctk.CTkLabel(row, text="(none)", anchor="w").pack(side="left", fill="x", expand=True)
        else:
            p = str(path)
            ctk.CTkLabel(row, text=p, anchor="w").pack(side="left")
            ctk.CTkLabel(row, text="Format:", font=ctk.CTkFont(weight="bold"), anchor="w").pack(side="left", padx=(10, 0))
            ctk.CTkLabel(row, text=mode, anchor="w").pack(side="left", fill="x", expand=True)

    def _summary_audio_meta_row(self, loop, start: str, end: str):
        """Loop / Start / End row — left-aligned, bold labels, no indent."""
        row = ctk.CTkFrame(self.summary_frame, fg_color="transparent")
        row.pack(fill="x", padx=5, pady=2)
        ctk.CTkLabel(row, text="Loop:", font=ctk.CTkFont(weight="bold"), anchor="w").pack(side="left")
        ctk.CTkLabel(row, text=str(loop), anchor="w").pack(side="left", padx=(3, 15))
        ctk.CTkLabel(row, text="Start:", font=ctk.CTkFont(weight="bold"), anchor="w").pack(side="left")
        ctk.CTkLabel(row, text=start, anchor="w").pack(side="left", padx=(3, 15))
        ctk.CTkLabel(row, text="End:", font=ctk.CTkFont(weight="bold"), anchor="w").pack(side="left")
        ctk.CTkLabel(row, text=end, anchor="w").pack(side="left", fill="x", expand=True)

    def validate_build_inputs(self) -> list:
        """
        Validate all inputs before building.

        Returns a list of (severity, message) tuples:
          - "error":   Must be fixed before building
          - "warning": Build can proceed but PSP won't work without action
          - "info":    Informational note (e.g., auto-detect behavior)

        Empty list means everything is ready to build.
        """
        issues = []

        title = self.title_entry.get().strip()
        if not title:
            issues.append(("error", "Game Title is required."))

        rom_path_raw = _strip_quotes(self.rom_path.get().strip())
        if not rom_path_raw:
            issues.append(("warning", "ROM PSP Path is not set. You MUST edit rom_path.txt before using on PSP."))
        elif not rom_path_raw.lower().endswith(".gba"):
            # Deferred: verify all supported formats
            pass

        emu_path_raw = _strip_quotes(self.emu_path.get().strip())
        if not emu_path_raw:
            issues.append(("info", "Emulator PSP Path not set. Launcher will auto-detect if folder is named 'tempgba4psp-mod' in parent directory."))

        # Check stub exists
        if not self.stub_path or not self.stub_path.exists():
            issues.append(("error", "EBOOT.PBP stub not found in assets/ folder."))

        # Check output path
        output_raw = _strip_quotes(self.output_entry.get().strip())
        if not output_raw:
            issues.append(("error", "Output Folder is not set."))
        else:
            output_p = Path(output_raw)
            if output_p.exists() and output_p.is_file():
                issues.append(("error", f"Output path is a file, not a folder:\n{output_raw}"))

        # Validate time formats
        start_parsed = self._parse_time(self.start_time_entry.get())
        if start_parsed < 0:
            issues.append(("error", f"Invalid Start Time: '{self.start_time_entry.get().strip()}'"))

        end_text = self.end_time_entry.get().strip()
        end_parsed = self._parse_time(end_text) if end_text else -1
        if end_text and end_parsed < 0:
            issues.append(("error", f"Invalid End Time: '{end_text}'"))
        if end_text and end_parsed <= start_parsed:
            issues.append(("error", "End time must be greater than start time."))

        # Check for missing asset files (validate raw entry text, not resolved path)
        from gui_utils import _strip_quotes as _sq
        for name, entry in [
            ("ICON0", self.icon_entry),
            ("PIC1", self.pic1_entry),
            ("PIC0", self.pic0_entry),
            ("Audio", self.audio_entry),
        ]:
            raw = _sq(entry.get().strip())
            if raw:
                p = Path(raw)
                if not p.exists() or not p.is_file():
                    issues.append(("error", f"Missing file {name}: {raw}"))

        return issues

    def _build_package(self):
        """
        Main build entry point. Validates, snapshots state, then spawns build thread.

        All widget values are read on the main thread and passed as kwargs to
        the background thread. This avoids any Tkinter thread-safety issues.
        """
        # --- Centralized validation ---
        issues = self.validate_build_inputs()
        errors = [msg for sev, msg in issues if sev == "error"]
        warnings = [msg for sev, msg in issues if sev == "warning"]
        infos = [msg for sev, msg in issues if sev == "info"]

        if errors:
            self._show_error("Cannot Build", "\n".join(f"• {e}" for e in errors))
            return

        # Gather values (validation passed, these are safe to use)
        title = self.title_entry.get().strip()
        rom_full = f"{self.rom_prefix.get()}{_strip_quotes(self.rom_path.get().strip())}"
        emu_path_raw = _strip_quotes(self.emu_path.get().strip())
        emu_full = f"{self.emu_prefix.get()}{emu_path_raw}" if emu_path_raw else ""
        output = Path(_strip_quotes(self.output_entry.get().strip()))

        if self._summary_dirty:
            self._update_summary()
            self._summary_dirty = False
        self.results_box.config(state="normal")
        self.results_box.delete("1.0", "end")
        self.results_box.config(state="disabled")
        self.update()

        # Resolve YouTube cache on main thread (thread-safe snapshot)
        yt_url = self.youtube_entry.get().strip()
        yt_cached_path = None
        if self.audio_type_var.get() == "youtube" and yt_url:
            if yt_url in self._yt_cache and self._yt_cache[yt_url].exists():
                yt_cached_path = self._yt_cache[yt_url]

        # SNAPSHOT all widget values on the main thread
        kwargs = {
            'title': title,
            'rom_path': rom_full,
            'emu_path': emu_full,
            'output': output,
            'icon_path': self._get_asset_path(self.icon_entry, 'icon_path'),
            'icon_mode': self.icon_mode.get(),
            'pic0_path': self._get_asset_path(self.pic0_entry, 'pic0_path'),
            'pic0_mode': self.pic0_mode.get(),
            'pic1_path': self._get_asset_path(self.pic1_entry, 'pic1_path'),
            'pic1_mode': self.pic1_mode.get(),
            'audio_type': self.audio_type_var.get(),
            'audio_path': self._get_asset_path(self.audio_entry, 'audio_path'),
            'youtube_url': yt_url,
            'loop': self.loop_var.get(),
            'start_ms': int(self._parse_time(self.start_time_entry.get()) * 1000),
            'end_ms': int(self._parse_time(self.end_time_entry.get().strip()) * 1000) if self.end_time_entry.get().strip() else 0,
            'yt_cached_path': yt_cached_path,
            'temp_dir': self._temp_dir,
        }

        # Check available disk space
        try:
            # If output folder doesn't exist yet, check its parent (or cwd as fallback)
            check_path = output if output.exists() else output.parent
            if not check_path.exists():
                check_path = Path(".")
            total, used, free = shutil.disk_usage(check_path)
            if free < 50 * 1024 * 1024:  # 50 MB minimum
                self._show_error("Insufficient Disk Space",
                    f"Only {free/1024/1024:.0f} MB free on output drive.\n"
                    f"At least 50 MB is recommended for audio conversion and packaging.")
                return
        except Exception:
            pass  # Can't check disk space on this platform, proceed anyway

        # Show progress indicator
        self.build_btn.configure(text="Building...", state="disabled")
        self.build_progress.pack(pady=(5, 0))
        self.build_progress.start()  # indeterminate animation
        self.update()  # force UI refresh so the button text updates immediately

        self.open_folder_btn.configure(state="disabled")

        self._build_thread = threading.Thread(target=self._do_build, kwargs=kwargs, daemon=True)
        self._build_thread.start()

    def _do_build(self, title, rom_path, emu_path, output, icon_path, icon_mode,
                  pic0_path, pic0_mode, pic1_path, pic1_mode,
                  audio_type, audio_path, youtube_url, loop, start_ms, end_ms,
                  yt_cached_path=None, temp_dir=None):
        """
        Background thread: actually build the package.

        Uses SingleGameBuilder to inject assets into the EBOOT.PBP stub,
        convert images/audio, write text files, and generate the output folder.
        """
        try:
            from builder import SingleGameBuilder
            from convert import download_youtube
            builder = SingleGameBuilder(self.stub_path, output)
            builder.set_title(title)

            if icon_path:
                builder.set_icon(icon_path, icon_mode)

            if pic1_path:
                builder.set_pic1(pic1_path, mode=pic1_mode)

            if pic0_path:
                builder.set_pic0(pic0_path, mode=pic0_mode)

            # Audio — yt_cached_path was resolved on main thread
            if audio_type == "youtube":
                url = youtube_url.strip()
                if url:
                    if yt_cached_path and yt_cached_path.exists():
                        builder.set_snd0(yt_cached_path, loop, start_ms, end_ms)
                    else:
                        # Fallback: download if not cached
                        tmp = temp_dir / f"yt_build_{time.time_ns()}.wav"
                        download_youtube(url, tmp)
                        # Do NOT write to self._yt_cache here — background thread
                        builder.set_snd0(tmp, loop, start_ms, end_ms)
            elif audio_path and audio_path.exists():
                builder.set_snd0(audio_path, loop, start_ms, end_ms)

            builder.write_text_files(rom_path, emu_path)
            builder.write_readme(rom_path, emu_path, title)

            log = builder.build()

            if not self._shutdown:
                self.after(0, lambda: self._show_results(log, output))

        except Exception as e:
            err_msg = str(e)
            # Clean up partial assets folder on failure
            try:
                if output.is_dir():
                    assets_dir = output / "assets"
                    if assets_dir.exists():
                        shutil.rmtree(assets_dir)
            except Exception:
                pass
            if not self._shutdown:
                self.after(0, lambda msg=err_msg: self._show_build_error(msg))

    # Display build log with aligned columns (3-tuple entries)
    def _show_results(self, log, output):
        """
        Display the build log in the Results textbox.

        Each log entry is a 3-tuple: (status, label, value).
        Aligns columns for readability and colors by status.
        """
        # Hide progress indicator
        self.build_progress.stop()
        self.build_progress.pack_forget()
        self.build_btn.configure(text="Build Package", state="normal")

        self.results_box.config(state="normal")
        self.results_box.delete("1.0", "end")

        # Builder log entries are always 3-tuples: (status, label, value)
        max_label_len = max(len(entry[1]) for entry in log) if log else 0
        col_width = max_label_len + 2

        for status, label, value in log:
            padded = label + " " * (col_width - len(label))
            if status == "OK":
                self.results_box.insert("end", f"[OK]   {padded}{value}\n", "ok")
            elif status == "WARN":
                self.results_box.insert("end", f"[WARN] {padded}{value}\n", "warn")
            else:
                self.results_box.insert("end", f"[{status}] {padded}{value}\n", "error")

        # Append output folder
        out_str = str(output)
        self.results_box.insert("end", f"Output Folder:\n{out_str}\n")

        self.results_box.config(state="disabled")

        self.open_folder_btn.configure(state="normal")
        self._output_path = output

        self._update_results_scrollbar()

        # Post-build warning for unset mandatory paths
        rom_path_raw = _strip_quotes(self.rom_path.get().strip())
        if not rom_path_raw:
            self.after(300, lambda: self._show_error(
                "⚠️ Build Complete — Action Required",
                "You built the package without specifying a ROM path.\n\n"
                "The launcher WILL NOT WORK on your PSP until you:\n"
                "1. Open the output folder\n"
                "2. Edit rom_path.txt\n"
                "3. Add the full path to your GBA ROM\n\n"
                "Example: ms0:/PSP/GAME/tempgba4psp-mod/roms/MyGame.gba\n\n"
                "You can also rebuild the package after filling in the ROM path.",
                warning_mode=True
            ))

    def _on_results_key(self, event):
        """
        Allow copy and select-all, block everything else.

        Handles Ctrl (Windows/Linux/macOS). Tkinter maps Command to Control
        on macOS for shortcuts.
        """
        if (event.state & 0x4) and event.keysym.lower() in ('c', 'a'):
            return None  # Let Ctrl+C/Cmd+C and Ctrl+A/Cmd+A pass through
        return "break"   # Swallow all other keys

    def _show_build_error(self, msg):
        """Display a build failure in the Results textbox."""
        logger.error("Build failed: %s", msg)

        # Hide progress indicator
        self.build_progress.stop()
        self.build_progress.pack_forget()
        self.build_btn.configure(text="Build Package", state="normal")

        self.results_box.config(state="normal")
        self.results_box.delete("1.0", "end")
        self.results_box.insert("end", f"[FAIL] Build failed: {msg}\n", "error")

        # Add log location for bug reports
        from logger import get_log_path
        log_path = get_log_path()
        self.results_box.insert("end", f"\nLog file (attach for bug reports):\n{log_path}\n")

        self.results_box.config(state="disabled")
        self._update_results_scrollbar()

        # Only enable Open Folder if the output path exists and is a directory
        output = Path(_strip_quotes(self.output_entry.get().strip()))
        if output.exists() and output.is_dir():
            self.open_folder_btn.configure(state="normal")
        else:
            self.open_folder_btn.configure(state="disabled")

    def _open_output(self):
        """Open the output folder in the system's file manager."""
        if hasattr(self, '_output_path') and self._output_path.exists():
            system = platform.system()
            if system == "Windows":
                os.startfile(self._output_path)
            elif system == "Darwin":
                subprocess.Popen(["open", str(self._output_path)])
            else:
                subprocess.Popen(["xdg-open", str(self._output_path)])