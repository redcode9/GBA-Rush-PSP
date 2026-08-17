# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later

"""
Asset conversion for TempGBA4PSP-mod Single-Game Launcher Builder.

Handles:
  - Image resizing and format conversion (ICON0, PIC0, PIC1)
  - Audio conversion: source -> WAV -> EA3 (ATRAC3) -> RIFF/WAVE (SND0.AT3)
  - YouTube audio download via yt-dlp
  - Generic URL file download with browser-like headers
"""

import subprocess
import sys
import tempfile
import wave
from pathlib import Path

from PIL import Image
import requests
import constants as C

import logging

logger = logging.getLogger("tempgba_builder")

from constants import ATRAC3_STEREO_BYTES_PER_SEC


class ConvertError(Exception):
    """Raised when any conversion step fails."""
    pass


# ---------------------------------------------------------------------------
# Image conversion
# ---------------------------------------------------------------------------

# ICON0 / PIC0 icon-mode converter (144x80 canvas, centered)
def convert_icon(source_path: Path, mode: str, output_path: Path):
    """
    Convert source image to PSP icon format (144x80 PNG).

    Args:
        source_path: Path to source image
        mode: GUI dropdown string from ICON_MODE_MAP (e.g., "144x80 PSP Fit")
        output_path: Where to write the 144x80 PNG
    """
    if mode not in C.ICON_MODE_MAP:
        raise ConvertError(f"Unknown icon mode: {mode}")

    inner_size, resize_mode = C.ICON_MODE_MAP[mode]
    inner_w, inner_h = inner_size

    try:
        img = Image.open(source_path).convert("RGBA")
    except Exception as e:
        raise ConvertError(f"Cannot open image: {e}")

    # Create transparent canvas and center the resized image
    canvas = Image.new("RGBA", (C.ICON0_W, C.ICON0_H), (0, 0, 0, 0))

    if resize_mode == "stretch":
        img = img.resize((inner_w, inner_h), Image.LANCZOS)
    else:  # fit
        img.thumbnail((inner_w, inner_h), Image.LANCZOS)

    x = (C.ICON0_W - img.width) // 2
    y = (C.ICON0_H - img.height) // 2
    canvas.paste(img, (x, y), img)
    canvas.save(output_path, "PNG")


# PIC1 background converter (centered fit)
def convert_pic(source_path: Path, output_path: Path, size: tuple = (C.PIC1_W, C.PIC1_H), mode: str = "stretch"):
    """
    Resize image to the specified size for PIC1 (background).

    Args:
        source_path: Path to source image
        output_path: Where to write the resized PNG
        size: Target dimensions (default 480x272)
        mode: GUI dropdown string from PIC1_MODE_MAP
    """
    if mode not in C.PIC1_MODE_MAP:
        raise ConvertError(f"Unknown pic mode: {mode}")

    _, resize_mode = C.PIC1_MODE_MAP[mode]

    try:
        img = Image.open(source_path).convert("RGBA")
    except Exception as e:
        raise ConvertError(f"Cannot open image: {e}")

    if resize_mode == "fit":
        # Maintain aspect ratio, center on transparent canvas
        canvas = Image.new("RGBA", size, (0, 0, 0, 0))
        img.thumbnail(size, Image.LANCZOS)
        x = (size[0] - img.width) // 2
        y = (size[1] - img.height) // 2
        canvas.paste(img, (x, y), img)
        canvas.save(output_path, "PNG")
    else:  # stretch
        img = img.resize(size, Image.LANCZOS)
        img.save(output_path, "PNG")


# PIC0 overlay converter (bottom-right anchored fit)
def convert_pic0(source_path: Path, output_path: Path,
                 outer_size: tuple = (C.PIC0_W, C.PIC0_H), mode: str = "stretch"):
    """
    Resize image for PIC0 overlay. Output is always outer_size (310x180).

    The source image is resized per mode, then pasted at the bottom-right
    corner of a outer_size transparent canvas.

    Args:
        mode: GUI dropdown string (e.g., "310x180 Fit", "80x80 GBA Stretch")
    """
    if mode not in C.PIC0_MODE_MAP:
        raise ConvertError(f"Unknown PIC0 mode: {mode}")

    inner_size, resize_mode = C.PIC0_MODE_MAP[mode]

    try:
        img = Image.open(source_path).convert("RGBA")
    except Exception as e:
        raise ConvertError(f"Cannot open image: {e}")

    canvas = Image.new("RGBA", outer_size, (0, 0, 0, 0))

    if resize_mode == "fit":
        img.thumbnail(inner_size, Image.LANCZOS)
    else:  # stretch
        img = img.resize(inner_size, Image.LANCZOS)

    # Bottom-right positioning
    x = outer_size[0] - img.width
    y = outer_size[1] - img.height
    canvas.paste(img, (x, y), img)
    canvas.save(output_path, "PNG")


