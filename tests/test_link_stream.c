#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_stream.h"

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

static LinkStreamStart make_start(uint32_t generation, uint32_t sequence)
{
  LinkStreamStart start;

  memset(&start, 0, sizeof(start));
  start.generation = generation;
  start.start_cycle = sequence * 1000u;
  start.finish_cycle = start.start_cycle + 0x2736u;
  start.clock_cycle = start.start_cycle;
  start.acknowledged_sequence = sequence == 1u ? 0u : sequence - 1u;
  start.outgoing = 0x1000u + sequence;
  start.identity = 0xA5000000u ^ sequence;
  start.mode = 2u;
  start.setting = 3u;
  start.player_count = 2u;
  start.flags = LINK_STREAM_FLAG_CLOCK_OWNER;
  return start;
}

static LinkStreamReply make_reply(const LinkStreamStart *start,
 uint32_t sequence)
{
  LinkStreamReply reply;

  memset(&reply, 0, sizeof(reply));
  reply.generation = start->generation;
  reply.clock_cycle = start->start_cycle + 100u;
  reply.acknowledged_sequence = sequence;
  reply.outgoing = 0x2000u + sequence;
  reply.start_identity = start->identity;
  reply.start_cycle = start->start_cycle;
  reply.mode = start->mode;
  reply.setting = start->setting;
  reply.player_count = start->player_count;
  return reply;
}

static void test_host_happy_path_and_duplicate_reply(void)
{
  LinkStream stream;
  LinkStreamStart start = make_start(7u, 1u);
  LinkStreamReply reply = make_reply(&start, 1u);
  uint32_t actions;

  link_stream_init(&stream, LINK_STREAM_ROLE_HOST, 7u);
  CHECK(link_stream_state(&stream) == LINK_STREAM_STATE_IDLE);
  actions = link_stream_host_begin(&stream, 1u, &start);
  CHECK(actions == LINK_STREAM_ACTION_SEND_START);
  CHECK(link_stream_state(&stream) == LINK_STREAM_STATE_HOST_WAIT_REPLY);
  CHECK(link_stream_active_sequence(&stream) == 1u);

  actions = link_stream_host_receive_reply(&stream, 1u, &reply);
  CHECK(actions == LINK_STREAM_ACTION_APPLY_HOST);
  CHECK(link_stream_state(&stream) == LINK_STREAM_STATE_HOST_WAIT_APPLY);

  actions = link_stream_host_receive_reply(&stream, 1u, &reply);
  CHECK(actions == LINK_STREAM_ACTION_DUPLICATE);
  CHECK(link_stream_apply_count(&stream) == 0u);

  actions = link_stream_note_applied(&stream, 1u);
  CHECK(actions == LINK_STREAM_ACTION_COMMITTED);
  CHECK(link_stream_state(&stream) == LINK_STREAM_STATE_IDLE);
  CHECK(link_stream_last_committed(&stream) == 1u);
  CHECK(link_stream_apply_count(&stream) == 1u);

  actions = link_stream_host_receive_reply(&stream, 1u, &reply);
  CHECK(actions == (LINK_STREAM_ACTION_DUPLICATE |
   LINK_STREAM_ACTION_STALE));
  CHECK(link_stream_apply_count(&stream) == 1u);
}

