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
 * Memory system probe, for diagnostic builds: the latency and bandwidth of
 * the scratchpad, the D-cache and RAM, and the line, capacity and ways of
 * both caches, found where the time per access jumps. This module builds
 * the access patterns, finds the knees and writes the report; the timing
 * runs on the PSP in hw_probe_psp.c.
 */

#ifndef HW_PROBE_H
#define HW_PROBE_H

#include <stddef.h>
#include <stdint.h>

#define HW_PROBE_STRIDES 6u
#define HW_PROBE_SIZES 7u
#define HW_PROBE_WAYS 16u
/* The largest slot spacing hw_probe_emit accepts: its branches reach
   128 KiB, the scans use 64 KiB at most. */
#define HW_PROBE_MAX_STRIDE (64u << 10)
/* Enough for any report. */
#define HW_PROBE_CAPACITY 4096u

/* Indices of latency_x100, read_mib_s and write_mib_s. */
#define HW_PROBE_SCRATCHPAD 0u
#define HW_PROBE_DCACHE 1u
#define HW_PROBE_RAM 2u

/* Line scan strides in bytes, capacity scan sizes in KiB; the ways scan
   runs K = 1..HW_PROBE_WAYS slots. */
extern const uint32_t hw_probe_stride[HW_PROBE_STRIDES];
extern const uint32_t hw_probe_size[HW_PROBE_SIZES];

typedef struct HwProbe
{
  uint32_t cpu_mhz;
  uint32_t bus_mhz;
  uint32_t latency_x100[3];   /* CPU cycles per dependent load, x100 */
  uint32_t read_mib_s[3];
  uint32_t write_mib_s[3];
  /* Cycles x100 per access (D) or per jump (I) at each scan point. */
  uint32_t dline[HW_PROBE_STRIDES];
  uint32_t dsize[HW_PROBE_SIZES];
  uint32_t dways[HW_PROBE_WAYS];
  uint32_t iline[HW_PROBE_STRIDES];
  uint32_t isize[HW_PROBE_SIZES];
  uint32_t iways[HW_PROBE_WAYS];
} HwProbe;

/* A cyclic pointer chain of count slots stride bytes apart in buffer:
   each slot holds the address of the next, the last that of the first.
   Returns the first slot, or NULL when count is 0 or stride is not a
   nonzero multiple of a pointer's size. */
uintptr_t *hw_probe_chain(void *buffer, uint32_t count, uint32_t stride);

/* MIPS code of count slots stride bytes apart: each slot branches to the
   next, the last returns. Returns the bytes to write back and invalidate
   before calling it, or 0 when count is 0 or stride is under 8, not a
   multiple of 4 or over HW_PROBE_MAX_STRIDE. */
uint32_t hw_probe_emit(uint32_t *code, uint32_t count, uint32_t stride);

/* CPU cycles x100 per access for accesses in us microseconds at mhz. */
uint32_t hw_probe_cycles_x100(uint64_t us, uint32_t mhz, uint32_t accesses);
uint32_t hw_probe_mib_per_s(uint64_t bytes, uint64_t us);

/* Knees of the scan curves; 0 when the curve shows none. The line is the
   first stride within 90% of the last point's time, the capacity the size
   before the first point over 125% of the first, the ways the points
   before that jump. */
uint32_t hw_probe_line(const uint32_t curve[HW_PROBE_STRIDES]);
uint32_t hw_probe_capacity(const uint32_t curve[HW_PROBE_SIZES]);
uint32_t hw_probe_ways(const uint32_t curve[HW_PROBE_WAYS]);

/* The report: key=value lines with the clocks, latencies, bandwidths, the
   knees and the raw curves. Returns its length, or -1 if it does not fit
   in size bytes. */
int hw_probe_format(char *out, size_t size, const HwProbe *probe);

/* Times the memory system and writes <dir>hw-probe.txt. It lives in
   hw_probe_psp.c, built only by make DIAGNOSTIC_HW_PROBE=1. */
void hw_probe_psp_run(const char *dir);

#endif
