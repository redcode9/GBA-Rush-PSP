/*
 * Host tests for perf_metrics.c, the counters behind the performance
 * report: session states (only an active session records), counters,
 * high and low watermarks, histogram percentiles, work time per frame and
 * its budget counters, the phase timers of sampled frames and the probe
 * calibration, the emulation thread's preemptions per frame, and
 * saturation of every counter at its type's maximum.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "perf_metrics.h"

static PerfMetricsSnapshot snapshot(const PerfMetrics *metrics)
{
  PerfMetricsSnapshot result;

  perf_metrics_snapshot(metrics, &result);
  return result;
}

/* Frames count only while ACTIVE; starting again after a stop clears the
   counters, and invalidate or reset end the session. */
static void test_session_states(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  result = snapshot(&metrics);
  assert(result.state == PERF_METRICS_IDLE);
  assert(!perf_metrics_active(&metrics));
  assert(!perf_metrics_stop(&metrics));

  perf_metrics_record_emulated_frame(&metrics, 16000, 0);
  assert(snapshot(&metrics).emulated_frames == 0);

  assert(perf_metrics_start(&metrics));
  assert(perf_metrics_active(&metrics));
  assert(!perf_metrics_start(&metrics));

  perf_metrics_record_emulated_frame(&metrics, 16000, 0);
  perf_metrics_record_presented_frame(&metrics, 500);
  perf_metrics_record_skipped_frame(&metrics);
  result = snapshot(&metrics);
  assert(result.state == PERF_METRICS_ACTIVE);
  assert(result.emulated_frames == 1);
  assert(result.presented_frames == 1);
  assert(result.skipped_frames == 1);

  assert(perf_metrics_stop(&metrics));
  assert(!perf_metrics_stop(&metrics));
  perf_metrics_record_emulated_frame(&metrics, 17000, 1);
  result = snapshot(&metrics);
  assert(result.state == PERF_METRICS_STOPPED);
  assert(result.emulated_frames == 1);
  assert(result.fast_forward_frames == 0);

  assert(perf_metrics_start(&metrics));
  result = snapshot(&metrics);
  assert(result.state == PERF_METRICS_ACTIVE);
  assert(result.emulated_frames == 0);
  assert(result.frame_time_samples == 0);

  perf_metrics_invalidate(&metrics);
  assert(!perf_metrics_active(&metrics));
  assert(snapshot(&metrics).state == PERF_METRICS_INVALIDATED);
  perf_metrics_record_skipped_frame(&metrics);
  assert(snapshot(&metrics).skipped_frames == 0);

  perf_metrics_reset(&metrics);
  assert(snapshot(&metrics).state == PERF_METRICS_IDLE);
}

static void test_counters_and_watermarks(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));

  perf_metrics_record_frame(&metrics, 16667, 300, 0, 1);
  perf_metrics_record_emulated_frame(&metrics, 16668, 0);
  perf_metrics_record_audio_recovery(&metrics);
  perf_metrics_record_audio_underrun(&metrics);
  perf_metrics_record_fast_forward_transition(&metrics);
  perf_metrics_record_fast_forward_audio_fallback(&metrics);
  perf_metrics_record_dynarec_compilation(&metrics);
  perf_metrics_record_dynarec_reuse(&metrics);
  perf_metrics_record_dynarec_retry(&metrics);
  perf_metrics_record_dynarec_flush(&metrics, 0);
  perf_metrics_record_dynarec_flush(&metrics, 1);
  /* Cache high water is the largest read-only + writable total seen
     (2048 + 256), not the sum of the two separate maxima. */
  perf_metrics_observe_dynarec_cache(&metrics, 1024, 512);
  perf_metrics_observe_dynarec_cache(&metrics, 900, 400);
  perf_metrics_observe_dynarec_cache(&metrics, 2048, 256);
  perf_metrics_observe_memory_available(&metrics, 8192);
  perf_metrics_observe_memory_available(&metrics, 12288);
  perf_metrics_observe_memory_available(&metrics, 4096);

  result = snapshot(&metrics);
  assert(result.emulated_frames == 2);
  assert(result.fast_forward_frames == 1);
  assert(result.fast_forward_presented_frames == 1);
  assert(result.fast_forward_time_us == 16667);
  assert(result.fast_forward_transitions == 1);
  assert(result.fast_forward_audio_fallbacks == 1);
  assert(result.presented_frames == 1);
  assert(result.audio_recoveries == 1);
  assert(result.audio_underruns == 1);
  assert(result.dynarec_compilations == 1);
  assert(result.dynarec_reuse_hits == 1);
  assert(result.dynarec_retries == 1);
  assert(result.dynarec_flushes == 2);
  assert(result.dynarec_readonly_flushes == 1);
  assert(result.dynarec_writable_flushes == 1);
  assert(result.dynarec_cache_high_water == 2304);
  assert(result.dynarec_readonly_high_water == 2048);
  assert(result.dynarec_writable_high_water == 512);
  assert(result.has_memory_sample);
  assert(result.memory_low_water == 4096);

  perf_metrics_reset(&metrics);
  result = snapshot(&metrics);
  assert(!result.has_memory_sample);
  assert(result.dynarec_cache_high_water == 0);
  assert(result.memory_low_water == 0);
}

/* Percentiles come from histograms with 250 us (frame time) and 50 us (GE
   wait) buckets and report a bucket's upper edge, so the samples sit on
   both sides of the edges. */
