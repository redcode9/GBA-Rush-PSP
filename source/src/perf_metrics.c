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

#include "perf_metrics.h"

#include <stddef.h>
#include <string.h>

static uint64_t perf_metrics_saturating_add(uint64_t value, uint64_t amount)
{
  if(UINT64_MAX - value < amount)
    return UINT64_MAX;

  return value + amount;
}

static void perf_metrics_histogram_init(PerfMetricsHistogram *histogram,
 uint32_t bucket_width_us)
{
  memset(histogram, 0, sizeof(*histogram));
  histogram->bucket_width_us = bucket_width_us;
}

static void perf_metrics_histogram_record(PerfMetricsHistogram *histogram,
 uint32_t value_us)
{
  uint64_t bucket;

  if(value_us == 0)
    bucket = 0;
  else
    bucket = ((uint64_t)value_us + histogram->bucket_width_us - 1U) /
     histogram->bucket_width_us;

  if(bucket >= PERF_METRICS_HISTOGRAM_BUCKETS)
    bucket = PERF_METRICS_HISTOGRAM_BUCKETS - 1U;

  histogram->buckets[bucket] = perf_metrics_saturating_add(
   histogram->buckets[bucket], 1);
  histogram->sample_count = perf_metrics_saturating_add(
   histogram->sample_count, 1);
  histogram->total_us = perf_metrics_saturating_add(histogram->total_us,
   value_us);
  if(value_us > histogram->max_us)
    histogram->max_us = value_us;
}

static uint64_t perf_metrics_percentile_rank(uint64_t count,
 uint32_t percentile)
{
  uint64_t quotient;
  uint64_t remainder;

  if(count == 0 || percentile == 0)
    return 0;

  quotient = count / 100U;
  remainder = count % 100U;
  return quotient * percentile +
   ((remainder * percentile + 99U) / 100U);
}

static uint32_t perf_metrics_histogram_percentile(
 const PerfMetricsHistogram *histogram, uint32_t percentile)
{
  uint64_t rank;
  uint64_t cumulative = 0;
  uint32_t bucket;

  rank = perf_metrics_percentile_rank(histogram->sample_count, percentile);
  if(rank == 0)
    return 0;

  for(bucket = 0; bucket < PERF_METRICS_HISTOGRAM_BUCKETS; bucket++)
  {
    cumulative = perf_metrics_saturating_add(cumulative,
     histogram->buckets[bucket]);
    if(cumulative >= rank)
    {
      uint64_t upper_bound;

      if(bucket == 0)
        return 0;

      upper_bound = (uint64_t)bucket * histogram->bucket_width_us;
      if(bucket == PERF_METRICS_HISTOGRAM_BUCKETS - 1U &&
         histogram->max_us > upper_bound)
        return histogram->max_us;
      if(upper_bound > UINT32_MAX)
        return UINT32_MAX;
      return (uint32_t)upper_bound;
    }
  }

  return histogram->max_us;
}

void perf_metrics_init(PerfMetrics *metrics)
{
  if(metrics == NULL)
    return;

  memset(metrics, 0, sizeof(*metrics));
  metrics->state = PERF_METRICS_IDLE;
  perf_metrics_histogram_init(&metrics->frame_time,
   PERF_METRICS_FRAME_BUCKET_US);
  perf_metrics_histogram_init(&metrics->ge_wait,
   PERF_METRICS_GE_BUCKET_US);
}

void perf_metrics_reset(PerfMetrics *metrics)
{
  perf_metrics_init(metrics);
}

int perf_metrics_start(PerfMetrics *metrics)
{
  if(metrics == NULL || metrics->state == PERF_METRICS_ACTIVE)
    return 0;

  perf_metrics_reset(metrics);
  metrics->state = PERF_METRICS_ACTIVE;
  return 1;
}

int perf_metrics_stop(PerfMetrics *metrics)
{
  if(metrics == NULL || metrics->state != PERF_METRICS_ACTIVE)
    return 0;

  metrics->state = PERF_METRICS_STOPPED;
  return 1;
}

void perf_metrics_invalidate(PerfMetrics *metrics)
{
  if(metrics != NULL && metrics->state == PERF_METRICS_ACTIVE)
    metrics->state = PERF_METRICS_INVALIDATED;
}

int perf_metrics_active(const PerfMetrics *metrics)
{
  return metrics != NULL && metrics->state == PERF_METRICS_ACTIVE;
}

