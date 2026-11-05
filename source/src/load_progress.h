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
 * The numbers behind the ROM loading popup in gui.c: when to redraw it,
 * how long its bar is and its "x / y KiB" line. Kept free of PSP headers
 * so tests/test_load_progress.c can check them on the host.
 */

#ifndef LOAD_PROGRESS_H
#define LOAD_PROGRESS_H

#include <stddef.h>
#include <stdint.h>

/* 45 ms between redraws: over 20 per second, with a margin, while each read
   takes less. */
#define LOAD_PROGRESS_INTERVAL_US 45000u

typedef struct
{
  uint64_t last_us;   /* the redraw grid's latest point */
  uint32_t last_done; /* done at the last call */
  int started;        /* nonzero after the first call */
} LoadProgressState;

/* Nonzero when the popup should be redrawn for done of total bytes at
   now_us: on the first call of a load (done only grows during a load, so a
   done not above the last call's starts a new one), at the first call on or
   after each LOAD_PROGRESS_INTERVAL_US step from there, and always at the
   end (done >= total, total not 0). */
int load_progress_due(LoadProgressState *state, uint64_t now_us,
 uint32_t done, uint32_t total);

/* The filled part of a bar width pixels long: 0 when total is 0, never
   more than width. */
uint32_t load_progress_bar(uint32_t done, uint32_t total, uint32_t width);

/* Writes "done / total KiB" into buf, cut to fit its cap bytes; done is
   rounded down and total up, so the line reads total / total only at the
   end. */
void load_progress_text(char *buf, size_t cap, uint32_t done, uint32_t total);

#endif /* LOAD_PROGRESS_H */
