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
 * Rewind history, free of PSP calls so the host tests can drive it
 * (tests/test_rewind_ring.c). The caller hands over spans: live memory, a
 * reference copy of it and, optionally, a map of the pages that may have
 * changed. A capture writes the backward XOR delta (old ^ new) of every
 * span, with runs of zero words compressed, into a ring of words the caller
 * owns, and copies live into the reference on the way. The ring drops its
 * oldest records when it needs room and never allocates.
 *
 * A cursor walks the history without consuming it: stepping back XORs the
 * record under the cursor into the references, stepping forward XORs it
 * again (XOR undoes itself), and landing drops the records above the
 * cursor. Copying the references back into live memory is the caller's job.
 * Depth 0 is the start point, the newest state.
 *
 * Record layout, in words, wrapping at the end of the buffer:
 *   frame, save generation, save busy  stamp of the state it restores
 *   payload length, span layout, checksum
 *   payload: tokens of (zero run << 16 | literal count) + the literals
 *   record length                      lets step back walk to older ones
 */

#ifndef REWIND_RING_H
#define REWIND_RING_H

#include <stdint.h>

/* One bit of a page map covers this many words (1 KiB). */
#define REWIND_PAGE_WORDS 256u

typedef enum
{
  REWIND_OK = 0,
  REWIND_AT_OLDEST = 1,      /* step back: no older record */
  REWIND_AT_START = 2,       /* step forward: already at the start point */
  REWIND_ERR_TOO_BIG = -1,   /* capture: the record would not fit the ring */
  REWIND_ERR_STATE = -2,     /* capture away from the start point, or spans
                                laid out unlike the record's */
  REWIND_ERR_CORRUPT = -3    /* step back: bad record, it and older dropped */
} RewindResult;

typedef struct
{
  const uint32_t *live;
  uint32_t *ref;
  uint32_t words;
  /* NULL: read every word. Otherwise bit p (byte p / 8, bit p % 8) set
     means page p may have changed; a clear page is not read and is
     recorded as unchanged. */
  const uint8_t *page_map;
} RewindSpan;

/* What landing on a state needs to know about the save memory. */
typedef struct
{
  uint32_t frame;
  uint32_t save_generation;  /* bumped whenever a save byte changes */
  uint32_t save_busy;        /* nonzero: a save access was in progress */
} RewindStamp;

/* Offsets and sizes are in words. Read the fields, change them only
   through the functions below. */
typedef struct
{
  uint32_t *buf;
  uint32_t capacity;
  uint32_t head;         /* where the next record starts */
  uint32_t tail;         /* first word of the oldest record */
  uint32_t used;
  uint32_t count;        /* records */
  uint32_t depth;        /* records stepped back over; 0 = start point */
  uint32_t cursor;       /* end of the next record to step back over */
  uint32_t above;        /* words of the records above the cursor */
  RewindStamp ref_stamp; /* stamp of the start point */
} RewindRing;

typedef struct
{
  uint32_t records;
  uint32_t frames;       /* from the oldest state to the start point */
  uint32_t bytes;
} RewindRingStats;

/* The references must hold the state stamped start. */
void rewind_ring_init(RewindRing *ring, uint32_t *buf,
 uint32_t capacity_words, const RewindStamp *start);
void rewind_ring_reset(RewindRing *ring, const RewindStamp *start);
/* Largest record, in words, a capture of these spans can write. */
uint32_t rewind_ring_record_bound(const RewindSpan *spans, uint32_t count);
/* Records the step from the references to live, stamped stamp. TOO_BIG
   leaves the ring and the references untouched; STATE away from the start
   point (land first). */
RewindResult rewind_ring_capture(RewindRing *ring, const RewindSpan *spans,
 uint32_t count, const RewindStamp *stamp);
/* On OK *out is the stamp of the state now in the references. */
RewindResult rewind_ring_step_back(RewindRing *ring,
 const RewindSpan *spans, uint32_t count, RewindStamp *out);
RewindResult rewind_ring_step_forward(RewindRing *ring,
 const RewindSpan *spans, uint32_t count, RewindStamp *out);
/* Makes the state under the cursor the start point. */
void rewind_ring_land(RewindRing *ring);
/* A state may be landed on unless a save access was in progress when it was
   captured and the save has changed since. */
int rewind_land_ok(const RewindStamp *stamp, uint32_t generation_now);
/* Depth of the admitted state nearest the cursor, searching both ways; on a
   tie the newer one. The start point is always admitted. Records below the
   cursor are checked read-only and the search stops at a bad one. */
uint32_t rewind_land_pick(const RewindRing *ring, uint32_t generation_now);
void rewind_ring_stats(const RewindRing *ring, RewindRingStats *out);

#endif /* REWIND_RING_H */
