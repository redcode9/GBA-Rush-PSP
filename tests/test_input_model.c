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
 * Host tests for input_model.c: one turbo train per input, Hold and
 * Toggle modes, the three speeds, stop, and the stick hysteresis.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "input_model.h"

#define IN_CIRCLE (1u << 1)
#define IN_CROSS (1u << 2)
#define GBA_A 0x001u
#define GBA_B 0x002u
#define GBA_R 0x100u

static InputModelTurbo turbo[INPUT_MODEL_INPUTS];

static void set_turbo(uint32_t input, uint16_t key, uint8_t speed,
 uint8_t toggle)
{
  turbo[input].key = key;
  turbo[input].speed = speed;
  turbo[input].toggle = toggle;
}

/* Circle is turbo A and Cross is turbo B, both 30 per second, Hold. */
static void reset_turbo(void)
{
  memset(turbo, 0, sizeof(turbo));
  set_turbo(1, GBA_A, INPUT_MODEL_TURBO_30, 0);
  set_turbo(2, GBA_B, INPUT_MODEL_TURBO_30, 0);
}

/* Counts the presses of key over frames first..first+count-1 while pad
   is held, as the game sees them: frames where key goes from up to
   down. */
static uint32_t count_presses(InputModelTurboState *state, uint32_t pad,
 uint32_t first, uint32_t count, uint32_t key)
{
  uint32_t presses = 0;
  uint32_t was_down = 0;
  uint32_t i;

  for(i = 0; i < count; i++)
  {
    uint32_t down = input_model_turbo(state, pad, first + i, turbo, 0) & key;

    if(down != 0 && was_down == 0)
      presses++;
    was_down = down;
  }
  return presses;
}

/* Two turbo inputs held together both pulse at full speed: one shared
   flag flipped per input left one always down and the other always up. */
static void test_two_turbo_inputs_both_pulse(void)
{
  InputModelTurboState state;

  reset_turbo();
  memset(&state, 0, sizeof(state));
  assert(count_presses(&state, IN_CIRCLE | IN_CROSS, 0, 60, GBA_A) == 30);
  memset(&state, 0, sizeof(state));
  assert(count_presses(&state, IN_CIRCLE | IN_CROSS, 0, 60, GBA_B) == 30);

  /* Pressed one frame apart, each train keeps its own phase. */
  memset(&state, 0, sizeof(state));
  assert(input_model_turbo(&state, IN_CIRCLE, 100, turbo, 0) == GBA_A);
  assert(input_model_turbo(&state, IN_CIRCLE | IN_CROSS, 101, turbo, 0) ==
   GBA_B);
  assert(input_model_turbo(&state, IN_CIRCLE | IN_CROSS, 102, turbo, 0) ==
   GBA_A);
  assert(input_model_turbo(&state, IN_CIRCLE | IN_CROSS, 103, turbo, 0) ==
   GBA_B);
}

/* A press lasting one read still reaches the game, at every speed, in
   both modes and across the frame counter's wrap. */
static void test_tap_gives_a_press(void)
{
  static const uint32_t frames[] = { 0, 1, 77, 0xFFFFFFFFu };
  uint32_t speed;
  uint32_t toggle;
  uint32_t f;

  for(speed = INPUT_MODEL_TURBO_30; speed <= INPUT_MODEL_TURBO_10; speed++)
  {
    for(toggle = 0; toggle < 2; toggle++)
    {
      for(f = 0; f < sizeof(frames) / sizeof(frames[0]); f++)
      {
        InputModelTurboState state;

        reset_turbo();
        set_turbo(1, GBA_A, (uint8_t)speed, (uint8_t)toggle);
        memset(&state, 0, sizeof(state));
        assert(input_model_turbo(&state, 0, frames[f] - 1, turbo, 0) == 0);
        assert(input_model_turbo(&state, IN_CIRCLE, frames[f], turbo, 0) ==
         GBA_A);
        /* Hold stops on the release, Toggle keeps pulsing. */
        assert((input_model_turbo(&state, 0, frames[f] + 1, turbo, 0) ==
         0) == (toggle == 0 || speed == INPUT_MODEL_TURBO_30));
        assert(input_model_turbo(&state, 0, frames[f] + 2 * (speed + 1),
         turbo, 0) == (toggle != 0 ? GBA_A : 0));
      }
    }
  }
}

/* Each press of a Toggle input inverts its train; the input does not
   need to stay held. */
