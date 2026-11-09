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

#include "autorun_config.h"

#include <string.h>

#define AUTORUN_CONFIG_TRACE_SUFFIX ".gbit"

/* In AutorunError order; tools/decode_runtime_log.py keeps the same
   list. */
static const char *const autorun_config_error_names[] =
{
  "none",
  "too-large",
  "not-text",
  "line-too-long",
  "version",
  "syntax",
  "unknown-key",
  "duplicate-key",
  "bad-value",
  "build-missing",
  "build-mismatch",
  "build-unknown",
  "needs-rom",
  "needs-frames",
  "read",
  "rom",
  "trace"
};

static const char *const autorun_config_status_names[] =
{
  "ignored",
  "failed",
  "started",
  "done"
};

typedef char autorun_config_error_names_check
 [(sizeof(autorun_config_error_names) / sizeof(autorun_config_error_names[0])
   == AUTORUN_ERROR_TRACE + 1) ? 1 : -1];

enum
{
  AUTORUN_KEY_VERSION,
  AUTORUN_KEY_BUILD,
  AUTORUN_KEY_ROM,
  AUTORUN_KEY_TRACE,
  AUTORUN_KEY_FRAMES,
  AUTORUN_KEY_EXIT,
  AUTORUN_KEY_COUNT
};

static const char *const autorun_config_keys[AUTORUN_KEY_COUNT] =
{
  "version", "build", "rom", "trace", "frames", "exit"
};

typedef struct
{
  char *out;
  size_t size;
  size_t length;
  int full;
} AutorunWriter;

static int autorun_config_fail(AutorunConfig *config, AutorunError error,
 uint32_t line)
{
  memset(config, 0, sizeof(*config));
  config->error = error;
  config->error_line = line;
  return 0;
}

/* The key's index, or AUTORUN_KEY_COUNT for an unknown key. */
static int autorun_config_key(const char *key, size_t length)
{
  int i;

  for(i = 0; i < AUTORUN_KEY_COUNT; i++)
  {
    if(strlen(autorun_config_keys[i]) == length &&
       memcmp(autorun_config_keys[i], key, length) == 0)
      return i;
  }
  return AUTORUN_KEY_COUNT;
}

/* Decimal digits only, 1 to AUTORUN_CONFIG_FRAMES_MAX. */
static int autorun_config_frames(const char *text, size_t length,
 uint32_t *frames)
{
  uint32_t value = 0;
  size_t i;

  if(length == 0)
    return 0;
  for(i = 0; i < length; i++)
  {
    uint32_t digit;

    if(text[i] < '0' || text[i] > '9')
      return 0;
    digit = (uint32_t)(text[i] - '0');
    if(value > (AUTORUN_CONFIG_FRAMES_MAX - digit) / 10U)
      return 0;
    value = value * 10U + digit;
  }
  if(value == 0)
    return 0;
  *frames = value;
  return 1;
}

