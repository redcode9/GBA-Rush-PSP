#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "perf_report.h"

static void test_golden_report(void)
{
  static const uint8_t rom_title[] =
  {
    'P', 'O', 'K', 'E', 0, '/', 'X', '\n'
  };
  static const uint8_t game_code[] = { 'B', 'P', 'R', 'E' };
  static const char expected[] =
   "gba-rush-performance-report=2\n"
   "version=1.3.0\n"
   "psp-model=1\n"
   "clock-mhz=333\n"
   "renderer=1\n"
   "scale=6\n"
   "filter=1\n"
   "frameskip-type=0\n"
   "frameskip-value=9\n"
   "sound-volume=10\n"
   "rom-title=POKE__X_\n"
   "game-code=BPRE\n"
   "rom-revision=1\n"
   "duration-us=3000000\n"
   "state=stopped\n"
   "emulated-frames=180\n"
   "presented-frames=175\n"
   "skipped-frames=5\n"
   "fast-forward-frames=30\n"
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
   "ge-wait-p99-us=2500\n";
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[2048];
  int length;

  memset(&metadata, 0, sizeof(metadata));
  metadata.emulator_version = "1.3.0";
  metadata.psp_model = 1;
  metadata.clock_mhz = 333;
  metadata.renderer = 1;
  metadata.scale = 6;
  metadata.filter = 1;
  metadata.frameskip_type = 0;
  metadata.frameskip_value = 9;
  metadata.sound_volume = 10;
  metadata.rom_title = rom_title;
  metadata.rom_title_length = sizeof(rom_title);
  metadata.game_code = game_code;
  metadata.game_code_length = sizeof(game_code);
  metadata.rom_revision = 1;
  metadata.duration_us = 3000000;

  memset(&metrics, 0, sizeof(metrics));
  metrics.state = PERF_METRICS_STOPPED;
  metrics.emulated_frames = 180;
  metrics.presented_frames = 175;
  metrics.skipped_frames = 5;
  metrics.fast_forward_frames = 30;
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

  length = perf_report_format(report, sizeof(report), &metadata, &metrics);
  assert(length == (int)strlen(expected));
  assert(strcmp(report, expected) == 0);
  assert(strchr(report, '/') == NULL);
}

static void test_states_and_empty_identity(void)
{
  PerfReportMetadata metadata;
  PerfMetricsSnapshot metrics;
  char report[2048];

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

int main(void)
{
  test_golden_report();
  test_states_and_empty_identity();
  test_invalid_arguments_and_small_buffer();
  puts("Performance report tests passed");
  return 0;
}
