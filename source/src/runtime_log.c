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

#include "runtime_log.h"

#include <limits.h>
#include <string.h>

#include "zlib/zlib.h"

static void runtime_log_barrier(void)
{
  __sync_synchronize();
}

static void runtime_log_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8);
  output[1] = (uint8_t)value;
}

static void runtime_log_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static void runtime_log_write_u64(uint8_t *output, uint64_t value)
{
  runtime_log_write_u32(output, (uint32_t)(value >> 32));
  runtime_log_write_u32(output + 4, (uint32_t)value);
}

static uint16_t runtime_log_read_u16(const uint8_t *input)
{
  return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t runtime_log_read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
   ((uint32_t)input[2] << 8) | input[3];
}

static uint64_t runtime_log_read_u64(const uint8_t *input)
{
  return ((uint64_t)runtime_log_read_u32(input) << 32) |
   runtime_log_read_u32(input + 4);
}

void runtime_log_ring_init(RuntimeLogRing *ring, RuntimeLogSource source)
{
  if(ring == NULL)
    return;
  memset(ring, 0, sizeof(*ring));
  ring->source = (uint8_t)source;
}

static uint32_t runtime_log_ring_limit(const RuntimeLogRing *ring)
{
  return ring->source == RUNTIME_LOG_SOURCE_CRITICAL ?
   RUNTIME_LOG_CRITICAL_CAPACITY : RUNTIME_LOG_RING_CAPACITY;
}

void runtime_log_ring_emit(RuntimeLogRing *ring, uint64_t timestamp_us,
 uint32_t emulated_cycle, RuntimeLogEventType type, uint8_t flags,
 uint32_t value0, uint32_t value1, uint32_t value2)
{
  RuntimeLogRecord *record;
  uint32_t limit;
  uint32_t ordinal;
  uint32_t index;

  if(ring == NULL || ring->source == 0 || type == 0)
    return;

  limit = runtime_log_ring_limit(ring);
  ordinal = ring->next_ordinal == UINT_MAX ? 1u :
   ring->next_ordinal + 1u;
  index = (ordinal - 1u) & (limit - 1u);
  ring->published[index] = 0;
  runtime_log_barrier();

  record = &ring->records[index];
  record->timestamp_us = timestamp_us;
  record->emulated_cycle = emulated_cycle;
  record->ordinal = ordinal;
  record->type = (uint16_t)type;
  record->source = ring->source;
  record->flags = flags;
  record->value[0] = value0;
  record->value[1] = value1;
  record->value[2] = value2;

  runtime_log_barrier();
  ring->published[index] = ordinal;
  runtime_log_barrier();
  ring->next_ordinal = ordinal;
  if(ordinal > limit && ring->dropped_records != UINT_MAX)
    ring->dropped_records++;
}

size_t runtime_log_ring_snapshot(const RuntimeLogRing *ring,
 RuntimeLogRecord *records, size_t capacity)
{
  RuntimeLogRecord record;
  uint32_t available;
  uint32_t limit;
  uint32_t first;
  uint32_t latest;
  uint32_t ordinal;
  uint32_t published;
  uint32_t index;
  size_t count = 0;
  size_t offset;

  if(ring == NULL || records == NULL || capacity == 0)
    return 0;

  runtime_log_barrier();
  latest = ring->next_ordinal;
  if(latest == 0)
    return 0;
  limit = runtime_log_ring_limit(ring);
  available = latest < limit ? latest : limit;
  if(available > capacity)
    available = (uint32_t)capacity;
  first = latest - available + 1u;

  for(offset = 0; offset < available; offset++)
  {
    ordinal = first + (uint32_t)offset;
    index = (ordinal - 1u) & (limit - 1u);
    published = ring->published[index];
    runtime_log_barrier();
    record = ring->records[index];
    runtime_log_barrier();
    if(published == ordinal && ring->published[index] == ordinal &&
       record.ordinal == ordinal)
      records[count++] = record;
  }
  return count;
}

uint32_t runtime_log_ring_dropped(const RuntimeLogRing *ring)
{
  if(ring == NULL)
    return 0;
  runtime_log_barrier();
  return ring->dropped_records;
}

static void runtime_log_encode_record(uint8_t *output,
 const RuntimeLogRecord *record)
{
  runtime_log_write_u64(output, record->timestamp_us);
  runtime_log_write_u32(output + 8, record->emulated_cycle);
  runtime_log_write_u32(output + 12, record->ordinal);
  runtime_log_write_u16(output + 16, record->type);
  output[18] = record->source;
  output[19] = record->flags;
  runtime_log_write_u32(output + 20, record->value[0]);
  runtime_log_write_u32(output + 24, record->value[1]);
  runtime_log_write_u32(output + 28, record->value[2]);
}

