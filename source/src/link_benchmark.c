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

#include "link_benchmark.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static void link_benchmark_increment(uint32_t *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

static uint32_t link_benchmark_next_sequence(uint32_t sequence)
{
  sequence++;
  return sequence == 0 ? 1u : sequence;
}

static uint32_t link_benchmark_mix(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  value ^= value >> 16;
  return value;
}

static uint32_t link_benchmark_bounded_us(uint64_t duration)
{
  return duration > UINT_MAX ? UINT_MAX : (uint32_t)duration;
}

static int link_benchmark_control_valid(const LinkBenchmarkControl *control)
{
  return control != NULL && control->run_id != 0 &&
   control->duration_ms != 0 && control->target_words_per_second != 0 &&
   control->maximum_burst_us != 0 && control->burst_words != 0 &&
   control->burst_words <= 64u && control->flags == 0;
}

static int link_benchmark_control_equal(const LinkBenchmarkControl *left,
 const LinkBenchmarkControl *right)
{
  return left->run_id == right->run_id &&
   left->duration_ms == right->duration_ms &&
   left->target_words_per_second == right->target_words_per_second &&
   left->maximum_burst_us == right->maximum_burst_us &&
   left->seed == right->seed && left->burst_words == right->burst_words &&
   left->flags == right->flags;
}

static void link_benchmark_control_copy(LinkBenchmarkControl *output,
 const LinkBenchmarkControl *input, LinkBenchmarkAction action)
{
  *output = *input;
  output->action = (uint8_t)action;
}

static void link_benchmark_record(uint32_t *histogram, uint32_t duration)
{
  uint32_t bucket = duration == 0 ? 0 :
   (duration - 1u) / LINK_BENCHMARK_HISTOGRAM_QUANTUM_US;

  if(bucket >= LINK_BENCHMARK_HISTOGRAM_BUCKETS)
    bucket = LINK_BENCHMARK_HISTOGRAM_BUCKETS - 1u;
  link_benchmark_increment(&histogram[bucket]);
}

static uint32_t link_benchmark_percentile(const uint32_t *histogram,
 uint32_t percentile)
{
  uint64_t total = 0;
  uint64_t target;
  uint64_t seen = 0;
  uint32_t index;

  for(index = 0; index < LINK_BENCHMARK_HISTOGRAM_BUCKETS; index++)
    total += histogram[index];
  if(total == 0)
    return 0;
  target = (total * percentile + 99u) / 100u;
  for(index = 0; index < LINK_BENCHMARK_HISTOGRAM_BUCKETS; index++)
  {
    seen += histogram[index];
    if(seen >= target)
    {
      /* The last bucket is unbounded, not a 32 ms upper bound. */
      if(index == LINK_BENCHMARK_HISTOGRAM_BUCKETS - 1u)
        return UINT_MAX;
      return (index + 1u) * LINK_BENCHMARK_HISTOGRAM_QUANTUM_US;
    }
  }
  return UINT_MAX;
}

static void link_benchmark_note_completed(LinkBenchmark *benchmark,
 uint32_t sequence, uint32_t host_word, uint32_t join_word, uint64_t now,
 int host)
{
  uint64_t elapsed;
  uint32_t duration;

  benchmark->last_progress_us = now;
  benchmark->observed_us = now;
  link_benchmark_increment(&benchmark->completed_words);
  benchmark->digest = link_benchmark_mix(benchmark->digest ^ sequence ^
   host_word ^ ((join_word << 7) | (join_word >> 25)));
  if(host)
  {
    elapsed = now >= benchmark->word_started_us ?
     now - benchmark->word_started_us : 0;
    link_benchmark_record(benchmark->rtt_histogram,
     link_benchmark_bounded_us(elapsed));
  }
  if((benchmark->completed_words - 1u) % benchmark->control.burst_words == 0)
    benchmark->burst_started_us = host ? benchmark->word_started_us : now;
  if(benchmark->completed_words % benchmark->control.burst_words == 0)
  {
    elapsed = now >= benchmark->burst_started_us ?
     now - benchmark->burst_started_us : 0;
    duration = link_benchmark_bounded_us(elapsed);
    link_benchmark_record(benchmark->burst_histogram, duration);
    if(duration > benchmark->burst_max_us)
      benchmark->burst_max_us = duration;
    benchmark->burst_started_us = 0;
  }
}

