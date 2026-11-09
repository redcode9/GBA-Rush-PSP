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

/* Copies at most destination_size - 1 bytes, replacing anything but
   [A-Za-z0-9 ._-] with '_', so cartridge header bytes cannot break the
   report's line format. */
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

/* Like perf_report_sanitize_bytes(), for a NUL-terminated string such as
   the emulator version or the build id. */
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

/* Appends formatted text; once anything does not fit, the writer stays
   failed and later appends do nothing. */
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

/* Appends name=value for a 64-bit counter. The PSP's C library does not
   print 64-bit integers with %llu, so the digits are written out here. */
static void perf_report_append_u64(PerfReportWriter *writer,
 const char *name, uint64_t value)
{
  char digits[21];
  size_t at = sizeof(digits) - 1u;

  digits[at] = '\0';
  do
  {
    digits[--at] = (char)('0' + (int)(value % 10u));
    value /= 10u;
  }
  while(value != 0u);
  perf_report_append(writer, "%s=%s\n", name, digits + at);
}

/* The report's state= value; compare-benchmarks.py only accepts a
   "stopped" capture. */
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
  char build_id[PERF_REPORT_BUILD_ID_MAX + 1U];
  char rom_title[13];
  char game_code[5];

  if(output == NULL || output_size == 0)
    return -1;

  output[0] = '\0';
  if(metadata == NULL || metrics == NULL)
    return -1;

  perf_report_sanitize_string(metadata->emulator_version, version,
   sizeof(version));
  perf_report_sanitize_string(metadata->build_id, build_id,
   sizeof(build_id));
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
  perf_report_append(&writer, "build-id=%s\n", build_id);
  perf_report_append(&writer, "psp-model=%u\n", metadata->psp_model);
  perf_report_append(&writer, "clock-mhz=%u\n", metadata->clock_mhz);
  perf_report_append(&writer, "psp-fw-version=%u\n",
   metadata->psp_sdk_fw_macro);
  perf_report_append(&writer, "vsync=%u\n", metadata->psp_vsync);
  perf_report_append(&writer, "renderer=%u\n", metadata->renderer);
  perf_report_append(&writer, "scale=%u\n", metadata->scale);
  perf_report_append(&writer, "filter=%u\n", metadata->filter);
  perf_report_append(&writer, "frameskip-type=%u\n",
   metadata->frameskip_type);
  perf_report_append(&writer, "frameskip-value=%u\n",
   metadata->frameskip_value);
  perf_report_append(&writer, "sound-volume=%u\n", metadata->sound_volume);
  perf_report_append(&writer, "audio-sample-rate=%u\n",
   metadata->audio_sample_rate);
  perf_report_append(&writer, "audio-samples=%u\n",
   metadata->audio_samples);
  perf_report_append(&writer, "fast-forward-game-override=%u\n",
   metadata->fast_forward_game_override);
  perf_report_append(&writer, "fast-forward-profile=%u\n",
   metadata->fast_forward_profile);
  perf_report_append(&writer, "fast-forward-activation=%u\n",
   metadata->fast_forward_activation);
  perf_report_append(&writer, "fast-forward-speed=%u\n",
   metadata->fast_forward_speed);
  perf_report_append(&writer, "fast-forward-presentation=%u\n",
   metadata->fast_forward_presentation);
  perf_report_append(&writer, "fast-forward-audio=%u\n",
   metadata->fast_forward_audio);
  perf_report_append(&writer, "rom-title=%s\n", rom_title);
  perf_report_append(&writer, "game-code=%s\n", game_code);
  perf_report_append(&writer, "rom-revision=%u\n", metadata->rom_revision);
  perf_report_append_u64(&writer, "duration-us", metadata->duration_us);
  perf_report_append(&writer, "gamepak-file-size=%u\n",
   metadata->gamepak_file_size);
  perf_report_append(&writer, "gamepak-ram-buffer-size=%u\n",
   metadata->gamepak_ram_buffer_size);
  perf_report_append(&writer, "gamepak-ram-pages=%u\n",
   metadata->gamepak_ram_pages);
  perf_report_append(&writer, "state=%s\n",
   perf_report_state_name(metrics->state));
  perf_report_append_u64(&writer, "emulated-frames", metrics->emulated_frames);
  perf_report_append_u64(&writer, "presented-frames",
   metrics->presented_frames);
  perf_report_append_u64(&writer, "skipped-frames", metrics->skipped_frames);
  perf_report_append_u64(&writer, "fast-forward-frames",
   metrics->fast_forward_frames);
  perf_report_append_u64(&writer, "fast-forward-presented-frames",
   metrics->fast_forward_presented_frames);
  perf_report_append_u64(&writer, "fast-forward-time-us",
   metrics->fast_forward_time_us);
  perf_report_append_u64(&writer, "fast-forward-transitions",
   metrics->fast_forward_transitions);
  perf_report_append_u64(&writer, "fast-forward-audio-fallbacks",
   metrics->fast_forward_audio_fallbacks);
  perf_report_append_u64(&writer, "audio-recoveries",
   metrics->audio_recoveries);
  perf_report_append_u64(&writer, "audio-underruns", metrics->audio_underruns);
  perf_report_append_u64(&writer, "dynarec-compilations",
   metrics->dynarec_compilations);
  perf_report_append_u64(&writer, "dynarec-reuse-hits",
   metrics->dynarec_reuse_hits);
  perf_report_append_u64(&writer, "dynarec-retries", metrics->dynarec_retries);
  perf_report_append_u64(&writer, "dynarec-flushes", metrics->dynarec_flushes);
  perf_report_append_u64(&writer, "dynarec-readonly-flushes",
   metrics->dynarec_readonly_flushes);
  perf_report_append_u64(&writer, "dynarec-writable-flushes",
   metrics->dynarec_writable_flushes);
  perf_report_append_u64(&writer, "dynarec-cache-high-water",
   metrics->dynarec_cache_high_water);
  perf_report_append_u64(&writer, "dynarec-readonly-high-water",
   metrics->dynarec_readonly_high_water);
  perf_report_append_u64(&writer, "dynarec-writable-high-water",
   metrics->dynarec_writable_high_water);
  perf_report_append_u64(&writer, "memory-low-water",
   metrics->memory_low_water);
  perf_report_append(&writer, "memory-sampled=%d\n",
   metrics->has_memory_sample != 0);
  perf_report_append(&writer, "heap-free-low=%u\n", metadata->heap_free_low);
  perf_report_append(&writer, "heap-largest-low=%u\n",
   metadata->heap_largest_low);
  perf_report_append(&writer, "kernel-free-low=%u\n",
   metadata->kernel_free_low);
  perf_report_append_u64(&writer, "frame-time-samples",
   metrics->frame_time_samples);
  perf_report_append_u64(&writer, "frame-time-total-us",
   metrics->frame_time_total_us);
  perf_report_append(&writer, "frame-time-max-us=%u\n",
   metrics->frame_time_max_us);
  perf_report_append(&writer, "frame-time-p50-us=%u\n",
   metrics->frame_time_p50_us);
  perf_report_append(&writer, "frame-time-p95-us=%u\n",
   metrics->frame_time_p95_us);
  perf_report_append(&writer, "frame-time-p99-us=%u\n",
   metrics->frame_time_p99_us);
  perf_report_append_u64(&writer, "ge-wait-samples", metrics->ge_wait_samples);
  perf_report_append_u64(&writer, "ge-wait-total-us",
   metrics->ge_wait_total_us);
  perf_report_append(&writer, "ge-wait-max-us=%u\n",
   metrics->ge_wait_max_us);
  perf_report_append(&writer, "ge-wait-p50-us=%u\n",
   metrics->ge_wait_p50_us);
  perf_report_append(&writer, "ge-wait-p95-us=%u\n",
   metrics->ge_wait_p95_us);
  perf_report_append(&writer, "ge-wait-p99-us=%u\n",
   metrics->ge_wait_p99_us);
  perf_report_append_u64(&writer, "vblank-backpressure-count",
   metrics->vblank_backpressure_count);
  perf_report_append_u64(&writer, "vblank-backpressure-total-us",
   metrics->vblank_backpressure_total_us);
  perf_report_append_u64(&writer, "vblank-present-count",
   metrics->vblank_present_count);
  perf_report_append_u64(&writer, "vblank-present-total-us",
   metrics->vblank_present_total_us);
  perf_report_append_u64(&writer, "vblank-audio-count",
   metrics->vblank_audio_count);
  perf_report_append_u64(&writer, "vblank-audio-total-us",
   metrics->vblank_audio_total_us);
  perf_report_append_u64(&writer, "renderer-total-us",
   metrics->renderer_total_us);
  perf_report_append(&writer, "renderer-max-us=%u\n",
   metrics->renderer_max_us);
  perf_report_append_u64(&writer, "dcache-writeback-count",
   metrics->dcache_writeback_count);
  perf_report_append_u64(&writer, "dcache-writeback-total-us",
   metrics->dcache_writeback_total_us);
  perf_report_append_u64(&writer, "ge-sync-direct-count",
   metrics->ge_sync_direct_count);
  perf_report_append_u64(&writer, "ge-sync-direct-total-us",
   metrics->ge_sync_direct_total_us);
  perf_report_append_u64(&writer, "sharp-presented-frames",
   metrics->sharp_presented_frames);
  perf_report_append_u64(&writer, "crisp-presented-frames",
   metrics->crisp_presented_frames);
  perf_report_append_u64(&writer, "gba-update-entries",
   metrics->gba_update_entries);
  perf_report_append_u64(&writer, "gba-update-iterations",
   metrics->gba_update_iterations);
  perf_report_append_u64(&writer, "dma16-transfers", metrics->dma16_transfers);
  perf_report_append_u64(&writer, "dma16-elements", metrics->dma16_elements);
  perf_report_append_u64(&writer, "dma16-bytes", metrics->dma16_bytes);
  perf_report_append_u64(&writer, "dma16-total-us", metrics->dma16_total_us);
  perf_report_append_u64(&writer, "dma32-transfers", metrics->dma32_transfers);
  perf_report_append_u64(&writer, "dma32-elements", metrics->dma32_elements);
  perf_report_append_u64(&writer, "dma32-bytes", metrics->dma32_bytes);
  perf_report_append_u64(&writer, "dma32-total-us", metrics->dma32_total_us);
  perf_report_append_u64(&writer, "dma-gamepak-pairs",
   metrics->dma_gamepak_pairs);
  perf_report_append_u64(&writer, "gamepak-evict-scan-count",
   metrics->gamepak_evict_scan_count);
  perf_report_append_u64(&writer, "gamepak-evict-scan-total-us",
   metrics->gamepak_evict_scan_total_us);
  perf_report_append_u64(&writer, "gamepak-miss-count",
   metrics->gamepak_miss_count);
  perf_report_append_u64(&writer, "gamepak-miss-bytes",
   metrics->gamepak_miss_bytes);
  perf_report_append_u64(&writer, "gamepak-read-errors",
   metrics->gamepak_read_errors);
  perf_report_append_u64(&writer, "gamepak-rom-seek-total-us",
   metrics->gamepak_rom_seek_total_us);
  perf_report_append_u64(&writer, "gamepak-rom-read-total-us",
   metrics->gamepak_rom_read_total_us);

  if(writer.failed)
  {
    output[0] = '\0';
    return -1;
  }

  return (int)writer.length;
}
