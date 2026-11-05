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
 * Host tests for toast.c: the ring of non-modal messages shown one at a
 * time, oldest first, each for its duration in real microseconds.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "toast.h"

#define DURATION 3000000U

static const char *visible_text(uint32_t now_us)
{
  const Toast *toast = toast_current(now_us);

  return toast == NULL ? NULL : toast->text;
}

/* The ninth toast, pushed at 500, drops the first; the rest follow in
   order, each shown for its whole duration from when it becomes visible. */
static void test_full_ring_drops_oldest(void)
{
  char text[TOAST_RING_SIZE + 1][12];
  uint32_t i;

  toast_clear();
  for(i = 0; i <= TOAST_RING_SIZE; i++)
  {
    snprintf(text[i], sizeof(text[i]), "toast %u", (unsigned)i);
    toast_push(text[i], 1000, 0, i == TOAST_RING_SIZE ? 500 : 0);
  }

  for(i = 1; i <= TOAST_RING_SIZE; i++)
  {
    assert(strcmp(visible_text(500 + (i - 1) * 1000), text[i]) == 0);
    assert(strcmp(visible_text(500 + i * 1000 - 1), text[i]) == 0);
  }
  assert(visible_text(500 + TOAST_RING_SIZE * 1000) == NULL);
}

/* A repeated text restarts its timer with the new duration and takes no
   second slot, whether it is visible or still queued. */
static void test_duplicate_restarts_timer(void)
{
  char long_text[100];

  toast_clear();
  toast_push("Slot 1", 1000, 0, 0);
  toast_push("Slot 10", 1000, 0, 0);
  toast_push("Slot 1", 1000, 0, 900);
  assert(strcmp(visible_text(1899), "Slot 1") == 0);
  assert(strcmp(visible_text(1900), "Slot 10") == 0);
  assert(visible_text(2900) == NULL);

  /* A shorter text does not match a longer queued one it starts. */
  toast_clear();
  toast_push("Slot 10", 1000, 0, 0);
  toast_push("Slot 1", 1000, 0, 0);
  assert(strcmp(visible_text(999), "Slot 10") == 0);
  assert(strcmp(visible_text(1000), "Slot 1") == 0);

  toast_clear();
  toast_push("A", 1000, 0, 0);
  toast_push("B", 1000, 0, 0);
  toast_push("B", 2000, 0, 0);
  assert(strcmp(visible_text(1000), "B") == 0);
  assert(strcmp(visible_text(2999), "B") == 0);
  assert(visible_text(3000) == NULL);

  /* Texts equal once cut to 63 bytes are the same toast. */
  memset(long_text, 'x', sizeof(long_text) - 1);
  long_text[sizeof(long_text) - 1] = '\0';
  toast_clear();
  toast_push(long_text, 1000, 0, 0);
  long_text[80] = 'y';
  toast_push(long_text, 1000, 0, 500);
  assert(strlen(visible_text(1499)) == TOAST_TEXT_SIZE - 1);
  assert(visible_text(1500) == NULL);
}

static void test_exact_expiry(void)
{
  toast_clear();
  toast_push("Saved", DURATION, 0, 5000);
  assert(visible_text(5000) != NULL);
  assert(visible_text(5000 + DURATION - 1) != NULL);
  assert(visible_text(5000 + DURATION) == NULL);
}

/* A push stamped after the reader's time, from a later clock read, is
   shown rather than dropped as expired. */
static void test_push_stamped_after_now(void)
{
  toast_clear();
  toast_push("Saved", DURATION, 0, 1005);
  assert(visible_text(1000) != NULL);
  assert(visible_text(1005 + DURATION - 1) != NULL);
  assert(visible_text(1005 + DURATION) == NULL);
}