static void test_toggle_inverts_on_each_press(void)
{
  InputModelTurboState state;
  uint32_t frame = 0;
  uint32_t round;

  reset_turbo();
  set_turbo(1, GBA_A, INPUT_MODEL_TURBO_15, 1);
  memset(&state, 0, sizeof(state));

  for(round = 0; round < 4; round++)
  {
    uint32_t i;

    /* Pressed for 3 reads, then released for 20. */
    for(i = 0; i < 23; i++, frame++)
    {
      uint32_t pad = i < 3 ? IN_CIRCLE : 0;
      uint32_t keys = input_model_turbo(&state, pad, frame, turbo, 0);

      if(round % 2 == 0)
        assert(keys == (((i / 2) % 2 == 0) ? GBA_A : 0));
      else
        assert(keys == 0);
    }
  }

  /* Held with the train running, Toggle keeps the 15 per second pace. */
  memset(&state, 0, sizeof(state));
  assert(count_presses(&state, IN_CIRCLE, 0, 60, GBA_A) == 15);
}

/* Stop ends every train; an input still held starts again only on a
   new press. */
static void test_stop(void)
{
  InputModelTurboState state;
  uint32_t frame;

  reset_turbo();
  set_turbo(2, GBA_B, INPUT_MODEL_TURBO_30, 1);
  memset(&state, 0, sizeof(state));
  assert(input_model_turbo(&state, IN_CIRCLE | IN_CROSS, 0, turbo, 0) ==
   (GBA_A | GBA_B));
  input_model_turbo_stop(&state);

  for(frame = 1; frame < 40; frame++)
    assert(input_model_turbo(&state, IN_CIRCLE, frame, turbo, 0) == 0);

  assert(input_model_turbo(&state, 0, 40, turbo, 0) == 0);
  assert(input_model_turbo(&state, IN_CIRCLE, 41, turbo, 0) == GBA_A);
  assert(input_model_turbo(&state, IN_CROSS, 42, turbo, 0) == GBA_B);
  assert(input_model_turbo(&state, 0, 43, turbo, 0) == 0);
  assert(input_model_turbo(&state, 0, 44, turbo, 0) == GBA_B);
}

/* Turbo A plus a normal input on A give a held A: the old rapid-fire
   code cleared A in the turbo's off frames. A normal key on another GBA
   key passes through while the turbo keeps pulsing. */
static void test_turbo_and_normal_key_hold(void)
{
  InputModelTurboState state;
  uint32_t frame;

  /* Circle is turbo A; Cross is a normal input, so its GBA key comes in
     as normal_keys. */
  reset_turbo();
  set_turbo(2, 0, 0, 0);
  memset(&state, 0, sizeof(state));
  for(frame = 0; frame < 60; frame++)
    assert(input_model_turbo(&state, IN_CIRCLE | IN_CROSS, frame, turbo,
     GBA_A) == GBA_A);

  memset(&state, 0, sizeof(state));
  for(frame = 0; frame < 60; frame++)
    assert(input_model_turbo(&state, IN_CIRCLE | IN_CROSS, frame, turbo,
     GBA_B) == (GBA_B | (frame % 2 == 0 ? GBA_A : 0)));
}

/* Speeds 30, 15 and 10 give 30, 15 and 10 presses over 60 frames, with
   on and off spans of 1, 2 and 3 frames, also across the counter's
   wrap. */
static void test_speeds(void)
{
  static const uint32_t presses[] = { 30, 15, 10 };
  uint32_t speed;

  for(speed = INPUT_MODEL_TURBO_30; speed <= INPUT_MODEL_TURBO_10; speed++)
  {
    uint32_t toggle;

    for(toggle = 0; toggle < 2; toggle++)
    {
      InputModelTurboState state;
      uint32_t i;

      reset_turbo();
      set_turbo(1, GBA_A, (uint8_t)speed, (uint8_t)toggle);
      memset(&state, 0, sizeof(state));
      assert(count_presses(&state, IN_CIRCLE, 0, 60, GBA_A) ==
       presses[speed]);

      memset(&state, 0, sizeof(state));
      assert(count_presses(&state, IN_CIRCLE, 0xFFFFFFF0u, 60, GBA_A) ==
       presses[speed]);

      memset(&state, 0, sizeof(state));
      for(i = 0; i < 12; i++)
        assert(input_model_turbo(&state, IN_CIRCLE, 500 + i, turbo, 0) ==
         (((i / (speed + 1)) % 2 == 0) ? GBA_A : 0));
    }
  }
}

static uint32_t lcg_state = 12345;

static uint32_t lcg_next(void)
{
  lcg_state = lcg_state * 1103515245u + 12345u;
  return lcg_state >> 16;
}

/* Same state, pad and frame give the same keys and state, and reading
   a frame more than once (the link waits) does not move the trains. */
static void test_same_inputs_same_keys(void)
{
  InputModelTurboState once;
  InputModelTurboState repeated;
  uint32_t frame;

  reset_turbo();
  set_turbo(4, GBA_R, INPUT_MODEL_TURBO_10, 1);
  memset(&once, 0, sizeof(once));
  memset(&repeated, 0, sizeof(repeated));

  for(frame = 0; frame < 2000; frame++)
  {
    uint32_t pad = lcg_next() & 0xFFFFu;
    uint32_t reads = 1 + lcg_next() % 3;
    uint32_t keys = input_model_turbo(&once, pad, frame, turbo, 0);
    InputModelTurboState copy = repeated;

    assert(input_model_turbo(&copy, pad, frame, turbo, 0) == keys);
    while(reads-- > 0)
      assert(input_model_turbo(&repeated, pad, frame, turbo, 0) == keys);
    assert(memcmp(&copy, &repeated, sizeof(copy)) == 0);
    assert(memcmp(&once, &repeated, sizeof(once)) == 0);
  }
}

