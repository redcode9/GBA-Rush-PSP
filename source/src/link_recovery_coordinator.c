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

#include "link_recovery_coordinator.h"

#include <string.h>

static int link_recovery_close(LinkRecoveryCoordinator *coordinator,
 LinkRecoveryPlan *plan, LinkRecoveryCloseReason reason)
{
  coordinator->state = LINK_RECOVERY_CLOSED;
  coordinator->close_reason = reason;
  memset(plan, 0, sizeof(*plan));
  plan->kind = LINK_RECOVERY_PLAN_CLOSE;
  plan->close_reason = reason;
  plan->target_epoch = coordinator->target_epoch;
  return 1;
}

static int link_recovery_fail(LinkRecoveryCoordinator *coordinator,
 LinkRecoveryCloseReason reason)
{
  coordinator->state = LINK_RECOVERY_CLOSED;
  coordinator->close_reason = reason;
  return 0;
}

static int link_recovery_snapshot_equal(const LinkRecoverySnapshot *left,
 const LinkRecoverySnapshot *right)
{
  const LinkJournalRecord *a = &left->record;
  const LinkJournalRecord *b = &right->record;

  return left->role == right->role && left->stage == right->stage &&
   left->applied == right->applied &&
   left->irq_delivered == right->irq_delivered &&
   a->session_nonce == b->session_nonce && a->epoch == b->epoch &&
   a->sequence == b->sequence && a->hash == b->hash &&
   a->checkpoint == b->checkpoint &&
   a->checkpoint_hash == b->checkpoint_hash &&
   a->start_cycle == b->start_cycle &&
   a->finish_cycle == b->finish_cycle &&
   a->release_shared == b->release_shared;
}

static int link_recovery_snapshot_valid(const LinkRecoverySnapshot *snapshot)
{
  const LinkJournalRecord *record = &snapshot->record;

  if(snapshot->stage < LINK_JOURNAL_EMPTY ||
     snapshot->stage > LINK_JOURNAL_RELEASED ||
     record->session_nonce == 0 || record->epoch == 0 ||
     (snapshot->stage != LINK_JOURNAL_EMPTY &&
      record->finish_cycle != record->start_cycle &&
      record->finish_cycle - record->start_cycle >= 0x80000000u) ||
     snapshot->applied != snapshot->irq_delivered)
    return 0;
  if(snapshot->stage == LINK_JOURNAL_EMPTY)
    return record->sequence == record->checkpoint &&
     record->hash == record->checkpoint_hash && record->start_cycle == 0 &&
     record->finish_cycle == 0 && !record->release_shared &&
     !snapshot->applied;
  if(!link_journal_serial_after(record->sequence, record->checkpoint))
    return 0;
  if(snapshot->stage == LINK_JOURNAL_ARMED)
    return record->hash == 0 && !record->release_shared &&
     !snapshot->applied;
  if(record->hash == 0)
    return 0;
  if(snapshot->stage == LINK_JOURNAL_DECIDED)
    return !record->release_shared && !snapshot->applied;
  return 1;
}

void link_recovery_coordinator_init(LinkRecoveryCoordinator *coordinator,
 uint32_t session_nonce, uint32_t epoch)
{
  if(coordinator == NULL)
    return;
  memset(coordinator, 0, sizeof(*coordinator));
  coordinator->state = LINK_RECOVERY_NORMAL;
  coordinator->session_nonce = session_nonce;
  coordinator->current_epoch = epoch;
  coordinator->target_epoch = epoch;
}

int link_recovery_begin(LinkRecoveryCoordinator *coordinator)
{
  if(coordinator == NULL || coordinator->state != LINK_RECOVERY_NORMAL ||
     coordinator->session_nonce == 0 || coordinator->current_epoch == 0)
    return 0;
  coordinator->state = LINK_RECOVERY_QUIESCENT;
  coordinator->target_epoch = coordinator->current_epoch + 1u;
  if(coordinator->target_epoch == 0)
    coordinator->target_epoch = 1u;
  coordinator->round++;
  if(coordinator->round == 0)
    coordinator->round = 1u;
  coordinator->host_valid = 0;
  coordinator->joiner_valid = 0;
  coordinator->close_reason = LINK_RECOVERY_CLOSE_NONE;
  return 1;
}

