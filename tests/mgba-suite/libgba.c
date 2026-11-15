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
 * What mGBA's test suite needs besides its own sources (see the
 * Makefile): the C half of the libgba stand-in (libgba.h), with the
 * interrupt table and the keys, what newlib needs, and a stand-in for the
 * Video section, which is not built. The keys are a script, not the pad:
 * the suite's menu reads them once a frame, so pressing Down SUITE times
 * and then A opens section SUITE of its list, runs it and leaves its
 * results on screen, with no one at the controls.
 */

#include <stddef.h>
#include <sys/types.h>

#include "libgba.h"
#include "suite.h"

static size_t video_list(const char **names, bool *results, size_t size,
 size_t offset)
{
  (void)names;
  (void)results;
  (void)size;
  (void)offset;
  return 0;
}

static void video_show(size_t index)
{
  (void)index;
}

static const unsigned video_none;

const struct TestSuite videoTestSuite =
{
  "Video tests (not built)", NULL, video_list, video_show, 0, &video_none,
  &video_none
};

#ifndef SUITE
#error "build with -DSUITE=<index of the section in the suite's list>"
#endif

#define IRQ_SLOTS 14

/* {handler, mask} pairs, ended by a zero mask; read by irq_dispatch. */
struct IrqSlot
{
  IntFn handler;
  u32 mask;
};

struct IrqSlot irq_table[IRQ_SLOTS + 1];
void irq_dispatch(void);

void irqInit(void)
{
  size_t i;

  for(i = 0; i <= IRQ_SLOTS; i++)
  {
    irq_table[i].handler = NULL;
    irq_table[i].mask = 0;
  }
  *(void (*volatile *)(void))0x03007FFC = irq_dispatch;
}

void irqSet(int mask, IntFn handler)
{
  size_t i;

  for(i = 0; i < IRQ_SLOTS; i++)
    if(irq_table[i].mask == (u32)mask || irq_table[i].mask == 0)
      break;
  if(i == IRQ_SLOTS)
    return;
  irq_table[i].handler = handler;
  irq_table[i].mask = (u32)mask;
}

/* Like libgba: the display sources also need their DISPSTAT bit, and
   IME ends up set. */
void irqEnable(int mask)
{
  REG_IME = 0;
  if(mask & IRQ_VBLANK)
    REG_DISPSTAT |= LCDC_VBL;
  if(mask & IRQ_HBLANK)
    REG_DISPSTAT |= LCDC_HBL;
  if(mask & IRQ_VCOUNT)
    REG_DISPSTAT |= LCDC_VCNT;
  REG_IE |= mask;
  REG_IME = 1;
}

void irqDisable(int mask)
{
  REG_IME = 0;
  if(mask & IRQ_VBLANK)
    REG_DISPSTAT &= ~LCDC_VBL;
  if(mask & IRQ_HBLANK)
    REG_DISPSTAT &= ~LCDC_HBL;
  if(mask & IRQ_VCOUNT)
    REG_DISPSTAT &= ~LCDC_VCNT;
  REG_IE &= ~mask;
  REG_IME = 1;
}

static unsigned key_reads;

void scanKeys(void)
{
}

u16 keysDownRepeat(void)
{
  unsigned read = key_reads++;

  if(read > SUITE)
    return 0;
  return read == SUITE ? KEY_A : KEY_DOWN;
}

void setRepeat(int delay, int repeat)
{
  (void)delay;
  (void)repeat;
}

/* mgba.c points newlib's stdout here when it runs in mGBA; nothing else
   reads it. */
const devoptab_t *devoptab_list[3];

/* printf-style calls only format into buffers: no heap. */
void *_sbrk(ptrdiff_t increment)
{
  (void)increment;
  return (void *)-1;
}
