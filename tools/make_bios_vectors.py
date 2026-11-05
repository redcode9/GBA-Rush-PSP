#!/usr/bin/env python3
# GBA Rush PSP
#
# Copyright (C) 2026 Piero Carrieri
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.

"""Write the BIOS reference vectors of the core test ROM.

Usage: make_bios_vectors.py [OUTPUT]
       make_bios_vectors.py --record SAVE [SAVE ...]

Writes tests/core-rom/source/bios_vectors.h (or OUTPUT): the inputs of the
BIOS copy and decompression functions and the output each one must produce,
for cases_bios.c. The source texts come from a fixed seed, and the
compressed streams from our own encoders of the formats GBATEK describes
(BIOS Decompression Functions): LZ77 (type 10h), run length (type 30h) and
Huffman with 4 and 8 bit data (type 20h). The expected output of a
decompression is the text itself; those of CpuSet, CpuFastSet and BitUnPack
follow GBATEK's description of each function. Running the script again
gives the same file byte for byte, which tests/test_make_bios_vectors.py
checks.

Each output is chosen so that GBATEK alone fixes every byte the BIOS
writes: the VRAM functions write 16 bits at a time, so they get texts of
even length and LZ77 streams that never copy from the byte just before
(displacement 0), whose halfword is not written yet; Huffman writes 32 bits
at a time, so it gets texts of a multiple of 4 bytes; BitUnPack gets
sources that fill whole output words, and offsets that never overflow the
output width.

The arithmetic functions (Sqrt, ArcTan, ArcTan2, BgAffineSet and
ObjAffineSet) get one case each, which runs the function on every input
of its list. For each input the ROM takes the CRC-32 of what comes back:
r0-r3, r12, the CPSR and the destination with the guard bytes around it.
It leaves those CRCs in SRAM from MATH_LOG on, past the signature, and
the case's detail is the CRC-32 of that list. GBATEK gives no formula
precise enough to fix every bit of these results, so the expected details
are the official BIOS's, recorded in RECORDED: --record reads the saves
of runs on the official BIOS in two emulators (build/host/rom_run and
mGBA) and prints them when all agree. With saves that disagree, such as
one made on the official BIOS and one on the built-in BIOS, it prints
the inputs where a case differs instead.
"""

import heapq
import os
import struct
import sys
import zlib

OUTPUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                      "tests", "core-rom", "source", "bios_vectors.h")
SEED = 0x47425253

# The fill guard of cases_bios.c holds one bit per vector of a case.
MAX_VECTORS = 32
# The generated header must stay under 64 KiB.
MAX_HEADER = 64 * 1024

# The per-input CRCs of the arithmetic cases, in SRAM past the signature's
# 8 KiB (RESULT_SPACE in tests/gba-rom/backup.h), at most math_log's 128
# per case (cases_bios.c).
MATH_LOG = 0x2000
MAX_MATH_CRCS = 128
# Registers left out of each case's CRCs, bit n for rn, because GBATEK
# gives no result in them and the two BIOSes leave them differently: the
# official BIOS leaves working values in r1 and r3 after Sqrt, ArcTan and
# ArcTan2 and in r3 after BgAffineSet, where the built-in one keeps r3,
# and the built-in BIOS's pointers end one entry further on after
# BgAffineSet (r0, r1) and ObjAffineSet (r0).
LEFT_OUT = {
    "sqrt": 0b1010,
    "arctan": 0b1010,
    "arctan2": 0b1010,
    "bg_affine_set": 0b1011,
    "obj_affine_set": 0b0001,
}
# The detail of each arithmetic case on the official BIOS: --record on
# the saves of build/host/rom_run and of mGBA 0.10.3, both with the
# official BIOS, which gave the same values.
RECORDED = {
    "sqrt": 0xA09527A0,
    "arctan": 0xE7591202,
    "arctan2": 0xC7AB61B8,
    "bg_affine_set": 0xB99CC27F,
    "obj_affine_set": 0x3A3AE3A8,
}


class Random:
    """xorshift32, so the texts do not depend on Python's own generator."""

    def __init__(self, seed):
        self.state = seed

    def byte(self):
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x >> 24

    def bytes(self, count):
        return bytes(self.byte() for _ in range(count))


