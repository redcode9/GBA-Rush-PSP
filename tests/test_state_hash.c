/*
 * Host tests for state_hash.c, the per-frame state hashes of deterministic
 * replays: FNV-1a over 32-bit words per named region, records in a fixed
 * caller array, the "GBSH v1" text and the first divergence of two logs.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "state_hash.h"

#define BASIS "811c9dc5"
#define RECORD_COUNT 200U

static const char *const region_names[STATE_HASH_REGION_COUNT] =
 { "regs", "iwram", "ewram", "vram", "oam", "palette", "io", "backup",
   "audio" };
static const uint8_t game_code[STATE_HASH_GAME_CODE_SIZE] =
 { 'B', 'P', 'R', 0x01 };

/* Two frames: the first hashes nothing, the second hashes the word 1 into
   regs and the bytes 1-6 (a word and a padded tail) into audio. */
static const char two_frame_text[] =
 "GBSH v1\n"
 "build=abc_def game=BPR_ rev=1 start=1000\n"
 "regions=regs,iwram,ewram,vram,oam,palette,io,backup,audio\n"
 "0 " BASIS " " BASIS " " BASIS " " BASIS " " BASIS " " BASIS " " BASIS
 " " BASIS " " BASIS "\n"
 "1 040c5b8c " BASIS " " BASIS " " BASIS " " BASIS " " BASIS " " BASIS
 " " BASIS " 00ad5cab\n"
 "counters late=3 skipped=0\n";

static uint8_t memory[STATE_HASH_REGION_COUNT][64];
static StateHashRecord records_a[RECORD_COUNT];
static StateHashRecord records_b[RECORD_COUNT];

static void fill_memory(void)
{
  uint32_t region;
  uint32_t i;

  for(region = 0; region < STATE_HASH_REGION_COUNT; region++)
  {
    for(i = 0; i < sizeof(memory[region]); i++)
      memory[region][i] = (uint8_t)(region * 31U + i * 7U);
  }
}

/* One span per region, sizes 64 down to 56 so some end in a tail. */
static int capture_all(StateHashLog *log, uint32_t frame)
{
  StateHashSpan spans[STATE_HASH_REGION_COUNT];
  uint32_t region;

  for(region = 0; region < STATE_HASH_REGION_COUNT; region++)
  {
    spans[region].data = memory[region];
    spans[region].size = 64U - region;
    spans[region].region = region;
  }
  return state_hash_capture(log, frame, spans, STATE_HASH_REGION_COUNT);
}

static void make_log(StateHashLog *log, StateHashRecord *records,
 uint32_t capacity, uint32_t start_frame)
{
  assert(state_hash_log_init(log, records, capacity, region_names,
   "abc+def", game_code, 1, start_frame));
}

static void make_two_frame_log(StateHashLog *log, StateHashRecord *records)
{
  static const uint8_t regs[4] = { 1, 0, 0, 0 };
  static const uint8_t audio[6] = { 1, 2, 3, 4, 5, 6 };
  StateHashSpan spans[2];

  spans[0].data = regs;
  spans[0].size = sizeof(regs);
  spans[0].region = STATE_HASH_REGS;
  spans[1].data = audio;
  spans[1].size = sizeof(audio);
  spans[1].region = STATE_HASH_AUDIO;
  make_log(log, records, 2, 1000);
  assert(state_hash_capture(log, 1000, NULL, 0));
  assert(state_hash_capture(log, 1001, spans, 2));
}

static void test_determinism_and_regions(void)
{
  StateHashLog log;
  uint32_t region;
  uint32_t other;

  fill_memory();
  make_log(&log, records_a, RECORD_COUNT, 0);
  assert(capture_all(&log, 0));
  assert(capture_all(&log, 1));
  assert(log.count == 2);
  assert(memcmp(records_a[0].hash, records_a[1].hash,
   sizeof(records_a[0].hash)) == 0);
  assert(records_a[1].frame == 1);

  /* One flipped bit changes its own region's hash and no other. */
  for(region = 0; region < STATE_HASH_REGION_COUNT; region++)
  {
    memory[region][region + 3U] ^= 0x10;
    assert(capture_all(&log, 2 + region));
    memory[region][region + 3U] ^= 0x10;
    for(other = 0; other < STATE_HASH_REGION_COUNT; other++)
    {
      if(other == region)
        assert(records_a[log.count - 1].hash[other] != records_a[0].hash[other]);
      else
        assert(records_a[log.count - 1].hash[other] == records_a[0].hash[other]);
    }
  }
}

