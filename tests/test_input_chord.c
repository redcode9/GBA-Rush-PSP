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
 * Host tests for input_chord.c: shortcut chords fired on a press or
 * after a hold, the inputs they hide from the game and table validation.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "input_chord.h"

#define KEY_TRIANGLE (1u << 0)
#define KEY_CIRCLE (1u << 1)
#define KEY_CROSS (1u << 2)
#define KEY_SQUARE (1u << 3)
#define KEY_L INPUT_CHORD_KEY_L
#define KEY_R INPUT_CHORD_KEY_R
#define KEY_SELECT INPUT_CHORD_KEY_SELECT
#define KEY_START INPUT_CHORD_KEY_START
#define KEY_LEFT (1u << 7)
/* Circle and Cross carry GBA A and B in the default button map. */
#define KEY_A_B (KEY_CIRCLE | KEY_CROSS)
#define MENU_KEYS (KEY_START | KEY_SELECT)
#define MS 1000u

#define BIT(action) (1u << (action))

/* Each chord is contained in the next: the shorter ones must yield. The
   hold has an A input of its own, which must not block it. */
static const InputChord nested_press[] =
{
  { INPUT_CHORD_SAVESTATE, KEY_SELECT | KEY_L, 0 },
  { INPUT_CHORD_LOADSTATE, KEY_SELECT | KEY_L | KEY_R, 0 },
  { INPUT_CHORD_MENU, KEY_SELECT | KEY_L | KEY_R | KEY_CIRCLE, 500 }
};

/* A hold contained in a press chord, and a chord with two triggers. */
static const InputChord nested_hold[] =
{
  { INPUT_CHORD_MENU, MENU_KEYS, 500 },
  { INPUT_CHORD_SAVESTATE, MENU_KEYS | KEY_L, 0 },
  { INPUT_CHORD_LOADSTATE, KEY_L | KEY_R, 0 }
};

/* The rules restated from the header, one chord at a time. */
static int is_full(uint32_t mask, uint32_t pad)
{
  return mask != 0 && (pad & mask) == mask;
}

static uint32_t trigger_of(uint32_t mask)
{
  return (mask & ~INPUT_CHORD_MODIFIERS) != 0 ?
   mask & ~INPUT_CHORD_MODIFIERS : mask;
}

static int yields(const InputChord *table, uint32_t count, uint32_t i,
 uint32_t pad)
{
  uint32_t mask = table[i].mask;
  uint32_t j;

  for(j = 0; j < count; j++)
  {
    if(table[j].mask != mask && (table[j].mask & mask) == mask &&
     is_full(table[j].mask, pad))
      return 1;
  }
  return 0;
}

static int hold_counts(const InputChord *table, uint32_t count, uint32_t i,
 uint32_t pad)
{
  return is_full(table[i].mask, pad) && !yields(table, count, i, pad) &&
   (pad & KEY_A_B & ~table[i].mask) == 0;
}

static int press_fires(const InputChord *table, uint32_t count, uint32_t i,
 uint32_t pad, uint32_t prev_pad)
{
  return is_full(table[i].mask, pad) && !yields(table, count, i, pad) &&
   (pad & ~prev_pad & trigger_of(table[i].mask)) != 0;
}

static uint32_t triggers_of(const InputChord *table, uint32_t count,
 uint32_t actions)
{
  uint32_t keys = 0;
  uint32_t i;

  for(i = 0; i < count; i++)
  {
    if(actions & BIT(table[i].action))
      keys |= trigger_of(table[i].mask);
  }
  return keys;
}

/* Every pad state after each previous state in prevs: hold prev_pad from
   0 ms, read pad at 1000 ms, then read pad again 1 us later. The table's
   actions are distinct, so a fired action names its chord. */
