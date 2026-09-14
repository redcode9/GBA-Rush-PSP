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

#include "adhoc_recovery.h"

#include <limits.h>
#include <string.h>

static uint64_t adhoc_recovery_add_time(uint64_t value, uint64_t amount)
{
  return UINT64_MAX - value < amount ? UINT64_MAX : value + amount;
}

static int adhoc_recovery_elapsed(uint64_t now_us, uint64_t started_us,
 uint64_t duration_us)
{
  return now_us >= started_us && now_us - started_us >= duration_us;
}

static void adhoc_recovery_increment(uint32_t *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

static void adhoc_recovery_enter(AdhocRecovery *recovery,
 AdhocRecoveryState state, uint64_t now_us)
{
  recovery->state = state;
  recovery->state_started_us = now_us;
  recovery->next_platform_us = now_us;
  recovery->platform_attempts = 0;
  recovery->requested = 0;
}

static void adhoc_recovery_complete(AdhocRecovery *recovery,
 uint64_t now_us)
{
  recovery->state = ADHOC_RECOVERY_HEALTHY;
  recovery->last_peer_us = now_us;
  recovery->state_started_us = 0;
  recovery->next_platform_us = 0;
  recovery->platform_attempts = 0;
  recovery->requested = 0;
}

void adhoc_recovery_init(AdhocRecovery *recovery, uint64_t now_us,
 const AdhocRecoveryConfig *config)
{
  if(recovery == NULL)
    return;
  memset(recovery, 0, sizeof(*recovery));
  if(config != NULL)
    recovery->config = *config;
  recovery->state = ADHOC_RECOVERY_HEALTHY;
  recovery->last_peer_us = now_us;
}

void adhoc_recovery_note_peer(AdhocRecovery *recovery, uint64_t now_us)
{
  if(recovery == NULL || now_us < recovery->last_peer_us)
    return;
  if(recovery->state == ADHOC_RECOVERY_PROBING)
    adhoc_recovery_complete(recovery, now_us);
  else
    recovery->last_peer_us = now_us;
}

void adhoc_recovery_probe(AdhocRecovery *recovery, uint64_t now_us)
{
  if(recovery == NULL || recovery->state != ADHOC_RECOVERY_HEALTHY)
    return;
  adhoc_recovery_enter(recovery, ADHOC_RECOVERY_PROBING, now_us);
}

void adhoc_recovery_request(AdhocRecovery *recovery)
{
  if(recovery != NULL && recovery->state != ADHOC_RECOVERY_FAILED)
    recovery->requested = 1;
}

static void adhoc_recovery_advance(AdhocRecovery *recovery,
 uint64_t now_us)
{
  if(recovery->requested &&
     recovery->state < ADHOC_RECOVERY_TRANSPORT_RESYNC)
  {
    adhoc_recovery_enter(recovery, ADHOC_RECOVERY_TRANSPORT_RESYNC,
     now_us);
    return;
  }

  switch(recovery->state)
  {
    case ADHOC_RECOVERY_HEALTHY:
      if(recovery->config.idle_timeout_us != 0 &&
         adhoc_recovery_elapsed(now_us, recovery->last_peer_us,
          recovery->config.idle_timeout_us))
        adhoc_recovery_enter(recovery, ADHOC_RECOVERY_PROBING, now_us);
      break;

    case ADHOC_RECOVERY_PROBING:
      if(adhoc_recovery_elapsed(now_us, recovery->state_started_us,
         recovery->config.probe_timeout_us))
      {
        adhoc_recovery_enter(recovery, ADHOC_RECOVERY_TRANSPORT_RESYNC,
         now_us);
      }
      break;

    case ADHOC_RECOVERY_TRANSPORT_RESYNC:
      if(adhoc_recovery_elapsed(now_us, recovery->state_started_us,
         recovery->config.transport_timeout_us))
        adhoc_recovery_enter(recovery, ADHOC_RECOVERY_GROUP_REBUILD,
         now_us);
      break;

    case ADHOC_RECOVERY_GROUP_REBUILD:
      if(adhoc_recovery_elapsed(now_us, recovery->state_started_us,
         recovery->config.group_timeout_us))
        adhoc_recovery_enter(recovery, ADHOC_RECOVERY_STACK_RESTART,
         now_us);
      break;

    case ADHOC_RECOVERY_STACK_RESTART:
      if(adhoc_recovery_elapsed(now_us, recovery->state_started_us,
         recovery->config.stack_timeout_us))
        recovery->state = ADHOC_RECOVERY_FAILED;
      break;

    case ADHOC_RECOVERY_FAILED:
      break;
  }
}

unsigned int adhoc_recovery_poll(AdhocRecovery *recovery,
 uint64_t now_us)
{
  if(recovery == NULL || recovery->state == ADHOC_RECOVERY_FAILED)
    return ADHOC_RECOVERY_ACTION_FAIL;

  adhoc_recovery_advance(recovery, now_us);
  if(recovery->state == ADHOC_RECOVERY_FAILED)
    return ADHOC_RECOVERY_ACTION_FAIL;
  if(recovery->state == ADHOC_RECOVERY_PROBING &&
     now_us >= recovery->next_platform_us)
    return ADHOC_RECOVERY_ACTION_PROBE;
  if(recovery->state < ADHOC_RECOVERY_TRANSPORT_RESYNC ||
     now_us < recovery->next_platform_us)
    return ADHOC_RECOVERY_ACTION_NONE;

  switch(recovery->state)
  {
    case ADHOC_RECOVERY_TRANSPORT_RESYNC:
      return ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT;
    case ADHOC_RECOVERY_GROUP_REBUILD:
      return ADHOC_RECOVERY_ACTION_REBUILD_GROUP;
    case ADHOC_RECOVERY_STACK_RESTART:
      return ADHOC_RECOVERY_ACTION_RESTART_STACK;
    default:
      return ADHOC_RECOVERY_ACTION_NONE;
  }
}

void adhoc_recovery_probe_sent(AdhocRecovery *recovery,
 uint64_t now_us)
{
  if(recovery == NULL || recovery->state != ADHOC_RECOVERY_PROBING)
    return;
  recovery->next_platform_us = adhoc_recovery_add_time(now_us,
   recovery->config.platform_retry_us);
}

void adhoc_recovery_platform_result(AdhocRecovery *recovery,
 uint64_t now_us, int succeeded)
{
  if(recovery == NULL ||
     recovery->state < ADHOC_RECOVERY_TRANSPORT_RESYNC ||
     recovery->state > ADHOC_RECOVERY_STACK_RESTART)
    return;

  adhoc_recovery_increment(&recovery->platform_attempts);
  if(!succeeded)
  {
    recovery->next_platform_us = adhoc_recovery_add_time(now_us,
     recovery->config.platform_retry_us);
    return;
  }

  if(recovery->state == ADHOC_RECOVERY_TRANSPORT_RESYNC)
    adhoc_recovery_increment(&recovery->total_transport_resyncs);
  else if(recovery->state == ADHOC_RECOVERY_GROUP_REBUILD)
    adhoc_recovery_increment(&recovery->total_group_rebuilds);
  else
    adhoc_recovery_increment(&recovery->total_stack_restarts);
  adhoc_recovery_complete(recovery, now_us);
}

int adhoc_recovery_active(const AdhocRecovery *recovery)
{
  return recovery != NULL &&
   recovery->state >= ADHOC_RECOVERY_PROBING &&
   recovery->state <= ADHOC_RECOVERY_STACK_RESTART;
}