static LinkBenchmarkStatus link_benchmark_result(const LinkBenchmark *benchmark,
 uint64_t now)
{
  uint64_t elapsed = now >= benchmark->started_us ?
   now - benchmark->started_us : 0;
  uint64_t rate = elapsed == 0 ? 0 :
   ((uint64_t)benchmark->completed_words * 1000000u) / elapsed;
  /* Offered load is 1000 words/s; the release acceptance floor is 600. */
  uint32_t minimum_rate = benchmark->control.target_words_per_second <
   LINK_BENCHMARK_MINIMUM_WORDS_PER_SECOND ?
    benchmark->control.target_words_per_second :
    LINK_BENCHMARK_MINIMUM_WORDS_PER_SECOND;

  if(benchmark->failure == LINK_BENCHMARK_FAILURE_LOCAL_ABORT ||
     benchmark->failure == LINK_BENCHMARK_FAILURE_PEER_ABORT)
    return LINK_BENCHMARK_ABORT;
  if(benchmark->failure != LINK_BENCHMARK_FAILURE_NONE ||
     benchmark->completed_words == 0 ||
     rate < minimum_rate ||
     link_benchmark_percentile(benchmark->burst_histogram, 99u) >
      benchmark->control.maximum_burst_us)
    return LINK_BENCHMARK_FAIL;
  return LINK_BENCHMARK_PASS;
}

static uint32_t link_benchmark_finish(LinkBenchmark *benchmark,
 LinkBenchmarkStatus status, uint64_t now)
{
  if(status == LINK_BENCHMARK_FAIL &&
     benchmark->failure == LINK_BENCHMARK_FAILURE_NONE)
    benchmark->failure = LINK_BENCHMARK_FAILURE_THRESHOLD;
  benchmark->status = status;
  benchmark->finished_us = now;
  benchmark->observed_us = now;
  return LINK_BENCHMARK_ACTION_UPDATED | LINK_BENCHMARK_ACTION_TERMINAL;
}

static uint32_t link_benchmark_begin_stop(LinkBenchmark *benchmark,
 LinkBenchmarkStatus terminal_status, LinkBenchmarkFailure failure,
 uint64_t now, LinkBenchmarkControl *outbound)
{
  if(benchmark->status == LINK_BENCHMARK_STOPPING)
    return LINK_BENCHMARK_ACTION_NONE;
  benchmark->failure = failure;
  benchmark->terminal_status = terminal_status;
  benchmark->status = LINK_BENCHMARK_STOPPING;
  benchmark->state_started_us = now;
  memset(outbound, 0, sizeof(*outbound));
  outbound->run_id = benchmark->control.run_id;
  outbound->action = LINK_BENCHMARK_STOP;
  return LINK_BENCHMARK_ACTION_SEND_CONTROL |
   LINK_BENCHMARK_ACTION_UPDATED;
}

void link_benchmark_init(LinkBenchmark *benchmark)
{
  if(benchmark != NULL)
    memset(benchmark, 0, sizeof(*benchmark));
}

void link_benchmark_default_control(LinkBenchmarkControl *control,
 uint32_t run_id, uint32_t seed)
{
  if(control == NULL)
    return;
  memset(control, 0, sizeof(*control));
  control->run_id = run_id == 0 ? 1u : run_id;
  control->duration_ms = LINK_BENCHMARK_DEFAULT_DURATION_MS;
  control->target_words_per_second =
   LINK_BENCHMARK_DEFAULT_WORDS_PER_SECOND;
  control->maximum_burst_us = LINK_BENCHMARK_DEFAULT_MAXIMUM_BURST_US;
  control->seed = seed;
  control->burst_words = LINK_BENCHMARK_DEFAULT_BURST_WORDS;
  control->action = LINK_BENCHMARK_START;
}