static void check_all_pads(const InputChord *table, uint32_t count)
{
  uint32_t prevs[1 + 16 + INPUT_CHORD_MAX];
  uint32_t prev_count = 0;
  uint32_t p;
  uint32_t i;

  prevs[prev_count++] = 0;
  for(i = 0; i < 16; i++)
    prevs[prev_count++] = 1u << i;
  for(i = 0; i < count; i++)
  {
    assert(table[i].hold_ms <= 1000);
    prevs[prev_count++] = table[i].mask;
  }

  for(p = 0; p < prev_count; p++)
  {
    uint32_t prev_pad = prevs[p];
    uint32_t pad;

    for(pad = 0; pad < 0x10000; pad++)
    {
      InputChordState state;
      uint32_t expect1 = 0;
      uint32_t expect2 = 0;
      uint32_t fired1;
      uint32_t fired2;
      uint32_t hide;
      uint32_t hide_again;

      for(i = 0; i < count; i++)
      {
        if(table[i].hold_ms == 0)
        {
          if(press_fires(table, count, i, prev_pad, 0))
            expect1 |= BIT(table[i].action);
          if(press_fires(table, count, i, pad, prev_pad))
            expect2 |= BIT(table[i].action);
        }
        else if(hold_counts(table, count, i, prev_pad) &&
         hold_counts(table, count, i, pad))
          expect2 |= BIT(table[i].action);
      }

      input_chord_init(&state, table, count);
      fired1 = input_chord_eval(&state, prev_pad, 0, 0, KEY_A_B, &hide);
      assert(fired1 == expect1);
      fired2 = input_chord_eval(&state, pad, prev_pad, 1000 * MS, KEY_A_B,
       &hide);
      assert(fired2 == expect2);
      /* Only triggers of fired chords are hidden, and only while held. */
      assert(hide == ((triggers_of(table, count, fired1) & prev_pad & pad) |
       triggers_of(table, count, fired2)));
      /* Each press fires once and a hold does not repeat. */
      assert(input_chord_eval(&state, pad, pad, 1000 * MS + 1, KEY_A_B,
       &hide_again) == 0);
      assert(hide_again == hide);
    }
  }
}

static void test_all_pad_states(void)
{
  check_all_pads(input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  check_all_pads(nested_press, 3);
  check_all_pads(nested_hold, 3);
}

static void test_defaults(void)
{
  InputChordState state;
  uint32_t hide;
  uint32_t i;

  assert(input_chord_validate(input_chord_defaults,
   INPUT_CHORD_DEFAULT_COUNT));
  /* update_input reads entry n as action n, for every action. */
  for(i = 0; i < INPUT_CHORD_DEFAULT_COUNT; i++)
    assert((uint32_t)input_chord_defaults[i].action == i);
  assert(input_chord_defaults[INPUT_CHORD_FASTFORWARD].mask == 0);
  assert(input_chord_defaults[INPUT_CHORD_FPS].mask == 0);
  assert(input_chord_defaults[INPUT_CHORD_VIDEO_CYCLE].mask == 0);
  assert(input_chord_defaults[0].action == INPUT_CHORD_SAVESTATE);
  assert(input_chord_defaults[0].mask == (KEY_SELECT | KEY_L));
  assert(input_chord_defaults[0].hold_ms == 0);
  assert(input_chord_defaults[1].action == INPUT_CHORD_LOADSTATE);
  assert(input_chord_defaults[1].mask == (KEY_SELECT | KEY_R));
  assert(input_chord_defaults[1].hold_ms == 0);
  assert(input_chord_defaults[2].action == INPUT_CHORD_MENU);
  assert(input_chord_defaults[2].mask == MENU_KEYS);
  assert(input_chord_defaults[2].hold_ms == 500);

  /* SELECT then L saves; L stays hidden after SELECT is let go. */
  input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  assert(input_chord_eval(&state, KEY_SELECT, 0, 0, KEY_A_B, &hide) == 0);
  assert(hide == 0);
  assert(input_chord_eval(&state, KEY_SELECT | KEY_L, KEY_SELECT, 1, KEY_A_B,
   &hide) == BIT(INPUT_CHORD_SAVESTATE));
  assert(hide == KEY_L);
  assert(input_chord_eval(&state, KEY_L, KEY_SELECT | KEY_L, 2, KEY_A_B,
   &hide) == 0);
  assert(hide == KEY_L);
  assert(input_chord_eval(&state, 0, KEY_L, 3, KEY_A_B, &hide) == 0);
  assert(hide == 0);
  assert(input_chord_eval(&state, KEY_SELECT | KEY_R, 0, 4, KEY_A_B, &hide)
   == BIT(INPUT_CHORD_LOADSTATE));
  assert(hide == KEY_R);

  /* L held in the game, then SELECT: the modifier came last. */
  input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  assert(input_chord_eval(&state, KEY_L, 0, 0, KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, KEY_SELECT | KEY_L, KEY_L, 1, KEY_A_B,
   &hide) == 0);
  assert(hide == 0);
}

/* Holds START+SELECT from start_us and reads it again calls times, evenly
   up to start_us + 500 ms: only the last read opens the menu. The reads
   just before 500 ms are in the other hold tests. */
static void check_menu_hold(uint32_t start_us, uint32_t calls)
{
  InputChordState state;
  uint32_t hide;
  uint32_t k;

  input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  assert(input_chord_eval(&state, MENU_KEYS, 0, start_us, KEY_A_B, &hide)
   == 0);
  assert(hide == 0);
  for(k = 1; k < calls; k++)
  {
    assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS,
     start_us + k * (500 * MS / calls), KEY_A_B, &hide) == 0);
    assert(hide == 0);
  }
  assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS, start_us + 500 * MS,
   KEY_A_B, &hide) == BIT(INPUT_CHORD_MENU));
  assert(hide == MENU_KEYS);
}