static void test_split_spans(void)
{
  StateHashSpan split[2];
  StateHashSpan whole;
  StateHashRecord records[2];
  StateHashLog log;

  fill_memory();
  /* A wrapped ring read as two spans hashes like one contiguous block. */
  split[0].data = memory[STATE_HASH_AUDIO];
  split[0].size = 24;
  split[0].region = STATE_HASH_AUDIO;
  split[1].data = memory[STATE_HASH_AUDIO] + 24;
  split[1].size = 40;
  split[1].region = STATE_HASH_AUDIO;
  whole.data = memory[STATE_HASH_AUDIO];
  whole.size = 64;
  whole.region = STATE_HASH_AUDIO;
  make_log(&log, records, 2, 0);
  assert(state_hash_capture(&log, 0, split, 2));
  assert(state_hash_capture(&log, 1, &whole, 1));
  assert(memcmp(records[0].hash, records[1].hash,
   sizeof(records[0].hash)) == 0);
  assert(records[0].hash[STATE_HASH_AUDIO] != 2166136261U);
  /* Nothing to hash leaves the basis; an unaligned start reads the same. */
  assert(state_hash_words(2166136261U, NULL, 0) == 2166136261U);
  memcpy(memory[0] + 1, memory[1], 8);
  assert(state_hash_words(7, memory[0] + 1, 8) ==
   state_hash_words(7, memory[1], 8));
}

static void test_capacity_and_arguments(void)
{
  static const char *const bad_names[STATE_HASH_REGION_COUNT] =
   { "regs", "iwram", "ewram", "vram", "oam", "pal ette", "io", "backup",
     "audio" };
  StateHashSpan bad;
  StateHashRecord records[2];
  StateHashLog log;

  fill_memory();
  make_log(&log, records, 2, 0);
  bad.data = memory[0];
  bad.size = 4;
  bad.region = STATE_HASH_REGION_COUNT;
  assert(!state_hash_capture(&log, 0, &bad, 1));
  assert(log.count == 0 && !log.full);
  assert(capture_all(&log, 0));
  assert(capture_all(&log, 1));
  assert(!log.full);
  assert(!capture_all(&log, 2));
  assert(log.full && log.count == 2);

  assert(!state_hash_log_init(&log, records, 2, bad_names, "", game_code, 0,
   0));
  assert(!state_hash_log_init(&log, NULL, 2, region_names, "", game_code, 0,
   0));
  assert(!state_hash_log_init(&log, records, 2, region_names, "", NULL, 0,
   0));
  /* The build id keeps its first 40 characters. */
  assert(state_hash_log_init(&log, records, 2, region_names,
   "0123456789012345678901234567890123456789XYZ", game_code, 0, 0));
  assert(strlen(log.build_id) == STATE_HASH_BUILD_ID_MAX);
  assert(log.build_id[39] == '9');
}

static void test_exact_text(void)
{
  char text[sizeof(two_frame_text)];
  char small[sizeof(two_frame_text)];
  const StateHashCounter counters[2] = { { "late", 3 }, { "skipped", 0 } };
  const StateHashCounter bad_counter = { "late=1", 3 };
  StateHashRecord records[2];
  StateHashLog log;
  size_t length = strlen(two_frame_text);
  size_t written = 0;
  size_t i;

  make_two_frame_log(&log, records);
  assert(state_hash_text_size(&log, counters, 2) == length);
  assert(state_hash_export(&log, counters, 2, text, length, &written));
  assert(written == length && memcmp(text, two_frame_text, length) == 0);

  /* One byte short: nothing written. */
  written = 1;
  memset(small, 'Z', sizeof(small));
  assert(!state_hash_export(&log, counters, 2, small, length - 1, &written));
  assert(written == 0);
  for(i = 0; i < sizeof(small); i++)
    assert(small[i] == 'Z');
  assert(state_hash_text_size(&log, &bad_counter, 1) == 0);
  assert(!state_hash_export(&log, &bad_counter, 1, small, sizeof(small),
   &written));
}

