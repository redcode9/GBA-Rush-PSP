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
 * PSP side of the runtime log: owns the three rings and writes a snapshot
 * to logs/session-1.gbrlog beside the EBOOT at each flush, keeping the
 * previous three sessions as session-2 to session-4.
 */

#ifndef RUNTIME_LOG_PSP_H
#define RUNTIME_LOG_PSP_H

#include <stdint.h>

#include "runtime_log.h"

/* base_path is the EBOOT directory. Always returns 1: if base_path is NULL
   or the logs directory cannot be used, the log stays in memory only;
   otherwise the old session files are rotated and a first snapshot written. */
int runtime_log_psp_init(const char *base_path);
/* Logs ROM_OPEN at every start or reset: the game, and the CPU core
   (CPU_CORE_* in main.h) it runs on. */
void runtime_log_psp_set_rom(const uint8_t *game_code, uint8_t revision,
 uint32_t rom_size, uint32_t cpu_core, uint32_t emulated_cycle);
/* Emulator thread only: each ring has a single writer (see RuntimeLogRing).
   Ignored before runtime_log_psp_init(). */
void runtime_log_psp_main(RuntimeLogEventType type, uint32_t emulated_cycle,
 uint8_t flags, uint32_t value0, uint32_t value1, uint32_t value2);
/* Logs to the network ring with no emulated cycle; one writer thread only. */
void runtime_log_psp_network(RuntimeLogEventType type, uint8_t flags,
 uint32_t value0, uint32_t value1, uint32_t value2);
/* Logs to the 64-record critical ring; one writer thread only. */
void runtime_log_psp_critical(RuntimeLogEventType type,
 uint32_t emulated_cycle, uint8_t flags, uint32_t value0,
 uint32_t value1, uint32_t value2);
/* runtime_log_name_probe_entry() on the main ring, so emulator thread
   only; 0 before runtime_log_psp_init(). */
size_t runtime_log_psp_name_probe_entry(uint32_t index, const uint8_t *name,
 size_t name_size, const uint8_t *fat_record, size_t fat_capacity,
 const int32_t open_result[RUNTIME_LOG_NAME_PROBE_OPENS],
 size_t record_budget);
void runtime_log_psp_get_drop_counts(uint32_t *main_dropped,
 uint32_t *network_dropped, uint32_t *critical_dropped);
/* Writes the current snapshot to storage; blocks on file I/O. */
int runtime_log_psp_flush(RuntimeLogFlushReason reason);

#endif /* RUNTIME_LOG_PSP_H */
