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
 * The GBA's picture processor, drawn one scanline at a time.
 *
 * The renderer keeps its own copies of palette RAM, OAM, VRAM and the video
 * registers. The host copies every write the game makes to them into the
 * renderer, calls gba_ppu_draw_line at the start of the H-Blank of each
 * visible line and gba_ppu_start_frame when V-Blank begins. Pixels come out
 * as BGR555 with bit 15 clear, which is also the PSP's 5551 format.
 *
 * Everything lives in one GbaPpu, so two renderers can run side by side.
 * The code allocates nothing, uses no floating point and calls nothing but
 * memcpy and memset, so it can run away from the main processor. It assumes
 * a little-endian processor, as both the PSP and the GBA are. A GbaPpu
 * filled with zeros is ready to use.
 *
 * The behaviour follows GBATEK's description of the GBA's LCD video
 * controller (problemkaputt.de/gbatek.htm).
 */

#ifndef GBA_PPU_H
#define GBA_PPU_H

#include <stdint.h>

#define GBA_PPU_WIDTH 240
#define GBA_PPU_HEIGHT 160
/* Video registers, from DISPCNT at 0x04000000 to BLDY at 0x04000054. */
#define GBA_PPU_IO_BYTES 0x58

typedef struct
{
  /* The GBA's video memory and registers, as the game last wrote them. */
  uint16_t palette[0x400 / 2];
  uint16_t oam[0x400 / 2];
  uint16_t vram[0x18000 / 2];
  uint16_t io[GBA_PPU_IO_BYTES / 2];

  /* BG2 and BG3's internal reference points in 1/256 pixel: loaded from
     BGxX and BGxY when V-Blank starts and whenever the game writes those
     registers, and moved by PB and PD after each line that draws them. */
  int32_t affine_x[2];
  int32_t affine_y[2];

  /* Lines each background still stays hidden after being switched on,
     and whether a line of this frame was drawn yet. */
  uint8_t enable_delay[4];
  uint8_t in_frame;

  /* Work space for the line being drawn. */
  uint32_t top[GBA_PPU_WIDTH];      /* front pixel at each column */
  uint32_t below[GBA_PPU_WIDTH];    /* the pixel right behind it */
  uint32_t obj[GBA_PPU_WIDTH];      /* front sprite pixel */
  uint8_t window[GBA_PPU_WIDTH];    /* layers and effects shown there */
  uint16_t layer[GBA_PPU_WIDTH + 16];  /* one background, 0 = transparent */
} GbaPpu;

/* Video register write; offset counts bytes from 0x04000000. */
void gba_ppu_write_io(GbaPpu *ppu, uint32_t offset, uint16_t value);

/* Copy size bytes the game wrote at offset into palette RAM (0-0x3FF), OAM
   (0-0x3FF) or VRAM (0-0x17FFF, with the GBA's mirror of the last 32 KB
   already folded back by the host). */
void gba_ppu_write_palette(GbaPpu *ppu, uint32_t offset, const void *data,
 uint32_t size);
void gba_ppu_write_oam(GbaPpu *ppu, uint32_t offset, const void *data,
 uint32_t size);
void gba_ppu_write_vram(GbaPpu *ppu, uint32_t offset, const void *data,
 uint32_t size);

/* V-Blank begins: reload the affine reference points. */
void gba_ppu_start_frame(GbaPpu *ppu);

/* Draw visible line y (0-159) into row, 240 pixels. */
void gba_ppu_draw_line(GbaPpu *ppu, int y, uint16_t *row);

#endif
