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

/* Bucket 0 holds exact zeros; bucket n holds values in ((n-1)*w, n*w]. */
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

/* Nearest-rank: ceil(count * percentile / 100), split so it cannot
   overflow. */
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

/* Upper bound of the bucket holding the percentile; the true maximum if it
   falls in the open-ended last bucket. */
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

/* A whole frame in one call: emulated, then skipped or presented. Only
   presented frames sample the GE wait. */
void perf_metrics_record_frame(PerfMetrics *metrics, uint32_t frame_time_us,
 uint32_t ge_wait_us, int skipped, int fast_forward)
{
  if(!perf_metrics_active(metrics))
    return;

  perf_metrics_record_emulated_frame(metrics, frame_time_us, fast_forward);

  if(skipped)
  {
    metrics->skipped_frames = perf_metrics_saturating_add(
     metrics->skipped_frames, 1);
  }
  else
  {
    metrics->presented_frames = perf_metrics_saturating_add(
     metrics->presented_frames, 1);
    if(fast_forward)
      metrics->fast_forward_presented_frames = perf_metrics_saturating_add(
       metrics->fast_forward_presented_frames, 1);
    perf_metrics_histogram_record(&metrics->ge_wait, ge_wait_us);
  }
}

void perf_metrics_record_emulated_frame(PerfMetrics *metrics,
 uint32_t frame_time_us, int fast_forward)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->emulated_frames = perf_metrics_saturating_add(
   metrics->emulated_frames, 1);
  if(fast_forward)
  {
    metrics->fast_forward_frames = perf_metrics_saturating_add(
     metrics->fast_forward_frames, 1);
    metrics->fast_forward_time_us = perf_metrics_saturating_add(
     metrics->fast_forward_time_us, frame_time_us);
  }
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

void perf_metrics_record_fast_forward_transition(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->fast_forward_transitions = perf_metrics_saturating_add(
     metrics->fast_forward_transitions, 1);
}

void perf_metrics_record_fast_forward_audio_fallback(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->fast_forward_audio_fallbacks = perf_metrics_saturating_add(
     metrics->fast_forward_audio_fallbacks, 1);
}

void perf_metrics_record_dynarec_compilation(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->dynarec_compilations = perf_metrics_saturating_add(
     metrics->dynarec_compilations, 1);
}

void perf_metrics_record_dynarec_reuse(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->dynarec_reuse_hits = perf_metrics_saturating_add(
     metrics->dynarec_reuse_hits, 1);
}

void perf_metrics_record_dynarec_retry(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->dynarec_retries = perf_metrics_saturating_add(
     metrics->dynarec_retries, 1);
}

void perf_metrics_record_dynarec_flush(PerfMetrics *metrics, int writable)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->dynarec_flushes = perf_metrics_saturating_add(
   metrics->dynarec_flushes, 1);
  if(writable)
    metrics->dynarec_writable_flushes = perf_metrics_saturating_add(
     metrics->dynarec_writable_flushes, 1);
  else
    metrics->dynarec_readonly_flushes = perf_metrics_saturating_add(
     metrics->dynarec_readonly_flushes, 1);
}

void perf_metrics_observe_dynarec_cache(PerfMetrics *metrics,
 uint64_t readonly_bytes, uint64_t writable_bytes)
{
  uint64_t total_bytes;

  if(!perf_metrics_active(metrics))
    return;

  total_bytes = perf_metrics_saturating_add(readonly_bytes, writable_bytes);
  if(total_bytes > metrics->dynarec_cache_high_water)
    metrics->dynarec_cache_high_water = total_bytes;
  if(readonly_bytes > metrics->dynarec_readonly_high_water)
    metrics->dynarec_readonly_high_water = readonly_bytes;
  if(writable_bytes > metrics->dynarec_writable_high_water)
    metrics->dynarec_writable_high_water = writable_bytes;
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

void perf_metrics_record_wait(PerfMetrics *metrics,
 PerfMetricsWaitSite site, uint32_t wait_us)
{
  uint64_t *count;
  uint64_t *total;

  if(!perf_metrics_active(metrics))
    return;

  switch(site)
  {
    case PERF_METRICS_WAIT_BACKPRESSURE:
      count = &metrics->vblank_backpressure_count;
      total = &metrics->vblank_backpressure_total_us;
      break;
    case PERF_METRICS_WAIT_PRESENT:
      count = &metrics->vblank_present_count;
      total = &metrics->vblank_present_total_us;
      break;
    case PERF_METRICS_WAIT_AUDIO:
      count = &metrics->vblank_audio_count;
      total = &metrics->vblank_audio_total_us;
      break;
    default:
      return;
  }

  *count = perf_metrics_saturating_add(*count, 1);
  *total = perf_metrics_saturating_add(*total, wait_us);
}

void perf_metrics_record_renderer_frame(PerfMetrics *metrics,
 uint32_t renderer_us)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->renderer_total_us = perf_metrics_saturating_add(
   metrics->renderer_total_us, renderer_us);
  if(renderer_us > metrics->renderer_max_us)
    metrics->renderer_max_us = renderer_us;
}

