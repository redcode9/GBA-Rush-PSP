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
 * GBA timer arithmetic for main.c, which keeps a running timer as the
 * distance to its next overflow: in cycles for a timer that counts
 * cycles, in overflows of the timer before for a count-up one. Between
 * overflows a timer ticks every 1 << shift cycles (shift 0, 6, 8 or 10;
 * 0 for count-up) and overflows after `reload` ticks, reload being
 * 0x10000 minus the TMxCNT_L value written; each overflow starts it
 * again from 0x10000 - reload. Its ticks fall 1 << shift cycles apart
 * from its start, or from the write that changed its rate.
 *
 * Kept free of PSP headers so tests/test_gba_timer.c checks it on the
 * host.
 */

#ifndef GBA_TIMER_H
#define GBA_TIMER_H

#include <stdint.h>

/* A timer whose next overflow is *left away, or was -*left ago when *left
   is 0 or less: returns the overflows due by now, and moves *left to the
   next overflow still ahead, in [1, reload << shift]. */
static inline uint32_t gba_timer_catch_up(int32_t *left, uint32_t reload,
 unsigned shift)
{
  uint32_t period = reload << shift;
  uint32_t late;
  uint32_t overflows = 1;

  if(*left > 0)
    return 0;
  late = 0u - (uint32_t)*left;
  if(late >= period)
  {
    overflows += late / period;
    late %= period;
  }
  *left = (int32_t)(period - late);
  return overflows;
}

/* TMxCNT_L of a running timer whose next overflow is `left` away, ahead
   or behind as for gba_timer_catch_up: always in [0x10000 - reload,
   0xFFFF]. */
static inline uint32_t gba_timer_counter(int32_t left, uint32_t reload,
 unsigned shift)
{
  gba_timer_catch_up(&left, reload, shift);
  return 0x10000u - (((uint32_t)left + (1u << shift) - 1) >> shift);
}

/* A TMxCNT_H write that leaves the timer running at `shift`: how far from
   the write its next overflow is. A start (running 0) loads the reload
   value. A running timer, whose next overflow was `left` away at
   old_shift, keeps its count: at an unchanged rate its overflow stays
   where it was, at a new one its count goes on from the write. */
static inline int32_t gba_timer_written(int running, int32_t left,
 uint32_t reload, unsigned old_shift, unsigned shift)
{
  if(!running)
    return (int32_t)(reload << shift);
  if(shift == old_shift)
    return left;
  return (int32_t)((0x10000u - gba_timer_counter(left, reload, old_shift)) <<
   shift);
}

#endif /* GBA_TIMER_H */
