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
 * Diagnostic input traces: records the GBA keys per frame as a list of
 * changes and replays them for repeatable test runs. Only built with
 * ENABLE_DIAGNOSTIC_INPUT_TRACE; input.c drives it from update_input.
 */

#ifndef INPUT_TRACE_H
#define INPUT_TRACE_H

#include <stddef.h>
#include <stdint.h>

#define INPUT_TRACE_MAGIC "GBIT"
#define INPUT_TRACE_VERSION 1U
#define INPUT_TRACE_HEADER_SIZE 32U
#define INPUT_TRACE_EVENT_SIZE 8U
#define INPUT_TRACE_GAME_CODE_SIZE 4U
/* Bytes of cartridge header needed to identify the ROM. */
#define INPUT_TRACE_IDENTITY_SIZE 0xC0U
/* The ten GBA key bits. */
#define INPUT_TRACE_KEY_MASK 0x03FFU

typedef enum
{
  INPUT_TRACE_IDLE,
  INPUT_TRACE_RECORDING,
  INPUT_TRACE_READY,
  INPUT_TRACE_REPLAYING,
  INPUT_TRACE_FINISHED,
  INPUT_TRACE_ERROR
} InputTraceState;

typedef enum
{
  INPUT_TRACE_ERROR_NONE,
  INPUT_TRACE_ERROR_ARGUMENT,
  INPUT_TRACE_ERROR_CAPACITY,
  INPUT_TRACE_ERROR_IDENTITY,
  INPUT_TRACE_ERROR_FORMAT,
  INPUT_TRACE_ERROR_CHECKSUM,
  INPUT_TRACE_ERROR_ORDER
} InputTraceError;

/* The keys held from frame_offset (relative to start_frame) onwards. */
typedef struct
{
  uint32_t frame_offset;
  uint16_t keys;
} InputTraceEvent;

typedef struct
{
  InputTraceEvent *events;
  uint32_t event_capacity;
  uint32_t event_count;
  uint32_t event_cursor;
  uint32_t start_frame;
  uint32_t duration_frames;
  uint16_t current_keys;
  uint16_t last_recorded_keys;
  uint8_t game_code[INPUT_TRACE_GAME_CODE_SIZE];
  uint8_t rom_revision;
  InputTraceState state;
  InputTraceError error;
} InputTrace;

void input_trace_init(InputTrace *trace, InputTraceEvent *events,
 uint32_t event_capacity);
void input_trace_cancel(InputTrace *trace);
int input_trace_begin_record(InputTrace *trace, uint32_t start_frame,
 const uint8_t game_code[INPUT_TRACE_GAME_CODE_SIZE], uint8_t rom_revision,
 uint16_t initial_keys);
int input_trace_record(InputTrace *trace, uint32_t frame, uint16_t keys);
int input_trace_finish_record(InputTrace *trace, uint32_t end_frame);
size_t input_trace_encoded_size(const InputTrace *trace);
int input_trace_encode(const InputTrace *trace, uint8_t *output,
 size_t output_size, size_t *written);
int input_trace_begin_replay(InputTrace *trace, const uint8_t *encoded,
 size_t encoded_size,
 const uint8_t expected_game_code[INPUT_TRACE_GAME_CODE_SIZE],
 uint8_t expected_rom_revision, uint32_t start_frame);
int input_trace_begin_record_from_header(InputTrace *trace,
 uint32_t start_frame,
 const uint8_t header[INPUT_TRACE_IDENTITY_SIZE], uint32_t file_size,
 uint16_t initial_keys);
int input_trace_begin_replay_from_header(InputTrace *trace,
 const uint8_t *encoded, size_t encoded_size,
 const uint8_t header[INPUT_TRACE_IDENTITY_SIZE], uint32_t file_size,
 uint32_t start_frame);
/* Called once per frame: records physical_keys while recording, returns the
   replayed keys while replaying, and physical_keys otherwise. */
uint16_t input_trace_apply(InputTrace *trace, uint32_t frame,
 uint16_t physical_keys);

#endif /* INPUT_TRACE_H */
