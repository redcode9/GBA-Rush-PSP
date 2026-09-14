#include <stdint.h>
#include <string.h>

#include "link_stream.h"

static LinkStreamStart proof_start(uint32_t generation, uint32_t sequence)
{
  LinkStreamStart start;

  memset(&start, 0, sizeof(start));
  start.generation = generation;
  start.start_cycle = sequence * 100u;
  start.finish_cycle = start.start_cycle + 10u;
  start.clock_cycle = start.start_cycle;
  start.acknowledged_sequence = sequence - 1u;
  start.outgoing = sequence;
  start.identity = 0xA5000000u ^ sequence;
  start.mode = 2u;
  start.setting = 3u;
  start.player_count = 2u;
  start.flags = LINK_STREAM_FLAG_CLOCK_OWNER;
  return start;
}

static int proof_reply_equal(const LinkStreamReply *left,
 const LinkStreamReply *right)
{
  return left->generation == right->generation &&
   left->clock_cycle == right->clock_cycle &&
   left->acknowledged_sequence == right->acknowledged_sequence &&
   left->outgoing == right->outgoing &&
   left->start_identity == right->start_identity &&
   left->start_cycle == right->start_cycle &&
   left->mode == right->mode && left->setting == right->setting &&
   left->player_count == right->player_count && left->flags == right->flags;
}