static void test_percentiles_and_totals(void)
{
  static const uint32_t frame_times[] =
  {
    1, 250, 251, 500, 501, 750, 751, 1000
  };
  static const uint32_t ge_waits[] = { 0, 1, 50, 51, 100 };
  PerfMetrics metrics;
  PerfMetricsSnapshot result;
  size_t i;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));

  for(i = 0; i < sizeof(frame_times) / sizeof(frame_times[0]); i++)
    perf_metrics_record_emulated_frame(&metrics, frame_times[i], 0);

  for(i = 0; i < sizeof(ge_waits) / sizeof(ge_waits[0]); i++)
    perf_metrics_record_presented_frame(&metrics, ge_waits[i]);

  result = snapshot(&metrics);
  assert(result.frame_time_samples == 8);
  assert(result.frame_time_total_us == 4004);
  assert(result.frame_time_max_us == 1000);
  assert(result.frame_time_p50_us == 500);
  assert(result.frame_time_p95_us == 1000);
  assert(result.frame_time_p99_us == 1000);

  assert(result.ge_wait_samples == 5);
  assert(result.ge_wait_total_us == 202);
  assert(result.ge_wait_max_us == 100);
  assert(result.ge_wait_p50_us == 50);
  assert(result.ge_wait_p95_us == 100);
  assert(result.ge_wait_p99_us == 100);
}

/* No samples report 0; a sample past the last bucket reports the true
   maximum instead of the bucket edge. */
static void test_empty_and_overflow_percentiles(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  result = snapshot(&metrics);
  assert(result.frame_time_p50_us == 0);
  assert(result.frame_time_p95_us == 0);
  assert(result.frame_time_p99_us == 0);

  perf_metrics_record_emulated_frame(&metrics, UINT32_MAX, 0);
  perf_metrics_record_presented_frame(&metrics, UINT32_MAX);
  result = snapshot(&metrics);
  assert(result.frame_time_p50_us == UINT32_MAX);
  assert(result.frame_time_p95_us == UINT32_MAX);
  assert(result.ge_wait_p99_us == UINT32_MAX);
}

static void test_saturating_counters(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  metrics.emulated_frames = UINT64_MAX;
  metrics.audio_recoveries = UINT64_MAX;
  metrics.frame_time.total_us = UINT64_MAX;
  metrics.frame_time.sample_count = UINT64_MAX;

  perf_metrics_record_emulated_frame(&metrics, 1000, 1);
  perf_metrics_record_audio_recovery(&metrics);
  result = snapshot(&metrics);
  assert(result.emulated_frames == UINT64_MAX);
  assert(result.fast_forward_frames == 1);
  assert(result.audio_recoveries == UINT64_MAX);
  assert(result.frame_time_total_us == UINT64_MAX);
  assert(result.frame_time_samples == UINT64_MAX);
}

/* Probes recorded before the session starts are dropped. */
static void test_wait_and_frame_probes(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_PRESENT, 50);
  perf_metrics_record_renderer_frame(&metrics, 50);
  perf_metrics_record_gba_update_entry(&metrics);
  perf_metrics_record_dma(&metrics, 0, 10, 100, 1);
  perf_metrics_record_dma(&metrics, 1, 10, 100, 1);
  perf_metrics_record_gamepak_evict_scan(&metrics, 100);
  perf_metrics_record_gamepak_rom_io(&metrics, 100, 200, 32 * 1024, 1);
  result = snapshot(&metrics);
  assert(result.vblank_present_count == 0);
  assert(result.renderer_total_us == 0);
  assert(result.gba_update_entries == 0);
  assert(result.dma16_transfers == 0);
  assert(result.dma32_transfers == 0);
  assert(result.gamepak_miss_count == 0);

  assert(perf_metrics_start(&metrics));
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 1000);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 500);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_PRESENT, 2000);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_AUDIO, 3000);
  perf_metrics_record_renderer_frame(&metrics, 1200);
  perf_metrics_record_renderer_frame(&metrics, 800);
  perf_metrics_record_dcache_writeback(&metrics, 150);
  perf_metrics_record_dcache_writeback(&metrics, 250);
  perf_metrics_record_ge_sync_direct(&metrics, 350);
  perf_metrics_record_quality_presented(&metrics, 1);
  perf_metrics_record_quality_presented(&metrics, 0);
  perf_metrics_record_gba_update_entry(&metrics);
  perf_metrics_record_gba_update_entry(&metrics);
  perf_metrics_record_gba_update_iteration(&metrics);
  perf_metrics_record_dma(&metrics, 0, 10, 100, 1);
  perf_metrics_record_dma(&metrics, 0, 4, 50, 0);
  perf_metrics_record_dma(&metrics, 1, 8, 200, 1);
  perf_metrics_record_gamepak_evict_scan(&metrics, 120);
  perf_metrics_record_gamepak_evict_scan(&metrics, 80);
  perf_metrics_record_gamepak_rom_io(&metrics, 50, 200, 32 * 1024, 0);
  perf_metrics_record_gamepak_rom_io(&metrics, 25, 100, 32 * 1024, 1);

  result = snapshot(&metrics);
  assert(result.vblank_backpressure_count == 2);
  assert(result.vblank_backpressure_total_us == 1500);
  assert(result.vblank_present_count == 1);
  assert(result.vblank_present_total_us == 2000);
  assert(result.vblank_audio_count == 1);
  assert(result.vblank_audio_total_us == 3000);
  assert(result.renderer_total_us == 2000);
  assert(result.renderer_max_us == 1200);
  assert(result.dcache_writeback_count == 2);
  assert(result.dcache_writeback_total_us == 400);
  assert(result.ge_sync_direct_count == 1);
  assert(result.ge_sync_direct_total_us == 350);
  assert(result.sharp_presented_frames == 1);
  assert(result.crisp_presented_frames == 1);
  assert(result.gba_update_entries == 2);
  assert(result.gba_update_iterations == 1);
  assert(result.dma16_transfers == 2);
  assert(result.dma16_elements == 14);
  assert(result.dma16_bytes == 28);
  assert(result.dma16_total_us == 150);
  assert(result.dma32_transfers == 1);
  assert(result.dma32_elements == 8);
  assert(result.dma32_bytes == 32);
  assert(result.dma32_total_us == 200);
  assert(result.dma_gamepak_pairs == 2);
  assert(result.gamepak_evict_scan_count == 2);
  assert(result.gamepak_evict_scan_total_us == 200);
  assert(result.gamepak_miss_count == 2);
  assert(result.gamepak_miss_bytes == 2 * (32 * 1024));
  assert(result.gamepak_read_errors == 1);
  assert(result.gamepak_rom_seek_total_us == 75);
  assert(result.gamepak_rom_read_total_us == 300);

  perf_metrics_reset(&metrics);
  result = snapshot(&metrics);
  assert(result.vblank_backpressure_count == 0);
  assert(result.renderer_total_us == 0);
  assert(result.gba_update_entries == 0);
  assert(result.dma16_transfers == 0);
  assert(result.dma32_transfers == 0);
  assert(result.gamepak_miss_count == 0);
}

