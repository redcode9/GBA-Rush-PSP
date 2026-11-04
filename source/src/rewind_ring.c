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

#include "rewind_ring.h"

#include <stddef.h>

/* Header words; the payload follows, then the record length. */
#define RECORD_FRAME 0u
#define RECORD_GENERATION 1u
#define RECORD_BUSY 2u
#define RECORD_PAYLOAD 3u
#define RECORD_LAYOUT 4u
#define RECORD_CHECKSUM 5u
#define RECORD_HEADER_WORDS 6u
#define RECORD_OVERHEAD (RECORD_HEADER_WORDS + 1u)

#define RUN_MAX 0xFFFFu

typedef struct
{
  RewindRing *ring;
  uint32_t pos;     /* next word to write */
  uint32_t words;   /* words of the record so far */
  uint32_t zeros;   /* zero run not yet in a token */
  uint32_t sum;
  int dry;          /* only count words: no ring or reference writes */
} RewindEncoder;

/* offset + k and offset - k around the ring, for k <= capacity. */
static uint32_t wrap_add(const RewindRing *ring, uint32_t offset, uint32_t k)
{
  uint32_t sum = offset + k;

  return sum >= ring->capacity ? sum - ring->capacity : sum;
}

static uint32_t wrap_sub(const RewindRing *ring, uint32_t offset, uint32_t k)
{
  return offset >= k ? offset - k : offset + ring->capacity - k;
}

/* Changing any single word changes the sum: the product with an odd
   constant is a bijection modulo 2^32. */
static uint32_t checksum_word(uint32_t value, uint32_t index)
{
  return (value ^ (index * 0x9E3779B9u)) * 0x85EBCA6Bu;
}

/* Catches spans laid out differently from the capture's. */
static uint32_t layout_signature(const RewindSpan *spans, uint32_t count)
{
  uint32_t signature = checksum_word(count, 0);
  uint32_t i;

  for(i = 0; i < count; i++)
    signature += checksum_word(spans[i].words, i + 1);
  return signature;
}

static uint32_t record_word(const RewindRing *ring, uint32_t start,
 uint32_t index)
{
  return ring->buf[wrap_add(ring, start, index)];
}

static void record_stamp(const RewindRing *ring, uint32_t start,
 RewindStamp *out)
{
  out->frame = record_word(ring, start, RECORD_FRAME);
  out->save_generation = record_word(ring, start, RECORD_GENERATION);
  out->save_busy = record_word(ring, start, RECORD_BUSY);
}

/* The stamp of the state the references hold. */
static void cursor_stamp(const RewindRing *ring, RewindStamp *out)
{
  if(ring->depth == 0)
    *out = ring->ref_stamp;
  else
    record_stamp(ring, ring->cursor, out);
}

/* Drops the oldest record to make room for the one being written. A
   damaged length drops every old record instead of walking off. */
static void evict_oldest(RewindEncoder *encoder)
{
  RewindRing *ring = encoder->ring;
  uint32_t old_words = ring->used - encoder->words;
  uint32_t length = ring->buf[wrap_add(ring, ring->tail, RECORD_PAYLOAD)] +
   RECORD_OVERHEAD;

  if(ring->count == 0 || length < RECORD_OVERHEAD || length > old_words)
  {
    ring->tail = wrap_sub(ring, encoder->pos, encoder->words);
    ring->used = encoder->words;
    ring->count = 0;
    return;
  }
  ring->tail = wrap_add(ring, ring->tail, length);
  ring->used -= length;
  ring->count--;
}

/* Appends a word without adding it to the checksum. */
static void encoder_put(RewindEncoder *encoder, uint32_t value)
{
  RewindRing *ring = encoder->ring;

  if(!encoder->dry)
  {
    if(ring->used == ring->capacity)
      evict_oldest(encoder);
    ring->buf[encoder->pos] = value;
    encoder->pos = wrap_add(ring, encoder->pos, 1);
    ring->used++;
  }
  encoder->words++;
}

static void encoder_word(RewindEncoder *encoder, uint32_t value)
{
  encoder->sum += checksum_word(value, encoder->words);
  encoder_put(encoder, value);
}

/* Fills a word reserved with encoder_put. */
static void encoder_patch(RewindEncoder *encoder, uint32_t pos,
 uint32_t index, uint32_t value)
{
  encoder->sum += checksum_word(value, index);
  if(!encoder->dry)
    encoder->ring->buf[pos] = value;
}