int autorun_config_parse(const char *text, size_t size, const char *build_id,
 AutorunConfig *config)
{
  uint32_t key_line[AUTORUN_KEY_COUNT] = { 0 };
  uint32_t number = 0;
  size_t at = 0;
  size_t suffix = sizeof(AUTORUN_CONFIG_TRACE_SUFFIX) - 1;

  memset(config, 0, sizeof(*config));
  if(size > AUTORUN_CONFIG_MAX_SIZE)
    return autorun_config_fail(config, AUTORUN_ERROR_TOO_LARGE, 0);
  if(build_id == NULL || build_id[0] == '\0' ||
     strcmp(build_id, "unknown") == 0)
    return autorun_config_fail(config, AUTORUN_ERROR_BUILD_UNKNOWN, 0);

  while(at < size)
  {
    const char *line = text + at;
    const char *value;
    size_t length = 0;
    size_t value_length;
    size_t i;
    int key;

    number++;
    while(at + length < size && line[length] != '\n')
      length++;
    at += length;
    if(at < size)
      at++;
    if(length > 0 && line[length - 1] == '\r')
      length--;

    if(length > AUTORUN_CONFIG_LINE_MAX)
      return autorun_config_fail(config, AUTORUN_ERROR_LINE_TOO_LONG,
       number);
    for(i = 0; i < length; i++)
    {
      if((unsigned char)line[i] < 0x20U || (unsigned char)line[i] > 0x7EU)
        return autorun_config_fail(config, AUTORUN_ERROR_NOT_TEXT, number);
    }
    if(length == 0 || line[0] == '#')
      continue;

    if(key_line[AUTORUN_KEY_VERSION] == 0)
    {
      if(length != 9 || memcmp(line, "version=1", 9) != 0)
        return autorun_config_fail(config, AUTORUN_ERROR_VERSION, number);
      key_line[AUTORUN_KEY_VERSION] = number;
      continue;
    }

    for(i = 0; i < length && line[i] != '='; i++)
      ;
    if(i == length)
      return autorun_config_fail(config, AUTORUN_ERROR_SYNTAX, number);
    key = autorun_config_key(line, i);
    value = line + i + 1;
    value_length = length - i - 1;
    if(key == AUTORUN_KEY_COUNT)
      return autorun_config_fail(config, AUTORUN_ERROR_UNKNOWN_KEY, number);
    if(key_line[key] != 0)
      return autorun_config_fail(config, AUTORUN_ERROR_DUPLICATE_KEY,
       number);
    /* The build gate comes before every other key is read. */
    if(key != AUTORUN_KEY_BUILD && key_line[AUTORUN_KEY_BUILD] == 0)
      return autorun_config_fail(config, AUTORUN_ERROR_BUILD_MISSING,
       number);
    key_line[key] = number;

    switch(key)
    {
      case AUTORUN_KEY_BUILD:
        if(value_length != strlen(build_id) ||
           memcmp(value, build_id, value_length) != 0)
          return autorun_config_fail(config, AUTORUN_ERROR_BUILD_MISMATCH,
           number);
        break;

      case AUTORUN_KEY_ROM:
        if(value_length == 0)
          return autorun_config_fail(config, AUTORUN_ERROR_BAD_VALUE,
           number);
        memcpy(config->rom, value, value_length);
        config->rom[value_length] = '\0';
        break;

      case AUTORUN_KEY_TRACE:
        /* main.c finds the .svs by swapping the extension. */
        if(value_length <= suffix ||
           memcmp(value + value_length - suffix, AUTORUN_CONFIG_TRACE_SUFFIX,
            suffix) != 0)
          return autorun_config_fail(config, AUTORUN_ERROR_BAD_VALUE,
           number);
        memcpy(config->trace, value, value_length);
        config->trace[value_length] = '\0';
        break;

      case AUTORUN_KEY_FRAMES:
        if(!autorun_config_frames(value, value_length, &config->frames))
          return autorun_config_fail(config, AUTORUN_ERROR_BAD_VALUE,
           number);
        break;

      case AUTORUN_KEY_EXIT:
        if(value_length != 1 || (value[0] != '0' && value[0] != '1'))
          return autorun_config_fail(config, AUTORUN_ERROR_BAD_VALUE,
           number);
        config->exit_when_done = value[0] == '1';
        break;

      default:
        /* version is caught above as a duplicate. */
        break;
    }
  }

  if(key_line[AUTORUN_KEY_VERSION] == 0)
    return autorun_config_fail(config, AUTORUN_ERROR_VERSION, 0);
  if(key_line[AUTORUN_KEY_BUILD] == 0)
    return autorun_config_fail(config, AUTORUN_ERROR_BUILD_MISSING, 0);
  if(key_line[AUTORUN_KEY_ROM] == 0)
  {
    if(key_line[AUTORUN_KEY_TRACE] != 0)
      return autorun_config_fail(config, AUTORUN_ERROR_NEEDS_ROM,
       key_line[AUTORUN_KEY_TRACE]);
    if(key_line[AUTORUN_KEY_FRAMES] != 0)
      return autorun_config_fail(config, AUTORUN_ERROR_NEEDS_ROM,
       key_line[AUTORUN_KEY_FRAMES]);
  }
  if(key_line[AUTORUN_KEY_EXIT] != 0 && key_line[AUTORUN_KEY_FRAMES] == 0)
    return autorun_config_fail(config, AUTORUN_ERROR_NEEDS_FRAMES,
     key_line[AUTORUN_KEY_EXIT]);
  return 1;
}

const char *autorun_config_error_name(AutorunError error)
{
  if((unsigned int)error > (unsigned int)AUTORUN_ERROR_TRACE)
    return "invalid";
  return autorun_config_error_names[error];
}

static void autorun_config_put(AutorunWriter *writer, const char *text)
{
  for(; *text != '\0'; text++)
  {
    if(writer->length + 1 >= writer->size)
    {
      writer->full = 1;
      return;
    }
    writer->out[writer->length++] = *text;
  }
}

static void autorun_config_put_number(AutorunWriter *writer, uint32_t value)
{
  char digits[11];
  size_t at = sizeof(digits) - 1;

  digits[at] = '\0';
  do
  {
    digits[--at] = (char)('0' + value % 10U);
    value /= 10U;
  }
  while(value != 0);
  autorun_config_put(writer, digits + at);
}

size_t autorun_config_result(const char *build_id, AutorunStatus status,
 AutorunError error, uint32_t line, uint32_t frames, char *out,
 size_t out_size)
{
  AutorunWriter writer;

  writer.out = out;
  writer.size = out_size;
  writer.length = 0;
  writer.full = 0;
  if(out == NULL || out_size == 0 ||
     (unsigned int)status > (unsigned int)AUTORUN_STATUS_DONE)
    return 0;

  autorun_config_put(&writer, "version=1\nbuild=");
  autorun_config_put(&writer, build_id != NULL ? build_id : "");
  autorun_config_put(&writer, "\nstatus=");
  autorun_config_put(&writer, autorun_config_status_names[status]);
  autorun_config_put(&writer, "\nreason=");
  autorun_config_put(&writer, autorun_config_error_name(error));
  autorun_config_put(&writer, "\nline=");
  autorun_config_put_number(&writer, line);
  autorun_config_put(&writer, "\nframes=");
  autorun_config_put_number(&writer, frames);
  autorun_config_put(&writer, "\n");
  if(writer.full)
    return 0;
  out[writer.length] = '\0';
  return writer.length;
}
