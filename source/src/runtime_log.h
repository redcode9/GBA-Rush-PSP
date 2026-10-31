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
 * Runtime diagnostic log. Events are 32-byte records kept in fixed rings in
 * memory (main, network and critical sources) and encoded into a "GBRL"
 * snapshot when runtime_log_psp.c flushes. tools/decode_runtime_log.py
 * reads the files; enum values and record layouts are part of the format.
 */

#ifndef RUNTIME_LOG_H
#define RUNTIME_LOG_H

#include <stddef.h>
#include <stdint.h>

#define RUNTIME_LOG_VERSION 5u
#define RUNTIME_LOG_HEADER_SIZE 64u
#define RUNTIME_LOG_RECORD_SIZE 32u
#define RUNTIME_LOG_RING_CAPACITY 128u
/* The critical ring uses only this many of its slots. */
#define RUNTIME_LOG_CRITICAL_CAPACITY 64u
#define RUNTIME_LOG_MAX_FILE_SIZE \
 (RUNTIME_LOG_HEADER_SIZE + \
  (((RUNTIME_LOG_RING_CAPACITY * 2u) + RUNTIME_LOG_CRITICAL_CAPACITY) * \
   RUNTIME_LOG_RECORD_SIZE))

/* Ring indices are masked, so the capacity must be a power of two. */
typedef char RuntimeLogRingCapacityCheck[
 (RUNTIME_LOG_RING_CAPACITY != 0u &&
  (RUNTIME_LOG_RING_CAPACITY & (RUNTIME_LOG_RING_CAPACITY - 1u)) == 0u) ?
  1 : -1];

typedef enum
{
  RUNTIME_LOG_SOURCE_MAIN = 1,
  RUNTIME_LOG_SOURCE_NETWORK = 2,
  RUNTIME_LOG_SOURCE_CRITICAL = 3
} RuntimeLogSource;

typedef enum
{
  RUNTIME_LOG_EVENT_SESSION_START = 1,
  RUNTIME_LOG_EVENT_SESSION_END,
  RUNTIME_LOG_EVENT_ROM_OPEN,
  RUNTIME_LOG_EVENT_MENU_PRESENCE,
  RUNTIME_LOG_EVENT_POWER_SUSPEND,
  RUNTIME_LOG_EVENT_POWER_RESUME,
  RUNTIME_LOG_EVENT_LINK_COMMAND,
  RUNTIME_LOG_EVENT_LINK_STATE,
  RUNTIME_LOG_EVENT_LINK_PAUSE_BEGIN,
  RUNTIME_LOG_EVENT_LINK_PAUSE_END,
  RUNTIME_LOG_EVENT_LINK_ERROR,
  RUNTIME_LOG_EVENT_LINK_RECOVERY,
  RUNTIME_LOG_EVENT_LINK_RESYNC,
  RUNTIME_LOG_EVENT_LINK_REQUEST_ACCEPTED,
  RUNTIME_LOG_EVENT_LINK_REQUEST_REJECTED,
  RUNTIME_LOG_EVENT_LINK_COUNTERS,
  RUNTIME_LOG_EVENT_PERFORMANCE_SLOW,
  RUNTIME_LOG_EVENT_LOG_FLUSH,
  RUNTIME_LOG_EVENT_LINK_TRANSFER_SAMPLE,
  RUNTIME_LOG_EVENT_LINK_CHECKPOINT,
  RUNTIME_LOG_EVENT_LINK_HEALTH,
  RUNTIME_LOG_EVENT_LINK_FAILURE,
  RUNTIME_LOG_EVENT_LINK_STREAM_METRICS,
  RUNTIME_LOG_EVENT_LINK_TIMEOUT,
  RUNTIME_LOG_EVENT_LINK_BENCHMARK
} RuntimeLogEventType;

typedef enum
{
  RUNTIME_LOG_FLUSH_START = 1,
  RUNTIME_LOG_FLUSH_LINK_FAILURE,
  RUNTIME_LOG_FLUSH_DISCONNECT,
  RUNTIME_LOG_FLUSH_SUSPEND,
  RUNTIME_LOG_FLUSH_ROM_CHANGE,
  RUNTIME_LOG_FLUSH_MENU,
  RUNTIME_LOG_FLUSH_EXIT,
  RUNTIME_LOG_FLUSH_LINK_CHECKPOINT
} RuntimeLogFlushReason;

/* value[] meanings depend on the event type; the decoder documents them. */
typedef struct
{
  uint64_t timestamp_us;
  uint32_t emulated_cycle;
  uint32_t ordinal;
  uint16_t type;
  uint8_t source;
  uint8_t flags;
  uint32_t value[3];
} RuntimeLogRecord;

typedef char RuntimeLogRecordSizeCheck[
 sizeof(RuntimeLogRecord) == RUNTIME_LOG_RECORD_SIZE ? 1 : -1];

/* One writer per ring; the oldest record is overwritten when it is full.
   published[i] is the ordinal of the complete record in slot i, or 0 while
   it is being written, so a reader can skip torn records. Ordinals count
   from 1. */
typedef struct
{
  RuntimeLogRecord records[RUNTIME_LOG_RING_CAPACITY];
  volatile uint32_t published[RUNTIME_LOG_RING_CAPACITY];
  volatile uint32_t next_ordinal;
  volatile uint32_t dropped_records;
  uint8_t source;
} RuntimeLogRing;

typedef struct
{
  uint32_t session_id;
  uint32_t flush_reason;
  uint32_t psp_model;
  uint32_t firmware;
  uint32_t cpu_clock_mhz;
  uint32_t rom_identity;
  uint64_t started_us;
  uint64_t flushed_us;
} RuntimeLogMetadata;

void runtime_log_ring_init(RuntimeLogRing *ring, RuntimeLogSource source);
/* Never blocks or allocates; safe on the emulator thread's hot paths. */
void runtime_log_ring_emit(RuntimeLogRing *ring, uint64_t timestamp_us,
 uint32_t emulated_cycle, RuntimeLogEventType type, uint8_t flags,
 uint32_t value0, uint32_t value1, uint32_t value2);
/* Copies the newest complete records, oldest first. */
size_t runtime_log_ring_snapshot(const RuntimeLogRing *ring,
 RuntimeLogRecord *records, size_t capacity);
/* Records overwritten since init because the ring was full, whether or not
   a flush had already saved them; stops at UINT_MAX. */
uint32_t runtime_log_ring_dropped(const RuntimeLogRing *ring);
int runtime_log_encode(const RuntimeLogRing *main_ring,
 const RuntimeLogRing *network_ring, const RuntimeLogRing *critical_ring,
 const RuntimeLogMetadata *metadata, uint8_t *output, size_t capacity,
 size_t *written);
/* Checks a snapshot's header, sizes and both CRCs; 1 if it is intact. */
int runtime_log_validate(const uint8_t *input, size_t size,
 RuntimeLogMetadata *metadata, uint16_t *main_count,
 uint16_t *network_count, uint16_t *critical_count);

#endif /* RUNTIME_LOG_H */