int link_benchmark_host_start(LinkBenchmark *benchmark,
 const LinkBenchmarkControl *control, uint32_t generation, uint64_t now,
 LinkBenchmarkControl *outbound)
{
  if(benchmark == NULL || outbound == NULL || generation == 0 ||
     benchmark->status != LINK_BENCHMARK_IDLE ||
     !link_benchmark_control_valid(control) ||
     control->action != LINK_BENCHMARK_START)
    return 0;

  benchmark->control = *control;
  benchmark->role = LINK_BENCHMARK_ROLE_HOST;
  benchmark->status = LINK_BENCHMARK_WAIT_ACK;
  benchmark->state_started_us = now;
  benchmark->digest = link_benchmark_mix(control->run_id ^ control->seed);
  link_stream_init(&benchmark->stream, LINK_STREAM_ROLE_HOST, generation);
  link_benchmark_control_copy(outbound, control, LINK_BENCHMARK_START);
  return 1;
}

uint32_t link_benchmark_receive_control(LinkBenchmark *benchmark,
 LinkBenchmarkRole local_role, const LinkBenchmarkControl *control,
 uint32_t generation, uint64_t now, LinkBenchmarkControl *outbound)
{
  if(benchmark == NULL || control == NULL || outbound == NULL ||
     generation == 0)
    return LINK_BENCHMARK_ACTION_NONE;

  if(control->action == LINK_BENCHMARK_START)
  {
    if(local_role != LINK_BENCHMARK_ROLE_JOINER ||
       !link_benchmark_control_valid(control))
      return LINK_BENCHMARK_ACTION_NONE;
    if((benchmark->status == LINK_BENCHMARK_RUNNING ||
        link_benchmark_terminal(benchmark)) &&
       link_benchmark_control_equal(&benchmark->control, control))
    {
      link_benchmark_control_copy(outbound, control, LINK_BENCHMARK_ACK);
      return LINK_BENCHMARK_ACTION_SEND_CONTROL;
    }
    if(benchmark->status != LINK_BENCHMARK_IDLE &&
       !link_benchmark_terminal(benchmark))
      return LINK_BENCHMARK_ACTION_NONE;
    link_benchmark_init(benchmark);
    benchmark->control = *control;
    benchmark->role = LINK_BENCHMARK_ROLE_JOINER;
    benchmark->status = LINK_BENCHMARK_RUNNING;
    benchmark->state_started_us = now;
    benchmark->started_us = now;
    benchmark->last_progress_us = now;
    benchmark->observed_us = now;
    benchmark->measurement_started = 1;
    benchmark->digest = link_benchmark_mix(control->run_id ^ control->seed);
    link_stream_init(&benchmark->stream, LINK_STREAM_ROLE_JOINER,
     generation);
    link_benchmark_control_copy(outbound, control, LINK_BENCHMARK_ACK);
    return LINK_BENCHMARK_ACTION_SEND_CONTROL |
     LINK_BENCHMARK_ACTION_UPDATED;
  }

  if(control->action == LINK_BENCHMARK_ACK &&
     control->run_id == benchmark->control.run_id &&
     link_benchmark_control_equal(&benchmark->control, control))
  {
    if(benchmark->role == LINK_BENCHMARK_ROLE_HOST &&
       benchmark->status == LINK_BENCHMARK_WAIT_ACK)
    {
      benchmark->status = LINK_BENCHMARK_RUNNING;
      benchmark->state_started_us = now;
      benchmark->started_us = now;
      benchmark->last_progress_us = now;
      benchmark->observed_us = now;
      benchmark->measurement_started = 1;
      benchmark->next_word_us = now;
      return LINK_BENCHMARK_ACTION_UPDATED;
    }
    if(benchmark->status == LINK_BENCHMARK_STOPPING)
      return link_benchmark_finish(benchmark,
       benchmark->terminal_status, now);
    return LINK_BENCHMARK_ACTION_NONE;
  }

  if(control->action == LINK_BENCHMARK_STOP &&
     control->run_id == benchmark->control.run_id &&
     link_benchmark_active(benchmark))
  {
    link_benchmark_control_copy(outbound, &benchmark->control,
     LINK_BENCHMARK_ACK);
    if(benchmark->role == LINK_BENCHMARK_ROLE_HOST)
    {
      benchmark->failure = LINK_BENCHMARK_FAILURE_PEER_ABORT;
      return LINK_BENCHMARK_ACTION_SEND_CONTROL |
       link_benchmark_finish(benchmark, LINK_BENCHMARK_ABORT, now);
    }
    return LINK_BENCHMARK_ACTION_SEND_CONTROL |
     link_benchmark_finish(benchmark,
      link_benchmark_result(benchmark, now), now);
  }
  if(control->action == LINK_BENCHMARK_STOP &&
     control->run_id == benchmark->control.run_id &&
     link_benchmark_terminal(benchmark))
  {
    link_benchmark_control_copy(outbound, &benchmark->control,
     LINK_BENCHMARK_ACK);
    return LINK_BENCHMARK_ACTION_SEND_CONTROL;
  }
  return LINK_BENCHMARK_ACTION_NONE;
}