static void test_round_trip(void)
{
  StateHashCounter counters[3] = { { "frames", 4294967295U },
   { "late", 0 }, { "audio.under-run_count", 17 } };
  StateHashCounter read[3] = { { "frames", 0 }, { "late", 5 },
   { "audio.under-run_count", 0 } };
  static char text[RECORD_COUNT * 100U + 512U];
  static char again[sizeof(text)];
  StateHashLog log;
  StateHashLog copy;
  size_t written = 0;
  size_t written_again = 0;
  uint32_t i;

  fill_memory();
  make_log(&log, records_a, RECORD_COUNT, 4294967000U);
  for(i = 0; i < 50; i++)
  {
    memory[STATE_HASH_IWRAM][0] = (uint8_t)i;
    assert(capture_all(&log, 4294967000U + i * 2U));
  }
  assert(state_hash_export(&log, counters, 3, text, sizeof(text), &written));

  assert(state_hash_log_init(&copy, records_b, RECORD_COUNT, region_names,
   "", (const uint8_t *)"XXXX", 0, 0));
  assert(state_hash_import(text, written, &copy, read, 3));
  assert(strcmp(copy.build_id, "abc_def") == 0);
  assert(strcmp(copy.game_code, "BPR_") == 0);
  assert(copy.revision == 1 && copy.start_frame == 4294967000U);
  assert(copy.count == 50 && records_b[49].frame == 98);
  assert(memcmp(records_a, records_b, 50 * sizeof(records_a[0])) == 0);
  for(i = 0; i < 3; i++)
    assert(read[i].value == counters[i].value);
  assert(state_hash_export(&copy, read, 3, again, sizeof(again),
   &written_again));
  assert(written_again == written && memcmp(text, again, written) == 0);

  /* No counters: no counters line, and a reader expecting none. */
  assert(state_hash_export(&log, NULL, 0, text, sizeof(text),
   &written_again));
  assert(written_again == written -
   strlen("counters frames=4294967295 late=0 audio.under-run_count=17\n"));
  assert(state_hash_import(text, written_again, &copy, NULL, 0));
  assert(copy.count == 50);
}

static void expect_rejected(const char *from, const char *to,
 uint32_t capacity)
{
  StateHashCounter read[2] = { { "late", 0 }, { "skipped", 0 } };
  StateHashRecord records[2];
  StateHashLog log;
  char text[sizeof(two_frame_text) + 64];
  size_t length = strlen(two_frame_text);
  size_t from_length = strlen(from);
  size_t to_length = strlen(to);
  size_t at = 0;

  while(memcmp(two_frame_text + at, from, from_length) != 0)
  {
    at++;
    assert(at + from_length <= length);
  }
  memcpy(text, two_frame_text, at);
  memcpy(text + at, to, to_length);
  memcpy(text + at + to_length, two_frame_text + at + from_length,
   length - at - from_length);
  make_log(&log, records, capacity, 0);
  assert(capture_all(&log, 0));
  assert(!state_hash_import(text, length - from_length + to_length, &log,
   read, 2));
  assert(log.count == 0);
}

