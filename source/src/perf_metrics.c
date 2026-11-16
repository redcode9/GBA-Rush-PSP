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

/* Zeroes everything to IDLE and sets the frame-time, GE-wait and work-time
   histogram bucket widths; perf_metrics_reset() does the same. */
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
  perf_metrics_histogram_init(&metrics->work_time,
   PERF_METRICS_FRAME_BUCKET_US);
}

void perf_metrics_reset(PerfMetrics *metrics)
{
  perf_metrics_init(metrics);
}

/* Begins a fresh capture, discarding the previous one; 0 if a capture is
   already running. */
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

/* Counts an emulated frame and its fast-forward time, without the
   frame-time histogram. */
static void perf_metrics_count_emulated_frame(PerfMetrics *metrics,
 uint32_t frame_time_us, int fast_forward)
{
  metrics->emulated_frames = perf_metrics_saturating_add(
   metrics->emulated_frames, 1);
  if(fast_forward)
  {
    metrics->fast_forward_frames = perf_metrics_saturating_add(
     metrics->fast_forward_frames, 1);
    metrics->fast_forward_time_us = perf_metrics_saturating_add(
     metrics->fast_forward_time_us, frame_time_us);
  }
}

/* A whole frame in one call: emulated, then skipped or presented. Only
   presented frames sample the GE wait. Its work time is the frame time
   minus the waits recorded since the last frame and, when presented, the
   GE wait; a skipped frame passes the GE wait of an earlier frame. A frame
   the phase timers sample keeps its waits and work for them and stays out
   of the frame and work histograms, which the timers' clock reads would
   inflate; its counters still count. A sampled frame longer than
   PERF_METRICS_PHASE_MAX_FRAME_US, or a second record before the next
   frame begins, drops the sample and is recorded like any frame. */
void perf_metrics_record_frame(PerfMetrics *metrics, uint32_t frame_time_us,
 uint32_t ge_wait_us, int skipped, int fast_forward)
{
  uint64_t waits;
  uint32_t work_us = 0;
  int sampled;

  if(!perf_metrics_active(metrics))
    return;

  if(metrics->phase_sampling &&
     (metrics->phase_recorded ||
      frame_time_us > PERF_METRICS_PHASE_MAX_FRAME_US))
    metrics->phase_sampling = 0;
  sampled = metrics->phase_sampling;

  perf_metrics_count_emulated_frame(metrics, frame_time_us, fast_forward);
  if(!sampled)
    perf_metrics_histogram_record(&metrics->frame_time, frame_time_us);

  waits = metrics->frame_wait_us;
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
    waits = perf_metrics_saturating_add(waits, ge_wait_us);
  }

  /* shortcut: main_perf_start restarts the frame clock, so the first
     recorded frame also carries the waits of the frame before it and its
     work can come out low or 0 (one frame per capture); exact only if
     psp_video.c reports that seed frame too. */
  if(waits < frame_time_us)
    work_us = frame_time_us - (uint32_t)waits;
  if(sampled)
  {
    metrics->phase_recorded = 1;
    metrics->phase_frame_wait_us = waits;
    metrics->phase_frame_work_us = work_us;
  }
  else
    perf_metrics_histogram_record(&metrics->work_time, work_us);

  if(work_us > PERF_METRICS_WORK_BUDGET_US)
    metrics->frames_work_over_budget = perf_metrics_saturating_add(
     metrics->frames_work_over_budget, 1);
  if(frame_time_us > PERF_METRICS_FRAME_OVER_BUDGET_US)
    metrics->frames_over_1_5x_budget = perf_metrics_saturating_add(
     metrics->frames_over_1_5x_budget, 1);
  if(!fast_forward && !metrics->frame_waited_backpressure)
    metrics->frames_late = perf_metrics_saturating_add(
     metrics->frames_late, 1);

  metrics->frame_wait_us = 0;
  metrics->frame_waited_backpressure = 0;
}

/* The emulated half of perf_metrics_record_frame(): counts the frame and
   its fast-forward time, and samples the frame-time histogram. */
void perf_metrics_record_emulated_frame(PerfMetrics *metrics,
 uint32_t frame_time_us, int fast_forward)
{
  if(!perf_metrics_active(metrics))
    return;

  perf_metrics_count_emulated_frame(metrics, frame_time_us, fast_forward);
  perf_metrics_histogram_record(&metrics->frame_time, frame_time_us);
}

/* The presented half of perf_metrics_record_frame(), without the
   fast-forward presented count. */
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

/* Audio came back: PSP output works again after a failure, or the sound
   sync gave up waiting and dropped the queued samples. */
