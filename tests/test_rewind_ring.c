/*
 * Host tests for rewind_ring.c: captures give back every state bit for bit
 * when the cursor walks back and forward, a full ring keeps the newest
 * records, an oversized record leaves everything untouched, a damaged
 * record stops the cursor, unmarked pages are not recorded, the landing
 * rule and its search, wrapping at the end of the buffer, the worst case
 * against record_bound, and 100,000 random operations against a model.
 * Every large array is static: the stack is small on some hosts.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rewind_ring.h"

#define SPAN_COUNT 7u
#define STATE_WORDS 1024u
#define SNAPSHOTS 500u
#define BIG_RING_WORDS 80000u
#define HUGE_WORDS 70000u
#define GUARD_WORDS 4u
#define GUARD_VALUE 0xDEADBEEFu

/* Six spans plus the small state; one is empty. */
static const uint32_t span_words[SPAN_COUNT] =
 { 300, 0, 256, 129, 200, 64, 75 };

static uint32_t live[STATE_WORDS];
static uint32_t ref[STATE_WORDS];
static RewindSpan spans[SPAN_COUNT];
static uint8_t span0_map[1];

static uint32_t states[SNAPSHOTS + 1][STATE_WORDS];
static RewindStamp stamps[SNAPSHOTS + 1];
static uint32_t big_ring[BIG_RING_WORDS];
static uint32_t huge_live[HUGE_WORDS];
static uint32_t huge_ref[HUGE_WORDS];

static uint32_t rng_state = 0x2545F491u;

static uint32_t rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static uint32_t rng_below(uint32_t n)
{
  return rng() % n;
}

static RewindStamp make_stamp(uint32_t frame, uint32_t generation,
 uint32_t busy)
{
  RewindStamp stamp;

  stamp.frame = frame;
  stamp.save_generation = generation;
  stamp.save_busy = busy;
  return stamp;
}

static int stamp_equal(const RewindStamp *a, const RewindStamp *b)
{
  return a->frame == b->frame && a->save_generation == b->save_generation &&
   a->save_busy == b->save_busy;
}

static int ring_equal(const RewindRing *a, const RewindRing *b)
{
  return a->buf == b->buf && a->capacity == b->capacity &&
   a->head == b->head && a->tail == b->tail && a->used == b->used &&
   a->count == b->count && a->depth == b->depth &&
   a->cursor == b->cursor && a->above == b->above &&
   stamp_equal(&a->ref_stamp, &b->ref_stamp);
}

/* live and ref hold the same random state; span 0 may use span0_map. */
static void setup_state(int with_map)
{
  uint32_t offset = 0;
  uint32_t i;

  for(i = 0; i < SPAN_COUNT; i++)
  {
    spans[i].live = live + offset;
    spans[i].ref = ref + offset;
    spans[i].words = span_words[i];
    spans[i].page_map = NULL;
    offset += span_words[i];
  }
  assert(offset == STATE_WORDS);
  if(with_map)
    spans[0].page_map = span0_map;
  for(i = 0; i < STATE_WORDS; i++)
    live[i] = rng_below(3) == 0 ? 0 : rng();
  memcpy(ref, live, sizeof(ref));
}

/* Span 0 words in a page its map leaves clear must not change. */
static int word_writable(uint32_t word)
{
  if(spans[0].page_map == NULL || word >= span_words[0])
    return 1;
  return (spans[0].page_map[0] >> (word / REWIND_PAGE_WORDS)) & 1;
}

/* A few single words, short runs and bit flips; some writes keep the
   value. */
static void mutate(uint32_t changes)
{
  while(changes-- != 0)
  {
    uint32_t at = rng_below(STATE_WORDS);
    uint32_t length = rng_below(4) == 0 ? 1 + rng_below(40) : 1;
    uint32_t kind = rng_below(3);
    uint32_t i;

    for(i = at; i < at + length && i < STATE_WORDS; i++)
    {
      if(!word_writable(i))
        continue;
      if(kind == 0)
        live[i] = rng();
      else if(kind == 1)
        live[i] ^= 1u << rng_below(32);
      else
        live[i] = rng_below(2) ? 0 : live[i];
    }
  }
}