uint32_t link_benchmark_poll(LinkBenchmark *benchmark, uint64_t now,
 LinkBenchmarkControl *outbound)
{
  uint64_t elapsed;

  if(benchmark == NULL || outbound == NULL)
    return LINK_BENCHMARK_ACTION_NONE;
  if(link_benchmark_active(benchmark))
    benchmark->observed_us = now;
  if(benchmark->status == LINK_BENCHMARK_WAIT_ACK ||
     benchmark->status == LINK_BENCHMARK_STOPPING)
  {
    if(now >= benchmark->state_started_us &&
       now - benchmark->state_started_us >= LINK_BENCHMARK_CONTROL_TIMEOUT_US)
    {
      link_benchmark_increment(&benchmark->timeout_count);
      benchmark->failure = LINK_BENCHMARK_FAILURE_TIMEOUT;
      return link_benchmark_finish(benchmark, LINK_BENCHMARK_FAIL, now);
    }
    return LINK_BENCHMARK_ACTION_NONE;
  }
  if(benchmark->status != LINK_BENCHMARK_RUNNING)
    return LINK_BENCHMARK_ACTION_NONE;

  /* A joiner has no pending_sequence while waiting for the next START.
     Heartbeats do not prove benchmark progress. */
  if((benchmark->pending_sequence != 0 && now >= benchmark->word_started_us &&
      now - benchmark->word_started_us >= LINK_BENCHMARK_WORD_TIMEOUT_US) ||
     (benchmark->role == LINK_BENCHMARK_ROLE_JOINER &&
      now >= benchmark->last_progress_us &&
      now - benchmark->last_progress_us >= LINK_BENCHMARK_WORD_TIMEOUT_US))
  {
    link_benchmark_increment(&benchmark->timeout_count);
    return link_benchmark_begin_stop(benchmark, LINK_BENCHMARK_FAIL,
     LINK_BENCHMARK_FAILURE_TIMEOUT, now, outbound);
  }
  elapsed = now >= benchmark->started_us ? now - benchmark->started_us : 0;
  if(benchmark->role == LINK_BENCHMARK_ROLE_HOST &&
     benchmark->pending_sequence == 0 &&
     elapsed >= (uint64_t)benchmark->control.duration_ms * 1000u)
  {
    return link_benchmark_begin_stop(benchmark,
     link_benchmark_result(benchmark, now),
     benchmark->failure, now, outbound);
  }
  return LINK_BENCHMARK_ACTION_NONE;
}