static void test_hold_counts_time_not_calls(void)
{
  check_menu_hold(1000, 1);
  check_menu_hold(1000, 50);
  /* The clock wraps during the hold. */
  check_menu_hold(0xFFFFFFFFu - 200 * MS, 1);
  check_menu_hold(0xFFFFFFFFu - 200 * MS, 50);
}

static void test_hold_never_with_a_or_b(void)
{
  static const uint32_t blockers[] =
  {
    KEY_CIRCLE, KEY_CROSS, KEY_CIRCLE | KEY_CROSS
  };
  static const InputChord select_hold =
  {
    INPUT_CHORD_MENU, KEY_SELECT | KEY_TRIANGLE, 500
  };
  InputChordState state;
  uint32_t hide;
  uint32_t b;
  uint32_t t;

  for(b = 0; b < sizeof(blockers) / sizeof(blockers[0]); b++)
  {
    uint32_t pad = MENU_KEYS | blockers[b];

    input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
    for(t = 0; t <= 2000 * MS; t += 10 * MS)
    {
      assert(input_chord_eval(&state, pad, t == 0 ? 0 : pad, t, KEY_A_B,
       &hide) == 0);
      assert(hide == 0);
    }
  }

  /* A pressed during the hold restarts it once A is let go. */
  input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  assert(input_chord_eval(&state, MENU_KEYS, 0, 0, KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, MENU_KEYS | KEY_CIRCLE, MENU_KEYS,
   300 * MS, KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS | KEY_CIRCLE,
   400 * MS, KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS, 900 * MS - 1,
   KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS, 900 * MS, KEY_A_B,
   &hide) == BIT(INPUT_CHORD_MENU));

  /* GBA A mapped to SELECT: the chord's own keys never block it. */
  input_chord_init(&state, &select_hold, 1);
  assert(input_chord_eval(&state, select_hold.mask, 0, 0, KEY_SELECT, &hide)
   == 0);
  assert(input_chord_eval(&state, select_hold.mask, select_hold.mask,
   500 * MS, KEY_SELECT, &hide) == BIT(INPUT_CHORD_MENU));
}

