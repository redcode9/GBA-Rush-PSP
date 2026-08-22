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

#ifndef PERF_METRICS_H
#define PERF_METRICS_H

#include <stdint.h>

#define PERF_METRICS_HISTOGRAM_BUCKETS 512U
#define PERF_METRICS_FRAME_BUCKET_US 250U
#define PERF_METRICS_GE_BUCKET_US 50U

typedef enum
{
  PERF_METRICS_IDLE,
  PERF_METRICS_ACTIVE,
  PERF_METRICS_STOPPED,
  PERF_METRICS_INVALIDATED
} PerfMetricsState;

typedef struct
{
  uint64_t buckets[PERF_METRICS_HISTOGRAM_BUCKETS];
  uint64_t sample_count;
  uint64_t total_us;
  uint32_t max_us;
  uint32_t bucket_width_us;
} PerfMetricsHistogram;

typedef struct
{
  PerfMetricsState state;
  uint64_t emulated_frames;
  uint64_t presented_frames;
  uint64_t skipped_frames;
  uint64_t fast_forward_frames;
  uint64_t fast_forward_presented_frames;
  uint64_t fast_forward_time_us;
  uint64_t fast_forward_transitions;
  uint64_t fast_forward_audio_fallbacks;
  uint64_t audio_recoveries;
  uint64_t audio_underruns;
  uint64_t dynarec_compilations;
  uint64_t dynarec_reuse_hits;
  uint64_t dynarec_retries;
  uint64_t dynarec_flushes;
  uint64_t dynarec_readonly_flushes;
  uint64_t dynarec_writable_flushes;
  uint64_t dynarec_cache_high_water;
  uint64_t dynarec_readonly_high_water;
  uint64_t dynarec_writable_high_water;
  uint64_t memory_low_water;
  int has_memory_sample;
  PerfMetricsHistogram frame_time;
  PerfMetricsHistogram ge_wait;
} PerfMetrics;

typedef struct
{
  PerfMetricsState state;
  uint64_t emulated_frames;
  uint64_t presented_frames;
  uint64_t skipped_frames;
  uint64_t fast_forward_frames;
  uint64_t fast_forward_presented_frames;
  uint64_t fast_forward_time_us;
  uint64_t fast_forward_transitions;
  uint64_t fast_forward_audio_fallbacks;
  uint64_t audio_recoveries;
  uint64_t audio_underruns;
  uint64_t dynarec_compilations;
  uint64_t dynarec_reuse_hits;
  uint64_t dynarec_retries;
  uint64_t dynarec_flushes;
  uint64_t dynarec_readonly_flushes;
  uint64_t dynarec_writable_flushes;
  uint64_t dynarec_cache_high_water;
  uint64_t dynarec_readonly_high_water;
  uint64_t dynarec_writable_high_water;
  uint64_t memory_low_water;
  int has_memory_sample;
  uint64_t frame_time_samples;
  uint64_t frame_time_total_us;
  uint32_t frame_time_max_us;
  uint32_t frame_time_p50_us;
  uint32_t frame_time_p95_us;
  uint32_t frame_time_p99_us;
  uint64_t ge_wait_samples;
  uint64_t ge_wait_total_us;
  uint32_t ge_wait_max_us;
  uint32_t ge_wait_p50_us;
  uint32_t ge_wait_p95_us;
  uint32_t ge_wait_p99_us;
} PerfMetricsSnapshot;

void perf_metrics_init(PerfMetrics *metrics);
void perf_metrics_reset(PerfMetrics *metrics);
int perf_metrics_start(PerfMetrics *metrics);
int perf_metrics_stop(PerfMetrics *metrics);
void perf_metrics_invalidate(PerfMetrics *metrics);
int perf_metrics_active(const PerfMetrics *metrics);
void perf_metrics_record_frame(PerfMetrics *metrics, uint32_t frame_time_us,
 uint32_t ge_wait_us, int skipped, int fast_forward);
void perf_metrics_record_emulated_frame(PerfMetrics *metrics,
 uint32_t frame_time_us, int fast_forward);
void perf_metrics_record_presented_frame(PerfMetrics *metrics,
 uint32_t ge_wait_us);
void perf_metrics_record_skipped_frame(PerfMetrics *metrics);
void perf_metrics_record_audio_recovery(PerfMetrics *metrics);
void perf_metrics_record_audio_underrun(PerfMetrics *metrics);
void perf_metrics_record_fast_forward_transition(PerfMetrics *metrics);
void perf_metrics_record_fast_forward_audio_fallback(PerfMetrics *metrics);
void perf_metrics_record_dynarec_compilation(PerfMetrics *metrics);
void perf_metrics_record_dynarec_reuse(PerfMetrics *metrics);
void perf_metrics_record_dynarec_retry(PerfMetrics *metrics);
void perf_metrics_record_dynarec_flush(PerfMetrics *metrics, int writable);
void perf_metrics_observe_dynarec_cache(PerfMetrics *metrics,
 uint64_t readonly_bytes, uint64_t writable_bytes);
void perf_metrics_observe_memory_available(PerfMetrics *metrics,
 uint64_t bytes);
void perf_metrics_snapshot(const PerfMetrics *metrics,
 PerfMetricsSnapshot *snapshot);

#endif /* PERF_METRICS_H */
