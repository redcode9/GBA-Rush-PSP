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
 * Per-frame state hashes for deterministic replays: during a replay the
 * caller hashes the emulated state once per frame, region by region, into
 * a fixed array it owns; at the end it exports the array as text. Two runs
 * of the same replay must give identical text, and the first differing
 * record names the frame and the region where they split. The module never
 * allocates and never touches files.
 *
 * Hash: 32-bit FNV-1a over 32-bit words (each word XORed in whole, then
 * multiplied by the FNV prime), starting from the FNV offset basis for each
 * region; a size that is not a multiple of 4 ends with a zero-padded word.
 * Words are read in the native order, which is little-endian on the PSP and
 * on the test hosts.
 *
 * Text format "GBSH v1", ASCII, every line ends with LF:
 *
 *   GBSH v1
 *   build=<id> game=<code> rev=<n> start=<frame>
 *   regions=<name>,<name>,... (the STATE_HASH_REGION_COUNT names in order)
 *   <frame> <hash> <hash> ... (one line per record)
 *   counters <name>=<value> <name>=<value> ... (optional, always last)
 *
 * - <id> is 0-40 characters and <code> exactly 4, both from [A-Za-z0-9._-]
 *   (anything else is written as '_'); <n> and <frame> are decimal.
 * - start= is the absolute frame counter when the replay began; the frame
 *   that opens each record line is relative to it, so replays started at
 *   different absolute frames still compare.
 * - Each <hash> is 8 lowercase hex digits, one per region in the order of
 *   the regions line.
 * - The counters line holds diagnostic counters (<name> from the same
 *   character set, <value> decimal). It is absent when there are none.
 */

#ifndef STATE_HASH_H
#define STATE_HASH_H

#include <stddef.h>
#include <stdint.h>

#define STATE_HASH_MAGIC "GBSH v1"
#define STATE_HASH_REGION_COUNT 9U
#define STATE_HASH_BUILD_ID_MAX 40U
#define STATE_HASH_GAME_CODE_SIZE 4U
/* The FNV offset basis that starts each region's hash; also seeds a
   running hash that is fed to state_hash_words() piece by piece. */
#define STATE_HASH_FNV_BASIS 2166136261U

/* Region indices; the caller supplies the matching names. */
enum
{
  STATE_HASH_REGS,
  STATE_HASH_IWRAM,
  STATE_HASH_EWRAM,
  STATE_HASH_VRAM,
  STATE_HASH_OAM,
  STATE_HASH_PALETTE,
  STATE_HASH_IO,
  STATE_HASH_BACKUP,
  STATE_HASH_AUDIO
};

/* A block of memory hashed into one region; several spans may feed the
   same region, in order (a wrapping ring is two spans). */
typedef struct
{
  const void *data;
  uint32_t size;
  uint32_t region;
} StateHashSpan;

/* One frame: its index relative to the replay start and one hash per
   region. Kept at 4 + 4 * STATE_HASH_REGION_COUNT bytes. */
typedef struct
{
  uint32_t frame;
  uint32_t hash[STATE_HASH_REGION_COUNT];
} StateHashRecord;

typedef struct
{
  StateHashRecord *records;
  uint32_t capacity;
  uint32_t count;
  /* Set once a capture found no room left. */
  uint32_t full;
  /* STATE_HASH_REGION_COUNT names, kept by pointer. */
  const char *const *names;
  char build_id[STATE_HASH_BUILD_ID_MAX + 1];
  char game_code[STATE_HASH_GAME_CODE_SIZE + 1];
  uint32_t revision;
  uint32_t start_frame;
} StateHashLog;

typedef struct
{
  const char *name;
  uint32_t value;
} StateHashCounter;

uint32_t state_hash_words(uint32_t hash, const void *data, uint32_t size);
/* Feeds count 4-byte words of a ring of ring_words words to hash, from
   word first on, wrapping to the ring's start. count is capped at
   ring_words; a first outside the ring feeds nothing. */
uint32_t state_hash_ring(uint32_t hash, const void *ring, uint32_t ring_words,
 uint32_t first, uint32_t count);
/* Fails when an argument is missing or a region name is empty or uses a
   character outside [A-Za-z0-9._-]. */
int state_hash_log_init(StateHashLog *log, StateHashRecord *records,
 uint32_t capacity, const char *const *names, const char *build_id,
 const uint8_t game_code[STATE_HASH_GAME_CODE_SIZE], uint32_t revision,
 uint32_t start_frame);
/* Appends the record of absolute frame `frame`; 0 when the log is full
   (and `full` is set) or a span names no region. */
int state_hash_capture(StateHashLog *log, uint32_t frame,
 const StateHashSpan *spans, uint32_t span_count);
/* Exact length of the text (no terminating NUL); 0 when a counter name is
   not valid. */
size_t state_hash_text_size(const StateHashLog *log,
 const StateHashCounter *counters, uint32_t counter_count);
/* Writes the text, without a NUL; fails without writing anything when out
   is smaller than state_hash_text_size(). */
int state_hash_export(const StateHashLog *log,
 const StateHashCounter *counters, uint32_t counter_count, char *out,
 size_t out_size, size_t *written);
/* Reads a text back into a log made by state_hash_log_init, keeping its
   records array, capacity and names, which the regions line must match.
   The counters line must name exactly counter_count counters in the order
   of counters[], whose values it fills; it must be absent when
   counter_count is 0. On failure the log holds no records. */
int state_hash_import(const char *text, size_t size, StateHashLog *log,
 StateHashCounter *counters, uint32_t counter_count);
/* 0 when both logs hold the same records. Otherwise 1, with the relative
   frame of the first difference and its region; region is
   STATE_HASH_REGION_COUNT when the frame indices or the lengths differ. */
int state_hash_first_divergence(const StateHashLog *a,
 const StateHashLog *b, uint32_t *frame, uint32_t *region);

#endif /* STATE_HASH_H */
