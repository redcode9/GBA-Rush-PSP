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

#include "state_hash.h"

#include <string.h>

#define STATE_HASH_FNV_PRIME 16777619U

/* The record size is part of the capture buffer's budget, and the region
   enum must end where the count says. */
typedef char state_hash_record_size_check
 [(sizeof(StateHashRecord) == 4U + 4U * STATE_HASH_REGION_COUNT) ? 1 : -1];
typedef char state_hash_region_count_check
 [(STATE_HASH_AUDIO + 1U == STATE_HASH_REGION_COUNT) ? 1 : -1];

typedef struct
{
  char *out;
  size_t length;
} StateHashWriter;

typedef struct
{
  const char *at;
  const char *end;
} StateHashReader;

static int state_hash_is_token(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
   (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

static int state_hash_is_name(const char *name)
{
  if(name == NULL || *name == '\0')
    return 0;
  for(; *name != '\0'; name++)
  {
    if(!state_hash_is_token(*name))
      return 0;
  }
  return 1;
}

uint32_t state_hash_words(uint32_t hash, const void *data, uint32_t size)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t words = size / 4U;
  uint32_t word;
  uint32_t i;

  /* memcpy keeps unaligned reads legal; GCC turns it into lwl/lwr. */
  for(i = 0; i < words; i++)
  {
    memcpy(&word, bytes + i * 4U, sizeof(word));
    hash = (hash ^ word) * STATE_HASH_FNV_PRIME;
  }
  if((size & 3U) != 0)
  {
    word = 0;
    memcpy(&word, bytes + words * 4U, size & 3U);
    hash = (hash ^ word) * STATE_HASH_FNV_PRIME;
  }
  return hash;
}

int state_hash_log_init(StateHashLog *log, StateHashRecord *records,
 uint32_t capacity, const char *const *names, const char *build_id,
 const uint8_t game_code[STATE_HASH_GAME_CODE_SIZE], uint32_t revision,
 uint32_t start_frame)
{
  uint32_t i;

  if(log == NULL || names == NULL || game_code == NULL ||
     (records == NULL && capacity != 0))
    return 0;
  for(i = 0; i < STATE_HASH_REGION_COUNT; i++)
  {
    if(!state_hash_is_name(names[i]))
      return 0;
  }

  memset(log, 0, sizeof(*log));
  log->records = records;
  log->capacity = capacity;
  log->names = names;
  log->revision = revision;
  log->start_frame = start_frame;
  /* Each identity field stays a single token of the identity line. */
  for(i = 0; build_id != NULL && i < STATE_HASH_BUILD_ID_MAX &&
   build_id[i] != '\0'; i++)
    log->build_id[i] = state_hash_is_token(build_id[i]) ? build_id[i] : '_';
  for(i = 0; i < STATE_HASH_GAME_CODE_SIZE; i++)
  {
    char c = (char)game_code[i];

    log->game_code[i] = state_hash_is_token(c) ? c : '_';
  }
  return 1;
}

int state_hash_capture(StateHashLog *log, uint32_t frame,
 const StateHashSpan *spans, uint32_t span_count)
{
  StateHashRecord *record;
  uint32_t i;

  if(log == NULL || (spans == NULL && span_count != 0))
    return 0;
  if(log->count >= log->capacity)
  {
    log->full = 1;
    return 0;
  }

  record = &log->records[log->count];
  record->frame = frame - log->start_frame;
  for(i = 0; i < STATE_HASH_REGION_COUNT; i++)
    record->hash[i] = STATE_HASH_FNV_BASIS;
  for(i = 0; i < span_count; i++)
  {
    uint32_t region = spans[i].region;

    if(region >= STATE_HASH_REGION_COUNT)
      return 0;
    record->hash[region] = state_hash_words(record->hash[region],
     spans[i].data, spans[i].size);
  }
  log->count++;
  return 1;
}

static void state_hash_put(StateHashWriter *writer, const char *text,
 size_t length)
{
  if(writer->out != NULL)
    memcpy(writer->out + writer->length, text, length);
  writer->length += length;
}

static void state_hash_put_text(StateHashWriter *writer, const char *text)
{
  state_hash_put(writer, text, strlen(text));
}

static void state_hash_put_decimal(StateHashWriter *writer, uint32_t value)
{
  char digits[10];
  size_t count = 0;

  do
  {
    digits[sizeof(digits) - 1 - count++] = (char)('0' + value % 10U);
    value /= 10U;
  } while(value != 0);
  state_hash_put(writer, digits + sizeof(digits) - count, count);
}

static void state_hash_put_hex(StateHashWriter *writer, uint32_t value)
{
  char digits[8];
  size_t i;

  for(i = 0; i < sizeof(digits); i++)
    digits[i] = "0123456789abcdef"[(value >> (28U - 4U * i)) & 15U];
  state_hash_put(writer, digits, sizeof(digits));
}

/* Lays out the whole text; with writer->out NULL it only measures it. */
static void state_hash_write(StateHashWriter *writer, const StateHashLog *log,
 const StateHashCounter *counters, uint32_t counter_count)
{
  uint32_t i;
  uint32_t region;

  state_hash_put_text(writer, STATE_HASH_MAGIC "\nbuild=");
  state_hash_put_text(writer, log->build_id);
  state_hash_put_text(writer, " game=");
  state_hash_put_text(writer, log->game_code);
  state_hash_put_text(writer, " rev=");
  state_hash_put_decimal(writer, log->revision);
  state_hash_put_text(writer, " start=");
  state_hash_put_decimal(writer, log->start_frame);
  state_hash_put_text(writer, "\nregions=");
  for(region = 0; region < STATE_HASH_REGION_COUNT; region++)
  {
    if(region != 0)
      state_hash_put_text(writer, ",");
    state_hash_put_text(writer, log->names[region]);
  }
  state_hash_put_text(writer, "\n");

  for(i = 0; i < log->count; i++)
  {
    state_hash_put_decimal(writer, log->records[i].frame);
    for(region = 0; region < STATE_HASH_REGION_COUNT; region++)
    {
      state_hash_put_text(writer, " ");
      state_hash_put_hex(writer, log->records[i].hash[region]);
    }
    state_hash_put_text(writer, "\n");
  }

  if(counter_count == 0)
    return;
  state_hash_put_text(writer, "counters");
  for(i = 0; i < counter_count; i++)
  {
    state_hash_put_text(writer, " ");
    state_hash_put_text(writer, counters[i].name);
    state_hash_put_text(writer, "=");
    state_hash_put_decimal(writer, counters[i].value);
  }
  state_hash_put_text(writer, "\n");
}

size_t state_hash_text_size(const StateHashLog *log,
 const StateHashCounter *counters, uint32_t counter_count)
{
  StateHashWriter writer = { NULL, 0 };
  uint32_t i;

  if(log == NULL || log->names == NULL ||
     (counters == NULL && counter_count != 0))
    return 0;
#if SIZE_MAX / 128U < UINT32_MAX
  /* A record line is at most 92 bytes: keep the total far from SIZE_MAX. */
  if(log->count > SIZE_MAX / 128U)
    return 0;
#endif
  for(i = 0; i < counter_count; i++)
  {
    if(!state_hash_is_name(counters[i].name))
      return 0;
  }

  state_hash_write(&writer, log, counters, counter_count);
  return writer.length;
}

int state_hash_export(const StateHashLog *log,
 const StateHashCounter *counters, uint32_t counter_count, char *out,
 size_t out_size, size_t *written)
{
  StateHashWriter writer;
  size_t required;

  if(written != NULL)
    *written = 0;
  required = state_hash_text_size(log, counters, counter_count);
  if(required == 0 || out == NULL || out_size < required || written == NULL)
    return 0;

  writer.out = out;
  writer.length = 0;
  state_hash_write(&writer, log, counters, counter_count);
  *written = writer.length;
  return 1;
}

static int state_hash_take(StateHashReader *reader, const char *literal)
{
  size_t length = strlen(literal);

  if((size_t)(reader->end - reader->at) < length ||
     memcmp(reader->at, literal, length) != 0)
    return 0;
  reader->at += length;
  return 1;
}

/* Copies at most `max` token characters and NUL-terminates them; returns
   how many it copied. A longer run fails on the separator that follows. */
static size_t state_hash_take_token(StateHashReader *reader, char *out,
 size_t max)
{
  size_t length = 0;

  while(length < max && reader->at < reader->end &&
   state_hash_is_token(*reader->at))
    out[length++] = *reader->at++;
  out[length] = '\0';
  return length;
}

static int state_hash_take_decimal(StateHashReader *reader, uint32_t *value)
{
  const char *start = reader->at;
  uint32_t result = 0;

  while(reader->at < reader->end && *reader->at >= '0' && *reader->at <= '9')
  {
    uint32_t digit = (uint32_t)(*reader->at - '0');

    if(result > (UINT32_MAX - digit) / 10U)
      return 0;
    result = result * 10U + digit;
    reader->at++;
  }
  *value = result;
  return reader->at != start;
}

static int state_hash_take_hex(StateHashReader *reader, uint32_t *value)
{
  uint32_t result = 0;
  size_t i;

  if(reader->end - reader->at < 8)
    return 0;
  for(i = 0; i < 8; i++)
  {
    char c = reader->at[i];

    if(c >= '0' && c <= '9')
      result = (result << 4) | (uint32_t)(c - '0');
    else if(c >= 'a' && c <= 'f')
      result = (result << 4) | (uint32_t)(c - 'a' + 10);
    else
      return 0;
  }
  reader->at += 8;
  *value = result;
  return 1;
}

static int state_hash_read(StateHashReader *reader, StateHashLog *log,
 StateHashCounter *counters, uint32_t counter_count, uint32_t *count)
{
  uint32_t i;
  uint32_t region;

  if(!state_hash_take(reader, STATE_HASH_MAGIC "\nbuild="))
    return 0;
  state_hash_take_token(reader, log->build_id, STATE_HASH_BUILD_ID_MAX);
  if(!state_hash_take(reader, " game=") ||
     state_hash_take_token(reader, log->game_code,
      STATE_HASH_GAME_CODE_SIZE) != STATE_HASH_GAME_CODE_SIZE ||
     !state_hash_take(reader, " rev=") ||
     !state_hash_take_decimal(reader, &log->revision) ||
     !state_hash_take(reader, " start=") ||
     !state_hash_take_decimal(reader, &log->start_frame) ||
     !state_hash_take(reader, "\nregions="))
    return 0;
  for(region = 0; region < STATE_HASH_REGION_COUNT; region++)
  {
    if((region != 0 && !state_hash_take(reader, ",")) ||
       !state_hash_take(reader, log->names[region]))
      return 0;
  }
  if(!state_hash_take(reader, "\n"))
    return 0;

  while(reader->at < reader->end && *reader->at >= '0' && *reader->at <= '9')
  {
    StateHashRecord *record;

    if(*count >= log->capacity)
      return 0;
    record = &log->records[*count];
    if(!state_hash_take_decimal(reader, &record->frame))
      return 0;
    for(region = 0; region < STATE_HASH_REGION_COUNT; region++)
    {
      if(!state_hash_take(reader, " ") ||
         !state_hash_take_hex(reader, &record->hash[region]))
        return 0;
    }
    if(!state_hash_take(reader, "\n"))
      return 0;
    (*count)++;
  }

  if(counter_count != 0)
  {
    if(!state_hash_take(reader, "counters"))
      return 0;
    for(i = 0; i < counter_count; i++)
    {
      if(!state_hash_take(reader, " ") ||
         !state_hash_take(reader, counters[i].name) ||
         !state_hash_take(reader, "=") ||
         !state_hash_take_decimal(reader, &counters[i].value))
        return 0;
    }
    if(!state_hash_take(reader, "\n"))
      return 0;
  }
  return reader->at == reader->end;
}

int state_hash_import(const char *text, size_t size, StateHashLog *log,
 StateHashCounter *counters, uint32_t counter_count)
{
  StateHashReader reader;
  uint32_t count = 0;
  uint32_t i;

  if(log == NULL || log->names == NULL || text == NULL ||
     (counters == NULL && counter_count != 0))
    return 0;
  log->count = 0;
  log->full = 0;
  for(i = 0; i < counter_count; i++)
  {
    if(!state_hash_is_name(counters[i].name))
      return 0;
  }

  reader.at = text;
  reader.end = text + size;
  if(!state_hash_read(&reader, log, counters, counter_count, &count))
    return 0;
  log->count = count;
  return 1;
}

int state_hash_first_divergence(const StateHashLog *a,
 const StateHashLog *b, uint32_t *frame, uint32_t *region)
{
  uint32_t shared = a->count < b->count ? a->count : b->count;
  uint32_t i;
  uint32_t r;

  for(i = 0; i < shared; i++)
  {
    const StateHashRecord *ra = &a->records[i];
    const StateHashRecord *rb = &b->records[i];

    if(ra->frame != rb->frame)
    {
      *frame = ra->frame < rb->frame ? ra->frame : rb->frame;
      *region = STATE_HASH_REGION_COUNT;
      return 1;
    }
    for(r = 0; r < STATE_HASH_REGION_COUNT; r++)
    {
      if(ra->hash[r] != rb->hash[r])
      {
        *frame = ra->frame;
        *region = r;
        return 1;
      }
    }
  }
  if(a->count == b->count)
    return 0;
  /* The first record only the longer log has. */
  *frame = (a->count > b->count ? a : b)->records[shared].frame;
  *region = STATE_HASH_REGION_COUNT;
  return 1;
}