/* Work is the frame time minus every wait recorded since the last frame
   and, for a presented frame, its GE wait; a skipped frame's stale GE wait
   is left out. */
static void test_work_time_subtracts_waits(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 3000);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_PRESENT, 1000);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_AUDIO, 500);
  perf_metrics_record_frame(&metrics, 16700, 200, 0, 0);
  result = snapshot(&metrics);
  assert(result.work_time_samples == 1);
  assert(result.work_time_total_us == 12000);
  assert(result.work_time_max_us == 12000);
  assert(result.work_time_p50_us == 12000);

  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 1000);
  perf_metrics_record_frame(&metrics, 10000, 9999, 1, 0);
  result = snapshot(&metrics);
  assert(result.work_time_samples == 2);
  assert(result.work_time_total_us == 21000);
  assert(result.work_time_p99_us == 12000);
  assert(result.frame_time_total_us == 26700);
}

/* The sound sync's sleep until the audio thread takes a block leaves the
   frame's work time like a wait, but it is no VBlank wait: no site counts
   it, and the frame still counts as late. */
static void test_audio_drain_leaves_work_only(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_AUDIO, 500);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_AUDIO_DRAIN, 8000);
  perf_metrics_record_frame(&metrics, 16700, 200, 0, 0);
  result = snapshot(&metrics);
  assert(result.work_time_total_us == 16700 - 500 - 8000 - 200);
  assert(result.vblank_audio_count == 1);
  assert(result.vblank_audio_total_us == 500);
  assert(result.vblank_backpressure_count == 0);
  assert(result.vblank_present_count == 0);
  assert(result.frames_late == 1);
}

/* Work percentiles come from the work histogram in 250 us buckets: 18
   frames of 8100 us work, one of 12100 and one of 20000 give p50 8250,
   p95 12250 and p99 20000, while the frame-time p95 is 16750. */
static void test_work_time_percentiles(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;
  unsigned frame;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  for(frame = 0; frame < 18; frame++)
  {
    perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 8600);
    perf_metrics_record_frame(&metrics, 16700, 0, 0, 0);
  }
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 4600);
  perf_metrics_record_frame(&metrics, 16700, 0, 0, 0);
  perf_metrics_record_frame(&metrics, 20000, 0, 0, 0);

  result = snapshot(&metrics);
  assert(result.work_time_samples == 20);
  assert(result.work_time_p50_us == 8250);
  assert(result.work_time_p95_us == 12250);
  assert(result.work_time_p99_us == 20000);
  assert(result.frame_time_p95_us == 16750);
}

/* Waits longer than the frame give 0 work, not a wrapped huge value; the
   wait sum itself saturates. */
static void test_work_time_clamps_at_zero(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 20000);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_AUDIO, 16000);
  perf_metrics_record_frame(&metrics, 16500, 1000, 0, 0);
  metrics.frame_wait_us = UINT64_MAX - 1U;
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_PRESENT, 10);
  assert(metrics.frame_wait_us == UINT64_MAX);
  perf_metrics_record_frame(&metrics, UINT32_MAX, UINT32_MAX, 0, 0);
  result = snapshot(&metrics);
  assert(result.work_time_samples == 3);
  assert(result.work_time_total_us == 0);
  assert(result.work_time_max_us == 0);
  assert(result.work_time_p99_us == 0);
  assert(result.frames_work_over_budget == 0);
}

