# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Images Tab Mixin for BuilderApp.

Provides:
  - ICON0, PIC1, PIC0 image selection (browse or URL download)
  - Mode dropdowns for each image (Fit/Stretch, various resolutions)
  - Live PSP XMB preview renderer with title scroll animation
  - Asynchronous image downloading from URLs
"""

import os
import shutil
import threading
import tkinter as tk
from pathlib import Path

import customtkinter as ctk
from PIL import Image as PILImage
import constants as C

import logging

logger = logging.getLogger("tempgba_builder")

from gui_utils import _strip_quotes


class ImagesTabMixin:
    """Mixin providing the Images tab and XMB preview."""

    # =====================================================================
    # Images Tab UI Construction
    # =====================================================================

    def _setup_images_tab(self):
        """Build all widgets for the 'Images' tab."""
        frame = ctk.CTkFrame(self.tab_images, fg_color="transparent")
        frame.pack(fill="both", expand=True, padx=10, pady=10)

        # --- ICON0 (Game Icon) ---
        icon_frame = ctk.CTkFrame(frame, fg_color=C.COLOR_BG_DARK, corner_radius=8)
        icon_frame.pack(fill="x", pady=5)

        icon_header = ctk.CTkFrame(icon_frame, fg_color="transparent")
        icon_header.pack(fill="x", padx=10, pady=(10, 5))

        ctk.CTkLabel(icon_header, text="ICON0 (Icon)", font=ctk.CTkFont(weight="bold")).pack(side="left")
        ctk.CTkLabel(icon_header, text="max px 144x80", font=ctk.CTkFont(weight="bold")).pack(side="left", padx=(53, 0))
        ctk.CTkButton(
            icon_header, text="Browse", width=80,
            command=lambda: self._browse_file(
                self.icon_entry,
                [("Images", "*.png *.jpg *.jpeg *.gif *.bmp")],
                self._load_icon_preview
            )
        ).pack(side="left", padx=(18, 0))
        ctk.CTkButton(icon_header, text="Load URL", width=80, command=self._load_icon_url).pack(side="left", padx=(5, 0))

        # Mode dropdown: controls how the source image is resized into 144x80
        self.icon_mode = ctk.CTkOptionMenu(
            icon_header,
            values=C.ICON_MODES,
            width=175
        )
        self.icon_mode.pack(side="left", padx=(5, 0))
        self.icon_mode.set(C.ICON_MODES[0])
        self.icon_mode.configure(command=lambda _: (self._refresh_xmb_preview(), self._on_any_change()))

        self.icon_entry = ctk.CTkEntry(icon_frame, placeholder_text="Path or URL to image (optional)")
        self.icon_entry.pack(fill="x", padx=10, pady=(0, 5))
        self.icon_entry.bind("<KeyRelease>", self._on_icon_entry_change)

        # --- PIC1 (Background) ---
        pic1_frame = ctk.CTkFrame(frame, fg_color=C.COLOR_BG_DARK, corner_radius=8)
        pic1_frame.pack(fill="x", pady=5)

        pic1_header = ctk.CTkFrame(pic1_frame, fg_color="transparent")
        pic1_header.pack(fill="x", padx=10, pady=(10, 5))

        ctk.CTkLabel(pic1_header, text="PIC1", font=ctk.CTkFont(weight="bold")).pack(side="left")
        ctk.CTkLabel(pic1_header, text="(Background) max px 480x272", font=ctk.CTkFont(weight="bold")).pack(side="left", padx=(15, 0))
        ctk.CTkButton(
            pic1_header, text="Browse", width=80,
            command=lambda: self._browse_file(
                self.pic1_entry,
                [("Images", "*.png *.jpg *.jpeg *.gif *.bmp")],
                self._load_pic1_preview
            )
        ).pack(side="left", padx=(10, 0))
        ctk.CTkButton(pic1_header, text="Load URL", width=80, command=self._load_pic1_url).pack(side="left", padx=(5, 0))

        self.pic1_mode = ctk.CTkOptionMenu(
            pic1_header,
            values=C.PIC1_MODES,
            width=175,
        )
        self.pic1_mode.pack(side="left", padx=(5, 0))
        self.pic1_mode.set(C.PIC1_MODES[0])
        self.pic1_mode.configure(command=lambda _: (self._refresh_xmb_preview(), self._on_any_change()))

        self.pic1_entry = ctk.CTkEntry(pic1_frame, placeholder_text="Path or URL to image (optional)")
        self.pic1_entry.pack(fill="x", padx=10, pady=(0, 5))
        self.pic1_entry.bind("<KeyRelease>", self._on_pic1_entry_change)

        # --- PIC0 (Overlay) ---
        pic0_frame = ctk.CTkFrame(frame, fg_color=C.COLOR_BG_DARK, corner_radius=8)
        pic0_frame.pack(fill="x", pady=5)

        pic0_header = ctk.CTkFrame(pic0_frame, fg_color="transparent")
        pic0_header.pack(fill="x", padx=10, pady=(10, 5))

        ctk.CTkLabel(pic0_header, text="PIC0", font=ctk.CTkFont(weight="bold")).pack(side="left")
        ctk.CTkLabel(pic0_header, text="(Overlay)", font=ctk.CTkFont(weight="bold")).pack(side="left", padx=(15, 0))
        ctk.CTkLabel(pic0_header, text="max px 310x180", font=ctk.CTkFont(weight="bold")).pack(side="left", padx=(33, 0))
        ctk.CTkButton(
            pic0_header, text="Browse", width=80,
            command=lambda: self._browse_file(
                self.pic0_entry,
                [("Images", "*.png *.jpg *.jpeg *.gif *.bmp")],
                self._load_pic0_preview
            )
        ).pack(side="left", padx=(10, 0))
        ctk.CTkButton(pic0_header, text="Load URL", width=80, command=self._load_pic0_url).pack(side="left", padx=(5, 0))

        self.pic0_mode = ctk.CTkOptionMenu(
            pic0_header,
            values=C.PIC0_MODES,
            width=175,
        )
        self.pic0_mode.pack(side="left", padx=(5, 0))
        self.pic0_mode.set(C.PIC0_MODES[0])
        self.pic0_mode.configure(command=lambda _: (self._refresh_xmb_preview(), self._on_any_change()))

        self.pic0_entry = ctk.CTkEntry(pic0_frame, placeholder_text="Path or URL to image (optional)")
        self.pic0_entry.pack(fill="x", padx=10, pady=(0, 5))
        self.pic0_entry.bind("<KeyRelease>", self._on_pic0_entry_change)

        # --- XMB Simulation Preview ---
        xmb_frame = ctk.CTkFrame(frame, fg_color=C.COLOR_BG_DARKEST, corner_radius=8)
        xmb_frame.pack(fill="x", pady=0)

        ctk.CTkLabel(xmb_frame, text="PSP XMB Preview", font=ctk.CTkFont(weight="bold")).pack(pady=(10, 5))

        # The preview label displays a 480x272 CTkImage
        self.xmb_preview = ctk.CTkLabel(xmb_frame, text="No assets loaded",
                                        width=C.PSP_SCREEN_W, height=C.PSP_SCREEN_H,
                                        fg_color=C.COLOR_BG_DARKEST)
        self.xmb_preview.pack(pady=10)

        ctk.CTkButton(xmb_frame, text="Refresh Preview", command=self._refresh_xmb_preview).pack(pady=(0, 5))

        ctk.CTkLabel(xmb_frame,
                     text="Note: Title text is hidden on a real PSP when PIC1 (background) or PIC0 (overlay) is present.",
                     font=ctk.CTkFont(size=10),
                     text_color=C.COLOR_TEXT_MUTED).pack(pady=(0, 10))

        self._refresh_xmb_preview()

    def _browse_file(self, entry_widget, filetypes, load_func):
        """
        Generic file browser for asset entries.

        Opens a file dialog, inserts the selected path into the entry widget,
        and calls the load function to preview/validate the file.
        """
        from tkinter import filedialog
        path = filedialog.askopenfilename(filetypes=filetypes)
        if path:
            clean = os.path.normpath(path)
            entry_widget.delete(0, "end")
            entry_widget.insert(0, clean)
            load_func(clean)
            self._on_any_change()

    def _load_icon_url(self):
        """Validate and start downloading an ICON0 image from URL."""
        url = self.icon_entry.get().strip()
        if not url.startswith("http"):
            self._show_error("Invalid URL", "URL must start with http:// or https://")
            return
        self._download_and_preview(url, "icon0")

    def _load_pic0_url(self):
        """Validate and start downloading a PIC0 image from URL."""
        url = self.pic0_entry.get().strip()
        if not url.startswith("http"):
            self._show_error("Invalid URL", "URL must start with http:// or https://")
            return
        self._download_and_preview(url, "pic0")

    def _load_pic1_url(self):
        """Validate and start downloading a PIC1 image from URL."""
        url = self.pic1_entry.get().strip()
        if not url.startswith("http"):
            self._show_error("Invalid URL", "URL must start with http:// or https://")
            return
        self._download_and_preview(url, "pic1")

    def _download_and_preview(self, url, asset_type):
        """
        Download an image from URL in a background thread, then load into preview.

        Steps:
          1. Download raw bytes to temp file
          2. Detect image format from magic bytes (PNG, JPEG, GIF, BMP, WEBP)
          3. Rename to proper extension
          4. Validate with PIL (thread-safe open/convert)
          5. Post result back to main thread via Tkinter's after()
        """
        # Show loading state immediately on main thread
        self.xmb_preview.configure(text="Downloading...", image=None)
        self.update()

        def _thread_worker():
            try:
                # Step 1: Download raw bytes
                tmp_raw = self._temp_dir / f"dl_{asset_type}_raw"
                from convert import download_url
                download_url(url, tmp_raw)

                # Step 2: Detect format from magic bytes
                with open(tmp_raw, "rb") as f:
                    header = f.read(16)

                if header[:8] == b'\x89PNG\r\n\x1a\n':
                    ext = ".png"
                elif header[:3] == b'\xff\xd8\xff':
                    ext = ".jpg"
                elif header[:6] in (b'GIF87a', b'GIF89a'):
                    ext = ".gif"
                elif header[:2] == b'BM':
                    ext = ".bmp"
                elif header[:4] == b'RIFF' and header[8:12] == b'WEBP':
                    ext = ".webp"
                else:
                    ext = ".jpg"

                # Step 3: Rename to proper extension
                tmp = self._temp_dir / f"dl_{asset_type}{ext}"
                if tmp.exists():
                    tmp.unlink()
                shutil.move(str(tmp_raw), str(tmp))

                # Step 4: Validate image (PIL is thread-safe for open/convert)
                with PILImage.open(tmp) as img:
                    img.convert("RGBA")

                # Step 5: Post success back to main thread
                if not self._shutdown:
                    self.after(0, lambda: self._on_image_downloaded(tmp, asset_type))

            except Exception as e:
                logger.exception("Image download failed")
                if not self._shutdown:
                    self.after(0, lambda msg=str(e): self._on_image_download_error(msg))

        # Launch background download
        self._image_download_thread = threading.Thread(target=_thread_worker, daemon=True)
        self._image_download_thread.start()

    def _on_image_downloaded(self, tmp: Path, asset_type: str):
        """Main-thread callback after successful image download."""
        if asset_type == "icon0":
            self._load_icon_preview(str(tmp))
        elif asset_type == "pic0":
            self._load_pic0_preview(str(tmp))
        elif asset_type == "pic1":
            self._load_pic1_preview(str(tmp))

    def _on_image_download_error(self, msg: str):
        """Main-thread callback after failed image download."""
        self._show_error("Download failed", msg)
        self._refresh_xmb_preview()  # Restore preview (clears "Downloading..." text)

    def _load_icon_preview(self, path):
        """Validate and store an ICON0 image path, then refresh preview."""
        p = Path(path)
        if not p.exists() or not p.is_file():
            self._show_error("Icon load failed", f"Not a valid file:\n{path}")
            return
        try:
            PILImage.open(path).convert("RGBA")
            self.icon_path = p
            self.icon_entry.delete(0, "end")
            self.icon_entry.insert(0, str(path))
            self._refresh_xmb_preview()
            self._on_any_change()
        except Exception as e:
            self._show_error("Icon load failed", str(e))

    def _load_pic0_preview(self, path):
        """Validate and store a PIC0 image path, then refresh preview."""
        p = Path(path)
        if not p.exists() or not p.is_file():
            self._show_error("PIC0 load failed", f"Not a valid file:\n{path}")
            return
        try:
            PILImage.open(path).convert("RGBA")
            self.pic0_path = p
            self.pic0_entry.delete(0, "end")
            self.pic0_entry.insert(0, str(path))
            self._refresh_xmb_preview()
            self._on_any_change()
        except Exception as e:
            self._show_error("PIC0 load failed", str(e))

    def _load_pic1_preview(self, path):
        """Validate and store a PIC1 image path, then refresh preview."""
        p = Path(path)
        if not p.exists() or not p.is_file():
            self._show_error("PIC1 load failed", f"Not a valid file:\n{path}")
            return
        try:
            PILImage.open(path).convert("RGBA")
            self.pic1_path = p
            self.pic1_entry.delete(0, "end")
            self.pic1_entry.insert(0, str(path))
            self._refresh_xmb_preview()
            self._on_any_change()
        except Exception as e:
            self._show_error("PIC1 load failed", str(e))

    def _on_icon_entry_change(self, event=None):
        """Debounced handler for ICON0 path entry typing."""
        self._debounce(300, self._validate_image_entry, self.icon_entry, 'icon_path', key='icon')
        self._on_any_change()

    def _on_pic0_entry_change(self, event=None):
        """Debounced handler for PIC0 path entry typing."""
        self._debounce(300, self._validate_image_entry, self.pic0_entry, 'pic0_path', key='pic0')
        self._on_any_change()

    def _on_pic1_entry_change(self, event=None):
        """Debounced handler for PIC1 path entry typing."""
        self._debounce(300, self._validate_image_entry, self.pic1_entry, 'pic1_path', key='pic1')
        self._on_any_change()

    def _validate_image_entry(self, entry_widget, cached_attr: str):
        """
        Validate an image path from an entry widget and refresh preview.

        Called via debounce when user types in an image path field.
        If the path is valid, stores it and refreshes the XMB preview.
        If invalid, clears the cached path.
        """
        text = _strip_quotes(entry_widget.get().strip())

        # Empty entry = no image selected
        if not text:
            setattr(self, cached_attr, None)
            self._refresh_xmb_preview()
            return

        # Check if path points to an actual file (not a directory)
        p = Path(text)
        if p.exists() and p.is_file():
            try:
                # Verify it's a valid image by opening it
                PILImage.open(text).convert("RGBA")
                # Valid image - store it and refresh preview
                setattr(self, cached_attr, p)
                self._refresh_xmb_preview()
                return
            except Exception:
                # Not a valid image - fall through to clear
                pass

        # Path doesn't exist, is a directory, or isn't a valid image
        setattr(self, cached_attr, None)
        self._refresh_xmb_preview()

    def _refresh_xmb_preview(self, event=None, scroll_offset=0, manage_animation=True, from_animation=False):
        """
        Render the full PSP XMB simulation with all layers.

        Args:
            scroll_offset: Pixel offset for title scroll animation (0=static, >0=scrolling, -1=blank)
            manage_animation: Whether to start/stop the title scroll animation loop
            from_animation: True if called from the animation timer (bypasses rate-limiting)
        """
        # Rate-limit user-initiated refreshes; animation frames must always draw
        if not from_animation and getattr(self, '_xmb_refresh_pending', False):
            return
        if not from_animation:
            self._xmb_refresh_pending = True

        try:
            # Lazy-init renderer (first call creates the XMBRenderer instance)
            if not hasattr(self, '_xmb_renderer'):
                from xmb_renderer import XMBRenderer
                self._xmb_renderer = XMBRenderer(
                    self._get_base_dir() / "assets",
                    self._load_preview_font
                )

            # Gather current settings from entry widgets
            title = self.title_entry.get().strip()
            icon = self._get_asset_path(self.icon_entry, 'icon_path')
            pic0 = self._get_asset_path(self.pic0_entry, 'pic0_path')
            pic1 = self._get_asset_path(self.pic1_entry, 'pic1_path')

            # Render the 480x272 preview image
            canvas = self._xmb_renderer.render(
                title=title,
                icon_path=icon,
                icon_mode=self.icon_mode.get(),
                pic0_path=pic0,
                pic0_mode=self.pic0_mode.get(),
                pic1_path=pic1,
                pic1_mode=self.pic1_mode.get(),
                scroll_offset=scroll_offset
            )

            # Clear old image reference to prevent Tkinter memory leak
            if hasattr(self.xmb_preview, 'image') and self.xmb_preview.image:
                try:
                    del self.xmb_preview.image
                except Exception:
                    pass

            ctk_img = ctk.CTkImage(
                light_image=canvas,
                dark_image=canvas,
                size=(480, 272)
            )

            # If label is corrupted, recreate it
            try:
                self.xmb_preview.configure(image=ctk_img, text="")
                self.xmb_preview.image = ctk_img
            except tk.TclError:
                # Label's internal image reference is corrupted — recreate
                self._recreate_xmb_preview()
                self.xmb_preview.configure(image=ctk_img, text="")
                self.xmb_preview.image = ctk_img

            # Manage animation state based on title width
            if manage_animation:
                needs_scroll = self._xmb_renderer.title_needs_scroll(title)
                if needs_scroll:
                    self._start_title_animation()
                else:
                    self._stop_title_animation()

        except Exception as e:
            self.xmb_preview.configure(image=None, text=f"Preview error:\n{str(e)[:200]}")
            self.xmb_preview.image = None

        finally:
            if not from_animation:
                self._xmb_refresh_pending = False

    def _start_title_animation(self):
        """
        Start the title scroll animation loop.

        The animation cycles through three phases:
          1. hold   — Show truncated static text (~2 seconds)
          2. scroll — Scroll full title left until off-screen
          3. gap    — Brief blank period (~0.5 seconds), then back to hold
        """
        # Always cancel any existing timer first — prevents ghost timers
        if self._title_scroll_after_id:
            try:
                self.after_cancel(self._title_scroll_after_id)
            except Exception:
                pass
            self._title_scroll_after_id = None

        self._title_animating = True
        self._title_scroll_phase = "hold"
        self._title_scroll_offset = 0
        self._title_phase_frames = 0
        self._title_scroll_loop()

    def _stop_title_animation(self):
        """Stop the title scroll animation and reset to static state."""
        self._title_animating = False
        self._title_scroll_phase = "hold"
        self._title_scroll_offset = 0
        self._title_phase_frames = 0
        if self._title_scroll_after_id:
            try:
                self.after_cancel(self._title_scroll_after_id)
            except Exception:
                pass
            self._title_scroll_after_id = None

    def _title_scroll_loop(self):
        """
        Single frame of the title scroll animation.

        Called repeatedly via self.after() at ~25 fps. Manages the three-phase
        state machine (hold -> scroll -> gap -> hold).
        """
        if not self._title_animating or self._shutdown:
            return

        try:
            if self._title_scroll_phase == "hold":
                # Phase 1: Showing truncated static text
                self._title_phase_frames += 1
                if self._title_phase_frames >= self.TITLE_HOLD_FRAMES:
                    # Switch to scroll phase
                    self._title_scroll_phase = "scroll"
                    self._title_phase_frames = 0
                    self._title_scroll_offset = 0
                # Render static (scroll_offset=0 triggers truncated text)
                self._refresh_xmb_preview(scroll_offset=0, manage_animation=False, from_animation=True)

            elif self._title_scroll_phase == "scroll":
                # Phase 2: Full title scrolling left
                self._title_scroll_offset += self.TITLE_SCROLL_SPEED

                # Check if title has fully exited left edge
                text_w = self._xmb_renderer._get_last_text_width()
                if text_w == 0:
                    # Re-measure if cache is empty
                    try:
                        font = self._xmb_renderer._get_font()
                        from PIL import ImageDraw as PILImageDraw
                        tmp = PILImage.new("RGBA", (1, 1))
                        draw = PILImageDraw.Draw(tmp)
                        text_w, _ = self._xmb_renderer._measure_text(draw, self.title_entry.get().strip(), font)
                    except Exception:
                        # Font measurement failed — stop animation to avoid hanging
                        self._stop_title_animation()
                        return

                # Title fully gone when scroll_offset > text_w
                # (right edge = left - scroll_offset + text_w, need < left)
                if self._title_scroll_offset > text_w:
                    # Fully off-screen left, switch to gap
                    self._title_scroll_phase = "gap"
                    self._title_phase_frames = 0

                self._refresh_xmb_preview(
                    scroll_offset=self._title_scroll_offset,
                    manage_animation=False,
                    from_animation=True
                )

            elif self._title_scroll_phase == "gap":
                # Phase 3: Brief blank (no text)
                self._title_phase_frames += 1
                if self._title_phase_frames >= self.TITLE_GAP_FRAMES:
                    # Back to hold (truncated static)
                    self._title_scroll_phase = "hold"
                    self._title_phase_frames = 0
                # scroll_offset=-1 signals "render blank"
                self._refresh_xmb_preview(scroll_offset=-1, manage_animation=False, from_animation=True)

        except Exception:
            logger.exception("Title animation loop failed")

        # Schedule next frame (~25 fps)
        self._title_scroll_after_id = self.after(self.TITLE_ANIM_INTERVAL, self._title_scroll_loop)

    def _recreate_xmb_preview(self):
        """Destroy and recreate the XMB preview label to clear corrupted state."""
        # Remember the parent frame
        parent = self.xmb_preview.master
        # Destroy the corrupted label
        self.xmb_preview.destroy()

        # Recreate it
        self.xmb_preview = ctk.CTkLabel(
            parent,
            text="No assets loaded",
            width=480,
            height=272,
            fg_color=C.COLOR_BG_DARKEST
        )
        self.xmb_preview.pack(pady=10)