def make_texts():
    """The source texts, as (name, bytes)."""
    rng = Random(SEED)
    random_text = rng.bytes(254)
    words = [b"rush", b"gba", b"psp", b"core", b"link", b"bios", b"lz",
             b"huffman", b"run", b"length"]
    repetitive = bytearray()
    while len(repetitive) < 510:
        repetitive += words[rng.byte() % len(words)] + b" "
    # Runs around the limits of the formats: RL stores 3 to 130 bytes per
    # run and 1 to 128 per literal block, LZ77 copies 3 to 18.
    runs = bytearray()
    for length in (1, 2, 3, 4, 5, 17, 18, 19, 127, 128, 129, 130, 131, 140,
                   2, 1, 133):
        runs += bytes([rng.byte() | 1]) * length
    # The same 18 bytes 4096 bytes apart, LZ77's longest displacement.
    far_bytes = bytes(b | 1 for b in rng.bytes(18))
    far = far_bytes + bytes(4096 - 18) + far_bytes + rng.bytes(8)
    return [
        ("one_byte", b"\x5a"),
        ("short", rng.bytes(7)),
        ("one_value", b"\xc3" * 66),
        ("random", random_text),
        ("repetitive", bytes(repetitive[:510])),
        ("runs", bytes(runs)),
        ("far", far),
    ]


def pad4(data):
    return data + bytes(-len(data) % 4)


def lz77(data, min_disp=0):
    """LZ77 type 10h: a flag byte, MSB first, before each 8 blocks; a 0
    flag is one literal byte, a 1 flag copies (byte0 >> 4) + 3 bytes from
    disp + 1 bytes back, disp = (byte0 & 0Fh) << 8 | byte1. Greedy longest
    match with displacements from min_disp to 4095."""
    out = bytearray((0x10 | len(data) << 8).to_bytes(4, "little"))
    seen = {}
    blocks = []
    i = 0
    while i < len(data):
        best_len, best_disp = 0, 0
        for j in reversed(seen.get(data[i:i + 3], ())):
            disp = i - j - 1
            if disp > 4095:
                break
            if disp < min_disp:
                continue
            n = 0
            while n < 18 and i + n < len(data) and data[j + n] == data[i + n]:
                n += 1
            if n > best_len:
                best_len, best_disp = n, disp
        if best_len >= 3:
            blocks.append(bytes([(best_len - 3) << 4 | best_disp >> 8,
                                 best_disp & 0xFF]))
            step = best_len
        else:
            blocks.append(data[i:i + 1])
            step = 1
        for k in range(i, i + step):
            seen.setdefault(data[k:k + 3], []).append(k)
        i += step
    for first in range(0, len(blocks), 8):
        group = blocks[first:first + 8]
        flags = 0
        for n, block in enumerate(group):
            if len(block) == 2:
                flags |= 0x80 >> n
        out.append(flags)
        for block in group:
            out += block
    return pad4(bytes(out))


