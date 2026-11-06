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
 * The LCD phase changes of main.c. Every scanline is 960 cycles of
 * H-draw, then 272 of H-blank, and each change of phase updates DISPSTAT
 * (bits 0-2: the V-blank, H-blank and VCOUNT match flags; 3-5 their IRQ
 * enables; 8-15 the VCOUNT match line) and VCOUNT. With the New core the
 * line event of update_gba waits for the next change that does more than
 * that (gba_line_next_served); the ones before it are applied to the two
 * registers when a pass or a CPU read reaches them (gba_line_step).
 *
 * Kept free of PSP headers so tests/test_gba_line.c checks it on the
 * host.
 */

#ifndef GBA_LINE_H
#define GBA_LINE_H

#include <stdint.h>

/* Applies the next LCD phase change to DISPSTAT and VCOUNT: the start of
   the H-blank when DISPSTAT bit 1 is clear, else the start of the next
   line, which sets or clears the VCOUNT match flag. Returns the cycles
   to the change after it. Only for the changes gba_line_next_served
   skips, so never the start of line 160 or 228, which also change the
   V-blank flag or wrap VCOUNT. */
static inline uint32_t gba_line_step(uint16_t *dispstat, uint16_t *vcount)
{
  if ((*dispstat & 0x02) == 0)
  {
    *dispstat |= 0x02;
    return 272;
  }
  (*vcount)++;
  *dispstat &= ~0x02;
  if (*vcount == (*dispstat >> 8))
    *dispstat |= 0x04;
  else
    *dispstat &= ~0x04;
  return 960;
}

/* The cycle of the first LCD phase change from `at`, the cycle of the
   next one, that does more than update DISPSTAT and VCOUNT, the two
   registers as they are before `at`. It is the start of an H-blank with
   the H-blank IRQ on, or in the visible area when visible_hblank (a
   drawn frame or an H-blank DMA), or in V-blank when vblank_hblank (a
   picture to poll from the Media Engine); or the start of line 160, of
   line 228 (the end of the frame) or of the VCOUNT match line with its
   IRQ on. V-blank starts and ends at changes this stops at, so the
   V-blank flag holds all along the walk. */
static inline uint32_t gba_line_next_served(uint32_t at, uint16_t dispstat,
 uint32_t vcount, int visible_hblank, int vblank_hblank)
{
  /* Above any line when the VCOUNT IRQ is off */
  uint32_t irq_line = (dispstat & 0x20) != 0 ? (uint32_t)(dispstat >> 8) :
   0x100;
  int hblank_served = (dispstat & 0x10) != 0 ||
   ((dispstat & 0x01) == 0 ? visible_hblank : vblank_hblank);
  int to_hblank = (dispstat & 0x02) == 0;

  for (;;)
  {
    if (to_hblank)
    {
      if (hblank_served)
        return at;
      at += 272;
    }
    else
    {
      vcount++;
      if (vcount == 160 || vcount > 227 || vcount == irq_line)
        return at;
      at += 960;
    }
    to_hblank = !to_hblank;
  }
}

#endif /* GBA_LINE_H */
