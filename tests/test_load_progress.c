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

/* A 16 MiB ROM read in 64 KiB chunks: the first chunk draws, then the
   first chunk on or after each 45 ms step from it, and the last chunk
   draws whatever the time. */
static void test_redraws_are_paced(void)
{
  LoadProgressState state = { 0, 0, 0 };
  uint64_t t = 1000;

  assert(load_progress_due(&state, t, 64 * KIB, 16 * MIB));
  assert(!load_progress_due(&state, t + 44999, 128 * KIB, 16 * MIB));
  /* A redraw 30 ms late does not push the next one back. */
  assert(load_progress_due(&state, t + 75000, 192 * KIB, 16 * MIB));
  assert(!load_progress_due(&state, t + 89999, 256 * KIB, 16 * MIB));
  assert(load_progress_due(&state, t + 90000, 320 * KIB, 16 * MIB));
  /* More than one step behind: one redraw, then a new grid from it. */
  assert(load_progress_due(&state, t + 200000, 384 * KIB, 16 * MIB));
  assert(!load_progress_due(&state, t + 244999, 448 * KIB, 16 * MIB));
  assert(load_progress_due(&state, t + 245000, 512 * KIB, 16 * MIB));
  assert(load_progress_due(&state, t + 245001, 16 * MIB, 16 * MIB));
}

/* Counts the redraws of a 16 MiB load whose 64 KiB reads take period_us
   each, from before the first read to the last. */
static uint32_t count_redraws(uint64_t period_us, uint64_t *elapsed_us)
{
  LoadProgressState state = { 0, 0, 0 };
  uint64_t t = 1000000;
  uint32_t done = 0;
  uint32_t redraws = 0;

  while(done < 16 * MIB)
  {
    t += period_us;
    done += 64 * KIB;
    redraws += (uint32_t)(load_progress_due(&state, t, done, 16 * MIB) != 0);
  }
  *elapsed_us = t - 1000000;
  return redraws;
}

/* At least 20 redraws per second for any read time up to 50 ms, including
   those that do not divide the interval, and no more than the interval
   allows plus the first and last. */
static void test_rate_is_at_least_20_per_second(void)
{
  static const uint64_t periods_us[] = {
    1000, 7000, 10000, 12000, 13000, 16000, 20000, 23000, 30000, 34000,
    44000, 45000, 46000, 49000, 50000
  };
  size_t i;

  for(i = 0; i < sizeof(periods_us) / sizeof(periods_us[0]); i++)
  {
    uint64_t elapsed_us;
    uint32_t redraws = count_redraws(periods_us[i], &elapsed_us);

    assert((uint64_t)redraws * 1000000u >= 20u * elapsed_us);
    assert((uint64_t)redraws * 45000u <= elapsed_us + 2u * 45000u);
  }
}

/* An unknown size (total 0) is paced like any other load: no chunk counts
   as the last one. */
static void test_unknown_size_is_paced(void)
{
  LoadProgressState state = { 0, 0, 0 };

  assert(load_progress_due(&state, 0, 64 * KIB, 0));
  assert(!load_progress_due(&state, 10000, 128 * KIB, 0));
  assert(load_progress_due(&state, 45000, 192 * KIB, 0));
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
  /* done rounds down and total up, until the end reads total / total. */
  check_text(1023, 1025, "0 / 2 KiB");
  check_text(1024, 1025, "1 / 2 KiB");
  check_text(1025, 1025, "2 / 2 KiB");
  check_text(4000001, 4000001, "3907 / 3907 KiB");
  check_text(UINT32_MAX - 1, UINT32_MAX, "4194303 / 4194304 KiB");
  check_text(UINT32_MAX, UINT32_MAX, "4194304 / 4194304 KiB");
  /* An unknown size has no end. */
  check_text(64 * KIB, 0, "64 / 0 KiB");
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
  test_rate_is_at_least_20_per_second();
  test_unknown_size_is_paced();
  test_new_load_draws_at_once();
  test_bar_length();
  test_kib_line();
  test_small_buffer_is_cut();
  puts("Load progress tests passed");
  return 0;
}
