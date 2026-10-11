#include <stdint.h>
#include <stdio.h>

#include "link_recovery_coordinator.h"

static int failures;

#define CHECK(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
       #condition); \
      failures++; \
    } \
  } while(0)

static LinkJournalRecord journal_record(const LinkJournal *journal,
 uint32_t sequence, uint32_t hash)
{
  LinkJournalRecord record;

  record.session_nonce = journal->session_nonce;
  record.epoch = journal->epoch;
  record.sequence = sequence;
  record.hash = hash;
  record.checkpoint = journal->checkpoint;
  record.checkpoint_hash = journal->checkpoint_hash;
  record.start_cycle = 100u;
  record.finish_cycle = 200u;
  record.release_shared = 0;
  return record;
}

static void init_pair(LinkJournal *host, LinkJournal *joiner)
{
  link_journal_init(host, LINK_JOURNAL_ROLE_HOST, 11u, 1u, 0u, 0u);
  link_journal_init(joiner, LINK_JOURNAL_ROLE_JOINER, 11u, 1u, 0u, 0u);
}

static void arm(LinkJournal *journal)
{
  LinkJournalRecord record = journal_record(journal, 1u, 0u);

  CHECK(link_journal_apply(journal, LINK_JOURNAL_TRANSITION_ARM, &record));
}

static void decide(LinkJournal *journal, uint32_t hash)
{
  LinkJournalRecord record = journal_record(journal, 1u, hash);
  LinkJournalTransitionId transition = journal->role == LINK_JOURNAL_ROLE_HOST ?
   LINK_JOURNAL_TRANSITION_ISSUE_DECISION :
   LINK_JOURNAL_TRANSITION_RECORD_DECISION;

  CHECK(link_journal_apply(journal, transition, &record));
}

static void release(LinkJournal *journal, uint32_t hash, int shared)
{
  LinkJournalRecord record = journal_record(journal, 1u, hash);
  LinkJournalTransitionId transition = journal->role == LINK_JOURNAL_ROLE_HOST ?
   LINK_JOURNAL_TRANSITION_MARK_RELEASED :
   LINK_JOURNAL_TRANSITION_RECORD_RELEASE;

  CHECK(link_journal_apply(journal, transition, &record));
  if(shared)
  {
    record.release_shared = 1;
    CHECK(link_journal_apply(journal,
     LINK_JOURNAL_TRANSITION_REPLAY_RELEASE, &record));
  }
}

static void note_pair(LinkRecoveryCoordinator *coordinator,
 const LinkJournal *host, const LinkJournal *joiner)
{
  LinkRecoverySnapshot host_snapshot;
  LinkRecoverySnapshot joiner_snapshot;

  link_recovery_snapshot_journal(host, &host_snapshot);
  link_recovery_snapshot_journal(joiner, &joiner_snapshot);
  CHECK(link_recovery_note_snapshot(coordinator, &joiner_snapshot));
  CHECK(link_recovery_note_snapshot(coordinator, &host_snapshot));
}

static void begin_pair(LinkRecoveryCoordinator *coordinator,
 const LinkJournal *host, const LinkJournal *joiner)
{
  link_recovery_coordinator_init(coordinator, 11u, 1u);
  CHECK(link_recovery_begin(coordinator));
  CHECK(!link_recovery_begin(coordinator));
  note_pair(coordinator, host, joiner);
}

static void test_armed_pair_cancels_to_empty(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkRecoveryCoordinator coordinator;
  LinkRecoveryPlan plan;

  init_pair(&host, &joiner);
  arm(&host);
  arm(&joiner);
  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_EMPTY);
  CHECK(plan.sequence_floor == 1u);
  CHECK(link_recovery_apply_plan(&host, &plan));
  CHECK(link_recovery_apply_plan(&joiner, &plan));
  CHECK(host.stage == LINK_JOURNAL_EMPTY);
  CHECK(joiner.stage == LINK_JOURNAL_EMPTY);
  CHECK(host.epoch == 2u && joiner.epoch == 2u);
  CHECK(link_recovery_complete(&coordinator, &plan));
  CHECK(coordinator.state == LINK_RECOVERY_NORMAL);
}

static void test_one_sided_arm_advances_sequence_floor(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkRecoveryCoordinator coordinator;
  LinkRecoveryPlan plan;

  init_pair(&host, &joiner);
  arm(&host);
  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_EMPTY);
  CHECK(plan.record.sequence == 0u);
  CHECK(plan.sequence_floor == 1u);
  CHECK(link_recovery_apply_plan(&host, &plan));
  CHECK(link_recovery_apply_plan(&joiner, &plan));
  CHECK(host.checkpoint == 0u && joiner.checkpoint == 0u);
}

static void test_host_decision_recovers_joiner(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkRecoveryCoordinator coordinator;
  LinkRecoveryPlan plan;

  init_pair(&host, &joiner);
  arm(&host);
  arm(&joiner);
  decide(&host, 0x11112222u);
  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_DECIDED);
  CHECK(plan.sequence_floor == 1u);
  CHECK(link_recovery_apply_plan(&host, &plan));
  CHECK(link_recovery_apply_plan(&joiner, &plan));
  CHECK(host.stage == LINK_JOURNAL_DECIDED);
  CHECK(joiner.stage == LINK_JOURNAL_DECIDED);
  CHECK(host.transfer_hash == joiner.transfer_hash);
  CHECK(host.epoch == 2u && joiner.epoch == 2u);
}