int link_recovery_note_snapshot(LinkRecoveryCoordinator *coordinator,
 const LinkRecoverySnapshot *snapshot)
{
  if(coordinator == NULL || snapshot == NULL ||
     (coordinator->state != LINK_RECOVERY_QUIESCENT &&
      coordinator->state != LINK_RECOVERY_EXCHANGING) ||
     (snapshot->role != LINK_JOURNAL_ROLE_HOST &&
      snapshot->role != LINK_JOURNAL_ROLE_JOINER))
    return 0;
  if(snapshot->record.session_nonce != coordinator->session_nonce ||
     snapshot->record.epoch != coordinator->current_epoch)
    return link_recovery_fail(coordinator, LINK_RECOVERY_CLOSE_IDENTITY);
  if(!link_recovery_snapshot_valid(snapshot))
    return link_recovery_fail(coordinator, LINK_RECOVERY_CLOSE_ARGUMENT);
  if(snapshot->role == LINK_JOURNAL_ROLE_HOST)
  {
    if(coordinator->host_valid &&
       !link_recovery_snapshot_equal(&coordinator->host, snapshot))
      return link_recovery_fail(coordinator, LINK_RECOVERY_CLOSE_APPLY);
    coordinator->host = *snapshot;
    coordinator->host_valid = 1;
  }
  else
  {
    if(coordinator->joiner_valid &&
       !link_recovery_snapshot_equal(&coordinator->joiner, snapshot))
      return link_recovery_fail(coordinator, LINK_RECOVERY_CLOSE_APPLY);
    coordinator->joiner = *snapshot;
    coordinator->joiner_valid = 1;
  }
  coordinator->state = LINK_RECOVERY_EXCHANGING;
  return 1;
}

static int link_recovery_same_checkpoint(const LinkRecoverySnapshot *left,
 const LinkRecoverySnapshot *right)
{
  return left->record.checkpoint == right->record.checkpoint &&
   left->record.checkpoint_hash == right->record.checkpoint_hash;
}

static int link_recovery_active_stage(LinkJournalStage stage)
{
  return stage == LINK_JOURNAL_ARMED || stage == LINK_JOURNAL_DECIDED ||
   stage == LINK_JOURNAL_RELEASED;
}

static int link_recovery_same_active(const LinkRecoverySnapshot *left,
 const LinkRecoverySnapshot *right)
{
  return left->record.sequence == right->record.sequence &&
   left->record.start_cycle == right->record.start_cycle &&
   left->record.finish_cycle == right->record.finish_cycle;
}

static int link_recovery_checkpoint_covers(
 const LinkRecoverySnapshot *empty, const LinkRecoverySnapshot *released)
{
  return empty->stage == LINK_JOURNAL_EMPTY &&
   released->stage == LINK_JOURNAL_RELEASED &&
   empty->record.checkpoint == released->record.sequence &&
   empty->record.checkpoint_hash == released->record.hash &&
   released->applied && released->irq_delivered;
}