void perf_metrics_record_audio_recovery(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->audio_recoveries = perf_metrics_saturating_add(
     metrics->audio_recoveries, 1);
}

/* An underrun here is a failed PSP audio output or channel re-reserve in
   sound.c, not an empty buffer. */
void perf_metrics_record_audio_underrun(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->audio_underruns = perf_metrics_saturating_add(
     metrics->audio_underruns, 1);
}

/* Fast-forward switched on or off, or changed speed while on (see
   main_fast_forward_record_change). */
void perf_metrics_record_fast_forward_transition(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->fast_forward_transitions = perf_metrics_saturating_add(
     metrics->fast_forward_transitions, 1);
}

/* Accelerated fast-forward audio starved and was muted until the next
   fast-forward; counted once per fast-forward run. */
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

/* A writable-RAM block matched earlier code by checksum, so its existing
   translation was reused instead of recompiled. */
void perf_metrics_record_dynarec_reuse(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->dynarec_reuse_hits = perf_metrics_saturating_add(
     metrics->dynarec_reuse_hits, 1);
}

/* A translation abandoned because the code cache filled and was flushed,
   then restarted from the top. */
void perf_metrics_record_dynarec_retry(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->dynarec_retries = perf_metrics_saturating_add(
     metrics->dynarec_retries, 1);
}

/* Counts a full-cache dynarec flush (the only kind cpu.c reports), split by
   the read-only or writable code cache. */
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

/* Raises the high-water marks of both code caches' used bytes and their
   sum; cpu.c reports after each block it compiles. */
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

/* Keeps the lowest free-memory sample of the capture; main.c samples at
   start and stop. */
void perf_metrics_observe_memory_available(PerfMetrics *metrics,
 uint64_t bytes)
{
  if(!perf_metrics_active(metrics))
    return;

  if(!metrics->has_memory_sample || bytes < metrics->memory_low_water)
    metrics->memory_low_water = bytes;
  metrics->has_memory_sample = 1;
}

/* Adds one VBlank wait to the count and total of its site and to the waits
   of the current frame; the audio drain, no VBlank wait, goes only to the
   frame's waits, and an unknown site is ignored. */
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
    case PERF_METRICS_WAIT_AUDIO_DRAIN:
      metrics->frame_wait_us = perf_metrics_saturating_add(
       metrics->frame_wait_us, wait_us);
      return;
    default:
      return;
  }

  *count = perf_metrics_saturating_add(*count, 1);
  *total = perf_metrics_saturating_add(*total, wait_us);
  metrics->frame_wait_us = perf_metrics_saturating_add(
   metrics->frame_wait_us, wait_us);
  if(site == PERF_METRICS_WAIT_BACKPRESSURE)
    metrics->frame_waited_backpressure = 1;
}

/* Time one update_screen() call took to render a GBA frame; keeps the
   total and the worst frame. */
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

/* Times the full D-cache writeback done before handing a frame to the GE
   (count and total). */
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

/* Like perf_metrics_record_dcache_writeback(), for a wait on a GU_DIRECT
   display list to finish. */
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

/* Counts a presented frame as SHARP (two-pass upscale) or CRISP (single
   pass, nearest filter); see video_adaptive.h. */
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

/* One call of update_gba (main.c); the iteration counter below counts
   the passes of its event loop. */
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

/* Times one scan of the ROM page cache for the least recently used page
   to evict (count and total). */
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

/* One frame emulated with a lockstep link connected (main.c
   synchronize()), so a link opened halfway through a capture shows too. */
void perf_metrics_record_link_lockstep_frame(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->link_lockstep_frames = perf_metrics_saturating_add(
     metrics->link_lockstep_frames, 1);
}

/* Called as each frame begins. Adds the sampled frame that just ended to
   the phase totals, if perf_metrics_record_frame() recorded it, and
   returns whether the new frame is sampled: every
   PERF_METRICS_PHASE_STRIDE-th frame of the capture, so never the first,
   whose work also holds the waits from before the capture. */