# ---------------------------------------------------------------------------
# Audio conversion
# ---------------------------------------------------------------------------

def convert_audio(source_path: Path, output_path: Path, loop: bool = False,
                  start_ms: int = 0, end_ms: int = 0, max_size: int = C.MAX_AT3_SIZE_BYTES):
    """
    Convert audio to SND0.AT3 using ffmpeg -> wav -> atracdenc -> riff wrapper.

    Supports start/end time selection (in milliseconds).
    If end_ms is 0 or not specified, auto-trims to fit the 500KB limit.

    The pipeline:
      1. ffmpeg: source -> 44100Hz stereo PCM WAV (with optional time slice)
      2. atracdenc: WAV -> EA3 raw ATRAC3 bitstream
      3. riff.create_riff: EA3 -> RIFF/WAVE container with loop markers
      4. Size check: if > max_size, auto-trim by re-slicing the master WAV
    """
    ffmpeg = _find_tool("ffmpeg")
    atracdenc = _find_tool("atracdenc")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        wav_path = tmp / "temp.wav"
        ea3_path = tmp / "temp.ea3"

        # Build ffmpeg args with optional time selection
        # ATRAC3 stereo ~132kbps = ~16.5 KB/sec. 500KB limit ≈ 30s max.
        # Start with a conservative estimate to avoid re-encode loops.
        estimated_max_duration = int(max_size / ATRAC3_STEREO_BYTES_PER_SEC)
        duration = estimated_max_duration

        cmd = [
            ffmpeg, "-y", "-i", str(source_path),
            "-ar", "44100",
            "-ac", "2",
            "-acodec", "pcm_s16le",
        ]

        # Add start time offset
        if start_ms > 0:
            cmd.extend(["-ss", str(start_ms / 1000.0)])

        # Add end time or duration limit
        if end_ms > start_ms:
            segment_duration = (end_ms - start_ms) / 1000.0
            cmd.extend(["-t", str(segment_duration)])
            duration = segment_duration  # user-specified segment, trust it
        else:
            cmd.extend(["-t", str(duration)])

        cmd.append(str(wav_path))
        _run(cmd, "ffmpeg conversion failed")

        # Check actual WAV duration
        with wave.open(str(wav_path), "r") as wf:
            number_of_samples = wf.getnframes()
            actual_duration = number_of_samples / wf.getframerate()

        # Step 2: atracdenc — WAV -> EA3 raw bitstream
        cmd = [atracdenc, "--encode=atrac3", "-i", str(wav_path), "-o", str(ea3_path)]
        _run(cmd, "atracdenc conversion failed")

        # Step 3: riff.py — wrap EA3 in proper RIFF/WAVE container
        from riff import create_riff
        create_riff(
            str(ea3_path),
            str(output_path),
            number_of_samples=number_of_samples,
            loop=loop
        )

        # Step 4: Check size; enforce max_size regardless of manual or auto segment
        size = output_path.stat().st_size
        if size > max_size:
            if end_ms > 0:
                # Manual segment specified but still too large — error out
                segment_duration_sec = (end_ms - start_ms) / 1000.0
                raise ConvertError(
                    f"Selected audio segment ({segment_duration_sec:.1f}s) exceeds "
                    f"PSP SND0.AT3 size limit ({max_size/1024:.0f}KB). "
                    f"Output is {size/1024:.0f}KB. "
                    f"Please use a shorter segment or leave End time blank for auto-trim."
                )

            # Auto-trim: save master WAV for re-slicing (only needed here)
            master_wav = tmp / "master.wav"
            import shutil
            shutil.copy(str(wav_path), str(master_wav))

            # Auto-trim: binary-search-like convergence for fewer iterations
            while size > max_size and duration > 5:
                # Linear estimate: if N seconds = size bytes, then target seconds = N * max_size / size
                # Apply 0.95 safety margin to guarantee undershoot
                new_duration = int(duration * 0.95 * max_size / size)
                if new_duration == duration:
                    # Stuck: reduce by 1 second to guarantee progress
                    new_duration = duration - 1
                duration = max(5, new_duration)

                # Re-slice from master WAV
                cmd = [
                    ffmpeg, "-y", "-i", str(master_wav),
                    "-ar", "44100", "-ac", "2",
                    "-acodec", "pcm_s16le",
                    "-t", str(duration),
                    str(wav_path)
                ]
                _run(cmd, "ffmpeg WAV re-slice failed")

                with wave.open(str(wav_path), "r") as wf:
                    number_of_samples = wf.getnframes()

                cmd = [atracdenc, "--encode=atrac3", "-i", str(wav_path), "-o", str(ea3_path)]
                _run(cmd, "atracdenc re-conversion failed")

                create_riff(
                    str(ea3_path),
                    str(output_path),
                    number_of_samples=number_of_samples,
                    loop=loop
                )
                size = output_path.stat().st_size

            if size > max_size:
                raise ConvertError(
                    f"AT3 output cannot be reduced below {max_size/1024:.0f}KB "
                    f"even at 5 seconds (got {size/1024:.0f}KB)"
                )