uint32_t link_benchmark_request_abort(LinkBenchmark *benchmark,
 uint64_t now, LinkBenchmarkControl *outbound)
{
  if(benchmark == NULL || outbound == NULL ||
     !link_benchmark_active(benchmark))
    return LINK_BENCHMARK_ACTION_NONE;
  return link_benchmark_begin_stop(benchmark, LINK_BENCHMARK_ABORT,
   LINK_BENCHMARK_FAILURE_LOCAL_ABORT, now, outbound);
}

uint32_t link_benchmark_fail(LinkBenchmark *benchmark,
 LinkBenchmarkFailure failure, uint64_t now)
{
  if(benchmark == NULL || !link_benchmark_active(benchmark))
    return LINK_BENCHMARK_ACTION_NONE;
  benchmark->failure = failure == LINK_BENCHMARK_FAILURE_NONE ?
   LINK_BENCHMARK_FAILURE_STREAM : failure;
  return link_benchmark_finish(benchmark,
   failure == LINK_BENCHMARK_FAILURE_LOCAL_ABORT ||
   failure == LINK_BENCHMARK_FAILURE_PEER_ABORT ? LINK_BENCHMARK_ABORT :
    LINK_BENCHMARK_FAIL, now);
}

uint32_t link_benchmark_host_word(uint32_t run_id, uint32_t seed,
 uint32_t sequence)
{
  return link_benchmark_mix(run_id ^ seed ^
   link_benchmark_mix(sequence + 0x9E3779B9u));
}

uint32_t link_benchmark_join_word(uint32_t run_id, uint32_t host_word,
 uint32_t sequence)
{
  return link_benchmark_mix(run_id ^ host_word ^ sequence ^ 0xA341316Cu);
}

uint32_t link_benchmark_host_begin_word(LinkBenchmark *benchmark,
 uint64_t now, uint32_t clock_cycle, LinkStreamStart *start,
 uint32_t *sequence)
{
  uint32_t actions;
  uint32_t next;

  if(benchmark == NULL || start == NULL || sequence == NULL ||
     benchmark->role != LINK_BENCHMARK_ROLE_HOST ||
     benchmark->status != LINK_BENCHMARK_RUNNING ||
     benchmark->pending_sequence != 0 || now < benchmark->next_word_us ||
     link_stream_state(&benchmark->stream) != LINK_STREAM_STATE_IDLE)
    return LINK_BENCHMARK_ACTION_NONE;

  next = link_benchmark_next_sequence(
   link_stream_last_committed(&benchmark->stream));
  memset(start, 0, sizeof(*start));
  start->generation = link_stream_generation(&benchmark->stream);
  start->start_cycle = clock_cycle;
  start->finish_cycle = clock_cycle + 1152u;
  start->clock_cycle = clock_cycle;
  start->acknowledged_sequence =
   link_stream_last_committed(&benchmark->stream);
  start->outgoing = link_benchmark_host_word(benchmark->control.run_id,
   benchmark->control.seed, next);
  start->identity = link_benchmark_mix(start->outgoing ^ next ^
   benchmark->control.run_id);
  if(start->identity == 0)
    start->identity = 1u;
  start->mode = 2u;
  start->setting = 3u;
  start->player_count = 2u;
  start->flags = LINK_STREAM_FLAG_CLOCK_OWNER;
  actions = link_stream_host_begin(&benchmark->stream, next, start);
  if(actions != LINK_STREAM_ACTION_SEND_START)
  {
    benchmark->failure = LINK_BENCHMARK_FAILURE_STREAM;
    return link_benchmark_finish(benchmark, LINK_BENCHMARK_FAIL, now);
  }
  benchmark->pending_sequence = next;
  benchmark->pending_host_word = start->outgoing;
  benchmark->word_started_us = now;
  if(benchmark->completed_words % benchmark->control.burst_words == 0)
    benchmark->burst_started_us = now;
  *sequence = next;
  return LINK_BENCHMARK_ACTION_SEND_START;
}

