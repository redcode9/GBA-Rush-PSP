"""Tests of tools/read_test_screen.py on synthetic screens drawn with
synthetic fonts, laid out as jsmolka's gba-tests and mGBA's test suite
draw their results (the real fonts are in ext/, which tests do not need)."""

import contextlib
import io
import os
import random
import re
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import read_test_screen as screen  # noqa: E402

WHITE, BLACK, RED = (248, 248, 248), (0, 0, 0), (248, 0, 0)


def font_rows():
    """96 distinct glyphs, ' ' blank: 8 rows of 8 0/1 pixels each."""
    rng = random.Random(1)
    rows = {" ": [[0] * 8 for _ in range(8)]}
    for n in range(1, 96):
        rows[chr(32 + n)] = [[rng.randint(0, 1) for _ in range(8)]
                             for _ in range(8)]
    return rows


FONT = font_rows()


def glyphs_asm(path):
    """jsmolka's layout: two words a glyph, a byte a row, bit 0 left."""
    words = []
    for n in range(96):
        data = bytes(sum(bit << x for x, bit in enumerate(row))
                     for row in FONT[chr(32 + n)])
        words += struct.unpack("<II", data)
    with open(path, "w") as f:
        f.write("glyphs:\n        dw      " +
                ",".join(f"0x{w:08X}" for w in words) + "\n")


def font_bmp(path):
    """A 256x24 4-bit BMP, 32 glyphs a row, ink palette index 1."""
    width, height, stride = 256, 24, 128
    pixels = bytearray(stride * height)
    for n in range(96):
        tx, ty = n % 32 * 8, n // 32 * 8
        for y, row in enumerate(FONT[chr(32 + n)]):
            for x, bit in enumerate(row):
                at = (height - 1 - ty - y) * stride + (tx + x) // 2
                pixels[at] |= bit << (4 if (tx + x) % 2 == 0 else 0)
    palette = b"\xff\x00\xff\x00" + bytes(4) + bytes(56)
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + 64 + len(pixels), 0,
                         0, 14 + 40 + 64)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 4, 0,
                       len(pixels), 0, 0, 16, 2)
    with open(path, "wb") as f:
        f.write(header + info + palette + pixels)


def blank():
    return [[WHITE] * 240 for _ in range(160)]


def draw(pixels, x, y, text, color=BLACK):
    for i, char in enumerate(text):
        for j, row in enumerate(FONT[char]):
            for k, bit in enumerate(row):
                if bit:
                    pixels[y + j][x + 8 * i + k] = color


def ppm(path, pixels):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (len(pixels[0]), len(pixels)))
        f.write(bytes(c for row in pixels for p in row for c in p))


def vram_dump(path, vram):
    """rom_run's --screen: VRAM halfword i as mode 3 pixel i."""
    pixels = []
    for y in range(160):
        row = []
        for x in range(240):
            half, = struct.unpack_from("<H", vram, 2 * (y * 240 + x))
            row.append(((half & 31) << 3, (half >> 5 & 31) << 3,
                        (half >> 10 & 31) << 3))
        pixels.append(row)
    ppm(path, pixels)


class ReadTestScreenTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.asm = self.path("glyphs.asm")
        self.bmp = self.path("font.bmp")
        glyphs_asm(self.asm)
        font_bmp(self.bmp)

    def tearDown(self):
        self.dir.cleanup()

    def path(self, name):
        return os.path.join(self.dir.name, name)

    def run_tool(self, *args):
        out = io.StringIO()
        with contextlib.redirect_stdout(out), \
                contextlib.redirect_stderr(io.StringIO()):
            status = screen.main(["read_test_screen.py", *args])
        return status, out.getvalue().strip()

    def test_jsmolka_failed_and_passed(self):
        frame = blank()
        draw(frame, 60, 76, "Failed test 235")
        ppm(self.path("f.ppm"), frame)
        self.assertEqual(self.run_tool("jsmolka", self.path("f.ppm"),
                                       self.asm), (0, "Failed test 235"))
        frame = blank()
        draw(frame, 56, 76, "All tests passed")
        ppm(self.path("p.ppm"), frame)
        self.assertEqual(self.run_tool("jsmolka", self.path("p.ppm"),
                                       self.asm), (0, "All tests passed"))

    def test_jsmolka_nothing_on_screen(self):
        ppm(self.path("b.ppm"), blank())
        status, out = self.run_tool("jsmolka", self.path("b.ppm"), self.asm)
        self.assertEqual(status, 1)
        self.assertIn("no result", out)

    def test_suite_counts(self):
        frame = blank()
        draw(frame, 0, 12, "Timer count-up tests")
        draw(frame, 168, 12, " 429/936 ")
        draw(frame, 8, 28, "0b, 0x0001", RED)
        ppm(self.path("s.ppm"), frame)
        self.assertEqual(self.run_tool("mgba-suite", self.path("s.ppm"),
                                       self.bmp),
                         (0, "Timer count-up tests: 429/936"))

    def test_other_sizes_are_refused(self):
        ppm(self.path("w.ppm"), [[WHITE] * 480 for _ in range(272)])
        with self.assertRaises(ValueError):
            self.run_tool("mgba-suite", self.path("w.ppm"), self.bmp)

    def test_cell_tolerates_a_few_pixels(self):
        table = screen.masks(FONT)
        frame = blank()
        draw(frame, 0, 0, "A")
        frame[0][0] = BLACK if frame[0][0] == WHITE else WHITE
        frame[3][5] = BLACK if frame[3][5] == WHITE else WHITE
        ink = screen.ink_map(frame)
        self.assertEqual(screen.read_cell(ink, 0, 0, table), "A")
        for y in range(8):
            for x in range(8):
                frame[y][x] = BLACK if (x + y) % 2 else WHITE
        self.assertEqual(screen.read_cell(screen.ink_map(frame), 0, 0,
                                          table), "?")

    def test_rom_run_vram_jsmolka(self):
        vram = bytearray(0x18000)
        for i, char in enumerate("Failed test 050"):
            for y, row in enumerate(FONT[char]):
                for x, bit in enumerate(row):
                    vram[(76 + y) * 240 + 60 + 8 * i + x] = bit
        vram_dump(self.path("v.ppm"), vram)
        self.assertEqual(self.run_tool("jsmolka", "--vram",
                                       self.path("v.ppm"), self.asm),
                         (0, "Failed test 050"))

    def test_rom_run_vram_suite_map(self):
        vram = bytearray(0x18000)
        row = "Timing tests".ljust(21) + "1350/2020"
        for c, char in enumerate(row):
            struct.pack_into("<H", vram, 0x800 + 2 * (32 + c),
                             ord(char) - 32 | 1 << 12)
        vram_dump(self.path("m.ppm"), vram)
        self.assertEqual(self.run_tool("mgba-suite", "--vram",
                                       self.path("m.ppm"), self.bmp),
                         (0, "Timing tests: 1350/2020"))

    def test_font_c_packs_left_pixel_low(self):
        self.assertEqual(self.run_tool("font-c", self.bmp, self.dir.name),
                         (0, ""))
        with open(self.path("font.c")) as f:
            words = [int(w, 16) for w in re.findall(r"0x([0-9A-F]{8})",
                                                    f.read())]
        self.assertEqual(len(words), 96 * 8)
        tile = struct.pack("<8I", *words[8:16])  # '!', tile 1
        row = FONT["!"][0]
        self.assertEqual(tile[0], row[0] | row[1] << 4)
        with open(self.path("font.h")) as f:
            self.assertIn("#define fontTilesLen 3072", f.read())


if __name__ == "__main__":
    unittest.main()