void perf_metrics_record_emulated_frame(PerfMetrics *metrics,
 uint32_t frame_time_us, int fast_forward)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->emulated_frames = perf_metrics_saturating_add(
   metrics->emulated_frames, 1);
  if(fast_forward)
    metrics->fast_forward_frames = perf_metrics_saturating_add(
     metrics->fast_forward_frames, 1);
  perf_metrics_histogram_record(&metrics->frame_time, frame_time_us);
}

void perf_metrics_record_presented_frame(PerfMetrics *metrics,
 uint32_t ge_wait_us)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->presented_frames = perf_metrics_saturating_add(
   metrics->presented_frames, 1);
  perf_metrics_histogram_record(&metrics->ge_wait, ge_wait_us);
}

void perf_metrics_record_skipped_frame(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->skipped_frames = perf_metrics_saturating_add(
     metrics->skipped_frames, 1);
}

void perf_metrics_record_audio_recovery(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->audio_recoveries = perf_metrics_saturating_add(
     metrics->audio_recoveries, 1);
}

void perf_metrics_record_audio_underrun(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->audio_underruns = perf_metrics_saturating_add(
     metrics->audio_underruns, 1);
}

void perf_metrics_record_dynarec_compilation(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->dynarec_compilations = perf_metrics_saturating_add(
     metrics->dynarec_compilations, 1);
}

void perf_metrics_record_dynarec_flush(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->dynarec_flushes = perf_metrics_saturating_add(
     metrics->dynarec_flushes, 1);
}

void perf_metrics_observe_dynarec_cache(PerfMetrics *metrics, uint64_t bytes)
{
  if(perf_metrics_active(metrics) &&
     bytes > metrics->dynarec_cache_high_water)
    metrics->dynarec_cache_high_water = bytes;
}

void perf_metrics_observe_memory_available(PerfMetrics *metrics,
 uint64_t bytes)
{
  if(!perf_metrics_active(metrics))
    return;

  if(!metrics->has_memory_sample || bytes < metrics->memory_low_water)
    metrics->memory_low_water = bytes;
  metrics->has_memory_sample = 1;
}

void perf_metrics_snapshot(const PerfMetrics *metrics,
 PerfMetricsSnapshot *snapshot)
{
  if(snapshot == NULL)
    return;

  memset(snapshot, 0, sizeof(*snapshot));
  if(metrics == NULL)
    return;

  snapshot->state = metrics->state;
  snapshot->emulated_frames = metrics->emulated_frames;
  snapshot->presented_frames = metrics->presented_frames;
  snapshot->skipped_frames = metrics->skipped_frames;
  snapshot->fast_forward_frames = metrics->fast_forward_frames;
  snapshot->audio_recoveries = metrics->audio_recoveries;
  snapshot->audio_underruns = metrics->audio_underruns;
  snapshot->dynarec_compilations = metrics->dynarec_compilations;
  snapshot->dynarec_flushes = metrics->dynarec_flushes;
  snapshot->dynarec_cache_high_water = metrics->dynarec_cache_high_water;
  snapshot->memory_low_water = metrics->memory_low_water;
  snapshot->has_memory_sample = metrics->has_memory_sample;

  snapshot->frame_time_samples = metrics->frame_time.sample_count;
  snapshot->frame_time_total_us = metrics->frame_time.total_us;
  snapshot->frame_time_max_us = metrics->frame_time.max_us;
  snapshot->frame_time_p50_us = perf_metrics_histogram_percentile(
   &metrics->frame_time, 50);
  snapshot->frame_time_p95_us = perf_metrics_histogram_percentile(
   &metrics->frame_time, 95);
  snapshot->frame_time_p99_us = perf_metrics_histogram_percentile(
   &metrics->frame_time, 99);

  snapshot->ge_wait_samples = metrics->ge_wait.sample_count;
  snapshot->ge_wait_total_us = metrics->ge_wait.total_us;
  snapshot->ge_wait_max_us = metrics->ge_wait.max_us;
  snapshot->ge_wait_p50_us = perf_metrics_histogram_percentile(
   &metrics->ge_wait, 50);
  snapshot->ge_wait_p95_us = perf_metrics_histogram_percentile(
   &metrics->ge_wait, 95);
  snapshot->ge_wait_p99_us = perf_metrics_histogram_percentile(
   &metrics->ge_wait, 99);
}
