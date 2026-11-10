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
 * Counters and histograms of one benchmark capture. Plain data with no I/O
 * or allocation, so recording is cheap on hot paths and testable on the
 * host. Every record call is ignored unless a capture is active, and all
 * counters saturate instead of wrapping.
 */

#ifndef PERF_METRICS_H
#define PERF_METRICS_H

#include <stdint.h>

/* Frame and work times in 250 us buckets (to 128 ms) and GE waits in 50 us
   buckets (to 25.6 ms); longer samples land in the last bucket. */
#define PERF_METRICS_HISTOGRAM_BUCKETS 512U
#define PERF_METRICS_FRAME_BUCKET_US 250U
#define PERF_METRICS_GE_BUCKET_US 50U

/* A GBA frame (280896 cycles at 16.78 MHz) lasts 16743 us: a frame whose
   work, its frame time minus the VBlank, audio and GE waits recorded in
   it, takes longer cannot keep full speed. A frame time over 1.5 times
   that (25115 us) is a visible stutter. */
#define PERF_METRICS_WORK_BUDGET_US 16743U
#define PERF_METRICS_FRAME_OVER_BUDGET_US 25115U

/* Phase timers: main.c times update_gba, update_scanline and the PSG
   update (update_gbc_sound) in one frame out of 11 of a capture. 11 is
   prime, so the sampled frames do not lock onto the 2- to 6-frame cycles
   of games. A sampled frame longer than the frame histogram's range
   (128 ms) held a menu, a save state or a stall: its phases are dropped. */
#define PERF_METRICS_PHASE_STRIDE 11U
#define PERF_METRICS_PHASE_MAX_FRAME_US \
 (PERF_METRICS_HISTOGRAM_BUCKETS * PERF_METRICS_FRAME_BUCKET_US)
/* Clock reads main.c makes back to back when a capture starts, to measure
   the cost of one read. */
#define PERF_METRICS_PROBE_CALIBRATION_READS 64U

/* INVALIDATED: a reset, ROM change or suspend cut the capture short. */
typedef enum
{
  PERF_METRICS_IDLE,
  PERF_METRICS_ACTIVE,
  PERF_METRICS_STOPPED,
  PERF_METRICS_INVALIDATED
} PerfMetricsState;

typedef struct
{
  uint64_t buckets[PERF_METRICS_HISTOGRAM_BUCKETS];
  uint64_t sample_count;
  uint64_t total_us;
  uint32_t max_us;
  uint32_t bucket_width_us;
} PerfMetricsHistogram;

/* Where the emulator thread waited for the PSP VBlank: frame pacing in
   synchronize(), before presenting a frame, or in the sound sync. */
typedef enum
{
  PERF_METRICS_WAIT_BACKPRESSURE,
  PERF_METRICS_WAIT_PRESENT,
  PERF_METRICS_WAIT_AUDIO
} PerfMetricsWaitSite;

/* What a phase timer measured. UPDATE_GBA is the whole call; the scanline
   and PSG phases and the frame's waits run inside it. */
typedef enum
{
  PERF_METRICS_PHASE_UPDATE_GBA,
  PERF_METRICS_PHASE_SCANLINE,
  PERF_METRICS_PHASE_GBC_SOUND
} PerfMetricsPhase;

