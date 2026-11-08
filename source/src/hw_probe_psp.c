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
 * The PSP side of the memory system probe (hw_probe.h), built only by
 * make DIAGNOSTIC_HW_PROBE=1. main.c runs it once at startup, at the
 * configured CPU clock, before the ROM buffer takes the heap. Each point
 * is the best of a few runs timed with the system clock: the VBlank
 * interrupt is already live, and the best run is the one it disturbed
 * least.
 */

#include "common.h"
#include "hw_probe.h"

#define PROBE_BUFFER_BYTES (4u << 20)
#define PROBE_SCRATCHPAD ((void *)(uintptr_t)0x00010000u)
#define PROBE_SCRATCHPAD_BYTES (16u << 10)
/* The buffer's tail keeps the scratchpad's contents while it is probed. */
#define PROBE_WORK_BYTES (PROBE_BUFFER_BYTES - PROBE_SCRATCHPAD_BYTES)
/* Far larger than either cache, so the line scans miss on every line. */
#define PROBE_LINE_REGION (1u << 20)
/* Well inside the D-cache. */
#define PROBE_DCACHE_BYTES (4u << 10)
#define PROBE_ACCESSES (1u << 16)
#define PROBE_REPEATS 5u
/* What each bandwidth run moves, in laps over its block. */
#define PROBE_MOVED_BYTES (4u << 20)
/* Used when a scan finds no knee, so the next scan still runs. */
#define PROBE_DEFAULT_LINE 64u
#define PROBE_DEFAULT_CAPACITY (16u << 10)

/* Cycles x100 per access through count slots stride bytes apart at
   base. */
typedef uint32_t (*ProbeScan)(void *base, uint32_t count, uint32_t stride,
 uint32_t mhz);
typedef void (*ProbeCode)(void);

/* Keeps the loads and sums being timed from being optimized away. */
static volatile uintptr_t probe_sink;

/* Microseconds for PROBE_ACCESSES dependent loads along the chain from
   start. */
static uint64_t probe_chase_us(const uintptr_t *start)
{
  uint64_t best = UINT64_MAX;
  uint32_t repeat;

  for(repeat = 0; repeat < PROBE_REPEATS; repeat++)
  {
    const uintptr_t *slot = start;
    uint32_t n = PROBE_ACCESSES / 8;
    uint64_t started = sceKernelGetSystemTimeWide();
    uint64_t spent;

    while(n--)
    {
      slot = (const uintptr_t *)*slot;
      slot = (const uintptr_t *)*slot;
      slot = (const uintptr_t *)*slot;
      slot = (const uintptr_t *)*slot;
      slot = (const uintptr_t *)*slot;
      slot = (const uintptr_t *)*slot;
      slot = (const uintptr_t *)*slot;
      slot = (const uintptr_t *)*slot;
    }
    probe_sink = (uintptr_t)slot;
    spent = sceKernelGetSystemTimeWide() - started;
    if(spent < best)
      best = spent;
  }
  return best;
}

/* Data side: dependent loads along a pointer chain. */
static uint32_t probe_chase(void *base, uint32_t count, uint32_t stride,
 uint32_t mhz)
{
  const uintptr_t *start = hw_probe_chain(base, count, stride);

  if(!start)
    return 0;
  return hw_probe_cycles_x100(probe_chase_us(start), mhz, PROBE_ACCESSES);
}

/* Instruction side: branches from slot to slot, about PROBE_ACCESSES of
   them per run. */
static uint32_t probe_jumps(void *base, uint32_t count, uint32_t stride,
 uint32_t mhz)
{
  uint32_t *code = base;
  ProbeCode run = (ProbeCode)(uintptr_t)code;
  uint32_t bytes = hw_probe_emit(code, count, stride);
  uint64_t best = UINT64_MAX;
  uint32_t calls;
  uint32_t repeat;

  if(!bytes)
    return 0;
  calls = count < PROBE_ACCESSES ? PROBE_ACCESSES / count : 1;
  /* The new code must reach instruction fetch, as in arm_gba.c. */
  sceKernelDcacheWritebackRange(code, bytes);
  sceKernelIcacheInvalidateRange(code, bytes);
  for(repeat = 0; repeat < PROBE_REPEATS; repeat++)
  {
    uint32_t n = calls;
    uint64_t started = sceKernelGetSystemTimeWide();
    uint64_t spent;

    while(n--)
      run();
    spent = sceKernelGetSystemTimeWide() - started;
    if(spent < best)
      best = spent;
  }
  return hw_probe_cycles_x100(best, mhz, calls * count);
}

/* Microseconds to read bytes at block laps times, eight words a step. */
static uint64_t probe_read_us(const void *block, uint32_t bytes,
 uint32_t laps)
{
  uint64_t best = UINT64_MAX;
  uint32_t repeat;

  for(repeat = 0; repeat < PROBE_REPEATS; repeat++)
  {
    uint32_t sum = 0;
    uint32_t lap;
    uint64_t started = sceKernelGetSystemTimeWide();
    uint64_t spent;

    for(lap = 0; lap < laps; lap++)
    {
      const volatile uint32_t *word = block;
      const volatile uint32_t *end = word + bytes / 4;

      for(; word < end; word += 8)
        sum += word[0] + word[1] + word[2] + word[3] + word[4] + word[5] +
         word[6] + word[7];
    }
    probe_sink = sum;
    spent = sceKernelGetSystemTimeWide() - started;
    if(spent < best)
      best = spent;
  }
  return best;
}