def rl(data):
    """Run length type 30h: a flag byte with bit 7 set stores one byte
    repeated (bits 0-6) + 3 times, with bit 7 clear (bits 0-6) + 1 bytes
    as they are."""
    out = bytearray((0x30 | len(data) << 8).to_bytes(4, "little"))
    literal = bytearray()

    def flush():
        for start in range(0, len(literal), 128):
            chunk = literal[start:start + 128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
        literal.clear()

    i = 0
    while i < len(data):
        run = 1
        while run < 130 and i + run < len(data) and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            flush()
            out += bytes([0x80 | run - 3, data[i]])
            i += run
        else:
            literal.append(data[i])
            i += 1
    flush()
    return pad4(bytes(out))


def huffman(data, bits):
    """Huffman type 20h with 4 or 8 bit data (4 bit: the low nibble of
    each byte first). After the header, the tree size byte and the tree,
    whose nodes keep their two children side by side at
    (address AND NOT 1) + offset * 2 + 2: bits 0-5 offset, bit 7 child 0
    is data, bit 6 child 1 is data. Then the bit stream in 32 bit units,
    most significant bit first, 0 for child 0."""
    if bits == 8:
        symbols = list(data)
    else:
        symbols = [s for b in data for s in (b & 15, b >> 4)]
    counts = {}
    for s in symbols:
        counts[s] = counts.get(s, 0) + 1
    if len(counts) == 1:
        # A tree needs two leaves: add one the text never uses.
        counts[symbols[0] ^ 1] = 0
    # Leaves are ints, inner nodes (child0, child1) tuples; the sequence
    # number breaks ties so the tree never depends on comparing nodes.
    heap = [(count, n, s) for n, (s, count) in enumerate(sorted(
        counts.items()))]
    heapq.heapify(heap)
    sequence = len(heap)
    while len(heap) > 1:
        c0, _, n0 = heapq.heappop(heap)
        c1, _, n1 = heapq.heappop(heap)
        heapq.heappush(heap, (c0 + c1, sequence, (n0, n1)))
        sequence += 1
    root = heap[0][2]

    codes = {}

    def walk(node, code):
        if isinstance(node, tuple):
            walk(node[0], code + "0")
            walk(node[1], code + "1")
        else:
            codes[node] = code

    walk(root, "")

    # Breadth first: the root is byte 1 of the table (byte 0 is its size)
    # and pair p sits at bytes 2p and 2p + 1, so a node of pair p whose
    # children are pair q stores offset q - p - 1.
    table = [0, 0]
    queue = [(root, 0, 1)]
    pairs = 0
    while queue:
        node, pair, index = queue.pop(0)
        pairs += 1
        offset = pairs - pair - 1
        if offset > 63:
            raise ValueError("Huffman tree too wide for 6-bit offsets")
        value = offset
        table += [0, 0]
        for side, child in enumerate(node):
            slot = 2 * pairs + side
            if isinstance(child, tuple):
                queue.append((child, pairs, slot))
            else:
                value |= 0x80 >> side
                table[slot] = child
        table[index] = value
    if pairs % 2 == 0:
        # The bit stream starts on a word: pad the tree with an unused pair.
        table += [0, 0]
        pairs += 1
    table[0] = pairs

    stream = "".join(codes[s] for s in symbols)
    stream += "0" * (-len(stream) % 32)
    words = b"".join(int(stream[k:k + 32], 2).to_bytes(4, "little")
                     for k in range(0, len(stream), 32))
    header = (0x20 | bits | len(data) << 8).to_bytes(4, "little")
    return header + bytes(table) + words


def bitunpack(source, src_bits, dst_bits, offset, zero_flag):
    """BitUnPack: each source unit, from the low bits of every byte up,
    becomes one destination unit, plus offset when it is not zero or when
    the zero flag is set; the units fill 32 bit words from the low bits."""
    words = []
    value = filled = 0
    for byte in source:
        for shift in range(0, 8, src_bits):
            unit = byte >> shift & (1 << src_bits) - 1
            if unit or zero_flag:
                unit += offset
            assert unit < 1 << dst_bits, "offset overflows the unit"
            value |= unit << filled
            filled += dst_bits
            if filled == 32:
                words.append(value)
                value = filled = 0
    assert filled == 0, "source does not fill whole words"
    return b"".join(w.to_bytes(4, "little") for w in words)


class Header:
    """The generated C text: u32 arrays (the BIOS wants word-aligned
    sources) and one table of vectors per function."""

    def __init__(self):
        self.arrays = []
        self.by_content = {}
        self.data = {}
        self.infos = []
        self.tables = []
        self.maths = []

    def array(self, name, data):
        """The C name of an array holding data, shared between equal ones."""
        data = pad4(data)
        if data in self.by_content:
            return self.by_content[data]
        self.by_content[data] = name
        self.data[name] = data
        self.arrays.append(name)
        return name

    def ref(self, name, offset=0):
        return f"(const u8 *){name} + {offset}" if offset else name

    def table(self, name, comment, vectors):
        assert len(vectors) <= MAX_VECTORS
        self.tables.append((name, comment, vectors))

    def render(self):
        lines = [
            "/*",
            " * Generated by tools/make_bios_vectors.py: do not edit, run it",
            " * again. Inputs and expected outputs of the BIOS copy and",
            " * decompression functions for cases_bios.c, from GBATEK's",
            " * formats and our own encoders.",
            " */",
            "",
            "#ifndef BIOS_VECTORS_H",
            "#define BIOS_VECTORS_H",
            "",
            "/* GBATEK's UnPackInfo for BitUnPack. */",
            "typedef struct",
            "{",
            "  u16 length;",
            "  u8 source_width;",
            "  u8 dest_width;",
            "  u32 offset;",
            "} BitUnPackInfo;",
            "",
            "/* One call: r0 is source, r1 the destination plus dest_offset,",
            "   r2 info when there is one and control otherwise. The",
            "   function must write expect_length bytes equal to expect and",
            "   nothing around them. */",
            "typedef struct",
            "{",
            "  const void *source;",
            "  u32 control;",
            "  const BitUnPackInfo *info;",
            "  const void *expect;",
            "  u16 expect_length;",
            "  u16 dest_offset;",
            "} BiosVector;",
            "",
            "/* An arithmetic case: count inputs of words u32 each from in,",
            "   the registers its CRCs leave out (bit n for rn), the SRAM",
            "   offset of the CRCs and the case's detail on the official",
            "   BIOS. */",
            "typedef struct",
            "{",
            "  const u32 *in;",
            "  u16 count;",
            "  u8 words;",
            "  u8 left_out;",
            "  u32 log;",
            "  u32 expect;",
            "} BiosMath;",
            "",
        ]
        for name in self.arrays:
            data = self.data[name]
            lines.append(f"static const u32 {name}[] =")
            lines.append("{")
            words = [int.from_bytes(data[k:k + 4], "little")
                     for k in range(0, len(data), 4)]
            for k in range(0, len(words), 6):
                row = words[k:k + 6]
                lines.append("  " + ", ".join(f"0x{w:08X}" for w in row) +
                             ",")
            lines.append("};")
            lines.append("")
        lines += self.infos + [""]
        for name, comment, vectors in self.tables:
            lines.append(f"/* {comment} */")
            lines.append(f"static const BiosVector {name}[] =")
            lines.append("{")
            for v in vectors:
                control = v.get("control", 0)
                lines.append(f"  {{ {v['source']}, 0x{control:08X},"
                             f" {v.get('info', 'NULL')},")
                lines.append(f"    {v['expect']}, {v['length']},"
                             f" {v.get('dest_offset', 0)} }},")
            lines.append("};")
            lines.append("")
        for name, comment, array, count, words, log in self.maths:
            lines.append(f"/* {comment} */")
            lines.append(f"static const BiosMath bios_{name} =")
            lines.append(f"  {{ {array}, {count}, {words},"
                         f" 0x{LEFT_OUT[name]:X}, 0x{log:04X},"
                         f" 0x{RECORDED[name]:08X} }};")
            lines.append("")
        biggest = max(v["length"] + v.get("dest_offset", 0)
                      for _, _, vectors in self.tables for v in vectors)
        lines.append("/* The most bytes a vector writes. */")
        lines.append(f"#define BIOS_VECTORS_MAX_OUTPUT {biggest}")
        lines.append("")
        lines.append("#endif")
        return "\n".join(lines) + "\n"


def decompression_tables(header, texts):
    """LZ77, RL and Huffman, each over every text that fits its output
    unit (the text cut to a multiple of it)."""
    formats = [
        ("bios_lz77_wram", "LZ77UnCompWram (SWI 11h).", 1, lz77, "lz77"),
        ("bios_lz77_vram", "LZ77UnCompVram (SWI 12h): no displacement 0.",
         2, lambda d: lz77(d, min_disp=1), "lz77v"),
        ("bios_rl_wram", "RLUnCompWram (SWI 14h).", 1, rl, "rl"),
        ("bios_rl_vram", "RLUnCompVram (SWI 15h).", 2, rl, "rl"),
        ("bios_huff4", "HuffUnComp (SWI 13h), 4 bit data.", 4,
         lambda d: huffman(d, 4), "huff4"),
        ("bios_huff8", "HuffUnComp (SWI 13h), 8 bit data.", 4,
         lambda d: huffman(d, 8), "huff8"),
    ]
    for table, comment, unit, encode, prefix in formats:
        vectors = []
        for name, text in texts:
            data = text[:len(text) - len(text) % unit]
            if not data:
                continue
            stream = header.array(f"{prefix}_{name}_{len(data)}",
                                  encode(data))
            vectors.append({"source": stream, "expect": f"text_{name}",
                            "length": len(data)})
        header.table(table, comment, vectors)


def copy_tables(header, source_name):
    """CpuSet and CpuFastSet: r2 is the count in bits 0-20, fill in bit 24
    and, for CpuSet, 32 bit units in bit 26. CpuFastSet moves 8 words at
    a time, its count rounded up to a multiple of 8."""
    source = header.data[source_name]
    cpuset = []
    for unit, fill, count, offset in (
            (2, 0, 0, 0), (2, 0, 1, 0), (2, 0, 7, 0), (2, 0, 13, 2),
            (4, 0, 0, 0), (4, 0, 1, 0), (4, 0, 5, 0), (4, 0, 11, 0),
            (2, 1, 0, 0), (2, 1, 1, 0), (2, 1, 9, 2),
            (4, 1, 0, 0), (4, 1, 1, 0), (4, 1, 3, 0), (4, 1, 10, 0)):
        control = count | fill << 24 | (unit == 4) << 26
        first = source[offset:offset + unit]
        if fill:
            data = first * count
            expect = (header.array(f"fill{unit * 8}_{count}", data)
                      if count else source_name)
        else:
            data = source[offset:offset + unit * count]
            expect = header.ref(source_name, offset)
        cpuset.append({"source": header.ref(source_name, offset),
                       "control": control, "expect": expect,
                       "length": len(data), "dest_offset": offset})
    header.table("bios_cpuset", "CpuSet (SWI 0Bh): copy and fill, 16 bit "
                 "(two halfword aligned), 32 bit.", cpuset)

    fastset = []
    for fill, count in ((0, 0), (0, 1), (0, 8), (0, 13), (0, 32),
                        (1, 0), (1, 1), (1, 9), (1, 16)):
        words = -(-count // 8) * 8
        if fill:
            data = source[:4] * words
            expect = (header.array(f"fastfill_{words}", data)
                      if words else source_name)
        else:
            data = source[:4 * words]
            expect = source_name
        fastset.append({"source": source_name, "control": count | fill << 24,
                        "expect": expect, "length": len(data)})
    header.table("bios_cpufastset", "CpuFastSet (SWI 0Ch): copy and fill, "
                 "counts rounded up to 8 words.", fastset)


def bitunpack_table(header, source_name):
    source = header.data[source_name]
    vectors = []
    for n, (length, src_bits, dst_bits, offset, zero_flag) in enumerate((
            (9, 1, 4, 0, 0), (9, 1, 4, 3, 1),
            (7, 2, 8, 0x10, 0), (7, 2, 8, 0x10, 1),
            (10, 4, 8, 0x80, 0), (10, 4, 8, 1, 1))):
        # Odd source offsets: BitUnPack reads bytes, aligned or not.
        start = 2 * n + 1
        data = bitunpack(source[start:start + length], src_bits, dst_bits,
                         offset, zero_flag)
        info = f"unpack_{n}"
        header.infos.append(
            f"static const BitUnPackInfo {info} = {{ {length}, {src_bits}, "
            f"{dst_bits}, 0x{offset | zero_flag << 31:08X} }};")
        vectors.append({"source": header.ref(source_name, start),
                        "info": f"&{info}",
                        "expect": header.array(f"unpacked_{n}", data),
                        "length": len(data)})
    header.table("bios_bitunpack", "BitUnPack (SWI 10h): 1 to 4, 2 to 8, "
                 "4 to 8 bits, zero flag off and on.", vectors)


def s16(value):
    return value & 0xFFFF


def math_inputs():
    """(case, comment, words per input, inputs as tuples of u32 words),
    in the order of the cases and of their CRCs in SRAM."""
    sqrt = [0, 1, 2, 3, 4, 5, 8, 9, 15, 16, 17, 99, 100, 101, 0xFFFF,
            0x10000, 0x10001, 0x3FFF0000, 0x3FFF0001, 0x3FFF0002,
            0x40000000, 0x7FFFFFFF, 0x80000000, 0xFFFE0000, 0xFFFE0001,
            0xFFFFFFFF]
    # Tangents and coordinates are 1.1.14 fixed point, sign extended.
    arctan = range(-0x4000, 0x4001, 0x200)
    arctan2 = [(0x4000, 0), (-0x4000, 0), (0, 0x4000), (0, -0x4000),
               (0x3000, 0x1000), (0x1000, 0x3000), (-0x1000, 0x3000),
               (-0x3000, 0x1000), (-0x3000, -0x1000), (-0x1000, -0x3000),
               (0x1000, -0x3000), (0x3000, -0x1000), (0x2000, 0x2000),
               (-0x2000, 0x2000), (-0x2000, -0x2000), (0x2000, -0x2000),
               (0, 0)]
    # Angles use their top byte; a few have a low byte the BIOS must
    # ignore. Scales are 8.8 fixed point, mirrored and at the limits too.
    angles = (0x0000, 0x0080, 0x2000, 0x4000, 0x6789, 0x8000, 0xA0FF,
              0xC000, 0xF000)
    scales = ((0x100, 0x100), (0x80, 0x200), (-0x100, 0x155),
              (0x7FFF, -0x8000))
    rng = Random(SEED + 1)
    bg, obj = [], []
    for angle in angles:
        for sx, sy in scales:
            # Centre in 20.8 fixed point, display centre in pixels; the
            # last halfword of each source is padding.
            cx = ((rng.byte() << 8 | rng.byte()) << 4) - 0x80000
            cy = ((rng.byte() << 8 | rng.byte()) << 4) - 0x80000
            dx, dy = rng.byte() - 128, rng.byte() - 128
            bg.append((cx & 0xFFFFFFFF, cy & 0xFFFFFFFF,
                       s16(dx) | s16(dy) << 16, s16(sx) | s16(sy) << 16,
                       angle | 0xA55A << 16))
            obj.append((s16(sx) | s16(sy) << 16, angle | 0xA55A << 16))
    return [
        ("sqrt", "Sqrt (SWI 08h): r0.", 1, [(v,) for v in sqrt]),
        ("arctan", "ArcTan (SWI 09h): r0 from -4000h to 4000h.", 1,
         [(v & 0xFFFFFFFF,) for v in arctan]),
        ("arctan2", "ArcTan2 (SWI 0Ah): r0 = x, r1 = y.", 2,
         [(x & 0xFFFFFFFF, y & 0xFFFFFFFF) for x, y in arctan2]),
        ("bg_affine_set", "BgAffineSet (SWI 0Eh): one 20-byte source "
         "each, then all in one call.", 5, bg),
        ("obj_affine_set", "ObjAffineSet (SWI 0Fh): one 8-byte source "
         "each, then all in one call.", 2, obj),
    ]


def math_layout():
    """(case, inputs, SRAM offset, CRCs) per arithmetic case: the affine
    cases end with one more call that takes the whole list."""
    layout = []
    log = MATH_LOG
    for name, _, _, inputs in math_inputs():
        crcs = len(inputs) + name.endswith("affine_set")
        assert crcs <= MAX_MATH_CRCS
        layout.append((name, inputs, log, crcs))
        log += 4 * crcs
    return layout


def math_tables(header):
    logs = {name: log for name, _, log, _ in math_layout()}
    for name, comment, words, inputs in math_inputs():
        array = header.array(f"{name}_in", b"".join(
            w.to_bytes(4, "little") for entry in inputs for w in entry))
        header.maths.append((name, comment, array, len(inputs), words,
                             logs[name]))


def record(paths):
    """Lines to print and the exit status for --record: the RECORDED
    entry of each case whose CRCs are the same in every save, and each
    input where the others differ."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import decode_test_rom

    runs = []
    for path in paths:
        with open(path, "rb") as f:
            data = f.read()
        details = {name: detail for name, _, detail in
                   decode_test_rom.parse(data)["cases"]}
        run = {}
        for name, _, log, crcs in math_layout():
            raw = data[log:log + 4 * crcs]
            if details.get(name) != zlib.crc32(raw):
                raise ValueError(f"{path}: {name} left no CRCs: it did not "
                                 "run, or did not finish")
            run[name] = struct.unpack(f"<{crcs}I", raw)
        runs.append(run)
    lines = []
    status = 0
    for name, inputs, _, crcs in math_layout():
        logs = [run[name] for run in runs]
        differ = [i for i in range(crcs)
                  if any(log[i] != logs[0][i] for log in logs)]
        if not differ:
            crc = zlib.crc32(struct.pack(f"<{crcs}I", *logs[0]))
            lines.append(f'    "{name}": 0x{crc:08X},')
        status |= bool(differ)
        for i in differ:
            what = (" ".join(f"{w:08X}" for w in inputs[i])
                    if i < len(inputs) else "all inputs in one call")
            lines.append(f"{name}: input {i} ({what}) differs: " +
                         " ".join(f"{log[i]:08X}" for log in logs))
    return lines, status


def generate():
    header = Header()
    texts = make_texts()
    for name, text in texts:
        header.array(f"text_{name}", text)
    decompression_tables(header, texts)
    copy_tables(header, "text_random")
    bitunpack_table(header, "text_random")
    math_tables(header)
    text = header.render()
    assert len(text.encode()) < MAX_HEADER, "bios_vectors.h over 64 KiB"
    return text


def main(argv):
    if len(argv) > 2 and argv[1] == "--record":
        try:
            lines, status = record(argv[2:])
        except ValueError as error:
            print(error, file=sys.stderr)
            return 2
        print("\n".join(lines))
        return status
    path = argv[1] if len(argv) > 1 else OUTPUT
    with open(path, "w", newline="\n") as f:
        f.write(generate())
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
