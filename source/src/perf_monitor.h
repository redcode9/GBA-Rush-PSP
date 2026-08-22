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

#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <stdint.h>

#include "perf_metrics.h"

/* The emulation thread owns this monitor. Worker threads publish events to
 * their owning module and the emulation thread forwards them here. */
void perf_monitor_init(void);
int perf_monitor_start(uint64_t now_us);
int perf_monitor_stop(uint64_t now_us);
void perf_monitor_invalidate(uint64_t now_us);
int perf_monitor_active(void);
uint64_t perf_monitor_duration_us(void);
void perf_monitor_snapshot(PerfMetricsSnapshot *snapshot);
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

#endif /* PERF_MONITOR_H */
