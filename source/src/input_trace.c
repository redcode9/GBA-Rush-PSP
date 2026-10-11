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

#include "input_trace.h"

#include <limits.h>
#include <string.h>

#define INPUT_TRACE_CHECKSUM_OFFSET 24U

static void input_trace_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8);
}

static void input_trace_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)value;
  output[1] = (uint8_t)(value >> 8);
  output[2] = (uint8_t)(value >> 16);
  output[3] = (uint8_t)(value >> 24);
}

static uint16_t input_trace_read_u16(const uint8_t *input)
{
  return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
}

static uint32_t input_trace_read_u32(const uint8_t *input)
{
  return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
   ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static uint32_t input_trace_checksum(const uint8_t *data, size_t size)
{
  uint32_t checksum = 2166136261U;
  size_t i;

  for(i = 0; i < size; i++)
  {
    uint8_t value = data[i];

    if(i >= INPUT_TRACE_CHECKSUM_OFFSET &&
       i < INPUT_TRACE_CHECKSUM_OFFSET + sizeof(uint32_t))
      value = 0;
    checksum ^= value;
    checksum *= 16777619U;
  }

  return checksum;
}

static int input_trace_fail(InputTrace *trace, InputTraceError error)
{
  if(trace != NULL)
  {
    trace->state = INPUT_TRACE_ERROR;
    trace->error = error;
  }
  return 0;
}

void input_trace_init(InputTrace *trace, InputTraceEvent *events,
 uint32_t event_capacity)
{
  if(trace == NULL)
    return;

  memset(trace, 0, sizeof(*trace));
  trace->events = events;
  trace->event_capacity = event_capacity;
  trace->state = INPUT_TRACE_IDLE;
}

void input_trace_cancel(InputTrace *trace)
{
  InputTraceEvent *events;
  uint32_t event_capacity;

  if(trace == NULL)
    return;

  events = trace->events;
  event_capacity = trace->event_capacity;
  memset(trace, 0, sizeof(*trace));
  trace->events = events;
  trace->event_capacity = event_capacity;
  trace->state = INPUT_TRACE_IDLE;
}

int input_trace_begin_record(InputTrace *trace, uint32_t start_frame,
 const uint8_t game_code[INPUT_TRACE_GAME_CODE_SIZE], uint8_t rom_revision,
 uint16_t initial_keys)
{
  if(trace == NULL || game_code == NULL || trace->events == NULL ||
     trace->event_capacity == 0)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ARGUMENT);

  input_trace_cancel(trace);
  trace->start_frame = start_frame;
  trace->last_recorded_keys = initial_keys & INPUT_TRACE_KEY_MASK;
  memcpy(trace->game_code, game_code, sizeof(trace->game_code));
  trace->rom_revision = rom_revision;
  trace->events[0].frame_offset = 0;
  trace->events[0].keys = trace->last_recorded_keys;
  trace->event_count = 1;
  trace->state = INPUT_TRACE_RECORDING;
  return 1;
}

int input_trace_record(InputTrace *trace, uint32_t frame, uint16_t keys)
{
  uint32_t frame_offset;

  if(trace == NULL || trace->state != INPUT_TRACE_RECORDING)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ARGUMENT);
  if(frame < trace->start_frame)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ORDER);

  frame_offset = frame - trace->start_frame;
  keys &= INPUT_TRACE_KEY_MASK;
  if(keys == trace->last_recorded_keys)
    return 1;

  if(frame_offset < trace->events[trace->event_count - 1U].frame_offset)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ORDER);

  if(frame_offset == trace->events[trace->event_count - 1U].frame_offset)
  {
    trace->events[trace->event_count - 1U].keys = keys;
    trace->last_recorded_keys = keys;
    return 1;
  }

  if(trace->event_count >= trace->event_capacity)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_CAPACITY);

  trace->events[trace->event_count].frame_offset = frame_offset;
  trace->events[trace->event_count].keys = keys;
  trace->event_count++;
  trace->last_recorded_keys = keys;
  return 1;
}

