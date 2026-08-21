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

#ifndef PERF_REPORT_H
#define PERF_REPORT_H

#include <stddef.h>
#include <stdint.h>

#include "perf_metrics.h"

#define PERF_REPORT_VERSION 2U

typedef struct
{
  const char *emulator_version;
  uint32_t psp_model;
  uint32_t clock_mhz;
  uint32_t renderer;
  uint32_t scale;
  uint32_t filter;
  uint32_t frameskip_type;
  uint32_t frameskip_value;
  uint32_t sound_volume;
  const uint8_t *rom_title;
  size_t rom_title_length;
  const uint8_t *game_code;
  size_t game_code_length;
  uint32_t rom_revision;
  uint64_t duration_us;
} PerfReportMetadata;

int perf_report_format(char *output, size_t output_size,
 const PerfReportMetadata *metadata, const PerfMetricsSnapshot *metrics);

#endif /* PERF_REPORT_H */
