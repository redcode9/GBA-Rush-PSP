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

#include "input_model.h"

uint32_t input_model_turbo(InputModelTurboState *state, uint32_t pad,
 uint32_t frame, const InputModelTurbo *turbo, uint32_t normal_keys,
 uint32_t *flipped)
{
  uint32_t pressed = pad & ~state->prev_pad;
  uint32_t keys = normal_keys;
  uint32_t flips = 0;
  uint32_t i;

  state->prev_pad = pad;
  for(i = 0; i < INPUT_MODEL_INPUTS; i++)
  {
    uint32_t bit = 1u << i;

    if(turbo[i].key == 0)
      continue;

    if((pressed & bit) != 0)
    {
      if(turbo[i].toggle != 0)
        flips |= bit;
      if(turbo[i].toggle != 0 && (state->active & bit) != 0)
      {
        state->active &= ~bit;
        continue;
      }
      state->active |= bit;
      state->start_frame[i] = frame;
    }
    else if(turbo[i].toggle == 0 && (pad & bit) == 0)
      state->active &= ~bit;

    /* Each input has its own train, so held turbo inputs never share a
       phase. An off frame clears nothing, so a normal input on the same
       GBA key stays held. */
    if((state->active & bit) != 0 &&
     ((frame - state->start_frame[i]) / (turbo[i].speed + 1u)) % 2 == 0)
      keys |= turbo[i].key;
  }
  if(flipped != NULL)
    *flipped = flips;
  return keys;
}

void input_model_turbo_stop(InputModelTurboState *state)
{
  state->active = 0;
  state->prev_pad = ~0u;
}

static uint32_t stick_axis(uint32_t held, uint32_t v, uint32_t threshold,
 uint32_t low, uint32_t high)
{
  uint32_t release = threshold + INPUT_MODEL_STICK_HYSTERESIS;
  uint32_t low_edge = threshold;
  uint32_t high_edge = 255 - threshold;
  uint32_t out = 0;

  if((held & low) != 0)
    low_edge = release < 127 ? release : 127;
  if((held & high) != 0)
    high_edge = 255 - (release < 126 ? release : 126);

  if(v < low_edge)
    out |= low;
  if(v > high_edge)
    out |= high;
  return out;
}

uint32_t input_model_stick(uint32_t held, uint32_t x, uint32_t y,
 uint32_t sensitivity)
{
  uint32_t threshold = 20 + sensitivity * 10;

  return stick_axis(held, x, threshold, INPUT_MODEL_STICK_LEFT,
   INPUT_MODEL_STICK_RIGHT) |
   stick_axis(held, y, threshold, INPUT_MODEL_STICK_UP,
   INPUT_MODEL_STICK_DOWN);
}
