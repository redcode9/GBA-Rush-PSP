#!/usr/bin/env python3
# GBA Rush PSP
#
# Copyright (C) 2026 Piero Carrieri
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.

"""Fill in the cartridge header of a test ROM built by tests/gba-rom.mk.

Usage: gba_header.py ROM TITLE GAME_CODE

Writes the title (upper case, cut to 12 characters), the game code, maker
code "00", the fixed byte 96h and the complement check, as GBATEK lays them
out, and pads the file with FFh to a power of two of at least 32 KiB. The
logo at 04h-9Fh is left as the build made it (blank): see tests/gba-rom.mk.
"""

import sys


def complement(header):
    """The header check the BIOS verifies: bytes A0h-BCh."""
    return (-(sum(header[0xA0:0xBD]) + 0x19)) & 0xFF


def fix(rom, title, code):
    size = 0x8000
    while size < len(rom):
        size *= 2
    rom = bytearray(rom) + b"\xff" * (size - len(rom))
    rom[0xA0:0xAC] = title.upper().encode("ascii")[:12].ljust(12, b"\0")
    rom[0xAC:0xB0] = code.encode("ascii")[:4].ljust(4, b"0")
    rom[0xB0:0xB2] = b"00"
    rom[0xB2] = 0x96
    rom[0xB3:0xBD] = bytes(10)
    rom[0xBD] = complement(rom)
    return rom


def main(argv):
    if len(argv) != 4:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    with open(argv[1], "rb") as f:
        rom = f.read()
    if len(rom) < 0xC0 or rom[3] != 0xEA:
        print(f"{argv[1]}: no branch at the start of the header",
              file=sys.stderr)
        return 1
    with open(argv[1], "wb") as f:
        f.write(fix(rom, argv[2], argv[3]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
