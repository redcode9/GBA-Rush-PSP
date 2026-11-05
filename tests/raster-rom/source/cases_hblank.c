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
 * HBlank cases: the timing the raster picture (main.c) relies on.
 *
 * GBATEK: a DMA started by HBlank runs in the HBlank of each visible
 * line, 0-159, and never in VBlank, lines 160-227; the HBlank IRQ comes
 * in every line, 0-227; in the HBlank of line N, VCOUNT reads N. A
 * repeating DMA reloads its count after each run, and its destination
 * too when the destination mode is increment/reload, but never its
 * source.
 *
 * Each case starts as VBlank begins and runs one whole frame, so it
 * sees the 68 VBlank lines first, then the 160 visible ones.
 *   hblank_dma_vcount  DMA0 copies VCOUNT to a buffer in each HBlank: the
 *                      buffer must hold 0-159 and nothing after. Detail:
 *                      the first wrong entry's index in bits 16-31, its
 *                      value (FFFFh if never written) in bits 0-15.
 *   hblank_dma_reload  DMA0 copies two words of a table to the same two
 *                      words in each HBlank, the way the picture writes
 *                      the affine registers: they must hold the table's
 *                      words 318 and 319, and the word after them must
 *                      stay untouched. Detail: the first of the two
 *                      words, plus 80000000h if the word after changed.
 *   hblank_irq_vcount  a handler logs VCOUNT at each HBlank IRQ: 228
 *                      IRQs, lines 160-227 then 0-159. Detail: the number
 *                      of IRQs if it is not 228, otherwise the first
 *                      wrong entry as in hblank_dma_vcount.
 * GBA Rush drops the HBlank IRQs outside its HBlank IRQ window option,
 * lines 1-160 by default, so there hblank_irq_vcount gets 160 IRQs
 * unless the window is turned off.
 *
 * mGBA 0.10.3 passes every case. To confirm on hardware.
 */

#include "raster.h"

#define UNTOUCHED 0xFFFFu
/* Room for a DMA run in every line of the frame. */
#define LOG_SIZE (FRAME_LINES + 4)

static u16 lines[LOG_SIZE] EWRAM_BSS;
static u32 table[2 * FRAME_LINES] EWRAM_BSS;
/* The two words, then room for every word a destination that is never
   reloaded would reach. */
static u32 pair[2 + 2 * FRAME_LINES] EWRAM_BSS;
static volatile u32 irq_lines;

/* What entry i of lines must hold after the DMA case: one per visible
   line. */
static u32 visible_line(u32 i)
{
  return i < VISIBLE_LINES ? i : UNTOUCHED;
}

/* And after the IRQ case: one per line of a frame that starts at VBlank,
   lines 160-227 then 0-159. */
static u32 logged_line(u32 i)
{
  if(i >= FRAME_LINES)
    return UNTOUCHED;
  return i < FRAME_LINES - VISIBLE_LINES ? VISIBLE_LINES + i :
   i - (FRAME_LINES - VISIBLE_LINES);
}

static void clear_lines(void)
{
  u32 i;

  for(i = 0; i < LOG_SIZE; i++)
    lines[i] = UNTOUCHED;
}

/* Fails on the first entry of lines that differs from expected(). */
static int check_lines(u32 (*expected)(u32), u32 *detail)
{
  u32 i;

  for(i = 0; i < LOG_SIZE; i++)
    if(lines[i] != expected(i))
    {
      *detail = i << 16 | lines[i];
      return RESULT_FAIL;
    }
  return RESULT_PASS;
}

static int hblank_dma_vcount(u32 *detail)
{
  clear_lines();
  wait_vblank();
  REG_DMA0SAD = (u32)&REG_VCOUNT;
  REG_DMA0DAD = (u32)lines;
  REG_DMA0CNT = DMA_ENABLE | DMA_HBLANK | DMA_REPEAT | DMA_SRC_FIXED | 1;
  wait_vblank();
  REG_DMA0CNT = 0;
  return check_lines(visible_line, detail);
}

static int hblank_dma_reload(u32 *detail)
{
  u32 i;

  for(i = 0; i < 2 * FRAME_LINES; i++)
    table[i] = i;
  for(i = 0; i < 2 + 2 * FRAME_LINES; i++)
    pair[i] = UNTOUCHED;
  wait_vblank();
  REG_DMA0SAD = (u32)table;
  REG_DMA0DAD = (u32)pair;
  REG_DMA0CNT = DMA_ENABLE | DMA_HBLANK | DMA_REPEAT | DMA_32BIT |
   DMA_DEST_RELOAD | 2;
  wait_vblank();
  REG_DMA0CNT = 0;
  *detail = pair[0] | (pair[2] != UNTOUCHED ? 0x80000000u : 0);
  return pair[0] == 2 * (VISIBLE_LINES - 1) &&
   pair[1] == 2 * VISIBLE_LINES - 1 && pair[2] == UNTOUCHED ?
   RESULT_PASS : RESULT_FAIL;
}

/* Called by the BIOS in ARM mode, from the work RAM so that it reads
   VCOUNT as early as it can. */
IWRAM_CODE __attribute__((target("arm"))) static void log_vcount(void)
{
  u32 line = REG_VCOUNT;

  if(irq_lines < LOG_SIZE)
    lines[irq_lines] = (u16)line;
  irq_lines++;
  REG_IF = IRQ_HBLANK;
}

static int hblank_irq_vcount(u32 *detail)
{
  clear_lines();
  irq_lines = 0;
  IRQ_VECTOR = log_vcount;
  wait_vblank();
  REG_DISPSTAT = DISPSTAT_HBLANK_IRQ;
  REG_IE = IRQ_HBLANK;
  REG_IME = 1;
  wait_vblank();
  REG_IME = 0;
  if(irq_lines != FRAME_LINES)
  {
    *detail = irq_lines;
    return RESULT_FAIL;
  }
  return check_lines(logged_line, detail);
}

const TestCase cases_hblank[] =
{
  { "hblank_dma_vcount", hblank_dma_vcount },
  { "hblank_dma_reload", hblank_dma_reload },
  { "hblank_irq_vcount", hblank_irq_vcount },
  { NULL, NULL }
};
