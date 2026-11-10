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

/*
 * The process-wide benchmark capture: one PerfMetrics plus its timing and
 * settings. Hot modules (cpu, memory, video, sound) call the record
 * functions; main.c starts, stops and reports.
 */

#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <stdint.h>

#include "perf_metrics.h"

/* The emulation thread owns this monitor. Worker threads publish events to
 * their owning module and the emulation thread forwards them here. */
void perf_monitor_init(void);
/* start clears the metrics and returns 0 if a capture is already running;
   stop returns 0 if none is. invalidate cuts a running capture short
   (reset, ROM change, suspend) so its report is marked as unusable. */
int perf_monitor_start(uint64_t now_us);
int perf_monitor_stop(uint64_t now_us);
void perf_monitor_invalidate(uint64_t now_us);
int perf_monitor_active(void);
/* The unbound benchmark: bench_start starts a capture, or returns 0 while
   one runs or for 0 frames, that stops itself once it has recorded frames
   frames. Its duration is then the sum of their frame times, which run back
   to back, so the menu exit before the first is left out. A stop before
   that, or an invalidate, cuts it short and marks it unusable. take_done
   returns 1 once after a bench reached its count; a new start drops it. */
int perf_monitor_bench_start(uint64_t now_us, uint32_t frames);
int perf_monitor_bench_running(void);
int perf_monitor_bench_take_done(void);
/* Length of the last capture, set by stop or invalidate; 0 while a capture
   is running. */
uint64_t perf_monitor_duration_us(void);
void perf_monitor_snapshot(PerfMetricsSnapshot *snapshot);

/* Metadata identity pinned when a capture starts. Written by the emulation
 * thread before recording and read verbatim when the report is written. */
typedef struct
{
  uint32_t clock_mhz;
  uint32_t psp_vsync;
  uint32_t renderer;
  uint32_t scale;
  uint32_t filter;
  uint32_t frameskip_type;
  uint32_t frameskip_value;
  uint32_t sound_volume;
  uint32_t fast_forward_game_override;
  uint32_t fast_forward_profile;
  uint32_t fast_forward_activation;
  uint32_t fast_forward_speed;
  uint32_t fast_forward_presentation;
  uint32_t fast_forward_audio;
  uint32_t gamepak_file_size;
  uint32_t gamepak_ram_buffer_size;
  uint32_t gamepak_ram_pages;
  uint8_t rom_title[12];
  uint8_t game_code[4];
  uint32_t rom_revision;
  /* Local wall time (RTC) the capture started, set when has_capture_time.
     It only orders runs: the RTC can jump, so never use it for a duration. */
  uint32_t has_capture_time;
  uint32_t capture_year;
  uint32_t capture_month;
  uint32_t capture_day;
  uint32_t capture_hour;
  uint32_t capture_minute;
  uint32_t capture_second;
  /* The report's bench-* keys (PERF_REPORT_BENCH_* in perf_report.h). */
  uint32_t bench_mode;
  uint32_t bench_frames;
  uint32_t bench_render;
  uint32_t bench_audio;
  uint32_t bench_input;
} PerfMonitorIdentity;

void perf_monitor_set_identity(const PerfMonitorIdentity *identity);
void perf_monitor_identity(PerfMonitorIdentity *identity);
/* Record calls are ignored unless a capture is running. */
void perf_monitor_record_frame(uint32_t frame_time_us, uint32_t ge_wait_us,
 int skipped, int fast_forward);
void perf_monitor_record_audio_recovery(void);
void perf_monitor_record_audio_underrun(void);
void perf_monitor_record_fast_forward_transition(void);
void perf_monitor_record_fast_forward_audio_fallback(void);
void perf_monitor_record_dynarec_compilation(void);
void perf_monitor_record_dynarec_reuse(void);
void perf_monitor_record_dynarec_retry(void);
void perf_monitor_record_dynarec_flush(int writable);
void perf_monitor_observe_dynarec_cache(uint64_t readonly_bytes,
 uint64_t writable_bytes);
void perf_monitor_observe_memory_available(uint64_t bytes);
void perf_monitor_record_wait(PerfMetricsWaitSite site, uint32_t wait_us);
void perf_monitor_record_renderer_frame(uint32_t renderer_us);
void perf_monitor_record_dcache_writeback(uint32_t wait_us);
void perf_monitor_record_ge_sync_direct(uint32_t wait_us);
void perf_monitor_record_quality_presented(int sharp);
void perf_monitor_record_gba_update_entry(void);
void perf_monitor_record_gba_update_iteration(void);
void perf_monitor_record_dma(int bits32, uint32_t elements, uint32_t dma_us,
 int gamepak_pair);
void perf_monitor_record_gamepak_evict_scan(uint32_t us);
void perf_monitor_record_gamepak_rom_io(uint32_t seek_us, uint32_t read_us,
 uint32_t bytes_read, int read_error);
void perf_monitor_record_link_lockstep_frame(void);
/* Phase timers (main.c): begin_frame returns whether the frame starting
   now is sampled, and 0 when no capture runs. */
int perf_monitor_phase_begin_frame(void);
void perf_monitor_phase_add(PerfMetricsPhase phase, uint32_t us);
void perf_monitor_record_probe_calibration(uint32_t elapsed_us,
 uint32_t reads);
/* Preemption counts (main.c): wanted is 0 without a capture or once the
   kernel refused a read, which unavailable reports. */
int perf_monitor_preempt_wanted(void);
void perf_monitor_preempt_sample(PerfMetricsPreemptPoint point,
 uint32_t thread_count, uint32_t intr_count);
void perf_monitor_preempt_unavailable(void);
void perf_monitor_record_preempt_calibration(uint32_t elapsed_us,
 uint32_t calls);

#endif /* PERF_MONITOR_H */
