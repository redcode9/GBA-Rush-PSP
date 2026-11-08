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
 * The raster test ROM. It runs the HBlank cases (cases_hblank.c) with
 * the result protocol of the test ROMs, leaves their results on screen
 * for three seconds, then shows its picture until it is switched off.
 *
 * The picture is the same in every frame and changes three things in
 * the middle of it:
 *   affine   BG2, in mode 2, is a checkerboard seen as a floor below the
 *            middle of the screen and as a ceiling above it, turned by 30
 *            degrees: DMA0 writes BG2PA-BG2PD, BG2X and BG2Y in each
 *            HBlank, so every line gets its own scale and origin;
 *   palette by DMA  DMA1 writes the backdrop colour (palette entry 0),
 *            seen through the empty squares, in each HBlank: blue at the
 *            top fading to red at the bottom;
 *   palette by the CPU  an HBlank IRQ handler turns the full squares
 *            (palette entry 1) from white to yellow in the HBlank of line
 *            SPLIT_LINE - 1.
 * Every write lands in an HBlank or in VBlank, never while a line is
 * being drawn, so the frame does not depend on how long the code takes.
 * A value written in the HBlank of line N shows from line N + 1; line
 * 0's values are written in VBlank, where both DMAs restart for the next
 * frame.
 */

#include "raster.h"

extern const TestCase cases_hblank[];

static const TestCase *const areas[] =
{
  cases_hblank,
  NULL
};

#define RESULT_FRAMES 180
#define SPLIT_LINE 112
#define HORIZON 80
/* cos and sin of 30 degrees in 8.8 fixed point */
#define COS30 222
#define SIN30 128
/* 8bpp tiles at 06000000h; a 256x256 map of one byte per tile at
   06004000h (screen block 8), wrapping around. */
#define TILES ((vu16 *)0x06000000)
#define MAP ((vu16 *)0x06004000)
#define BG2CNT_PICTURE (1 << 14 | 1 << 13 | 8 << 8)

/* An entry per line and one for line 160, which is never drawn: the
   HBlank of line N copies entry N + 1. DMA0 reads only internal memory,
   hence the work RAM. */
static u32 affine[VISIBLE_LINES + 1][4] EWRAM_BSS;
static u16 backdrop[VISIBLE_LINES + 1] EWRAM_BSS;

/* Called by the BIOS in ARM mode for every HBlank. */
IWRAM_CODE __attribute__((target("arm"))) static void split_irq(void)
{
  if(REG_VCOUNT == SPLIT_LINE - 1)
    PALETTE[1] = COLOR_YELLOW;
  REG_IF = IRQ_HBLANK;
}

static void build_tables(void)
{
  u32 y;

  for(y = 0; y <= VISIBLE_LINES; y++)
  {
    /* Lines from the horizon, plus 8 to keep the far squares apart. */
    int h = (y < HORIZON ? HORIZON - (int)y : (int)y - HORIZON + 1) + 8;
    /* Texture pixels per screen pixel, 8.8, and the distance ahead in
       texture pixels, 20.8. */
    int scale = 32 * 256 / h;
    int distance = scale * 96;
    int pa = scale * COS30 >> 8;
    int pc = scale * SIN30 >> 8;

    /* Screen x maps to (-sin, cos) * distance + (cos, sin) * scale *
       (x - 120); PB and PD stay 0, since every line sets X and Y. */
    affine[y][0] = (u16)pa;
    affine[y][1] = (u16)pc;
    affine[y][2] = (u32)(-(distance * SIN30 >> 8) - 120 * pa);
    affine[y][3] = (u32)((distance * COS30 >> 8) - 120 * pc);
    backdrop[y] = (u16)((31 - y * 31 / VISIBLE_LINES) << 10 | 8 << 5 |
     y * 31 / VISIBLE_LINES);
  }
}

/* Tile 0 is empty, tile 1 all colour 1; the map has squares of 2x2
   tiles. VRAM takes no byte writes, so two map bytes go at a time. */
static void load_picture(void)
{
  u32 i;

  for(i = 0; i < 64; i++)
    TILES[i] = i < 32 ? 0 : 0x0101;
  for(i = 0; i < 32 * 32 / 2; i++)
  {
    u32 tile = (i % 16 ^ i / 32) & 1;

    MAP[i] = (u16)(tile | tile << 8);
  }
  REG_BG2CNT = BG2CNT_PICTURE;
  REG_DISPCNT = DISPCNT_MODE2 | DISPCNT_BG2;
}

/* Line 0's values, then both DMAs from line 1's entries; in VBlank. */
static void start_frame(void)
{
  u32 i;

  REG_DMA0CNT = 0;
  REG_DMA1CNT = 0;
  for(i = 0; i < 4; i++)
    REG_BG2AFFINE[i] = affine[0][i];
  PALETTE[0] = backdrop[0];
  PALETTE[1] = COLOR_WHITE;
  REG_DMA0SAD = (u32)affine[1];
  REG_DMA0DAD = (u32)REG_BG2AFFINE;
  REG_DMA0CNT = DMA_ENABLE | DMA_HBLANK | DMA_REPEAT | DMA_32BIT |
   DMA_DEST_RELOAD | 4;
  REG_DMA1SAD = (u32)&backdrop[1];
  REG_DMA1DAD = (u32)PALETTE;
  REG_DMA1CNT = DMA_ENABLE | DMA_HBLANK | DMA_REPEAT | DMA_DEST_FIXED | 1;
}

int main(void)
{
  u32 i;

  result_run(areas);
  build_tables();
  for(i = 0; i < RESULT_FRAMES; i++)
    wait_vblank();

  load_picture();
  IRQ_VECTOR = split_irq;
  REG_DISPSTAT = DISPSTAT_HBLANK_IRQ;
  REG_IE = IRQ_HBLANK;
  REG_IME = 1;
  for(;;)
  {
    start_frame();
    wait_vblank();
  }
}
