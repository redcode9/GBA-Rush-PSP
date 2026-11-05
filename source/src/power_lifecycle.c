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

#include "power_lifecycle.h"

#include <string.h>

/* Full barrier between the power callback and the emulator thread:
   Allegrex SYNC on the PSP, the GCC builtin in host tests. */
static void power_lifecycle_memory_barrier(void)
{
#ifdef __PSP__
  __builtin_allegrex_sync();
#else
  __sync_synchronize();
#endif
}

void power_lifecycle_init(PowerLifecycle *lifecycle)
{
  if(lifecycle == NULL)
    return;

  memset(lifecycle, 0, sizeof(*lifecycle));
}

/* SUSPENDING enters; POWER_SWITCH enters only when it is not part of a
   resume; RESUME_COMPLETE resumes. */
uint32_t power_lifecycle_classify(uint32_t bits)
{
  uint32_t action = 0;

  if(bits & POWER_LIFECYCLE_BIT_SUSPENDING)
    action = POWER_LIFECYCLE_ENTRY_SUSPENDING;
  else if((bits & POWER_LIFECYCLE_BIT_POWER_SWITCH) &&
   !(bits & (POWER_LIFECYCLE_BIT_RESUMING |
    POWER_LIFECYCLE_BIT_RESUME_COMPLETE)))
    action = POWER_LIFECYCLE_ENTRY_SWITCH;
  if(bits & POWER_LIFECYCLE_BIT_RESUME_COMPLETE)
    action |= POWER_LIFECYCLE_ACTION_RESUME;
  return action;
}

/* The counters are published before the suspended word turns non-zero, so
   the emulator thread never sees the entry without the request behind it.
   The emulator thread only ever moves the word from SWITCH to 0, so the
   upgrade to SUSPENDING must be a compare-and-swap. */
void power_lifecycle_suspend(PowerLifecycle *lifecycle, uint32_t entry,
 int request_save)
{
  if(lifecycle == NULL || entry == 0)
    return;

  /* The switch already asked for this suspend's save. */
  if(entry == POWER_LIFECYCLE_ENTRY_SUSPENDING &&
     __sync_bool_compare_and_swap(&lifecycle->suspended,
      POWER_LIFECYCLE_ENTRY_SWITCH, POWER_LIFECYCLE_ENTRY_SUSPENDING))
    return;
  if(lifecycle->suspended)
    return;

  if(request_save)
    lifecycle->save_sequence++;
  power_lifecycle_memory_barrier();
  lifecycle->suspended = entry;
}

/* The callback is the only writer: the slot is filled before the count
   that publishes it. */
void power_lifecycle_note_bits(PowerLifecycle *lifecycle, uint32_t bits)
{
  uint32_t count;

  if(lifecycle == NULL)
    return;

  count = lifecycle->raw_callbacks;
  lifecycle->raw_bits[count % POWER_LIFECYCLE_RAW_SLOTS] = bits;
  power_lifecycle_memory_barrier();
  lifecycle->raw_callbacks = count + 1u;
}

void power_lifecycle_resume(PowerLifecycle *lifecycle, int audio_result)
{
  if(lifecycle == NULL || !lifecycle->suspended)
    return;

  lifecycle->audio_result = audio_result;
  lifecycle->resume_sequence++;
  power_lifecycle_memory_barrier();
  lifecycle->suspended = 0;
}

PowerLifecycleState power_lifecycle_state(const PowerLifecycle *lifecycle)
{
  if(lifecycle == NULL)
    return POWER_LIFECYCLE_RUNNING;

  power_lifecycle_memory_barrier();
  if(lifecycle->suspended)
    return POWER_LIFECYCLE_SUSPENDED;
  if(lifecycle->handled_resume_sequence != lifecycle->resume_sequence)
    return POWER_LIFECYCLE_RESUME_PENDING;
  return POWER_LIFECYCLE_RUNNING;
}

int power_lifecycle_take_save(PowerLifecycle *lifecycle)
{
  uint32_t sequence;

  if(lifecycle == NULL)
    return 0;

  power_lifecycle_memory_barrier();
  sequence = lifecycle->save_sequence;
  if(lifecycle->handled_save_sequence == sequence)
    return 0;

  lifecycle->handled_save_sequence = sequence;
  return 1;
}

int power_lifecycle_take_resume(PowerLifecycle *lifecycle,
 int *audio_result)
{
  uint32_t sequence;

  if(lifecycle == NULL)
    return 0;

  power_lifecycle_memory_barrier();
  sequence = lifecycle->resume_sequence;
  if(lifecycle->handled_resume_sequence == sequence)
    return 0;

  if(audio_result != NULL)
    *audio_result = lifecycle->audio_result;
  lifecycle->handled_resume_sequence = sequence;
  return 1;
}

/* The compare-and-swap loses to a SUSPENDING or RESUME_COMPLETE that the
   callback handled first; a SUSPENDING after it is a fresh suspend. */
int power_lifecycle_expire_switch(PowerLifecycle *lifecycle,
 uint64_t entered_us, uint64_t now_us)
{
  if(lifecycle == NULL ||
     now_us < entered_us + POWER_LIFECYCLE_SWITCH_TIMEOUT_US ||
     !__sync_bool_compare_and_swap(&lifecycle->suspended,
      POWER_LIFECYCLE_ENTRY_SWITCH, 0u))
    return 0;

  lifecycle->audio_result = POWER_LIFECYCLE_AUDIO_NOT_RESERVED;
  __sync_fetch_and_add(&lifecycle->resume_sequence, 1u);
  return 1;
}

/* Callback index i overwrites the slot of i - POWER_LIFECYCLE_RAW_SLOTS
   once the count reaches i, so a slot read while the count stays below its
   index + POWER_LIFECYCLE_RAW_SLOTS is intact; otherwise skip ahead. */
int power_lifecycle_take_bits(PowerLifecycle *lifecycle, uint32_t *bits,
 uint32_t *number)
{
  uint32_t count;
  uint32_t next;
  uint32_t value;

  if(lifecycle == NULL)
    return 0;

  do
  {
    power_lifecycle_memory_barrier();
    count = lifecycle->raw_callbacks;
    next = lifecycle->taken_callbacks;
    if(next == count)
      return 0;
    if(count - next >= POWER_LIFECYCLE_RAW_SLOTS)
      next = count - (POWER_LIFECYCLE_RAW_SLOTS - 1u);
    value = lifecycle->raw_bits[next % POWER_LIFECYCLE_RAW_SLOTS];
    power_lifecycle_memory_barrier();
  }
  while(lifecycle->raw_callbacks - next >= POWER_LIFECYCLE_RAW_SLOTS);

  lifecycle->taken_callbacks = next + 1u;
  if(bits != NULL)
    *bits = value;
  if(number != NULL)
    *number = next + 1u;
  return 1;
}