static RewindResult capture(RewindRing *ring, const RewindStamp *stamp)
{
  return rewind_ring_capture(ring, spans, SPAN_COUNT, stamp);
}

static RewindResult step_back(RewindRing *ring, RewindStamp *out)
{
  return rewind_ring_step_back(ring, spans, SPAN_COUNT, out);
}

static RewindResult step_forward(RewindRing *ring, RewindStamp *out)
{
  return rewind_ring_step_forward(ring, spans, SPAN_COUNT, out);
}

/* Captures states 1..snapshots after states[0]; stamps[k] is state k's. */
static void record_history(RewindRing *ring, uint32_t *buf,
 uint32_t capacity, uint32_t snapshots)
{
  uint32_t k;

  stamps[0] = make_stamp(100, 0, 0);
  memcpy(states[0], live, sizeof(live));
  rewind_ring_init(ring, buf, capacity, &stamps[0]);
  for(k = 1; k <= snapshots; k++)
  {
    mutate(rng_below(6));
    /* A change in the last word keeps a literal at the end of the payload
       (the damaged-record test flips it). */
    live[STATE_WORDS - 1] ^= 1u << rng_below(32);
    stamps[k] = make_stamp(100 + k * 3, rng_below(4), rng_below(2));
    assert(capture(ring, &stamps[k]) == REWIND_OK);
    assert(memcmp(ref, live, sizeof(ref)) == 0);
    assert(ring->used <= capacity);
    memcpy(states[k], live, sizeof(live));
  }
}

static void test_history_round_trip(void)
{
  RewindRing ring;
  RewindRingStats stats;
  RewindStamp out;
  uint32_t k;

  setup_state(0);
  record_history(&ring, big_ring, BIG_RING_WORDS, SNAPSHOTS);
  assert(ring.count == SNAPSHOTS);
  rewind_ring_stats(&ring, &stats);
  assert(stats.records == SNAPSHOTS);
  assert(stats.frames == SNAPSHOTS * 3);
  assert(stats.bytes == ring.used * 4);

  for(k = SNAPSHOTS; k > 0; k--)
  {
    assert(step_back(&ring, &out) == REWIND_OK);
    assert(memcmp(ref, states[k - 1], sizeof(ref)) == 0);
    assert(stamp_equal(&out, &stamps[k - 1]));
  }
  assert(step_back(&ring, &out) == REWIND_AT_OLDEST);
  assert(memcmp(ref, states[0], sizeof(ref)) == 0);
  assert(ring.depth == SNAPSHOTS);
  /* Capturing away from the start point is refused. */
  assert(capture(&ring, &stamps[0]) == REWIND_ERR_STATE);

  for(k = 1; k <= SNAPSHOTS; k++)
  {
    assert(step_forward(&ring, &out) == REWIND_OK);
    assert(memcmp(ref, states[k], sizeof(ref)) == 0);
    assert(stamp_equal(&out, &stamps[k]));
  }
  assert(step_forward(&ring, &out) == REWIND_AT_START);
  assert(ring.depth == 0 && ring.count == SNAPSHOTS);
}

