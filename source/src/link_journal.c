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

#include "link_journal.h"

#include <string.h>

#define LINK_JOURNAL_BUILD_ROW(name, source, target, role, trigger, flags) \
  {name, source, target, role, trigger, flags},

static const LinkJournalTransition link_journal_transitions[] =
{
  LINK_JOURNAL_TRANSITION_ROWS(LINK_JOURNAL_BUILD_ROW)
};

#undef LINK_JOURNAL_BUILD_ROW

typedef char LinkJournalTransitionCountMatches[
 (sizeof(link_journal_transitions) / sizeof(link_journal_transitions[0]) ==
  LINK_JOURNAL_TRANSITION_ID_COUNT) ? 1 : -1];

static int link_journal_fail(LinkJournal *journal, LinkJournalError error)
{
  if(journal != NULL)
    journal->last_error = error;
  return 0;
}

int link_journal_serial_after(uint32_t value, uint32_t reference)
{
  uint32_t difference = value - reference;

  return difference != 0 && difference < 0x80000000u;
}

const LinkJournalTransition *link_journal_transition(
 LinkJournalTransitionId transition_id)
{
  if(transition_id < 0 ||
     transition_id >= LINK_JOURNAL_TRANSITION_ID_COUNT)
    return NULL;
  return &link_journal_transitions[transition_id];
}

void link_journal_init(LinkJournal *journal, LinkJournalRole role,
 uint32_t session_nonce, uint32_t epoch, uint32_t checkpoint,
 uint32_t checkpoint_hash)
{
  if(journal == NULL)
    return;
  memset(journal, 0, sizeof(*journal));
  journal->role = role;
  journal->stage = LINK_JOURNAL_EMPTY;
  journal->session_nonce = session_nonce;
  journal->epoch = epoch;
  journal->checkpoint = checkpoint;
  journal->checkpoint_hash = checkpoint_hash;
}

int link_journal_set_epoch(LinkJournal *journal, uint32_t epoch)
{
  if(journal == NULL || journal->session_nonce == 0 || epoch == 0)
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_ARGUMENT);
  if(journal->stage != LINK_JOURNAL_EMPTY || journal->active_sequence != 0)
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_TRANSITION);
  journal->epoch = epoch;
  journal->last_error = LINK_JOURNAL_ERROR_NONE;
  return 1;
}

int link_journal_recover_epoch(LinkJournal *journal, uint32_t epoch)
{
  if(journal == NULL || journal->session_nonce == 0 || epoch == 0)
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_ARGUMENT);
  if(!link_journal_serial_after(epoch, journal->epoch))
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_IDENTITY);
  journal->epoch = epoch;
  journal->last_error = LINK_JOURNAL_ERROR_NONE;
  return 1;
}

int link_journal_local_cancel_permitted(const LinkJournal *journal)
{
  if(journal == NULL)
    return 0;
  if(journal->stage == LINK_JOURNAL_EMPTY)
    return 1;
  return journal->role == LINK_JOURNAL_ROLE_HOST &&
   journal->stage == LINK_JOURNAL_ARMED;
}

static int link_journal_identity_matches(const LinkJournal *journal,
 const LinkJournalRecord *record)
{
  return record->session_nonce == journal->session_nonce &&
   record->epoch == journal->epoch;
}

static int link_journal_checkpoint_matches(const LinkJournal *journal,
 const LinkJournalRecord *record)
{
  return record->checkpoint == journal->checkpoint &&
   record->checkpoint_hash == journal->checkpoint_hash;
}

static int link_journal_active_matches(const LinkJournal *journal,
 const LinkJournalRecord *record)
{
  return record->sequence == journal->active_sequence &&
   record->start_cycle == journal->start_cycle &&
   record->finish_cycle == journal->finish_cycle;
}

static void link_journal_clear_active(LinkJournal *journal)
{
  journal->active_sequence = 0;
  journal->transfer_hash = 0;
  journal->start_cycle = 0;
  journal->finish_cycle = 0;
  journal->release_shared = 0;
  journal->applied = 0;
  journal->irq_delivered = 0;
}