/* Each recorded frame starts a new wait sum and a new backpressure flag. */
static void test_waits_reset_each_frame(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 5000);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  result = snapshot(&metrics);
  assert(result.work_time_total_us == 11000 + 16000);
  assert(result.work_time_max_us == 16000);
  assert(result.frames_late == 1);

  /* A restart drops waits left over from the previous capture. */
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 4000);
  assert(perf_metrics_stop(&metrics));
  assert(perf_metrics_start(&metrics));
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  result = snapshot(&metrics);
  assert(result.work_time_total_us == 16000);
  assert(result.frames_late == 1);
}

/* Over budget means strictly above 16743 us of work or 25115 us of frame
   time; late means no backpressure wait outside fast-forward. */
static void test_over_budget_and_late_counts(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 0);
  perf_metrics_record_frame(&metrics, PERF_METRICS_WORK_BUDGET_US, 0, 0, 0);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 0);
  perf_metrics_record_frame(&metrics, 16744, 0, 0, 0);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 8372);
  perf_metrics_record_frame(&metrics, PERF_METRICS_FRAME_OVER_BUDGET_US, 0,
   0, 0);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 8372);
  perf_metrics_record_frame(&metrics, 25116, 0, 0, 0);
  /* The GE wait of a presented frame counts; a skipped frame's does not. */
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 0);
  perf_metrics_record_frame(&metrics, 16744, 1, 0, 0);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 0);
  perf_metrics_record_frame(&metrics, 16744, 16744, 1, 0);
  result = snapshot(&metrics);
  assert(result.frames_work_over_budget == 3);
  assert(result.frames_over_1_5x_budget == 1);
  assert(result.frames_late == 0);

  /* A fast-forward frame is never late; another wait site does not pace. */
  perf_metrics_record_frame(&metrics, 10000, 0, 0, 1);
  perf_metrics_record_frame(&metrics, 10000, 0, 1, 1);
  perf_metrics_record_frame(&metrics, 10000, 0, 0, 0);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_PRESENT, 100);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_AUDIO, 100);
  perf_metrics_record_frame(&metrics, 10000, 0, 0, 0);
  perf_metrics_record_frame(&metrics, 10000, 0, 1, 0);
  result = snapshot(&metrics);
  assert(result.frames_late == 3);
  assert(result.frames_work_over_budget == 3);
}

static void test_new_counters_saturate(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  metrics.frames_work_over_budget = UINT64_MAX;
  metrics.frames_over_1_5x_budget = UINT64_MAX;
  metrics.frames_late = UINT64_MAX;
  metrics.link_lockstep_frames = UINT64_MAX;
  metrics.work_time.sample_count = UINT64_MAX;
  metrics.work_time.total_us = UINT64_MAX;

  perf_metrics_record_frame(&metrics, 30000, 0, 0, 0);
  perf_metrics_record_link_lockstep_frame(&metrics);
  result = snapshot(&metrics);
  assert(result.frames_work_over_budget == UINT64_MAX);
  assert(result.frames_over_1_5x_budget == UINT64_MAX);
  assert(result.frames_late == UINT64_MAX);
  assert(result.link_lockstep_frames == UINT64_MAX);
  assert(result.work_time_samples == UINT64_MAX);
  assert(result.work_time_total_us == UINT64_MAX);
  assert(result.work_time_max_us == 30000);
}

/* Lockstep frames count only while a capture is active. */
static void test_link_lockstep_frames(void)
{
  PerfMetrics metrics;

  perf_metrics_init(&metrics);
  perf_metrics_record_link_lockstep_frame(&metrics);
  assert(snapshot(&metrics).link_lockstep_frames == 0);
  assert(perf_metrics_start(&metrics));
  perf_metrics_record_link_lockstep_frame(&metrics);
  perf_metrics_record_link_lockstep_frame(&metrics);
  assert(perf_metrics_stop(&metrics));
  perf_metrics_record_link_lockstep_frame(&metrics);
  assert(snapshot(&metrics).link_lockstep_frames == 2);
  perf_metrics_reset(&metrics);
  assert(snapshot(&metrics).link_lockstep_frames == 0);
}

/* Begins and records count plain frames of frame_time_us with no waits;
   returns how many of them were sampled. */
static unsigned run_frames(PerfMetrics *metrics, unsigned count,
 uint32_t frame_time_us)
{
  unsigned sampled = 0;

  while(count-- > 0)
  {
    sampled += (unsigned)perf_metrics_phase_begin_frame(metrics);
    perf_metrics_record_frame(metrics, frame_time_us, 0, 0, 0);
  }
  return sampled;
}

/* Exactly the frames 11, 22, 33, ... of every capture are sampled, the
   same ones after a restart, never the first and never without a capture.
   Over 11 * p frames the samples land once on each phase of a p-frame
   cycle, for the cycles of 2 to 6 frames games use. */
static void test_phase_stride_is_deterministic_and_coprime(void)
{
  PerfMetrics metrics;
  unsigned run;
  unsigned frame;
  unsigned period;

  assert(PERF_METRICS_PHASE_STRIDE == 11U);
  perf_metrics_init(&metrics);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  for(run = 0; run < 2; run++)
  {
    assert(perf_metrics_start(&metrics));
    for(frame = 1; frame <= 11U * 60U; frame++)
      assert(perf_metrics_phase_begin_frame(&metrics) ==
       (frame % 11U == 0U));
    /* No frame was recorded, so none was added. */
    assert(snapshot(&metrics).phase_sampled_frames == 0);
    assert(perf_metrics_stop(&metrics));
    assert(!perf_metrics_phase_begin_frame(&metrics));
  }

  for(period = 2; period <= 6; period++)
  {
    unsigned hits[6] = { 0 };

    assert(perf_metrics_start(&metrics));
    for(frame = 1; frame <= 11U * period; frame++)
    {
      if(perf_metrics_phase_begin_frame(&metrics))
        hits[frame % period]++;
    }
    for(frame = 0; frame < period; frame++)
      assert(hits[frame] == 1);
    assert(perf_metrics_stop(&metrics));
  }
}

