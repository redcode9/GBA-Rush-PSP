#!/usr/bin/env python3
"""Print the size of the image an ELF32 file loads into memory.

The image runs from the lowest PT_LOAD segment's start to the highest
PT_LOAD segment's end. The release manifest records it for TempGBA.elf.
"""

import argparse
import pathlib
import re
import struct
import sys


PT_LOAD = 1
PROGRAM_HEADER_SIZE = 32
RAM_BUDGET = pathlib.Path(__file__).resolve().parents[1] / "docs" / "RAM-BUDGET.md"
# The lines of docs/RAM-BUDGET.md that give the PSP-1000 numbers (PRF-4.2).
RAM_BUDGET_KEYS = ("psp1000-image-plus-startup-heap-bytes",
                   "psp1000-min-startup-heap-bytes")


def ram_budget(path=RAM_BUDGET):
    """The PSP-1000 memory numbers measured on hardware, in bytes.

    (load image plus startup heap, lowest startup heap allowed), read from
    two lines of docs/RAM-BUDGET.md that start at the first column, such as
    "psp1000-image-plus-startup-heap-bytes: 25165824". None while the
    document or both lines are missing; an error when only one is there,
    or when either is not one whole number.
    """
    try:
        text = pathlib.Path(path).read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    found = [re.findall(rf"^{key}:(.*)$", text, re.MULTILINE)
             for key in RAM_BUDGET_KEYS]
    if not any(found):
        return None
    numbers = []
    for key, values in zip(RAM_BUDGET_KEYS, found):
        if len(values) != 1 or not re.fullmatch(r"[0-9]+", values[0].strip()):
            raise ValueError(f"{path}: needs one line '{key}: <bytes>'")
        numbers.append(int(values[0]))
    return tuple(numbers)


def load_image_size(path):
    """Highest PT_LOAD end minus lowest PT_LOAD start, in bytes."""
    with open(path, "rb") as stream:
        data = stream.read()
    if data[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not an ELF file")
    if data[4:6] != b"\x01\x01":
        raise ValueError(f"{path}: not a little-endian ELF32 file")
    try:
        (phoff,) = struct.unpack_from("<I", data, 28)
        phentsize, phnum = struct.unpack_from("<HH", data, 42)
        if phentsize < PROGRAM_HEADER_SIZE:
            raise ValueError(f"{path}: bad program header size {phentsize}")
        segments = []
        for index in range(phnum):
            p_type, _, vaddr, _, _, memsz = struct.unpack_from(
             "<6I", data, phoff + index * phentsize)
            if p_type == PT_LOAD:
                segments.append((vaddr, vaddr + memsz))
    except struct.error as error:
        raise ValueError(f"{path}: truncated ELF file ({error})") from error
    if not segments:
        raise ValueError(f"{path}: no PT_LOAD segment")
    return max(end for _, end in segments) - min(start for start, _ in segments)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf")
    args = parser.parse_args(argv)
    try:
        print(load_image_size(args.elf))
    except (OSError, ValueError) as error:
        print(f"elf_info.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