static uint32_t stick_threshold(uint32_t sensitivity)
{
  return 20 + sensitivity * 10;
}

static int stick_high_side(uint32_t dir)
{
  return dir == INPUT_MODEL_STICK_RIGHT || dir == INPUT_MODEL_STICK_DOWN;
}

/* How far from its own edge a held direction is released: threshold +
   16, but at most 127 from the low edge (raw 127) or 126 from the high
   edge (raw 129). */
static uint32_t stick_release(uint32_t sensitivity, uint32_t dir)
{
  uint32_t release = stick_threshold(sensitivity) +
   INPUT_MODEL_STICK_HYSTERESIS;
  uint32_t limit = stick_high_side(dir) ? 126 : 127;

  return release < limit ? release : limit;
}

/* Reads one direction on its axis with the other axis centred and
   returns its bit alone. v is measured from the direction's own edge
   (0 at the edge). */
static uint32_t stick_read(uint32_t held, uint32_t dir, uint32_t v,
 uint32_t sensitivity)
{
  uint32_t raw = stick_high_side(dir) ? 255 - v : v;

  if(dir == INPUT_MODEL_STICK_LEFT || dir == INPUT_MODEL_STICK_RIGHT)
    return input_model_stick(held, raw, 128, sensitivity) & dir;
  return input_model_stick(held, 128, raw, sensitivity) & dir;
}

/* Every position on every axis against the rule: press past the
   threshold, release only at threshold + 16, never past raw 127 or 129,
   which leaves the centre released at every sensitivity; 10 is the
   first where the limit applies. */
static void test_stick_edges(void)
{
  static const uint32_t dirs[] = { INPUT_MODEL_STICK_UP,
   INPUT_MODEL_STICK_DOWN, INPUT_MODEL_STICK_LEFT, INPUT_MODEL_STICK_RIGHT };
  uint32_t sensitivity;

  for(sensitivity = 0; sensitivity <= 10; sensitivity++)
  {
    uint32_t threshold = stick_threshold(sensitivity);
    uint32_t d;

    for(d = 0; d < 4; d++)
    {
      uint32_t release = stick_release(sensitivity, dirs[d]);
      uint32_t v;

      for(v = 0; v < 256; v++)
      {
        assert(stick_read(0, dirs[d], v, sensitivity) ==
         (v < threshold ? dirs[d] : 0));
        assert(stick_read(dirs[d], dirs[d], v, sensitivity) ==
         (v < release ? dirs[d] : 0));
      }
      assert(stick_read(dirs[d], dirs[d], 127, sensitivity) == 0);
      assert(stick_read(dirs[d], dirs[d], 128, sensitivity) == 0);
    }
    assert(input_model_stick(0xF, 128, 128, sensitivity) == 0);
  }
}

/* Samples within 6 of the press point or of the release point change a
   direction at most once, at every sensitivity from 0 to 9. */
static void test_stick_noise_never_oscillates(void)
{
  static const uint32_t dirs[] = { INPUT_MODEL_STICK_UP,
   INPUT_MODEL_STICK_DOWN, INPUT_MODEL_STICK_LEFT, INPUT_MODEL_STICK_RIGHT };
  uint32_t sensitivity;

  for(sensitivity = 0; sensitivity <= 9; sensitivity++)
  {
    uint32_t d;
    uint32_t c;

    for(d = 0; d < 4; d++)
    {
      for(c = 0; c < 2; c++)
      {
        uint32_t centre = c == 0 ? stick_threshold(sensitivity) :
         stick_release(sensitivity, dirs[d]);
        uint32_t held = c == 0 ? 0 : dirs[d];
        uint32_t changes = 0;
        uint32_t i;

        for(i = 0; i < 1000; i++)
        {
          uint32_t v = centre - 6 + lcg_next() % 13;
          uint32_t next = stick_read(held, dirs[d], v, sensitivity);

          if(next != held)
            changes++;
          held = next;
        }
        assert(changes <= 1);
      }
    }
  }
}

int main(void)
{
  test_two_turbo_inputs_both_pulse();
  test_tap_gives_a_press();
  test_toggle_inverts_on_each_press();
  test_stop();
  test_turbo_and_normal_key_hold();
  test_speeds();
  test_same_inputs_same_keys();
  test_stick_edges();
  test_stick_noise_never_oscillates();
  puts("Input model tests passed");
  return 0;
}