int link_journal_apply(LinkJournal *journal,
 LinkJournalTransitionId transition_id, const LinkJournalRecord *record)
{
  const LinkJournalTransition *transition =
   link_journal_transition(transition_id);

  if(journal == NULL || record == NULL || transition == NULL ||
     journal->session_nonce == 0 || journal->epoch == 0)
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_ARGUMENT);
  if(transition->source != journal->stage)
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_TRANSITION);
  if(transition->role != LINK_JOURNAL_ROLE_EITHER &&
     transition->role != journal->role)
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_ROLE);
  if(!link_journal_identity_matches(journal, record))
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_IDENTITY);
  if(!link_journal_checkpoint_matches(journal, record))
    return link_journal_fail(journal, LINK_JOURNAL_ERROR_CHECKPOINT);

  if(transition_id == LINK_JOURNAL_TRANSITION_ARM)
  {
    if(!link_journal_serial_after(record->sequence, journal->checkpoint))
      return link_journal_fail(journal, LINK_JOURNAL_ERROR_SEQUENCE);
    if(record->hash != 0 ||
       (record->finish_cycle != record->start_cycle &&
        record->finish_cycle - record->start_cycle >= 0x80000000u))
      return link_journal_fail(journal, LINK_JOURNAL_ERROR_CYCLE);
    journal->active_sequence = record->sequence;
    journal->transfer_hash = 0;
    journal->start_cycle = record->start_cycle;
    journal->finish_cycle = record->finish_cycle;
    journal->release_shared = 0;
    journal->applied = 0;
    journal->irq_delivered = 0;
  }
  else if(transition_id == LINK_JOURNAL_TRANSITION_RECOVER_EMPTY)
  {
    if(record->sequence != journal->checkpoint ||
       record->hash != journal->checkpoint_hash)
      return link_journal_fail(journal, LINK_JOURNAL_ERROR_CHECKPOINT);
  }
  else
  {
    if(!link_journal_active_matches(journal, record))
      return link_journal_fail(journal, LINK_JOURNAL_ERROR_SEQUENCE);
    if(transition->target == LINK_JOURNAL_DECIDED ||
       journal->stage == LINK_JOURNAL_DECIDED ||
       journal->stage == LINK_JOURNAL_RELEASED)
    {
      if(record->hash == 0)
        return link_journal_fail(journal, LINK_JOURNAL_ERROR_HASH);
      if(journal->transfer_hash != 0 &&
         record->hash != journal->transfer_hash)
        return link_journal_fail(journal, LINK_JOURNAL_ERROR_HASH);
    }
  }

  if(transition->target == LINK_JOURNAL_DECIDED)
    journal->transfer_hash = record->hash;
  if(transition->target == LINK_JOURNAL_RELEASED)
  {
    journal->transfer_hash = record->hash;
    if(transition_id == LINK_JOURNAL_TRANSITION_RECORD_RELEASE ||
       transition_id == LINK_JOURNAL_TRANSITION_RECOVER_RELEASE ||
       record->release_shared)
      journal->release_shared = 1;
  }
  if(transition_id == LINK_JOURNAL_TRANSITION_REPLAY_RELEASE &&
     record->release_shared)
    journal->release_shared = 1;

  if(transition->target == LINK_JOURNAL_EMPTY &&
     transition->source == LINK_JOURNAL_ARMED)
  {
    if(record->hash != 0)
      return link_journal_fail(journal, LINK_JOURNAL_ERROR_HASH);
    link_journal_clear_active(journal);
  }
  else if(transition->target == LINK_JOURNAL_EMPTY &&
          transition->source == LINK_JOURNAL_RELEASED)
  {
    if(!journal->release_shared || !journal->applied ||
       !journal->irq_delivered || record->sequence != journal->active_sequence ||
       record->hash != journal->transfer_hash)
      return link_journal_fail(journal, LINK_JOURNAL_ERROR_EFFECT);
    journal->checkpoint = journal->active_sequence;
    journal->checkpoint_hash = journal->transfer_hash;
    link_journal_clear_active(journal);
  }

  journal->stage = transition->target;
  journal->last_error = LINK_JOURNAL_ERROR_NONE;
  return 1;
}

LinkJournalEffectResult link_journal_claim_effect(LinkJournal *journal,
 const LinkJournalRecord *record)
{
  if(journal == NULL || record == NULL ||
     journal->stage != LINK_JOURNAL_RELEASED ||
     !link_journal_identity_matches(journal, record) ||
     !link_journal_checkpoint_matches(journal, record) ||
     !link_journal_active_matches(journal, record) ||
     record->hash != journal->transfer_hash)
  {
    link_journal_fail(journal, LINK_JOURNAL_ERROR_EFFECT);
    return LINK_JOURNAL_EFFECT_ERROR;
  }
  if(journal->applied || journal->irq_delivered)
  {
    if(journal->applied && journal->irq_delivered &&
       journal->applied_sequence == record->sequence &&
       journal->irq_sequence == record->sequence)
      return LINK_JOURNAL_EFFECT_ALREADY_APPLIED;
    link_journal_fail(journal, LINK_JOURNAL_ERROR_EFFECT);
    return LINK_JOURNAL_EFFECT_ERROR;
  }
  journal->applied = 1;
  journal->irq_delivered = 1;
  journal->applied_sequence = record->sequence;
  journal->irq_sequence = record->sequence;
  journal->last_error = LINK_JOURNAL_ERROR_NONE;
  return LINK_JOURNAL_EFFECT_FIRST;
}

void link_journal_snapshot(const LinkJournal *journal,
 LinkJournalRecord *record)
{
  if(record == NULL)
    return;
  memset(record, 0, sizeof(*record));
  if(journal == NULL)
    return;
  record->session_nonce = journal->session_nonce;
  record->epoch = journal->epoch;
  record->sequence = journal->stage == LINK_JOURNAL_EMPTY ?
   journal->checkpoint : journal->active_sequence;
  record->hash = journal->stage == LINK_JOURNAL_EMPTY ?
   journal->checkpoint_hash : journal->transfer_hash;
  record->checkpoint = journal->checkpoint;
  record->checkpoint_hash = journal->checkpoint_hash;
  record->start_cycle = journal->start_cycle;
  record->finish_cycle = journal->finish_cycle;
  record->release_shared = journal->release_shared;
}
