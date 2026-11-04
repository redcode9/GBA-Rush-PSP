#!/usr/bin/env python3
# GBA Rush PSP
#
# Copyright (C) 2026 Piero Carrieri
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.

"""Print the result signature a GBA Rush test ROM left in its save file.

Usage: decode_test_rom.py [--allow-fail NAME,...] SAVE

The test ROMs (tests/core-rom, tests/save-rom; layout in
tests/gba-rom/result.h) write the signature at the start of their save chip,
SRAM, Flash or EEPROM alike, so this reads it from offset 0 of the emulator's
.sav file. The case names come from the signature itself.

Exit status: 0 when every case passed or was skipped, 1 when one failed,
hung or did not run (except failures of the cases --allow-fail names),
2 when the file holds no valid signature.
"""

import argparse
import struct
import sys
import zlib

MAGIC = b"GRTS"
VERSION = 1
HEADER_SIZE = 48
NO_CASE = 0xFFFF
STATUS = ("NOT RUN", "PASS", "FAIL", "SKIP", "HUNG")


def parse(data):
    """The signature at the start of data as a dict; ValueError if none."""
    if len(data) < HEADER_SIZE or data[:4] != MAGIC:
        raise ValueError("no test ROM signature")
    version, count, running, boots, crc, size = struct.unpack_from(
        "<BxHHHII", data, 4)
    if version != VERSION:
        raise ValueError(f"signature version {version}, not {VERSION}")
    if size > len(data) or size < HEADER_SIZE:
        raise ValueError(f"signature size {size} does not fit the file")
    if zlib.crc32(data[:12] + bytes(4) + data[16:size]) != crc:
        raise ValueError("signature CRC does not match")
    details_at = HEADER_SIZE + (count + 3) // 4 * 4
    names_at = details_at + count * 4
    names = data[names_at:size].split(b"\0")[:count]
    if names_at > size or len(names) != count:
        raise ValueError("signature is cut short")
    rom = data[20:HEADER_SIZE].split(b"\0")[0].decode("ascii", "replace")
    cases = []
    for i in range(count):
        status = data[HEADER_SIZE + i]
        detail, = struct.unpack_from("<I", data, details_at + i * 4)
        cases.append((names[i].decode("ascii", "replace"),
                      STATUS[status] if status < len(STATUS) else
                      f"status {status}", detail))
    return {"rom": rom, "boots": boots,
            "running": None if running == NO_CASE else running,
            "cases": cases}


def report(signature, allow_fail=()):
    """Lines to print and the exit status."""
    cases = signature["cases"]
    running = signature["running"]
    lines = [f"{signature['rom']}: {len(cases)} cases, "
             f"boot {signature['boots']}"]
    if running is not None and running < len(cases):
        lines.append(f"stopped while running {cases[running][0]}: "
                     "it hung or the emulator ended")
    bad = 0
    for i, (name, status, detail) in enumerate(cases):
        shown = "RUNNING" if i == running else status
        lines.append(f"  {name:<24} {shown:<8} {detail:08X}")
        if shown == "FAIL" and name in allow_fail:
            continue
        if shown not in ("PASS", "SKIP"):
            bad += 1
    return lines, 1 if bad else 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Print the result signature of a GBA Rush test ROM.")
    parser.add_argument("--allow-fail", default="",
                        help="comma-separated cases whose failure is "
                        "expected here")
    parser.add_argument("save", help="the emulator's .sav file")
    args = parser.parse_args(argv)
    with open(args.save, "rb") as f:
        data = f.read()
    try:
        signature = parse(data)
    except ValueError as error:
        print(f"{args.save}: {error}", file=sys.stderr)
        return 2
    lines, status = report(signature,
                           [n for n in args.allow_fail.split(",") if n])
    print("\n".join(lines))
    return status


if __name__ == "__main__":
    sys.exit(main())
