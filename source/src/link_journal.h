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

#ifndef LINK_JOURNAL_H
#define LINK_JOURNAL_H

#include <stdint.h>

#include "link_journal_transitions.h"

typedef enum
{
  LINK_JOURNAL_ERROR_NONE,
  LINK_JOURNAL_ERROR_ARGUMENT,
  LINK_JOURNAL_ERROR_TRANSITION,
  LINK_JOURNAL_ERROR_ROLE,
  LINK_JOURNAL_ERROR_IDENTITY,
  LINK_JOURNAL_ERROR_SEQUENCE,
  LINK_JOURNAL_ERROR_HASH,
  LINK_JOURNAL_ERROR_CYCLE,
  LINK_JOURNAL_ERROR_CHECKPOINT,
  LINK_JOURNAL_ERROR_EFFECT
} LinkJournalError;

typedef enum
{
  LINK_JOURNAL_EFFECT_ERROR,
  LINK_JOURNAL_EFFECT_FIRST,
  LINK_JOURNAL_EFFECT_ALREADY_APPLIED
} LinkJournalEffectResult;

typedef struct
{
  uint32_t session_nonce;
  uint32_t epoch;
  uint32_t sequence;
  uint32_t hash;
  uint32_t checkpoint;
  uint32_t checkpoint_hash;
  uint32_t start_cycle;
  uint32_t finish_cycle;
  uint8_t release_shared;
} LinkJournalRecord;

typedef struct
{
  LinkJournalRole role;
  LinkJournalStage stage;
  uint32_t session_nonce;
  uint32_t epoch;
  uint32_t active_sequence;
  uint32_t transfer_hash;
  uint32_t checkpoint;
  uint32_t checkpoint_hash;
  uint32_t start_cycle;
  uint32_t finish_cycle;
  uint32_t applied_sequence;
  uint32_t irq_sequence;
  LinkJournalError last_error;
  uint8_t release_shared;
  uint8_t applied;
  uint8_t irq_delivered;
} LinkJournal;

int link_journal_serial_after(uint32_t value, uint32_t reference);
void link_journal_init(LinkJournal *journal, LinkJournalRole role,
 uint32_t session_nonce, uint32_t epoch, uint32_t checkpoint,
 uint32_t checkpoint_hash);
int link_journal_set_epoch(LinkJournal *journal, uint32_t epoch);
int link_journal_recover_epoch(LinkJournal *journal, uint32_t epoch);
int link_journal_local_cancel_permitted(const LinkJournal *journal);
int link_journal_apply(LinkJournal *journal,
 LinkJournalTransitionId transition_id, const LinkJournalRecord *record);
LinkJournalEffectResult link_journal_claim_effect(LinkJournal *journal,
 const LinkJournalRecord *record);
void link_journal_snapshot(const LinkJournal *journal,
 LinkJournalRecord *record);
const LinkJournalTransition *link_journal_transition(
 LinkJournalTransitionId transition_id);

#endif /* LINK_JOURNAL_H */
