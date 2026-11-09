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
 * reads the files. The record layout and the meaning of existing enum
 * values are the format; a value added at the end of an enum keeps the
 * version, and older decoders show it as a number.
 */

#ifndef RUNTIME_LOG_H
#define RUNTIME_LOG_H

#include <stddef.h>
#include <stdint.h>

#define RUNTIME_LOG_VERSION 6u
/* Version 6 grew the header to hold the build id; files of versions 1-5
   have the 64-byte one. */
#define RUNTIME_LOG_HEADER_SIZE 96u
#define RUNTIME_LOG_V5_HEADER_SIZE 64u
/* The build id field: NUL-padded ASCII, so at most 23 characters. */
#define RUNTIME_LOG_BUILD_ID_SIZE 24u
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
  RUNTIME_LOG_EVENT_LINK_BENCHMARK,
  RUNTIME_LOG_EVENT_NAME_PROBE
} RuntimeLogEventType;

/* NAME_PROBE records, written by Diagnostics for each entry of the
   name-probe folder: flags bits 7-6 are the part, bits 5-0 the chunk index
   (or, in the header part, the kind). A chunk carries 12 bytes in value0..2,
   value0's top byte first, zero-padded at the end.
   - header, kind ENTRY: entry index in directory order, d_name length,
     length of the FAT private record bytes logged;
   - NAME chunks: the raw d_name bytes;
   - FAT chunks: the raw private record, which the firmware fills in one of
     two layouts (8.3 alias at 0 and long name at 13, or a size word, the
     alias at 4 and the long name at 20), so both are covered;
   - OPEN, index 0: the sceIoOpen result with d_name, the alias at 0 and
     the alias at 4; index 1: with the long name at 13, the long name at 20,
     then 0. Each is an fd (closed at once), a PSP error or NOT_TRIED;
   - header, kind END: sceIoDopen result, entries seen, entries logged;
   - header, kind STOPPED, in END's place when sceIoDread failed: that
     error, entries seen, entries logged. */
#define RUNTIME_LOG_NAME_PROBE_HEADER 0u
#define RUNTIME_LOG_NAME_PROBE_NAME 1u
#define RUNTIME_LOG_NAME_PROBE_FAT 2u
#define RUNTIME_LOG_NAME_PROBE_OPEN 3u
#define RUNTIME_LOG_NAME_PROBE_ENTRY 0u
#define RUNTIME_LOG_NAME_PROBE_END 1u
#define RUNTIME_LOG_NAME_PROBE_STOPPED 2u
#define RUNTIME_LOG_NAME_PROBE_FLAGS(part, index) \
 ((uint8_t)(((part) << 6) | ((index) & 0x3Fu)))
#define RUNTIME_LOG_NAME_PROBE_CHUNK_SIZE 12u
/* Longest name logged from d_name or from either long-name field; at most
   23 chunks, which fits the 6-bit chunk index. */
#define RUNTIME_LOG_NAME_PROBE_MAX_NAME 255u
/* Not a PSP error (those are 0x80xxxxxx) nor an fd: the open was skipped
   because the name was empty or the path did not fit. */
#define RUNTIME_LOG_NAME_PROBE_NOT_TRIED (-1)
/* sceIoOpen results per entry, in the OPEN order above. */
#define RUNTIME_LOG_NAME_PROBE_OPENS 5u

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
  /* GBA_RUSH_BUILD_ID; empty when read from a file older than version 6. */
  char build_id[RUNTIME_LOG_BUILD_ID_SIZE];
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
/* Emits one name-probe entry (see NAME_PROBE above), all or nothing: the
   header, the d_name chunks (name is a buffer of name_size bytes, read up to
   its NUL), the FAT chunks and the open results. Returns the records
   emitted, or 0 with nothing emitted when they would exceed record_budget. */
size_t runtime_log_name_probe_entry(RuntimeLogRing *ring,
 uint64_t timestamp_us, uint32_t index, const uint8_t *name,
 size_t name_size, const uint8_t *fat_record, size_t fat_capacity,
 const int32_t open_result[RUNTIME_LOG_NAME_PROBE_OPENS],
 size_t record_budget);
/* Emits the probe's last record: STOPPED with dread_result when the last
   sceIoDread failed, otherwise END with dopen_result (dread_result is 0
   when the listing ended or never started). */
void runtime_log_name_probe_end(RuntimeLogRing *ring, uint64_t timestamp_us,
 int32_t dopen_result, int32_t dread_result, uint32_t seen,
 uint32_t logged);
int runtime_log_encode(const RuntimeLogRing *main_ring,
 const RuntimeLogRing *network_ring, const RuntimeLogRing *critical_ring,
 const RuntimeLogMetadata *metadata, uint8_t *output, size_t capacity,
 size_t *written);
/* Checks a snapshot's header, sizes and both CRCs; 1 if it is intact. */
int runtime_log_validate(const uint8_t *input, size_t size,
 RuntimeLogMetadata *metadata, uint16_t *main_count,
 uint16_t *network_count, uint16_t *critical_count);

#endif /* RUNTIME_LOG_H */