void perf_metrics_record_dcache_writeback(PerfMetrics *metrics,
 uint32_t wait_us)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->dcache_writeback_count = perf_metrics_saturating_add(
   metrics->dcache_writeback_count, 1);
  metrics->dcache_writeback_total_us = perf_metrics_saturating_add(
   metrics->dcache_writeback_total_us, wait_us);
}

void perf_metrics_record_ge_sync_direct(PerfMetrics *metrics,
 uint32_t wait_us)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->ge_sync_direct_count = perf_metrics_saturating_add(
   metrics->ge_sync_direct_count, 1);
  metrics->ge_sync_direct_total_us = perf_metrics_saturating_add(
   metrics->ge_sync_direct_total_us, wait_us);
}

void perf_metrics_record_quality_presented(PerfMetrics *metrics,
 int sharp)
{
  if(!perf_metrics_active(metrics))
    return;

  if(sharp)
    metrics->sharp_presented_frames = perf_metrics_saturating_add(
     metrics->sharp_presented_frames, 1);
  else
    metrics->crisp_presented_frames = perf_metrics_saturating_add(
     metrics->crisp_presented_frames, 1);
}

void perf_metrics_record_gba_update_entry(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->gba_update_entries = perf_metrics_saturating_add(
     metrics->gba_update_entries, 1);
}

void perf_metrics_record_gba_update_iteration(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->gba_update_iterations = perf_metrics_saturating_add(
     metrics->gba_update_iterations, 1);
}

/* gamepak_pair: source and destination both lie in cartridge space
   (0x08000000-0x0DFFFFFF). */
void perf_metrics_record_dma(PerfMetrics *metrics, int bits32,
 uint32_t elements, uint32_t dma_us, int gamepak_pair)
{
  if(!perf_metrics_active(metrics))
    return;

  uint64_t bytes = (uint64_t)elements * (bits32 ? 4U : 2U);

  if(bits32)
  {
    metrics->dma32_transfers = perf_metrics_saturating_add(
     metrics->dma32_transfers, 1);
    metrics->dma32_elements = perf_metrics_saturating_add(
     metrics->dma32_elements, elements);
    metrics->dma32_bytes = perf_metrics_saturating_add(
     metrics->dma32_bytes, bytes);
    metrics->dma32_total_us = perf_metrics_saturating_add(
     metrics->dma32_total_us, dma_us);
  }
  else
  {
    metrics->dma16_transfers = perf_metrics_saturating_add(
     metrics->dma16_transfers, 1);
    metrics->dma16_elements = perf_metrics_saturating_add(
     metrics->dma16_elements, elements);
    metrics->dma16_bytes = perf_metrics_saturating_add(
     metrics->dma16_bytes, bytes);
    metrics->dma16_total_us = perf_metrics_saturating_add(
     metrics->dma16_total_us, dma_us);
  }

  if(gamepak_pair)
    metrics->dma_gamepak_pairs = perf_metrics_saturating_add(
     metrics->dma_gamepak_pairs, 1);
}

void perf_metrics_record_gamepak_evict_scan(PerfMetrics *metrics,
 uint32_t us)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->gamepak_evict_scan_count = perf_metrics_saturating_add(
   metrics->gamepak_evict_scan_count, 1);
  metrics->gamepak_evict_scan_total_us = perf_metrics_saturating_add(
   metrics->gamepak_evict_scan_total_us, us);
}

/* One ROM page cache miss: the seek and read that loaded the page from
   the ROM file. */
