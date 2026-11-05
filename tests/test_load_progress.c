/*
 * Host tests for load_progress.c, the numbers behind the ROM loading
 * popup: redraw pacing, bar length and the "x / y KiB" line.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "load_progress.h"

#define KIB 1024u
#define MIB (1024u * 1024u)

/* A 16 MiB ROM read in 64 KiB chunks: the first chunk draws, then one
   redraw per 50 ms, and the last chunk draws whatever the time. */
static void test_redraws_are_paced(void)
{
  LoadProgressState state = { 0, 0, 0 };
  uint64_t t = 1000;

  assert(load_progress_due(&state, t, 64 * KIB, 16 * MIB));
  assert(!load_progress_due(&state, t + 49999, 128 * KIB, 16 * MIB));
  assert(load_progress_due(&state, t + 50000, 192 * KIB, 16 * MIB));
  /* The interval runs from the last redraw, not from the last call. */
  assert(!load_progress_due(&state, t + 50001, 256 * KIB, 16 * MIB));
  assert(!load_progress_due(&state, t + 99999, 320 * KIB, 16 * MIB));
  assert(load_progress_due(&state, t + 100000, 384 * KIB, 16 * MIB));
  assert(load_progress_due(&state, t + 100001, 16 * MIB, 16 * MIB));
}

/* The first call of a load draws even right after the last redraw: done
   going back to (or staying at) an earlier value starts a new load. */
static void test_new_load_draws_at_once(void)
{
  LoadProgressState state = { 0, 0, 0 };

  /* The very first call, at time 0. */
  assert(load_progress_due(&state, 0, 64 * KIB, 8 * MIB));
  assert(!load_progress_due(&state, 10, 128 * KIB, 8 * MIB));
  assert(load_progress_due(&state, 20, 64 * KIB, 4 * MIB));
  assert(!load_progress_due(&state, 30, 128 * KIB, 4 * MIB));
  assert(load_progress_due(&state, 40, 128 * KIB, 4 * MIB));
  /* A single-chunk ROM: its only call is also the last. */
  assert(load_progress_due(&state, 50, 32 * KIB, 32 * KIB));
}

static void test_bar_length(void)
{
  assert(load_progress_bar(0, 16 * MIB, 200) == 0);
  assert(load_progress_bar(8 * MIB, 16 * MIB, 200) == 100);
  assert(load_progress_bar(16 * MIB - 1, 16 * MIB, 200) == 199);
  assert(load_progress_bar(16 * MIB, 16 * MIB, 200) == 200);
  /* done * 200 no longer fits 32 bits from about 20.5 MiB on. */
  assert(load_progress_bar(24 * MIB, 32 * MIB, 200) == 150);
  assert(load_progress_bar(32 * MIB - 1, 32 * MIB, 200) == 199);
  assert(load_progress_bar(UINT32_MAX - 1, UINT32_MAX, 200) == 199);
  assert(load_progress_bar(UINT32_MAX, UINT32_MAX, 200) == 200);
  /* Clamped to the bar, and an unknown size draws nothing. */
  assert(load_progress_bar(33 * MIB, 32 * MIB, 200) == 200);
  assert(load_progress_bar(64 * KIB, 0, 200) == 0);
  assert(load_progress_bar(0, 0, 200) == 0);
}

static void check_text(uint32_t done, uint32_t total, const char *expected)
{
  char buf[32];

  load_progress_text(buf, sizeof(buf), done, total);
  assert(strcmp(buf, expected) == 0);
}

static void test_kib_line(void)
{
  check_text(0, 16 * MIB, "0 / 16384 KiB");
  check_text(64 * KIB, 16 * MIB, "64 / 16384 KiB");
  check_text(16 * MIB, 16 * MIB, "16384 / 16384 KiB");
  check_text(32 * MIB - 1, 32 * MIB, "32767 / 32768 KiB");
  check_text(32 * MIB, 32 * MIB, "32768 / 32768 KiB");
  /* done rounds down and total up. */
  check_text(1023, 1025, "0 / 2 KiB");
  check_text(1025, 1025, "1 / 2 KiB");
  check_text(UINT32_MAX, UINT32_MAX, "4194303 / 4194304 KiB");
  check_text(0, 0, "0 / 0 KiB");
}

/* A buffer too small for the line gets as much as fits and its NUL, and
   nothing past cap is written. */
static void test_small_buffer_is_cut(void)
{
  char buf[16];

  memset(buf, 'X', sizeof(buf));
  load_progress_text(buf, 6, 16 * MIB, 16 * MIB);
  assert(strcmp(buf, "16384") == 0);
  assert(buf[6] == 'X' && buf[15] == 'X');

  memset(buf, 'X', sizeof(buf));
  load_progress_text(buf, 1, 16 * MIB, 16 * MIB);
  assert(buf[0] == '\0' && buf[1] == 'X');

  memset(buf, 'X', sizeof(buf));
  load_progress_text(buf, 0, 16 * MIB, 16 * MIB);
  assert(buf[0] == 'X');
}

int main(void)
{
  test_redraws_are_paced();
  test_new_load_draws_at_once();
  test_bar_length();
  test_kib_line();
  test_small_buffer_is_cut();
  puts("Load progress tests passed");
  return 0;
}