int input_trace_finish_record(InputTrace *trace, uint32_t end_frame)
{
  uint32_t duration;

  if(trace == NULL || trace->state != INPUT_TRACE_RECORDING)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ARGUMENT);
  if(end_frame < trace->start_frame)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ORDER);

  duration = end_frame - trace->start_frame;
  if(duration < trace->events[trace->event_count - 1U].frame_offset)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ORDER);

  trace->duration_frames = duration;
  trace->state = INPUT_TRACE_READY;
  return 1;
}

size_t input_trace_encoded_size(const InputTrace *trace)
{
  if(trace == NULL || trace->state != INPUT_TRACE_READY ||
     trace->event_count == 0)
    return 0;
#if SIZE_MAX < UINT32_MAX
  if(trace->event_count > (SIZE_MAX - INPUT_TRACE_HEADER_SIZE) /
     INPUT_TRACE_EVENT_SIZE)
    return 0;
#endif

  return INPUT_TRACE_HEADER_SIZE +
   (size_t)trace->event_count * INPUT_TRACE_EVENT_SIZE;
}

int input_trace_encode(const InputTrace *trace, uint8_t *output,
 size_t output_size, size_t *written)
{
  size_t required;
  uint32_t i;

  if(written != NULL)
    *written = 0;
  required = input_trace_encoded_size(trace);
  if(required == 0 || output == NULL || output_size < required ||
     written == NULL)
    return 0;

  memset(output, 0, required);
  memcpy(output, INPUT_TRACE_MAGIC, 4);
  input_trace_write_u16(output + 4, INPUT_TRACE_VERSION);
  input_trace_write_u16(output + 6, INPUT_TRACE_HEADER_SIZE);
  input_trace_write_u32(output + 8, trace->event_count);
  memcpy(output + 12, trace->game_code, sizeof(trace->game_code));
  output[16] = trace->rom_revision;
  input_trace_write_u32(output + 20, trace->duration_frames);
  input_trace_write_u32(output + 28, trace->start_frame);

  for(i = 0; i < trace->event_count; i++)
  {
    uint8_t *event = output + INPUT_TRACE_HEADER_SIZE +
     (size_t)i * INPUT_TRACE_EVENT_SIZE;

    input_trace_write_u32(event, trace->events[i].frame_offset);
    input_trace_write_u16(event + 4, trace->events[i].keys);
  }

  input_trace_write_u32(output + INPUT_TRACE_CHECKSUM_OFFSET,
   input_trace_checksum(output, required));
  *written = required;
  return 1;
}

int input_trace_begin_replay(InputTrace *trace, const uint8_t *encoded,
 size_t encoded_size,
 const uint8_t expected_game_code[INPUT_TRACE_GAME_CODE_SIZE],
 uint8_t expected_rom_revision, uint32_t start_frame)
{
  uint32_t event_count;
  uint32_t duration_frames;
  uint32_t previous_frame = 0;
  size_t required;
  uint32_t i;

  if(trace == NULL || encoded == NULL || expected_game_code == NULL ||
     trace->events == NULL)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_ARGUMENT);

  input_trace_cancel(trace);
  if(encoded_size < INPUT_TRACE_HEADER_SIZE ||
     memcmp(encoded, INPUT_TRACE_MAGIC, 4) != 0 ||
     input_trace_read_u16(encoded + 4) != INPUT_TRACE_VERSION ||
     input_trace_read_u16(encoded + 6) != INPUT_TRACE_HEADER_SIZE ||
     encoded[17] != 0 || encoded[18] != 0 || encoded[19] != 0)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_FORMAT);

  event_count = input_trace_read_u32(encoded + 8);
  if(event_count == 0)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_FORMAT);
#if SIZE_MAX < UINT32_MAX
  if(event_count > (SIZE_MAX - INPUT_TRACE_HEADER_SIZE) /
     INPUT_TRACE_EVENT_SIZE)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_FORMAT);
