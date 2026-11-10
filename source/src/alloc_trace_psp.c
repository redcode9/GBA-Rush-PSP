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
 * The allocation trace of the diagnostic profile. source/Makefile links it
 * with -Wl,--wrap=malloc,--wrap=free, so every malloc and free call lands
 * here first and goes on to newlib's through __real_malloc and
 * __real_free. calloc, realloc and memalign are not wrapped. The wrappers
 * never allocate or format: they only push to a mem_stats ring.
 */

#include "common.h"
#include "mem_stats.h"

void *__real_malloc(size_t size);
void __real_free(void *pointer);
void *__wrap_malloc(size_t size);
void __wrap_free(void *pointer);

static MemStatsTraceRing alloc_trace_ring;
/* The traced thread; -1 until alloc_trace_start. */
static SceUID alloc_trace_thread = -1;

/* shortcut: other threads untraced, upgrade if a leak is suspected there.
   One thread keeps the ring single-writer, as the runtime log rings are. */
static int alloc_trace_here(void)
{
  return alloc_trace_thread >= 0 &&
   sceKernelGetThreadId() == alloc_trace_thread;
}

/* Nothing names the wrappers until the linker applies --wrap. */
__attribute__((used)) void *__wrap_malloc(size_t size)
{
  void *caller = __builtin_return_address(0);
  void *pointer = __real_malloc(size);

  if(alloc_trace_here())
    mem_stats_trace_push(&alloc_trace_ring, (uint32_t)(uintptr_t)pointer,
     (uint32_t)size, (uint32_t)(uintptr_t)caller);
  return pointer;
}

__attribute__((used)) void __wrap_free(void *pointer)
{
  void *caller = __builtin_return_address(0);

  if(pointer != NULL && alloc_trace_here())
    mem_stats_trace_push(&alloc_trace_ring, (uint32_t)(uintptr_t)pointer,
     MEM_STATS_TRACE_FREE, (uint32_t)(uintptr_t)caller);
  __real_free(pointer);
}

void alloc_trace_start(void)
{
  alloc_trace_thread = sceKernelGetThreadId();
}

size_t alloc_trace_snapshot(MemStatsTraceEntry *out, size_t capacity)
{
  return mem_stats_trace_snapshot(&alloc_trace_ring, out, capacity);
}
