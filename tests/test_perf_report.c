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
   "gba-rush-performance-report=7\n"
   "version=1.3.0\n"
   "build-id=0123456789ab-dirty\n"
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
   "heap-free-low=1048576\n"
   "heap-largest-low=8388608\n"
   "kernel-free-low=3145728\n"
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
   "gamepak-rom-read-total-us=96000\n"
   "work-time-samples=180\n"
   "work-time-total-us=1800000\n"
   "work-time-max-us=21000\n"
   "work-time-p50-us=10000\n"
   "work-time-p95-us=15250\n"
   "work-time-p99-us=17000\n"
   "frames-work-over-budget=2\n"
   "frames-over-1-5x-budget=0\n"
   "frames-late=7\n"
   "link-lockstep-frames=0\n"
   "capture-local-time=2026-03-07T09:05:01\n"
   "phase-main-sampled-frames=16\n"
   "phase-main-update-gba-us=48000\n"
   "phase-main-scanline-us=96000\n"
   "phase-main-gbc-sound-us=8000\n"
   "phase-main-work-us=180000\n"
   "probe-read-ns=312\n"
   "probe-reads=24576\n"
   "preempt-available=1\n"
   "preempt-work-thread=412\n"
   "preempt-work-intr=1960\n"
   "preempt-wait-thread=35\n"
   "preempt-wait-intr=180\n"
   "preempt-max-per-frame=19\n"
   "preempt-frames-0=2\n"
   "preempt-frames-1=51\n"
   "preempt-frames-2=60\n"
   "preempt-frames-3-4=40\n"
   "preempt-frames-5-8=20\n"
   "preempt-frames-9-16=4\n"
   "preempt-frames-17-plus=1\n"
   "preempt-call-ns=1850\n"
   "bench-mode=unbound\n"
   "bench-frames=3600\n"
   "bench-render=1\n"
   "bench-audio=1\n"
   "bench-input=none\n"
   "bench-trace-checksum=\n";
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];
  int length;

  memset(&metadata, 0, sizeof(metadata));
  metadata.emulator_version = "1.3.0";
  metadata.build_id = "0123456789ab-dirty";
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
  metadata.heap_free_low = 1048576;
  metadata.heap_largest_low = 8388608;
  metadata.kernel_free_low = 3145728;
  metadata.has_capture_time = 1;
  metadata.capture_year = 2026;
  metadata.capture_month = 3;
  metadata.capture_day = 7;
  metadata.capture_hour = 9;
  metadata.capture_minute = 5;
  metadata.capture_second = 1;
  metadata.bench_mode = PERF_REPORT_BENCH_UNBOUND;
  metadata.bench_frames = 3600;
  metadata.bench_render = 1;
  metadata.bench_audio = 1;
  metadata.bench_input = PERF_REPORT_BENCH_INPUT_NONE;

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
  metrics.work_time_samples = 180;
  metrics.work_time_total_us = 1800000;
  metrics.work_time_max_us = 21000;
  metrics.work_time_p50_us = 10000;
  metrics.work_time_p95_us = 15250;
  metrics.work_time_p99_us = 17000;
  metrics.frames_work_over_budget = 2;
  metrics.frames_over_1_5x_budget = 0;
  metrics.frames_late = 7;
  metrics.link_lockstep_frames = 0;
  metrics.phase_sampled_frames = 16;
  metrics.phase_update_gba_us = 48000;
  metrics.phase_scanline_us = 96000;
  metrics.phase_gbc_sound_us = 8000;
  metrics.phase_work_us = 180000;
  metrics.probe_read_ns = 312;
  metrics.probe_reads = 24576;
  metrics.preempt_available = 1;
  metrics.preempt_work_thread = 412;
  metrics.preempt_work_intr = 1960;
  metrics.preempt_wait_thread = 35;
  metrics.preempt_wait_intr = 180;
  metrics.preempt_max_per_frame = 19;
  metrics.preempt_frames[0] = 2;
  metrics.preempt_frames[1] = 51;
  metrics.preempt_frames[2] = 60;
  metrics.preempt_frames[3] = 40;
  metrics.preempt_frames[4] = 20;
  metrics.preempt_frames[5] = 4;
  metrics.preempt_frames[6] = 1;
  metrics.preempt_call_ns = 1850;

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
  assert(strstr(report, "build-id=\n") != NULL);
  assert(strstr(report, "rom-title=\n") != NULL);
  /* No RTC read: the key stays, with an empty value. */
  assert(strstr(report, "\ncapture-local-time=\n") != NULL);
  metadata.capture_year = 2026;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\ncapture-local-time=\n") != NULL);

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
   capacity is proven sufficient and one byte less is refused cleanly.
   The build id is one character too long and comes out cut. */
