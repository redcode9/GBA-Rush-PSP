# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Shared constants for TempGBA4PSP-mod Builder.

Single source of truth for PSP dimensions, asset limits, mode strings,
and UI theming. Import from here — never hardcode elsewhere.
"""

# =============================================================================
# Audio
# =============================================================================

# ATRAC3 stereo @ 132kbps = ~16,537 bytes/sec
# Used to estimate max duration for the 500KB SND0.AT3 limit
ATRAC3_STEREO_BYTES_PER_SEC = 16537

# PSP SND0.AT3 hard size limit (~30 seconds of stereo ATRAC3)
MAX_AT3_SIZE_BYTES = 500 * 1024

# =============================================================================
# PSP Display & Asset Dimensions
# =============================================================================

PSP_SCREEN_W = 480
PSP_SCREEN_H = 272

# ICON0 (icon in XMB game list)
ICON0_W = 144
ICON0_H = 80
ICON0_GBA_SIZE = 80  # For GBA-sized icons (80x80) centered in 144x80 slot

# PIC0 (overlay, bottom-right of XMB)
PIC0_W = 310
PIC0_H = 180

# PIC1 (full-screen background)
PIC1_W = 480
PIC1_H = 272

# XMB layout positions (authentic PSP coordinates)
ICON_X = 20
ICON_Y = 96
PIC0_X = 165
PIC0_Y = 87

# =============================================================================
# Title / Text Limits
# =============================================================================

# PSP reads TITLE as UTF-8 with a hard 127-byte limit
MAX_TITLE_UTF8_BYTES = 127

# =============================================================================
# Asset Filenames
# =============================================================================

# Default images bundled in assets/
DEFAULT_BG_NAME = "default_background-icons_no-icon0_no-text.png"
DEFAULT_ICON_NAME = "default_icon0.png"
DEFAULT_CHROME_NAME = "default_icons.png"
FONT_NAME = "DejaVuSans.ttf"

# =============================================================================
# Image Mode Maps
# =============================================================================
# Centralized so GUI dropdowns and converters never drift out of sync.
# Format: mode_string -> ( (inner_w, inner_h), resize_mode )

ICON_MODE_MAP = {
    "144x80 PSP Fit":     ((ICON0_W, ICON0_H), "fit"),
    "144x80 PSP Stretch": ((ICON0_W, ICON0_H), "stretch"),
    "80x80 GBA Fit":      ((ICON0_GBA_SIZE, ICON0_GBA_SIZE), "fit"),
    "80x80 GBA Stretch":  ((ICON0_GBA_SIZE, ICON0_GBA_SIZE), "stretch"),
}

PIC1_MODE_MAP = {
    "480x272 Fit":        ((PIC1_W, PIC1_H), "fit"),
    "480x272 Stretch":    ((PIC1_W, PIC1_H), "stretch"),
}

PIC0_MODE_MAP = {
    "310x180 Fit":        ((PIC0_W, PIC0_H), "fit"),
    "310x180 Stretch":    ((PIC0_W, PIC0_H), "stretch"),
    "144x80 PSP Fit":     ((ICON0_W, ICON0_H), "fit"),
    "144x80 PSP Stretch": ((ICON0_W, ICON0_H), "stretch"),
    "80x80 GBA Fit":      ((ICON0_GBA_SIZE, ICON0_GBA_SIZE), "fit"),
    "80x80 GBA Stretch":  ((ICON0_GBA_SIZE, ICON0_GBA_SIZE), "stretch"),
    "160x160 GBA x2 Fit":     ((160, 160), "fit"),
    "160x160 GBA x2 Stretch": ((160, 160), "stretch"),
}

# Convenience lists for GUI dropdowns
ICON_MODES = list(ICON_MODE_MAP.keys())
PIC1_MODES = list(PIC1_MODE_MAP.keys())
PIC0_MODES = list(PIC0_MODE_MAP.keys())

# =============================================================================
# Window & Animation
# =============================================================================

WINDOW_W = 715
WINDOW_H = 875

# Title scroll animation tuning
TITLE_SCROLL_SPEED = 4          # pixels per frame
TITLE_HOLD_FRAMES = 50          # ~2 seconds at 25 fps
TITLE_GAP_FRAMES = 13           # ~0.5 seconds at 25 fps
TITLE_ANIM_INTERVAL = 40        # ms between frames (25 fps)

# =============================================================================
# UI Colors
# =============================================================================

COLOR_BG_DARKEST = "#0a0a0a"
COLOR_BG_DARK = "#1a1a1a"
COLOR_BG_MEDIUM = "#121212"
COLOR_ACCENT = "#1f538d"
COLOR_SUCCESS = "#4CAF50"
COLOR_WARNING = "#FFA500"
COLOR_ERROR = "#F44336"
COLOR_TEXT_MAIN = "#e0e0e0"
COLOR_TEXT_MUTED = "gray"
COLOR_TEXT_DIM = "#666666"      # copyright, secondary metadata