int main(void)
{
  LinkStream joiner;
  LinkStream host;
  LinkStream pending_host;
  LinkStream invalid_joiner;
  LinkStream unlatched_joiner;
  LinkStreamStart start = proof_start(1u, 1u);
  LinkStreamStart changed = start;
  LinkStreamReply reply;
  LinkStreamReply replay;
  uint32_t actions;

  link_stream_init(&joiner, LINK_STREAM_ROLE_JOINER, 1u);
  actions = link_stream_join_receive_start(&joiner, 1u, &start, &replay);
  __CPROVER_assert(actions == LINK_STREAM_ACTION_LATCH_JOIN,
   "first start requests one latch");
  actions = link_stream_join_receive_start(&joiner, 1u, &start, &replay);
  __CPROVER_assert((actions & LINK_STREAM_ACTION_LATCH_JOIN) == 0,
   "duplicate start cannot request a second latch");
  unlatched_joiner = joiner;
  __CPROVER_assert(link_stream_discard_unlatched_start(&unlatched_joiner),
   "recovery may discard an admitted start before a guest latch");
  __CPROVER_assert(link_stream_active_sequence(&unlatched_joiner) == 0u &&
   link_stream_last_committed(&unlatched_joiner) == 0u &&
   link_stream_apply_count(&unlatched_joiner) == 0u,
   "discarding an unlatched start cannot create a guest effect");
  __CPROVER_assert(link_stream_recover_joiner_pending(&unlatched_joiner,
    2u, 1u, start.identity),
   "host may replay the unlatched start in the recovered generation");
  actions = link_stream_join_latched(&joiner, 1u, 0x2222u, 110u, &reply);
  __CPROVER_assert(!link_stream_discard_unlatched_start(&joiner),
   "recovery cannot discard an already latched result");
  __CPROVER_assert((actions & LINK_STREAM_ACTION_APPLY_JOIN) != 0,
   "latched start requests one join apply");
  actions = link_stream_join_receive_start(&joiner, 1u, &start, &replay);
  __CPROVER_assert((actions & LINK_STREAM_ACTION_APPLY_JOIN) == 0,
   "replay cannot request a second join apply");
  __CPROVER_assert(proof_reply_equal(&reply, &replay),
   "replay preserves the cached word and identity");
  (void)link_stream_note_applied(&joiner, 1u);
  actions = link_stream_join_receive_start(&joiner, 1u, &start, &replay);
  __CPROVER_assert((actions & LINK_STREAM_ACTION_APPLY_JOIN) == 0,
   "post-commit duplicate cannot apply again");
  __CPROVER_assert(link_stream_latch_count(&joiner) == 1u,
   "one sequence latches once");
  __CPROVER_assert(link_stream_apply_count(&joiner) == 1u,
   "one sequence commits once on joiner");
  __CPROVER_assert(link_stream_reply_cache_depth(&joiner) <= 1u,
   "reply cache is bounded");

  invalid_joiner = joiner;
  __CPROVER_assert(!link_stream_recover_joiner_pending(&invalid_joiner,
    2u, 1u, start.identity ^ 1u),
   "recovery rejects an impossible cached-reply identity");
  __CPROVER_assert(invalid_joiner.generation == 1u &&
   link_stream_apply_count(&invalid_joiner) == 1u,
   "rejected recovery cannot mutate generation or side effects");

  changed.identity ^= 1u;
  actions = link_stream_join_receive_start(&joiner, 1u, &changed, &replay);
  __CPROVER_assert(actions == LINK_STREAM_ACTION_CONFLICT,
   "same sequence with changed identity conflicts");
  __CPROVER_assert(link_stream_apply_count(&joiner) == 1u,
   "conflict cannot apply");

  link_stream_init(&host, LINK_STREAM_ROLE_HOST, 1u);
  (void)link_stream_host_begin(&host, 1u, &start);
  actions = link_stream_host_receive_reply(&host, 1u, &reply);
  __CPROVER_assert(actions == LINK_STREAM_ACTION_APPLY_HOST,
   "matching reply requests host apply");
  actions = link_stream_host_receive_reply(&host, 1u, &reply);
  __CPROVER_assert((actions & LINK_STREAM_ACTION_APPLY_HOST) == 0,
   "duplicate reply cannot request a second host apply");
  (void)link_stream_note_applied(&host, 1u);
  __CPROVER_assert(link_stream_apply_count(&host) == 1u,
   "one sequence commits once on host");

  link_stream_init(&pending_host, LINK_STREAM_ROLE_HOST, 1u);
  __CPROVER_assert(link_stream_host_begin(&pending_host, 1u, &start) ==
    LINK_STREAM_ACTION_SEND_START,
   "host admits one causal start");
  __CPROVER_assert(link_stream_recover_host_pending(&pending_host, 2u),
   "pending host start rebases into the recovery generation");
  actions = link_stream_host_receive_reply(&pending_host, 1u, &reply);
  __CPROVER_assert(actions == LINK_STREAM_ACTION_STALE_GENERATION,
   "old-generation reply cannot satisfy recovered host state");
  reply.generation = 2u;
  actions = link_stream_host_receive_reply(&pending_host, 1u, &reply);
  __CPROVER_assert(actions == LINK_STREAM_ACTION_APPLY_HOST,
   "matching recovered reply remains live");
  __CPROVER_assert(link_stream_note_applied(&pending_host, 1u) ==
    LINK_STREAM_ACTION_COMMITTED &&
    link_stream_apply_count(&pending_host) == 1u,
   "recovered host applies and commits exactly once");

  __CPROVER_assert(link_stream_recover_joiner_pending(&joiner, 2u, 1u,
    start.identity),
   "committed reply cache rebases into the recovery generation");
  changed = start;
  changed.generation = 2u;
  actions = link_stream_join_receive_start(&joiner, 1u, &changed, &replay);
  __CPROVER_assert((actions & LINK_STREAM_ACTION_REPLAY_REPLY) != 0,
   "recovered start replays the semantic reply");
  __CPROVER_assert(link_stream_latch_count(&joiner) == 1u &&
   link_stream_apply_count(&joiner) == 1u,
   "recovered replay cannot repeat GBA side effects");

  actions = link_stream_join_receive_start(&joiner, 1u, &start, &replay);
  __CPROVER_assert(actions == LINK_STREAM_ACTION_STALE_GENERATION,
   "old generation cannot request latch or apply");
  __CPROVER_assert(link_stream_latch_count(&joiner) == 1u &&
   link_stream_apply_count(&joiner) == 1u,
   "old generation exposes no new side effects after recovery");

  (void)link_stream_acknowledge(&joiner, 0u);
  __CPROVER_assert(link_stream_reply_cache_depth(&joiner) == 1u,
   "an older acknowledgement cannot retire the reply cache");
  (void)link_stream_acknowledge(&joiner, 1u);
  __CPROVER_assert(link_stream_reply_cache_depth(&joiner) == 0u,
   "matching acknowledgement retires the bounded reply cache");

  __CPROVER_assert(link_stream_recover_committed(&host, 2u, 1u),
   "committed host rebases into the recovery generation");
  reply.generation = 1u;
  actions = link_stream_host_receive_reply(&host, 1u, &reply);
  __CPROVER_assert(actions == LINK_STREAM_ACTION_STALE_GENERATION,
   "old reply cannot request host apply after recovery");
  __CPROVER_assert(link_stream_apply_count(&host) == 1u,
   "old reply cannot repeat the host side effect");
  return 0;
}
