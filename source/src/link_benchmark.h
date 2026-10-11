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

#ifndef LINK_BENCHMARK_H
#define LINK_BENCHMARK_H

#include <stdint.h>

#include "link_protocol.h"
#include "link_stream.h"

#define LINK_BENCHMARK_DEFAULT_DURATION_MS 600000u
#define LINK_BENCHMARK_DEFAULT_WORDS_PER_SECOND 1000u
#define LINK_BENCHMARK_MINIMUM_WORDS_PER_SECOND 600u
#define LINK_BENCHMARK_DEFAULT_BURST_WORDS 9u
#define LINK_BENCHMARK_DEFAULT_MAXIMUM_BURST_US 20000u
#define LINK_BENCHMARK_CONTROL_TIMEOUT_US 5000000u
#define LINK_BENCHMARK_WORD_TIMEOUT_US 2000000u
#define LINK_BENCHMARK_HISTOGRAM_BUCKETS 128u
#define LINK_BENCHMARK_HISTOGRAM_QUANTUM_US 250u

typedef enum
{
  LINK_BENCHMARK_ROLE_NONE,
  LINK_BENCHMARK_ROLE_HOST,
  LINK_BENCHMARK_ROLE_JOINER
} LinkBenchmarkRole;

typedef enum
{
  LINK_BENCHMARK_IDLE,
  LINK_BENCHMARK_WAIT_ACK,
  LINK_BENCHMARK_RUNNING,
  LINK_BENCHMARK_STOPPING,
  LINK_BENCHMARK_PASS,
  LINK_BENCHMARK_FAIL,
  LINK_BENCHMARK_ABORT
} LinkBenchmarkStatus;

typedef enum
{
  LINK_BENCHMARK_FAILURE_NONE,
  LINK_BENCHMARK_FAILURE_INVALID_CONTROL,
  LINK_BENCHMARK_FAILURE_DIGEST_MISMATCH,
  LINK_BENCHMARK_FAILURE_PEER_ABORT,
  LINK_BENCHMARK_FAILURE_TIMEOUT,
  LINK_BENCHMARK_FAILURE_STREAM,
  LINK_BENCHMARK_FAILURE_THRESHOLD,
  LINK_BENCHMARK_FAILURE_LOCAL_ABORT
} LinkBenchmarkFailure;

typedef enum
{
  LINK_BENCHMARK_ACTION_NONE = 0,
  LINK_BENCHMARK_ACTION_SEND_CONTROL = 1u << 0,
  LINK_BENCHMARK_ACTION_SEND_START = 1u << 1,
  LINK_BENCHMARK_ACTION_SEND_REPLY = 1u << 2,
  LINK_BENCHMARK_ACTION_UPDATED = 1u << 3,
  LINK_BENCHMARK_ACTION_TERMINAL = 1u << 4,
  LINK_BENCHMARK_ACTION_REPLAY = 1u << 5
} LinkBenchmarkEvent;

typedef struct
{
  uint32_t run_id;
  uint32_t status;
  uint32_t failure;
  uint32_t role;
  uint32_t duration_ms;
  uint32_t completed_words;
  uint32_t words_per_second;
  uint32_t digest;
  uint32_t rtt_p50_us;
  uint32_t rtt_p95_us;
  uint32_t rtt_p99_us;
  uint32_t burst_p99_us;
  uint32_t burst_max_us;
  uint32_t duplicates;
  uint32_t replays;
  uint32_t stale_generations;
  uint32_t timeout_count;
  uint32_t checksum;
} LinkBenchmarkSummary;

typedef struct
{
  LinkStream stream;
  LinkBenchmarkControl control;
  LinkBenchmarkRole role;
  LinkBenchmarkStatus status;
  LinkBenchmarkStatus terminal_status;
  LinkBenchmarkFailure failure;
  uint64_t state_started_us;
  uint64_t started_us;
  uint64_t finished_us;
  uint64_t word_started_us;
  uint64_t burst_started_us;
  uint64_t next_word_us;
  uint64_t last_progress_us;
  uint64_t observed_us;
  uint32_t pending_sequence;
  uint32_t pending_host_word;
  uint32_t completed_words;
  uint32_t digest;
  uint32_t burst_max_us;
  uint32_t timeout_count;
  uint32_t rtt_histogram[LINK_BENCHMARK_HISTOGRAM_BUCKETS];
  uint32_t burst_histogram[LINK_BENCHMARK_HISTOGRAM_BUCKETS];
  uint8_t measurement_started;
} LinkBenchmark;

void link_benchmark_init(LinkBenchmark *benchmark);
void link_benchmark_default_control(LinkBenchmarkControl *control,
 uint32_t run_id, uint32_t seed);
int link_benchmark_host_start(LinkBenchmark *benchmark,
 const LinkBenchmarkControl *control, uint32_t generation, uint64_t now,
 LinkBenchmarkControl *outbound);
uint32_t link_benchmark_receive_control(LinkBenchmark *benchmark,
 LinkBenchmarkRole local_role, const LinkBenchmarkControl *control,
 uint32_t generation, uint64_t now, LinkBenchmarkControl *outbound);
uint32_t link_benchmark_poll(LinkBenchmark *benchmark, uint64_t now,
 LinkBenchmarkControl *outbound);
uint32_t link_benchmark_request_abort(LinkBenchmark *benchmark,
 uint64_t now, LinkBenchmarkControl *outbound);
uint32_t link_benchmark_fail(LinkBenchmark *benchmark,
 LinkBenchmarkFailure failure, uint64_t now);
uint32_t link_benchmark_host_begin_word(LinkBenchmark *benchmark,
 uint64_t now, uint32_t clock_cycle, LinkStreamStart *start,
 uint32_t *sequence);
uint32_t link_benchmark_join_receive_start(LinkBenchmark *benchmark,
 uint32_t sequence, const LinkStreamStart *start, uint64_t now,
 uint32_t clock_cycle, LinkStreamReply *reply);
uint32_t link_benchmark_host_receive_reply(LinkBenchmark *benchmark,
 uint32_t sequence, const LinkStreamReply *reply, uint64_t now);
int link_benchmark_active(const LinkBenchmark *benchmark);
int link_benchmark_terminal(const LinkBenchmark *benchmark);
uint32_t link_benchmark_host_word(uint32_t run_id, uint32_t seed,
 uint32_t sequence);
uint32_t link_benchmark_join_word(uint32_t run_id, uint32_t host_word,
 uint32_t sequence);
void link_benchmark_summary(const LinkBenchmark *benchmark,
 LinkBenchmarkSummary *summary);
uint32_t link_benchmark_summary_checksum(const LinkBenchmarkSummary *summary);

#endif /* LINK_BENCHMARK_H */