static void test_release_recovers_decided_peer(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkRecoveryCoordinator coordinator;
  LinkRecoveryPlan plan;

  init_pair(&host, &joiner);
  arm(&host);
  arm(&joiner);
  decide(&host, 0x33334444u);
  decide(&joiner, 0x33334444u);
  release(&host, 0x33334444u, 1);
  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_RELEASED);
  CHECK(plan.sequence_floor == 1u);
  CHECK(link_recovery_apply_plan(&host, &plan));
  CHECK(link_recovery_apply_plan(&joiner, &plan));
  CHECK(host.stage == LINK_JOURNAL_RELEASED);
  CHECK(joiner.stage == LINK_JOURNAL_RELEASED);
  CHECK(host.release_shared && joiner.release_shared);
}

static void test_checkpoint_covers_released_peer(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkRecoveryCoordinator coordinator;
  LinkRecoveryPlan plan;
  LinkJournalRecord record;

  init_pair(&host, &joiner);
  arm(&host);
  arm(&joiner);
  decide(&host, 0x55556666u);
  decide(&joiner, 0x55556666u);
  release(&host, 0x55556666u, 0);
  release(&joiner, 0x55556666u, 1);
  record = journal_record(&host, 1u, 0x55556666u);
  record.release_shared = 1;
  CHECK(link_journal_claim_effect(&host, &record) ==
   LINK_JOURNAL_EFFECT_FIRST);
  record = journal_record(&joiner, 1u, 0x55556666u);
  record.release_shared = 1;
  CHECK(link_journal_claim_effect(&joiner, &record) ==
   LINK_JOURNAL_EFFECT_FIRST);
  CHECK(link_journal_apply(&joiner,
   LINK_JOURNAL_TRANSITION_CLEAR_RELEASED, &record));

  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_CHECKPOINT);
  CHECK(plan.sequence_floor == 1u);
  CHECK(link_recovery_apply_plan(&host, &plan));
  CHECK(link_recovery_apply_plan(&joiner, &plan));
  CHECK(host.stage == LINK_JOURNAL_EMPTY);
  CHECK(joiner.stage == LINK_JOURNAL_EMPTY);
  CHECK(host.checkpoint == 1u && joiner.checkpoint == 1u);
  CHECK(host.checkpoint_hash == 0x55556666u);
  CHECK(joiner.checkpoint_hash == 0x55556666u);
}

static void test_conflicts_close(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkRecoveryCoordinator coordinator;
  LinkRecoveryPlan plan;
  LinkRecoverySnapshot snapshot;

  init_pair(&host, &joiner);
  arm(&host);
  arm(&joiner);
  decide(&host, 0x77778888u);
  decide(&joiner, 0x9999aaaau);
  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_CLOSE);
  CHECK(plan.close_reason == LINK_RECOVERY_CLOSE_HASH);
  CHECK(coordinator.state == LINK_RECOVERY_CLOSED);

  init_pair(&host, &joiner);
  link_recovery_coordinator_init(&coordinator, 11u, 1u);
  CHECK(link_recovery_begin(&coordinator));
  link_recovery_snapshot_journal(&host, &snapshot);
  snapshot.record.epoch = 2u;
  CHECK(!link_recovery_note_snapshot(&coordinator, &snapshot));
  CHECK(coordinator.state == LINK_RECOVERY_CLOSED);
  CHECK(coordinator.close_reason == LINK_RECOVERY_CLOSE_IDENTITY);

  init_pair(&host, &joiner);
  joiner.checkpoint = 1u;
  joiner.checkpoint_hash = 0x12345678u;
  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_CLOSE);
  CHECK(plan.close_reason == LINK_RECOVERY_CLOSE_CHECKPOINT);

  init_pair(&host, &joiner);
  arm(&host);
  arm(&joiner);
  decide(&joiner, 0x77778888u);
  begin_pair(&coordinator, &host, &joiner);
  CHECK(link_recovery_make_plan(&coordinator, &plan));
  CHECK(plan.kind == LINK_RECOVERY_PLAN_CLOSE);
  CHECK(plan.close_reason == LINK_RECOVERY_CLOSE_STAGE);
}

static void test_duplicate_snapshot_is_semantic(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkRecoveryCoordinator coordinator;
  LinkRecoverySnapshot snapshot;

  init_pair(&host, &joiner);
  link_recovery_coordinator_init(&coordinator, 11u, 1u);
  CHECK(link_recovery_begin(&coordinator));
  link_recovery_snapshot_journal(&host, &snapshot);
  CHECK(link_recovery_note_snapshot(&coordinator, &snapshot));
  CHECK(link_recovery_note_snapshot(&coordinator, &snapshot));
  snapshot.record.checkpoint_hash = 1u;
  snapshot.record.hash = 1u;
  CHECK(!link_recovery_note_snapshot(&coordinator, &snapshot));
  CHECK(coordinator.state == LINK_RECOVERY_CLOSED);
}

int main(void)
{
  test_armed_pair_cancels_to_empty();
  test_one_sided_arm_advances_sequence_floor();
  test_host_decision_recovers_joiner();
  test_release_recovers_decided_peer();
  test_checkpoint_covers_released_peer();
  test_conflicts_close();
  test_duplicate_snapshot_is_semantic();

  if(failures != 0)
  {
    fprintf(stderr, "%d recovery coordinator test(s) failed\n", failures);
    return 1;
  }
  puts("link recovery coordinator tests passed");
  return 0;
}
