# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
PSP XMB preview renderer.

Renders a 480x272 preview image from asset paths and mode settings.
Uses PIL only — no Tkinter or customtkinter dependencies. This allows it to
run in background threads safely and be reused without GUI coupling.
"""

from pathlib import Path

from PIL import Image as PILImage, ImageDraw as PILImageDraw
import constants as C

import logging

logger = logging.getLogger("tempgba_builder")


class XMBRenderer:
    """
    Renders a PSP XMB preview image from asset paths and mode settings.

    Layer order (bottom to top):
      1. Base background (PIC1 or default)
      2. Title text (only when no custom PIC1 and no custom PIC0)
      3. Selected-state chrome overlay (only on custom backgrounds)
      4. ICON0 (game icon or placeholder)
      5. PIC0 overlay (bottom-right corner)
      6. Screen edge border (subtle white outline)
    """

    # PSP XMB display constants
    XMB_W, XMB_H = C.PSP_SCREEN_W, C.PSP_SCREEN_H
    ICON_X, ICON_Y = C.ICON_X, C.ICON_Y
    ICON_W, ICON_H = C.ICON0_W, C.ICON0_H
    PIC0_W, PIC0_H = C.PIC0_W, C.PIC0_H
    PIC0_X, PIC0_Y = C.PIC0_X, C.PIC0_Y
    TITLE_X_OFFSET = 10          # gap between icon right-edge and text
    TITLE_RIGHT_MARGIN = 18      # 18 px from right edge of 480 px screen
    TITLE_FONT_SIZE = 14

    def __init__(self, assets_dir: Path, font_loader):
        """
        Args:
            assets_dir: Path to the assets/ folder containing default images and fonts
            font_loader: Callable(size) -> PIL ImageFont
        """
        self.assets_dir = assets_dir
        self._font_loader = font_loader
        self._font = None

        # Reusable buffer for title scroll animation (avoids per-frame allocation)
        self._scroll_buffer = PILImage.new("RGBA", (self.XMB_W, self.XMB_H), (0, 0, 0, 0))

    def _get_last_text_width(self):
        """Return cached width of last rendered title, or 0 if unknown."""
        return getattr(self, '_last_text_width', 0)

    def _get_font(self):
        """Lazy-load the font at TITLE_FONT_SIZE."""
        if self._font is None:
            self._font = self._font_loader(self.TITLE_FONT_SIZE)
        return self._font

    def title_needs_scroll(self, title: str) -> bool:
        """
        Return True if the title is too wide for the static space.

        The static space is between the icon's right edge and TITLE_RIGHT_MARGIN.
        """
        if not title:
            return False
        try:
            font = self._get_font()
            tmp = PILImage.new("RGBA", (1, 1))
            draw = PILImageDraw.Draw(tmp)
            bbox = draw.textbbox((0, 0), title, font=font)
            text_w = bbox[2] - bbox[0]
            self._last_text_width = text_w
            left = self.ICON_X + self.ICON_W + self.TITLE_X_OFFSET
            static_right = self.XMB_W - self.TITLE_RIGHT_MARGIN
            return text_w > (static_right - left)
        except Exception:
            return False

    def render(self, title: str, icon_path: Path, icon_mode: str,
               pic0_path: Path, pic0_mode: str,
               pic1_path: Path, pic1_mode: str,
               scroll_offset: int = 0) -> PILImage.Image:
        """
        Render full XMB preview. Returns PIL RGBA Image.

        Args:
            scroll_offset: 0 = static truncated, >0 = scroll phase (px moved from start),
                          -1 = blank (gap phase)
        """
        # --- Layer 1: Base background ---
        canvas = self._render_base(pic1_path, pic1_mode)

        # --- Title (only when no custom PIC1 and no custom PIC0) ---
        if not pic1_path and not pic0_path:
            self._render_title(canvas, title, scroll_offset)

        # --- Layer 3: Selected-state chrome ---
        # Only draw chrome on custom backgrounds; default already has it baked in
        self._render_chrome(canvas, has_custom_pic1=bool(pic1_path))

        # --- Layer 4: Icon ---
        self._render_icon(canvas, icon_path, icon_mode)

        # --- Layer 5: PIC0 overlay ---
        self._render_pic0(canvas, pic0_path, pic0_mode)

        # --- Layer 6: PSP screen edge border ---
        # Subtle border so users can always see the 480×272 screen boundaries,
        # especially when PIC1 Fit mode leaves transparent/black letterboxing.
        # Drawn on a separate overlay for correct alpha blending.
        border_overlay = PILImage.new("RGBA", (self.XMB_W, self.XMB_H), (0, 0, 0, 0))
        draw = PILImageDraw.Draw(border_overlay)
        draw.rectangle([0, 0, self.XMB_W - 1, self.XMB_H - 1],
                       outline=(255, 255, 255, 60), width=1)
        canvas = PILImage.alpha_composite(canvas, border_overlay)

        return canvas

    def _render_base(self, pic1_path: Path, pic1_mode: str) -> PILImage.Image:
        """
        Render the background layer.

        If a custom PIC1 is provided, resize it per mode. Otherwise use the
        default background image (which includes baked-in chrome and layout).
        """
        if pic1_path:
            canvas = PILImage.new("RGBA", (self.XMB_W, self.XMB_H), (0, 0, 0, 255))
            bg = PILImage.open(pic1_path).convert("RGBA")
            if pic1_mode == "480x272 Stretch":
                img = bg.resize((self.XMB_W, self.XMB_H), PILImage.LANCZOS)
                canvas.paste(img, (0, 0), img)
            else:
                # Fit: maintain aspect ratio, center on black background
                bg.thumbnail((self.XMB_W, self.XMB_H), PILImage.LANCZOS)
                c = PILImage.new("RGBA", (self.XMB_W, self.XMB_H), (0, 0, 0, 255))
                x = (self.XMB_W - bg.width) // 2
                y = (self.XMB_H - bg.height) // 2
                c.paste(bg, (x, y), bg)
                canvas.paste(c, (0, 0), c)
            return canvas

        # No custom PIC1: use default background from assets/
        base_path = self.assets_dir / C.DEFAULT_BG_NAME
        if base_path.exists():
            return PILImage.open(base_path).convert("RGBA")
        # Ultimate fallback: solid dark color
        return PILImage.new("RGBA", (self.XMB_W, self.XMB_H), (20, 20, 30, 255))

    def _measure_text(self, draw, text, font):
        """
        Measure text width/height, handling both FreeType and bitmap fonts.

        Pillow 10+ removed textsize(), so we use textbbox() for FreeType fonts
        and getmask().getsize() for bitmap fonts.
        """
        try:
            bbox = draw.textbbox((0, 0), text, font=font)
            return bbox[2] - bbox[0], bbox[3] - bbox[1]
        except AttributeError:
            # Bitmap font (load_default) — Pillow 10+ removed textsize()
            mask = font.getmask(text)
            return mask.getsize()

    def _render_title(self, canvas: PILImage.Image, title: str, scroll_offset: int = 0):
        """
        Render title with static truncation or marquee scroll.

        Args:
            scroll_offset: 0 = static truncated, >0 = scroll phase (px moved from start),
                          -1 = blank (gap phase)
        """
        if not title:
            return
        try:
            font = self._get_font()
            draw = PILImageDraw.Draw(canvas)

            left = self.ICON_X + self.ICON_W + self.TITLE_X_OFFSET   # 174
            static_right = self.XMB_W - self.TITLE_RIGHT_MARGIN       # 462 (static edge)
            scroll_right = self.XMB_W - 5                             # 475 (scroll edge)
            available_w_static = static_right - left                    # 288
            available_w_scroll = scroll_right - left                   # 301

            # Measure actual pixel bounds
            text_w, text_h = self._measure_text(draw, title, font)
            self._last_text_width = text_w  # cache for animation loop
            y = self.ICON_Y + self.ICON_H // 2 - text_h // 2

            # --- Fits in static width: show full title ---
            if text_w <= available_w_static:
                # Shadow + text for readability
                draw.text((left + 1, y + 1), title, font=font, fill=(0, 0, 0, 140))
                draw.text((left, y), title, font=font, fill=(240, 240, 240, 255))
                return

            # --- Too long: static truncated or scroll ---
            if scroll_offset <= 0:
                # scroll_offset == -1: blank (gap phase)
                if scroll_offset == -1:
                    return

                # Static truncated with ... at right edge
                ellipsis = "..."
                ew, _ = self._measure_text(draw, ellipsis, font)

                # Binary search: find how many chars fit before static_right
                lo, hi = 0, len(title)
                while lo < hi:
                    mid = (lo + hi + 1) // 2
                    test = title[:mid]
                    tw, _ = self._measure_text(draw, test, font)
                    # Char right edge must be at or before static_right
                    if tw <= available_w_static:
                        lo = mid
                    else:
                        hi = mid - 1

                truncated = title[:lo] + ellipsis
                draw.text((left + 1, y + 1), truncated, font=font, fill=(0, 0, 0, 140))
                draw.text((left, y), truncated, font=font, fill=(240, 240, 240, 255))
                return

            # --- Scroll: full title, clipped to [left, scroll_right] ---
            x = left - scroll_offset

            # Clear reusable buffer
            self._scroll_buffer.paste((0, 0, 0, 0), (0, 0, self.XMB_W, self.XMB_H))

            buf_draw = PILImageDraw.Draw(self._scroll_buffer)
            buf_draw.text((x + 1, y + 1), title, font=font, fill=(0, 0, 0, 140))
            buf_draw.text((x, y), title, font=font, fill=(240, 240, 240, 255))

            # Crop to scroll window and paste
            box = self._scroll_buffer.crop((left, 0, scroll_right, canvas.height))
            canvas.paste(box, (left, 0), box)

        except Exception:
            logger.exception("Preview title rendering failed")

    def _render_chrome(self, canvas: PILImage.Image, has_custom_pic1: bool = False):
        """
        Draw the XMB chrome overlay (selected-state UI elements).

        Only drawn when a custom PIC1 is selected because the default background
        already includes chrome baked into the image.
        """
        if not has_custom_pic1:
            return
        chrome_path = self.assets_dir / C.DEFAULT_CHROME_NAME
        if chrome_path.exists():
            chrome = PILImage.open(chrome_path).convert("RGBA")
            canvas.paste(chrome, (0, 0), chrome)

    def _render_icon(self, canvas: PILImage.Image, icon_path: Path, icon_mode: str):
        """
        Draw the ICON0 (game icon) at the authentic PSP XMB position.

        If no icon is provided, shows the default placeholder.
        Supports four resize modes: PSP Fit/Stretch and GBA Fit/Stretch.
        """
        if not icon_path:
            placeholder_path = self.assets_dir / C.DEFAULT_ICON_NAME
            if placeholder_path.exists():
                placeholder = PILImage.open(placeholder_path).convert("RGBA")
                canvas.paste(placeholder, (self.ICON_X, self.ICON_Y), placeholder)
            return

        icon = PILImage.open(icon_path).convert("RGBA")

        if icon_mode == "144x80 PSP Stretch":
            icon = icon.resize((self.ICON_W, self.ICON_H), PILImage.LANCZOS)
            x, y = self.ICON_X, self.ICON_Y
        elif icon_mode == "144x80 PSP Fit":
            icon.thumbnail((self.ICON_W, self.ICON_H), PILImage.LANCZOS)
            x = self.ICON_X + (self.ICON_W - icon.width) // 2
            y = self.ICON_Y + (self.ICON_H - icon.height) // 2
        elif icon_mode == "80x80 GBA Stretch":
            icon = icon.resize((80, 80), PILImage.LANCZOS)
            x = self.ICON_X + (self.ICON_W - 80) // 2
            y = self.ICON_Y + (self.ICON_H - 80) // 2
        else:  # "80x80 GBA Fit"
            icon.thumbnail((80, 80), PILImage.LANCZOS)
            x = self.ICON_X + (self.ICON_W - icon.width) // 2
            y = self.ICON_Y + (self.ICON_H - icon.height) // 2

        canvas.paste(icon, (x, y), icon)

    def _render_pic0(self, canvas: PILImage.Image, pic0_path: Path, pic0_mode: str):
        """
        Draw the PIC0 overlay at the bottom-right corner.

        PIC0 is always rendered onto a 310x180 canvas, then pasted at the
        bottom-right of the 480x272 screen. The source image is resized per
        mode and positioned at the bottom-right of the 310x180 sub-canvas.
        """
        if not pic0_path:
            return

        overlay = PILImage.open(pic0_path).convert("RGBA")

        # Determine inner image size from mode string
        if "310x180" in pic0_mode:
            inner_w, inner_h = 310, 180
        elif "144x80" in pic0_mode:
            inner_w, inner_h = 144, 80
        elif "160x160" in pic0_mode:
            inner_w, inner_h = 160, 160
        else:  # 80x80
            inner_w, inner_h = 80, 80

        # Sub-canvas for PIC0 (310x180, transparent)
        sub_canvas = PILImage.new("RGBA", (self.PIC0_W, self.PIC0_H), (0, 0, 0, 0))

        if "Stretch" in pic0_mode:
            overlay = overlay.resize((inner_w, inner_h), PILImage.LANCZOS)
        else:
            overlay.thumbnail((inner_w, inner_h), PILImage.LANCZOS)

        # Bottom-right positioning within the 310x180 sub-canvas
        sub_x = self.PIC0_W - overlay.width
        sub_y = self.PIC0_H - overlay.height
        sub_canvas.paste(overlay, (sub_x, sub_y), overlay)

        # Paste sub-canvas onto main canvas at PIC0 position
        canvas.paste(sub_canvas, (self.PIC0_X, self.PIC0_Y), sub_canvas)