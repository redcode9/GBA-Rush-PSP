#!/usr/bin/env python3
"""Rebuild source/src/open_bios.h, the built-in GBA BIOS image.

The image is the open BIOS that libretro/gpsp and ReGBA ship (sha256
UPSTREAM_SHA256, see source/bios/README.md) with GBA Rush PSP's changes:

- core.s, reset.s and divide.s from source/bios/source are assembled with
  arm-none-eabi-gcc and linked at their places: core.s at 0x0000, reset.s
  at 0x1804, and divide.s, which outgrew its old place, at 0x2440 in the
  unused space after the original code. The unchanged core.s and reset.s
  give back the original bytes, which this script checks.
- The C code of the BIOS cannot be rebuilt byte for byte (see the README),
  so its bytes stay the original ones, with one word patched to do what
  its source change does: the old Div entry that ArcTan2 calls branches to
  the new Div (0x17A8).

Usage:
  python3 scripts/build-open-bios.py            rewrite source/src/open_bios.h
  python3 scripts/build-open-bios.py --check    exit 1 if that would change it
  --base FILE starts from a copy of the original image (libretro's
  bios/open_gba_bios.bin) instead of recovering it from open_bios.h.
"""

import argparse
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "source/src/open_bios.h"
SOURCES = ROOT / "source/bios/source"
UPSTREAM_SHA256 = "661a9afb93624f2c5e77d07dab137bd8d23cff79e8639c62fe621b49bb064749"
SIZE = 0x4000
# The original image is all zeros from here on.
FREE = 0x2440
# Each assembly source: its section and where it goes.
PLACES = (("core.s", ".init", 0x0000), ("reset.s", ".text", 0x1804),
          ("divide.s", ".text", FREE))
# The only bytes that may differ from the original image, with their
# original values.
UPSTREAM_BYTES = (
    (0x00C8, struct.pack("<I", 0x17A8)),      # SWI table: Div
    (0x00CC, struct.pack("<I", 0x1798)),      # SWI table: DivArm
    (0x17A8, struct.pack("<I", 0xE210C102)),  # old Div, called by ArcTan2
    (FREE, bytes(SIZE - FREE)),
)
PREFIX = os.environ.get("ARM_PREFIX", "arm-none-eabi-")

COMMENT = """/*
 * GBA Rush PSP - built-in GBA BIOS
 *
 * Custom GBA BIOS replacement
 * Copyright (c) 2002-2006 VBA Development Team
 * Copyright (c) 2006-2013 VBA-M Development Team
 * Copyright (c) 2013 Normmatt
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * The BIOS image as bytes, written by scripts/build-open-bios.py from the
 * source in source/bios/ (see its README); do not edit it by hand. It is
 * bios/open_gba_bios.bin of libretro/gpsp
 * 5819380c2ffb0900219d700a382ee68c464ebb99, identical to bios/gba_bios.bin
 * of Nebuleon/ReGBA 313ddd06932aae69daaf66d6102d77e0475de88a (sha256
 * 661a9afb93624f2c5e77d07dab137bd8d23cff79e8639c62fe621b49bb064749), with
 * GBA Rush PSP's changes to Div and DivArm. sha256 of this image:
 * %s.
 */

static const u8 open_bios_rom[0x4000] =
{
"""


def run(*command, cwd=None):
    result = subprocess.run(command, capture_output=True, text=True, cwd=cwd)
    if result.returncode != 0:
        sys.exit(" ".join(command) + " failed:\n" + result.stdout + result.stderr)
    return result.stdout


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def header_image(text):
    start = text.index("{", text.index("open_bios_rom[0x4000] ="))
    return bytes(int(value, 16) for value in
                 re.findall(r"0x([0-9a-f]{2})", text[start:text.rindex("}")]))


