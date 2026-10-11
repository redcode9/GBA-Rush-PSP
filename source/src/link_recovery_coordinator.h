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

#ifndef LINK_RECOVERY_COORDINATOR_H
#define LINK_RECOVERY_COORDINATOR_H

#include <stdint.h>

#include "link_journal.h"

typedef enum
{
  LINK_RECOVERY_NORMAL,
  LINK_RECOVERY_QUIESCENT,
  LINK_RECOVERY_EXCHANGING,
  LINK_RECOVERY_RECONCILING,
  LINK_RECOVERY_CLOSED
} LinkRecoveryState;

typedef enum
{
  LINK_RECOVERY_PLAN_NONE,
  LINK_RECOVERY_PLAN_EMPTY,
  LINK_RECOVERY_PLAN_DECIDED,
  LINK_RECOVERY_PLAN_RELEASED,
  LINK_RECOVERY_PLAN_CHECKPOINT,
  LINK_RECOVERY_PLAN_CLOSE
} LinkRecoveryPlanKind;

typedef enum
{
  LINK_RECOVERY_CLOSE_NONE,
  LINK_RECOVERY_CLOSE_ARGUMENT,
  LINK_RECOVERY_CLOSE_IDENTITY,
  LINK_RECOVERY_CLOSE_CHECKPOINT,
  LINK_RECOVERY_CLOSE_SEQUENCE,
  LINK_RECOVERY_CLOSE_HASH,
  LINK_RECOVERY_CLOSE_STAGE,
  LINK_RECOVERY_CLOSE_APPLY
} LinkRecoveryCloseReason;

typedef struct
{
  LinkJournalRole role;
  LinkJournalStage stage;
  LinkJournalRecord record;
  uint8_t applied;
  uint8_t irq_delivered;
} LinkRecoverySnapshot;

typedef struct
{
  LinkRecoveryPlanKind kind;
  LinkRecoveryCloseReason close_reason;
  LinkJournalRecord record;
  uint32_t sequence_floor;
  uint32_t target_epoch;
} LinkRecoveryPlan;

typedef struct
{
  LinkRecoveryState state;
  LinkRecoveryCloseReason close_reason;
  uint32_t session_nonce;
  uint32_t current_epoch;
  uint32_t target_epoch;
  uint32_t round;
  LinkRecoverySnapshot host;
  LinkRecoverySnapshot joiner;
  uint8_t host_valid;
  uint8_t joiner_valid;
} LinkRecoveryCoordinator;

void link_recovery_coordinator_init(LinkRecoveryCoordinator *coordinator,
 uint32_t session_nonce, uint32_t epoch);
int link_recovery_begin(LinkRecoveryCoordinator *coordinator);
int link_recovery_note_snapshot(LinkRecoveryCoordinator *coordinator,
 const LinkRecoverySnapshot *snapshot);
int link_recovery_make_plan(LinkRecoveryCoordinator *coordinator,
 LinkRecoveryPlan *plan);
int link_recovery_apply_plan(LinkJournal *journal,
 const LinkRecoveryPlan *plan);
int link_recovery_complete(LinkRecoveryCoordinator *coordinator,
 const LinkRecoveryPlan *plan);
void link_recovery_snapshot_journal(const LinkJournal *journal,
 LinkRecoverySnapshot *snapshot);

#endif /* LINK_RECOVERY_COORDINATOR_H */
