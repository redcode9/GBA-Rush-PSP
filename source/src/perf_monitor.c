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

/* The unbound benchmark: RUNNING while its capture records, DONE once it
   reached its frame count, until taken. */
typedef enum
{
  PERF_MONITOR_BENCH_IDLE,
  PERF_MONITOR_BENCH_RUNNING,
  PERF_MONITOR_BENCH_DONE
} PerfMonitorBench;

static PerfMetrics perf_monitor_metrics;
static PerfMonitorIdentity perf_monitor_current_identity;
static uint64_t perf_monitor_started_us;
static uint64_t perf_monitor_elapsed_us;
static PerfMonitorBench perf_monitor_bench;
static uint32_t perf_monitor_bench_count;
static uint32_t perf_monitor_bench_target;
static uint64_t perf_monitor_bench_elapsed_us;

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
  perf_monitor_bench = PERF_MONITOR_BENCH_IDLE;
}

int perf_monitor_start(uint64_t now_us)
{
  if(!perf_metrics_start(&perf_monitor_metrics))
    return 0;

  perf_monitor_started_us = now_us;
  perf_monitor_elapsed_us = 0;
  memset(&perf_monitor_current_identity, 0,
   sizeof(perf_monitor_current_identity));
  perf_monitor_bench = PERF_MONITOR_BENCH_IDLE;
  return 1;
}

int perf_monitor_stop(uint64_t now_us)
{
  if(!perf_metrics_active(&perf_monitor_metrics))
    return 0;

  if(perf_monitor_bench == PERF_MONITOR_BENCH_RUNNING)
  {
    perf_monitor_invalidate(now_us);
    return 1;
  }
  perf_monitor_elapsed_us = perf_monitor_elapsed(now_us);
  return perf_metrics_stop(&perf_monitor_metrics);
}

void perf_monitor_invalidate(uint64_t now_us)
{
  if(!perf_metrics_active(&perf_monitor_metrics))
    return;

  perf_monitor_elapsed_us = perf_monitor_elapsed(now_us);
  perf_metrics_invalidate(&perf_monitor_metrics);
  perf_monitor_bench = PERF_MONITOR_BENCH_IDLE;
}

int perf_monitor_active(void)
{
  return perf_metrics_active(&perf_monitor_metrics);
}

int perf_monitor_bench_start(uint64_t now_us, uint32_t frames)
{
  if(frames == 0 || !perf_monitor_start(now_us))
    return 0;

  perf_monitor_bench = PERF_MONITOR_BENCH_RUNNING;
  perf_monitor_bench_count = 0;
  perf_monitor_bench_target = frames;
  perf_monitor_bench_elapsed_us = 0;
  return 1;
}

int perf_monitor_bench_running(void)
{
  return perf_monitor_bench == PERF_MONITOR_BENCH_RUNNING;
}

uint32_t perf_monitor_bench_frames_left(void)
{
  if(perf_monitor_bench != PERF_MONITOR_BENCH_RUNNING)
    return 0;

  return perf_monitor_bench_target - perf_monitor_bench_count;
}

int perf_monitor_bench_take_done(void)
{
  if(perf_monitor_bench != PERF_MONITOR_BENCH_DONE)
    return 0;

  perf_monitor_bench = PERF_MONITOR_BENCH_IDLE;
  return 1;
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
  if(perf_monitor_bench != PERF_MONITOR_BENCH_RUNNING)
    return;

  perf_monitor_bench_elapsed_us += frame_time_us;
  if(++perf_monitor_bench_count < perf_monitor_bench_target)
    return;

  perf_metrics_stop(&perf_monitor_metrics);
  perf_monitor_elapsed_us = perf_monitor_bench_elapsed_us;
  perf_monitor_bench = PERF_MONITOR_BENCH_DONE;
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

void perf_monitor_record_link_lockstep_frame(void)
{
  perf_metrics_record_link_lockstep_frame(&perf_monitor_metrics);
}

int perf_monitor_phase_begin_frame(void)
{
  return perf_metrics_phase_begin_frame(&perf_monitor_metrics);
}

void perf_monitor_phase_add(PerfMetricsPhase phase, uint32_t us)
{
  perf_metrics_phase_add(&perf_monitor_metrics, phase, us);
}

void perf_monitor_record_probe_calibration(uint32_t elapsed_us,
 uint32_t reads)
{
  perf_metrics_record_probe_calibration(&perf_monitor_metrics, elapsed_us,
   reads);
}

int perf_monitor_preempt_wanted(void)
{
  return perf_metrics_preempt_wanted(&perf_monitor_metrics);
}

void perf_monitor_preempt_sample(PerfMetricsPreemptPoint point,
 uint32_t thread_count, uint32_t intr_count)
{
  perf_metrics_preempt_sample(&perf_monitor_metrics, point, thread_count,
   intr_count);
}

void perf_monitor_preempt_unavailable(void)
{
  perf_metrics_preempt_unavailable(&perf_monitor_metrics);
}

void perf_monitor_record_preempt_calibration(uint32_t elapsed_us,
 uint32_t calls)
{
  perf_metrics_record_preempt_calibration(&perf_monitor_metrics, elapsed_us,
   calls);
}