/* update_gba's own time is its total minus the scanline and PSG phases
   and the frame's waits, the GE wait included when the frame is
   presented; nested phases longer than the total give 0, not a wrap. The
   totals arrive when the next frame begins. */
static void test_nested_phase_subtraction(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  assert(run_frames(&metrics, 10, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 4000);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE, 1200);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE, 2000);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_GBC_SOUND, 500);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 9000);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 1000);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_AUDIO, 200);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 2000);
  perf_metrics_record_frame(&metrics, 16500, 300, 0, 0);
  /* update_gba time after the frame clock, up to the next frame start. */
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 100);
  perf_metrics_phase_add(&metrics, (PerfMetricsPhase)7, 100000);
  assert(snapshot(&metrics).phase_sampled_frames == 0);

  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.phase_sampled_frames == 1);
  assert(result.phase_update_gba_us == 15100 - 3200 - 500 - 1500);
  assert(result.phase_scanline_us == 3200);
  assert(result.phase_gbc_sound_us == 500);
  assert(result.phase_work_us == 16500 - 1500);
  assert(result.probe_reads == 2U * 7U);

  /* A skipped frame's stale GE wait is not one of its waits. */
  assert(run_frames(&metrics, 9, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 10000);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_GBC_SOUND, 400);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE, 600);
  perf_metrics_record_frame(&metrics, 16000, 5000, 1, 0);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.phase_sampled_frames == 2);
  assert(result.phase_update_gba_us == 9900 + 10000 - 400 - 600);
  assert(result.phase_work_us == 15000 + 15400);

  /* Nested time above the update_gba total. */
  assert(run_frames(&metrics, 9, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 1000);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE, 900);
  perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_PRESENT, 200);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.phase_sampled_frames == 3);
  assert(result.phase_update_gba_us == 18900);
  assert(result.phase_scanline_us == 3200 + 900);
}

/* A sampled frame counts in every counter (fast-forward and late included)
   but stays out of the frame and work histograms. One longer than 128 ms
   (a menu or a save state) drops its phases and is counted like any
   frame; a capture stopped during a sampled frame never adds it. */
static void test_sampled_frames_excluded_from_histograms(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;
  unsigned frame;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  for(frame = 1; frame <= 22; frame++)
  {
    int sampled = perf_metrics_phase_begin_frame(&metrics);

    assert(sampled == (frame == 11 || frame == 22));
    perf_metrics_record_frame(&metrics, sampled ? 40000 : 16000, 100, 0,
     frame == 22);
  }
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.emulated_frames == 22);
  assert(result.presented_frames == 22);
  assert(result.ge_wait_samples == 22);
  assert(result.frames_over_1_5x_budget == 2);
  assert(result.frames_work_over_budget == 2);
  assert(result.frames_late == 21);
  assert(result.fast_forward_frames == 1);
  assert(result.fast_forward_time_us == 40000);
  assert(result.fast_forward_presented_frames == 1);
  assert(result.frame_time_samples == 20);
  assert(result.frame_time_total_us == 20U * 16000U);
  assert(result.frame_time_max_us == 16000);
  assert(result.frame_time_p99_us == 16000);
  assert(result.work_time_samples == 20);
  assert(result.work_time_max_us == 15900);
  assert(result.phase_sampled_frames == 2);
  assert(result.phase_work_us == 2U * 39900U);

  /* 128 ms exactly is still sampled; a microsecond more is not. */
  assert(run_frames(&metrics, 9, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_record_frame(&metrics, PERF_METRICS_PHASE_MAX_FRAME_US, 0,
   0, 0);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  assert(snapshot(&metrics).phase_sampled_frames == 3);
  assert(snapshot(&metrics).frame_time_samples == 29);

  assert(run_frames(&metrics, 9, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE, 3000);
  perf_metrics_record_frame(&metrics, PERF_METRICS_PHASE_MAX_FRAME_US + 1U,
   0, 0, 0);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 5000);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.phase_sampled_frames == 3);
  assert(result.phase_scanline_us == 0);
  assert(result.phase_update_gba_us == 0);
  assert(result.frame_time_samples == 39);
  assert(result.frame_time_max_us == PERF_METRICS_PHASE_MAX_FRAME_US + 1U);

  /* Two frame records between two frame starts also drop the sample; the
     second goes to the histograms. */
  assert(run_frames(&metrics, 9, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  perf_metrics_record_frame(&metrics, 17000, 0, 0, 0);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.phase_sampled_frames == 3);
  assert(result.frame_time_samples == 49);
  assert(result.frame_time_total_us ==
   38U * 16000U + 128001U + 9U * 16000U + 17000U);

  assert(run_frames(&metrics, 9, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 5000);
  assert(perf_metrics_stop(&metrics));
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.phase_sampled_frames == 3);
  assert(result.phase_update_gba_us == 0);
  assert(result.emulated_frames == 62);
}

/* The calibration turns 64 back-to-back reads into the cost of one (63
   intervals); the report gets that cost and the reads the timers made in
   the frames they added, two per timed stretch. */
static void test_probe_cost_reported(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;

  assert(!perf_metrics_phase_begin_frame(NULL));
  perf_metrics_phase_add(NULL, PERF_METRICS_PHASE_SCANLINE, 1);
  perf_metrics_record_probe_calibration(NULL, 63, 64);

  perf_metrics_init(&metrics);
  perf_metrics_record_probe_calibration(&metrics, 63, 64);
  assert(snapshot(&metrics).probe_read_ns == 0);

  assert(perf_metrics_start(&metrics));
  perf_metrics_record_probe_calibration(&metrics, 63,
   PERF_METRICS_PROBE_CALIBRATION_READS);
  assert(snapshot(&metrics).probe_read_ns == 1000);
  perf_metrics_record_probe_calibration(&metrics, 20, 64);
  assert(snapshot(&metrics).probe_read_ns == 317);
  perf_metrics_record_probe_calibration(&metrics, 5, 1);
  perf_metrics_record_probe_calibration(&metrics, 5, 0);
  assert(snapshot(&metrics).probe_read_ns == 317);
  perf_metrics_record_probe_calibration(&metrics, UINT32_MAX, 2);
  assert(snapshot(&metrics).probe_read_ns == UINT32_MAX);
  perf_metrics_record_probe_calibration(&metrics, 0, 64);
  assert(snapshot(&metrics).probe_read_ns == 0);
  perf_metrics_record_probe_calibration(&metrics, 19, 64);

  /* Reads outside a sampled frame are not the timers'. */
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE, 10);
  assert(run_frames(&metrics, 10, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 10);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE, 3);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_GBC_SOUND, 2);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  assert(snapshot(&metrics).probe_reads == 0);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.probe_read_ns == 301);
  assert(result.probe_reads == 6);

  metrics.probe_reads = UINT64_MAX - 1U;
  metrics.phase_sampled_frames = UINT64_MAX;
  metrics.phase_update_gba_us = UINT64_MAX;
  assert(run_frames(&metrics, 9, 16000) == 0);
  assert(perf_metrics_phase_begin_frame(&metrics));
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA, 10);
  perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE, 3);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  assert(!perf_metrics_phase_begin_frame(&metrics));
  result = snapshot(&metrics);
  assert(result.probe_reads == UINT64_MAX);
  assert(result.phase_sampled_frames == UINT64_MAX);
  assert(result.phase_update_gba_us == UINT64_MAX);

  /* A new capture starts from no calibration and no reads. */
  assert(perf_metrics_stop(&metrics));
  assert(perf_metrics_start(&metrics));
  result = snapshot(&metrics);
  assert(result.probe_read_ns == 0);
  assert(result.probe_reads == 0);
  assert(result.phase_sampled_frames == 0);
}