static void test_small_ring_keeps_recent_records(void)
{
  static uint32_t small_ring[600];
  RewindRing ring;
  RewindRingStats stats;
  RewindStamp out;
  uint32_t kept;
  uint32_t k;

  setup_state(0);
  record_history(&ring, small_ring, 600, 200);
  kept = ring.count;
  assert(kept > 1 && kept < 200);
  rewind_ring_stats(&ring, &stats);
  assert(stats.frames == stamps[200].frame - stamps[200 - kept].frame);

  for(k = 0; k < kept; k++)
  {
    assert(step_back(&ring, &out) == REWIND_OK);
    assert(memcmp(ref, states[199 - k], sizeof(ref)) == 0);
    assert(stamp_equal(&out, &stamps[199 - k]));
  }
  assert(step_back(&ring, &out) == REWIND_AT_OLDEST);
  assert(step_back(&ring, &out) == REWIND_AT_OLDEST);
  assert(memcmp(ref, states[200 - kept], sizeof(ref)) == 0);
  assert(ring.depth == kept);
}

static void test_record_bigger_than_ring_is_refused(void)
{
  static uint32_t tiny_ring[64];
  static uint32_t ref_before[STATE_WORDS];
  static uint32_t ring_before[64];
  RewindRing ring;
  RewindRing ring_saved;
  RewindStamp stamp = make_stamp(1, 0, 0);
  RewindStamp out;
  uint32_t i;

  setup_state(0);
  memcpy(states[0], live, sizeof(live));
  rewind_ring_init(&ring, tiny_ring, 64, &stamp);
  live[5] ^= 1;
  stamp.frame = 2;
  assert(capture(&ring, &stamp) == REWIND_OK);
  assert(ring.count == 1);

  for(i = 0; i < STATE_WORDS; i++)
    live[i] = ~ref[i];
  memcpy(ref_before, ref, sizeof(ref));
  memcpy(ring_before, tiny_ring, sizeof(tiny_ring));
  ring_saved = ring;
  stamp.frame = 3;
  assert(capture(&ring, &stamp) == REWIND_ERR_TOO_BIG);
  assert(memcmp(ref, ref_before, sizeof(ref)) == 0);
  assert(memcmp(tiny_ring, ring_before, sizeof(tiny_ring)) == 0);
  assert(ring_equal(&ring, &ring_saved));

  /* Worst case above the ring, actual record within it: counted first,
     then written, dropping the old record to make room. */
  memcpy(live, ref, sizeof(live));
  for(i = 0; i < 20; i++)
    live[i * 50] ^= 0x80000000u;
  stamp.frame = 4;
  assert(rewind_ring_record_bound(spans, SPAN_COUNT) > 64);
  assert(capture(&ring, &stamp) == REWIND_OK);
  assert(ring.count == 1 && ring.used <= 64);
  assert(step_back(&ring, &out) == REWIND_OK);
  assert(memcmp(ref, ref_before, sizeof(ref)) == 0);
  assert(out.frame == 2);
  assert(step_back(&ring, &out) == REWIND_AT_OLDEST);

  /* A ring too small for any record. */
  rewind_ring_init(&ring, tiny_ring, 0, &stamp);
  ring_saved = ring;
  assert(capture(&ring, &stamp) == REWIND_ERR_TOO_BIG);
  assert(ring_equal(&ring, &ring_saved));
}