#endif
  required = INPUT_TRACE_HEADER_SIZE +
   (size_t)event_count * INPUT_TRACE_EVENT_SIZE;
  if(encoded_size != required)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_FORMAT);

  if(input_trace_read_u32(encoded + INPUT_TRACE_CHECKSUM_OFFSET) !=
     input_trace_checksum(encoded, encoded_size))
    return input_trace_fail(trace, INPUT_TRACE_ERROR_CHECKSUM);
  if(memcmp(encoded + 12, expected_game_code,
      INPUT_TRACE_GAME_CODE_SIZE) != 0 ||
     encoded[16] != expected_rom_revision)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_IDENTITY);
  if(event_count > trace->event_capacity)
    return input_trace_fail(trace, INPUT_TRACE_ERROR_CAPACITY);

  duration_frames = input_trace_read_u32(encoded + 20);
  for(i = 0; i < event_count; i++)
  {
    const uint8_t *event = encoded + INPUT_TRACE_HEADER_SIZE +
     (size_t)i * INPUT_TRACE_EVENT_SIZE;
    uint32_t frame_offset = input_trace_read_u32(event);
    uint16_t keys = input_trace_read_u16(event + 4);

    if((i == 0 && frame_offset != 0) ||
       (i != 0 && frame_offset <= previous_frame) ||
       frame_offset > duration_frames ||
       (keys & ~INPUT_TRACE_KEY_MASK) != 0 ||
       input_trace_read_u16(event + 6) != 0)
      return input_trace_fail(trace, INPUT_TRACE_ERROR_FORMAT);
    previous_frame = frame_offset;
  }

  for(i = 0; i < event_count; i++)
  {
    const uint8_t *event = encoded + INPUT_TRACE_HEADER_SIZE +
     (size_t)i * INPUT_TRACE_EVENT_SIZE;

    trace->events[i].frame_offset = input_trace_read_u32(event);
    trace->events[i].keys = input_trace_read_u16(event + 4);
  }

  trace->event_count = event_count;
  trace->event_cursor = 0;
  trace->start_frame = start_frame;
  trace->duration_frames = duration_frames;
  memcpy(trace->game_code, encoded + 12, sizeof(trace->game_code));
  trace->rom_revision = encoded[16];
  trace->current_keys = 0;
  trace->state = INPUT_TRACE_REPLAYING;
  trace->error = INPUT_TRACE_ERROR_NONE;
  return 1;
}

int input_trace_begin_record_from_header(InputTrace *trace,
 uint32_t start_frame,
 const uint8_t header[INPUT_TRACE_IDENTITY_SIZE], uint32_t file_size,
 uint16_t initial_keys)
{
  if(trace == NULL || header == NULL ||
     file_size < INPUT_TRACE_IDENTITY_SIZE)
    return 0;

  return input_trace_begin_record(trace, start_frame, header + 0xAC,
   header[0xBC], initial_keys);
}

int input_trace_begin_replay_from_header(InputTrace *trace,
 const uint8_t *encoded, size_t encoded_size,
 const uint8_t header[INPUT_TRACE_IDENTITY_SIZE], uint32_t file_size,
 uint32_t start_frame)
{
  if(trace == NULL || header == NULL ||
     file_size < INPUT_TRACE_IDENTITY_SIZE)
    return 0;

  return input_trace_begin_replay(trace, encoded, encoded_size,
   header + 0xAC, header[0xBC], start_frame);
}

uint16_t input_trace_apply(InputTrace *trace, uint32_t frame,
 uint16_t physical_keys)
{
  uint32_t frame_offset;

  physical_keys &= INPUT_TRACE_KEY_MASK;
  if(trace == NULL)
    return physical_keys;

  if(trace->state == INPUT_TRACE_RECORDING)
  {
    input_trace_record(trace, frame, physical_keys);
    return physical_keys;
  }
  if(trace->state != INPUT_TRACE_REPLAYING || frame < trace->start_frame)
    return physical_keys;

  frame_offset = frame - trace->start_frame;
  if(frame_offset > trace->duration_frames)
  {
    trace->state = INPUT_TRACE_FINISHED;
    return physical_keys;
  }

  while(trace->event_cursor < trace->event_count &&
        trace->events[trace->event_cursor].frame_offset <= frame_offset)
  {
    trace->current_keys = trace->events[trace->event_cursor].keys;
    trace->event_cursor++;
  }
  return trace->current_keys;
}
