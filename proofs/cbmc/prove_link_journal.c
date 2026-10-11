#include <stdint.h>

#include "link_journal.h"

extern unsigned int nondet_uint(void);

static int journal_valid(const LinkJournal *journal)
{
  if(journal->role < LINK_JOURNAL_ROLE_HOST ||
     journal->role > LINK_JOURNAL_ROLE_JOINER ||
     journal->stage < LINK_JOURNAL_EMPTY ||
     journal->stage > LINK_JOURNAL_RELEASED ||
     journal->session_nonce == 0 || journal->epoch == 0)
    return 0;
  if(journal->stage == LINK_JOURNAL_EMPTY)
    return journal->active_sequence == 0 && journal->transfer_hash == 0 &&
     !journal->release_shared && !journal->applied &&
     !journal->irq_delivered;
  if(journal->stage == LINK_JOURNAL_ARMED)
    return journal->transfer_hash == 0 && !journal->release_shared &&
     !journal->applied && !journal->irq_delivered;
  if(journal->transfer_hash == 0)
    return 0;
  return journal->applied == journal->irq_delivered;
}

int main(void)
{
  LinkJournal before;
  LinkJournal journal;
  LinkJournalRecord record;
  LinkJournalTransitionId transition_id;
  const LinkJournalTransition *transition;
  int applied;
  unsigned int raw_role = nondet_uint();
  unsigned int raw_stage = nondet_uint();
  unsigned int raw_transition_id = nondet_uint();

  __CPROVER_assume(raw_role <= (unsigned int)LINK_JOURNAL_ROLE_JOINER);
  __CPROVER_assume(raw_stage <= (unsigned int)LINK_JOURNAL_RELEASED);
  __CPROVER_assume(raw_transition_id >=
   (unsigned int)LINK_JOURNAL_TRANSITION_ARM &&
   raw_transition_id < (unsigned int)LINK_JOURNAL_TRANSITION_ID_COUNT);
  journal.role = (LinkJournalRole)raw_role;
  journal.stage = (LinkJournalStage)raw_stage;
  transition_id = (LinkJournalTransitionId)raw_transition_id;
  __CPROVER_assume(journal_valid(&journal));
  if(journal.stage == LINK_JOURNAL_ARMED)
  {
    __CPROVER_assert(link_journal_local_cancel_permitted(&journal) ==
     (journal.role == LINK_JOURNAL_ROLE_HOST),
     "only an armed coordinator retains local cancel authority");
  }
  if(journal.stage == LINK_JOURNAL_DECIDED ||
     journal.stage == LINK_JOURNAL_RELEASED)
  {
    __CPROVER_assert(!link_journal_local_cancel_permitted(&journal),
     "decided or released evidence cannot be cancelled locally");
  }
  before = journal;
  transition = link_journal_transition(transition_id);
  applied = link_journal_apply(&journal, transition_id, &record);
  if(applied)
  {
    __CPROVER_assert(transition != 0 && journal.stage == transition->target,
     "successful transition reaches its generated target");
    __CPROVER_assert(journal.session_nonce == before.session_nonce &&
     journal.epoch == before.epoch,
     "transition preserves session identity");
    __CPROVER_assert(journal.stage != LINK_JOURNAL_DECIDED ||
     journal.transfer_hash != 0, "decision hash is nonzero");
    __CPROVER_assert(before.stage != LINK_JOURNAL_DECIDED ||
     journal.stage != LINK_JOURNAL_ARMED,
     "a decision never regresses to armed");
    if(before.transfer_hash != 0 &&
       (journal.stage == LINK_JOURNAL_DECIDED ||
        journal.stage == LINK_JOURNAL_RELEASED))
      __CPROVER_assert(journal.transfer_hash == before.transfer_hash,
       "canonical transfer hash is immutable");
    __CPROVER_assert(journal_valid(&journal),
     "journal invariant is inductive");
  }
  else
  {
    __CPROVER_assert(journal.stage == before.stage,
     "failed transition does not change stage");
  }
  return 0;
}
