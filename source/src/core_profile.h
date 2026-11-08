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
 * Sampling profile of the new core, for diagnostic builds: a sampler that
 * wakes at a fixed interval adds what the core's thread was doing (its
 * phase, arm_mips.h, and for generated code the block it was in), or that
 * the thread was blocked. The report tells where emulation time goes:
 * generated code, dispatch, memory accesses, translation, the interpreter
 * or the rest of the system, and which blocks are hot.
 */

#ifndef CORE_PROFILE_H
#define CORE_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "arm_mips.h"

/* Blocks counted one by one; samples of others go to code-other. */
#define CORE_PROFILE_BLOCKS 1024u
/* Blocks the report lists, hottest first. */
#define CORE_PROFILE_TOP 32u
/* Enough for any report. */
#define CORE_PROFILE_CAPACITY 2048u

typedef struct
{
  uint32_t phase[ARM_MIPS_PHASES];
  uint32_t waiting;           /* samples with the core's thread blocked */
  uint32_t other;             /* code samples of no block in the table */
  uint32_t key[CORE_PROFILE_BLOCKS];    /* block key + 1, 0 when free */
  uint32_t count[CORE_PROFILE_BLOCKS];
} CoreProfile;

void core_profile_clear(CoreProfile *profile);
/* A sample with the core's thread running in phase; for generated code,
   key is the block's (0 when unknown). */
void core_profile_add(CoreProfile *profile, unsigned phase, uint32_t key);
void core_profile_add_waiting(CoreProfile *profile);

/* The report: key=value lines with the sample interval, the samples per
   phase with their share in thousandths of the running samples, the
   core's translation times (stats->decode_ticks and compile_ticks taken
   as microseconds) and the hottest blocks. Returns its length, or -1 if
   it does not fit in size bytes. */
int core_profile_format(char *out, size_t size, const CoreProfile *profile,
 uint32_t sample_us, const ArmMipsStats *stats);

#endif
