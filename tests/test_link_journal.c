#include <stdint.h>
#include <stdio.h>

#include "link_journal.h"

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

static LinkJournalRecord record(uint32_t sequence, uint32_t hash)
{
  LinkJournalRecord value;

  value.session_nonce = 11u;
  value.epoch = 1u;
  value.sequence = sequence;
  value.hash = hash;
  value.checkpoint = 0u;
  value.checkpoint_hash = 0u;
  value.start_cycle = 100u;
  value.finish_cycle = 200u;
  value.release_shared = 0;
  return value;
}

static void test_host_happy_path_uses_generated_edges(void)
{
  LinkJournal journal;
  LinkJournalRecord value = record(1u, 0u);

  link_journal_init(&journal, LINK_JOURNAL_ROLE_HOST, 11u, 1u, 0u, 0u);
  CHECK(link_journal_apply(&journal, LINK_JOURNAL_TRANSITION_ARM, &value));
  value.hash = 0x12345678u;
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_ISSUE_DECISION, &value));
  CHECK(journal.stage == LINK_JOURNAL_DECIDED);
  CHECK(!link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_RECORD_DECISION, &value));
  CHECK(journal.last_error == LINK_JOURNAL_ERROR_TRANSITION);
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_MARK_RELEASED, &value));
  CHECK(journal.stage == LINK_JOURNAL_RELEASED);
  CHECK(link_journal_claim_effect(&journal, &value) ==
   LINK_JOURNAL_EFFECT_FIRST);
  CHECK(link_journal_claim_effect(&journal, &value) ==
   LINK_JOURNAL_EFFECT_ALREADY_APPLIED);
  CHECK(!link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_CLEAR_RELEASED, &value));
  value.release_shared = 1;
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_REPLAY_RELEASE, &value));
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_CLEAR_RELEASED, &value));
  CHECK(journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(journal.checkpoint == 1u);
  CHECK(journal.checkpoint_hash == 0x12345678u);
}

static void test_joiner_cancel_and_recovery_paths(void)
{
  LinkJournal journal;
  LinkJournalRecord value = record(1u, 0u);

  link_journal_init(&journal, LINK_JOURNAL_ROLE_JOINER, 11u, 1u, 0u, 0u);
  CHECK(link_journal_apply(&journal, LINK_JOURNAL_TRANSITION_ARM, &value));
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_RECOVER_CANCEL_ARM, &value));
  CHECK(journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(link_journal_apply(&journal, LINK_JOURNAL_TRANSITION_ARM, &value));
  value.hash = 0xabcdef01u;
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_RECOVER_DECISION, &value));
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_RECOVER_RELEASE, &value));
  CHECK(journal.release_shared);
}

static void test_identity_hash_and_role_fail_closed(void)
{
  LinkJournal journal;
  LinkJournalRecord value = record(1u, 0u);

  link_journal_init(&journal, LINK_JOURNAL_ROLE_JOINER, 11u, 1u, 0u, 0u);
  value.epoch = 2u;
  CHECK(!link_journal_apply(&journal, LINK_JOURNAL_TRANSITION_ARM, &value));
  CHECK(journal.last_error == LINK_JOURNAL_ERROR_IDENTITY);
  value.epoch = 1u;
  CHECK(link_journal_apply(&journal, LINK_JOURNAL_TRANSITION_ARM, &value));
  value.hash = 7u;
  CHECK(!link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_ISSUE_DECISION, &value));
  CHECK(journal.last_error == LINK_JOURNAL_ERROR_ROLE);
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_RECORD_DECISION, &value));
  value.hash = 8u;
  CHECK(!link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_REPLAY_DECISION, &value));
  CHECK(journal.last_error == LINK_JOURNAL_ERROR_HASH);
}

static void test_sequence_wrap_and_epoch_gate(void)
{
  LinkJournal journal;
  LinkJournalRecord value = record(1u, 0u);

  link_journal_init(&journal, LINK_JOURNAL_ROLE_HOST, 11u, 1u,
   UINT32_MAX, 99u);
  value.checkpoint = UINT32_MAX;
  value.checkpoint_hash = 99u;
  value.start_cycle = UINT32_MAX - 31u;
  value.finish_cycle = value.start_cycle + 64u;
  CHECK(link_journal_apply(&journal, LINK_JOURNAL_TRANSITION_ARM, &value));
  CHECK(journal.start_cycle == UINT32_MAX - 31u);
  CHECK(journal.finish_cycle == 32u);
  CHECK(!link_journal_set_epoch(&journal, 2u));
  CHECK(link_journal_apply(&journal,
   LINK_JOURNAL_TRANSITION_CANCEL_ARM, &value));
  CHECK(link_journal_set_epoch(&journal, 2u));
}

static void test_local_cancel_authority_follows_role_and_stage(void)
{
  LinkJournal host;
  LinkJournal joiner;
  LinkJournalRecord value = record(1u, 0u);

  link_journal_init(&host, LINK_JOURNAL_ROLE_HOST, 11u, 1u, 0u, 0u);
  link_journal_init(&joiner, LINK_JOURNAL_ROLE_JOINER, 11u, 1u, 0u, 0u);
  CHECK(link_journal_local_cancel_permitted(&host));
  CHECK(link_journal_local_cancel_permitted(&joiner));
  CHECK(link_journal_apply(&host, LINK_JOURNAL_TRANSITION_ARM, &value));
  CHECK(link_journal_apply(&joiner, LINK_JOURNAL_TRANSITION_ARM, &value));
  CHECK(link_journal_local_cancel_permitted(&host));
  CHECK(!link_journal_local_cancel_permitted(&joiner));
  value.hash = 0x12345678u;
  CHECK(link_journal_apply(&host,
   LINK_JOURNAL_TRANSITION_ISSUE_DECISION, &value));
  CHECK(!link_journal_local_cancel_permitted(&host));
  CHECK(!link_journal_local_cancel_permitted(NULL));
}

int main(void)
{
  CHECK(LINK_JOURNAL_TRANSITION_ID_COUNT ==
   LINK_JOURNAL_TRANSITION_COUNT);
  test_host_happy_path_uses_generated_edges();
  test_joiner_cancel_and_recovery_paths();
  test_identity_hash_and_role_fail_closed();
  test_sequence_wrap_and_epoch_gate();
  test_local_cancel_authority_follows_role_and_stage();

  if(failures != 0)
  {
    fprintf(stderr, "%d link journal test(s) failed\n", failures);
    return 1;
  }
  puts("link journal tests passed");
  return 0;
}