static void test_joiner_latch_replay_and_ack(void)
{
  LinkStream stream;
  LinkStreamStart first = make_start(7u, 1u);
  LinkStreamStart second = make_start(7u, 2u);
  LinkStreamReply reply;
  LinkStreamReply replay;
  uint32_t actions;

  link_stream_init(&stream, LINK_STREAM_ROLE_JOINER, 7u);
  CHECK(link_stream_state(&stream) == LINK_STREAM_STATE_JOIN_WAIT_START);

  actions = link_stream_join_receive_start(&stream, 1u, &first, &replay);
  CHECK(actions == LINK_STREAM_ACTION_LATCH_JOIN);
  CHECK(link_stream_state(&stream) == LINK_STREAM_STATE_JOIN_WAIT_LATCH);
  CHECK(link_stream_latch_count(&stream) == 1u);

  actions = link_stream_join_receive_start(&stream, 1u, &first, &replay);
  CHECK(actions == LINK_STREAM_ACTION_DUPLICATE);
  CHECK(link_stream_latch_count(&stream) == 1u);

  actions = link_stream_join_latched(&stream, 1u, 0x2001u,
   first.start_cycle + 100u, &reply);
  CHECK(actions == (LINK_STREAM_ACTION_SEND_REPLY |
   LINK_STREAM_ACTION_APPLY_JOIN));
  CHECK(reply.acknowledged_sequence == 1u);
  CHECK(reply.outgoing == 0x2001u);
  CHECK(link_stream_state(&stream) == LINK_STREAM_STATE_JOIN_WAIT_APPLY);

  memset(&replay, 0, sizeof(replay));
  actions = link_stream_join_receive_start(&stream, 1u, &first, &replay);
  CHECK(actions == (LINK_STREAM_ACTION_DUPLICATE |
   LINK_STREAM_ACTION_REPLAY_REPLY));
  CHECK(memcmp(&reply, &replay, sizeof(reply)) == 0);
  CHECK(link_stream_latch_count(&stream) == 1u);

  actions = link_stream_note_applied(&stream, 1u);
  CHECK(actions == LINK_STREAM_ACTION_COMMITTED);
  CHECK(link_stream_last_committed(&stream) == 1u);
  CHECK(link_stream_apply_count(&stream) == 1u);

  actions = link_stream_join_receive_start(&stream, 2u, &second, &replay);
  CHECK(actions == LINK_STREAM_ACTION_LATCH_JOIN);
  CHECK(link_stream_latch_count(&stream) == 2u);

  actions = link_stream_join_receive_start(&stream, 1u, &first, &replay);
  CHECK(actions == (LINK_STREAM_ACTION_DUPLICATE |
   LINK_STREAM_ACTION_STALE));
  CHECK(link_stream_latch_count(&stream) == 2u);
}

static void test_conflicts_do_not_apply(void)
{
  LinkStream host;
  LinkStream joiner;
  LinkStreamStart start = make_start(4u, 1u);
  LinkStreamStart changed = start;
  LinkStreamReply reply = make_reply(&start, 1u);
  LinkStreamReply replay;
  uint32_t actions;

  link_stream_init(&joiner, LINK_STREAM_ROLE_JOINER, 4u);
  CHECK(link_stream_join_receive_start(&joiner, 1u, &start, &replay) ==
   LINK_STREAM_ACTION_LATCH_JOIN);
  changed.identity ^= 1u;
  actions = link_stream_join_receive_start(&joiner, 1u, &changed, &replay);
  CHECK(actions == LINK_STREAM_ACTION_CONFLICT);
  CHECK(link_stream_conflict_count(&joiner) == 1u);
  CHECK(link_stream_latch_count(&joiner) == 1u);

  link_stream_init(&host, LINK_STREAM_ROLE_HOST, 4u);
  CHECK(link_stream_host_begin(&host, 1u, &start) ==
   LINK_STREAM_ACTION_SEND_START);
  reply.start_identity ^= 1u;
  actions = link_stream_host_receive_reply(&host, 1u, &reply);
  CHECK(actions == LINK_STREAM_ACTION_CONFLICT);
  CHECK(link_stream_state(&host) == LINK_STREAM_STATE_HOST_WAIT_REPLY);
  CHECK(link_stream_apply_count(&host) == 0u);
}

static void test_generation_future_and_reset(void)
{
  LinkStream host;
  LinkStream joiner;
  LinkStreamStart start = make_start(3u, 1u);
  LinkStreamStart future = make_start(3u, 2u);
  LinkStreamReply replay;
  uint32_t actions;

  link_stream_init(&joiner, LINK_STREAM_ROLE_JOINER, 3u);
  start.generation = 2u;
  actions = link_stream_join_receive_start(&joiner, 1u, &start, &replay);
  CHECK(actions == LINK_STREAM_ACTION_STALE_GENERATION);
  CHECK(link_stream_latch_count(&joiner) == 0u);

  actions = link_stream_join_receive_start(&joiner, 2u, &future, &replay);
  CHECK(actions == LINK_STREAM_ACTION_FUTURE);
  CHECK(link_stream_latch_count(&joiner) == 0u);

  link_stream_init(&host, LINK_STREAM_ROLE_HOST, 3u);
  start = make_start(3u, 1u);
  CHECK(link_stream_host_begin(&host, 1u, &start) ==
   LINK_STREAM_ACTION_SEND_START);
  link_stream_reset_generation(&host, 4u);
  CHECK(link_stream_generation(&host) == 4u);
  CHECK(link_stream_state(&host) == LINK_STREAM_STATE_IDLE);
  CHECK(link_stream_active_sequence(&host) == 0u);
  CHECK(link_stream_last_committed(&host) == 0u);
}

