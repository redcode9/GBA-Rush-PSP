/*
 * Host tests for perf_report.c, which writes a performance session as
 * key=value lines for scripts/compare-benchmarks.py. The golden report
 * pins every key, its order and the format version.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "perf_report.h"

static void test_golden_report(void)
{
  /* NUL, '/' and newline in the cartridge title come out as '_', so a
     title can neither break a line nor look like a path. */
  static const uint8_t rom_title[] =
  {
    'P', 'O', 'K', 'E', 0, '/', 'X', '\n'
  };
  static const uint8_t game_code[] = { 'B', 'P', 'R', 'E' };
  static const char expected[] =
   "gba-rush-performance-report=6\n"
   "version=1.3.0\n"
   "psp-model=1\n"
   "clock-mhz=333\n"
   "psp-fw-version=600\n"
   "vsync=1\n"
   "renderer=1\n"
   "scale=6\n"
   "filter=1\n"
   "frameskip-type=0\n"
   "frameskip-value=9\n"
   "sound-volume=10\n"
   "audio-sample-rate=48000\n"
   "audio-samples=800\n"
   "fast-forward-game-override=1\n"
   "fast-forward-profile=2\n"
   "fast-forward-activation=1\n"
   "fast-forward-speed=1\n"
   "fast-forward-presentation=0\n"
   "fast-forward-audio=1\n"
   "rom-title=POKE__X_\n"
   "game-code=BPRE\n"
   "rom-revision=1\n"
   "duration-us=3000000\n"
   "gamepak-file-size=67108864\n"
   "gamepak-ram-buffer-size=33554432\n"
   "gamepak-ram-pages=1024\n"
   "state=stopped\n"
   "emulated-frames=180\n"
   "presented-frames=175\n"
   "skipped-frames=5\n"
   "fast-forward-frames=30\n"
   "fast-forward-presented-frames=15\n"
   "fast-forward-time-us=250000\n"
   "fast-forward-transitions=4\n"
   "fast-forward-audio-fallbacks=1\n"
   "audio-recoveries=2\n"
   "audio-underruns=1\n"
   "dynarec-compilations=40\n"
   "dynarec-reuse-hits=8\n"
   "dynarec-retries=2\n"
   "dynarec-flushes=3\n"
   "dynarec-readonly-flushes=1\n"
   "dynarec-writable-flushes=2\n"
   "dynarec-cache-high-water=65536\n"
   "dynarec-readonly-high-water=49152\n"
   "dynarec-writable-high-water=16384\n"
   "memory-low-water=4194304\n"
   "memory-sampled=1\n"
   "frame-time-samples=180\n"
   "frame-time-total-us=3000000\n"
   "frame-time-max-us=22000\n"
   "frame-time-p50-us=16750\n"
   "frame-time-p95-us=17250\n"
   "frame-time-p99-us=20000\n"
   "ge-wait-samples=175\n"
   "ge-wait-total-us=87500\n"
   "ge-wait-max-us=3000\n"
   "ge-wait-p50-us=500\n"
   "ge-wait-p95-us=1000\n"
   "ge-wait-p99-us=2500\n"
   "vblank-backpressure-count=2\n"
   "vblank-backpressure-total-us=1500\n"
   "vblank-present-count=175\n"
   "vblank-present-total-us=350000\n"
   "vblank-audio-count=1\n"
   "vblank-audio-total-us=2000\n"
   "renderer-total-us=1980000\n"
   "renderer-max-us=22000\n"
   "dcache-writeback-count=175\n"
   "dcache-writeback-total-us=35000\n"
   "ge-sync-direct-count=180\n"
   "ge-sync-direct-total-us=36000\n"
   "sharp-presented-frames=170\n"
   "crisp-presented-frames=5\n"
   "gba-update-entries=180\n"
   "gba-update-iterations=185\n"
   "dma16-transfers=180\n"
   "dma16-elements=12345\n"
   "dma16-bytes=24690\n"
   "dma16-total-us=360000\n"
   "dma32-transfers=90\n"
   "dma32-elements=6553\n"
   "dma32-bytes=26212\n"
   "dma32-total-us=180000\n"
   "dma-gamepak-pairs=120\n"
   "gamepak-evict-scan-count=64\n"
   "gamepak-evict-scan-total-us=12800\n"
   "gamepak-miss-count=64\n"
   "gamepak-miss-bytes=2097152\n"
   "gamepak-read-errors=1\n"
   "gamepak-rom-seek-total-us=3200\n"
   "gamepak-rom-read-total-us=96000\n";
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];
  int length;

  memset(&metadata, 0, sizeof(metadata));
  metadata.emulator_version = "1.3.0";
  metadata.psp_model = 1;
  metadata.clock_mhz = 333;
  metadata.psp_sdk_fw_macro = 600;
  metadata.psp_vsync = 1;
  metadata.renderer = 1;
  metadata.scale = 6;
  metadata.filter = 1;
  metadata.frameskip_type = 0;
  metadata.frameskip_value = 9;
  metadata.sound_volume = 10;
  metadata.audio_sample_rate = 48000;
  metadata.audio_samples = 800;
  metadata.fast_forward_game_override = 1;
  metadata.fast_forward_profile = 2;
  metadata.fast_forward_activation = 1;
  metadata.fast_forward_speed = 1;
  metadata.fast_forward_presentation = 0;
  metadata.fast_forward_audio = 1;
  metadata.rom_title = rom_title;
  metadata.rom_title_length = sizeof(rom_title);
  metadata.game_code = game_code;
  metadata.game_code_length = sizeof(game_code);
  metadata.rom_revision = 1;
  metadata.duration_us = 3000000;
  metadata.gamepak_file_size = 67108864;
  metadata.gamepak_ram_buffer_size = 33554432;
  metadata.gamepak_ram_pages = 1024;

  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  metrics.emulated_frames = 180;
  metrics.presented_frames = 175;
  metrics.skipped_frames = 5;
  metrics.fast_forward_frames = 30;
  metrics.fast_forward_presented_frames = 15;
  metrics.fast_forward_time_us = 250000;
  metrics.fast_forward_transitions = 4;
  metrics.fast_forward_audio_fallbacks = 1;
  metrics.audio_recoveries = 2;
  metrics.audio_underruns = 1;
  metrics.dynarec_compilations = 40;
  metrics.dynarec_reuse_hits = 8;
  metrics.dynarec_retries = 2;
  metrics.dynarec_flushes = 3;
  metrics.dynarec_readonly_flushes = 1;
  metrics.dynarec_writable_flushes = 2;
  metrics.dynarec_cache_high_water = 65536;
  metrics.dynarec_readonly_high_water = 49152;
  metrics.dynarec_writable_high_water = 16384;
  metrics.memory_low_water = 4194304;
  metrics.has_memory_sample = 1;
  metrics.frame_time_samples = 180;
  metrics.frame_time_total_us = 3000000;
  metrics.frame_time_max_us = 22000;
  metrics.frame_time_p50_us = 16750;
  metrics.frame_time_p95_us = 17250;
  metrics.frame_time_p99_us = 20000;
  metrics.ge_wait_samples = 175;
  metrics.ge_wait_total_us = 87500;
  metrics.ge_wait_max_us = 3000;
  metrics.ge_wait_p50_us = 500;
  metrics.ge_wait_p95_us = 1000;
  metrics.ge_wait_p99_us = 2500;
  metrics.vblank_backpressure_count = 2;
  metrics.vblank_backpressure_total_us = 1500;
  metrics.vblank_present_count = 175;
  metrics.vblank_present_total_us = 350000;
  metrics.vblank_audio_count = 1;
  metrics.vblank_audio_total_us = 2000;
  metrics.renderer_total_us = 1980000;
  metrics.renderer_max_us = 22000;
  metrics.dcache_writeback_count = 175;
  metrics.dcache_writeback_total_us = 35000;
  metrics.ge_sync_direct_count = 180;
  metrics.ge_sync_direct_total_us = 36000;
  metrics.sharp_presented_frames = 170;
  metrics.crisp_presented_frames = 5;
  metrics.gba_update_entries = 180;
  metrics.gba_update_iterations = 185;
  metrics.dma16_transfers = 180;
  metrics.dma16_elements = 12345;
  metrics.dma16_bytes = 24690;
  metrics.dma16_total_us = 360000;
  metrics.dma32_transfers = 90;
  metrics.dma32_elements = 6553;
  metrics.dma32_bytes = 26212;
  metrics.dma32_total_us = 180000;
  metrics.dma_gamepak_pairs = 120;
  metrics.gamepak_evict_scan_count = 64;
  metrics.gamepak_evict_scan_total_us = 12800;
  metrics.gamepak_miss_count = 64;
  metrics.gamepak_miss_bytes = 2097152;
  metrics.gamepak_read_errors = 1;
  metrics.gamepak_rom_seek_total_us = 3200;
  metrics.gamepak_rom_read_total_us = 96000;

  length = perf_report_format(report, sizeof(report), &metadata, &metrics);
  assert(length == (int)strlen(expected));
  assert(strcmp(report, expected) == 0);
  assert(strchr(report, '/') == NULL);
}