static void test_damaged_record_stops_the_cursor(void)
{
  RewindRing ring;
  RewindRingStats stats;
  RewindStamp out;
  uint32_t length;
  uint32_t k;

  setup_state(0);
  record_history(&ring, big_ring, BIG_RING_WORDS, 20);
  for(k = 0; k < 5; k++)
    assert(step_back(&ring, &out) == REWIND_OK);

  /* Flip a literal of the next record down: its last payload word. */
  length = big_ring[ring.cursor - 1];
  big_ring[ring.cursor - 2] ^= 0x10;
  assert(length > 7);
  assert(step_back(&ring, &out) == REWIND_ERR_CORRUPT);
  assert(memcmp(ref, states[15], sizeof(ref)) == 0);
  assert(ring.depth == 5 && ring.count == 5);
  assert(step_back(&ring, &out) == REWIND_AT_OLDEST);
  rewind_ring_stats(&ring, &stats);
  assert(stats.records == 5);
  assert(stats.frames == stamps[20].frame - stamps[15].frame);

  for(k = 16; k <= 20; k++)
  {
    assert(step_forward(&ring, &out) == REWIND_OK);
    assert(memcmp(ref, states[k], sizeof(ref)) == 0);
    assert(stamp_equal(&out, &stamps[k]));
  }
  assert(step_forward(&ring, &out) == REWIND_AT_START);
  live[7] ^= 1;
  assert(capture(&ring, &stamps[0]) == REWIND_OK);
  assert(ring.count == 6);

  /* A damaged header (the frame) and a damaged length word. */
  setup_state(0);
  record_history(&ring, big_ring, BIG_RING_WORDS, 3);
  length = big_ring[ring.cursor - 1];
  big_ring[ring.cursor - length] ^= 1;
  assert(step_back(&ring, &out) == REWIND_ERR_CORRUPT);
  assert(memcmp(ref, states[3], sizeof(ref)) == 0);
  assert(ring.count == 0 && ring.used == 0);
  assert(step_back(&ring, &out) == REWIND_AT_OLDEST);

  setup_state(0);
  record_history(&ring, big_ring, BIG_RING_WORDS, 3);
  big_ring[ring.cursor - 1] = 3;
  assert(step_back(&ring, &out) == REWIND_ERR_CORRUPT);
  assert(ring.count == 0);
}

/* Pages 0, 2 and 3 (partial) marked, page 1 clear. */
static void test_page_map_skips_clear_pages(void)
{
  static uint32_t map_live[1000];
  static uint32_t map_ref[1000];
  static uint32_t old_state[1000];
  static uint32_t ring_words[64];
  const uint8_t map[1] = { 0x0D };
  RewindSpan span;
  RewindRing ring;
  RewindStamp stamp = make_stamp(1, 0, 0);
  RewindStamp out;
  uint32_t i;

  for(i = 0; i < 1000; i++)
    map_live[i] = rng();
  memcpy(map_ref, map_live, sizeof(map_ref));
  memcpy(old_state, map_live, sizeof(old_state));
  span.live = map_live;
  span.ref = map_ref;
  span.words = 1000;
  span.page_map = map;
  rewind_ring_init(&ring, ring_words, 64, &stamp);

  for(i = 256; i < 512; i++)
    map_live[i] = ~map_live[i];
  map_live[10] ^= 1;
  map_live[600] ^= 2;
  map_live[900] ^= 4;
  stamp.frame = 2;
  assert(rewind_ring_capture(&ring, &span, 1, &stamp) == REWIND_OK);
  for(i = 256; i < 512; i++)
    assert(map_ref[i] == old_state[i]);
  assert(map_ref[10] == map_live[10]);
  assert(map_ref[600] == map_live[600]);
  assert(map_ref[900] == map_live[900]);
  /* Tokens (10,1) (589,1) (299,1) (99,0) and three literals: the changed
     page is not in the record at all. */
  assert(ring.used == 7 + 7);

  assert(rewind_ring_step_back(&ring, &span, 1, &out) == REWIND_OK);
  assert(memcmp(map_ref, old_state, sizeof(map_ref)) == 0);
  assert(out.frame == 1);
}

static void test_land_rule(void)
{
  RewindStamp stamp = make_stamp(10, 5, 1);

  assert(!rewind_land_ok(&stamp, 6));
  assert(rewind_land_ok(&stamp, 5));
  stamp.save_busy = 0;
  assert(rewind_land_ok(&stamp, 6));
}

/* States 0..8 captured into the big ring with stamps admitted or not at
   generation 9, then the cursor stepped back to depth. State 8 is the start
   point and always looks rejected, so only the rule admits it. */
