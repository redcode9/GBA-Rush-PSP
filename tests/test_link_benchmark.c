#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_benchmark.h"

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

typedef struct
{
  LinkBenchmark host;
  LinkBenchmark joiner;
  LinkBenchmarkControl config;
  uint64_t now;
} BenchmarkPair;

static void start_pair(BenchmarkPair *pair, uint32_t duration_ms,
 uint32_t target, uint32_t maximum_burst_us)
{
  LinkBenchmarkControl outbound;
  LinkBenchmarkControl ack;
  uint32_t actions;

  memset(pair, 0, sizeof(*pair));
  link_benchmark_init(&pair->host);
  link_benchmark_init(&pair->joiner);
  link_benchmark_default_control(&pair->config, 0x12345678u,
   0x9ABCDEF0u);
  pair->config.duration_ms = duration_ms;
  pair->config.target_words_per_second = target;
  pair->config.maximum_burst_us = maximum_burst_us;
  CHECK(link_benchmark_host_start(&pair->host, &pair->config, 7u,
   pair->now, &outbound));
  actions = link_benchmark_receive_control(&pair->joiner,
   LINK_BENCHMARK_ROLE_JOINER, &outbound, 7u, pair->now, &ack);
  CHECK((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0);
  CHECK(ack.action == LINK_BENCHMARK_ACK);
  actions = link_benchmark_receive_control(&pair->host,
   LINK_BENCHMARK_ROLE_HOST, &ack, 7u, pair->now, &outbound);
  CHECK((actions & LINK_BENCHMARK_ACTION_UPDATED) != 0);
  CHECK(pair->host.status == LINK_BENCHMARK_RUNNING);
  CHECK(pair->joiner.status == LINK_BENCHMARK_RUNNING);
}

static void transfer_word(BenchmarkPair *pair, uint32_t latency_us,
 int corrupt_reply)
{
  LinkStreamStart start;
  LinkStreamReply reply;
  uint32_t sequence = 0;
  uint32_t actions;

  actions = link_benchmark_host_begin_word(&pair->host, pair->now,
   (uint32_t)pair->now, &start, &sequence);
  CHECK((actions & LINK_BENCHMARK_ACTION_SEND_START) != 0);
  CHECK(sequence != 0);
  pair->now += latency_us / 2u;
  actions = link_benchmark_join_receive_start(&pair->joiner, sequence,
   &start, pair->now, (uint32_t)pair->now, &reply);
  CHECK((actions & LINK_BENCHMARK_ACTION_SEND_REPLY) != 0);
  if(corrupt_reply)
    reply.outgoing ^= 1u;
  pair->now += latency_us - latency_us / 2u;
  actions = link_benchmark_host_receive_reply(&pair->host, sequence,
   &reply, pair->now);
  CHECK((actions & LINK_BENCHMARK_ACTION_UPDATED) != 0);
}

static void stop_pair(BenchmarkPair *pair)
{
  LinkBenchmarkControl stop;
  LinkBenchmarkControl ack;
  uint32_t actions;

  actions = link_benchmark_poll(&pair->host, pair->now, &stop);
  CHECK((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0);
  CHECK(stop.action == LINK_BENCHMARK_STOP);
  actions = link_benchmark_receive_control(&pair->joiner,
   LINK_BENCHMARK_ROLE_JOINER, &stop, 7u, pair->now, &ack);
  CHECK((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0);
  CHECK((actions & LINK_BENCHMARK_ACTION_TERMINAL) != 0);
  actions = link_benchmark_receive_control(&pair->host,
   LINK_BENCHMARK_ROLE_HOST, &ack, 7u, pair->now, &stop);
  CHECK((actions & LINK_BENCHMARK_ACTION_TERMINAL) != 0);
}

static void test_deterministic_success_and_summary(void)
{
  BenchmarkPair pair;
  LinkBenchmarkSummary host;
  LinkBenchmarkSummary joiner;
  uint32_t index;
  uint32_t first_word;

  start_pair(&pair, 18u, 1000u, 20000u);
  first_word = link_benchmark_host_word(pair.config.run_id,
   pair.config.seed, 1u);
  CHECK(first_word != link_benchmark_host_word(pair.config.run_id,
   pair.config.seed, 2u));
  for(index = 0; index < 18u; index++)
    transfer_word(&pair, 1000u, 0);
  stop_pair(&pair);

  link_benchmark_summary(&pair.host, &host);
  link_benchmark_summary(&pair.joiner, &joiner);
  CHECK(host.status == LINK_BENCHMARK_PASS);
  CHECK(joiner.status == LINK_BENCHMARK_PASS);
  CHECK(host.completed_words == 18u);
  CHECK(host.completed_words == joiner.completed_words);
  CHECK(host.digest == joiner.digest);
  CHECK(host.words_per_second == 1000u);
  CHECK(host.rtt_p99_us == 1000u);
  CHECK(host.burst_p99_us == 9000u);
  CHECK(host.checksum == link_benchmark_summary_checksum(&host));
  CHECK(joiner.checksum == link_benchmark_summary_checksum(&joiner));
}

static void test_digest_mismatch_fails(void)
{
  BenchmarkPair pair;

  start_pair(&pair, 100u, 100u, 20000u);
  transfer_word(&pair, 1000u, 1);
  CHECK(pair.host.status == LINK_BENCHMARK_FAIL);
  CHECK(pair.host.failure == LINK_BENCHMARK_FAILURE_DIGEST_MISMATCH);
}

static void test_peer_abort_is_distinct(void)
{
  BenchmarkPair pair;
  LinkBenchmarkControl stop;
  LinkBenchmarkControl ack;
  uint32_t actions;

  start_pair(&pair, 100u, 100u, 20000u);
  actions = link_benchmark_request_abort(&pair.joiner, pair.now, &stop);
  CHECK((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0);
  actions = link_benchmark_receive_control(&pair.host,
   LINK_BENCHMARK_ROLE_HOST, &stop, 7u, pair.now, &ack);
  CHECK((actions & LINK_BENCHMARK_ACTION_TERMINAL) != 0);
  CHECK(pair.host.status == LINK_BENCHMARK_ABORT);
  CHECK(pair.host.failure == LINK_BENCHMARK_FAILURE_PEER_ABORT);
}

static void test_word_and_control_timeouts(void)
{
  BenchmarkPair pair;
  LinkStreamStart start;
  LinkBenchmarkControl control;
  uint32_t sequence;
  uint32_t actions;

  start_pair(&pair, 10000u, 100u, 20000u);
  CHECK((link_benchmark_host_begin_word(&pair.host, pair.now, 0,
    &start, &sequence) & LINK_BENCHMARK_ACTION_SEND_START) != 0);
  pair.now += LINK_BENCHMARK_WORD_TIMEOUT_US;
  actions = link_benchmark_poll(&pair.host, pair.now, &control);
  CHECK((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0);
  CHECK(pair.host.failure == LINK_BENCHMARK_FAILURE_TIMEOUT);
  pair.now += LINK_BENCHMARK_CONTROL_TIMEOUT_US;
  actions = link_benchmark_poll(&pair.host, pair.now, &control);
  CHECK((actions & LINK_BENCHMARK_ACTION_TERMINAL) != 0);
  CHECK(pair.host.status == LINK_BENCHMARK_FAIL);
  CHECK(pair.host.timeout_count == 2u);
}

static void test_counter_saturation(void)
{
  BenchmarkPair pair;

  start_pair(&pair, 100u, 1u, 20000u);
  pair.host.completed_words = UINT_MAX;
  transfer_word(&pair, 1000u, 0);
  CHECK(pair.host.completed_words == UINT_MAX);
}

static void test_threshold_boundary(void)
{
  BenchmarkPair pair;
  LinkBenchmarkSummary summary;
  uint32_t index;

  start_pair(&pair, 9u, 1000u, 9000u);
  for(index = 0; index < 9u; index++)
    transfer_word(&pair, 1000u, 0);
  stop_pair(&pair);
  link_benchmark_summary(&pair.host, &summary);
  CHECK(summary.words_per_second == 1000u);
  CHECK(summary.burst_p99_us == 9000u);
  CHECK(summary.status == LINK_BENCHMARK_PASS);
}

static void test_terminal_control_replay_is_idempotent(void)
{
  BenchmarkPair pair;
  LinkBenchmarkControl start;
  LinkBenchmarkControl stop;
  LinkBenchmarkControl ack;
  LinkBenchmarkSummary before;
  LinkBenchmarkSummary after;
  uint32_t actions;
  uint32_t index;

  start_pair(&pair, 9u, 1000u, 9000u);
  for(index = 0; index < 9u; index++)
    transfer_word(&pair, 1000u, 0);
  stop_pair(&pair);
  link_benchmark_summary(&pair.joiner, &before);

  start = pair.config;
  start.action = LINK_BENCHMARK_START;
  actions = link_benchmark_receive_control(&pair.joiner,
   LINK_BENCHMARK_ROLE_JOINER, &start, 7u, pair.now, &ack);
  CHECK(actions == LINK_BENCHMARK_ACTION_SEND_CONTROL);
  CHECK(ack.action == LINK_BENCHMARK_ACK);
  link_benchmark_summary(&pair.joiner, &after);
  CHECK(memcmp(&before, &after, sizeof(before)) == 0);

  memset(&stop, 0, sizeof(stop));
  stop.run_id = pair.config.run_id;
  stop.action = LINK_BENCHMARK_STOP;
  actions = link_benchmark_receive_control(&pair.joiner,
   LINK_BENCHMARK_ROLE_JOINER, &stop, 7u, pair.now, &ack);
  CHECK(actions == LINK_BENCHMARK_ACTION_SEND_CONTROL);
  CHECK(ack.action == LINK_BENCHMARK_ACK);
  link_benchmark_summary(&pair.joiner, &after);
  CHECK(memcmp(&before, &after, sizeof(before)) == 0);
}

static void test_live_summary_uses_measurement_time_not_session_uptime(void)
{
  BenchmarkPair pair;
  LinkBenchmarkControl ignored;
  LinkBenchmarkSummary summary;

  start_pair(&pair, 600000u, 1000u, 20000u);
  transfer_word(&pair, 1000u, 0);
  link_benchmark_poll(&pair.host, pair.now, &ignored);
  link_benchmark_summary(&pair.host, &summary);
  CHECK(summary.duration_ms == 1u);
  CHECK(summary.words_per_second == 1000u);
  CHECK(summary.completed_words == 1u);
  pair.now += 5000u;
  link_benchmark_poll(&pair.host, pair.now, &ignored);
  link_benchmark_summary(&pair.host, &summary);
  CHECK(summary.duration_ms == 6u);
  CHECK(summary.words_per_second == 166u);
}

static void test_acceptance_floor_is_not_offered_load(void)
{
  BenchmarkPair pair;
  uint32_t index;

  start_pair(&pair, 27u, 1000u, 20000u);
  for(index = 0; index < 18u; index++)
    transfer_word(&pair, 1500u, 0);
  stop_pair(&pair);
  CHECK(pair.host.status == LINK_BENCHMARK_PASS);
  CHECK(pair.joiner.status == LINK_BENCHMARK_PASS);
}

static void test_unbounded_histogram_tail_cannot_report_fast_bursts(void)
{
  BenchmarkPair pair;
  LinkBenchmarkSummary summary;
  uint32_t index;

  start_pair(&pair, 90u, 1000u, 20000u);
  for(index = 0; index < 9u; index++)
    transfer_word(&pair, 10000u, 0);
  stop_pair(&pair);
  link_benchmark_summary(&pair.host, &summary);
  CHECK(summary.burst_p99_us == UINT_MAX);
  CHECK(summary.burst_max_us == 90000u);
  CHECK(summary.status == LINK_BENCHMARK_FAIL);
}

static void test_joiner_times_out_before_first_word_and_terminal_is_stable(void)
{
  BenchmarkPair pair;
  LinkBenchmarkControl outbound;
  LinkBenchmarkSummary before;
  LinkBenchmarkSummary after;

  start_pair(&pair, 600000u, 1000u, 20000u);
  pair.now += LINK_BENCHMARK_WORD_TIMEOUT_US;
  CHECK(link_benchmark_poll(&pair.joiner, pair.now, &outbound) &
   LINK_BENCHMARK_ACTION_SEND_CONTROL);
  CHECK(pair.joiner.failure == LINK_BENCHMARK_FAILURE_TIMEOUT);
  pair.now += LINK_BENCHMARK_CONTROL_TIMEOUT_US;
  CHECK(link_benchmark_poll(&pair.joiner, pair.now, &outbound) &
   LINK_BENCHMARK_ACTION_TERMINAL);
  link_benchmark_summary(&pair.joiner, &before);
  pair.now += LINK_BENCHMARK_CONTROL_TIMEOUT_US;
  CHECK(link_benchmark_poll(&pair.joiner, pair.now, &outbound) == 0);
  CHECK(link_benchmark_fail(&pair.joiner, LINK_BENCHMARK_FAILURE_STREAM,
   pair.now) == 0);
  link_benchmark_summary(&pair.joiner, &after);
  CHECK(memcmp(&before, &after, sizeof(before)) == 0);
}

int main(void)
{
  test_deterministic_success_and_summary();
  test_digest_mismatch_fails();
  test_peer_abort_is_distinct();
  test_word_and_control_timeouts();
  test_counter_saturation();
  test_threshold_boundary();
  test_terminal_control_replay_is_idempotent();
  test_live_summary_uses_measurement_time_not_session_uptime();
  test_acceptance_floor_is_not_offered_load();
  test_unbounded_histogram_tail_cannot_report_fast_bursts();
  test_joiner_times_out_before_first_word_and_terminal_is_stable();

  if(failures)
  {
    fprintf(stderr, "%d link benchmark test(s) failed\n", failures);
    return 1;
  }
  puts("Link benchmark tests passed");
  return 0;
}
