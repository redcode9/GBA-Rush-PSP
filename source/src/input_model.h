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
 * Turbo trains and stick hysteresis, with no SDK calls. Input bit i is
 * gamepad_config_map[i] (button_psp_mask_to_config[i] in input.c).
 * Every result depends only on the state, the pad and the frame passed
 * in. Host-tested by tests/test_input_model.c.
 */

#ifndef INPUT_MODEL_H
#define INPUT_MODEL_H

#include <stdint.h>

#define INPUT_MODEL_INPUTS 16

/* Presses per second at 60 frames per second; a train is on for
 * speed + 1 frames, then off for as many. */
enum
{
  INPUT_MODEL_TURBO_30,
  INPUT_MODEL_TURBO_15,
  INPUT_MODEL_TURBO_10
};

/* A zeroed entry is no turbo; with a key it is 30 per second, Hold. */
typedef struct
{
  uint16_t key;    /* GBA keys the input pulses; 0: not a turbo input */
  uint8_t speed;   /* INPUT_MODEL_TURBO_* */
  uint8_t toggle;  /* 0: pulses while held; 1: a press starts, the next stops */
} InputModelTurbo;

/* A zeroed state has no train running. */
typedef struct
{
  uint32_t start_frame[INPUT_MODEL_INPUTS];
  uint32_t active;
  uint32_t prev_pad;
} InputModelTurboState;

/* Returns normal_keys, the GBA keys of the normal inputs, with the keys
 * of the turbo trains that are on ORed in; a turbo off frame never
 * clears a key a normal input holds. pad holds the 16 inputs held now;
 * frame is the emulated frame counter, which may wrap. Calling again
 * with the same frame and pad gives the same keys. A train is on in the
 * frame of its press. turbo has INPUT_MODEL_INPUTS entries. */
uint32_t input_model_turbo(InputModelTurboState *state, uint32_t pad,
 uint32_t frame, const InputModelTurbo *turbo, uint32_t normal_keys);
/* Stops every train, for the menu, state loads and sleep; an input
 * still held starts again only on a new press. */
void input_model_turbo_stop(InputModelTurboState *state);

/* Stick directions as bits, in the order of inputs 12 to 15. */
#define INPUT_MODEL_STICK_UP (1u << 0)
#define INPUT_MODEL_STICK_DOWN (1u << 1)
#define INPUT_MODEL_STICK_LEFT (1u << 2)
#define INPUT_MODEL_STICK_RIGHT (1u << 3)
/* How far past its threshold a held direction must come back. */
#define INPUT_MODEL_STICK_HYSTERESIS 16

/* Returns the directions held after reading the stick at x, y (0-255,
 * 0 is left and up), given the ones held before. A direction is pressed
 * past threshold 20 + sensitivity * 10 from its edge and released only
 * INPUT_MODEL_STICK_HYSTERESIS nearer the centre, never past 127 or
 * 129. */
uint32_t input_model_stick(uint32_t held, uint32_t x, uint32_t y,
 uint32_t sensitivity);

#endif