uint32_t link_benchmark_join_receive_start(LinkBenchmark *benchmark,
 uint32_t sequence, const LinkStreamStart *start, uint64_t now,
 uint32_t clock_cycle, LinkStreamReply *reply)
{
  uint32_t actions;
  uint32_t response;

  if(benchmark == NULL || start == NULL || reply == NULL ||
     benchmark->role != LINK_BENCHMARK_ROLE_JOINER ||
     benchmark->status != LINK_BENCHMARK_RUNNING)
    return LINK_BENCHMARK_ACTION_NONE;
  actions = link_stream_join_receive_start(&benchmark->stream, sequence,
   start, reply);
  if((actions & LINK_STREAM_ACTION_REPLAY_REPLY) != 0)
    return LINK_BENCHMARK_ACTION_SEND_REPLY | LINK_BENCHMARK_ACTION_REPLAY;
  if((actions & (LINK_STREAM_ACTION_CONFLICT | LINK_STREAM_ACTION_FUTURE)) !=
      0 || (actions & LINK_STREAM_ACTION_LATCH_JOIN) == 0 ||
     start->outgoing != link_benchmark_host_word(benchmark->control.run_id,
      benchmark->control.seed, sequence))
  {
    benchmark->failure = LINK_BENCHMARK_FAILURE_DIGEST_MISMATCH;
    return link_benchmark_finish(benchmark, LINK_BENCHMARK_FAIL, now);
  }
  response = link_benchmark_join_word(benchmark->control.run_id,
   start->outgoing, sequence);
  actions = link_stream_join_latched(&benchmark->stream, sequence, response,
   clock_cycle, reply);
  if((actions & (LINK_STREAM_ACTION_SEND_REPLY |
      LINK_STREAM_ACTION_APPLY_JOIN)) !=
     (LINK_STREAM_ACTION_SEND_REPLY | LINK_STREAM_ACTION_APPLY_JOIN) ||
     link_stream_note_applied(&benchmark->stream, sequence) !=
      LINK_STREAM_ACTION_COMMITTED)
  {
    benchmark->failure = LINK_BENCHMARK_FAILURE_STREAM;
    return link_benchmark_finish(benchmark, LINK_BENCHMARK_FAIL, now);
  }
  link_benchmark_note_completed(benchmark, sequence, start->outgoing,
   response, now, 0);
  return LINK_BENCHMARK_ACTION_SEND_REPLY | LINK_BENCHMARK_ACTION_UPDATED;
}

uint32_t link_benchmark_host_receive_reply(LinkBenchmark *benchmark,
 uint32_t sequence, const LinkStreamReply *reply, uint64_t now)
{
  uint32_t actions;
  uint32_t expected;
  uint64_t elapsed;

  if(benchmark == NULL || reply == NULL ||
     benchmark->role != LINK_BENCHMARK_ROLE_HOST ||
     benchmark->status != LINK_BENCHMARK_RUNNING)
    return LINK_BENCHMARK_ACTION_NONE;
  actions = link_stream_host_receive_reply(&benchmark->stream, sequence,
   reply);
  if((actions & LINK_STREAM_ACTION_DUPLICATE) != 0)
    return LINK_BENCHMARK_ACTION_NONE;
  expected = link_benchmark_join_word(benchmark->control.run_id,
   benchmark->pending_host_word, sequence);
  if((actions & LINK_STREAM_ACTION_APPLY_HOST) == 0 ||
     sequence != benchmark->pending_sequence || reply->outgoing != expected)
  {
    benchmark->failure = LINK_BENCHMARK_FAILURE_DIGEST_MISMATCH;
    return link_benchmark_finish(benchmark, LINK_BENCHMARK_FAIL, now);
  }
  if(link_stream_note_applied(&benchmark->stream, sequence) !=
     LINK_STREAM_ACTION_COMMITTED)
  {
    benchmark->failure = LINK_BENCHMARK_FAILURE_STREAM;
    return link_benchmark_finish(benchmark, LINK_BENCHMARK_FAIL, now);
  }
  link_benchmark_note_completed(benchmark, sequence,
   benchmark->pending_host_word, reply->outgoing, now, 1);
  benchmark->pending_sequence = 0;
  benchmark->pending_host_word = 0;
  elapsed = benchmark->completed_words == UINT_MAX ? UINT64_MAX :
   ((uint64_t)benchmark->completed_words * 1000000u) /
    benchmark->control.target_words_per_second;
  benchmark->next_word_us = elapsed == UINT64_MAX ? UINT64_MAX :
   benchmark->started_us + elapsed;
  return LINK_BENCHMARK_ACTION_UPDATED;
}

