# Copyright (C) 2026 JxP
# SPDX-License-Identifier: GPL-3.0-or-later
# Original: Copyright gba2psp authors, licensed under LGPL-2.1

"""
RIFF/WAVE AT3 wrapper for PSP SND0.AT3 creation.

Based on gba2psp project's riff.py — adapted as an importable module.
Original: https://github.com/sahlberg/gba2psp

Modifications Copyright (C) 2026 JxP
Original licensed under the GNU Lesser General Public License v2.1 (LGPL-2.1)
https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

Modifications from original (2026):
1. Refactored from standalone CLI script to importable Python module
   (removed argparse, __main__ block, print()/exit()-based I/O)
2. Removed copy_riff() function (not needed for module use)
3. Added RiffError exception class for proper error handling
4. Silent failure in parse_riff() — returns None instead of printing errors
5. Removed dump_riff() function (CLI debug output, not needed for module use)
6. create_riff() raises RiffError instead of printing + exit()
7. Named constant ATRAC3_STEREO_BYTES_PER_SEC = 16537
8. Critical bugfix: loop_end clamping with max(0x800, ...)
9. Code style cleanup (quotes, explicit escapes, pass vs True)
10. Added module docstring with attribution and modification summary
11. Removed unused max_data_size parameter from create_riff()
12. Added SPDX license header
13. Moved ATRAC3_STEREO_BYTES_PER_SEC to shared constants.py
14. Removed unused parse_riff() function
    - Dead code, never called by the builder; removed to reduce module surface
"""

import os
import struct

from constants import ATRAC3_STEREO_BYTES_PER_SEC


class RiffError(Exception):
    """Raised when RIFF creation fails (e.g., invalid EA3 input)."""
    pass


def create_riff(ea3, riff, number_of_samples=0, loop=False):
    """
    Wrap an EA3 raw ATRAC3 bitstream in a RIFF/WAVE container.

    Args:
        ea3: Path to input EA3 file (raw ATRAC3 bitstream)
        riff: Path to output RIFF/WAVE file
        number_of_samples: Total sample count for the fact chunk. If 0, estimated from data size.
        loop: If True, adds smpl chunk with loop points for seamless PSP XMB playback.
    """
    with open(riff, "wb") as f:
        # --- RIFF header ---
        buf = bytearray(12)
        buf[:4] = b"RIFF"
        buf[8:12] = b"WAVE"
        f.write(buf)

        # --- fmt chunk (format descriptor) ---
        _b = bytearray(24)
        _b[:4] = b"fmt "
        struct.pack_into("<H", _b, 8, 0x270)  # compression code (ATRAC3)
        struct.pack_into("<H", _b, 10, 2)     # number of channels (stereo)
        struct.pack_into("<I", _b, 12, 44100)  # sample rate
        struct.pack_into("<I", _b, 16, ATRAC3_STEREO_BYTES_PER_SEC)  # avg bytes per sec
        struct.pack_into("<H", _b, 20, 384)   # block align
        struct.pack_into("<H", _b, 22, 0)     # significant bits per sample
        _b = _b + b"\x0e\x00"
        _b = _b + b"\x01\x00\x00\x10\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00"
        struct.pack_into("<I", _b, 4, len(_b) - 8)
        f.write(_b)

        # --- Read and validate EA3 data ---
        with open(ea3, "rb") as d:
            buf = d.read()
            if buf[:4] != b"EA3\x01":
                raise RiffError(f"Not a valid EA3 file: {ea3}")
            buf = buf[96:]  # Strip EA3 header, keep raw ATRAC3 data

        data_size = len(buf)
        if not number_of_samples:
            # Guesstimate number of samples from data size
            # ATRAC3 stereo: 0xC0 bytes per frame, 0x201 samples per frame
            number_of_samples = int(data_size / 0xC0 * 0x201)

        if loop:
            # --- fact chunk (loop-aware) ---
            _b = bytearray(16)
            _b[:4] = b"fact"
            struct.pack_into("<I", _b, 4, len(_b) - 8)
            struct.pack_into("<I", _b, 8, (number_of_samples & ~0xFFF) - 0x2000)
            struct.pack_into("<I", _b, 12, 0x800)
            f.write(_b)

            # --- smpl chunk (loop markers) ---
            _l = bytearray(24)
            struct.pack_into("<I", _l, 0, 0)          # cue point id
            struct.pack_into("<I", _l, 4, 0)          # type
            struct.pack_into("<I", _l, 8, 0x800)      # start
            # Critical bugfix: clamp loop_end to prevent underflow on short files
            loop_end = max(0x800, (number_of_samples & ~0xFFF) - 0x2000 - 0x2801)
            struct.pack_into("<I", _l, 12, loop_end)  # end
            struct.pack_into("<I", _l, 16, 0)         # fraction
            struct.pack_into("<I", _l, 20, 0)         # play count

            _s = bytearray(36)
            struct.pack_into("<I", _s, 0, 0)          # manufacturer
            struct.pack_into("<I", _s, 4, 0)          # product
            struct.pack_into("<I", _s, 8, 22676)      # sample period
            struct.pack_into("<I", _s, 12, 60)        # midi unity note
            struct.pack_into("<I", _s, 16, 0)         # midi pitch fraction
            struct.pack_into("<I", _s, 20, 0)         # smpte format
            struct.pack_into("<I", _s, 24, 0)         # smpte offset
            struct.pack_into("<I", _s, 28, int(len(_l) / 24))  # num sample loops
            struct.pack_into("<I", _s, 32, len(_l))   # sampler data

            _b = bytearray(8)
            _b[:4] = b"smpl"
            struct.pack_into("<I", _b, 4, len(_s) + len(_l))
            f.write(_b + _s + _l)

        # --- data chunk (raw ATRAC3 frames) ---
        _b = bytearray(8)
        _b[:4] = b"data"
        struct.pack_into("<I", _b, 4, data_size)
        buf = _b + buf
        if len(buf) & 1:
            buf = buf + b"\x00"  # Pad to even length
        f.write(buf)

        if not loop:
            # --- fact chunk (non-looping) ---
            _b = bytearray(12)
            _b[:4] = b"fact"
            struct.pack_into("<I", _b, 4, len(_b) - 8)
            struct.pack_into("<I", _b, 8, number_of_samples)
            f.write(_b)

        # --- LIST chunk (software info) ---
        buf = b"ATRACDENC\x00"
        _b = bytearray(4)
        struct.pack_into("<I", _b, 0, len(buf))
        buf = b"INFOISFT" + _b + buf
        _b = bytearray(4)
        struct.pack_into("<I", _b, 0, len(buf))
        buf = b"LIST" + _b + buf
        if len(buf) & 1:
            buf = buf + b"\x00"
        f.write(buf)

        # --- Update RIFF length in header ---
        f.seek(0, 2)
        x = f.tell() - 8
        _b = bytearray(4)
        struct.pack_into("<I", _b, 0, x)
        f.seek(4)
        f.write(_b)