typedef struct
{
  PerfMetricsState state;
  uint64_t emulated_frames;
  uint64_t presented_frames;
  uint64_t skipped_frames;
  uint64_t fast_forward_frames;
  uint64_t fast_forward_presented_frames;
  uint64_t fast_forward_time_us;
  uint64_t fast_forward_transitions;
  uint64_t fast_forward_audio_fallbacks;
  uint64_t audio_recoveries;
  uint64_t audio_underruns;
  uint64_t dynarec_compilations;
  uint64_t dynarec_reuse_hits;
  uint64_t dynarec_retries;
  uint64_t dynarec_flushes;
  uint64_t dynarec_readonly_flushes;
  uint64_t dynarec_writable_flushes;
  uint64_t dynarec_cache_high_water;
  uint64_t dynarec_readonly_high_water;
  uint64_t dynarec_writable_high_water;
  uint64_t memory_low_water;
  int has_memory_sample;
  uint64_t vblank_backpressure_count;
  uint64_t vblank_backpressure_total_us;
  uint64_t vblank_present_count;
  uint64_t vblank_present_total_us;
  uint64_t vblank_audio_count;
  uint64_t vblank_audio_total_us;
  uint64_t renderer_total_us;
  uint32_t renderer_max_us;
  uint64_t dcache_writeback_count;
  uint64_t dcache_writeback_total_us;
  uint64_t ge_sync_direct_count;
  uint64_t ge_sync_direct_total_us;
  uint64_t sharp_presented_frames;
  uint64_t crisp_presented_frames;
  uint64_t gba_update_entries;
  uint64_t gba_update_iterations;
  uint64_t dma16_transfers;
  uint64_t dma16_elements;
  uint64_t dma16_bytes;
  uint64_t dma16_total_us;
  uint64_t dma32_transfers;
  uint64_t dma32_elements;
  uint64_t dma32_bytes;
  uint64_t dma32_total_us;
  uint64_t dma_gamepak_pairs;
  uint64_t gamepak_evict_scan_count;
  uint64_t gamepak_evict_scan_total_us;
  uint64_t gamepak_miss_count;
  uint64_t gamepak_miss_bytes;
  uint64_t gamepak_read_errors;
  uint64_t gamepak_rom_seek_total_us;
  uint64_t gamepak_rom_read_total_us;
  uint64_t frames_work_over_budget;
  uint64_t frames_over_1_5x_budget;
  /* Frames outside fast-forward that synchronize() did not pace with a
     backpressure VBlank wait, as real time was already ahead. A frame that
     a wait of the frame before (the present wait with vsync on, or the
     audio sync) carried past the VBlank counts too, so compare late counts
     only between captures with the same vsync setting. */
  uint64_t frames_late;
  /* Frames emulated with a lockstep link connected: forced frameskip and
     a different emulator thread priority. */
  uint64_t link_lockstep_frames;
  /* Waits recorded since the last frame; each recorded frame clears them. */
  uint64_t frame_wait_us;
  int frame_waited_backpressure;
  /* Phase timers. phase_frames counts the frames begun in the capture;
     phase_sampling is set while the current frame is sampled, and
     phase_recorded once perf_metrics_record_frame() gave its waits and
     work. The phase_frame_ fields hold that frame until the next one
     begins and adds them to the totals. */
  uint32_t phase_frames;
  int phase_sampling;
  int phase_recorded;
  uint64_t phase_frame_update_gba_us;
  uint64_t phase_frame_scanline_us;
  uint64_t phase_frame_gbc_sound_us;
  uint64_t phase_frame_wait_us;
  uint64_t phase_frame_reads;
  uint32_t phase_frame_work_us;
  uint64_t phase_sampled_frames;
  /* update_gba's own time: without the scanline and PSG phases and the
     frame's waits. */
  uint64_t phase_update_gba_us;
  uint64_t phase_scanline_us;
  uint64_t phase_gbc_sound_us;
  uint64_t phase_work_us;
  /* Cost of one clock read, and the reads the phase timers made in the
     sampled frames: their product is the time the timers added. */
  uint32_t probe_read_ns;
  uint64_t probe_reads;
  PerfMetricsHistogram frame_time;
  PerfMetricsHistogram ge_wait;
  PerfMetricsHistogram work_time;
} PerfMetrics;

/* PerfMetrics with the histograms reduced to totals and percentiles. */
typedef struct
{
  PerfMetricsState state;
  uint64_t emulated_frames;
  uint64_t presented_frames;
  uint64_t skipped_frames;
  uint64_t fast_forward_frames;
  uint64_t fast_forward_presented_frames;
  uint64_t fast_forward_time_us;
  uint64_t fast_forward_transitions;
  uint64_t fast_forward_audio_fallbacks;
  uint64_t audio_recoveries;
  uint64_t audio_underruns;
  uint64_t dynarec_compilations;
  uint64_t dynarec_reuse_hits;
  uint64_t dynarec_retries;
  uint64_t dynarec_flushes;
  uint64_t dynarec_readonly_flushes;
  uint64_t dynarec_writable_flushes;
  uint64_t dynarec_cache_high_water;
  uint64_t dynarec_readonly_high_water;
  uint64_t dynarec_writable_high_water;
  uint64_t memory_low_water;
  int has_memory_sample;
  uint64_t frame_time_samples;
  uint64_t frame_time_total_us;
  uint32_t frame_time_max_us;
  uint32_t frame_time_p50_us;
  uint32_t frame_time_p95_us;
  uint32_t frame_time_p99_us;
  uint64_t ge_wait_samples;
  uint64_t ge_wait_total_us;
  uint32_t ge_wait_max_us;
  uint32_t ge_wait_p50_us;
  uint32_t ge_wait_p95_us;
  uint32_t ge_wait_p99_us;
  uint64_t vblank_backpressure_count;
  uint64_t vblank_backpressure_total_us;
  uint64_t vblank_present_count;
  uint64_t vblank_present_total_us;
  uint64_t vblank_audio_count;
  uint64_t vblank_audio_total_us;
  uint64_t renderer_total_us;
  uint32_t renderer_max_us;
  uint64_t dcache_writeback_count;
  uint64_t dcache_writeback_total_us;
  uint64_t ge_sync_direct_count;
  uint64_t ge_sync_direct_total_us;
  uint64_t sharp_presented_frames;
  uint64_t crisp_presented_frames;
  uint64_t gba_update_entries;
  uint64_t gba_update_iterations;
  uint64_t dma16_transfers;
  uint64_t dma16_elements;
  uint64_t dma16_bytes;
  uint64_t dma16_total_us;
  uint64_t dma32_transfers;
  uint64_t dma32_elements;
  uint64_t dma32_bytes;
  uint64_t dma32_total_us;
  uint64_t dma_gamepak_pairs;
  uint64_t gamepak_evict_scan_count;
  uint64_t gamepak_evict_scan_total_us;
  uint64_t gamepak_miss_count;
  uint64_t gamepak_miss_bytes;
  uint64_t gamepak_read_errors;
  uint64_t gamepak_rom_seek_total_us;
  uint64_t gamepak_rom_read_total_us;
  uint64_t work_time_samples;
  uint64_t work_time_total_us;
  uint32_t work_time_max_us;
  uint32_t work_time_p50_us;
  uint32_t work_time_p95_us;
  uint32_t work_time_p99_us;
  uint64_t frames_work_over_budget;
  uint64_t frames_over_1_5x_budget;
  uint64_t frames_late;
  uint64_t link_lockstep_frames;
  uint64_t phase_sampled_frames;
  uint64_t phase_update_gba_us;
  uint64_t phase_scanline_us;
  uint64_t phase_gbc_sound_us;
  uint64_t phase_work_us;
  uint32_t probe_read_ns;
  uint64_t probe_reads;
} PerfMetricsSnapshot;