int runtime_log_encode(const RuntimeLogRing *main_ring,
 const RuntimeLogRing *network_ring, const RuntimeLogRing *critical_ring,
 const RuntimeLogMetadata *metadata, uint8_t *output, size_t capacity,
 size_t *written)
{
  RuntimeLogRecord main_records[RUNTIME_LOG_RING_CAPACITY];
  RuntimeLogRecord network_records[RUNTIME_LOG_RING_CAPACITY];
  RuntimeLogRecord critical_records[RUNTIME_LOG_CRITICAL_CAPACITY];
  size_t main_count;
  size_t network_count;
  size_t critical_count;
  size_t payload_size;
  size_t total_size;
  size_t index;
  uint8_t *cursor;
  uLong crc;

  if(main_ring == NULL || network_ring == NULL || critical_ring == NULL ||
     metadata == NULL || output == NULL || written == NULL)
    return 0;

  main_count = runtime_log_ring_snapshot(main_ring, main_records,
   RUNTIME_LOG_RING_CAPACITY);
  network_count = runtime_log_ring_snapshot(network_ring, network_records,
   RUNTIME_LOG_RING_CAPACITY);
  critical_count = runtime_log_ring_snapshot(critical_ring, critical_records,
   RUNTIME_LOG_CRITICAL_CAPACITY);
  payload_size = (main_count + network_count + critical_count) *
   RUNTIME_LOG_RECORD_SIZE;
  total_size = RUNTIME_LOG_HEADER_SIZE + payload_size;
  if(total_size > capacity || main_count > UINT16_MAX ||
     network_count > UINT16_MAX || critical_count > UINT16_MAX)
    return 0;

  memset(output, 0, RUNTIME_LOG_HEADER_SIZE);
  memcpy(output, "GBRL", 4);
  runtime_log_write_u16(output + 4, RUNTIME_LOG_VERSION);
  runtime_log_write_u16(output + 6, RUNTIME_LOG_HEADER_SIZE);
  runtime_log_write_u16(output + 8, RUNTIME_LOG_RECORD_SIZE);
  runtime_log_write_u16(output + 10, (uint16_t)critical_count);
  runtime_log_write_u16(output + 12, (uint16_t)main_count);
  runtime_log_write_u16(output + 14, (uint16_t)network_count);
  runtime_log_write_u32(output + 16, metadata->session_id);
  runtime_log_write_u32(output + 20, metadata->flush_reason);
  runtime_log_write_u32(output + 24, metadata->psp_model);
  runtime_log_write_u32(output + 28, metadata->firmware);
  runtime_log_write_u32(output + 32, metadata->cpu_clock_mhz);
  runtime_log_write_u32(output + 36, metadata->rom_identity);
  runtime_log_write_u64(output + 40, metadata->started_us);
  runtime_log_write_u64(output + 48, metadata->flushed_us);

  cursor = output + RUNTIME_LOG_HEADER_SIZE;
  for(index = 0; index < main_count; index++)
  {
    runtime_log_encode_record(cursor, &main_records[index]);
    cursor += RUNTIME_LOG_RECORD_SIZE;
  }
  for(index = 0; index < network_count; index++)
  {
    runtime_log_encode_record(cursor, &network_records[index]);
    cursor += RUNTIME_LOG_RECORD_SIZE;
  }
  for(index = 0; index < critical_count; index++)
  {
    runtime_log_encode_record(cursor, &critical_records[index]);
    cursor += RUNTIME_LOG_RECORD_SIZE;
  }

  crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, output + RUNTIME_LOG_HEADER_SIZE,
   (uInt)payload_size);
  runtime_log_write_u32(output + 56, (uint32_t)crc);
  crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, output, 60);
  runtime_log_write_u32(output + 60, (uint32_t)crc);
  *written = total_size;
  return 1;
}

int runtime_log_validate(const uint8_t *input, size_t size,
 RuntimeLogMetadata *metadata, uint16_t *main_count,
 uint16_t *network_count, uint16_t *critical_count)
{
  uint16_t decoded_version;
  uint16_t decoded_main;
  uint16_t decoded_network;
  uint16_t decoded_critical;
  size_t expected_size;
  uLong crc;

  if(input == NULL || size < RUNTIME_LOG_HEADER_SIZE ||
     memcmp(input, "GBRL", 4) != 0 ||
     runtime_log_read_u16(input + 6) != RUNTIME_LOG_HEADER_SIZE ||
     runtime_log_read_u16(input + 8) != RUNTIME_LOG_RECORD_SIZE)
    return 0;

  decoded_version = runtime_log_read_u16(input + 4);
  if(decoded_version < 1u || decoded_version > RUNTIME_LOG_VERSION)
    return 0;
  decoded_critical = decoded_version >= 2u ?
   runtime_log_read_u16(input + 10) : 0u;
  decoded_main = runtime_log_read_u16(input + 12);
  decoded_network = runtime_log_read_u16(input + 14);
  if(decoded_main > RUNTIME_LOG_RING_CAPACITY ||
     decoded_network > RUNTIME_LOG_RING_CAPACITY ||
     decoded_critical > RUNTIME_LOG_CRITICAL_CAPACITY)
    return 0;
  expected_size = RUNTIME_LOG_HEADER_SIZE +
   ((size_t)decoded_main + decoded_network + decoded_critical) *
   RUNTIME_LOG_RECORD_SIZE;
  if(size != expected_size)
    return 0;

  crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, input, 60);
  if((uint32_t)crc != runtime_log_read_u32(input + 60))
    return 0;
  crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, input + RUNTIME_LOG_HEADER_SIZE,
   (uInt)(size - RUNTIME_LOG_HEADER_SIZE));
  if((uint32_t)crc != runtime_log_read_u32(input + 56))
    return 0;

  if(metadata != NULL)
  {
    memset(metadata, 0, sizeof(*metadata));
    metadata->session_id = runtime_log_read_u32(input + 16);
    metadata->flush_reason = runtime_log_read_u32(input + 20);
    metadata->psp_model = runtime_log_read_u32(input + 24);
    metadata->firmware = runtime_log_read_u32(input + 28);
    metadata->cpu_clock_mhz = runtime_log_read_u32(input + 32);
    metadata->rom_identity = runtime_log_read_u32(input + 36);
    metadata->started_us = runtime_log_read_u64(input + 40);
    metadata->flushed_us = runtime_log_read_u64(input + 48);
  }
  if(main_count != NULL)
    *main_count = decoded_main;
  if(network_count != NULL)
    *network_count = decoded_network;
  if(critical_count != NULL)
    *critical_count = decoded_critical;
  return 1;
}