static void encoder_zeros(RewindEncoder *encoder, uint32_t words)
{
  encoder->zeros += words;
  while(encoder->zeros > RUN_MAX)
  {
    encoder_word(encoder, RUN_MAX << 16);
    encoder->zeros -= RUN_MAX;
  }
}

/* The hot loop: unchanged words are only compared, changed ones are
   written to the record and copied into the reference. */
static void encode_words(RewindEncoder *encoder, const uint32_t *live,
 uint32_t *ref, uint32_t words)
{
  uint32_t i = 0;

  while(i < words)
  {
    uint32_t token_pos;
    uint32_t token_index;
    uint32_t literals = 0;

    if(live[i] == ref[i])
    {
      uint32_t run_end = i + 1;

      while(run_end < words && live[run_end] == ref[run_end])
        run_end++;
      encoder_zeros(encoder, run_end - i);
      i = run_end;
      continue;
    }

    token_pos = encoder->pos;
    token_index = encoder->words;
    encoder_put(encoder, 0);
    while(i < words && literals < RUN_MAX)
    {
      uint32_t delta = live[i] ^ ref[i];

      if(delta == 0)
        break;
      if(!encoder->dry)
        ref[i] = live[i];
      encoder_word(encoder, delta);
      literals++;
      i++;
    }
    encoder_patch(encoder, token_pos, token_index,
     (encoder->zeros << 16) | literals);
    encoder->zeros = 0;
  }
}

static int page_marked(const uint8_t *page_map, uint32_t page)
{
  return (page_map[page >> 3] >> (page & 7)) & 1;
}

/* Tokens never cross spans. Consecutive pages with the same mark are one
   range, so a page boundary does not split a literal run. */
static void encode_spans(RewindEncoder *encoder, const RewindSpan *spans,
 uint32_t count)
{
  uint32_t i;

  for(i = 0; i < count; i++)
  {
    const RewindSpan *span = &spans[i];

    encoder->zeros = 0;
    if(span->page_map == NULL)
      encode_words(encoder, span->live, span->ref, span->words);
    else
    {
      uint32_t pages = (span->words + REWIND_PAGE_WORDS - 1) /
       REWIND_PAGE_WORDS;
      uint32_t page = 0;

      while(page < pages)
      {
        int marked = page_marked(span->page_map, page);
        uint32_t end = page + 1;
        uint32_t first = page * REWIND_PAGE_WORDS;
        uint32_t last;

        while(end < pages && page_marked(span->page_map, end) == marked)
          end++;
        last = end * REWIND_PAGE_WORDS;
        if(last > span->words)
          last = span->words;
        if(marked)
          encode_words(encoder, span->live + first, span->ref + first,
           last - first);
        else
          encoder_zeros(encoder, last - first);
        page = end;
      }
    }
    if(encoder->zeros != 0)
      encoder_word(encoder, encoder->zeros << 16);
  }
}

/* Length and checksum, reading only. length is at least RECORD_OVERHEAD
   and fits the ring. */
static int record_ok(const RewindRing *ring, uint32_t start, uint32_t length)
{
  uint32_t payload = length - RECORD_OVERHEAD;
  uint32_t sum = 0;
  uint32_t pos;
  uint32_t i;

  if(record_word(ring, start, RECORD_PAYLOAD) != payload)
    return 0;
  for(i = 0; i < RECORD_CHECKSUM; i++)
    sum += checksum_word(record_word(ring, start, i), i);
  pos = wrap_add(ring, start, RECORD_HEADER_WORDS);
  for(i = RECORD_HEADER_WORDS; i < RECORD_HEADER_WORDS + payload; i++)
  {
    sum += checksum_word(ring->buf[pos], i);
    pos = wrap_add(ring, pos, 1);
  }
  return sum == record_word(ring, start, RECORD_CHECKSUM);
}

/* The tokens cover every span exactly and stay inside the payload, so
   applying the record cannot write outside the references. */
static int record_shape_ok(const RewindRing *ring, uint32_t start,
 const RewindSpan *spans, uint32_t count)
{
  uint32_t left = record_word(ring, start, RECORD_PAYLOAD);
  uint32_t pos = wrap_add(ring, start, RECORD_HEADER_WORDS);
  uint32_t i;

  for(i = 0; i < count; i++)
  {
    uint32_t remaining = spans[i].words;

    while(remaining != 0)
    {
      uint32_t token;
      uint32_t covered;
      uint32_t literals;

      if(left == 0)
        return 0;
      token = ring->buf[pos];
      literals = token & RUN_MAX;
      covered = (token >> 16) + literals;
      if(covered == 0 || covered > remaining || literals >= left)
        return 0;
      left -= literals + 1;
      pos = wrap_add(ring, pos, literals + 1);
      remaining -= covered;
    }
  }
  return left == 0;
}

