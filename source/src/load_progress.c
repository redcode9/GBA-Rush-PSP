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
  int new_load = !state->started || done <= state->last_done;
  uint64_t late = now_us - state->last_us;
  int due = new_load || late >= LOAD_PROGRESS_INTERVAL_US ||
   (total != 0 && done >= total);

  state->started = 1;
  state->last_done = done;
  /* Redraws keep to a grid of whole intervals from the first one, so a read
     time that does not divide the interval cannot stretch it; a load that
     fell more than an interval behind starts a new grid. */
  if(new_load || late >= 2 * LOAD_PROGRESS_INTERVAL_US)
    state->last_us = now_us;
  else if(late >= LOAD_PROGRESS_INTERVAL_US)
    state->last_us += LOAD_PROGRESS_INTERVAL_US;
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
  unsigned int done_kib = total != 0 && done >= total ? total_kib :
   (unsigned int)(done >> 10);

  snprintf(buf, cap, "%u / %u KiB", done_kib, total_kib);
}