static void test_worst_case_capacity(void)
{
  static char version[64];
  static char build_id[PERF_REPORT_BUILD_ID_MAX + 2U];
  static uint8_t rom_title[13];
  static uint8_t game_code[5];
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];
  const char *line;
  size_t i;
  int length;

  for(i = 0; i + 1U < sizeof(version); i++)
    version[i] = 'A';
  for(i = 0; i + 1U < sizeof(build_id); i++)
    build_id[i] = 'D';
  for(i = 0; i + 1U < sizeof(rom_title); i++)
    rom_title[i] = 'B';
  for(i = 0; i + 1U < sizeof(game_code); i++)
    game_code[i] = 'C';

  memset(&metadata, 0, sizeof(metadata));
  metadata.emulator_version = version;
  metadata.build_id = build_id;
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
  metadata.heap_free_low = UINT32_MAX;
  metadata.heap_largest_low = UINT32_MAX;
  metadata.kernel_free_low = UINT32_MAX;
  metadata.has_capture_time = 1;
  metadata.capture_year = UINT32_MAX;
  metadata.capture_month = UINT32_MAX;
  metadata.capture_day = UINT32_MAX;
  metadata.capture_hour = UINT32_MAX;
  metadata.capture_minute = UINT32_MAX;
  metadata.capture_second = UINT32_MAX;
  metadata.bench_mode = PERF_REPORT_BENCH_UNBOUND;
  metadata.bench_frames = UINT32_MAX;
  metadata.bench_render = UINT32_MAX;
  metadata.bench_audio = UINT32_MAX;
  metadata.bench_input = PERF_REPORT_BENCH_INPUT_TRACE;
  metadata.bench_trace_checksum = UINT32_MAX;

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
  metrics.work_time_samples = UINT64_MAX;
  metrics.work_time_total_us = UINT64_MAX;
  metrics.work_time_max_us = UINT32_MAX;
  metrics.work_time_p50_us = UINT32_MAX;
  metrics.work_time_p95_us = UINT32_MAX;
  metrics.work_time_p99_us = UINT32_MAX;
  metrics.frames_work_over_budget = UINT64_MAX;
  metrics.frames_over_1_5x_budget = UINT64_MAX;
  metrics.frames_late = UINT64_MAX;
  metrics.link_lockstep_frames = UINT64_MAX;
  metrics.phase_sampled_frames = UINT64_MAX;
  metrics.phase_update_gba_us = UINT64_MAX;
  metrics.phase_scanline_us = UINT64_MAX;
  metrics.phase_gbc_sound_us = UINT64_MAX;
  metrics.phase_work_us = UINT64_MAX;
  metrics.probe_read_ns = UINT32_MAX;
  metrics.probe_reads = UINT64_MAX;
  metrics.preempt_available = 1;
  metrics.preempt_work_thread = UINT64_MAX;
  metrics.preempt_work_intr = UINT64_MAX;
  metrics.preempt_wait_thread = UINT64_MAX;
  metrics.preempt_wait_intr = UINT64_MAX;
  metrics.preempt_max_per_frame = UINT64_MAX;
  for(i = 0; i < PERF_METRICS_PREEMPT_CLASSES; i++)
    metrics.preempt_frames[i] = UINT64_MAX;
  metrics.preempt_call_ns = UINT32_MAX;

  length = perf_report_format(report, sizeof(report), &metadata, &metrics);
  assert(length == (int)sizeof(report) - 1);
  line = strstr(report, "\nbuild-id=");
  assert(line != NULL);
  for(i = 0; i < PERF_REPORT_BUILD_ID_MAX; i++)
    assert(line[10 + i] == 'D');
  assert(line[10 + PERF_REPORT_BUILD_ID_MAX] == '\n');

  memset(report, 'X', sizeof(report));
  assert(perf_report_format(report, sizeof(report) - 1U, &metadata,
   &metrics) < 0);
  assert(report[0] == '\0');
}

