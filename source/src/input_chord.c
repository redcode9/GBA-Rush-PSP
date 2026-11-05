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

#include "input_chord.h"

enum
{
  HOLD_IDLE,
  HOLD_COUNTING,
  HOLD_FIRED
};

const InputChord input_chord_defaults[INPUT_CHORD_DEFAULT_COUNT] =
{
  { INPUT_CHORD_SAVESTATE, INPUT_CHORD_KEY_SELECT | INPUT_CHORD_KEY_L, 0 },
  { INPUT_CHORD_LOADSTATE, INPUT_CHORD_KEY_SELECT | INPUT_CHORD_KEY_R, 0 },
  { INPUT_CHORD_MENU, INPUT_CHORD_KEY_START | INPUT_CHORD_KEY_SELECT, 500 },
  { INPUT_CHORD_FASTFORWARD, 0, 0 },
  { INPUT_CHORD_FPS, 0, 0 }
};

static uint32_t chord_key_count(uint32_t mask)
{
  uint32_t count = 0;

  for(; mask != 0; mask &= mask - 1)
    count++;
  return count;
}

/* The keys that make a chord fire and are then hidden from the game. */
static uint32_t chord_trigger(uint32_t mask)
{
  uint32_t trigger = mask & ~INPUT_CHORD_MODIFIERS;

  return trigger != 0 ? trigger : mask;
}

int input_chord_validate(const InputChord *table, uint32_t count)
{
  uint32_t i;
  uint32_t j;

  if(count > INPUT_CHORD_MAX)
    return 0;

  for(i = 0; i < count; i++)
  {
    if(table[i].action >= INPUT_CHORD_ACTION_COUNT ||
     chord_key_count(table[i].mask) > INPUT_CHORD_MAX_KEYS)
      return 0;

    for(j = 0; j < i; j++)
    {
      if(table[i].mask != 0 && table[j].mask == table[i].mask)
        return 0;
    }
  }
  return 1;
}

void input_chord_init(InputChordState *state, const InputChord *table,
 uint32_t count)
{
  uint32_t i;
  uint32_t j;

  if(count > INPUT_CHORD_MAX)
    count = INPUT_CHORD_MAX;

  state->count = count;
  state->hidden = 0;
  for(i = 0; i < count; i++)
  {
    state->chord[i] = table[i];
    state->hold_phase[i] = HOLD_IDLE;
    state->hold_start_us[i] = 0;
  }

  /* Done once here so that input_chord_eval stays linear. */
  for(i = 0; i < count; i++)
  {
    uint32_t mask = table[i].mask;

    state->contained_by[i] = 0;
    for(j = 0; j < count; j++)
    {
      if(mask != 0 && table[j].mask != mask &&
       (table[j].mask & mask) == mask)
        state->contained_by[i] |= (uint8_t)(1u << j);
    }
  }
}

uint32_t input_chord_eval(InputChordState *state, uint32_t pad,
 uint32_t prev_pad, uint32_t now_us, uint32_t hold_block_mask,
 uint32_t *hide_mask)
{
  uint32_t pressed = pad & ~prev_pad;
  uint32_t held = 0;
  uint32_t fired = 0;
  uint32_t hidden = state->hidden;
  uint32_t i;

  for(i = 0; i < state->count; i++)
  {
    uint32_t mask = state->chord[i].mask;

    if(mask != 0 && (pad & mask) == mask)
      held |= 1u << i;
  }

  for(i = 0; i < state->count; i++)
  {
    const InputChord *chord = &state->chord[i];
    uint32_t trigger = chord_trigger(chord->mask);
    uint32_t is_held = (held >> i) & 1;
    /* A longer held chord that contains this one takes precedence. */
    int ready = is_held && (state->contained_by[i] & held) == 0;

    if(chord->hold_ms == 0)
    {
      if(!ready || (pressed & trigger) == 0)
        continue;
    }
    else
    {
      if(!is_held)
      {
        state->hold_phase[i] = HOLD_IDLE;
        continue;
      }
      if(state->hold_phase[i] == HOLD_FIRED)
        continue;
      if(!ready || (pad & hold_block_mask & ~(uint32_t)chord->mask) != 0)
      {
        state->hold_phase[i] = HOLD_IDLE;
        continue;
      }
      if(state->hold_phase[i] == HOLD_IDLE)
      {
        state->hold_phase[i] = HOLD_COUNTING;
        state->hold_start_us[i] = now_us;
        continue;
      }
      if(now_us - state->hold_start_us[i] < chord->hold_ms * 1000u)
        continue;
      state->hold_phase[i] = HOLD_FIRED;
    }

    fired |= 1u << chord->action;
    hidden |= trigger;
  }

  state->hidden = (uint16_t)(hidden & pad);
  *hide_mask = state->hidden;
  return fired;
}
