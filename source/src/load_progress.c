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

#include <stdio.h>

#include "load_progress.h"

int load_progress_due(LoadProgressState *state, uint64_t now_us,
 uint32_t done, uint32_t total)
{
  int due = !state->started || done <= state->last_done || done >= total ||
   now_us - state->last_us >= LOAD_PROGRESS_INTERVAL_US;

  state->started = 1;
  state->last_done = done;
  if(due)
    state->last_us = now_us;
  return due;
}

uint32_t load_progress_bar(uint32_t done, uint32_t total, uint32_t width)
{
  if(total == 0)
    return 0;
  if(done >= total)
    return width;
  return (uint32_t)((uint64_t)done * width / total);
}

void load_progress_text(char *buf, size_t cap, uint32_t done, uint32_t total)
{
  unsigned int total_kib = (unsigned int)(total >> 10) +
   ((total & 1023u) != 0);

  snprintf(buf, cap, "%u / %u KiB", (unsigned int)(done >> 10), total_kib);
}