/* Microseconds to write bytes at block laps times, eight words a step;
   volatile, so the stores stay although the buffer is freed unread. */
static uint64_t probe_write_us(void *block, uint32_t bytes, uint32_t laps)
{
  uint64_t best = UINT64_MAX;
  uint32_t repeat;

  for(repeat = 0; repeat < PROBE_REPEATS; repeat++)
  {
    uint32_t lap;
    uint64_t started = sceKernelGetSystemTimeWide();
    uint64_t spent;

    for(lap = 0; lap < laps; lap++)
    {
      volatile uint32_t *word = block;
      volatile uint32_t *end = word + bytes / 4;

      for(; word < end; word += 8)
      {
        word[0] = lap;
        word[1] = lap;
        word[2] = lap;
        word[3] = lap;
        word[4] = lap;
        word[5] = lap;
        word[6] = lap;
        word[7] = lap;
      }
    }
    spent = sceKernelGetSystemTimeWide() - started;
    if(spent < best)
      best = spent;
  }
  return best;
}

static void probe_bandwidth(HwProbe *probe, uint32_t memory, void *block,
 uint32_t bytes)
{
  uint32_t laps = PROBE_MOVED_BYTES / bytes;
  uint64_t moved = (uint64_t)bytes * laps;

  probe->read_mib_s[memory] =
   hw_probe_mib_per_s(moved, probe_read_us(block, bytes, laps));
  probe->write_mib_s[memory] =
   hw_probe_mib_per_s(moved, probe_write_us(block, bytes, laps));
}

/* Line, capacity and ways of one cache, each scan spaced by what the one
   before it found. */
static void probe_cache(ProbeScan scan, void *buffer, uint32_t mhz,
 uint32_t *lines, uint32_t *sizes, uint32_t *ways)
{
  uint32_t line;
  uint32_t capacity;
  uint32_t i;

  for(i = 0; i < HW_PROBE_STRIDES; i++)
    lines[i] = scan(buffer, PROBE_LINE_REGION / hw_probe_stride[i],
     hw_probe_stride[i], mhz);
  line = hw_probe_line(lines);
  if(!line)
    line = PROBE_DEFAULT_LINE;
  for(i = 0; i < HW_PROBE_SIZES; i++)
    sizes[i] = scan(buffer, (hw_probe_size[i] << 10) / line, line, mhz);
  capacity = hw_probe_capacity(sizes);
  if(!capacity)
    capacity = PROBE_DEFAULT_CAPACITY;
  /* Slots a whole capacity apart all fall in the same set. */
  for(i = 0; i < HW_PROBE_WAYS; i++)
    ways[i] = scan(buffer, i + 1, capacity, mhz);
}

void hw_probe_psp_run(const char *dir)
{
  HwProbe probe;
  char text[HW_PROBE_CAPACITY];
  char path[MAX_PATH];
  void *buffer;
  uint8_t *saved;
  int length;
  int written;
  int n;
  SceUID file;

  if(!dir || !dir[0])
    return;
  buffer = memalign(64, PROBE_BUFFER_BYTES);
  if(!buffer)
    return;
  saved = (uint8_t *)buffer + PROBE_WORK_BYTES;
  memcpy(saved, PROBE_SCRATCHPAD, PROBE_SCRATCHPAD_BYTES);

  memset(&probe, 0, sizeof(probe));
  probe.cpu_mhz = (uint32_t)scePowerGetCpuClockFrequencyInt();
  probe.bus_mhz = (uint32_t)scePowerGetBusClockFrequencyInt();
  probe.latency_x100[HW_PROBE_SCRATCHPAD] = probe_chase(PROBE_SCRATCHPAD,
   PROBE_SCRATCHPAD_BYTES / 64, 64, probe.cpu_mhz);
  probe.latency_x100[HW_PROBE_DCACHE] = probe_chase(buffer,
   PROBE_DCACHE_BYTES / 64, 64, probe.cpu_mhz);
  probe.latency_x100[HW_PROBE_RAM] = probe_chase(buffer,
   PROBE_WORK_BYTES / 256, 256, probe.cpu_mhz);
  probe_bandwidth(&probe, HW_PROBE_SCRATCHPAD, PROBE_SCRATCHPAD,
   PROBE_SCRATCHPAD_BYTES);
  probe_bandwidth(&probe, HW_PROBE_DCACHE, buffer, PROBE_DCACHE_BYTES);
  probe_bandwidth(&probe, HW_PROBE_RAM, buffer, PROBE_WORK_BYTES);
  probe_cache(probe_chase, buffer, probe.cpu_mhz, probe.dline, probe.dsize,
   probe.dways);
  probe_cache(probe_jumps, buffer, probe.cpu_mhz, probe.iline, probe.isize,
   probe.iways);

  memcpy(PROBE_SCRATCHPAD, saved, PROBE_SCRATCHPAD_BYTES);
  free(buffer);

  length = hw_probe_format(text, sizeof(text), &probe);
  n = snprintf(path, sizeof(path), "%shw-probe.txt", dir);
  if(length <= 0 || n < 0 || n >= (int)sizeof(path))
    return;
  FILE_OPEN(file, path, WRITE);
  if(!FILE_CHECK_VALID(file))
    return;
  written = FILE_WRITE(file, text, length);
  FILE_CLOSE(file);
  if(written < length)
    sceIoRemove(path);
}