int perf_metrics_phase_begin_frame(PerfMetrics *metrics)
{
  if(!perf_metrics_active(metrics))
    return 0;

  if(metrics->phase_sampling && metrics->phase_recorded)
  {
    /* The frame's waits ran inside update_gba (synchronize(), the sound
       sync and the flip), like the scanline and PSG phases. */
    uint64_t nested = perf_metrics_saturating_add(
     perf_metrics_saturating_add(metrics->phase_frame_scanline_us,
     metrics->phase_frame_gbc_sound_us), metrics->phase_frame_wait_us);
    uint64_t self_us = 0;

    if(metrics->phase_frame_update_gba_us > nested)
      self_us = metrics->phase_frame_update_gba_us - nested;
    metrics->phase_sampled_frames = perf_metrics_saturating_add(
     metrics->phase_sampled_frames, 1);
    metrics->phase_update_gba_us = perf_metrics_saturating_add(
     metrics->phase_update_gba_us, self_us);
    metrics->phase_scanline_us = perf_metrics_saturating_add(
     metrics->phase_scanline_us, metrics->phase_frame_scanline_us);
    metrics->phase_gbc_sound_us = perf_metrics_saturating_add(
     metrics->phase_gbc_sound_us, metrics->phase_frame_gbc_sound_us);
    metrics->phase_work_us = perf_metrics_saturating_add(
     metrics->phase_work_us, metrics->phase_frame_work_us);
    metrics->probe_reads = perf_metrics_saturating_add(
     metrics->probe_reads, metrics->phase_frame_reads);
  }

  metrics->phase_frames++;
  metrics->phase_sampling =
   metrics->phase_frames % PERF_METRICS_PHASE_STRIDE == 0U;
  metrics->phase_recorded = 0;
  metrics->phase_frame_update_gba_us = 0;
  metrics->phase_frame_scanline_us = 0;
  metrics->phase_frame_gbc_sound_us = 0;
  metrics->phase_frame_wait_us = 0;
  metrics->phase_frame_reads = 0;
  metrics->phase_frame_work_us = 0;
  return metrics->phase_sampling;
}

/* One timed stretch of the current frame, measured with two clock reads.
   It reaches the totals only if the frame is sampled and recorded; an
   unknown phase is ignored. */
void perf_metrics_phase_add(PerfMetrics *metrics, PerfMetricsPhase phase,
 uint32_t us)
{
  uint64_t *total;

  if(!perf_metrics_active(metrics))
    return;

  switch(phase)
  {
    case PERF_METRICS_PHASE_UPDATE_GBA:
      total = &metrics->phase_frame_update_gba_us;
      break;
    case PERF_METRICS_PHASE_SCANLINE:
      total = &metrics->phase_frame_scanline_us;
      break;
    case PERF_METRICS_PHASE_GBC_SOUND:
      total = &metrics->phase_frame_gbc_sound_us;
      break;
    default:
      return;
  }

  *total = perf_metrics_saturating_add(*total, us);
  metrics->phase_frame_reads = perf_metrics_saturating_add(
   metrics->phase_frame_reads, 2);
}

/* Nanoseconds of each of count steps that took elapsed_us together,
   saturated; count must not be 0. */
static uint32_t perf_metrics_ns_each(uint32_t elapsed_us, uint32_t count)
{
  uint64_t ns = (uint64_t)elapsed_us * 1000U / count;

  return ns > UINT32_MAX ? UINT32_MAX : (uint32_t)ns;
}

/* The cost of one clock read from `reads` reads made back to back, which
   took elapsed_us from the first to the last: reads - 1 intervals. */
void perf_metrics_record_probe_calibration(PerfMetrics *metrics,
 uint32_t elapsed_us, uint32_t reads)
{
  if(!perf_metrics_active(metrics) || reads < 2U)
    return;

  metrics->probe_read_ns = perf_metrics_ns_each(elapsed_us, reads - 1U);
}

/* Whether main.c should read the preemption counts: a capture runs and
   the kernel has not refused a read. */
int perf_metrics_preempt_wanted(const PerfMetrics *metrics)
{
  return perf_metrics_active(metrics) && !metrics->preempt_unavailable;
}

/* Adds one frame: the work deltas held since its work end, and its wait
   deltas. */
static void perf_metrics_record_preempt_frame(PerfMetrics *metrics,
 uint32_t wait_thread, uint32_t wait_intr)
{
  uint64_t count = (uint64_t)metrics->preempt_frame_work_thread +
   metrics->preempt_frame_work_intr + wait_thread + wait_intr;
  uint32_t preempt_class;

  metrics->preempt_work_thread = perf_metrics_saturating_add(
   metrics->preempt_work_thread, metrics->preempt_frame_work_thread);
  metrics->preempt_work_intr = perf_metrics_saturating_add(
   metrics->preempt_work_intr, metrics->preempt_frame_work_intr);
  metrics->preempt_wait_thread = perf_metrics_saturating_add(
   metrics->preempt_wait_thread, wait_thread);
  metrics->preempt_wait_intr = perf_metrics_saturating_add(
   metrics->preempt_wait_intr, wait_intr);
  if(count > metrics->preempt_max_per_frame)
    metrics->preempt_max_per_frame = count;

  if(count <= 2U)
    preempt_class = (uint32_t)count;
  else if(count <= 4U)
    preempt_class = 3;
  else if(count <= 8U)
    preempt_class = 4;
  else if(count <= 16U)
    preempt_class = 5;
  else
    preempt_class = 6;
  metrics->preempt_frames[preempt_class] = perf_metrics_saturating_add(
   metrics->preempt_frames[preempt_class], 1);
}

