# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
TempGBA4PSP-mod Single-Game Launcher Builder — Main GUI Application

This is the entry point for the builder application. It creates the main window,
assembles all tabs (Game Info, Images, Audio, Build), and manages global state
such as audio playback, temporary directories, and the title scroll animation.

The app uses a mixin architecture: BuilderApp inherits from tab-specific mixins
(InfoTabMixin, ImagesTabMixin, etc.) plus customtkinter.CTk. Each mixin handles
one tab's UI and logic, keeping the codebase modular.
"""

import sys
import tempfile
from pathlib import Path
import os

import customtkinter as ctk
from PIL import ImageFont as PILImageFont

# AudioPlayer may be imported as a relative module when running from source,
# or as a top-level module in the PyInstaller bundle.
try:
    from audio_player import AudioPlayer
except ImportError:
    from .audio_player import AudioPlayer

from logger import setup_logging
from version import __version__
import constants as C

# Tab mixins — each provides _setup_<name>_tab() and related helpers
from gui_info import InfoTabMixin
from gui_images import ImagesTabMixin
from gui_audio import AudioTabMixin
from gui_build import BuildTabMixin
from gui_credits import CreditsMixin

# Configure invisible file logging (one log per session, auto-cleanup)
logger = setup_logging()


# ---------------------------------------------------------------------------
# Main Application
# ---------------------------------------------------------------------------

class BuilderApp(InfoTabMixin, ImagesTabMixin, AudioTabMixin,
                 BuildTabMixin, CreditsMixin, ctk.CTk):
    """
    Main application window. Inherits from all tab mixins and CTk.

    Responsibilities:
      - Window setup (size, theme, centering)
      - Global asset path tracking (icon, pic0, pic1, audio)
      - Audio player lifecycle (load, play, pause, stop, poll)
      - Temporary directory management (auto-cleanup on exit)
      - Title scroll animation for XMB preview
      - Tab-switch coordination (stop audio when leaving Audio tab, etc.)
    """

    def __init__(self):
        # Initialize customtkinter root window
        super().__init__()

        # --- Theme & Window Geometry ---
        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("dark-blue")

        self.title("TempGBA4PSP-mod Single-Game Launcher Builder")
        self.geometry(f"{C.WINDOW_W}x{C.WINDOW_H}")
        self.minsize(C.WINDOW_W, C.WINDOW_H)
        self._center_window()
        self.configure(fg_color=C.COLOR_BG_DARKEST)

        # --- Core Paths ---
        # Path to the EBOOT.PBP stub (launcher template) in assets/
        self.stub_path = self._find_stub()
        # Flag: has the user manually chosen an output folder? If so, stop auto-updating it from title
        self._user_picked_output = False

        # --- Asset State ---
        # These hold Path objects to the currently selected image/audio files
        self.icon_path = None
        self.pic0_path = None
        self.pic1_path = None
        self.audio_path = None
        # "local" or "youtube" — determines which UI controls are shown
        self.audio_source_type = "local"
        self.youtube_url = ""

        # --- Per-Source Time Persistence ---
        # Start/end times are remembered separately for Local and YouTube so
        # switching sources doesn't lose the user's segment selection
        self._local_start = "0:00:00"
        self._local_end = ""
        self._yt_start = "0:00:00"
        self._yt_end = ""

        # --- YouTube Cache ---
        # Maps URL -> downloaded WAV Path. Only one entry is kept at a time.
        self._yt_cache = {}
        # Monotonically increasing ID to ignore stale download callbacks
        self._yt_download_id = 0
        self._download_thread = None

        # --- Tkinter After-ID Tracking ---
        # We track every self.after() ID so we can cancel them all on shutdown,
        # preventing "callback after window destroyed" errors
        self._after_ids = set()
        self._shutdown = False

        # --- Temporary Directory ---
        # Created once per session; deleted on exit via atexit + WM_DELETE_WINDOW
        import atexit
        self._temp_dir = Path(tempfile.mkdtemp(prefix="tempgba4psp-mod_"))
        atexit.register(self._cleanup_temp)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        # --- Audio Engine ---
        # _audio_available = True if miniaudio imported successfully
        self._audio_available = False
        self._player = AudioPlayer()
        self._audio_loaded = False
        self._poll_id = None  # ID of the UI poll loop (timeline updates)

        # Debounce timer for timeline drag-seek (ms)
        self._drag_debounce_id = None

        # --- Build UI ---
        self._build_ui()

        # --- Summary & Animation State ---
        self._summary_dirty = True  # Rebuild summary when Build tab is shown

        # Title scroll animation state for XMB preview
        self._title_scroll_offset = 0
        self._title_scroll_after_id = None
        self._title_animating = False

        # Animation tuning constants (pixels/frame, hold durations in frames)
        self.TITLE_SCROLL_SPEED = C.TITLE_SCROLL_SPEED
        self.TITLE_HOLD_FRAMES = C.TITLE_HOLD_FRAMES
        self.TITLE_GAP_FRAMES = C.TITLE_GAP_FRAMES
        self.TITLE_ANIM_INTERVAL = C.TITLE_ANIM_INTERVAL

        # --- Default Output Folder ---
        # Start with "Untitled" in the writable base directory
        default = str(self._get_output_base_dir() / "Untitled")
        # Force uppercase Windows drive letter for consistency
        if sys.platform == "win32" and len(default) >= 2 and default[1] == ':':
            default = default[0].upper() + default[1:]
        self.output_entry.insert(0, default)

        # --- Fatal Startup Check ---
        if not self.stub_path:
            self._show_error("EBOOT.PBP not found",
                "Place EBOOT.PBP in the assets/ folder next to this executable.")

    def _build_ui(self):
        """Construct the main window layout: header, tabview, footer."""
        # --- Header ---
        header = ctk.CTkLabel(self, text="TempGBA4PSP-mod Single-Game Launcher Builder",
                              font=ctk.CTkFont(size=24, weight="bold"))
        header.pack(pady=(15, 5))

        subtitle = ctk.CTkLabel(self, text="Build custom XMB bubbles for your GBA games",
                                font=ctk.CTkFont(size=12))
        subtitle.pack(pady=(0, 10))

        # --- Tab View ---
        self.tabview = ctk.CTkTabview(
            self,
            fg_color=C.COLOR_BG_MEDIUM,
            segmented_button_selected_color=C.COLOR_ACCENT,
            command=self._on_tab_changed
        )
        self.tabview.pack(fill="both", expand=True, padx=15, pady=10)

        self.tab_info = self.tabview.add("Game Info")
        self.tab_images = self.tabview.add("Images")
        self.tab_audio = self.tabview.add("Audio")
        self.tab_build = self.tabview.add("Build")

        # Each mixin provides its own _setup_*_tab() method
        self._setup_info_tab()
        self._setup_images_tab()
        self._setup_audio_tab()
        self._setup_build_tab()

        # --- Footer Bar ---
        footer = ctk.CTkFrame(self, fg_color="transparent", height=24)
        footer.pack(fill="x", side="bottom", padx=15, pady=(0, 10))
        footer.pack_propagate(False)

        # Version label (left)
        ctk.CTkLabel(footer, text=f"v{__version__}",
                     font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_MUTED).pack(side="left")

        # Copyright (left, padded)
        ctk.CTkLabel(footer, text="© 2026 JxP  |  GPL-3.0+",
                     font=ctk.CTkFont(size=10), text_color=C.COLOR_TEXT_DIM).pack(side="left", padx=(10, 0))

        # Credits link (right, clickable)
        credits_btn = ctk.CTkLabel(footer, text="Credits",
                                   font=ctk.CTkFont(size=10, underline=True),
                                   text_color=C.COLOR_ACCENT, cursor="hand2")
        credits_btn.pack(side="right")
        credits_btn.bind("<Button-1>", lambda e: self._show_credits())

    def _on_tab_changed(self):
        """
        Called whenever the user switches tabs.

        Responsibilities:
          - Stop audio playback when leaving the Audio tab
          - Stop title animation when leaving the Images tab
          - Refresh Build summary when entering Build tab
          - Restore Audio tab state when entering Audio tab
          - Refresh XMB preview when entering Images tab
        """
        current = self.tabview.get()

        # Stop audio playback when leaving the Audio tab
        if current != "Audio":
            self._stop_preview()

        # Always stop title animation when leaving Images tab
        if current != "Images":
            self._stop_title_animation()

        if current == "Build":
            # Rebuild summary if anything changed since last visit
            if getattr(self, '_summary_dirty', False):
                self._update_summary()
                self._summary_dirty = False
            self._refresh_summary_scroll()
            self._update_results_scrollbar()
        elif current == "Images":
            # Start title scroll animation only if the title is too long to fit
            if hasattr(self, '_xmb_renderer'):
                needs = self._xmb_renderer.title_needs_scroll(self.title_entry.get().strip())
                if needs:
                    self._start_title_animation()
                else:
                    self._stop_title_animation()
            self._refresh_xmb_preview()
        elif current == "Audio":
            self._restore_audio_tab_state()

    def _on_close(self):
        """
        Graceful shutdown sequence.

        Cancels all pending Tkinter callbacks, stops audio, joins background
        threads, cleans up the temporary directory, and destroys the window.
        """
        self._shutdown = True
        self._stop_title_animation()

        # Cancel all tracked after() calls
        for after_id in list(self._after_ids):
            try:
                self.after_cancel(after_id)
            except Exception:
                logger.debug("Failed to cancel after_id %s", after_id)
        self._after_ids.clear()

        # Cancel debounce timers from image entry fields
        for attr in list(vars(self)):
            if attr.startswith('_debounce_id_'):
                try:
                    self.after_cancel(getattr(self, attr))
                except Exception:
                    pass

        # Cancel timeline drag debounce
        if self._drag_debounce_id:
            try:
                self.after_cancel(self._drag_debounce_id)
            except Exception:
                pass
            self._drag_debounce_id = None

        # Cancel audio poll UI
        if self._poll_id:
            try:
                self.after_cancel(self._poll_id)
            except Exception:
                pass
            self._poll_id = None

        # Give background threads a chance to finish cleanly
        for attr in ['_download_thread', '_build_thread']:
            thread = getattr(self, attr, None)
            if thread and thread.is_alive():
                thread.join(timeout=2.0)

        self._player.stop()
        self._cleanup_temp()
        self.destroy()

    def _cleanup_temp(self):
        """Delete the app temp folder and all contents."""
        if self._temp_dir and self._temp_dir.exists():
            import shutil
            try:
                shutil.rmtree(self._temp_dir)
            except Exception:
                pass

    def _center_window(self):
        """Center the window on the primary monitor."""
        self.update_idletasks()
        w, h = C.WINDOW_W, C.WINDOW_H
        x = (self.winfo_screenwidth() // 2) - (w // 2)
        y = (self.winfo_screenheight() // 2) - (h // 2)
        self.geometry(f"{w}x{h}+{x}+{y}")

    def _find_stub(self):
        """
        Locate the EBOOT.PBP stub file required for building.

        Searches multiple locations to support both PyInstaller bundles
        (where assets are extracted to _MEIPASS) and source runs.
        """
        candidates = [
            self._get_base_dir() / "assets" / "EBOOT.PBP",
            Path(__file__).parent / "assets" / "EBOOT.PBP",
            Path(__file__).parent / "EBOOT.PBP",
            Path.cwd() / "assets" / "EBOOT.PBP",
            Path.cwd() / "EBOOT.PBP",
        ]
        for c in candidates:
            if c.exists():
                return c
        return None

    def _get_base_dir(self) -> Path:
        """
        Return the base directory containing assets (images, fonts, EBOOT.PBP).

        In a PyInstaller bundle, this is the _MEIPASS temp folder.
        When running from source, it's the script's directory.
        """
        if hasattr(sys, '_MEIPASS'):
            return Path(sys._MEIPASS)
        return Path(__file__).parent

    def _get_output_base_dir(self) -> Path:
        """
        Return the default base directory for output folders.

        Tries the executable/script directory first; falls back to the user's
        Documents folder if that location is not writable (e.g., Program Files).
        """
        if hasattr(sys, '_MEIPASS'):
            exe_dir = Path(sys.executable).parent
            # Test if writable
            try:
                test = exe_dir / ".write_test"
                test.write_text("ok")
                test.unlink()
                return exe_dir
            except PermissionError:
                # Fallback to Documents
                return Path(os.path.expanduser("~/Documents"))

        # Running from source — test writability in case source is read-only
        source_dir = Path(__file__).parent
        try:
            test = source_dir / ".write_test"
            test.write_text("ok")
            test.unlink()
            return source_dir
        except PermissionError:
            return Path(os.path.expanduser("~/Documents"))

    def _safe_after(self, ms, func):
        """
        Schedule a Tkinter after() call and track its ID for cleanup on shutdown.

        Uses a callable class instead of a closure to avoid a Python 3.11+
        NameError that can occur when ms=0 callbacks fire before the enclosing
        scope completes.
        """
        class _Callback:
            __slots__ = ('app', 'func', '_aid')
            def __init__(self, app, func):
                self.app = app
                self.func = func
                self._aid = None
            def __call__(self):
                if self._aid is not None:
                    self.app._after_ids.discard(self._aid)
                self.func()

        cb = _Callback(self, func)
        after_id = self.after(ms, cb)
        cb._aid = after_id
        self._after_ids.add(after_id)
        return after_id

    def _debounce(self, ms, func, *args, key=None):
        """
        Cancel any pending after() call for *key* and schedule a new one.

        Used for entry-field validation (e.g., typing a path) so we don't
        validate on every keystroke — only after the user stops typing.
        """
        attr = f'_debounce_id_{key}' if key else '_debounce_id'
        current = getattr(self, attr, None)
        if current:
            try:
                self.after_cancel(current)
            except Exception:
                pass
        after_id = self.after(ms, lambda: func(*args))
        setattr(self, attr, after_id)

    def _show_error(self, title, message, warning_mode=False):
        """
        Show a centered modal error/warning dialog.

        Dynamically sizes itself based on message length and caps height at
        80% of the parent window. Supports scrollable text for very long messages.
        """
        dlg = ctk.CTkToplevel(self)
        dlg.title(title)
        dlg.configure(fg_color=C.COLOR_BG_MEDIUM)
        dlg.transient(self)
        dlg.grab_set()
        dlg.resizable(False, False)

        # Dynamic sizing based on message length
        lines = message.split('\n')
        line_count = len(lines)
        max_line_len = max(len(l) for l in lines) if lines else 0

        # Base size + scale with content
        dlg_w = min(600, max(400, min(max_line_len * 8, 800)))
        # Cap height to 80% of parent window
        self.update_idletasks()
        max_h = int(self.winfo_height() * 0.8)
        dlg_h = min(max_h, max(180, 80 + line_count * 22))

        # Center on parent window
        self.update_idletasks()
        parent_x = self.winfo_x()
        parent_y = self.winfo_y()
        parent_w = self.winfo_width()
        parent_h = self.winfo_height()
        x = parent_x + (parent_w - dlg_w) // 2
        y = parent_y + (parent_h - dlg_h) // 2
        dlg.geometry(f"{dlg_w}x{dlg_h}+{x}+{y}")

        title_color = C.COLOR_WARNING if warning_mode else "#ffffff"
        ctk.CTkLabel(dlg, text=title, font=ctk.CTkFont(weight="bold", size=16),
                     text_color=title_color).pack(pady=(20, 10))

        # Use a scrollable textbox for very long messages
        if line_count > 12 or max_line_len > 70:
            textbox = ctk.CTkTextbox(dlg, width=dlg_w - 40, height=dlg_h - 140,
                                     fg_color=C.COLOR_BG_DARK, font=ctk.CTkFont(size=11),
                                     wrap="word")
            textbox.pack(padx=20, pady=5)
            textbox.insert("1.0", message)
            textbox.configure(state="disabled")
        else:
            ctk.CTkLabel(dlg, text=message, wraplength=dlg_w - 40,
                        font=ctk.CTkFont(size=11)).pack(pady=5)

        ctk.CTkButton(dlg, text="OK", command=dlg.destroy).pack(pady=10)

    def _get_asset_path(self, entry_widget, cached_path_attr: str):
        """
        Generic path resolver for asset file entry widgets.

        Reads the entry widget text, strips Windows Explorer quotes, checks if
        the path exists as a file, and updates the cached attribute. Falls back
        to the cached path if the entry is empty/invalid but the cache is still valid.
        """
        from gui_utils import _strip_quotes as sq
        typed = sq(entry_widget.get().strip())
        if typed:
            p = Path(typed)
            if p.exists() and p.is_file():
                setattr(self, cached_path_attr, p)
                return p

        # Fallback to cached path if it still exists
        cached = getattr(self, cached_path_attr, None)
        if cached and cached.exists() and cached.is_file():
            return cached

        setattr(self, cached_path_attr, None)
        return None

    def _load_preview_font(self, size: int = 20):
        """
        Load a TrueType font for XMB preview title rendering.

        Tries the bundled DejaVu Sans first, then falls back to system fonts
        depending on the platform (Windows, macOS, Linux). Ultimate fallback is
        Pillow's built-in default font.
        """
        candidates = [
            # Bundled font (guaranteed on all platforms in PyInstaller release)
            self._get_base_dir() / "assets" / C.FONT_NAME,
        ]
        if sys.platform == "win32":
            candidates.extend([
                Path("C:/Windows/Fonts/DejaVuSans.ttf"),
                Path("C:/Windows/Fonts/arial.ttf"),
                Path("C:/Windows/Fonts/segoeui.ttf"),
                Path("C:/Windows/Fonts/tahoma.ttf"),
            ])
        elif sys.platform == "darwin":
            candidates.extend([
                Path("/Library/Fonts/DejaVuSans.ttf"),
                Path("/System/Library/Fonts/Helvetica.ttc"),
                Path("/Library/Fonts/Arial.ttf"),
                Path("/System/Library/Fonts/SFNSText.ttf"),
            ])
        else:
            candidates.extend([
                Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
                Path("/usr/share/fonts/opentype/dejavu/DejaVuSans.otf"),
                Path("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"),
                Path("/usr/share/fonts/truetype/freefont/FreeSans.ttf"),
            ])
        for path in candidates:
            if path.exists():
                try:
                    return PILImageFont.truetype(str(path), size)
                except Exception:
                    continue
        # Ultimate fallback: Pillow 10.1+ supports size in load_default()
        try:
            return PILImageFont.load_default(size=size)
        except Exception:
            # Older Pillow: returns bitmap font without getbbox()
            return PILImageFont.load_default()

    def _update_results_scrollbar(self):
        """
        Show or hide the horizontal scrollbar for the Build results textbox.

        Measures the widest line of text; if it exceeds the widget width,
        shows the scrollbar. Otherwise hides it to keep the layout clean.
        """
        if self._shutdown:
            return
        self.results_box.update_idletasks()

        from tkinter import font
        f = font.Font(font=self.results_box.cget("font"))

        max_line_width = 0
        end_line = int(self.results_box.index("end").split(".")[0])
        for i in range(1, end_line + 1):
            line = self.results_box.get(f"{i}.0", f"{i}.end")
            line_width = f.measure(line)
            max_line_width = max(max_line_width, line_width)

        widget_width = self.results_box.winfo_width()
        if widget_width <= 1:
            self._safe_after(100, self._update_results_scrollbar)
            return

        view_width = max(1, widget_width - 4)  # Account for padx=2 on each side

        if max_line_width > view_width:
            # Content overflows: show scrollbar
            self.results_h_scroll.pack(fill="x", expand=True)
            self.results_box.configure(xscrollcommand=self.results_h_scroll.set)
            self.results_h_scroll.configure(command=self.results_box.xview)
            self.results_h_scroll.set(0.0, min(1.0, view_width / max_line_width))
        else:
            # Content fits: hide scrollbar but keep container height stable
            self.results_h_scroll.pack_forget()
            self.results_box.configure(xscrollcommand=None)


def main():
    """Application entry point. Sets Windows DPI awareness, then launches."""
    if sys.platform == "win32":
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    app = BuilderApp()
    app.mainloop()


if __name__ == "__main__":
    main()