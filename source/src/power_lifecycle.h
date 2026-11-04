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
 * Suspend and resume hand-off between the PSP power callback thread and the
 * emulator thread. The callback only bumps sequence counters; the emulator
 * thread takes each pending save and resume exactly once (main.c).
 */

#ifndef POWER_LIFECYCLE_H
#define POWER_LIFECYCLE_H

#include <stdint.h>

/* Power callback bits, copied from libpower/psppower.h (PSP only); main.c
   checks at compile time that the two agree. */
#define POWER_LIFECYCLE_BIT_POWER_SWITCH 0x80000000u
#define POWER_LIFECYCLE_BIT_RESUME_COMPLETE 0x00040000u
#define POWER_LIFECYCLE_BIT_RESUMING 0x00020000u
#define POWER_LIFECYCLE_BIT_SUSPENDING 0x00010000u

/* How a suspend was entered, kept in PowerLifecycle.suspended (0 while
   running). A switch-only entry becomes a SUSPENDING one if that follows. */
#define POWER_LIFECYCLE_ENTRY_SWITCH 1u
#define POWER_LIFECYCLE_ENTRY_SUSPENDING 2u
#define POWER_LIFECYCLE_ENTRY_MASK 3u
/* power_lifecycle_classify() ORs this onto the entry kind on resume. */
#define POWER_LIFECYCLE_ACTION_RESUME 4u
/* Runtime log outcome of a switch-only entry that timed out; 0-2 are the
   entry kinds above. */
#define POWER_LIFECYCLE_SWITCH_EXPIRED 3u

/* A switch-only entry with neither SUSPENDING nor RESUME_COMPLETE after it
   ends after this long, measured by the emulator thread. */
#define POWER_LIFECYCLE_SWITCH_TIMEOUT_US 2000000u
/* audio_result of a resume the callback did not do (the switch timed out):
   the emulator thread must reserve the audio channel itself. PSP error
   codes never take this value. */
#define POWER_LIFECYCLE_AUDIO_NOT_RESERVED INT32_MIN

typedef enum
{
  POWER_LIFECYCLE_RUNNING,
  POWER_LIFECYCLE_SUSPENDED,
  POWER_LIFECYCLE_RESUME_PENDING
} PowerLifecycleState;

typedef struct
{
  /* Written by the power callback. */
  volatile uint32_t suspended;
  volatile uint32_t save_sequence;
  volatile uint32_t resume_sequence;
  volatile int32_t audio_result;
  /* Raw callback bits ORed together and the callbacks counted since the
     emulator thread last took them. */
  volatile uint32_t raw_bits;
  volatile uint32_t raw_callbacks;
  /* Written by the emulator thread. */
  uint32_t handled_save_sequence;
  uint32_t handled_resume_sequence;
} PowerLifecycle;

void power_lifecycle_init(PowerLifecycle *lifecycle);
/* Power callback side. classify() turns callback bits into an entry kind
   (or 0) plus POWER_LIFECYCLE_ACTION_RESUME. suspend() takes that entry
   kind; audio_result is what reopening audio returned. */
uint32_t power_lifecycle_classify(uint32_t bits);
void power_lifecycle_suspend(PowerLifecycle *lifecycle, uint32_t entry,
 int request_save);
void power_lifecycle_resume(PowerLifecycle *lifecycle, int audio_result);
void power_lifecycle_note_bits(PowerLifecycle *lifecycle, uint32_t bits);
/* Emulator thread side. The take functions return 1 once per event. */
PowerLifecycleState power_lifecycle_state(const PowerLifecycle *lifecycle);
int power_lifecycle_take_save(PowerLifecycle *lifecycle);
int power_lifecycle_take_resume(PowerLifecycle *lifecycle,
 int *audio_result);
/* Ends a switch-only entry once the timeout has passed since entered_us,
   as a resume with POWER_LIFECYCLE_AUDIO_NOT_RESERVED. Returns 1 if it did. */
int power_lifecycle_expire_switch(PowerLifecycle *lifecycle,
 uint64_t entered_us, uint64_t now_us);
/* Returns the bits noted since the last call (0 if none) and stores the
   callback count in *callbacks. */
uint32_t power_lifecycle_take_bits(PowerLifecycle *lifecycle,
 uint32_t *callbacks);

#endif /* POWER_LIFECYCLE_H */