/* The decimal value of key in a report, read digit by digit into 64 bits
   (the host's long may be 32 bits wide). */
static uint64_t report_value(const char *report, const char *key)
{
  char needle[64];
  size_t length = strlen(key);
  const char *at;
  uint64_t value = 0;

  assert(length + 3U <= sizeof(needle));
  needle[0] = '\n';
  memcpy(needle + 1, key, length);
  needle[length + 1] = '=';
  needle[length + 2] = '\0';
  at = strstr(report, needle);
  assert(at != NULL);
  at += strlen(needle);
  assert(*at >= '0' && *at <= '9');
  while(*at >= '0' && *at <= '9')
    value = value * 10U + (uint64_t)(*at++ - '0');
  assert(*at == '\n');
  return value;
}

/* A counter above 32 bits and the lockstep frames come back exactly. */
static void test_u64_and_link_values_read_back(void)
{
  const uint64_t big = ((uint64_t)1 << 40) + 1U;
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  metrics.work_time_total_us = big;
  metrics.frames_late = big;
  metrics.link_lockstep_frames = 3600;

  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(report_value(report, "work-time-total-us") == big);
  assert(report_value(report, "frames-late") == big);
  assert(report_value(report, "link-lockstep-frames") == 3600);
  assert(report_value(report, "frames-work-over-budget") == 0);
}

/* The phase and probe keys come back exactly, and every phase key starts
   with phase-main-, apart from the New core profile's phase-<name> keys. */
static void test_phase_and_probe_keys(void)
{
  const uint64_t big = ((uint64_t)1 << 40) + 3U;
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];
  const char *at;
  unsigned phase_keys = 0;

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  metrics.phase_sampled_frames = 327;
  metrics.phase_update_gba_us = big;
  metrics.phase_scanline_us = 1100000;
  metrics.phase_gbc_sound_us = 90000;
  metrics.phase_work_us = big + 5U;
  metrics.probe_read_ns = UINT32_MAX;
  metrics.probe_reads = big + 7U;

  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(report_value(report, "phase-main-sampled-frames") == 327);
  assert(report_value(report, "phase-main-update-gba-us") == big);
  assert(report_value(report, "phase-main-scanline-us") == 1100000);
  assert(report_value(report, "phase-main-gbc-sound-us") == 90000);
  assert(report_value(report, "phase-main-work-us") == big + 5U);
  assert(report_value(report, "probe-read-ns") == UINT32_MAX);
  assert(report_value(report, "probe-reads") == big + 7U);

  for(at = strstr(report, "\nphase-"); at != NULL;
      at = strstr(at + 1, "\nphase-"))
  {
    assert(strncmp(at, "\nphase-main-", 12) == 0);
    phase_keys++;
  }
  assert(phase_keys == 5);
}

/* The preemption keys come back exactly, and a capture whose read was
   refused says preempt-available=0. */
static void test_preempt_keys(void)
{
  const uint64_t big = ((uint64_t)1 << 40) + 9U;
  static const char *const class_keys[PERF_METRICS_PREEMPT_CLASSES] =
  {
    "preempt-frames-0", "preempt-frames-1", "preempt-frames-2",
    "preempt-frames-3-4", "preempt-frames-5-8", "preempt-frames-9-16",
    "preempt-frames-17-plus"
  };
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];
  uint32_t i;

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\npreempt-available=0\n") != NULL);

  metrics.preempt_available = 2;
  metrics.preempt_work_thread = big;
  metrics.preempt_work_intr = 7;
  metrics.preempt_wait_thread = big + 1U;
  metrics.preempt_wait_intr = 11;
  metrics.preempt_max_per_frame = 23;
  for(i = 0; i < PERF_METRICS_PREEMPT_CLASSES; i++)
    metrics.preempt_frames[i] = 100U + i;
  metrics.preempt_call_ns = 2125;

  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(report_value(report, "preempt-available") == 1);
  assert(report_value(report, "preempt-work-thread") == big);
  assert(report_value(report, "preempt-work-intr") == 7);
  assert(report_value(report, "preempt-wait-thread") == big + 1U);
  assert(report_value(report, "preempt-wait-intr") == 11);
  assert(report_value(report, "preempt-max-per-frame") == 23);
  for(i = 0; i < PERF_METRICS_PREEMPT_CLASSES; i++)
    assert(report_value(report, class_keys[i]) == 100U + i);
  assert(report_value(report, "preempt-call-ns") == 2125);
}