static void record_apply(const RewindRing *ring, uint32_t start,
 const RewindSpan *spans, uint32_t count)
{
  uint32_t pos = wrap_add(ring, start, RECORD_HEADER_WORDS);
  uint32_t i;

  for(i = 0; i < count; i++)
  {
    uint32_t *ref = spans[i].ref;
    uint32_t word = 0;

    while(word < spans[i].words)
    {
      uint32_t token = ring->buf[pos];
      uint32_t literals = token & RUN_MAX;

      pos = wrap_add(ring, pos, 1);
      word += token >> 16;
      while(literals-- != 0)
      {
        ref[word++] ^= ring->buf[pos];
        pos = wrap_add(ring, pos, 1);
      }
    }
  }
}

/* Length of the record ending at end, or 0 if it cannot be one of the
   below words under it. */
static uint32_t record_length_before(const RewindRing *ring, uint32_t end,
 uint32_t below)
{
  uint32_t length = ring->buf[wrap_sub(ring, end, 1)];

  if(length < RECORD_OVERHEAD || length > below)
    return 0;
  return length;
}

void rewind_ring_init(RewindRing *ring, uint32_t *buf,
 uint32_t capacity_words, const RewindStamp *start)
{
  ring->buf = buf;
  ring->capacity = capacity_words;
  rewind_ring_reset(ring, start);
}

void rewind_ring_reset(RewindRing *ring, const RewindStamp *start)
{
  ring->head = 0;
  ring->tail = 0;
  ring->used = 0;
  ring->count = 0;
  ring->depth = 0;
  ring->cursor = 0;
  ring->above = 0;
  ring->ref_stamp = *start;
}

/* Every nonzero word costs one literal; a token header follows a zero word,
   a full literal run or a full zero run, plus one at each end. */
uint32_t rewind_ring_record_bound(const RewindSpan *spans, uint32_t count)
{
  uint32_t bound = RECORD_OVERHEAD;
  uint32_t i;

  for(i = 0; i < count; i++)
    bound += spans[i].words + 2 + spans[i].words / RUN_MAX;
  return bound;
}

RewindResult rewind_ring_capture(RewindRing *ring, const RewindSpan *spans,
 uint32_t count, const RewindStamp *stamp)
{
  RewindEncoder encoder;
  uint32_t start = ring->head;
  uint32_t payload;
  uint32_t i;

  if(ring->depth != 0)
    return REWIND_ERR_STATE;

  encoder.ring = ring;
  encoder.pos = start;
  encoder.zeros = 0;
  encoder.sum = 0;

  /* Only a ring smaller than the worst case pays for a counting pass. */
  if(rewind_ring_record_bound(spans, count) > ring->capacity)
  {
    encoder.words = RECORD_HEADER_WORDS;
    encoder.dry = 1;
    encode_spans(&encoder, spans, count);
    if(encoder.words + 1 > ring->capacity)
      return REWIND_ERR_TOO_BIG;
    encoder.sum = 0;
  }

  /* Header words are reserved now and filled once the payload is known. */
  encoder.words = 0;
  encoder.dry = 0;
  for(i = 0; i < RECORD_HEADER_WORDS; i++)
    encoder_put(&encoder, 0);
  encode_spans(&encoder, spans, count);
  payload = encoder.words - RECORD_HEADER_WORDS;
  encoder_put(&encoder, payload + RECORD_OVERHEAD);

  /* The record restores the previous start point, so it carries its stamp. */
  encoder_patch(&encoder, wrap_add(ring, start, RECORD_FRAME), RECORD_FRAME,
   ring->ref_stamp.frame);
  encoder_patch(&encoder, wrap_add(ring, start, RECORD_GENERATION),
   RECORD_GENERATION, ring->ref_stamp.save_generation);
  encoder_patch(&encoder, wrap_add(ring, start, RECORD_BUSY), RECORD_BUSY,
   ring->ref_stamp.save_busy);
  encoder_patch(&encoder, wrap_add(ring, start, RECORD_PAYLOAD),
   RECORD_PAYLOAD, payload);
  encoder_patch(&encoder, wrap_add(ring, start, RECORD_LAYOUT),
   RECORD_LAYOUT, layout_signature(spans, count));
  ring->buf[wrap_add(ring, start, RECORD_CHECKSUM)] = encoder.sum;

  ring->head = encoder.pos;
  ring->cursor = ring->head;
  ring->count++;
  ring->ref_stamp = *stamp;
  return REWIND_OK;
}

