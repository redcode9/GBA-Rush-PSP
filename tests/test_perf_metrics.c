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

  perf_metrics_record_emulated_frame(&metrics, 16667, 1);
  perf_metrics_record_emulated_frame(&metrics, 16668, 0);
  perf_metrics_record_presented_frame(&metrics, 300);
  perf_metrics_record_audio_recovery(&metrics);
  perf_metrics_record_audio_underrun(&metrics);
  perf_metrics_record_dynarec_compilation(&metrics);
  perf_metrics_record_dynarec_reuse(&metrics);
  perf_metrics_record_dynarec_retry(&metrics);
  perf_metrics_record_dynarec_flush(&metrics, 0);
  perf_metrics_record_dynarec_flush(&metrics, 1);
  perf_metrics_observe_dynarec_cache(&metrics, 1024, 512);
  perf_metrics_observe_dynarec_cache(&metrics, 900, 400);
  perf_metrics_observe_dynarec_cache(&metrics, 2048, 256);
  perf_metrics_observe_memory_available(&metrics, 8192);
  perf_metrics_observe_memory_available(&metrics, 12288);
  perf_metrics_observe_memory_available(&metrics, 4096);

  result = snapshot(&metrics);
  assert(result.emulated_frames == 2);
  assert(result.fast_forward_frames == 1);
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

int main(void)
{
  test_session_states();
  test_counters_and_watermarks();
  test_percentiles_and_totals();
  test_empty_and_overflow_percentiles();
  test_saturating_counters();
  puts("Performance metrics tests passed");
  return 0;
}