static void test_malformed_text(void)
{
  StateHashCounter read[2] = { { "late", 0 }, { "skipped", 0 } };
  StateHashRecord records[2];
  StateHashLog log;
  size_t length = strlen(two_frame_text);
  size_t cut;

  make_log(&log, records, 2, 0);
  assert(state_hash_import(two_frame_text, length, &log, read, 2));
  assert(log.count == 2 && read[0].value == 3);
  /* Cut short anywhere, the text is refused; a reader expecting no
     counters refuses the counters line. */
  for(cut = 0; cut < length; cut++)
    assert(!state_hash_import(two_frame_text, cut, &log, read, 2));
  assert(!state_hash_import(two_frame_text, length, &log, NULL, 0));
  assert(log.count == 0);

  expect_rejected("GBSH v1", "GBSH v2", 2);
  expect_rejected("GBSH v1\n", "GBSH v1\r\n", 2);
  expect_rejected("040c5b8c", "040C5B8C", 2);
  expect_rejected("040c5b8c", "040c5b8g", 2);
  expect_rejected("040c5b8c", "040c5b8", 2);
  expect_rejected("040c5b8c", "040c5b8c0", 2);
  expect_rejected(" 00ad5cab\n", "\n", 2);
  expect_rejected(" 00ad5cab\n", " 00ad5cab " BASIS "\n", 2);
  expect_rejected("1 040c5b8c", "4294967296 040c5b8c", 2);
  expect_rejected("game=BPR_", "game=BPR", 2);
  expect_rejected("game=BPR_", "game=BPR_X", 2);
  expect_rejected("build=abc_def", "build=abc def", 2);
  expect_rejected("rev=1", "rev=", 2);
  expect_rejected("start=1000", "start=-1000", 2);
  expect_rejected("regions=regs,", "regions=reg,", 2);
  expect_rejected(",audio\n", ",audio,more\n", 2);
  expect_rejected("late=3", "lates=3", 2);
  expect_rejected(" skipped=0", "", 2);
  expect_rejected("counters late=3 skipped=0\n", "", 2);
  expect_rejected("skipped=0\n", "skipped=0\nextra\n", 2);
  /* More records than the log holds. */
  expect_rejected("GBSH", "GBSH", 1);
}

static void test_first_divergence(void)
{
  StateHashLog a;
  StateHashLog b;
  uint32_t frame = 0;
  uint32_t region = 0;
  uint32_t i;

  fill_memory();
  make_log(&a, records_a, RECORD_COUNT, 0);
  make_log(&b, records_b, RECORD_COUNT, 5000);
  for(i = 0; i < RECORD_COUNT; i++)
  {
    memory[STATE_HASH_EWRAM][5] = (uint8_t)i;
    assert(capture_all(&a, i));
    assert(capture_all(&b, 5000 + i));
  }
  /* Different absolute starts, same relative frames: identical. */
  assert(state_hash_first_divergence(&a, &b, &frame, &region) == 0);

  records_b[120].hash[STATE_HASH_VRAM] ^= 1;
  records_b[150].hash[STATE_HASH_REGS] ^= 1;
  assert(state_hash_first_divergence(&a, &b, &frame, &region) == 1);
  assert(frame == 120 && region == STATE_HASH_VRAM);
  records_b[120].hash[STATE_HASH_VRAM] ^= 1;
  records_b[150].hash[STATE_HASH_REGS] ^= 1;

  records_a[37].hash[STATE_HASH_AUDIO] += 1;
  assert(state_hash_first_divergence(&a, &b, &frame, &region) == 1);
  assert(frame == 37 && region == STATE_HASH_AUDIO);
  records_a[37].hash[STATE_HASH_AUDIO] -= 1;

  b.count = 150;
  assert(state_hash_first_divergence(&a, &b, &frame, &region) == 1);
  assert(frame == 150 && region == STATE_HASH_REGION_COUNT);
  assert(state_hash_first_divergence(&b, &a, &frame, &region) == 1);
  assert(frame == 150 && region == STATE_HASH_REGION_COUNT);
  b.count = RECORD_COUNT;

  /* A skipped frame shows as a frame mismatch at the earlier index. */
  records_b[60].frame = 61;
  assert(state_hash_first_divergence(&a, &b, &frame, &region) == 1);
  assert(frame == 60 && region == STATE_HASH_REGION_COUNT);
}

/* Runs every hashing, text and divergence case; a failing assert aborts the
   run. */
int main(void)
{
  test_determinism_and_regions();
  test_split_spans();
  test_capacity_and_arguments();
  test_exact_text();
  test_round_trip();
  test_malformed_text();
  test_first_divergence();
  puts("State hash tests passed");
  return 0;
}