/* Drops the bad record and everything older. What is above the cursor was
   checked when the cursor passed it, so it stays reachable. */
static RewindResult drop_below_cursor(RewindRing *ring)
{
  ring->tail = ring->cursor;
  ring->used = ring->above;
  ring->count = ring->depth;
  return REWIND_ERR_CORRUPT;
}

RewindResult rewind_ring_step_back(RewindRing *ring,
 const RewindSpan *spans, uint32_t count, RewindStamp *out)
{
  uint32_t length;
  uint32_t start;

  if(ring->depth == ring->count)
    return REWIND_AT_OLDEST;

  length = record_length_before(ring, ring->cursor, ring->used - ring->above);
  if(length == 0)
    return drop_below_cursor(ring);
  start = wrap_sub(ring, ring->cursor, length);
  if(!record_ok(ring, start, length))
    return drop_below_cursor(ring);
  if(record_word(ring, start, RECORD_LAYOUT) !=
   layout_signature(spans, count))
    return REWIND_ERR_STATE;
  if(!record_shape_ok(ring, start, spans, count))
    return drop_below_cursor(ring);

  record_apply(ring, start, spans, count);
  ring->cursor = start;
  ring->depth++;
  ring->above += length;
  record_stamp(ring, start, out);
  return REWIND_OK;
}

RewindResult rewind_ring_step_forward(RewindRing *ring,
 const RewindSpan *spans, uint32_t count, RewindStamp *out)
{
  uint32_t length;

  if(ring->depth == 0)
    return REWIND_AT_START;
  if(record_word(ring, ring->cursor, RECORD_LAYOUT) !=
   layout_signature(spans, count))
    return REWIND_ERR_STATE;

  length = record_word(ring, ring->cursor, RECORD_PAYLOAD) + RECORD_OVERHEAD;
  record_apply(ring, ring->cursor, spans, count);
  ring->cursor = wrap_add(ring, ring->cursor, length);
  ring->depth--;
  ring->above -= length;
  cursor_stamp(ring, out);
  return REWIND_OK;
}

void rewind_ring_land(RewindRing *ring)
{
  if(ring->depth == 0)
    return;
  cursor_stamp(ring, &ring->ref_stamp);
  ring->head = ring->cursor;
  ring->used -= ring->above;
  ring->count -= ring->depth;
  ring->depth = 0;
  ring->above = 0;
}

int rewind_land_ok(const RewindStamp *stamp, uint32_t generation_now)
{
  return stamp->save_busy == 0 || stamp->save_generation == generation_now;
}

uint32_t rewind_land_pick(const RewindRing *ring, uint32_t generation_now)
{
  RewindStamp stamp;
  uint32_t newer = ring->depth;
  uint32_t pos = ring->cursor;
  uint32_t below = ring->used - ring->above;
  uint32_t older;

  /* Toward the start point (records already checked); depth 0 always
     qualifies. */
  while(newer != 0)
  {
    record_stamp(ring, pos, &stamp);
    if(rewind_land_ok(&stamp, generation_now))
      break;
    pos = wrap_add(ring, pos,
     record_word(ring, pos, RECORD_PAYLOAD) + RECORD_OVERHEAD);
    newer--;
  }

  /* Away from it, only while strictly nearer: a tie goes to the newer. */
  pos = ring->cursor;
  for(older = ring->depth + 1; older <= ring->count &&
   older - ring->depth < ring->depth - newer; older++)
  {
    uint32_t length = record_length_before(ring, pos, below);

    pos = wrap_sub(ring, pos, length);
    if(length == 0 || !record_ok(ring, pos, length))
      break;
    record_stamp(ring, pos, &stamp);
    if(rewind_land_ok(&stamp, generation_now))
      return older;
    below -= length;
  }
  return newer;
}

void rewind_ring_stats(const RewindRing *ring, RewindRingStats *out)
{
  out->records = ring->count;
  out->frames = 0;
  if(ring->count != 0)
    out->frames = ring->ref_stamp.frame -
     record_word(ring, ring->tail, RECORD_FRAME);
  out->bytes = ring->used * 4;
}