int link_recovery_make_plan(LinkRecoveryCoordinator *coordinator,
 LinkRecoveryPlan *plan)
{
  const LinkRecoverySnapshot *host;
  const LinkRecoverySnapshot *joiner;
  const LinkRecoverySnapshot *authority;

  if(coordinator == NULL || plan == NULL ||
     coordinator->state != LINK_RECOVERY_EXCHANGING ||
     !coordinator->host_valid || !coordinator->joiner_valid)
    return 0;
  memset(plan, 0, sizeof(*plan));
  plan->target_epoch = coordinator->target_epoch;
  host = &coordinator->host;
  joiner = &coordinator->joiner;
  if(host->record.session_nonce != joiner->record.session_nonce ||
     host->record.epoch != joiner->record.epoch)
    return link_recovery_close(coordinator, plan,
     LINK_RECOVERY_CLOSE_IDENTITY);

  if(link_recovery_checkpoint_covers(host, joiner))
    authority = host;
  else if(link_recovery_checkpoint_covers(joiner, host))
    authority = joiner;
  else
    authority = NULL;
  if(authority != NULL)
  {
    plan->kind = LINK_RECOVERY_PLAN_CHECKPOINT;
    plan->record = authority->record;
    plan->sequence_floor = authority->record.checkpoint;
    coordinator->state = LINK_RECOVERY_RECONCILING;
    return 1;
  }
  if(!link_recovery_same_checkpoint(host, joiner))
    return link_recovery_close(coordinator, plan,
     LINK_RECOVERY_CLOSE_CHECKPOINT);

  if((host->stage == LINK_JOURNAL_EMPTY ||
      host->stage == LINK_JOURNAL_ARMED) &&
     (joiner->stage == LINK_JOURNAL_EMPTY ||
      joiner->stage == LINK_JOURNAL_ARMED))
  {
    if(link_recovery_active_stage(host->stage) &&
       link_recovery_active_stage(joiner->stage) &&
       !link_recovery_same_active(host, joiner))
      return link_recovery_close(coordinator, plan,
       LINK_RECOVERY_CLOSE_SEQUENCE);
    plan->kind = LINK_RECOVERY_PLAN_EMPTY;
    plan->record = host->record;
    plan->sequence_floor = host->record.checkpoint;
    if(host->stage == LINK_JOURNAL_ARMED)
      plan->sequence_floor = host->record.sequence;
    else if(joiner->stage == LINK_JOURNAL_ARMED)
      plan->sequence_floor = joiner->record.sequence;
    plan->record.sequence = host->record.checkpoint;
    plan->record.hash = host->record.checkpoint_hash;
    plan->record.start_cycle = 0;
    plan->record.finish_cycle = 0;
    coordinator->state = LINK_RECOVERY_RECONCILING;
    return 1;
  }

  if(host->stage == LINK_JOURNAL_ARMED ||
     host->stage == LINK_JOURNAL_EMPTY)
    return link_recovery_close(coordinator, plan, LINK_RECOVERY_CLOSE_STAGE);
  if(!link_recovery_active_stage(joiner->stage) ||
     !link_recovery_same_active(host, joiner))
    return link_recovery_close(coordinator, plan,
     LINK_RECOVERY_CLOSE_SEQUENCE);
  if(host->record.hash == 0 ||
     ((joiner->stage == LINK_JOURNAL_DECIDED ||
       joiner->stage == LINK_JOURNAL_RELEASED) &&
      joiner->record.hash != host->record.hash))
    return link_recovery_close(coordinator, plan, LINK_RECOVERY_CLOSE_HASH);

  plan->record = host->record;
  plan->sequence_floor = host->record.sequence;
  plan->record.release_shared =
   host->stage == LINK_JOURNAL_RELEASED ||
   joiner->stage == LINK_JOURNAL_RELEASED;
  plan->kind = plan->record.release_shared ?
   LINK_RECOVERY_PLAN_RELEASED : LINK_RECOVERY_PLAN_DECIDED;
  coordinator->state = LINK_RECOVERY_RECONCILING;
  return 1;
}

static void link_recovery_local_record(const LinkJournal *journal,
 const LinkRecoveryPlan *plan, LinkJournalRecord *record)
{
  *record = plan->record;
  record->session_nonce = journal->session_nonce;
  record->epoch = journal->epoch;
  record->checkpoint = journal->checkpoint;
  record->checkpoint_hash = journal->checkpoint_hash;
}

