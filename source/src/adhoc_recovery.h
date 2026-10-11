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

#ifndef ADHOC_RECOVERY_H
#define ADHOC_RECOVERY_H

#include <stdint.h>

typedef enum
{
  ADHOC_RECOVERY_HEALTHY,
  ADHOC_RECOVERY_PROBING,
  ADHOC_RECOVERY_TRANSPORT_RESYNC,
  ADHOC_RECOVERY_GROUP_REBUILD,
  ADHOC_RECOVERY_STACK_RESTART,
  ADHOC_RECOVERY_FAILED
} AdhocRecoveryState;

typedef enum
{
  ADHOC_RECOVERY_ACTION_NONE = 0,
  ADHOC_RECOVERY_ACTION_PROBE = 1 << 0,
  ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT = 1 << 1,
  ADHOC_RECOVERY_ACTION_REBUILD_GROUP = 1 << 2,
  ADHOC_RECOVERY_ACTION_RESTART_STACK = 1 << 3,
  ADHOC_RECOVERY_ACTION_FAIL = 1 << 4
} AdhocRecoveryAction;

typedef struct
{
  uint64_t idle_timeout_us;
  uint64_t probe_timeout_us;
  uint64_t transport_timeout_us;
  uint64_t group_timeout_us;
  uint64_t stack_timeout_us;
  uint64_t platform_retry_us;
} AdhocRecoveryConfig;

typedef struct
{
  AdhocRecoveryConfig config;
  AdhocRecoveryState state;
  uint64_t last_peer_us;
  uint64_t state_started_us;
  uint64_t next_platform_us;
  uint32_t platform_attempts;
  uint32_t total_transport_resyncs;
  uint32_t total_group_rebuilds;
  uint32_t total_stack_restarts;
  uint8_t requested;
} AdhocRecovery;

void adhoc_recovery_init(AdhocRecovery *recovery, uint64_t now_us,
 const AdhocRecoveryConfig *config);
void adhoc_recovery_note_peer(AdhocRecovery *recovery, uint64_t now_us);
void adhoc_recovery_probe(AdhocRecovery *recovery, uint64_t now_us);
void adhoc_recovery_request(AdhocRecovery *recovery);
unsigned int adhoc_recovery_poll(AdhocRecovery *recovery,
 uint64_t now_us);
void adhoc_recovery_probe_sent(AdhocRecovery *recovery,
 uint64_t now_us);
void adhoc_recovery_platform_result(AdhocRecovery *recovery,
 uint64_t now_us, int succeeded);
int adhoc_recovery_active(const AdhocRecovery *recovery);

#endif /* ADHOC_RECOVERY_H */
