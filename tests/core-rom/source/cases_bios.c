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
 * div_<arm|thumb> and divarm_<arm|thumb> run Div (SWI 06h) and DivArm
 * (07h, numerator and denominator swapped) on 16 pairs, from ARM and from
 * Thumb code, with the condition flags set to 6h or 9h. Each must return
 * the quotient in r0, the remainder in r1 and the absolute quotient in
 * r3, keep r2, r12 and the CPSR, and return at all: the expected values
 * are the official BIOS's, recorded on it, including 0, 1 and -1 divided
 * by 0 and INT32_MIN / -1. The detail has bit n set when pair n failed.
 * div_cycles times the 16 Div calls from Thumb code in ROM with timer 0
 * at F/1 and passes whatever the count (the detail), for comparing BIOS
 * images and cores.
 *
 * intrwait_dma0 calls IntrWait (SWI 04h) with r0 = 1 and r1 = 0100h, the
 * DMA0 flag, right after a VBlank, with DMA0 set to start at the next one
 * and raise its IRQ. IntrWait must return once that IRQ has set the flag
 * (GBATEK); a BIOS that only checks flags 0-7 keeps waiting and the case
 * hangs. The detail is the IRQs taken meanwhile.
 *
 * mGBA 0.10.3 passes every case on the official BIOS; on its own BIOS
 * it fails the four rl_* cases. To confirm on hardware.
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

/* in = r0, r1 and the flags (bits 28-31); out = r0, r1, r2, r3, r12 and
   the CPSR after the SWI. */
typedef void DivCall(const u32 in[3], u32 out[6]);

DivCall div_div_arm, div_div_thumb, div_divarm_arm, div_divarm_thumb;

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

#define TM0CNT_L (*(vu16 *)0x04000100)
#define TM0CNT_H (*(vu16 *)0x04000102)
#define TM1CNT_L (*(vu16 *)0x04000104)
#define TM1CNT_H (*(vu16 *)0x04000106)
#define DMA0SAD (*(vu32 *)0x040000B0)
#define DMA0DAD (*(vu32 *)0x040000B4)
#define DMA0CNT (*(vu32 *)0x040000B8)
#define DMA_VBLANK 0x10000000u
#define DMA_IRQ 0x40000000u
#define IRQ_DMA0 0x0100

/* Results of the official BIOS's Div: numerator, denominator, r0, r1,
   r3. */
static const u32 div_pairs[16][5] =
{
  { 0xFFFFFB2E, 10, 0xFFFFFF85, 0xFFFFFFFC, 0x7B },
  { 100, 7, 14, 2, 14 },
  { 0xFFFFFF9C, 7, 0xFFFFFFF2, 0xFFFFFFFE, 14 },
  { 100, 0xFFFFFFF9, 0xFFFFFFF2, 2, 14 },
  { 0xFFFFFF9C, 0xFFFFFFF9, 14, 0xFFFFFFFE, 14 },
  { 0, 0, 1, 0, 1 },
  { 1, 0, 1, 1, 1 },
  { 0xFFFFFFFF, 0, 0xFFFFFFFF, 0xFFFFFFFF, 1 },
  { 0x80000000, 0xFFFFFFFF, 0x80000000, 0, 0x80000000 },
  { 0x80000000, 1, 0x80000000, 0, 0x80000000 },
  { 0x80000000, 2, 0xC0000000, 0, 0x40000000 },
  { 0x80000000, 3, 0xD5555556, 0xFFFFFFFE, 0x2AAAAAAA },
  { 0x80000000, 0x80000000, 1, 0, 1 },
  { 5, 0x80000000, 0, 5, 0 },
  { 0x7FFFFFFF, 1, 0x7FFFFFFF, 0, 0x7FFFFFFF },
  { 0x12345678, 1000, 0x0004A90B, 0x380, 0x0004A90B }
};

static int div_run(DivCall *call, int swapped, u32 *detail)
{
  unsigned i;

  for(i = 0; i < COUNT(div_pairs); i++)
  {
    const u32 *pair = div_pairs[i];
    u32 flags = i & 1 ? 0x90000000u : 0x60000000u;
    u32 in[3];
    u32 out[6];

    in[0] = pair[swapped];
    in[1] = pair[!swapped];
    in[2] = flags;
    call(in, out);
    if(out[0] != pair[2] || out[1] != pair[3] || out[2] != 0x22222222u ||
       out[3] != pair[4] || out[4] != 0xCCCCCCCCu ||
       (out[5] & 0xF00000FFu) != (flags | 0x1F))
      *detail |= 1u << i;
  }
  return *detail ? RESULT_FAIL : RESULT_PASS;
}

static int div_arm(u32 *detail)
{
  return div_run(div_div_arm, 0, detail);
}

static int div_thumb(u32 *detail)
{
  return div_run(div_div_thumb, 0, detail);
}

static int divarm_arm(u32 *detail)
{
  return div_run(div_divarm_arm, 1, detail);
}

static int divarm_thumb(u32 *detail)
{
  return div_run(div_divarm_thumb, 1, detail);
}

static int div_cycles(u32 *detail)
{
  unsigned i;

  TM0CNT_L = 0;
  TM1CNT_L = 0;
  TM1CNT_H = 0x0084;  /* counts timer 0's overflows */
  TM0CNT_H = 0x0080;
  for(i = 0; i < COUNT(div_pairs); i++)
    swi_div(div_pairs[i][0], div_pairs[i][1]);
  TM0CNT_H = 0;
  *detail = TM0CNT_L | (u32)TM1CNT_L << 16;
  TM1CNT_H = 0;
  return RESULT_PASS;
}

static inline void swi_intr_wait(u32 discard, u32 flags)
{
  register u32 r0 __asm__("r0") = discard;
  register u32 r1 __asm__("r1") = flags;

  __asm__ volatile("swi 0x04" : "+r"(r0), "+r"(r1) : : "r2", "r3", "r12",
   "memory");
}

static u32 dma_source = 0x13579BDFu;
static vu32 dma_dest;

static int intrwait_dma0(u32 *detail)
{
  u32 count = irq_count;

  dma_dest = 0;
  /* Just after a VBlank starts: the DMA comes a frame later, long after
     IntrWait has begun to wait. */
  wait_vblank();
  REG_IE = IRQ_DMA0;
  DMA0SAD = (u32)&dma_source;
  DMA0DAD = (u32)&dma_dest;
  DMA0CNT = 1 | DMA_32BIT | DMA_VBLANK | DMA_IRQ | DMA_ENABLE;
  swi_intr_wait(1, IRQ_DMA0);
  *detail = irq_count - count;
  return dma_dest == dma_source && irq_count != count ?
   RESULT_PASS : RESULT_FAIL;
}

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
  { "div_arm", div_arm },
  { "div_thumb", div_thumb },
  { "divarm_arm", divarm_arm },
  { "divarm_thumb", divarm_thumb },
  { "div_cycles", div_cycles },
  { "intrwait_dma0", intrwait_dma0 },
  { NULL, NULL }
};