/* Synthetic frames built as on the PSP: the frame time is the CPU time
   outside update_gba plus update_gba, which holds the waits, the
   scanline phase and the PSG phase. Summed over the sampled frames, the
   phases then never exceed their work, even where the waits outweigh the
   CPU time, and match update_gba minus the waits exactly. */
static void test_phase_sum_within_sampled_work(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;
  uint32_t seed = 12345U;
  uint64_t expected_sum = 0;
  uint64_t sum;
  unsigned frame;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  for(frame = 1; frame <= 11U * 200U; frame++)
  {
    uint32_t cpu_us;
    uint32_t wait_us;
    uint32_t scanline_us;
    uint32_t sound_us;
    uint32_t own_us;
    int sampled = perf_metrics_phase_begin_frame(&metrics);

    seed = seed * 1103515245U + 12345U;
    cpu_us = (seed >> 8) % 3000U;
    wait_us = (seed >> 4) % 9000U;
    scanline_us = seed % 4000U;
    sound_us = (seed >> 12) % 800U;
    own_us = (seed >> 16) % 3000U;
    if(sampled)
    {
      perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_SCANLINE,
       scanline_us);
      perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_GBC_SOUND,
       sound_us);
      perf_metrics_phase_add(&metrics, PERF_METRICS_PHASE_UPDATE_GBA,
       scanline_us + sound_us + wait_us + own_us);
      expected_sum += scanline_us + sound_us + own_us;
    }
    perf_metrics_record_wait(&metrics, PERF_METRICS_WAIT_BACKPRESSURE,
     wait_us);
    perf_metrics_record_frame(&metrics,
     cpu_us + scanline_us + sound_us + wait_us + own_us, 0, frame % 3U == 0U,
     0);
  }
  assert(!perf_metrics_phase_begin_frame(&metrics));

  result = snapshot(&metrics);
  sum = result.phase_update_gba_us + result.phase_scanline_us +
   result.phase_gbc_sound_us;
  assert(result.phase_sampled_frames == 200);
  assert(sum == expected_sum);
  assert(sum <= result.phase_work_us);
  assert(result.frame_time_samples == 11U * 200U - 200U);
}

/* One frame of preemption reads as main.c makes them: the work end, then
   the wait end, each giving the kernel's running counts after its window
   added its thread and interrupt preemptions. The counts wrap at 32 bits
   like the kernel's. */