/* Every report carries the bench keys, so a paced capture says so; the
   unbound benchmark writes its frame count and gives the game no input. */
static void test_bench_keys(void)
{
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  metadata.bench_render = 1;
  metadata.bench_audio = 1;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\nbench-mode=paced\nbench-frames=0\n"
   "bench-render=1\nbench-audio=1\nbench-input=pad\n") != NULL);

  metadata.bench_mode = PERF_REPORT_BENCH_UNBOUND;
  metadata.bench_frames = 3600;
  metadata.bench_render = 0;
  metadata.bench_audio = 2;
  metadata.bench_input = PERF_REPORT_BENCH_INPUT_NONE;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\nbench-mode=unbound\n") != NULL);
  assert(report_value(report, "bench-frames") == 3600);
  assert(report_value(report, "bench-render") == 0);
  assert(report_value(report, "bench-audio") == 1);
  assert(strstr(report, "\nbench-input=none\n") != NULL);
}

/* The render-off and audio-off arms of diagnostic builds each write their
   own 0, and only that one. */
static void test_bench_arm_keys(void)
{
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  metadata.bench_mode = PERF_REPORT_BENCH_UNBOUND;
  metadata.bench_frames = 3600;
  metadata.bench_input = PERF_REPORT_BENCH_INPUT_NONE;

  metadata.bench_render = 0;
  metadata.bench_audio = 1;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\nbench-render=0\nbench-audio=1\n") != NULL);

  metadata.bench_render = 1;
  metadata.bench_audio = 0;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\nbench-render=1\nbench-audio=0\n") != NULL);
}

/* A benchmark that replays a diagnostic input trace says so and ends the
   report with the trace's checksum in eight hex digits; any other capture
   leaves that checksum empty, whatever the field holds. */
static void test_bench_trace_keys(void)
{
  static const char trace_tail[] =
   "\nbench-input=trace\nbench-trace-checksum=0x0badf00d\n";
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[PERF_REPORT_CAPACITY];
  int length;

  memset(&metadata, 0, sizeof(metadata));
  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  metadata.bench_mode = PERF_REPORT_BENCH_UNBOUND;
  metadata.bench_frames = 1234;
  metadata.bench_render = 1;
  metadata.bench_audio = 1;
  metadata.bench_input = PERF_REPORT_BENCH_INPUT_TRACE;
  metadata.bench_trace_checksum = 0x0BADF00DU;
  length = perf_report_format(report, sizeof(report), &metadata, &metrics);
  assert(length > (int)sizeof(trace_tail));
  assert(strcmp(report + length - (sizeof(trace_tail) - 1U),
   trace_tail) == 0);

  metadata.bench_trace_checksum = 0;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\nbench-trace-checksum=0x00000000\n") != NULL);

  metadata.bench_trace_checksum = 0x0BADF00DU;
  metadata.bench_input = PERF_REPORT_BENCH_INPUT_NONE;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\nbench-input=none\nbench-trace-checksum=\n") !=
   NULL);
  metadata.bench_mode = PERF_REPORT_BENCH_PACED;
  metadata.bench_input = PERF_REPORT_BENCH_INPUT_PAD;
  assert(perf_report_format(report, sizeof(report), &metadata, &metrics) > 0);
  assert(strstr(report, "\nbench-input=pad\nbench-trace-checksum=\n") !=
   NULL);
}

/* Runs every report case; a failing assert aborts the run. */
int main(void)
{
  test_golden_report();
  test_states_and_empty_identity();
  test_invalid_arguments_and_small_buffer();
  test_worst_case_capacity();
  test_u64_and_link_values_read_back();
  test_phase_and_probe_keys();
  test_preempt_keys();
  test_bench_keys();
  test_bench_arm_keys();
  test_bench_trace_keys();
  puts("Performance report tests passed");
  return 0;
}
