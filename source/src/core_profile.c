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

#include "core_profile.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const phase_names[ARM_MIPS_PHASES] =
{
  "code", "dispatch", "memory", "translate", "interpret", "events"
};

void core_profile_clear(CoreProfile *profile)
{
  memset(profile, 0, sizeof(*profile));
}

void core_profile_add(CoreProfile *profile, unsigned phase, uint32_t key)
{
  unsigned slot;
  unsigned probe;

  if(phase >= ARM_MIPS_PHASES)
    return;
  profile->phase[phase]++;
  if(phase != ARM_MIPS_PHASE_CODE)
    return;
  /* Open addressing on the key; a full neighbourhood counts as other. */
  slot = ((key * 0x9E3779B1u) >> 16) & (CORE_PROFILE_BLOCKS - 1);
  for(probe = 0; key && probe < 16; probe++)
  {
    unsigned i = (slot + probe) & (CORE_PROFILE_BLOCKS - 1);

    if(!profile->key[i])
      profile->key[i] = key + 1;
    if(profile->key[i] == key + 1)
    {
      profile->count[i]++;
      return;
    }
  }
  profile->other++;
}

void core_profile_add_waiting(CoreProfile *profile)
{
  profile->waiting++;
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

static uint32_t thousandths(uint32_t part, uint32_t whole)
{
  return whole ? (uint32_t)((uint64_t)part * 1000u / whole) : 0;
}

int core_profile_format(char *out, size_t size, const CoreProfile *profile,
 uint32_t sample_us, const ArmMipsStats *stats)
{
  uint8_t listed[CORE_PROFILE_BLOCKS];
  uint32_t running = 0;
  uint32_t ticks = stats->decode_ticks + stats->compile_ticks;
  uint32_t unlisted = profile->other;
  int length = 0;
  unsigned i;
  unsigned n;

  if(!size)
    return -1;
  out[0] = '\0';
  for(i = 0; i < ARM_MIPS_PHASES; i++)
    running += profile->phase[i];
  append(out, size, &length, "core-profile=1\nsample-us=%u\n",
   (unsigned)sample_us);
  append(out, size, &length, "samples=%u\nwaiting=%u\n", (unsigned)running,
   (unsigned)profile->waiting);
  for(i = 0; i < ARM_MIPS_PHASES; i++)
    append(out, size, &length, "phase-%s=%u\nphase-%s-permille=%u\n",
     phase_names[i], (unsigned)profile->phase[i], phase_names[i],
     (unsigned)thousandths(profile->phase[i], running));
  append(out, size, &length, "translate-decode-us=%u\n"
   "translate-compile-us=%u\n", (unsigned)stats->decode_ticks,
   (unsigned)stats->compile_ticks);
  append(out, size, &length, "translate-instructions=%u\n"
   "translate-ns-per-instruction=%u\n", (unsigned)stats->guest_insns,
   (unsigned)(stats->guest_insns ? (uint64_t)ticks * 1000u /
   stats->guest_insns : 0));
  append(out, size, &length, "blocks-compiled=%u\nflushes=%u\n",
   (unsigned)stats->blocks, (unsigned)stats->flushes);
  append(out, size, &length, "bails=%u\ndispatch-misses=%u\n",
   (unsigned)stats->bails, (unsigned)stats->misses);

  /* The hottest blocks, by repeated selection (the report is rare). */
  memset(listed, 0, sizeof(listed));
  for(n = 0; n < CORE_PROFILE_TOP; n++)
  {
    unsigned best = CORE_PROFILE_BLOCKS;

    for(i = 0; i < CORE_PROFILE_BLOCKS; i++)
      if(profile->key[i] && !listed[i] && (best == CORE_PROFILE_BLOCKS ||
         profile->count[i] > profile->count[best]))
        best = i;
    if(best == CORE_PROFILE_BLOCKS)
      break;
    listed[best] = 1;
    append(out, size, &length, "block-%08X=%u\n",
     (unsigned)(profile->key[best] - 1), (unsigned)profile->count[best]);
  }
  for(i = 0; i < CORE_PROFILE_BLOCKS; i++)
    if(profile->key[i] && !listed[i])
      unlisted += profile->count[i];
  append(out, size, &length, "code-other=%u\n", (unsigned)unlisted);
  return length;
}
