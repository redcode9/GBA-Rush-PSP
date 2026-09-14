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

#ifndef LIFECYCLE_TEARDOWN_H
#define LIFECYCLE_TEARDOWN_H

#include <stdint.h>

typedef enum
{
  LIFECYCLE_TEARDOWN_IDLE,
  LIFECYCLE_TEARDOWN_ADMISSION_STOPPED,
  LIFECYCLE_TEARDOWN_WORKER_SIGNALED,
  LIFECYCLE_TEARDOWN_WAITING,
  LIFECYCLE_TEARDOWN_WORKER_QUIESCED,
  LIFECYCLE_TEARDOWN_RESOURCES_CLOSED,
  LIFECYCLE_TEARDOWN_MODULES_UNLOADED,
  LIFECYCLE_TEARDOWN_WORKER_STOPPED,
  LIFECYCLE_TEARDOWN_COMPLETE,
  LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED
} LifecycleTeardownPhase;

typedef struct
{
  volatile uint32_t cancellation_token;
  volatile uint32_t phase;
  uint64_t deadline_us;
} LifecycleTeardown;

typedef enum
{
  LIFECYCLE_TEARDOWN_POLL_PENDING,
  LIFECYCLE_TEARDOWN_POLL_WORKER_STOPPED,
  LIFECYCLE_TEARDOWN_POLL_DEADLINE_EXPIRED
} LifecycleTeardownPoll;

void lifecycle_teardown_init(LifecycleTeardown *teardown);
uint32_t lifecycle_teardown_begin(LifecycleTeardown *teardown,
 uint64_t now_us, uint64_t timeout_us);
int lifecycle_teardown_cancelled(const LifecycleTeardown *teardown);
int lifecycle_teardown_advance(LifecycleTeardown *teardown,
 LifecycleTeardownPhase phase);
uint32_t lifecycle_teardown_remaining_us(const LifecycleTeardown *teardown,
 uint64_t now_us);
LifecycleTeardownPoll lifecycle_teardown_poll(LifecycleTeardown *teardown,
 uint64_t now_us, int worker_stopped);
void lifecycle_teardown_expire(LifecycleTeardown *teardown);
LifecycleTeardownPhase lifecycle_teardown_phase(
 const LifecycleTeardown *teardown);

#endif /* LIFECYCLE_TEARDOWN_H */