def download_youtube(url: str, output_path: Path):
    """
    Download audio from YouTube URL using yt-dlp.

    Extracts best audio, converts to WAV, and moves to output_path.
    """
    ytdlp = _find_tool("yt-dlp")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        cmd = [
            ytdlp, "-x", "--audio-format", "wav",
            "--audio-quality", "0",
            "--no-playlist",           # Only download the single video, not the playlist
            "-o", str(tmp / "audio.%(ext)s"),
            "--max-filesize", "50M",
            "--",
            url
        ]
        _run(cmd, "YouTube download failed")

        wav_files = list(tmp.glob("*.wav"))
        if not wav_files:
            raise ConvertError("yt-dlp did not produce a WAV file")

        import shutil
        shutil.move(wav_files[0], output_path)


# ---------------------------------------------------------------------------
# URL download
# ---------------------------------------------------------------------------

def download_url(url: str, output_path: Path):
    """
    Download file from URL with browser-like headers.

    Uses requests with a realistic User-Agent and proper error handling.
    Validates that the downloaded file is non-empty.
    """
    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
        'Accept': 'image/avif,image/webp,image/apng,image/*,*/*;q=0.8',
        'Accept-Language': 'en-US,en;q=0.9',
        'Referer': 'https://www.google.com/',
    }
    try:
        r = requests.get(url, timeout=30, stream=True, headers=headers, allow_redirects=True)
        r.raise_for_status()

        # Check we got actual data, not an empty response
        content_length = r.headers.get('Content-Length')
        if content_length and int(content_length) == 0:
            raise ConvertError("Server returned empty file (0 bytes)")

        with open(output_path, "wb") as f:
            for chunk in r.iter_content(chunk_size=8192):
                if chunk:
                    f.write(chunk)

        # Verify file was actually written
        if output_path.stat().st_size == 0:
            raise ConvertError("Downloaded file is 0 bytes")

    except requests.exceptions.HTTPError as e:
        logger.error("HTTP download error: %s %s", e.response.status_code, e.response.reason)
        raise ConvertError(f"HTTP {e.response.status_code}: {e.response.reason}")
    except requests.exceptions.ConnectionError:
        logger.error("Network connection failed for URL download")
        raise ConvertError("Connection failed. Check your internet connection.")
    except requests.exceptions.Timeout:
        logger.error("Download timed out after 30 seconds")
        raise ConvertError("Download timed out after 30 seconds.")
    except Exception as e:
        logger.exception("Unexpected download failure")
        raise ConvertError(f"Download failed: {e}")


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _find_tool(name: str) -> str:
    """
    Find bundled tool or system tool.

    Search order:
      1. PyInstaller bundle (_MEIPASS)
      2. Script directory
      3. System PATH
    """
    # PyInstaller bundle
    if hasattr(sys, '_MEIPASS'):
        for suffix in ["", ".exe"]:
            p = Path(sys._MEIPASS) / (name + suffix)
            if p.exists():
                return str(p)

    # Script directory
    for suffix in ["", ".exe"]:
        p = Path(__file__).parent / (name + suffix)
        if p.exists():
            return str(p)

    # PATH
    import shutil as sh
    path = sh.which(name) or sh.which(f"{name}.exe")
    if path:
        return path

    raise ConvertError(f"Required tool not found: {name}")


def _run(cmd, error_msg, timeout=300):
    """
    Run external command with timeout to prevent UI hangs.

    Raises ConvertError on non-zero exit or timeout.
    """
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        raise ConvertError(f"{error_msg}: Timed out after {timeout} seconds")
    if result.returncode != 0:
        err = result.stderr[-500:] if result.stderr else "unknown error"
        raise ConvertError(f"{error_msg}: {err}")