static void preempt_frame(PerfMetrics *metrics, uint32_t counts[2],
 uint32_t work_thread, uint32_t work_intr, uint32_t wait_thread,
 uint32_t wait_intr)
{
  counts[0] += work_thread;
  counts[1] += work_intr;
  perf_metrics_preempt_sample(metrics, PERF_METRICS_PREEMPT_WORK_END,
   counts[0], counts[1]);
  counts[0] += wait_thread;
  counts[1] += wait_intr;
  perf_metrics_preempt_sample(metrics, PERF_METRICS_PREEMPT_WAIT_END,
   counts[0], counts[1]);
}

/* A frame's preemptions, thread and interrupt in both windows, land in
   one class: 0, 1, 2, 3-4, 5-8, 9-16 or 17+. Both edges of every class
   are hit, each frame spread over the four counts so each one counts. */
static void test_preempt_histogram_bins(void)
{
  static const uint32_t cases[][2] =
  {
    { 0, 0 }, { 1, 1 }, { 2, 2 }, { 3, 3 }, { 4, 3 }, { 5, 4 }, { 8, 4 },
    { 9, 5 }, { 16, 5 }, { 17, 6 }, { 1000, 6 }
  };
  PerfMetrics metrics;
  uint32_t counts[2] = { 0, 0 };
  uint64_t expected[PERF_METRICS_PREEMPT_CLASSES] = { 0 };
  size_t i;
  uint32_t preempt_class;

  perf_metrics_init(&metrics);
  assert(perf_metrics_start(&metrics));
  perf_metrics_preempt_sample(&metrics, PERF_METRICS_PREEMPT_WAIT_END, 0, 0);
  for(i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
  {
    uint32_t n = cases[i][0];
    PerfMetricsSnapshot result;

    preempt_frame(&metrics, counts, n / 4U, n / 4U, n / 4U, n - 3U * (n / 4U));
    expected[cases[i][1]]++;
    result = snapshot(&metrics);
    for(preempt_class = 0; preempt_class < PERF_METRICS_PREEMPT_CLASSES;
        preempt_class++)
      assert(result.preempt_frames[preempt_class] == expected[preempt_class]);
  }
  /* One preemption in any single count is a frame of class 1. */
  preempt_frame(&metrics, counts, 1, 0, 0, 0);
  preempt_frame(&metrics, counts, 0, 1, 0, 0);
  preempt_frame(&metrics, counts, 0, 0, 1, 0);
  assert(snapshot(&metrics).preempt_frames[1] == 4);

  metrics.preempt_frames[6] = UINT64_MAX;
  preempt_frame(&metrics, counts, 20, 0, 0, 0);
  assert(snapshot(&metrics).preempt_frames[6] == UINT64_MAX);
}

/* The maximum is the most preemptions in one frame, both windows and both
   kinds together; the totals keep work and wait, thread and interrupt
   apart. A frame counts only once its work window opened at the previous
   frame's wait end, and the deltas survive the counts wrapping. */
static void test_preempt_max_per_frame(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;
  uint32_t counts[2] = { 100, 200 };

  perf_metrics_preempt_sample(NULL, PERF_METRICS_PREEMPT_WAIT_END, 0, 0);
  perf_metrics_init(&metrics);
  perf_metrics_preempt_sample(&metrics, PERF_METRICS_PREEMPT_WAIT_END, 0, 0);
  preempt_frame(&metrics, counts, 9, 9, 9, 9);
  assert(snapshot(&metrics).preempt_max_per_frame == 0);

  /* The first frame of a capture began before it: not counted. */
  assert(perf_metrics_start(&metrics));
  preempt_frame(&metrics, counts, 30, 30, 30, 30);
  result = snapshot(&metrics);
  assert(result.preempt_max_per_frame == 0);
  assert(result.preempt_work_thread == 0 && result.preempt_wait_intr == 0);

  preempt_frame(&metrics, counts, 5, 0, 5, 0);
  preempt_frame(&metrics, counts, 0, 0, 0, 9);
  preempt_frame(&metrics, counts, 1, 2, 3, 1);
  result = snapshot(&metrics);
  assert(result.preempt_max_per_frame == 10);
  assert(result.preempt_work_thread == 6);
  assert(result.preempt_work_intr == 2);
  assert(result.preempt_wait_thread == 8);
  assert(result.preempt_wait_intr == 10);

  /* A second work end with no wait end before it opens no window, and an
     unknown point changes nothing. */
  counts[0] += 50;
  perf_metrics_preempt_sample(&metrics, PERF_METRICS_PREEMPT_WORK_END,
   counts[0], counts[1]);
  preempt_frame(&metrics, counts, 40, 0, 40, 0);
  assert(snapshot(&metrics).preempt_work_thread == 6);
  perf_metrics_preempt_sample(&metrics, (PerfMetricsPreemptPoint)7,
   counts[0] + 1000U, counts[1] + 1000U);
  preempt_frame(&metrics, counts, 2, 0, 0, 0);
  result = snapshot(&metrics);
  assert(result.preempt_work_thread == 8);
  assert(result.preempt_wait_thread == 8);
  assert(result.preempt_max_per_frame == 10);

  /* The kernel's counts wrap at 32 bits. */
  counts[0] = UINT32_MAX - 1U;
  counts[1] = UINT32_MAX;
  perf_metrics_preempt_sample(&metrics, PERF_METRICS_PREEMPT_WAIT_END,
   counts[0], counts[1]);
  preempt_frame(&metrics, counts, 3, 2, 4, 3);
  result = snapshot(&metrics);
  assert(counts[0] == 5U && counts[1] == 4U);
  assert(result.preempt_work_thread == 11);
  assert(result.preempt_work_intr == 4);
  assert(result.preempt_wait_thread == 12);
  assert(result.preempt_wait_intr == 13);
  assert(result.preempt_max_per_frame == 12);

  metrics.preempt_work_thread = UINT64_MAX - 1U;
  metrics.preempt_wait_intr = UINT64_MAX;
  preempt_frame(&metrics, counts, 2, 0, 0, 1);
  result = snapshot(&metrics);
  assert(result.preempt_work_thread == UINT64_MAX);
  assert(result.preempt_wait_intr == UINT64_MAX);

  /* A new capture starts empty and again skips its first frame. */
  assert(perf_metrics_stop(&metrics));
  preempt_frame(&metrics, counts, 99, 0, 0, 0);
  assert(snapshot(&metrics).preempt_max_per_frame == 12);
  assert(perf_metrics_start(&metrics));
  preempt_frame(&metrics, counts, 1, 0, 0, 0);
  result = snapshot(&metrics);
  assert(result.preempt_max_per_frame == 0);
  assert(result.preempt_frames[0] == 0 && result.preempt_frames[1] == 0);
}

/* A refused read turns the preemption counts off but not the capture:
   frames still count, later reads are ignored, and the snapshot says
   preempt_available = 0 until a new capture starts. */
static void test_preempt_unavailable_path(void)
{
  PerfMetrics metrics;
  PerfMetricsSnapshot result;
  uint32_t counts[2] = { 0, 0 };

  perf_metrics_preempt_unavailable(NULL);
  assert(!perf_metrics_preempt_wanted(NULL));
  perf_metrics_init(&metrics);
  assert(!perf_metrics_preempt_wanted(&metrics));
  perf_metrics_preempt_unavailable(&metrics);

  assert(perf_metrics_start(&metrics));
  assert(perf_metrics_preempt_wanted(&metrics));
  assert(snapshot(&metrics).preempt_available == 1);
  preempt_frame(&metrics, counts, 0, 0, 0, 0);
  preempt_frame(&metrics, counts, 2, 1, 0, 3);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);

  perf_metrics_preempt_unavailable(&metrics);
  assert(!perf_metrics_preempt_wanted(&metrics));
  assert(perf_metrics_active(&metrics));
  preempt_frame(&metrics, counts, 7, 7, 7, 7);
  preempt_frame(&metrics, counts, 7, 7, 7, 7);
  perf_metrics_record_frame(&metrics, 16000, 0, 0, 0);
  result = snapshot(&metrics);
  assert(result.preempt_available == 0);
  assert(result.emulated_frames == 2);
  assert(result.preempt_work_thread == 2);
  assert(result.preempt_wait_intr == 3);
  assert(result.preempt_max_per_frame == 6);

  assert(perf_metrics_stop(&metrics));
  assert(snapshot(&metrics).preempt_available == 0);
  assert(perf_metrics_start(&metrics));
  assert(perf_metrics_preempt_wanted(&metrics));
  result = snapshot(&metrics);
  assert(result.preempt_available == 1);
  assert(result.preempt_work_thread == 0);
}

