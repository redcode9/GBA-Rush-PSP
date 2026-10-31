/*
 * Host tests for perf_metrics.c, the counters behind the performance
 * report: session states (only an active session records), counters,
 * high and low watermarks, histogram percentiles and saturation of every
 * counter at its type's maximum.
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

int main(void)
{
  test_session_states();
  test_counters_and_watermarks();
  test_percentiles_and_totals();
  test_empty_and_overflow_percentiles();
  test_saturating_counters();
  test_wait_and_frame_probes();
  puts("Performance metrics tests passed");
  return 0;
}
