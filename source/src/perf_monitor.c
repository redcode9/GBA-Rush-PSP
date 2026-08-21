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

#include "perf_monitor.h"

static PerfMetrics perf_monitor_metrics;
static uint64_t perf_monitor_started_us;
static uint64_t perf_monitor_elapsed_us;

static uint64_t perf_monitor_elapsed(uint64_t now_us)
{
  if(now_us < perf_monitor_started_us)
    return 0;

  return now_us - perf_monitor_started_us;
}

void perf_monitor_init(void)
{
  perf_metrics_init(&perf_monitor_metrics);
  perf_monitor_started_us = 0;
  perf_monitor_elapsed_us = 0;
}

int perf_monitor_start(uint64_t now_us)
{
  if(!perf_metrics_start(&perf_monitor_metrics))
    return 0;

  perf_monitor_started_us = now_us;
  perf_monitor_elapsed_us = 0;
  return 1;
}

int perf_monitor_stop(uint64_t now_us)
{
  if(!perf_metrics_active(&perf_monitor_metrics))
    return 0;

  perf_monitor_elapsed_us = perf_monitor_elapsed(now_us);
  return perf_metrics_stop(&perf_monitor_metrics);
}

void perf_monitor_invalidate(uint64_t now_us)
{
  if(!perf_metrics_active(&perf_monitor_metrics))
    return;

  perf_monitor_elapsed_us = perf_monitor_elapsed(now_us);
  perf_metrics_invalidate(&perf_monitor_metrics);
}

int perf_monitor_active(void)
{
  return perf_metrics_active(&perf_monitor_metrics);
}

uint64_t perf_monitor_duration_us(void)
{
  return perf_monitor_elapsed_us;
}

void perf_monitor_snapshot(PerfMetricsSnapshot *snapshot)
{
  perf_metrics_snapshot(&perf_monitor_metrics, snapshot);
}

void perf_monitor_record_frame(uint32_t frame_time_us, uint32_t ge_wait_us,
 int skipped, int fast_forward)
{
  perf_metrics_record_frame(&perf_monitor_metrics, frame_time_us, ge_wait_us,
   skipped, fast_forward);
}

void perf_monitor_record_audio_recovery(void)
{
  perf_metrics_record_audio_recovery(&perf_monitor_metrics);
}

void perf_monitor_record_audio_underrun(void)
{
  perf_metrics_record_audio_underrun(&perf_monitor_metrics);
}

void perf_monitor_record_dynarec_compilation(void)
{
  perf_metrics_record_dynarec_compilation(&perf_monitor_metrics);
}

void perf_monitor_record_dynarec_flush(void)
{
  perf_metrics_record_dynarec_flush(&perf_monitor_metrics);
}

void perf_monitor_observe_dynarec_cache(uint64_t bytes)
{
  perf_metrics_observe_dynarec_cache(&perf_monitor_metrics, bytes);
}

void perf_monitor_observe_memory_available(uint64_t bytes)
{
  perf_metrics_observe_memory_available(&perf_monitor_metrics, bytes);
}
