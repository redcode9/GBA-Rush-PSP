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

#ifndef RUNTIME_LOG_PSP_H
#define RUNTIME_LOG_PSP_H

#include <stdint.h>

#include "runtime_log.h"

int runtime_log_psp_init(const char *base_path);
void runtime_log_psp_set_rom(const uint8_t *game_code, uint8_t revision,
 uint32_t rom_size, uint32_t emulated_cycle);
void runtime_log_psp_main(RuntimeLogEventType type, uint32_t emulated_cycle,
 uint8_t flags, uint32_t value0, uint32_t value1, uint32_t value2);
void runtime_log_psp_network(RuntimeLogEventType type, uint8_t flags,
 uint32_t value0, uint32_t value1, uint32_t value2);
void runtime_log_psp_critical(RuntimeLogEventType type,
 uint32_t emulated_cycle, uint8_t flags, uint32_t value0,
 uint32_t value1, uint32_t value2);
void runtime_log_psp_get_drop_counts(uint32_t *main_dropped,
 uint32_t *network_dropped, uint32_t *critical_dropped);
int runtime_log_psp_flush(RuntimeLogFlushReason reason);

#endif /* RUNTIME_LOG_PSP_H */
