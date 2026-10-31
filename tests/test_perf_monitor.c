/*
 * Host tests for perf_monitor.c, the emulator-wide performance session: a
 * single PerfMetrics wrapped with start and stop times (in microseconds)
 * and the identity of the game the session measured.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
  perf_monitor_record_fast_forward_transition();
  perf_monitor_record_fast_forward_audio_fallback();
  perf_monitor_record_dynarec_compilation();
  perf_monitor_record_dynarec_reuse();
  perf_monitor_record_dynarec_retry();
  perf_monitor_record_dynarec_flush(1);
  perf_monitor_observe_dynarec_cache(4096, 1024);
  perf_monitor_observe_memory_available(8192);
  perf_monitor_observe_memory_available(6144);
  /* Stopping before the start time (a clock that went backwards) still
     works and gives a zero duration. */
  assert(perf_monitor_stop(400));

  result = snapshot();
  assert(result.audio_recoveries == 1);
  assert(result.audio_underruns == 1);
  assert(result.fast_forward_transitions == 1);
  assert(result.fast_forward_audio_fallbacks == 1);
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

static void test_wait_and_frame_wrappers(void)
{
  PerfMetricsSnapshot result;

  perf_monitor_init();
  perf_monitor_record_wait(PERF_METRICS_WAIT_BACKPRESSURE, 100);
  perf_monitor_record_renderer_frame(100);
  perf_monitor_record_gba_update_entry();
  perf_monitor_record_dma(0, 10, 100, 1);
  perf_monitor_record_dma(1, 10, 100, 1);
  perf_monitor_record_gamepak_evict_scan(100);
  perf_monitor_record_gamepak_rom_io(100, 200, 32 * 1024, 1);
  assert(snapshot().vblank_backpressure_count == 0);
  assert(snapshot().renderer_total_us == 0);
  assert(snapshot().gba_update_entries == 0);
  assert(snapshot().dma16_transfers == 0);
  assert(snapshot().dma32_transfers == 0);
  assert(snapshot().gamepak_miss_count == 0);

  assert(perf_monitor_start(500));
  perf_monitor_record_wait(PERF_METRICS_WAIT_BACKPRESSURE, 1000);
  perf_monitor_record_wait(PERF_METRICS_WAIT_PRESENT, 2000);
  perf_monitor_record_wait(PERF_METRICS_WAIT_AUDIO, 3000);
  perf_monitor_record_renderer_frame(1200);
  perf_monitor_record_renderer_frame(800);
  perf_monitor_record_dcache_writeback(150);
  perf_monitor_record_ge_sync_direct(350);
  perf_monitor_record_quality_presented(1);
  perf_monitor_record_quality_presented(0);
  perf_monitor_record_gba_update_entry();
  perf_monitor_record_gba_update_iteration();
  perf_monitor_record_dma(0, 10, 100, 1);
  perf_monitor_record_dma(1, 8, 200, 1);
  perf_monitor_record_gamepak_evict_scan(120);
  perf_monitor_record_gamepak_rom_io(50, 200, 32 * 1024, 1);
  assert(perf_monitor_stop(400));

  result = snapshot();
  assert(result.vblank_backpressure_count == 1);
  assert(result.vblank_backpressure_total_us == 1000);
  assert(result.vblank_present_count == 1);
  assert(result.vblank_present_total_us == 2000);
  assert(result.vblank_audio_count == 1);
  assert(result.vblank_audio_total_us == 3000);
  assert(result.renderer_total_us == 2000);
  assert(result.renderer_max_us == 1200);
  assert(result.dcache_writeback_count == 1);
  assert(result.dcache_writeback_total_us == 150);
  assert(result.ge_sync_direct_count == 1);
  assert(result.ge_sync_direct_total_us == 350);
  assert(result.sharp_presented_frames == 1);
  assert(result.crisp_presented_frames == 1);
  assert(result.gba_update_entries == 1);
  assert(result.gba_update_iterations == 1);
  assert(result.dma16_transfers == 1);
  assert(result.dma16_elements == 10);
  assert(result.dma16_bytes == 20);
  assert(result.dma16_total_us == 100);
  assert(result.dma32_transfers == 1);
  assert(result.dma32_elements == 8);
  assert(result.dma32_bytes == 32);
  assert(result.dma32_total_us == 200);
  assert(result.dma_gamepak_pairs == 2);
  assert(result.gamepak_evict_scan_count == 1);
  assert(result.gamepak_evict_scan_total_us == 120);
  assert(result.gamepak_miss_count == 1);
  assert(result.gamepak_miss_bytes == 32 * 1024);
  assert(result.gamepak_read_errors == 1);
  assert(result.gamepak_rom_seek_total_us == 50);
  assert(result.gamepak_rom_read_total_us == 200);

  perf_monitor_init();
  result = snapshot();
  assert(result.vblank_backpressure_count == 0);
  assert(result.renderer_total_us == 0);
  assert(result.dma16_transfers == 0);
  assert(result.gamepak_miss_count == 0);
}

/* The identity is copied in and frozen once the session ends, so loading
   another ROM afterwards cannot change the report; a new session starts
   with an empty identity. */
static void test_capture_identity_survives_rom_change(void)
{
  PerfMonitorIdentity original = { 0 };
  PerfMonitorIdentity replacement = { 0 };
  PerfMonitorIdentity saved;

  memcpy(original.rom_title, "FIRST GAME  ", 12);
  memcpy(original.game_code, "ABCD", 4);
  original.rom_revision = 2;
  original.gamepak_file_size = 0x200000;
  original.clock_mhz = 333;
  memcpy(replacement.rom_title, "SECOND GAME ", 12);
  replacement.clock_mhz = 222;

  perf_monitor_init();
  assert(perf_monitor_start(0));
  perf_monitor_set_identity(&original);
  assert(perf_monitor_stop(100));
  perf_monitor_set_identity(&replacement);
  perf_monitor_identity(&saved);
  assert(memcmp(&saved, &original, sizeof(saved)) == 0);
  memset(&original, 0, sizeof(original));
  perf_monitor_identity(&saved);
  assert(memcmp(saved.rom_title, "FIRST GAME  ", 12) == 0);
  assert(memcmp(saved.game_code, "ABCD", 4) == 0);
  assert(saved.rom_revision == 2);

  assert(perf_monitor_start(200));
  perf_monitor_identity(&saved);
  assert(saved.gamepak_file_size == 0);
  perf_monitor_set_identity(&replacement);
  perf_monitor_invalidate(300);
  perf_monitor_set_identity(&original);
  perf_monitor_identity(&saved);
  assert(memcmp(&saved, &replacement, sizeof(saved)) == 0);
}

/* Runs every session case; a failing assert aborts the run. */
int main(void)
{
  test_duration_and_states();
  test_events_and_watermarks();
  test_wait_and_frame_wrappers();
  test_capture_identity_survives_rom_change();
  puts("Performance monitor tests passed");
  return 0;
}
