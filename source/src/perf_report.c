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

#include "perf_report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
  char *output;
  size_t size;
  size_t length;
  int failed;
} PerfReportWriter;

static int perf_report_character_allowed(uint8_t value)
{
  return (value >= 'A' && value <= 'Z') ||
   (value >= 'a' && value <= 'z') ||
   (value >= '0' && value <= '9') || value == ' ' || value == '-' ||
   value == '_' || value == '.';
}

static void perf_report_sanitize_bytes(const uint8_t *source,
 size_t source_length, char *destination, size_t destination_size)
{
  size_t count;
  size_t i;

  if(destination_size == 0)
    return;

  destination[0] = '\0';
  if(source == NULL)
    return;

  count = source_length;
  if(count >= destination_size)
    count = destination_size - 1U;

  for(i = 0; i < count; i++)
  {
    uint8_t value = source[i];

    destination[i] = perf_report_character_allowed(value) ?
     (char)value : '_';
  }
  destination[count] = '\0';
}

static void perf_report_sanitize_string(const char *source,
 char *destination, size_t destination_size)
{
  size_t length = 0;

  if(source != NULL)
  {
    while(length + 1U < destination_size && source[length] != '\0')
      length++;
  }

  perf_report_sanitize_bytes((const uint8_t *)source, length, destination,
   destination_size);
}

static void perf_report_append(PerfReportWriter *writer,
 const char *format, ...)
{
  size_t remaining;
  va_list arguments;
  int written;

  if(writer->failed)
    return;

  remaining = writer->size - writer->length;
  va_start(arguments, format);
  written = vsnprintf(writer->output + writer->length, remaining, format,
   arguments);
  va_end(arguments);

  if(written < 0 || (size_t)written >= remaining)
  {
    writer->failed = 1;
    return;
  }

  writer->length += (size_t)written;
}

static const char *perf_report_state_name(PerfMetricsState state)
{
  switch(state)
  {
    case PERF_METRICS_IDLE:
      return "idle";
    case PERF_METRICS_ACTIVE:
      return "active";
    case PERF_METRICS_STOPPED:
      return "stopped";
    case PERF_METRICS_INVALIDATED:
      return "invalidated";
    default:
      return "unknown";
  }
}

int perf_report_format(char *output, size_t output_size,
 const PerfReportMetadata *metadata, const PerfMetricsSnapshot *metrics)
{
  PerfReportWriter writer;
  char version[64];
  char rom_title[13];
  char game_code[5];

  if(output == NULL || output_size == 0)
    return -1;

  output[0] = '\0';
  if(metadata == NULL || metrics == NULL)
    return -1;

  perf_report_sanitize_string(metadata->emulator_version, version,
   sizeof(version));
  perf_report_sanitize_bytes(metadata->rom_title, metadata->rom_title_length,
   rom_title, sizeof(rom_title));
  perf_report_sanitize_bytes(metadata->game_code, metadata->game_code_length,
   game_code, sizeof(game_code));

  writer.output = output;
  writer.size = output_size;
  writer.length = 0;
  writer.failed = 0;

  perf_report_append(&writer, "gba-rush-performance-report=%u\n",
   PERF_REPORT_VERSION);
  perf_report_append(&writer, "version=%s\n", version);
  perf_report_append(&writer, "psp-model=%u\n", metadata->psp_model);
  perf_report_append(&writer, "clock-mhz=%u\n", metadata->clock_mhz);
  perf_report_append(&writer, "renderer=%u\n", metadata->renderer);
  perf_report_append(&writer, "scale=%u\n", metadata->scale);
  perf_report_append(&writer, "filter=%u\n", metadata->filter);
  perf_report_append(&writer, "frameskip-type=%u\n",
   metadata->frameskip_type);
  perf_report_append(&writer, "frameskip-value=%u\n",
   metadata->frameskip_value);
  perf_report_append(&writer, "sound-volume=%u\n", metadata->sound_volume);
  perf_report_append(&writer, "rom-title=%s\n", rom_title);
  perf_report_append(&writer, "game-code=%s\n", game_code);
  perf_report_append(&writer, "rom-revision=%u\n", metadata->rom_revision);
  perf_report_append(&writer, "duration-us=%llu\n",
   (unsigned long long)metadata->duration_us);
  perf_report_append(&writer, "state=%s\n",
   perf_report_state_name(metrics->state));
  perf_report_append(&writer, "emulated-frames=%llu\n",
   (unsigned long long)metrics->emulated_frames);
  perf_report_append(&writer, "presented-frames=%llu\n",
   (unsigned long long)metrics->presented_frames);
  perf_report_append(&writer, "skipped-frames=%llu\n",
   (unsigned long long)metrics->skipped_frames);
  perf_report_append(&writer, "fast-forward-frames=%llu\n",
   (unsigned long long)metrics->fast_forward_frames);
  perf_report_append(&writer, "audio-recoveries=%llu\n",
   (unsigned long long)metrics->audio_recoveries);
  perf_report_append(&writer, "audio-underruns=%llu\n",
   (unsigned long long)metrics->audio_underruns);
  perf_report_append(&writer, "dynarec-compilations=%llu\n",
   (unsigned long long)metrics->dynarec_compilations);
  perf_report_append(&writer, "dynarec-flushes=%llu\n",
   (unsigned long long)metrics->dynarec_flushes);
  perf_report_append(&writer, "dynarec-cache-high-water=%llu\n",
   (unsigned long long)metrics->dynarec_cache_high_water);
  perf_report_append(&writer, "memory-low-water=%llu\n",
   (unsigned long long)metrics->memory_low_water);
  perf_report_append(&writer, "memory-sampled=%d\n",
   metrics->has_memory_sample != 0);
  perf_report_append(&writer, "frame-time-samples=%llu\n",
   (unsigned long long)metrics->frame_time_samples);
  perf_report_append(&writer, "frame-time-total-us=%llu\n",
   (unsigned long long)metrics->frame_time_total_us);
  perf_report_append(&writer, "frame-time-max-us=%u\n",
   metrics->frame_time_max_us);
  perf_report_append(&writer, "frame-time-p50-us=%u\n",
   metrics->frame_time_p50_us);
  perf_report_append(&writer, "frame-time-p95-us=%u\n",
   metrics->frame_time_p95_us);
  perf_report_append(&writer, "frame-time-p99-us=%u\n",
   metrics->frame_time_p99_us);
  perf_report_append(&writer, "ge-wait-samples=%llu\n",
   (unsigned long long)metrics->ge_wait_samples);
  perf_report_append(&writer, "ge-wait-total-us=%llu\n",
   (unsigned long long)metrics->ge_wait_total_us);
  perf_report_append(&writer, "ge-wait-max-us=%u\n",
   metrics->ge_wait_max_us);
  perf_report_append(&writer, "ge-wait-p50-us=%u\n",
   metrics->ge_wait_p50_us);
  perf_report_append(&writer, "ge-wait-p95-us=%u\n",
   metrics->ge_wait_p95_us);
  perf_report_append(&writer, "ge-wait-p99-us=%u\n",
   metrics->ge_wait_p99_us);

  if(writer.failed)
  {
    output[0] = '\0';
    return -1;
  }

  return (int)writer.length;
}