def original_image(base):
    """The upstream image, checked against its sha256."""
    if base:
        data = Path(base).read_bytes()
    else:
        data = bytearray(header_image(HEADER.read_text()))
        for offset, original in UPSTREAM_BYTES:
            data[offset:offset + len(original)] = original
        data = bytes(data)
    if sha256(data) != UPSTREAM_SHA256:
        sys.exit("the original image does not match sha256 " + UPSTREAM_SHA256)
    return data


def assemble(original, work):
    """Each section of PLACES as bytes, and the addresses of their symbols."""
    objects = []
    for name, _, _ in PLACES:
        target = work / (name[:-2] + ".o")
        run(PREFIX + "gcc", "-mcpu=arm7tdmi", "-mthumb-interwork", "-c",
            str(SOURCES / name), "-o", str(target))
        objects.append(target)
    defined = set(run(PREFIX + "nm", "--defined-only", "--format=just-symbols",
                      *map(str, objects)).split())
    # The C functions keep their original addresses: the ones in the SWI
    # table, and DrawLogo, whose address is the literal at 0x160.
    names = re.findall(r"^\s*\.word\s+(\w+)", (SOURCES / "core.s").read_text(),
                       re.M)
    addresses = {name: struct.unpack_from("<I", original, 0xB0 + 4 * i)[0]
                 for i, name in enumerate(names)}
    addresses["DrawLogo"] = struct.unpack_from("<I", original, 0x160)[0]
    script = "SECTIONS {\n%s  /DISCARD/ : { *(*) }\n}\n" % "".join(
        "  .%s 0x%X : { %s(%s) }\n" % (name[:-2], address, obj.name, section)
        for (name, section, address), obj in zip(PLACES, objects))
    (work / "bios.ld").write_text(script)
    elf = work / "bios.elf"
    run(PREFIX + "ld", "-T", "bios.ld", "-o", elf.name,
        *(obj.name for obj in objects),
        *("--defsym=%s=0x%X" % item for item in sorted(addresses.items())
          if item[0] not in defined), cwd=work)
    sections = []
    for name, _, address in PLACES:
        out = work / (name + ".bin")
        run(PREFIX + "objcopy", "-O", "binary", "-j", "." + name[:-2],
            str(elf), str(out))
        sections.append((address, out.read_bytes()))
    symbols = {}
    for line in run(PREFIX + "nm", str(elf)).splitlines():
        value, _, name = line.split()
        symbols[name] = int(value, 16)
    return sections, symbols


def build(original):
    with tempfile.TemporaryDirectory(prefix="open-bios-") as directory:
        sections, symbols = assemble(original, Path(directory))
    image = bytearray(original)
    for address, code in sections:
        image[address:address + len(code)] = code
    if FREE + len(sections[-1][1]) > SIZE:
        sys.exit("divide.s does not fit in the free space")
    # b Div, from the old entry
    offset = (symbols["Div"] - (0x17A8 + 8)) >> 2
    image[0x17A8:0x17AC] = struct.pack("<I", 0xEA000000 | (offset & 0xFFFFFF))
    allowed = set()
    for start, original_bytes in UPSTREAM_BYTES:
        allowed.update(range(start, start + len(original_bytes)))
    changed = [i for i in range(SIZE) if image[i] != original[i] and i not in allowed]
    if changed:
        sys.exit("the build changed bytes it should not, from 0x%X" % changed[0])
    return bytes(image)


def header_text(image):
    rows = (", ".join("0x%02x" % byte for byte in image[i:i + 16])
            for i in range(0, SIZE, 16))
    return COMMENT % sha256(image) + "  " + ",\n  ".join(rows) + "\n};\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", help="a copy of the original image")
    parser.add_argument("--check", action="store_true",
                        help="only report whether open_bios.h is up to date")
    args = parser.parse_args()
    text = header_text(build(original_image(args.base)))
    if args.check:
        if HEADER.read_text() != text:
            sys.exit("source/src/open_bios.h is not what source/bios builds")
        print("source/src/open_bios.h matches source/bios")
        return
    HEADER.write_text(text)
    print("wrote source/src/open_bios.h, sha256 " + sha256(header_image(text)))


if __name__ == "__main__":
    main()
