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

#include "lifecycle_teardown.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static void lifecycle_teardown_barrier(void)
{
#ifdef __PSP__
  __builtin_allegrex_sync();
#else
  __sync_synchronize();
#endif
}

void lifecycle_teardown_init(LifecycleTeardown *teardown)
{
  if(teardown == NULL)
    return;
  memset(teardown, 0, sizeof(*teardown));
}

uint32_t lifecycle_teardown_begin(LifecycleTeardown *teardown,
 uint64_t now_us, uint64_t timeout_us)
{
  uint64_t proposed_deadline;
  uint32_t token;

  if(teardown == NULL)
    return 0;

  proposed_deadline = UINT64_MAX - now_us < timeout_us ? UINT64_MAX :
   now_us + timeout_us;
  lifecycle_teardown_barrier();
  if(teardown->phase != LIFECYCLE_TEARDOWN_IDLE)
  {
    if(proposed_deadline < teardown->deadline_us)
    {
      teardown->deadline_us = proposed_deadline;
      lifecycle_teardown_barrier();
    }
    return teardown->cancellation_token;
  }

  token = teardown->cancellation_token + 1u;
  if(token == 0)
    token = 1u;
  teardown->deadline_us = proposed_deadline;
  teardown->phase = LIFECYCLE_TEARDOWN_ADMISSION_STOPPED;
  lifecycle_teardown_barrier();
  teardown->cancellation_token = token;
  lifecycle_teardown_barrier();
  return token;
}

int lifecycle_teardown_cancelled(const LifecycleTeardown *teardown)
{
  if(teardown == NULL)
    return 0;
  lifecycle_teardown_barrier();
  return teardown->cancellation_token != 0;
}

int lifecycle_teardown_advance(LifecycleTeardown *teardown,
 LifecycleTeardownPhase phase)
{
  uint32_t current;

  if(teardown == NULL || phase <= LIFECYCLE_TEARDOWN_IDLE ||
     phase >= LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED)
    return 0;

  for(;;)
  {
    lifecycle_teardown_barrier();
    current = teardown->phase;
    if(current == LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED ||
       current == LIFECYCLE_TEARDOWN_COMPLETE || current >= (uint32_t)phase)
      return current == (uint32_t)phase;
    if(__sync_bool_compare_and_swap(&teardown->phase, current,
       (uint32_t)phase))
    {
      lifecycle_teardown_barrier();
      return 1;
    }
  }
}

uint32_t lifecycle_teardown_remaining_us(const LifecycleTeardown *teardown,
 uint64_t now_us)
{
  uint64_t remaining;

  if(teardown == NULL)
    return 0;
  lifecycle_teardown_barrier();
  if(teardown->phase == LIFECYCLE_TEARDOWN_IDLE ||
     teardown->phase == LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED ||
     now_us >= teardown->deadline_us)
    return 0;
  remaining = teardown->deadline_us - now_us;
  return remaining > UINT_MAX ? UINT_MAX : (uint32_t)remaining;
}

LifecycleTeardownPoll lifecycle_teardown_poll(LifecycleTeardown *teardown,
 uint64_t now_us, int worker_stopped)
{
  if(worker_stopped || lifecycle_teardown_phase(teardown) ==
     LIFECYCLE_TEARDOWN_COMPLETE)
    return LIFECYCLE_TEARDOWN_POLL_WORKER_STOPPED;
  if(lifecycle_teardown_remaining_us(teardown, now_us) != 0)
    return LIFECYCLE_TEARDOWN_POLL_PENDING;
  lifecycle_teardown_expire(teardown);
  return LIFECYCLE_TEARDOWN_POLL_DEADLINE_EXPIRED;
}

void lifecycle_teardown_expire(LifecycleTeardown *teardown)
{
  uint32_t current;

  if(teardown == NULL)
    return;
  do
  {
    lifecycle_teardown_barrier();
    current = teardown->phase;
    if(current == LIFECYCLE_TEARDOWN_COMPLETE ||
       current == LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED)
      return;
  }
  while(!__sync_bool_compare_and_swap(&teardown->phase, current,
   LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED));
  lifecycle_teardown_barrier();
}

LifecycleTeardownPhase lifecycle_teardown_phase(
 const LifecycleTeardown *teardown)
{
  if(teardown == NULL)
    return LIFECYCLE_TEARDOWN_IDLE;
  lifecycle_teardown_barrier();
  return (LifecycleTeardownPhase)teardown->phase;
}