int link_recovery_apply_plan(LinkJournal *journal,
 const LinkRecoveryPlan *plan)
{
  LinkJournalRecord record;

  if(journal == NULL || plan == NULL || plan->kind == LINK_RECOVERY_PLAN_NONE ||
     plan->kind == LINK_RECOVERY_PLAN_CLOSE || plan->target_epoch == 0)
    return 0;
  link_recovery_local_record(journal, plan, &record);
  if(plan->kind == LINK_RECOVERY_PLAN_EMPTY)
  {
    if(journal->stage == LINK_JOURNAL_ARMED)
    {
      record.sequence = journal->active_sequence;
      record.hash = 0;
      record.start_cycle = journal->start_cycle;
      record.finish_cycle = journal->finish_cycle;
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_RECOVER_CANCEL_ARM, &record))
        return 0;
    }
    else if(journal->stage == LINK_JOURNAL_EMPTY)
    {
      record.sequence = journal->checkpoint;
      record.hash = journal->checkpoint_hash;
      record.start_cycle = 0;
      record.finish_cycle = 0;
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_RECOVER_EMPTY, &record))
        return 0;
    }
    else
    {
      return 0;
    }
  }
  else if(plan->kind == LINK_RECOVERY_PLAN_DECIDED)
  {
    if(journal->stage == LINK_JOURNAL_ARMED)
    {
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_RECOVER_DECISION, &record))
        return 0;
    }
    else if(journal->stage == LINK_JOURNAL_DECIDED)
    {
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_REPLAY_DECISION, &record))
        return 0;
    }
    else
    {
      return 0;
    }
  }
  else if(plan->kind == LINK_RECOVERY_PLAN_RELEASED)
  {
    record.release_shared = 1;
    if(journal->stage == LINK_JOURNAL_ARMED)
    {
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_RECOVER_DECISION, &record))
        return 0;
    }
    if(journal->stage == LINK_JOURNAL_DECIDED)
    {
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_RECOVER_RELEASE, &record))
        return 0;
    }
    else if(journal->stage == LINK_JOURNAL_RELEASED)
    {
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_REPLAY_RELEASE, &record))
        return 0;
    }
    else
    {
      return 0;
    }
  }
  else if(plan->kind == LINK_RECOVERY_PLAN_CHECKPOINT)
  {
    if(journal->stage == LINK_JOURNAL_RELEASED)
    {
      record.sequence = journal->active_sequence;
      record.hash = journal->transfer_hash;
      record.start_cycle = journal->start_cycle;
      record.finish_cycle = journal->finish_cycle;
      record.release_shared = 1;
      if(!journal->release_shared &&
         !link_journal_apply(journal,
          LINK_JOURNAL_TRANSITION_REPLAY_RELEASE, &record))
        return 0;
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_RECOVER_RELEASED, &record))
        return 0;
    }
    else if(journal->stage == LINK_JOURNAL_EMPTY)
    {
      record.sequence = journal->checkpoint;
      record.hash = journal->checkpoint_hash;
      record.start_cycle = 0;
      record.finish_cycle = 0;
      if(!link_journal_apply(journal,
         LINK_JOURNAL_TRANSITION_RECOVER_EMPTY, &record))
        return 0;
    }
    else
    {
      return 0;
    }
  }
  return link_journal_recover_epoch(journal, plan->target_epoch);
}

int link_recovery_complete(LinkRecoveryCoordinator *coordinator,
 const LinkRecoveryPlan *plan)
{
  if(coordinator == NULL || plan == NULL ||
     coordinator->state != LINK_RECOVERY_RECONCILING ||
     plan->kind == LINK_RECOVERY_PLAN_NONE ||
     plan->kind == LINK_RECOVERY_PLAN_CLOSE ||
     plan->target_epoch != coordinator->target_epoch)
    return 0;
  coordinator->current_epoch = coordinator->target_epoch;
  coordinator->state = LINK_RECOVERY_NORMAL;
  coordinator->host_valid = 0;
  coordinator->joiner_valid = 0;
  return 1;
}

void link_recovery_snapshot_journal(const LinkJournal *journal,
 LinkRecoverySnapshot *snapshot)
{
  if(snapshot == NULL)
    return;
  memset(snapshot, 0, sizeof(*snapshot));
  if(journal == NULL)
    return;
  snapshot->role = journal->role;
  snapshot->stage = journal->stage;
  snapshot->applied = journal->applied;
  snapshot->irq_delivered = journal->irq_delivered;
  link_journal_snapshot(journal, &snapshot->record);
}