/* sceKernelGetSystemTimeLow wraps after 2^32 us. */
static void test_time_wraps_mid_toast(void)
{
  const uint32_t start = UINT32_MAX - 999999U;

  toast_clear();
  toast_push("Wrap", DURATION, 0, start);
  assert(visible_text(start) != NULL);
  assert(visible_text(UINT32_MAX) != NULL);
  assert(visible_text(0) != NULL);
  assert(visible_text(start + DURATION - 1) != NULL);
  assert(visible_text(start + DURATION) == NULL);

  /* A queued toast promoted after the wrap gets its whole duration. */
  toast_push("First", 1000000U, 0, start);
  toast_push("Second", 1000000U, 0, start);
  assert(strcmp(visible_text(start + 1000000U), "Second") == 0);
  assert(strcmp(visible_text(start + 1999999U), "Second") == 0);
  assert(visible_text(start + 2000000U) == NULL);
}

/* Pushes text alone and returns the stored copy. */
static const Toast *push_alone(const char *text, int sjis)
{
  const Toast *toast;

  toast_clear();
  toast_push(text, DURATION, sjis, 0);
  toast = toast_current(0);
  assert(toast != NULL);
  assert(toast->sjis == (sjis != 0));
  return toast;
}

static void test_gbk_cut_keeps_pairs(void)
{
  char text[101];
  const Toast *toast;
  size_t i;

  /* 0xC4 0xE3 pairs: byte 62 starts a pair, so the cut stops before it. */
  for(i = 0; i < 100; i += 2)
  {
    text[i] = (char)0xC4;
    text[i + 1] = (char)0xE3;
  }
  text[100] = '\0';
  toast = push_alone(text, 0);
  assert(strlen(toast->text) == 62);
  assert(memcmp(toast->text, text, 62) == 0);

  /* One ASCII byte first: pairs now end on byte 62 and 63 bytes fit. */
  text[0] = 'a';
  memmove(text + 1, text + 2, 98);
  text[99] = '\0';
  toast = push_alone(text, 0);
  assert(strlen(toast->text) == 63);
  assert((unsigned char)toast->text[62] == 0xE3);

  toast = push_alone("Short", 0);
  assert(strcmp(toast->text, "Short") == 0);
}

static void test_shift_jis_cut(void)
{
  char text[101];
  const Toast *toast;
  size_t i;

  /* Half-width katakana are single bytes in Shift-JIS but GBK leads. */
  memset(text, 0xB1, 70);
  text[70] = '\0';
  assert(strlen(push_alone(text, 1)->text) == 63);
  assert(strlen(push_alone(text, 0)->text) == 62);

  /* Two katakana, then 0x82 0xA0 pairs: byte 62 starts a pair. */
  text[0] = (char)0xB1;
  text[1] = (char)0xB1;
  for(i = 2; i < 100; i += 2)
  {
    text[i] = (char)0x82;
    text[i + 1] = (char)0xA0;
  }
  text[100] = '\0';
  toast = push_alone(text, 1);
  assert(strlen(toast->text) == 62);
  assert((unsigned char)toast->text[61] == 0xA0);

  /* One katakana: the last pair ends on byte 62 and fits. */
  toast = push_alone(text + 1, 1);
  assert(strlen(toast->text) == 63);
  assert((unsigned char)toast->text[0] == 0xB1);
  assert((unsigned char)toast->text[62] == 0xA0);
}

static void test_clear_empties_ring(void)
{
  toast_clear();
  toast_push("One", DURATION, 0, 0);
  toast_push("Two", DURATION, 0, 0);
  toast_push("Three", DURATION, 0, 0);
  toast_clear();
  assert(toast_current(0) == NULL);

  toast_push("Two", DURATION, 0, 10);
  assert(strcmp(visible_text(10), "Two") == 0);
  assert(visible_text(10 + DURATION) == NULL);

  toast_push(NULL, DURATION, 0, 20);
  assert(toast_current(20) == NULL);
}

/* Runs every case; a failing assert aborts the run. */
int main(void)
{
  test_full_ring_drops_oldest();
  test_duplicate_restarts_timer();
  test_exact_expiry();
  test_push_stamped_after_now();
  test_time_wraps_mid_toast();
  test_gbk_cut_keeps_pairs();
  test_shift_jis_cut();
  test_clear_empties_ring();
  puts("Toast tests passed");
  return 0;
}
