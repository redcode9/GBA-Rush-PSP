/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * BIOS copy and decompression cases: CpuSet, CpuFastSet, BitUnPack,
 * LZ77, run length and Huffman, each called once from ARM and once from
 * Thumb code (cases_bios_asm.s). The inputs and expected outputs are in
 * bios_vectors.h, written by tools/make_bios_vectors.py from GBATEK's
 * formats. Before each call the destination and 32 bytes past it are
 * filled with a pattern; the call must leave exactly the expected bytes
 * and the pattern around them. WRAM functions write to a buffer in the
 * on-board work RAM, VRAM functions to 06014000h, the object VRAM of the
 * bitmap modes, past the mode 3 frame buffer the results are drawn on.
 * The detail has bit n set when vector n of the case failed.
 *
 * To confirm in mGBA and on hardware: every value here (GBATEK only).
 */

#include "result.h"
#include "bios_vectors.h"

#define GUARD 32
#define FILL 0xA55AC33Cu
#define VRAM_FREE ((u32 *)0x06014000)
#define COUNT(table) (sizeof(table) / sizeof((table)[0]))

/* r0, r1 and r2 of the BIOS function. */
typedef void BiosCall(const void *source, void *dest, u32 r2);

BiosCall swi_cpuset_arm, swi_cpuset_thumb;
BiosCall swi_cpufastset_arm, swi_cpufastset_thumb;
BiosCall swi_bitunpack_arm, swi_bitunpack_thumb;
BiosCall swi_lz77_wram_arm, swi_lz77_wram_thumb;
BiosCall swi_lz77_vram_arm, swi_lz77_vram_thumb;
BiosCall swi_huffman_arm, swi_huffman_thumb;
BiosCall swi_rl_wram_arm, swi_rl_wram_thumb;
BiosCall swi_rl_vram_arm, swi_rl_vram_thumb;

EWRAM_BSS static u32 wram_dest[(BIOS_VECTORS_MAX_OUTPUT + GUARD + 3) / 4];

static int run(const BiosVector *v, unsigned count, BiosCall *call,
 u32 *dest, u32 *detail)
{
  const u8 *out = (const u8 *)dest;
  unsigned i, k;

  for(i = 0; i < count; i++, v++)
  {
    const u8 *expect = v->expect;
    unsigned start = v->dest_offset;
    unsigned end = start + v->expect_length;

    for(k = 0; k < (end + GUARD + 3) / 4; k++)
      dest[k] = FILL;
    call(v->source, (u8 *)dest + start,
     v->info != NULL ? (u32)v->info : v->control);
    for(k = 0; k < end + GUARD; k++)
    {
      u8 want = k < start || k >= end ?
       (u8)(FILL >> (k % 4 * 8)) : expect[k - start];

      if(out[k] != want)
      {
        *detail |= 1u << i;
        break;
      }
    }
  }
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

#define BIOS_CASE(name, call, table, dest) \
  static int name(u32 *detail) \
  { \
    return run(table, COUNT(table), call, dest, detail); \
  }

BIOS_CASE(cpuset_arm, swi_cpuset_arm, bios_cpuset, wram_dest)
BIOS_CASE(cpuset_thumb, swi_cpuset_thumb, bios_cpuset, wram_dest)
BIOS_CASE(cpufastset_arm, swi_cpufastset_arm, bios_cpufastset, wram_dest)
BIOS_CASE(cpufastset_thumb, swi_cpufastset_thumb, bios_cpufastset,
 wram_dest)
BIOS_CASE(bitunpack_arm, swi_bitunpack_arm, bios_bitunpack, wram_dest)
BIOS_CASE(bitunpack_thumb, swi_bitunpack_thumb, bios_bitunpack, wram_dest)
BIOS_CASE(lz77_wram_arm, swi_lz77_wram_arm, bios_lz77_wram, wram_dest)
BIOS_CASE(lz77_wram_thumb, swi_lz77_wram_thumb, bios_lz77_wram, wram_dest)
BIOS_CASE(lz77_vram_arm, swi_lz77_vram_arm, bios_lz77_vram, VRAM_FREE)
BIOS_CASE(lz77_vram_thumb, swi_lz77_vram_thumb, bios_lz77_vram, VRAM_FREE)
BIOS_CASE(rl_wram_arm, swi_rl_wram_arm, bios_rl_wram, wram_dest)
BIOS_CASE(rl_wram_thumb, swi_rl_wram_thumb, bios_rl_wram, wram_dest)
BIOS_CASE(rl_vram_arm, swi_rl_vram_arm, bios_rl_vram, VRAM_FREE)
BIOS_CASE(rl_vram_thumb, swi_rl_vram_thumb, bios_rl_vram, VRAM_FREE)
BIOS_CASE(huff4_arm, swi_huffman_arm, bios_huff4, wram_dest)
BIOS_CASE(huff4_thumb, swi_huffman_thumb, bios_huff4, wram_dest)
BIOS_CASE(huff8_arm, swi_huffman_arm, bios_huff8, wram_dest)
BIOS_CASE(huff8_thumb, swi_huffman_thumb, bios_huff8, wram_dest)

const TestCase cases_bios[] =
{
  { "cpuset_arm", cpuset_arm },
  { "cpuset_thumb", cpuset_thumb },
  { "cpufastset_arm", cpufastset_arm },
  { "cpufastset_thumb", cpufastset_thumb },
  { "bitunpack_arm", bitunpack_arm },
  { "bitunpack_thumb", bitunpack_thumb },
  { "lz77_wram_arm", lz77_wram_arm },
  { "lz77_wram_thumb", lz77_wram_thumb },
  { "lz77_vram_arm", lz77_vram_arm },
  { "lz77_vram_thumb", lz77_vram_thumb },
  { "rl_wram_arm", rl_wram_arm },
  { "rl_wram_thumb", rl_wram_thumb },
  { "rl_vram_arm", rl_vram_arm },
  { "rl_vram_thumb", rl_vram_thumb },
  { "huff4_arm", huff4_arm },
  { "huff4_thumb", huff4_thumb },
  { "huff8_arm", huff8_arm },
  { "huff8_thumb", huff8_thumb },
  { NULL, NULL }
};