static void test_wrap_skips_reserved_zero(void)
{
  LinkStream stream;
  LinkStreamStart start = make_start(9u, 1u);

  link_stream_init_at(&stream, LINK_STREAM_ROLE_HOST, 9u, UINT32_MAX);
  start.acknowledged_sequence = UINT32_MAX;
  CHECK(link_stream_host_begin(&stream, 1u, &start) ==
   LINK_STREAM_ACTION_SEND_START);
  CHECK(link_stream_active_sequence(&stream) == 1u);
}

static void test_duplicate_storm_is_bounded(void)
{
  LinkStream stream;
  LinkStreamStart start = make_start(5u, 1u);
  LinkStreamReply reply;
  LinkStreamReply replay;
  uint32_t i;

  link_stream_init(&stream, LINK_STREAM_ROLE_JOINER, 5u);
  CHECK(link_stream_join_receive_start(&stream, 1u, &start, &replay) ==
   LINK_STREAM_ACTION_LATCH_JOIN);
  CHECK(link_stream_join_latched(&stream, 1u, 0x2345u,
   start.start_cycle + 10u, &reply) ==
   (LINK_STREAM_ACTION_SEND_REPLY | LINK_STREAM_ACTION_APPLY_JOIN));
  CHECK(link_stream_note_applied(&stream, 1u) ==
   LINK_STREAM_ACTION_COMMITTED);

  for(i = 0; i < 100000u; i++)
  {
    CHECK(link_stream_join_receive_start(&stream, 1u, &start, &replay) ==
     (LINK_STREAM_ACTION_DUPLICATE |
      LINK_STREAM_ACTION_REPLAY_REPLY));
  }
  CHECK(link_stream_latch_count(&stream) == 1u);
  CHECK(link_stream_apply_count(&stream) == 1u);
  CHECK(link_stream_reply_cache_depth(&stream) == 1u);
  CHECK(link_stream_replay_count(&stream) == 100000u);
}

static void test_recovery_rebases_pending_start(void)
{
  LinkStream host;
  LinkStreamStart start = make_start(7u, 1u);

  link_stream_init(&host, LINK_STREAM_ROLE_HOST, 7u);
  CHECK(link_stream_host_begin(&host, 1u, &start) ==
   LINK_STREAM_ACTION_SEND_START);
  CHECK(link_stream_recover_host_pending(&host, 8u));
  CHECK(link_stream_generation(&host) == 8u);
  CHECK(host.active_start.generation == 8u);
  CHECK(host.active_start.identity == start.identity);
  CHECK(host.active_start.acknowledged_sequence == 0u);
  CHECK(link_stream_latch_count(&host) == 0u);
  CHECK(link_stream_apply_count(&host) == 0u);
}

