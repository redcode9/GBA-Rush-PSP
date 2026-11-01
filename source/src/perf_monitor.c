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

#include <stddef.h>
#include <string.h>

static PerfMetrics perf_monitor_metrics;
static PerfMonitorIdentity perf_monitor_current_identity;
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
  memset(&perf_monitor_current_identity, 0,
   sizeof(perf_monitor_current_identity));
  perf_monitor_started_us = 0;
  perf_monitor_elapsed_us = 0;
}

int perf_monitor_start(uint64_t now_us)
{
  if(!perf_metrics_start(&perf_monitor_metrics))
    return 0;

  perf_monitor_started_us = now_us;
  perf_monitor_elapsed_us = 0;
  memset(&perf_monitor_current_identity, 0,
   sizeof(perf_monitor_current_identity));
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

void perf_monitor_record_fast_forward_transition(void)
{
  perf_metrics_record_fast_forward_transition(&perf_monitor_metrics);
}

void perf_monitor_record_fast_forward_audio_fallback(void)
{
  perf_metrics_record_fast_forward_audio_fallback(&perf_monitor_metrics);
}

void perf_monitor_record_dynarec_compilation(void)
{
  perf_metrics_record_dynarec_compilation(&perf_monitor_metrics);
}

void perf_monitor_record_dynarec_reuse(void)
{
  perf_metrics_record_dynarec_reuse(&perf_monitor_metrics);
}

void perf_monitor_record_dynarec_retry(void)
{
  perf_metrics_record_dynarec_retry(&perf_monitor_metrics);
}

void perf_monitor_record_dynarec_flush(int writable)
{
  perf_metrics_record_dynarec_flush(&perf_monitor_metrics, writable);
}

void perf_monitor_observe_dynarec_cache(uint64_t readonly_bytes,
 uint64_t writable_bytes)
{
  perf_metrics_observe_dynarec_cache(&perf_monitor_metrics,
   readonly_bytes, writable_bytes);
}

void perf_monitor_observe_memory_available(uint64_t bytes)
{
  perf_metrics_observe_memory_available(&perf_monitor_metrics, bytes);
}

/* The settings the capture runs under, for the report. Ignored unless a
   capture is active; main_perf_start sets it right after starting. */
void perf_monitor_set_identity(const PerfMonitorIdentity *identity)
{
  if(identity != NULL && perf_monitor_active())
    perf_monitor_current_identity = *identity;
}

void perf_monitor_identity(PerfMonitorIdentity *identity)
{
  if(identity != NULL)
    *identity = perf_monitor_current_identity;
}

void perf_monitor_record_wait(PerfMetricsWaitSite site, uint32_t wait_us)
{
  perf_metrics_record_wait(&perf_monitor_metrics, site, wait_us);
}

void perf_monitor_record_renderer_frame(uint32_t renderer_us)
{
  perf_metrics_record_renderer_frame(&perf_monitor_metrics, renderer_us);
}

void perf_monitor_record_dcache_writeback(uint32_t wait_us)
{
  perf_metrics_record_dcache_writeback(&perf_monitor_metrics, wait_us);
}

void perf_monitor_record_ge_sync_direct(uint32_t wait_us)
{
  perf_metrics_record_ge_sync_direct(&perf_monitor_metrics, wait_us);
}

void perf_monitor_record_quality_presented(int sharp)
{
  perf_metrics_record_quality_presented(&perf_monitor_metrics, sharp);
}

void perf_monitor_record_gba_update_entry(void)
{
  perf_metrics_record_gba_update_entry(&perf_monitor_metrics);
}

void perf_monitor_record_gba_update_iteration(void)
{
  perf_metrics_record_gba_update_iteration(&perf_monitor_metrics);
}

void perf_monitor_record_dma(int bits32, uint32_t elements, uint32_t dma_us,
 int gamepak_pair)
{
  perf_metrics_record_dma(&perf_monitor_metrics, bits32, elements, dma_us,
   gamepak_pair);
}

void perf_monitor_record_gamepak_evict_scan(uint32_t us)
{
  perf_metrics_record_gamepak_evict_scan(&perf_monitor_metrics, us);
}

void perf_monitor_record_gamepak_rom_io(uint32_t seek_us, uint32_t read_us,
 uint32_t bytes_read, int read_error)
{
  perf_metrics_record_gamepak_rom_io(&perf_monitor_metrics, seek_us, read_us,
   bytes_read, read_error);
}
