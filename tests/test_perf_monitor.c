#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "perf_monitor.h"

static PerfMetricsSnapshot snapshot(void)
{
  PerfMetricsSnapshot result;

  perf_monitor_snapshot(&result);
  return result;
}

static void test_duration_and_states(void)
{
  PerfMetricsSnapshot result;

  perf_monitor_init();
  assert(!perf_monitor_active());
  assert(perf_monitor_duration_us() == 0);
  assert(snapshot().state == PERF_METRICS_IDLE);

  assert(perf_monitor_start(100));
  assert(perf_monitor_active());
  assert(!perf_monitor_start(200));
  assert(perf_monitor_duration_us() == 0);

  perf_monitor_record_frame(16667, 500, 0, 0);
  assert(perf_monitor_stop(600));
  assert(!perf_monitor_stop(700));
  assert(perf_monitor_duration_us() == 500);
  result = snapshot();
  assert(result.state == PERF_METRICS_STOPPED);
  assert(result.emulated_frames == 1);
  assert(result.presented_frames == 1);
  assert(result.skipped_frames == 0);

  assert(perf_monitor_start(1000));
  assert(perf_monitor_duration_us() == 0);
  perf_monitor_record_frame(17000, 0, 1, 1);
  perf_monitor_invalidate(1250);
  assert(!perf_monitor_active());
  assert(perf_monitor_duration_us() == 250);
  result = snapshot();
  assert(result.state == PERF_METRICS_INVALIDATED);
  assert(result.emulated_frames == 1);
  assert(result.presented_frames == 0);
  assert(result.skipped_frames == 1);
  assert(result.fast_forward_frames == 1);

  perf_monitor_record_frame(18000, 100, 0, 0);
  assert(snapshot().emulated_frames == 1);
}

static void test_events_and_watermarks(void)
{
  PerfMetricsSnapshot result;

  perf_monitor_init();
  assert(perf_monitor_start(500));
  perf_monitor_record_audio_recovery();
  perf_monitor_record_audio_underrun();
  perf_monitor_record_dynarec_compilation();
  perf_monitor_record_dynarec_reuse();
  perf_monitor_record_dynarec_retry();
  perf_monitor_record_dynarec_flush(1);
  perf_monitor_observe_dynarec_cache(4096, 1024);
  perf_monitor_observe_memory_available(8192);
  perf_monitor_observe_memory_available(6144);
  assert(perf_monitor_stop(400));

  result = snapshot();
  assert(result.audio_recoveries == 1);
  assert(result.audio_underruns == 1);
  assert(result.dynarec_compilations == 1);
  assert(result.dynarec_reuse_hits == 1);
  assert(result.dynarec_retries == 1);
  assert(result.dynarec_flushes == 1);
  assert(result.dynarec_readonly_flushes == 0);
  assert(result.dynarec_writable_flushes == 1);
  assert(result.dynarec_cache_high_water == 5120);
  assert(result.dynarec_readonly_high_water == 4096);
  assert(result.dynarec_writable_high_water == 1024);
  assert(result.memory_low_water == 6144);
  assert(perf_monitor_duration_us() == 0);
}

int main(void)
{
  test_duration_and_states();
  test_events_and_watermarks();
  puts("Performance monitor tests passed");
  return 0;
}