/* The call cost comes from reads timed together when a capture starts. */
static void test_preempt_call_cost(void)
{
  PerfMetrics metrics;

  perf_metrics_record_preempt_calibration(NULL, 40, 16);
  perf_metrics_init(&metrics);
  perf_metrics_record_preempt_calibration(&metrics, 40, 16);
  assert(snapshot(&metrics).preempt_call_ns == 0);

  assert(perf_metrics_start(&metrics));
  perf_metrics_record_preempt_calibration(&metrics, 40,
   PERF_METRICS_PREEMPT_CALIBRATION_CALLS);
  assert(snapshot(&metrics).preempt_call_ns == 2500);
  perf_metrics_record_preempt_calibration(&metrics, 5, 0);
  assert(snapshot(&metrics).preempt_call_ns == 2500);
  perf_metrics_record_preempt_calibration(&metrics, 1, 3);
  assert(snapshot(&metrics).preempt_call_ns == 333);
  perf_metrics_record_preempt_calibration(&metrics, UINT32_MAX, 1);
  assert(snapshot(&metrics).preempt_call_ns == UINT32_MAX);

  assert(perf_metrics_stop(&metrics));
  assert(perf_metrics_start(&metrics));
  assert(snapshot(&metrics).preempt_call_ns == 0);
}

/* Runs every counter case; a failing assert aborts the run. */
int main(void)
{
  test_session_states();
  test_counters_and_watermarks();
  test_percentiles_and_totals();
  test_empty_and_overflow_percentiles();
  test_saturating_counters();
  test_wait_and_frame_probes();
  test_work_time_subtracts_waits();
  test_audio_drain_leaves_work_only();
  test_work_time_percentiles();
  test_work_time_clamps_at_zero();
  test_waits_reset_each_frame();
  test_over_budget_and_late_counts();
  test_new_counters_saturate();
  test_link_lockstep_frames();
  test_phase_stride_is_deterministic_and_coprime();
  test_nested_phase_subtraction();
  test_sampled_frames_excluded_from_histograms();
  test_probe_cost_reported();
  test_phase_sum_within_sampled_work();
  test_preempt_histogram_bins();
  test_preempt_max_per_frame();
  test_preempt_unavailable_path();
  test_preempt_call_cost();
  puts("Performance metrics tests passed");
  return 0;
}