int link_benchmark_active(const LinkBenchmark *benchmark)
{
  return benchmark != NULL &&
   (benchmark->status == LINK_BENCHMARK_WAIT_ACK ||
    benchmark->status == LINK_BENCHMARK_RUNNING ||
    benchmark->status == LINK_BENCHMARK_STOPPING);
}

int link_benchmark_terminal(const LinkBenchmark *benchmark)
{
  return benchmark != NULL &&
   (benchmark->status == LINK_BENCHMARK_PASS ||
    benchmark->status == LINK_BENCHMARK_FAIL ||
    benchmark->status == LINK_BENCHMARK_ABORT);
}

uint32_t link_benchmark_summary_checksum(const LinkBenchmarkSummary *summary)
{
  uint32_t hash = 2166136261u;
  size_t count;
  size_t index;

  if(summary == NULL)
    return 0;
  /* Explicit wire order avoids pointer arithmetic across struct members. */
  const uint32_t values[] =
  {
    summary->run_id, summary->status, summary->failure, summary->role,
    summary->duration_ms, summary->completed_words, summary->words_per_second,
    summary->digest, summary->rtt_p50_us, summary->rtt_p95_us,
    summary->rtt_p99_us, summary->burst_p99_us, summary->burst_max_us,
    summary->duplicates, summary->replays, summary->stale_generations,
    summary->timeout_count
  };
  count = sizeof(values) / sizeof(values[0]);
  for(index = 0; index < count; index++)
  {
    uint32_t value = values[index];
    unsigned int byte;

    for(byte = 0; byte < 4u; byte++)
    {
      hash ^= value & 0xFFu;
      hash *= 16777619u;
      value >>= 8;
    }
  }
  return hash;
}

void link_benchmark_summary(const LinkBenchmark *benchmark,
 LinkBenchmarkSummary *summary)
{
  uint64_t elapsed;
  uint64_t rate;

  if(summary == NULL)
    return;
  memset(summary, 0, sizeof(*summary));
  if(benchmark == NULL)
    return;
  elapsed = benchmark->measurement_started &&
   benchmark->observed_us >= benchmark->started_us ?
    benchmark->observed_us - benchmark->started_us : 0;
  rate = elapsed == 0 ? 0 :
   ((uint64_t)benchmark->completed_words * 1000000u) / elapsed;
  summary->run_id = benchmark->control.run_id;
  summary->status = (uint32_t)benchmark->status;
  summary->failure = (uint32_t)benchmark->failure;
  summary->role = (uint32_t)benchmark->role;
  summary->duration_ms = (uint32_t)(elapsed / 1000u);
  summary->completed_words = benchmark->completed_words;
  summary->words_per_second = rate > UINT_MAX ? UINT_MAX : (uint32_t)rate;
  summary->digest = benchmark->digest;
  summary->rtt_p50_us = link_benchmark_percentile(
   benchmark->rtt_histogram, 50u);
  summary->rtt_p95_us = link_benchmark_percentile(
   benchmark->rtt_histogram, 95u);
  summary->rtt_p99_us = link_benchmark_percentile(
   benchmark->rtt_histogram, 99u);
  summary->burst_p99_us = link_benchmark_percentile(
   benchmark->burst_histogram, 99u);
  summary->burst_max_us = benchmark->burst_max_us;
  summary->duplicates = benchmark->stream.duplicate_count;
  summary->replays = benchmark->stream.replay_count;
  summary->stale_generations = benchmark->stream.stale_generation_count;
  summary->timeout_count = benchmark->timeout_count;
  summary->checksum = link_benchmark_summary_checksum(summary);
}