void perf_metrics_init(PerfMetrics *metrics);
void perf_metrics_reset(PerfMetrics *metrics);
int perf_metrics_start(PerfMetrics *metrics);
int perf_metrics_stop(PerfMetrics *metrics);
void perf_metrics_invalidate(PerfMetrics *metrics);
int perf_metrics_active(const PerfMetrics *metrics);
void perf_metrics_record_frame(PerfMetrics *metrics, uint32_t frame_time_us,
 uint32_t ge_wait_us, int skipped, int fast_forward);
void perf_metrics_record_emulated_frame(PerfMetrics *metrics,
 uint32_t frame_time_us, int fast_forward);
void perf_metrics_record_presented_frame(PerfMetrics *metrics,
 uint32_t ge_wait_us);
void perf_metrics_record_skipped_frame(PerfMetrics *metrics);
void perf_metrics_record_audio_recovery(PerfMetrics *metrics);
void perf_metrics_record_audio_underrun(PerfMetrics *metrics);
void perf_metrics_record_fast_forward_transition(PerfMetrics *metrics);
void perf_metrics_record_fast_forward_audio_fallback(PerfMetrics *metrics);
void perf_metrics_record_dynarec_compilation(PerfMetrics *metrics);
void perf_metrics_record_dynarec_reuse(PerfMetrics *metrics);
void perf_metrics_record_dynarec_retry(PerfMetrics *metrics);
void perf_metrics_record_dynarec_flush(PerfMetrics *metrics, int writable);
void perf_metrics_observe_dynarec_cache(PerfMetrics *metrics,
 uint64_t readonly_bytes, uint64_t writable_bytes);
void perf_metrics_observe_memory_available(PerfMetrics *metrics,
 uint64_t bytes);
void perf_metrics_record_wait(PerfMetrics *metrics,
 PerfMetricsWaitSite site, uint32_t wait_us);
void perf_metrics_record_renderer_frame(PerfMetrics *metrics,
 uint32_t renderer_us);
void perf_metrics_record_dcache_writeback(PerfMetrics *metrics,
 uint32_t wait_us);
void perf_metrics_record_ge_sync_direct(PerfMetrics *metrics,
 uint32_t wait_us);
void perf_metrics_record_quality_presented(PerfMetrics *metrics,
 int sharp);
void perf_metrics_record_gba_update_entry(PerfMetrics *metrics);
void perf_metrics_record_gba_update_iteration(PerfMetrics *metrics);
void perf_metrics_record_dma(PerfMetrics *metrics, int bits32,
 uint32_t elements, uint32_t dma_us, int gamepak_pair);
void perf_metrics_record_gamepak_evict_scan(PerfMetrics *metrics,
 uint32_t us);
void perf_metrics_record_gamepak_rom_io(PerfMetrics *metrics,
 uint32_t seek_us, uint32_t read_us, uint32_t bytes_read,
 int read_error);
void perf_metrics_record_link_lockstep_frame(PerfMetrics *metrics);
int perf_metrics_phase_begin_frame(PerfMetrics *metrics);
void perf_metrics_phase_add(PerfMetrics *metrics, PerfMetricsPhase phase,
 uint32_t us);
void perf_metrics_record_probe_calibration(PerfMetrics *metrics,
 uint32_t elapsed_us, uint32_t reads);
void perf_metrics_snapshot(const PerfMetrics *metrics,
 PerfMetricsSnapshot *snapshot);

#endif /* PERF_METRICS_H */