static void test_states_and_empty_identity(void)
{
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));

  metrics.state = PERF_METRICS_IDLE;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "state=idle\n") != NULL);
  assert(strstr(report, "version=\n") != NULL);
  assert(strstr(report, "rom-title=\n") != NULL);

  metrics.state = PERF_METRICS_ACTIVE;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "state=active\n") != NULL);

  metrics.state = PERF_METRICS_INVALIDATED;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "state=invalidated\n") != NULL);
}

static void test_invalid_arguments_and_small_buffer(void)
{
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[16];

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));
  memset(report, 'X', sizeof(report));

  assert(perf_report_format(NULL, 0, &metadata, &metrics) < 0);
  assert(perf_report_format(report, sizeof(report), NULL, &metrics) < 0);
  assert(report[0] == '\0');

  memset(report, 'X', sizeof(report));
  assert(perf_report_format(report, sizeof(report), &metadata, NULL) < 0);
  assert(report[0] == '\0');

  memset(report, 'X', sizeof(report));
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) < 0);
  assert(report[0] == '\0');
}

/* Every field at its longest fills PERF_REPORT_CAPACITY exactly, so the
   capacity is proven sufficient and one byte less is refused cleanly. */
static void test_worst_case_capacity(void)
{
  static char version[64];
  static uint8_t rom_title[13];
  static uint8_t game_code[5];
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];
  size_t i;
  int length;

  for(i = 0; i + 1U < sizeof(version); i++)
    version[i] = 'A';
  for(i = 0; i + 1U < sizeof(rom_title); i++)
    rom_title[i] = 'B';
  for(i = 0; i + 1U < sizeof(game_code); i++)
    game_code[i] = 'C';

  memset(&metadata, 0, sizeof(metadata));
  metadata.emulator_version = version;
  metadata.psp_model = UINT32_MAX;
  metadata.clock_mhz = UINT32_MAX;
  metadata.psp_sdk_fw_macro = UINT32_MAX;
  metadata.psp_vsync = UINT32_MAX;
  metadata.renderer = UINT32_MAX;
  metadata.scale = UINT32_MAX;
  metadata.filter = UINT32_MAX;
  metadata.frameskip_type = UINT32_MAX;
  metadata.frameskip_value = UINT32_MAX;
  metadata.sound_volume = UINT32_MAX;
  metadata.audio_sample_rate = UINT32_MAX;
  metadata.audio_samples = UINT32_MAX;
  metadata.fast_forward_game_override = UINT32_MAX;
  metadata.fast_forward_profile = UINT32_MAX;
  metadata.fast_forward_activation = UINT32_MAX;
  metadata.fast_forward_speed = UINT32_MAX;
  metadata.fast_forward_presentation = UINT32_MAX;
  metadata.fast_forward_audio = UINT32_MAX;
  metadata.rom_title = rom_title;
  metadata.rom_title_length = sizeof(rom_title);
  metadata.game_code = game_code;
  metadata.game_code_length = sizeof(game_code);
  metadata.rom_revision = UINT32_MAX;
  metadata.duration_us = UINT64_MAX;
  metadata.gamepak_file_size = UINT32_MAX;
  metadata.gamepak_ram_buffer_size = UINT32_MAX;
  metadata.gamepak_ram_pages = UINT32_MAX;

  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_INVALIDATED;
  metrics.emulated_frames = UINT64_MAX;
  metrics.presented_frames = UINT64_MAX;
  metrics.skipped_frames = UINT64_MAX;
  metrics.fast_forward_frames = UINT64_MAX;
  metrics.fast_forward_presented_frames = UINT64_MAX;
  metrics.fast_forward_time_us = UINT64_MAX;
  metrics.fast_forward_transitions = UINT64_MAX;
  metrics.fast_forward_audio_fallbacks = UINT64_MAX;
  metrics.audio_recoveries = UINT64_MAX;
  metrics.audio_underruns = UINT64_MAX;
  metrics.dynarec_compilations = UINT64_MAX;
  metrics.dynarec_reuse_hits = UINT64_MAX;
  metrics.dynarec_retries = UINT64_MAX;
  metrics.dynarec_flushes = UINT64_MAX;
  metrics.dynarec_readonly_flushes = UINT64_MAX;
  metrics.dynarec_writable_flushes = UINT64_MAX;
  metrics.dynarec_cache_high_water = UINT64_MAX;
  metrics.dynarec_readonly_high_water = UINT64_MAX;
  metrics.dynarec_writable_high_water = UINT64_MAX;
  metrics.memory_low_water = UINT64_MAX;
  metrics.has_memory_sample = 1;
  metrics.frame_time_samples = UINT64_MAX;
  metrics.frame_time_total_us = UINT64_MAX;
  metrics.frame_time_max_us = UINT32_MAX;
  metrics.frame_time_p50_us = UINT32_MAX;
  metrics.frame_time_p95_us = UINT32_MAX;
  metrics.frame_time_p99_us = UINT32_MAX;
  metrics.ge_wait_samples = UINT64_MAX;
  metrics.ge_wait_total_us = UINT64_MAX;
  metrics.ge_wait_max_us = UINT32_MAX;
  metrics.ge_wait_p50_us = UINT32_MAX;
  metrics.ge_wait_p95_us = UINT32_MAX;
  metrics.ge_wait_p99_us = UINT32_MAX;
  metrics.vblank_backpressure_count = UINT64_MAX;
  metrics.vblank_backpressure_total_us = UINT64_MAX;
  metrics.vblank_present_count = UINT64_MAX;
  metrics.vblank_present_total_us = UINT64_MAX;
  metrics.vblank_audio_count = UINT64_MAX;
  metrics.vblank_audio_total_us = UINT64_MAX;
  metrics.renderer_total_us = UINT64_MAX;
  metrics.renderer_max_us = UINT32_MAX;
  metrics.dcache_writeback_count = UINT64_MAX;
  metrics.dcache_writeback_total_us = UINT64_MAX;
  metrics.ge_sync_direct_count = UINT64_MAX;
  metrics.ge_sync_direct_total_us = UINT64_MAX;
  metrics.sharp_presented_frames = UINT64_MAX;
  metrics.crisp_presented_frames = UINT64_MAX;
  metrics.gba_update_entries = UINT64_MAX;
  metrics.gba_update_iterations = UINT64_MAX;
  metrics.dma16_transfers = UINT64_MAX;
  metrics.dma16_elements = UINT64_MAX;
  metrics.dma16_bytes = UINT64_MAX;
  metrics.dma16_total_us = UINT64_MAX;
  metrics.dma32_transfers = UINT64_MAX;
  metrics.dma32_elements = UINT64_MAX;
  metrics.dma32_bytes = UINT64_MAX;
  metrics.dma32_total_us = UINT64_MAX;
  metrics.dma_gamepak_pairs = UINT64_MAX;
  metrics.gamepak_evict_scan_count = UINT64_MAX;
  metrics.gamepak_evict_scan_total_us = UINT64_MAX;
  metrics.gamepak_miss_count = UINT64_MAX;
  metrics.gamepak_miss_bytes = UINT64_MAX;
  metrics.gamepak_read_errors = UINT64_MAX;
  metrics.gamepak_rom_seek_total_us = UINT64_MAX;
  metrics.gamepak_rom_read_total_us = UINT64_MAX;

  length = perf_report_format(report, sizeof(report), &metadata, &metrics);
  assert(length == (int)sizeof(report) - 1);

  memset(report, 'X', sizeof(report));
  assert(perf_report_format(report, sizeof(report) - 1U, &metadata,
   &metrics) < 0);
  assert(report[0] == '\0');
}

/* Runs every report case; a failing assert aborts the run. */
int main(void)
{
  test_golden_report();
  test_states_and_empty_identity();
  test_invalid_arguments_and_small_buffer();
  test_worst_case_capacity();
  puts("Performance report tests passed");
  return 0;
}
