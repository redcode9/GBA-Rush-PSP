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
 * Toasts: short non-modal messages shown one at a time, oldest first.
 * A ring of TOAST_RING_SIZE entries with no allocation and no I/O; times
 * are microseconds from sceKernelGetSystemTimeLow, so they wrap every
 * 2^32 us (about 71 minutes). Host-tested by tests/test_toast.c.
 */

#ifndef TOAST_H
#define TOAST_H

#include <stdint.h>

#define TOAST_RING_SIZE 8
#define TOAST_TEXT_SIZE 64

typedef struct
{
  char text[TOAST_TEXT_SIZE];
  uint32_t start_us;
  uint32_t duration_us;
  uint8_t sjis;
} Toast;

/* Queues text, cut to TOAST_TEXT_SIZE - 1 bytes without splitting a
 * double-byte character (Shift-JIS when sjis is non-zero, else GBK).
 * A text already queued restarts its timer instead; a full ring drops
 * its oldest entry. */
void toast_push(const char *text, uint32_t duration_us, int sjis,
 uint32_t now_us);
/* The toast to show at now_us, or NULL. Drops expired toasts; the next
 * one in the queue is shown for its whole duration from now_us. A toast
 * pushed with a now_us up to its duration later than this one is shown,
 * not dropped, so the caller may read the clock before or after pushes. */
const Toast *toast_current(uint32_t now_us);
void toast_clear(void);

#endif