void perf_metrics_record_gamepak_rom_io(PerfMetrics *metrics,
 uint32_t seek_us, uint32_t read_us, uint32_t bytes_read, int read_error)
{
  if(!perf_metrics_active(metrics))
    return;

  metrics->gamepak_miss_count = perf_metrics_saturating_add(
   metrics->gamepak_miss_count, 1);
  metrics->gamepak_miss_bytes = perf_metrics_saturating_add(
   metrics->gamepak_miss_bytes, bytes_read);
  metrics->gamepak_rom_seek_total_us = perf_metrics_saturating_add(
   metrics->gamepak_rom_seek_total_us, seek_us);
  metrics->gamepak_rom_read_total_us = perf_metrics_saturating_add(
   metrics->gamepak_rom_read_total_us, read_us);
  if(read_error)
    metrics->gamepak_read_errors = perf_metrics_saturating_add(
     metrics->gamepak_read_errors, 1);
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
  snapshot->fast_forward_presented_frames =
   metrics->fast_forward_presented_frames;
  snapshot->fast_forward_time_us = metrics->fast_forward_time_us;
  snapshot->fast_forward_transitions = metrics->fast_forward_transitions;
  snapshot->fast_forward_audio_fallbacks =
   metrics->fast_forward_audio_fallbacks;
  snapshot->audio_recoveries = metrics->audio_recoveries;
  snapshot->audio_underruns = metrics->audio_underruns;
  snapshot->dynarec_compilations = metrics->dynarec_compilations;
  snapshot->dynarec_reuse_hits = metrics->dynarec_reuse_hits;
  snapshot->dynarec_retries = metrics->dynarec_retries;
  snapshot->dynarec_flushes = metrics->dynarec_flushes;
  snapshot->dynarec_readonly_flushes = metrics->dynarec_readonly_flushes;
  snapshot->dynarec_writable_flushes = metrics->dynarec_writable_flushes;
  snapshot->dynarec_cache_high_water = metrics->dynarec_cache_high_water;
  snapshot->dynarec_readonly_high_water =
   metrics->dynarec_readonly_high_water;
  snapshot->dynarec_writable_high_water =
   metrics->dynarec_writable_high_water;
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

  snapshot->vblank_backpressure_count = metrics->vblank_backpressure_count;
  snapshot->vblank_backpressure_total_us =
   metrics->vblank_backpressure_total_us;
  snapshot->vblank_present_count = metrics->vblank_present_count;
  snapshot->vblank_present_total_us = metrics->vblank_present_total_us;
  snapshot->vblank_audio_count = metrics->vblank_audio_count;
  snapshot->vblank_audio_total_us = metrics->vblank_audio_total_us;
  snapshot->renderer_total_us = metrics->renderer_total_us;
  snapshot->renderer_max_us = metrics->renderer_max_us;
  snapshot->dcache_writeback_count = metrics->dcache_writeback_count;
  snapshot->dcache_writeback_total_us = metrics->dcache_writeback_total_us;
  snapshot->ge_sync_direct_count = metrics->ge_sync_direct_count;
  snapshot->ge_sync_direct_total_us = metrics->ge_sync_direct_total_us;
  snapshot->sharp_presented_frames = metrics->sharp_presented_frames;
  snapshot->crisp_presented_frames = metrics->crisp_presented_frames;
  snapshot->gba_update_entries = metrics->gba_update_entries;
  snapshot->gba_update_iterations = metrics->gba_update_iterations;
  snapshot->dma16_transfers = metrics->dma16_transfers;
  snapshot->dma16_elements = metrics->dma16_elements;
  snapshot->dma16_bytes = metrics->dma16_bytes;
  snapshot->dma16_total_us = metrics->dma16_total_us;
  snapshot->dma32_transfers = metrics->dma32_transfers;
  snapshot->dma32_elements = metrics->dma32_elements;
  snapshot->dma32_bytes = metrics->dma32_bytes;
  snapshot->dma32_total_us = metrics->dma32_total_us;
  snapshot->dma_gamepak_pairs = metrics->dma_gamepak_pairs;
  snapshot->gamepak_evict_scan_count = metrics->gamepak_evict_scan_count;
  snapshot->gamepak_evict_scan_total_us =
   metrics->gamepak_evict_scan_total_us;
  snapshot->gamepak_miss_count = metrics->gamepak_miss_count;
  snapshot->gamepak_miss_bytes = metrics->gamepak_miss_bytes;
  snapshot->gamepak_read_errors = metrics->gamepak_read_errors;
  snapshot->gamepak_rom_seek_total_us = metrics->gamepak_rom_seek_total_us;
  snapshot->gamepak_rom_read_total_us = metrics->gamepak_rom_read_total_us;
}
