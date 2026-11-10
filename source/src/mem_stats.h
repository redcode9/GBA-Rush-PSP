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
 * Memory measurements for the runtime log: the image, the heap, the kernel
 * memory outside it and the ROM buffer, taken at a few cold points. This
 * module finds the largest block an allocator gives and packs a
 * measurement into two MEMORY_STATS records; main.c reads the PSP side.
 */

#ifndef MEM_STATS_H
#define MEM_STATS_H

#include <stddef.h>
#include <stdint.h>

/* The step of the largest block search. */
#define MEM_STATS_STEP 4096u

/* Where a measurement was taken; tools/decode_runtime_log.py names them in
   this order (MEMORY_POINTS). */
typedef enum
{
  MEM_STATS_BOOT = 1,
  MEM_STATS_ROM_LOAD,
  MEM_STATS_LINK_START,
  MEM_STATS_CAPTURE_START,
  MEM_STATS_CAPTURE_STOP
} MemStatsPoint;

/* Bytes. */
typedef struct
{
  uint32_t image;
  uint32_t heap_free;
  uint32_t heap_largest;
  uint32_t kernel_free;
  uint32_t kernel_largest;
  uint32_t rom_buffer;
} MemStats;

/* The largest multiple of MEM_STATS_STEP up to limit that alloc returns
   non-NULL for, found by bisection; 0 when one step fails. Each block is
   released before the next try, so at most one is held at a time. */
uint32_t mem_stats_largest_block(void *(*alloc)(size_t, void *),
 void (*release)(void *, void *), void *context, uint32_t limit);

/* Lowers each field of lowest to sample's where that is lower. */
void mem_stats_keep_lowest(MemStats *lowest, const MemStats *sample);

/* Two records with flags = point << 1 | part. Part 0 holds image,
   heap_free and heap_largest, part 1 kernel_free, kernel_largest and
   rom_buffer, all in KiB rounded down. The ROM pages are rom_buffer / 32
   KiB, as memory.c counts them, so they are not stored. */
void mem_stats_encode(const MemStats *stats, MemStatsPoint point,
 uint8_t flags[2], uint32_t values[2][3]);
/* The inverse, in bytes: 0 when a point is unknown, the two points differ,
   the parts are not 0 then 1, or a value is over 4 GiB. */
int mem_stats_decode(const uint8_t flags[2], const uint32_t values[2][3],
 MemStats *stats, MemStatsPoint *point);

/* The diagnostic profile's allocation trace: the last malloc and free calls
   of one thread, written by alloc_trace_psp.c. */
#define MEM_STATS_TRACE_CAPACITY 256u
/* The size of an entry that records a free. */
#define MEM_STATS_TRACE_FREE UINT32_MAX

/* Ring indices are masked, so the capacity must be a power of two. */
typedef char MemStatsTraceCapacityCheck[
 (MEM_STATS_TRACE_CAPACITY != 0u &&
  (MEM_STATS_TRACE_CAPACITY & (MEM_STATS_TRACE_CAPACITY - 1u)) == 0u) ?
  1 : -1];

typedef struct
{
  uint32_t pointer;
  uint32_t size;
  uint32_t caller;
} MemStatsTraceEntry;

/* Zeroed means empty. next counts the pushes, and stays at least the
   capacity once the ring is full. */
typedef struct
{
  MemStatsTraceEntry entries[MEM_STATS_TRACE_CAPACITY];
  uint32_t next;
} MemStatsTraceRing;

/* Records an entry, overwriting the oldest when the ring is full. */
void mem_stats_trace_push(MemStatsTraceRing *ring, uint32_t pointer,
 uint32_t size, uint32_t caller);
/* Copies the newest entries, at most capacity of them, oldest first;
   returns how many. */
size_t mem_stats_trace_snapshot(const MemStatsTraceRing *ring,
 MemStatsTraceEntry *out, size_t capacity);

/* The PSP side, in alloc_trace_psp.c, built only by PROFILE=diagnostic:
   traces the calling thread's malloc and free from now on, and copies the
   trace as mem_stats_trace_snapshot does. */
void alloc_trace_start(void);
size_t alloc_trace_snapshot(MemStatsTraceEntry *out, size_t capacity);

#endif
