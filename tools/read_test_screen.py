#!/usr/bin/env python3
# GBA Rush PSP, Copyright (C) 2026 Piero Carrieri, GPL-2.0-or-later.

"""Reads the result a third-party GBA test ROM leaves on screen.

  read_test_screen.py jsmolka IMAGE GLYPHS [--vram]
      jsmolka's gba-tests (arm.gba, thumb.gba, memory.gba, bios.gba):
      prints "All tests passed" or "Failed test N". GLYPHS is the
      suite's lib/glyphs.asm, the font the ROMs draw with.
  read_test_screen.py mgba-suite IMAGE FONT [--vram]
      a section of mGBA's test suite built by tests/mgba-suite: prints
      "<section>: <passed>/<total>". FONT is the suite's
      gfx/raw/font.bmp.
  read_test_screen.py font-c FONT DIR
      writes DIR/font.c and DIR/font.h, the suite's font as the 4-bit
      tiles its build expects (what devkitARM's grit makes of font.bmp).

IMAGE is a 240x160 binary PPM: mgba_run --frame, or the frame GBA Rush
presents in PPSSPP (tools/ppsspp_frame.py). Text is read by matching
each 8x8 cell against the ROM's own font. With --vram, IMAGE is
rom_run's --screen dump, which holds VRAM as a mode 3 picture: rom_run
has no PPU, so the text is read from VRAM instead, the mode 4 bitmap
jsmolka draws or the tile map the mGBA suite writes. Exit status 1 when
the result cannot be read; the cells read are then printed to stderr.
"""

import re
import struct
import sys

WIDTH, HEIGHT = 240, 160


def read_image(path):
    """pixels[y][x] is an (r, g, b) of a 240x160 binary PPM."""
    with open(path, "rb") as f:
        data = f.read()
    fields = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not fields or (int(fields.group(1)), int(fields.group(2))) != \
            (WIDTH, HEIGHT):
        raise ValueError(f"{path}: not a 240x160 binary PPM")
    raw = data[fields.end():]
    return [[tuple(raw[(y * WIDTH + x) * 3:(y * WIDTH + x) * 3 + 3])
             for x in range(WIDTH)] for y in range(HEIGHT)]


def ink_map(screen):
    """Dark pixels: black text, and the red of the suite's failed lines."""
    return [[sum(p) < 384 for p in row] for row in screen]


