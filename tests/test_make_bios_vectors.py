"""Tests of tools/make_bios_vectors.py: each encoder against a decoder
written from GBATEK's description of the BIOS decompression functions, the
BitUnPack model against a worked example, and the committed
tests/core-rom/source/bios_vectors.h against a fresh run."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import make_bios_vectors as gen  # noqa: E402


def header(stream, kind):
    word = int.from_bytes(stream[:4], "little")
    assert word & 0xF0 == kind, hex(word)
    return word >> 8, word & 0x0F


def lz77_decode(stream, vram=False, disps=None):
    size, _ = header(stream, 0x10)
    out = bytearray()
    pos = 4
    while len(out) < size:
        flags = stream[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & 0x80 >> bit:
                b0, b1 = stream[pos], stream[pos + 1]
                pos += 2
                disp = (b0 & 0x0F) << 8 | b1
                # 16-bit writes: the byte just before is not in VRAM yet.
                assert not (vram and disp == 0)
                if disps is not None:
                    disps.append(disp)
                for _ in range((b0 >> 4) + 3):
                    out.append(out[-disp - 1])
            else:
                out.append(stream[pos])
                pos += 1
    assert len(out) == size, "a block runs past the size"
    return bytes(out)


def rl_decode(stream):
    size, _ = header(stream, 0x30)
    out = bytearray()
    pos = 4
    while len(out) < size:
        flag = stream[pos]
        pos += 1
        if flag & 0x80:
            out += stream[pos:pos + 1] * ((flag & 0x7F) + 3)
            pos += 1
        else:
            out += stream[pos:pos + (flag & 0x7F) + 1]
            pos += (flag & 0x7F) + 1
    assert len(out) == size, "a block runs past the size"
    return bytes(out)


def huffman_decode(stream):
    size, bits = header(stream, 0x20)
    tree = 4
    bitstream = tree + (stream[tree] + 1) * 2
    assert bitstream % 4 == 0
    units = []
    address, node = tree + 1, stream[tree + 1]
    pos = bitstream
    while len(units) * bits < size * 8:
        word = int.from_bytes(stream[pos:pos + 4], "little")
        pos += 4
        for k in range(31, -1, -1):
            bit = word >> k & 1
            child = (address & ~1) + (node & 0x3F) * 2 + 2 + bit
            if node & (0x80 >> bit):
                units.append(stream[child])
                address, node = tree + 1, stream[tree + 1]
            else:
                address, node = child, stream[child]
            if len(units) * bits >= size * 8:
                break
    if bits == 8:
        return bytes(units)
    return bytes(units[k] | units[k + 1] << 4
                 for k in range(0, len(units), 2))


class MakeBiosVectorsTest(unittest.TestCase):
    def test_encoders_round_trip(self):
        for name, text in gen.make_texts():
            with self.subTest(text=name):
                self.assertEqual(lz77_decode(gen.lz77(text)), text)
                even = text[:len(text) & ~1]
                if even:
                    self.assertEqual(
                        lz77_decode(gen.lz77(even, min_disp=1), vram=True),
                        even)
                self.assertEqual(rl_decode(gen.rl(text)), text)
                aligned = text[:len(text) & ~3]
                if aligned:
                    self.assertEqual(huffman_decode(gen.huffman(aligned, 4)),
                                     aligned)
                    self.assertEqual(huffman_decode(gen.huffman(aligned, 8)),
                                     aligned)

    def test_lz77_reaches_the_longest_displacement(self):
        disps = []
        lz77_decode(gen.lz77(dict(gen.make_texts())["far"]), disps=disps)
        self.assertEqual(max(disps), 4095)

    def test_bitunpack_example(self):
        # 1 to 4 bits, offset 2: bits 1,0,0,0,1,1,0,1 of B1h become 3 where
        # set and stay 0 where clear, unless the zero flag adds 2 to them.
        self.assertEqual(gen.bitunpack(b"\xB1", 1, 4, 2, 0),
                         (0x30330003).to_bytes(4, "little"))
        self.assertEqual(gen.bitunpack(b"\xB1", 1, 4, 2, 1),
                         (0x32332223).to_bytes(4, "little"))

    def test_committed_header_is_current(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "bios_vectors.h")
            gen.main(["make_bios_vectors.py", path])
            with open(path, "rb") as f:
                fresh = f.read()
        with open(gen.OUTPUT, "rb") as f:
            self.assertEqual(f.read(), fresh,
                             "run tools/make_bios_vectors.py")
        self.assertLess(len(fresh), gen.MAX_HEADER)


if __name__ == "__main__":
    unittest.main()
