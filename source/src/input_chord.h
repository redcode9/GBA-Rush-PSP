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
 * Shortcut chords: 1 to 4 of the 16 inputs held together, fired on a
 * press or after a hold, with no SDK calls and no added latency.
 * Input bit i is gamepad_config_map[i] (button_psp_mask_to_config[i]
 * in input.c): 0 Triangle, 1 Circle, 2 Cross, 3 Square, 4 L, 5 R,
 * 6 Down, 7 Left, 8 Up, 9 Right, 10 Select, 11 Start, 12-15 stick
 * up, down, left, right. Host-tested by tests/test_input_chord.c.
 */

#ifndef INPUT_CHORD_H
#define INPUT_CHORD_H

#include <stdint.h>

#define INPUT_CHORD_MAX 8
#define INPUT_CHORD_MAX_KEYS 4

#define INPUT_CHORD_KEY_L (1u << 4)
#define INPUT_CHORD_KEY_R (1u << 5)
#define INPUT_CHORD_KEY_SELECT (1u << 10)
#define INPUT_CHORD_KEY_START (1u << 11)
/* A chord with other keys fires when one of them is pressed while the
 * modifiers in it are held, never when a modifier is pressed last. */
#define INPUT_CHORD_MODIFIERS (INPUT_CHORD_KEY_SELECT | INPUT_CHORD_KEY_START)

/* Separate from BUTTON_ID_* so saved button maps keep their meaning.
 * New actions are appended, never inserted. */
enum
{
  INPUT_CHORD_SAVESTATE,
  INPUT_CHORD_LOADSTATE,
  INPUT_CHORD_MENU,
  INPUT_CHORD_FASTFORWARD,
  INPUT_CHORD_FPS,
  INPUT_CHORD_ACTION_COUNT
};

/* One default per action, in action order. */
#define INPUT_CHORD_DEFAULT_COUNT INPUT_CHORD_ACTION_COUNT

/* mask 0 means no chord; hold_ms 0 means the chord fires on a press. */
typedef struct
{
  uint8_t action;
  uint16_t mask;
  uint16_t hold_ms;
} InputChord;

typedef struct
{
  InputChord chord[INPUT_CHORD_MAX];
  uint32_t count;
  /* Bit j of contained_by[i]: chord j strictly contains chord i. */
  uint8_t contained_by[INPUT_CHORD_MAX];
  uint8_t hold_phase[INPUT_CHORD_MAX];
  uint32_t hold_start_us[INPUT_CHORD_MAX];
  uint16_t hidden;
} InputChordState;

/* SELECT+L saves, SELECT+R loads, START+SELECT held 500 ms opens the
 * menu; fast-forward and the FPS overlay have no chord. Entry n is
 * action n; its hold_ms is that action's fixed hold, also for a chord
 * the user sets. */
extern const InputChord input_chord_defaults[INPUT_CHORD_DEFAULT_COUNT];

/* Non-zero when count fits, every action exists, every chord has 0
 * (none) or 1 to INPUT_CHORD_MAX_KEYS keys and no two chords are equal. */
int input_chord_validate(const InputChord *table, uint32_t count);
/* Copies the first count (at most INPUT_CHORD_MAX) chords of a table
 * that passed input_chord_validate and clears the holds and the hidden
 * inputs. */
void input_chord_init(InputChordState *state, const InputChord *table,
 uint32_t count);
/* Returns the fired actions as bits (1u << action). pad and prev_pad
 * are this read and the previous one; now_us is a microsecond clock that
 * may wrap. A chord yields while a longer chord containing it is held.
 * A hold fires once hold_ms have passed, then again only after a
 * release; it does not count while an input of hold_block_mask that is
 * not in the chord is held.
 * *hide_mask gets the inputs to keep from the game: the non-modifier
 * keys of fired chords (all keys when a chord has only modifiers),
 * while they stay held. */
uint32_t input_chord_eval(InputChordState *state, uint32_t pad,
 uint32_t prev_pad, uint32_t now_us, uint32_t hold_block_mask,
 uint32_t *hide_mask);

#endif
