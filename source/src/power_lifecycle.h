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
  /* Written by the emulator thread. */
  uint32_t handled_save_sequence;
  uint32_t handled_resume_sequence;
} PowerLifecycle;

void power_lifecycle_init(PowerLifecycle *lifecycle);
/* Power callback side. audio_result is what reopening audio returned. */
void power_lifecycle_suspend(PowerLifecycle *lifecycle, int request_save);
void power_lifecycle_resume(PowerLifecycle *lifecycle, int audio_result);
/* Emulator thread side. The take functions return 1 once per event. */
PowerLifecycleState power_lifecycle_state(const PowerLifecycle *lifecycle);
int power_lifecycle_take_save(PowerLifecycle *lifecycle);
int power_lifecycle_take_resume(PowerLifecycle *lifecycle,
 int *audio_result);

#endif /* POWER_LIFECYCLE_H */