static void build_pick_ring(RewindRing *ring, const char *admitted,
 uint32_t depth)
{
  RewindStamp out;
  uint32_t k;

  setup_state(0);
  for(k = 0; k <= 8; k++)
    stamps[k] = admitted[k] == 'y' ? make_stamp(k, 3, 0) :
     make_stamp(k, 3, 1);
  memcpy(states[0], live, sizeof(live));
  rewind_ring_init(ring, big_ring, BIG_RING_WORDS, &stamps[0]);
  for(k = 1; k <= 8; k++)
  {
    live[k] ^= 1;
    live[STATE_WORDS - 1] ^= 1u << k;
    assert(capture(ring, &stamps[k]) == REWIND_OK);
    memcpy(states[k], live, sizeof(live));
  }
  for(k = 0; k < depth; k++)
    assert(step_back(ring, &out) == REWIND_OK);
}

static void test_land_pick(void)
{
  static uint32_t ring_before[1024];
  RewindRing ring;
  RewindRing saved;
  RewindStamp out;
  uint32_t length;

  /* Index k is state k; depth d is state 8 - d. The cursor is at depth 4. */
  build_pick_ring(&ring, "nnnnnnnnn", 4);
  assert(rewind_land_pick(&ring, 9) == 0);
  assert(rewind_land_pick(&ring, 3) == 4);  /* nothing changed since */

  build_pick_ring(&ring, "nnnnynnnn", 4);
  assert(rewind_land_pick(&ring, 9) == 4);

  build_pick_ring(&ring, "nnnynnynn", 4);
  assert(rewind_land_pick(&ring, 9) == 5);

  build_pick_ring(&ring, "nnynnnynn", 4);
  assert(rewind_land_pick(&ring, 9) == 2);  /* tie: the newer */

  build_pick_ring(&ring, "ynnnnnnyn", 4);
  assert(rewind_land_pick(&ring, 9) == 1);  /* found searching forward */

  build_pick_ring(&ring, "nnnnnnnnn", 0);
  assert(rewind_land_pick(&ring, 9) == 0);

  /* A damaged record below the cursor ends the search, read-only. */
  build_pick_ring(&ring, "nnnynnnnn", 4);
  length = big_ring[ring.cursor - 1];
  big_ring[ring.cursor - 2] ^= 0x100;
  saved = ring;
  assert(ring.head <= 1024);
  memcpy(ring_before, big_ring, sizeof(ring_before));
  assert(rewind_land_pick(&ring, 9) == 0);
  assert(ring_equal(&ring, &saved));
  assert(memcmp(ring_before, big_ring, sizeof(ring_before)) == 0);
  assert(length > 7);

  /* Landing makes the picked state the start point. */
  build_pick_ring(&ring, "nnnynnnnn", 4);
  assert(rewind_land_pick(&ring, 9) == 5);
  assert(step_back(&ring, &out) == REWIND_OK);
  rewind_ring_land(&ring);
  assert(ring.count == 3 && ring.depth == 0);
  assert(stamp_equal(&ring.ref_stamp, &stamps[3]));
  assert(memcmp(ref, states[3], sizeof(ref)) == 0);
  memcpy(live, ref, sizeof(live));
  live[100] ^= 1;
  assert(capture(&ring, &stamps[8]) == REWIND_OK);
  assert(step_back(&ring, &out) == REWIND_OK);
  assert(memcmp(ref, states[3], sizeof(ref)) == 0);
  assert(stamp_equal(&out, &stamps[3]));
  assert(step_back(&ring, &out) == REWIND_OK);
  assert(memcmp(ref, states[2], sizeof(ref)) == 0);
}

static void check_guard(const uint32_t *buf, uint32_t capacity)
{
  uint32_t i;

  for(i = 0; i < GUARD_WORDS; i++)
    assert(buf[capacity + i] == GUARD_VALUE);
}