static void test_recovery_rebases_cached_reply_without_reapply(void)
{
  LinkStream joiner;
  LinkStreamStart start = make_start(7u, 1u);
  LinkStreamReply reply;
  LinkStreamReply replay;

  link_stream_init(&joiner, LINK_STREAM_ROLE_JOINER, 7u);
  CHECK(link_stream_join_receive_start(&joiner, 1u, &start, &replay) ==
   LINK_STREAM_ACTION_LATCH_JOIN);
  CHECK(link_stream_join_latched(&joiner, 1u, 0x4321u,
   start.start_cycle + 12u, &reply) ==
   (LINK_STREAM_ACTION_SEND_REPLY | LINK_STREAM_ACTION_APPLY_JOIN));
  CHECK(link_stream_note_applied(&joiner, 1u) ==
   LINK_STREAM_ACTION_COMMITTED);

  CHECK(link_stream_recover_joiner_pending(&joiner, 8u, 1u,
   start.identity));
  start.generation = 8u;
  CHECK(link_stream_join_receive_start(&joiner, 1u, &start, &replay) ==
   (LINK_STREAM_ACTION_DUPLICATE |
    LINK_STREAM_ACTION_REPLAY_REPLY));
  CHECK(replay.generation == 8u);
  CHECK(replay.outgoing == 0x4321u);
  CHECK(link_stream_latch_count(&joiner) == 1u);
  CHECK(link_stream_apply_count(&joiner) == 1u);
}

static void test_recovery_rejects_missing_or_mismatched_cache(void)
{
  LinkStream joiner;
  LinkStreamStart start = make_start(7u, 1u);
  LinkStreamReply reply;
  LinkStreamReply replay;

  link_stream_init(&joiner, LINK_STREAM_ROLE_JOINER, 7u);
  CHECK(link_stream_join_receive_start(&joiner, 1u, &start, &replay) ==
   LINK_STREAM_ACTION_LATCH_JOIN);
  CHECK(link_stream_join_latched(&joiner, 1u, 0x4321u,
   start.start_cycle + 12u, &reply) ==
   (LINK_STREAM_ACTION_SEND_REPLY | LINK_STREAM_ACTION_APPLY_JOIN));
  CHECK(link_stream_note_applied(&joiner, 1u) ==
   LINK_STREAM_ACTION_COMMITTED);
  CHECK(!link_stream_recover_joiner_pending(&joiner, 8u, 1u,
   start.identity ^ 1u));
  joiner.reply_cache.valid = 0;
  CHECK(!link_stream_recover_joiner_pending(&joiner, 8u, 1u,
   start.identity));
}

static void test_recovery_discards_only_unlatched_start(void)
{
  LinkStream joiner;
  LinkStreamStart start = make_start(7u, 1u);
  LinkStreamReply reply;

  link_stream_init(&joiner, LINK_STREAM_ROLE_JOINER, 7u);
  CHECK(!link_stream_discard_unlatched_start(&joiner));
  CHECK(link_stream_join_receive_start(&joiner, 1u, &start, &reply) ==
   LINK_STREAM_ACTION_LATCH_JOIN);
  CHECK(link_stream_discard_unlatched_start(&joiner));
  CHECK(link_stream_last_committed(&joiner) == 0);
  CHECK(link_stream_active_sequence(&joiner) == 0);
  CHECK(link_stream_apply_count(&joiner) == 0);
  CHECK(link_stream_recover_joiner_pending(&joiner, 8u, 1u, start.identity));
  start.generation = 8u;
  CHECK(link_stream_join_receive_start(&joiner, 1u, &start, &reply) ==
   LINK_STREAM_ACTION_LATCH_JOIN);
  CHECK(link_stream_join_latched(&joiner, 1u, 0x4321u, 0, &reply) &
   LINK_STREAM_ACTION_APPLY_JOIN);
  CHECK(!link_stream_discard_unlatched_start(&joiner));
  CHECK(link_stream_note_applied(&joiner, 1u) == LINK_STREAM_ACTION_COMMITTED);
  CHECK(!link_stream_discard_unlatched_start(&joiner));
  CHECK(link_stream_apply_count(&joiner) == 1);
}

int main(void)
{
  test_host_happy_path_and_duplicate_reply();
  test_joiner_latch_replay_and_ack();
  test_conflicts_do_not_apply();
  test_generation_future_and_reset();
  test_wrap_skips_reserved_zero();
  test_duplicate_storm_is_bounded();
  test_recovery_rebases_pending_start();
  test_recovery_rebases_cached_reply_without_reapply();
  test_recovery_rejects_missing_or_mismatched_cache();
  test_recovery_discards_only_unlatched_start();

  if(failures)
  {
    fprintf(stderr, "%d link stream test(s) failed\n", failures);
    return 1;
  }

  puts("Link stream tests passed");
  return 0;
}
