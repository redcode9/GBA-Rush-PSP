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

#include "mem_stats.h"

uint32_t mem_stats_largest_block(void *(*alloc)(size_t, void *),
 void (*release)(void *, void *), void *context, uint32_t limit)
{
  /* In steps: low is known to fit, anything above high is known not to. */
  uint32_t low = 0;
  uint32_t high = limit / MEM_STATS_STEP;

  while(low < high)
  {
    uint32_t middle = low + (high - low + 1u) / 2u;
    void *block = alloc((size_t)middle * MEM_STATS_STEP, context);

    if(block != NULL)
    {
      release(block, context);
      low = middle;
    }
    else
      high = middle - 1u;
  }
  return low * MEM_STATS_STEP;
}

void mem_stats_encode(const MemStats *stats, MemStatsPoint point,
 uint8_t flags[2], uint32_t values[2][3])
{
  flags[0] = (uint8_t)(point << 1);
  flags[1] = (uint8_t)(point << 1 | 1u);
  values[0][0] = stats->image >> 10;
  values[0][1] = stats->heap_free >> 10;
  values[0][2] = stats->heap_largest >> 10;
  values[1][0] = stats->kernel_free >> 10;
  values[1][1] = stats->kernel_largest >> 10;
  values[1][2] = stats->rom_buffer >> 10;
}

int mem_stats_decode(const uint8_t flags[2], const uint32_t values[2][3],
 MemStats *stats, MemStatsPoint *point)
{
  uint32_t first = flags[0] >> 1;
  int part;
  int i;

  if(first < MEM_STATS_BOOT || first > MEM_STATS_CAPTURE_STOP ||
   (flags[0] & 1) || flags[1] != (flags[0] | 1))
    return 0;
  for(part = 0; part < 2; part++)
    for(i = 0; i < 3; i++)
      if(values[part][i] > UINT32_MAX >> 10)
        return 0;
  stats->image = values[0][0] << 10;
  stats->heap_free = values[0][1] << 10;
  stats->heap_largest = values[0][2] << 10;
  stats->kernel_free = values[1][0] << 10;
  stats->kernel_largest = values[1][1] << 10;
  stats->rom_buffer = values[1][2] << 10;
  *point = (MemStatsPoint)first;
  return 1;
}