/* Reads pad every 1 ms from from_us to before to_us and returns how many
   reads opened the menu; *fired_us gets the time of the last one. */
static uint32_t menu_fires(InputChordState *state, uint32_t *prev_pad,
 uint32_t pad, uint32_t from_us, uint32_t to_us, uint32_t *fired_us)
{
  uint32_t fires = 0;
  uint32_t hide;
  uint32_t t;

  for(t = from_us; t < to_us; t += MS)
  {
    if(input_chord_eval(state, pad, *prev_pad, t, KEY_A_B, &hide) &
     BIT(INPUT_CHORD_MENU))
    {
      fires++;
      *fired_us = t;
    }
    *prev_pad = pad;
  }
  return fires;
}

/* A+B+START+SELECT is the soft reset of many games: in whatever order the
   keys arrive, it never opens the menu, and once A and B are both let go
   the hold needs a full 500 ms of its own. */
static void test_soft_reset_keys_never_open_menu(void)
{
  static const uint32_t first_keys[] = { MENU_KEYS, KEY_A_B };
  static const uint32_t first_released[] = { KEY_CIRCLE, KEY_CROSS };
  InputChordState state;
  uint32_t prev_pad = 0;
  uint32_t fired_us = 0;
  uint32_t f;
  uint32_t r;

  /* START+SELECT alone: one menu, at 500 ms, for a 2 s hold. */
  input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  assert(menu_fires(&state, &prev_pad, MENU_KEYS, 0, 2000 * MS, &fired_us)
   == 1);
  assert(fired_us == 500 * MS);

  for(f = 0; f < 2; f++)
  {
    for(r = 0; r < 2; r++)
    {
      uint32_t still_held = KEY_A_B & ~first_released[r];

      input_chord_init(&state, input_chord_defaults,
       INPUT_CHORD_DEFAULT_COUNT);
      prev_pad = 0;
      assert(menu_fires(&state, &prev_pad, first_keys[f], 0, 100 * MS,
       &fired_us) == 0);
      assert(menu_fires(&state, &prev_pad, MENU_KEYS | KEY_A_B, 100 * MS,
       2100 * MS, &fired_us) == 0);
      /* One of A and B let go: the other still blocks the hold. */
      assert(menu_fires(&state, &prev_pad, MENU_KEYS | still_held,
       2100 * MS, 3000 * MS, &fired_us) == 0);
      assert(menu_fires(&state, &prev_pad, MENU_KEYS, 3000 * MS, 5000 * MS,
       &fired_us) == 1);
      assert(fired_us == 3500 * MS);
    }
  }
}

static void test_hold_rearms_after_release(void)
{
  InputChordState state;
  uint32_t hide;
  uint32_t t;

  input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  assert(input_chord_eval(&state, MENU_KEYS, 0, 0, KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS, 500 * MS, KEY_A_B,
   &hide) == BIT(INPUT_CHORD_MENU));

  /* Still held, even with A pressed and let go: no second menu. */
  for(t = 510 * MS; t <= 2000 * MS; t += 10 * MS)
  {
    uint32_t pad = (t >= 1000 * MS && t < 1100 * MS) ?
     MENU_KEYS | KEY_CIRCLE : MENU_KEYS;

    assert(input_chord_eval(&state, pad, pad, t, KEY_A_B, &hide) == 0);
  }

  /* SELECT let go and pressed again: a new hold. */
  assert(input_chord_eval(&state, KEY_START, MENU_KEYS, 2010 * MS, KEY_A_B,
   &hide) == 0);
  assert(hide == KEY_START);
  assert(input_chord_eval(&state, MENU_KEYS, KEY_START, 2100 * MS, KEY_A_B,
   &hide) == 0);
  assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS, 2600 * MS - 1,
   KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, MENU_KEYS, MENU_KEYS, 2600 * MS, KEY_A_B,
   &hide) == BIT(INPUT_CHORD_MENU));
}

