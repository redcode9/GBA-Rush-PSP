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

#include "hw_probe.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* MIPS "b" (beq $0, $0) with an empty offset, "jr $ra" and "nop". */
#define MIPS_B 0x10000000u
#define MIPS_JR_RA 0x03E00008u
#define MIPS_NOP 0x00000000u

const uint32_t hw_probe_stride[HW_PROBE_STRIDES] = { 8, 16, 32, 64, 128, 256 };
const uint32_t hw_probe_size[HW_PROBE_SIZES] = { 1, 2, 4, 8, 16, 32, 64 };

static const char *const memory_names[3] = { "scratchpad", "dcache", "ram" };

uintptr_t *hw_probe_chain(void *buffer, uint32_t count, uint32_t stride)
{
  unsigned char *base = buffer;
  uint32_t i;

  if(!count || !stride || stride % sizeof(uintptr_t))
    return NULL;
  for(i = 0; i < count; i++)
    *(uintptr_t *)(base + (size_t)i * stride) =
     (uintptr_t)(base + (size_t)((i + 1) % count) * stride);
  return buffer;
}

uint32_t hw_probe_emit(uint32_t *code, uint32_t count, uint32_t stride)
{
  uint32_t i;

  if(!count || stride < 8 || stride % 4 || stride > HW_PROBE_MAX_STRIDE)
    return 0;
  /* A branch lands at its address + 4 + offset * 4. */
  for(i = 0; i + 1 < count; i++)
  {
    code[i * (stride / 4)] = MIPS_B | ((stride - 4) >> 2);
    code[i * (stride / 4) + 1] = MIPS_NOP;
  }
  code[i * (stride / 4)] = MIPS_JR_RA;
  code[i * (stride / 4) + 1] = MIPS_NOP;
  return i * stride + 8;
}

uint32_t hw_probe_cycles_x100(uint64_t us, uint32_t mhz, uint32_t accesses)
{
  uint64_t value;

  if(!accesses)
    return 0;
  value = us * mhz * 100u / accesses;
  return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

uint32_t hw_probe_mib_per_s(uint64_t bytes, uint64_t us)
{
  uint64_t value;

  if(!us)
    return 0;
  value = bytes * 1000000u / us >> 20;
  return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

/* The first point after the first over 125% of it, or 0 when there is
   none; a first point of 0 is no baseline, so it gives 0 too. */
static uint32_t first_jump(const uint32_t *curve, uint32_t points)
{
  uint32_t i;

  if(!curve[0])
    return 0;
  for(i = 1; i < points; i++)
    if((uint64_t)curve[i] * 100u > (uint64_t)curve[0] * 125u)
      return i;
  return 0;
}

uint32_t hw_probe_line(const uint32_t curve[HW_PROBE_STRIDES])
{
  uint64_t last = curve[HW_PROBE_STRIDES - 1];
  uint32_t i;

  if(!first_jump(curve, HW_PROBE_STRIDES))
    return 0;
  for(i = 0; (uint64_t)curve[i] * 10u < last * 9u; i++)
    ;
  return hw_probe_stride[i];
}

uint32_t hw_probe_capacity(const uint32_t curve[HW_PROBE_SIZES])
{
  uint32_t jump = first_jump(curve, HW_PROBE_SIZES);

  return jump ? hw_probe_size[jump - 1] << 10 : 0;
}

uint32_t hw_probe_ways(const uint32_t curve[HW_PROBE_WAYS])
{
  return first_jump(curve, HW_PROBE_WAYS);
}

/* Appends to out at *length; keeps -1 there once something does not fit. */
static void append(char *out, size_t size, int *length, const char *format,
 ...)
{
  va_list args;
  int n;

  if(*length < 0)
    return;
  va_start(args, format);
  n = vsnprintf(out + *length, size - (size_t)*length, format, args);
  va_end(args);
  *length = n < 0 || (size_t)n >= size - (size_t)*length ? -1 : *length + n;
}

static void append_cache(char *out, size_t size, int *length,
 const char *cache, const uint32_t *line, const uint32_t *sizes,
 const uint32_t *ways)
{
  append(out, size, length, "%s-line-bytes=%u\n%s-capacity-bytes=%u\n"
   "%s-ways=%u\n", cache, (unsigned)hw_probe_line(line), cache,
   (unsigned)hw_probe_capacity(sizes), cache, (unsigned)hw_probe_ways(ways));
}

static void append_curves(char *out, size_t size, int *length,
 const char *cache, const uint32_t *line, const uint32_t *sizes,
 const uint32_t *ways)
{
  uint32_t i;

  for(i = 0; i < HW_PROBE_STRIDES; i++)
    append(out, size, length, "%s-stride-%u=%u\n", cache,
     (unsigned)hw_probe_stride[i], (unsigned)line[i]);
  for(i = 0; i < HW_PROBE_SIZES; i++)
    append(out, size, length, "%s-size-%u=%u\n", cache,
     (unsigned)(hw_probe_size[i] << 10), (unsigned)sizes[i]);
  for(i = 0; i < HW_PROBE_WAYS; i++)
    append(out, size, length, "%s-ways-%u=%u\n", cache, (unsigned)(i + 1),
     (unsigned)ways[i]);
}

int hw_probe_format(char *out, size_t size, const HwProbe *probe)
{
  int length = 0;
  uint32_t i;

  if(!size)
    return -1;
  out[0] = '\0';
  append(out, size, &length, "hw-probe=1\ncpu-mhz=%u\nbus-mhz=%u\n",
   (unsigned)probe->cpu_mhz, (unsigned)probe->bus_mhz);
  for(i = 0; i < 3; i++)
    append(out, size, &length, "%s-latency-cycles-x100=%u\n"
     "%s-read-mib-s=%u\n%s-write-mib-s=%u\n", memory_names[i],
     (unsigned)probe->latency_x100[i], memory_names[i],
     (unsigned)probe->read_mib_s[i], memory_names[i],
     (unsigned)probe->write_mib_s[i]);
  append_cache(out, size, &length, "dcache", probe->dline, probe->dsize,
   probe->dways);
  append_cache(out, size, &length, "icache", probe->iline, probe->isize,
   probe->iways);
  append_curves(out, size, &length, "dcache", probe->dline, probe->dsize,
   probe->dways);
  append_curves(out, size, &length, "icache", probe->iline, probe->isize,
   probe->iways);
  return length;
}