def vram_of_dump(pixels):
    """rom_run's --screen writes VRAM halfword i as pixel i of a mode 3
    picture, keeping 5 bits a channel: bits 0-14 come back, bit 15 not."""
    vram = bytearray()
    for row in pixels:
        for r, g, b in row:
            half = (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10
            vram += bytes((half & 255, half >> 8))
    return vram


def bmp_font(path):
    """The 96 glyphs, ' ' to DEL, of a 4-bit BMP with 32 8x8 glyphs a
    row: glyph[char] is 8 rows of 8 palette indices."""
    with open(path, "rb") as f:
        data = f.read()
    start, = struct.unpack_from("<I", data, 10)
    width, height, _, bits = struct.unpack_from("<iiHH", data, 18)
    compression, = struct.unpack_from("<I", data, 30)
    if bits != 4 or compression:
        raise ValueError(f"{path}: not an uncompressed 4-bit BMP")
    stride = (width * 4 + 31) // 32 * 4

    def index(x, y):
        row = height - 1 - y if height > 0 else y
        byte = data[start + row * stride + x // 2]
        return byte >> 4 if x % 2 == 0 else byte & 15

    glyphs = {}
    for n in range(96):
        tx, ty = n % (width // 8) * 8, n // (width // 8) * 8
        glyphs[chr(32 + n)] = [[index(tx + x, ty + y) for x in range(8)]
                               for y in range(8)]
    return glyphs


def asm_font(path):
    """jsmolka's glyphs.asm: two words a glyph from ' ', a byte a row
    from the top, bit 0 the leftmost pixel."""
    with open(path) as f:
        words = [int(w, 16) for w in re.findall(r"0x([0-9A-Fa-f]{8})",
                                                f.read())]
    glyphs = {}
    for n in range(len(words) // 2):
        rows = struct.pack("<II", words[2 * n], words[2 * n + 1])
        glyphs[chr(32 + n)] = [[rows[y] >> x & 1 for x in range(8)]
                               for y in range(8)]
    return glyphs


def masks(glyphs):
    """Ink mask (64 booleans) -> character; the first of equal glyphs."""
    table = {}
    for char, rows in glyphs.items():
        table.setdefault(tuple(bool(v) for row in rows for v in row), char)
    return table


def read_cell(ink, x, y, table):
    """The character drawn at (x, y): the exact glyph, or the closest one
    within 4 pixels when it is the only one that close; '?' otherwise."""
    cell = tuple(ink[y + j][x + i] for j in range(8) for i in range(8))
    if cell in table:
        return table[cell]
    scored = sorted((sum(a != b for a, b in zip(cell, mask)), char)
                    for mask, char in table.items())
    if scored[0][0] <= 4 and scored[1][0] > scored[0][0]:
        return scored[0][1]
    return "?"


def read_line(ink, x, y, count, table):
    return "".join(read_cell(ink, x + 8 * i, y, table)
                   for i in range(count) if x + 8 * i + 8 <= WIDTH)


def jsmolka(ink, table):
    """The line m_test_eval draws at y 76: x 56 when all pass, x 60 with
    the number of the first failed test."""
    lines = [read_line(ink, x, 76, 16, table).strip() for x in (56, 60)]
    for line in lines:
        if line == "All tests passed" or re.fullmatch(r"Failed test \d+",
                                                       line):
            return line, lines
    return None, lines


# The suite's text map: rows of 32 tiles, shown from row 0 with BG1VOFS
# -4, so row r covers screen lines 8r+4 to 8r+11 and rows 0-18 fit.
SUITE_ROWS = 19
SUITE_MAP = 0x800


def suite_counts(rows):
    """runSuite prints the section's name at row 1, column 0, and the
    passed and total results at column 21."""
    match = re.fullmatch(r"(.*?)\s+(\d+)/(\d+)", rows[1].strip())
    if not match:
        return None
    return f"{match.group(1)}: {match.group(2)}/{match.group(3)}"


def font_c(font, directory):
    words = []
    for n in range(96):
        rows = font[chr(32 + n)]
        tile = bytes(row[x] | row[x + 1] << 4
                     for row in rows for x in range(0, 8, 2))
        words += struct.unpack("<8I", tile)
    with open(f"{directory}/font.c", "w") as f:
        f.write("/* tools/read_test_screen.py font-c: the suite's font.bmp"
                " as 4-bit tiles. */\n\n"
                f"const unsigned int fontTiles[{len(words)}]"
                " __attribute__((aligned(4))) =\n{\n")
        for i in range(0, len(words), 6):
            f.write("  " + ", ".join(f"0x{w:08X}" for w in words[i:i + 6])
                    + ",\n")
        f.write("};\n")
    with open(f"{directory}/font.h", "w") as f:
        f.write(f"#define fontTilesLen {len(words) * 4}\n"
                f"extern const unsigned int fontTiles[{len(words)}];\n")


def main(argv):
    args = [a for a in argv[1:] if a != "--vram"]
    vram = "--vram" in argv
    if len(args) != 3 or args[0] not in ("jsmolka", "mgba-suite", "font-c"):
        sys.stderr.write(__doc__)
        return 2
    command, image, font = args
    if command == "font-c":
        font_c(bmp_font(image), font)
        return 0
    pixels = read_image(image)
    if vram:
        memory = vram_of_dump(pixels)
    else:
        ink = ink_map(pixels)
    if command == "jsmolka":
        if vram:
            ink = [[memory[y * WIDTH + x] != 0 for x in range(WIDTH)]
                   for y in range(HEIGHT)]
        result, read = jsmolka(ink, masks(asm_font(font)))
    else:
        if vram:
            rows = ["".join(chr((struct.unpack_from(
                "<H", memory, SUITE_MAP + 2 * (r * 32 + c))[0] & 0x3FF)
                + 32) for c in range(30)) for r in range(SUITE_ROWS)]
        else:
            table = masks(bmp_font(font))
            rows = [read_line(ink, 0, 8 * r + 4, 30, table)
                    for r in range(SUITE_ROWS)]
        result, read = suite_counts(rows), rows
    if result is None:
        sys.stderr.write("".join(f"|{line}|\n" for line in read))
        print(f"{image}: no result on screen")
        return 1
    print(result)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