static void test_records_wrap_at_buffer_end(void)
{
  static uint32_t wrap_ring[97 + GUARD_WORDS];
  RewindRing ring;
  RewindStamp out;
  uint32_t crossed = 0;
  uint32_t k;

  for(k = 0; k < GUARD_WORDS; k++)
    wrap_ring[97 + k] = GUARD_VALUE;
  setup_state(0);
  stamps[0] = make_stamp(0, 0, 0);
  memcpy(states[0], live, sizeof(live));
  rewind_ring_init(&ring, wrap_ring, 97, &stamps[0]);
  for(k = 1; k <= 60; k++)
  {
    uint32_t head_before = ring.head;
    uint32_t changes = 1 + rng_below(6);

    /* Single words only, so every record fits the 97 words. */
    while(changes-- != 0)
      live[rng_below(STATE_WORDS)] ^= 1u << rng_below(32);
    stamps[k] = make_stamp(k, 0, 0);
    assert(capture(&ring, &stamps[k]) == REWIND_OK);
    memcpy(states[k], live, sizeof(live));
    check_guard(wrap_ring, 97);
    if(ring.head < head_before && ring.head != 0)
      crossed++;
  }
  assert(crossed > 2);
  assert(ring.count > 1);

  for(k = 0; k < ring.count; k++)
  {
    assert(step_back(&ring, &out) == REWIND_OK);
    assert(memcmp(ref, states[59 - k], sizeof(ref)) == 0);
  }
  assert(step_back(&ring, &out) == REWIND_AT_OLDEST);
  for(k = 60 - ring.count + 1; k <= 60; k++)
  {
    assert(step_forward(&ring, &out) == REWIND_OK);
    assert(memcmp(ref, states[k], sizeof(ref)) == 0);
  }
  check_guard(wrap_ring, 97);
}

static void test_worst_case_within_bound(void)
{
  RewindRing ring;
  RewindSpan huge;
  RewindStamp stamp = make_stamp(0, 0, 0);
  RewindStamp out;
  uint32_t phase;
  uint32_t i;

  /* Alternating changed and unchanged words, both phases. */
  for(phase = 0; phase < 2; phase++)
  {
    setup_state(0);
    memcpy(states[0], live, sizeof(live));
    rewind_ring_init(&ring, big_ring, BIG_RING_WORDS, &stamp);
    for(i = phase; i < STATE_WORDS; i += 2)
      live[i] ^= 0x01010101u;
    assert(capture(&ring, &stamp) == REWIND_OK);
    assert(ring.used <= rewind_ring_record_bound(spans, SPAN_COUNT));
    assert(step_back(&ring, &out) == REWIND_OK);
    assert(memcmp(ref, states[0], sizeof(ref)) == 0);
  }

  /* Runs longer than a token holds: all unchanged, then all changed. */
  for(i = 0; i < HUGE_WORDS; i++)
    huge_live[i] = rng();
  memcpy(huge_ref, huge_live, sizeof(huge_ref));
  huge.live = huge_live;
  huge.ref = huge_ref;
  huge.words = HUGE_WORDS;
  huge.page_map = NULL;
  rewind_ring_init(&ring, big_ring, BIG_RING_WORDS, &stamp);
  assert(rewind_ring_capture(&ring, &huge, 1, &stamp) == REWIND_OK);
  assert(ring.used == 7 + 2);
  for(i = 0; i < HUGE_WORDS; i++)
    huge_live[i] = ~huge_live[i];
  assert(rewind_ring_capture(&ring, &huge, 1, &stamp) == REWIND_OK);
  assert(ring.used - 9 == 7 + HUGE_WORDS + 2);
  assert(ring.used - 9 <= rewind_ring_record_bound(&huge, 1));
  assert(rewind_ring_step_back(&ring, &huge, 1, &out) == REWIND_OK);
  for(i = 0; i < HUGE_WORDS; i++)
    assert(huge_ref[i] == ~huge_live[i]);
  /* Spans laid out differently from the record are refused. */
  huge.words--;
  assert(rewind_ring_step_back(&ring, &huge, 1, &out) == REWIND_ERR_STATE);
  assert(rewind_ring_step_forward(&ring, &huge, 1, &out) ==
   REWIND_ERR_STATE);
}