/* The video cycle has no chord by default, so no pad state fires it; a
   chord the user binds to it fires once per press and hides its key. */
static void test_video_cycle_binding(void)
{
  InputChord table[INPUT_CHORD_DEFAULT_COUNT];
  InputChordState state;
  uint32_t hide;
  uint32_t pad;
  uint32_t i;

  input_chord_init(&state, input_chord_defaults, INPUT_CHORD_DEFAULT_COUNT);
  for(pad = 0; pad <= 0xFFFF; pad++)
  {
    assert((input_chord_eval(&state, pad, 0, pad, 0, &hide) &
     BIT(INPUT_CHORD_VIDEO_CYCLE)) == 0);
    input_chord_eval(&state, 0, pad, pad, 0, &hide);
  }

  for(i = 0; i < INPUT_CHORD_DEFAULT_COUNT; i++)
    table[i] = input_chord_defaults[i];
  table[INPUT_CHORD_VIDEO_CYCLE].mask = KEY_SELECT | KEY_LEFT;
  assert(input_chord_validate(table, INPUT_CHORD_DEFAULT_COUNT));
  input_chord_init(&state, table, INPUT_CHORD_DEFAULT_COUNT);
  assert(input_chord_eval(&state, KEY_SELECT, 0, 0, KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, KEY_SELECT | KEY_LEFT, KEY_SELECT, 1,
   KEY_A_B, &hide) == BIT(INPUT_CHORD_VIDEO_CYCLE));
  assert(hide == KEY_LEFT);
  assert(input_chord_eval(&state, KEY_SELECT | KEY_LEFT,
   KEY_SELECT | KEY_LEFT, 2, KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, KEY_SELECT, KEY_SELECT | KEY_LEFT, 3,
   KEY_A_B, &hide) == 0);
  assert(input_chord_eval(&state, KEY_SELECT | KEY_LEFT, KEY_SELECT, 4,
   KEY_A_B, &hide) == BIT(INPUT_CHORD_VIDEO_CYCLE));

  /* The same keys as another action's chord are a conflict. */
  table[INPUT_CHORD_VIDEO_CYCLE].mask = KEY_SELECT | KEY_L;
  assert(!input_chord_validate(table, INPUT_CHORD_DEFAULT_COUNT));
}

static void test_validate(void)
{
  InputChord table[INPUT_CHORD_MAX + 1] =
  {
    { INPUT_CHORD_SAVESTATE, KEY_SELECT | KEY_L | KEY_R | KEY_TRIANGLE, 0 },
    { INPUT_CHORD_LOADSTATE, 0, 0 },
    { INPUT_CHORD_MENU, 0, 500 }
  };

  /* Four keys and repeated empty chords are fine. */
  assert(input_chord_validate(table, 3));
  assert(input_chord_validate(table, 0));

  table[0].mask |= KEY_SQUARE;
  assert(!input_chord_validate(table, 3));
  table[0].mask &= ~KEY_SQUARE;

  /* The same keys for two actions, press or hold, is a conflict. */
  table[2].mask = table[0].mask;
  assert(!input_chord_validate(table, 3));
  table[2].mask = MENU_KEYS;
  assert(input_chord_validate(table, 3));

  table[1].action = INPUT_CHORD_ACTION_COUNT;
  assert(!input_chord_validate(table, 3));
  table[1].action = INPUT_CHORD_LOADSTATE;

  assert(!input_chord_validate(table, INPUT_CHORD_MAX + 1));
}

int main(void)
{
  test_validate();
  test_defaults();
  test_hold_counts_time_not_calls();
  test_hold_never_with_a_or_b();
  test_soft_reset_keys_never_open_menu();
  test_hold_rearms_after_release();
  test_video_cycle_binding();
  test_all_pad_states();
  puts("Input chord tests passed");
  return 0;
}