/* The thread and interrupt preemption counts the kernel gave at a point.
   The counts only grow and wrap at 32 bits, so a window's preemptions are
   the difference from the read before. A frame is added at its wait end
   once its work window and the previous frame's wait end were both read:
   never the first frame of a capture, which began before the capture. An
   unknown point is ignored. */
void perf_metrics_preempt_sample(PerfMetrics *metrics,
 PerfMetricsPreemptPoint point, uint32_t thread_count, uint32_t intr_count)
{
  uint32_t thread_delta;
  uint32_t intr_delta;

  if(!perf_metrics_preempt_wanted(metrics))
    return;

  thread_delta = (uint32_t)(thread_count - metrics->preempt_last_thread);
  intr_delta = (uint32_t)(intr_count - metrics->preempt_last_intr);
  switch(point)
  {
    case PERF_METRICS_PREEMPT_WORK_END:
      metrics->preempt_frame_work_thread = thread_delta;
      metrics->preempt_frame_work_intr = intr_delta;
      metrics->preempt_stage = metrics->preempt_stage == 1 ? 2 : 0;
      break;
    case PERF_METRICS_PREEMPT_WAIT_END:
      if(metrics->preempt_stage == 2)
        perf_metrics_record_preempt_frame(metrics, thread_delta, intr_delta);
      metrics->preempt_stage = 1;
      break;
    default:
      return;
  }
  metrics->preempt_last_thread = thread_count;
  metrics->preempt_last_intr = intr_count;
}

/* The kernel refused a read (some firmware, PPSSPP): the capture goes on
   without preemptions and its report says so. */
void perf_metrics_preempt_unavailable(PerfMetrics *metrics)
{
  if(perf_metrics_active(metrics))
    metrics->preempt_unavailable = 1;
}

/* The cost of one preemption read, from `calls` reads timed together. */
void perf_metrics_record_preempt_calibration(PerfMetrics *metrics,
 uint32_t elapsed_us, uint32_t calls)
{
  if(!perf_metrics_active(metrics) || calls == 0U)
    return;

  metrics->preempt_call_ns = perf_metrics_ns_each(elapsed_us, calls);
}

/* Copies the counters, reducing each histogram to samples, total, max and
   p50/p95/p99; an all-zero snapshot when metrics is NULL. */
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

  snapshot->work_time_samples = metrics->work_time.sample_count;
  snapshot->work_time_total_us = metrics->work_time.total_us;
  snapshot->work_time_max_us = metrics->work_time.max_us;
  snapshot->work_time_p50_us = perf_metrics_histogram_percentile(
   &metrics->work_time, 50);
  snapshot->work_time_p95_us = perf_metrics_histogram_percentile(
   &metrics->work_time, 95);
  snapshot->work_time_p99_us = perf_metrics_histogram_percentile(
   &metrics->work_time, 99);
  snapshot->frames_work_over_budget = metrics->frames_work_over_budget;
  snapshot->frames_over_1_5x_budget = metrics->frames_over_1_5x_budget;
  snapshot->frames_late = metrics->frames_late;
  snapshot->link_lockstep_frames = metrics->link_lockstep_frames;
  snapshot->phase_sampled_frames = metrics->phase_sampled_frames;
  snapshot->phase_update_gba_us = metrics->phase_update_gba_us;
  snapshot->phase_scanline_us = metrics->phase_scanline_us;
  snapshot->phase_gbc_sound_us = metrics->phase_gbc_sound_us;
  snapshot->phase_work_us = metrics->phase_work_us;
  snapshot->probe_read_ns = metrics->probe_read_ns;
  snapshot->probe_reads = metrics->probe_reads;
  snapshot->preempt_available = !metrics->preempt_unavailable;
  snapshot->preempt_work_thread = metrics->preempt_work_thread;
  snapshot->preempt_work_intr = metrics->preempt_work_intr;
  snapshot->preempt_wait_thread = metrics->preempt_wait_thread;
  snapshot->preempt_wait_intr = metrics->preempt_wait_intr;
  snapshot->preempt_max_per_frame = metrics->preempt_max_per_frame;
  memcpy(snapshot->preempt_frames, metrics->preempt_frames,
   sizeof(snapshot->preempt_frames));
  snapshot->preempt_call_ns = metrics->preempt_call_ns;
}