/* The model of the random test: states 0..count, oldest first, in a
   circular array; sizes[slot(a)] is the record that restores state a. */
#define MODEL_MAX 512u
#define OPS_RING_MAX 4096u

static uint32_t model_states[MODEL_MAX][STATE_WORDS];
static RewindStamp model_stamps[MODEL_MAX];
static uint32_t model_sizes[MODEL_MAX];
static uint32_t model_base;
static uint32_t model_count;
static uint32_t model_depth;
static uint32_t model_used;
static uint32_t ops_ring[OPS_RING_MAX + GUARD_WORDS];

static uint32_t slot(uint32_t age)
{
  return (model_base + age) % MODEL_MAX;
}

/* Record size from a word-by-word walk, independent of the encoder. */
static uint32_t model_record_words(void)
{
  uint32_t total = 7;
  uint32_t s;

  for(s = 0; s < SPAN_COUNT; s++)
  {
    uint32_t zeros = 0;
    uint32_t literals = 0;
    int open = 0;
    uint32_t i;

    for(i = 0; i < spans[s].words; i++)
    {
      uint32_t delta = spans[s].live[i] ^ spans[s].ref[i];

      if(spans[s].page_map != NULL &&
       !((spans[s].page_map[i / 2048] >> ((i / 256) & 7)) & 1))
        delta = 0;
      if(delta == 0)
      {
        if(open)
          zeros = 0;
        open = 0;
        zeros++;
        if(zeros > 0xFFFF)
        {
          total++;
          zeros -= 0xFFFF;
        }
        continue;
      }
      if(!open || literals == 0xFFFF)
      {
        total++;
        open = 1;
        literals = 0;
      }
      zeros = 0;
      literals++;
      total++;
    }
    if(!open && zeros != 0)
      total++;
  }
  return total;
}

static uint32_t model_pick(uint32_t generation_now)
{
  uint32_t best = 0;
  uint32_t best_distance = model_depth;
  uint32_t depth;

  for(depth = 1; depth <= model_count; depth++)
  {
    uint32_t distance = depth > model_depth ? depth - model_depth :
     model_depth - depth;

    if(rewind_land_ok(&model_stamps[slot(model_count - depth)],
     generation_now) && distance < best_distance)
    {
      best = depth;
      best_distance = distance;
    }
  }
  return best;
}

static void model_start(const RewindStamp *stamp)
{
  model_base = 0;
  model_count = 0;
  model_depth = 0;
  model_used = 0;
  memcpy(model_states[0], ref, sizeof(ref));
  model_stamps[0] = *stamp;
}

static void model_check(const RewindRing *ring)
{
  assert(ring->count == model_count);
  assert(ring->depth == model_depth);
  assert(ring->used == model_used);
  assert(ring->used <= ring->capacity);
  assert(memcmp(ref, model_states[slot(model_count - model_depth)],
   sizeof(ref)) == 0);
  check_guard(ops_ring, ring->capacity);
}

static void test_random_operations(void)
{
  static const uint32_t capacities[] = { 0, 5, 13, 60, 300, 1500, 4096 };
  RewindRing ring;
  RewindRing saved;
  RewindStamp stamp = make_stamp(0, 0, 0);
  RewindStamp out;
  uint32_t capacity = OPS_RING_MAX;
  uint32_t op;
  uint32_t i;

  setup_state(1);
  span0_map[0] = 3;
  for(i = 0; i < GUARD_WORDS; i++)
    ops_ring[capacity + i] = GUARD_VALUE;
  rewind_ring_init(&ring, ops_ring, capacity, &stamp);
  model_start(&stamp);

  for(op = 0; op < 100000; op++)
  {
    uint32_t choice = rng_below(100);

    if(choice < 40)
    {
      RewindResult result;
      uint32_t size;

      if(model_depth != 0)
      {
        saved = ring;
        assert(capture(&ring, &stamp) == REWIND_ERR_STATE);
        assert(ring_equal(&ring, &saved));
        continue;
      }
      span0_map[0] = (uint8_t)rng_below(4);
      mutate(rng_below(50) == 0 ? 400 : rng_below(5));
      size = model_record_words();
      assert(size <= rewind_ring_record_bound(spans, SPAN_COUNT));
      stamp = make_stamp(stamp.frame + 1 + rng_below(6), rng_below(4),
       rng_below(2));
      saved = ring;
      result = capture(&ring, &stamp);
      if(size > capacity)
      {
        assert(result == REWIND_ERR_TOO_BIG);
        assert(ring_equal(&ring, &saved));
        /* The references kept the old state; the glue would too. */
        assert(memcmp(ref, model_states[slot(model_count)],
         sizeof(ref)) == 0);
        memcpy(live, ref, sizeof(live));
      }
      else
      {
        assert(result == REWIND_OK);
        assert(memcmp(ref, live, sizeof(ref)) == 0);
        while(model_used + size > capacity)
        {
          model_used -= model_sizes[slot(0)];
          model_base = slot(1);
          model_count--;
        }
        model_sizes[slot(model_count)] = size;
        model_count++;
        assert(model_count < MODEL_MAX);
        memcpy(model_states[slot(model_count)], live, sizeof(live));
        model_stamps[slot(model_count)] = stamp;
        model_used += size;
        assert(stamp_equal(&ring.ref_stamp, &stamp));
      }
    }
    else if(choice < 65)
    {
      RewindResult result = step_back(&ring, &out);

      if(model_depth == model_count)
        assert(result == REWIND_AT_OLDEST);
      else
      {
        assert(result == REWIND_OK);
        model_depth++;
        assert(stamp_equal(&out,
         &model_stamps[slot(model_count - model_depth)]));
      }
      memcpy(live, ref, sizeof(live));
    }
    else if(choice < 85)
    {
      RewindResult result = step_forward(&ring, &out);

      if(model_depth == 0)
        assert(result == REWIND_AT_START);
      else
      {
        assert(result == REWIND_OK);
        model_depth--;
        assert(stamp_equal(&out,
         &model_stamps[slot(model_count - model_depth)]));
      }
      memcpy(live, ref, sizeof(live));
    }
    else if(choice < 93)
    {
      rewind_ring_land(&ring);
      for(i = model_count - model_depth; i < model_count; i++)
        model_used -= model_sizes[slot(i)];
      model_count -= model_depth;
      model_depth = 0;
      assert(stamp_equal(&ring.ref_stamp,
       &model_stamps[slot(model_count)]));
      stamp = ring.ref_stamp;
    }
    else if(choice < 97)
    {
      uint32_t generation = rng_below(4);

      saved = ring;
      assert(rewind_land_pick(&ring, generation) == model_pick(generation));
      assert(ring_equal(&ring, &saved));
    }
    else
    {
      stamp = make_stamp(stamp.frame + 1, rng_below(4), rng_below(2));
      if(choice == 99)
      {
        capacity = capacities[rng_below(7)];
        for(i = 0; i < GUARD_WORDS; i++)
          ops_ring[capacity + i] = GUARD_VALUE;
        rewind_ring_init(&ring, ops_ring, capacity, &stamp);
      }
      else
        rewind_ring_reset(&ring, &stamp);
      model_start(&stamp);
    }
    model_check(&ring);
  }
}

int main(void)
{
  test_history_round_trip();
  test_small_ring_keeps_recent_records();
  test_record_bigger_than_ring_is_refused();
  test_damaged_record_stops_the_cursor();
  test_page_map_skips_clear_pages();
  test_land_rule();
  test_land_pick();
  test_records_wrap_at_buffer_end();
  test_worst_case_within_bound();
  test_random_operations();
  puts("Rewind ring tests passed");
  return 0;
}
