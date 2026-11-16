/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2007 takka <takka@tfact.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* PSP controller input. update_input maps the pad to the GBA keys once per
   frame, raises the keypad IRQ and handles hotkeys and shortcut chords
   (menu, states, FPS, fast-forward); get_gui_input turns the pad into menu
   actions with key repeat. */

#include "common.h"
#include "config_model.h"
#include "input_chord.h"
#include "input_model.h"
#include "perf_monitor.h"
#include "toast.h"

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
#include "input_trace.h"
#define DIAGNOSTIC_TRACE_EVENT_CAPACITY 16384U

static InputTraceEvent diagnostic_trace_events[DIAGNOSTIC_TRACE_EVENT_CAPACITY];
static InputTrace diagnostic_trace;
static u32 diagnostic_trace_frame;

/* In STATE_HASH_* order. */
static const char *const diagnostic_hash_names[STATE_HASH_REGION_COUNT] =
{
  "regs", "iwram", "ewram", "vram", "oam", "palette", "io", "backup", "audio"
};
/* records is NULL while no replay hashes are attached. */
static StateHashLog diagnostic_hashes;
static u32 diagnostic_perturb;
static u32 diagnostic_perturb_frame;
/* Set at the last frame of a benchmark that replays the trace, until
   input_diagnostic_trace_take_bench_hash runs at that frame's end. */
static u32 diagnostic_bench_hash_due;
#endif

extern u32 option_swap_confirm_buttons;


/* The digital buttons; drops HOME, HOLD and higher bits such as the analog
   flags below. */
#define PSP_ALL_BUTTON_MASK 0xFFFF

/* Menu key repeat delay and rate, in microseconds (SceCtrlData.TimeStamp). */
#define BUTTON_REPEAT_START    200000
#define BUTTON_REPEAT_CONTINUE 50000

/* Stick directions as extra button bits, unused by the PSP button mask. */
#define PSP_CTRL_ANALOG_UP    (1 << 28)
#define PSP_CTRL_ANALOG_DOWN  (1 << 29)
#define PSP_CTRL_ANALOG_LEFT  (1 << 30)
#define PSP_CTRL_ANALOG_RIGHT (1 << 31)


/* User-mode controller calls, or kubridge's kernel-mode ones after
   init_input_kernel. */
static int (*__sceCtrlPeekBufferPositive)(SceCtrlData *pad_data, int count);
static int (*__sceCtrlReadBufferPositive)(SceCtrlData *pad_data, int count);

/* GBA keys held this frame, as BUTTON_* bits (1 = pressed). */
u32 gba_keys_held = 0;

/* The BUTTON_ID_* action of each PSP button, indexed like
   button_psp_mask_to_config. Saved with the settings. */
u32 ALIGN_DATA gamepad_config_map[16] =
{
  BUTTON_ID_FASTFORWARD,		//0
  BUTTON_ID_A,		//1
  BUTTON_ID_B,		//2
  BUTTON_ID_FPS,	//3
  BUTTON_ID_L,		//4
  BUTTON_ID_R,		//5
  BUTTON_ID_DOWN,		//6
  BUTTON_ID_LEFT,		//7
  BUTTON_ID_UP,		//8
  BUTTON_ID_RIGHT,	//9
  BUTTON_ID_SELECT,	//10
  BUTTON_ID_START,	//11
  BUTTON_ID_UP,		//12
  BUTTON_ID_DOWN,		//13
  BUTTON_ID_LEFT,		//14
  BUTTON_ID_RIGHT		//15
};

/* PSP button for each gamepad_config_map index. */
const u32 ALIGN_DATA button_psp_mask_to_config[] =
{
  PSP_CTRL_TRIANGLE,		//0
  PSP_CTRL_CIRCLE,		//1
  PSP_CTRL_CROSS,			//2
  PSP_CTRL_SQUARE,		//3
  PSP_CTRL_LTRIGGER,		//4
  PSP_CTRL_RTRIGGER,		//5
  PSP_CTRL_DOWN,			//6
  PSP_CTRL_LEFT,			//7
  PSP_CTRL_UP,			//8
  PSP_CTRL_RIGHT,			//9
  PSP_CTRL_SELECT,		//10
  PSP_CTRL_START,			//11
  PSP_CTRL_ANALOG_UP,		//12
  PSP_CTRL_ANALOG_DOWN,	//13
  PSP_CTRL_ANALOG_LEFT,	//14
  PSP_CTRL_ANALOG_RIGHT	//15
};

/* GBA key of each BUTTON_ID_*; IDs from BUTTON_ID_MENU on press no key. */
const u32 ALIGN_DATA button_id_to_gba_mask[] =
{
  BUTTON_UP,
  BUTTON_DOWN,
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_A,
  BUTTON_B,
  BUTTON_L,
  BUTTON_R,
  BUTTON_START,
  BUTTON_SELECT,
  BUTTON_NONE,
  BUTTON_NONE,
  BUTTON_NONE,
  BUTTON_NONE
};

static u32 fast_forward_button_mask = PSP_CTRL_TRIANGLE;

/* Shortcut chords: slot n holds the input mask of chord action n
   (input_chord.h), as stored in the settings. */
u32 option_shortcut[CONFIG_MODEL_SHORTCUT_COUNT];
typedef char ShortcutActionsMustFit[
 ((u32)INPUT_CHORD_ACTION_COUNT <= CONFIG_MODEL_SHORTCUT_COUNT) &&
 (INPUT_CHORD_ACTION_COUNT <= INPUT_CHORD_MAX) ? 1 : -1];
static InputChordState shortcut_chords;
/* A fast-forward chord stays held from the read it fires on until one of
   its keys is let go, as a fast-forward button would. */
static u32 shortcut_fast_forward_held = 0;
/* The inputs mapped to GBA A or B: a hold chord does not count while one
   of them is held, so A+B+START+SELECT still resets the game. */
static u32 shortcut_block_inputs = 0;

/* Turbo A, B, L and R, in BUTTON_ID_RAPIDFIRE_* order, as stored in the
   settings: speed INPUT_MODEL_TURBO_*, mode 0 Hold or 1 Toggle. */
u32 option_turbo_speed[CONFIG_MODEL_TURBO_COUNT];
u32 option_turbo_mode[CONFIG_MODEL_TURBO_COUNT];

/* The inputs mapped to BUTTON_ID_RAPIDFIRE_* pulse their GBA key, each
   with its own train, at the speed and in the mode of that turbo. */
static InputModelTurbo turbo_inputs[INPUT_MODEL_INPUTS];
static InputModelTurboState turbo_state;
/* Emulated frames, counted by input_frame_tick. */
static u32 turbo_frame = 0;
/* Stick directions held after the last read, as INPUT_MODEL_STICK_* bits. */
static u32 stick_held = 0;

/* Collects the PSP buttons mapped to fast-forward, the inputs that block
   held chords and the turbo inputs; call after gamepad_config_map or the
   turbo options change. */
void rebuild_fast_forward_button_mask(void)
{
  u32 i;

  fast_forward_button_mask = 0;
  shortcut_block_inputs = 0;
  for(i = 0; i < 16; i++)
  {
    if(gamepad_config_map[i] == BUTTON_ID_FASTFORWARD)
      fast_forward_button_mask |= button_psp_mask_to_config[i];
    if((gamepad_config_map[i] == BUTTON_ID_A) ||
       (gamepad_config_map[i] == BUTTON_ID_B) ||
       (gamepad_config_map[i] == BUTTON_ID_RAPIDFIRE_A) ||
       (gamepad_config_map[i] == BUTTON_ID_RAPIDFIRE_B))
      shortcut_block_inputs |= 1u << i;

    turbo_inputs[i].key = 0;
    if((gamepad_config_map[i] >= BUTTON_ID_RAPIDFIRE_A) &&
       (gamepad_config_map[i] <= BUTTON_ID_RAPIDFIRE_R))
    {
      u32 turbo = gamepad_config_map[i] - BUTTON_ID_RAPIDFIRE_A;

      turbo_inputs[i].key = button_id_to_gba_mask[turbo + BUTTON_ID_A];
      turbo_inputs[i].speed = option_turbo_speed[turbo];
      turbo_inputs[i].toggle = option_turbo_mode[turbo];
    }
  }
}

/* Loads option_shortcut into the chord table; call after it changes. A
   table with two equal chords falls back to the default chords. */
void rebuild_shortcut_chords(void)
{
  InputChord table[INPUT_CHORD_ACTION_COUNT];
  u32 i;

  for(i = 0; i < INPUT_CHORD_ACTION_COUNT; i++)
  {
    table[i] = input_chord_defaults[i];
    table[i].mask = option_shortcut[i];
  }

  if(input_chord_validate(table, INPUT_CHORD_ACTION_COUNT))
    input_chord_init(&shortcut_chords, table, INPUT_CHORD_ACTION_COUNT);
  else
    input_chord_init(&shortcut_chords, input_chord_defaults,
     INPUT_CHORD_DEFAULT_COUNT);
}

typedef enum
{
  BUTTON_NOT_HELD,
  BUTTON_HELD_INITIAL,
  BUTTON_HELD_REPEAT
} BUTTON_REPEAT_STATE_TYPE;

/* Key repeat state for get_gui_input. */
BUTTON_REPEAT_STATE_TYPE button_repeat_state = BUTTON_NOT_HELD;
u32 button_repeat = 0;
GUI_ACTION_TYPE cursor_repeat = CURSOR_NONE;

#define BUTTON_REPEAT_MASK \
 (PSP_CTRL_UP | PSP_CTRL_RIGHT | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)

/* Buttons of the previous read, to detect new presses. */
u32 last_buttons = 0;
u32 button_repeat_timestamp;

/* Buttons still held when the menu closed; update_input hides each one from
   the game and the hotkeys until it is released. */
static u32 menu_held_buttons = 0;

/* Tilt cartridges read 12-bit values; 0x800 is level. */
u32 enable_tilt_sensor = 0;
u32 tilt_sensorX = 0x800;
u32 tilt_sensorY = 0x800;

static void trigger_key(u32 keys_down);


/* Raises the keypad IRQ if KEYCNT asks for it. KEYCNT (REG_P1CNT) bits 0-9
   select keys, bit 14 enables the IRQ, bit 15 requires all selected keys
   (AND) instead of any (OR). In STOP mode the check runs regardless. */
static void trigger_key(u32 keys_down)
{
  u16 p1_cnt = pIO_REG(REG_P1CNT);

  if (((p1_cnt & 0x4000) != 0) || (reg[CPU_HALT_STATE] == CPU_STOP))
  {
    u16 key_intersection = (p1_cnt & keys_down) & 0x3FF;

    if ((p1_cnt & 0x8000) != 0)
    {
      if (key_intersection == (p1_cnt & 0x3FF))
        pIO_REG(REG_IF) |= IRQ_KEYPAD;
    }
    else
    {
      if (key_intersection != 0)
        pIO_REG(REG_IF) |= IRQ_KEYPAD;
    }
  }
}


/* The PSP buttons in mask held right now, read without blocking and
   without stick or key mapping; 0 before init_input. */
u32 get_pad_input(u32 mask)
{
  if (__sceCtrlPeekBufferPositive)
  {
    SceCtrlData ctrl_data;
    (*__sceCtrlPeekBufferPositive)(&ctrl_data, 1);
    return ctrl_data.Buttons & mask;
  }

  return 0;
}

/* Hides buttons (the keys held as the menu closes) from update_input until
   each is released, so closing the menu never waits for the keys to go up. */
void input_hold_until_released(u32 buttons)
{
  menu_held_buttons = buttons;
}


/* Menu input: one action per new press, the last match below winning, plus
   key repeat for the d-pad and shoulder buttons. Also serves power events. */
GUI_ACTION_TYPE get_gui_input(void)
{
  SceCtrlData ctrl_data;
  GUI_ACTION_TYPE new_button = CURSOR_NONE;
  u32 new_buttons;
  // Stick axes run 0-255; within this distance of either end counts as a press
  u32 analog_sensitivity = 20 + (option_analog_sensitivity * 10);
  u32 inv_analog_sensitivity = 255 - analog_sensitivity;

  main_power_service();

  sceCtrlReadBufferPositive(&ctrl_data, 1);

  if ((option_enable_analog != 0) && ((ctrl_data.Buttons & PSP_CTRL_HOLD) == 0))
  {
    if (ctrl_data.Lx < analog_sensitivity)
      ctrl_data.Buttons |= PSP_CTRL_LEFT;

    if (ctrl_data.Lx > inv_analog_sensitivity)
      ctrl_data.Buttons |= PSP_CTRL_RIGHT;

    if (ctrl_data.Ly < analog_sensitivity)
      ctrl_data.Buttons |= PSP_CTRL_UP;

    if (ctrl_data.Ly > inv_analog_sensitivity)
      ctrl_data.Buttons |= PSP_CTRL_DOWN;
  }

  ctrl_data.Buttons &= PSP_ALL_BUTTON_MASK;

  new_buttons = (last_buttons ^ ctrl_data.Buttons) & ctrl_data.Buttons;
  last_buttons = ctrl_data.Buttons;

  if ((new_buttons & PSP_CTRL_RTRIGGER) != 0)
    new_button = CURSOR_RTRIGGER;

  if ((new_buttons & PSP_CTRL_LTRIGGER) != 0)
    new_button = CURSOR_LTRIGGER;

  if ((new_buttons & PSP_CTRL_LEFT) != 0)
    new_button = CURSOR_LEFT;

  if ((new_buttons & PSP_CTRL_RIGHT) != 0)
    new_button = CURSOR_RIGHT;

  if ((new_buttons & PSP_CTRL_UP) != 0)
    new_button = CURSOR_UP;

  if ((new_buttons & PSP_CTRL_DOWN) != 0)
    new_button = CURSOR_DOWN;

  if ((new_buttons & PSP_CTRL_START) != 0)
    new_button = CURSOR_SELECT;

  if ((new_buttons & PSP_CTRL_SELECT) != 0)
    new_button = CURSOR_DEFAULT;

  if (option_swap_confirm_buttons == 0)
  {
    if ((new_buttons & PSP_CTRL_CIRCLE) != 0)
      new_button = CURSOR_SELECT;

    if ((new_buttons & PSP_CTRL_CROSS) != 0)
      new_button = CURSOR_EXIT;
  }
  else
  {
    if ((new_buttons & PSP_CTRL_CROSS) != 0)
      new_button = CURSOR_SELECT;

    if ((new_buttons & PSP_CTRL_CIRCLE) != 0)
      new_button = CURSOR_EXIT;
  }

  if ((new_buttons & PSP_CTRL_SQUARE) != 0)
    new_button = CURSOR_BACK;

  if (new_button != CURSOR_NONE)
  {
    button_repeat_timestamp = ctrl_data.TimeStamp;
    button_repeat_state = BUTTON_HELD_INITIAL;
    button_repeat = new_buttons & BUTTON_REPEAT_MASK;
    cursor_repeat = new_button;
  }
  else
  {
    if ((ctrl_data.Buttons & button_repeat) != 0)
    {
      u32 new_ticks = ctrl_data.TimeStamp;

      if (button_repeat_state == BUTTON_HELD_INITIAL)
      {
        if ((new_ticks - button_repeat_timestamp) > BUTTON_REPEAT_START)
        {
          new_button = cursor_repeat;
          button_repeat_timestamp = new_ticks;
          button_repeat_state = BUTTON_HELD_REPEAT;
        }
      }

      if (button_repeat_state == BUTTON_HELD_REPEAT)
      {
        if ((new_ticks - button_repeat_timestamp) > BUTTON_REPEAT_CONTINUE)
        {
          new_button = cursor_repeat;
          button_repeat_timestamp = new_ticks;
        }
      }
    }
  }

  return new_button;
}

/* Like get_gui_input, but returns CURSOR_BACK once the PSP button at
   gamepad_config_map index button_id is no longer held. */
GUI_ACTION_TYPE get_gui_input_fs_hold(u32 button_id)
{
  GUI_ACTION_TYPE new_button = get_gui_input();

  if ((last_buttons & button_psp_mask_to_config[button_id]) == 0)
    return CURSOR_BACK;

  return new_button;
}

/* The 16 inputs held in buttons, bit n for gamepad_config_map index n;
   buttons carries the stick directions as PSP_CTRL_ANALOG_* bits. */
static u32 buttons_to_inputs(u32 buttons)
{
  u32 inputs = 0;
  u32 i;

  for (i = 0; i < 16; i++)
  {
    if ((buttons & button_psp_mask_to_config[i]) != 0)
      inputs |= 1u << i;
  }

  return inputs;
}

/* Advances the emulated frame count that turbo trains follow; update_gba
   calls it once per frame, never in link waits. */
void input_frame_tick(void)
{
  turbo_frame++;
}

/* Stops every turbo train, for the menu, state loads and sleep; an input
   still held pulses again only after a new press. */
void input_turbo_stop(void)
{
  input_model_turbo_stop(&turbo_state);
}

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
extern u32 cpu_ticks;

/* Hashes the machine into log's next record, as this frame: what a
   savestate stores, except reg[] past REG_CPSR other than CPU_MODE and
   CPU_HALT_STATE. Those are unused slots, the stubs' scratch (REG_SAVE..3,
   GP_SAVE, REG_TMP: host addresses), the dynarec's CHANGED_PC_STATUS and
   EXECUTE_CYCLES, and the DMA open-bus latch CPU_DMA_HACK/CPU_DMA_LAST,
   left out until a hardware self-check shows them deterministic. audio is
   the hash of the frame's samples. 0 when the log is full. */
static int input_diagnostic_trace_capture(StateHashLog *log, u32 audio)
{
  u32 backup_size;
  const u8 *backup = memory_backup_bytes(&backup_size);

  /* The New core keeps its registers in its own state until asked. */
  arm_gba_save_registers();
  {
    const StateHashSpan spans[] =
    {
      { reg, (REG_CPSR + 1) * sizeof(u32), STATE_HASH_REGS },
      { reg + CPU_MODE, (CPU_HALT_STATE - CPU_MODE + 1) * sizeof(u32),
        STATE_HASH_REGS },
      { reg_mode, sizeof(reg_mode), STATE_HASH_REGS },
      { spsr, sizeof(spsr), STATE_HASH_REGS },
      { iwram, sizeof(iwram), STATE_HASH_IWRAM },
      { ewram, sizeof(ewram), STATE_HASH_EWRAM },
      { vram, sizeof(vram), STATE_HASH_VRAM },
      { oam_ram, sizeof(oam_ram), STATE_HASH_OAM },
      { palette_ram, sizeof(palette_ram), STATE_HASH_PALETTE },
      /* The registers, not the io_readable flags after them. */
      { io_registers, 0x400, STATE_HASH_IO },
      { backup, backup_size, STATE_HASH_BACKUP },
      { &audio, sizeof(audio), STATE_HASH_AUDIO }
    };

    return state_hash_capture(log, diagnostic_trace_frame, spans,
     sizeof(spans) / sizeof(spans[0]));
  }
}

/* Hashes the machine of this replayed frame into the replay's next
   record. */
static void input_diagnostic_trace_hash_frame(void)
{
  /* The perturbation run's flipped bit. It goes straight into the array,
     past the dynarec's code checks: enough for the hashes to see it. */
  if(diagnostic_perturb &&
     diagnostic_trace_frame - diagnostic_hashes.start_frame ==
     diagnostic_perturb_frame)
    iwram[0] ^= 1;

  /* Up to this VBlank's tick; update_gba's own VBlank update, at the same
     tick, then mixes nothing. */
  input_diagnostic_trace_capture(&diagnostic_hashes,
   sound_diagnostic_audio_take(cpu_ticks));
}

/* update_gba calls this once per emulated frame, after the VBlank read of
   update_input, which link waits call too: the replay's hash of the frame,
   the end of a benchmark that replays the trace, and the frame count the
   trace records and replays by. A pass the menu or a state load dropped
   runs no frame and does not call it. */
void input_diagnostic_trace_frame_end(void)
{
  /* Until the replay passes its last frame and turns FINISHED. */
  if(diagnostic_hashes.records != NULL &&
     diagnostic_trace.state == INPUT_TRACE_REPLAYING)
    input_diagnostic_trace_hash_frame();
  // A benchmark that replays the trace ends with the trace's last frame
  if(input_trace_replay_last_frame(&diagnostic_trace, diagnostic_trace_frame)
     && perf_monitor_bench_running())
  {
    perf_monitor_bench_end_with_frame();
    diagnostic_bench_hash_due = 1;
  }
  diagnostic_trace_frame++;
}

/* update_gba calls this at every frame end. Once the benchmark that
   replays the trace has timed its last frame, it returns 1 and one hash
   of the guest state, the replay's regions without the audio (the last),
   which a benchmark does not hash; it hashes nothing on other frames, nor
   when the bench or the trace was cancelled. */
int input_diagnostic_trace_take_bench_hash(u32 *hash)
{
  StateHashRecord record;
  StateHashLog log;

  if(!diagnostic_bench_hash_due)
    return 0;
  diagnostic_bench_hash_due = 0;
  if(diagnostic_trace.state != INPUT_TRACE_REPLAYING ||
     !state_hash_log_init(&log, &record, 1, diagnostic_hash_names,
      GBA_RUSH_BUILD_ID, diagnostic_trace.game_code,
      diagnostic_trace.rom_revision, diagnostic_trace_frame) ||
     !input_diagnostic_trace_capture(&log, 0))
    return 0;
  *hash = state_hash_words(STATE_HASH_FNV_BASIS, record.hash,
   STATE_HASH_AUDIO * sizeof(record.hash[0]));
  return 1;
}
#endif

/* Called at every GBA V-blank and during link waits. Returns nonzero when
   the menu or a state load changed the machine and update_gba must not
   continue its pass. */
u32 update_input(void)
{
  SceCtrlData ctrl_data = { 0, };
  u32 buttons;
  u32 non_repeat_buttons;
  u32 inputs;
  u32 shortcut_actions;
  u32 fast_forward_chord;
  uint32_t hidden_inputs;
  uint32_t turbo_flips;
  u32 button_id = 0;
  u32 menu_result;
  u32 i;
  u32 new_key = 0;
  u32 stick = 0;
  u32 analog_sensitivity = 20 + (option_analog_sensitivity * 10);
  u32 inv_analog_sensitivity = 255 - analog_sensitivity;

  tilt_sensorX = 0x800;
  tilt_sensorY = 0x800;

  if (__sceCtrlPeekBufferPositive)
    (*__sceCtrlPeekBufferPositive)(&ctrl_data, 1);

  // The digital buttons plus HOME (0x10000) and HOLD (0x20000)
  buttons = ctrl_data.Buttons & 0x0003FFFF;

  if ((option_enable_analog != 0) && ((ctrl_data.Buttons & PSP_CTRL_HOLD) == 0))
  {
    // Tilt carts get the inverted stick position instead of d-pad presses
    if (enable_tilt_sensor != 0)
    {
      if ((ctrl_data.Lx < analog_sensitivity) || (ctrl_data.Lx > inv_analog_sensitivity))
      {
        tilt_sensorX = (0xFF - ctrl_data.Lx) << 4;
      }

      if ((ctrl_data.Ly < analog_sensitivity) || (ctrl_data.Ly > inv_analog_sensitivity))
      {
        tilt_sensorY = (0xFF - ctrl_data.Ly) << 4;
      }
    }
    else
    {
      // A held direction is let go only nearer the centre, so noise at
      // the threshold does not flicker it
      stick = input_model_stick(stick_held, ctrl_data.Lx, ctrl_data.Ly,
       option_analog_sensitivity);

      if ((stick & INPUT_MODEL_STICK_LEFT) != 0)
        buttons |= PSP_CTRL_ANALOG_LEFT;

      if ((stick & INPUT_MODEL_STICK_RIGHT) != 0)
        buttons |= PSP_CTRL_ANALOG_RIGHT;

      if ((stick & INPUT_MODEL_STICK_UP) != 0)
        buttons |= PSP_CTRL_ANALOG_UP;

      if ((stick & INPUT_MODEL_STICK_DOWN) != 0)
        buttons |= PSP_CTRL_ANALOG_DOWN;
    }
  }
  stick_held = stick;

  // A key held since the menu closed counts as up until it is released,
  // for chords and turbo too
  menu_held_buttons &= buttons;
  buttons &= ~menu_held_buttons;

  // Decided on this read, so a chord adds no latency; holds count real time
  inputs = buttons_to_inputs(buttons);
  shortcut_actions = input_chord_eval(&shortcut_chords, inputs,
   buttons_to_inputs(last_buttons), sceKernelGetSystemTimeLow(),
   shortcut_block_inputs, &hidden_inputs);

  fast_forward_chord = shortcut_chords.chord[INPUT_CHORD_FASTFORWARD].mask;
  shortcut_fast_forward_held =
   ((shortcut_actions & (1u << INPUT_CHORD_FASTFORWARD)) != 0) ||
   ((shortcut_fast_forward_held != 0) && (fast_forward_chord != 0) &&
    ((inputs & fast_forward_chord) == fast_forward_chord));

  non_repeat_buttons = (last_buttons ^ buttons) & buttons;
  last_buttons = buttons;

  // The keys of a fired chord reach neither the game nor single-key mappings
  for (i = 0; i < 16; i++)
  {
    if ((hidden_inputs & (1u << i)) != 0)
      buttons &= ~button_psp_mask_to_config[i];
  }
  non_repeat_buttons &= buttons;

  main_fast_forward_input((non_repeat_buttons & fast_forward_button_mask) |
   (shortcut_actions & (1u << INPUT_CHORD_FASTFORWARD)),
   (buttons & fast_forward_button_mask) | shortcut_fast_forward_held);

  if ((enable_home_menu != 0) && ((non_repeat_buttons & PSP_CTRL_HOME) != 0))
  {
    input_turbo_stop();
    menu_result = menu();
    if (menu_result != 0)
      return menu_result;
    new_key = gba_keys_held;
    goto game_keys;
  }

  // Same effects as the BUTTON_ID_ mappings below
  if ((shortcut_actions & (1u << INPUT_CHORD_SAVESTATE)) != 0)
  {
    main_perf_bench_cancel();
    action_savestate();
    new_key = gba_keys_held;
    goto game_keys;
  }

  if ((shortcut_actions & (1u << INPUT_CHORD_LOADSTATE)) != 0)
  {
    input_turbo_stop();
    main_perf_bench_cancel();
    action_loadstate();
    return 1;
  }

  // No hold ever closes the game: the menu hold only opens the menu
  if ((shortcut_actions & (1u << INPUT_CHORD_MENU)) != 0)
  {
    input_turbo_stop();
    menu_result = menu();
    if (menu_result != 0)
      return menu_result;
    new_key = gba_keys_held;
    goto game_keys;
  }

  if ((shortcut_actions & (1u << INPUT_CHORD_FPS)) != 0)
  {
    psp_fps_debug ^= 1;
    new_key = gba_keys_held;
    goto game_keys;
  }

  if ((shortcut_actions & (1u << INPUT_CHORD_VIDEO_CYCLE)) != 0)
  {
    menu_cycle_video_mode();
    new_key = gba_keys_held;
    goto game_keys;
  }

  for (i = 0; i < 16; i++)
  {
    if ((non_repeat_buttons & button_psp_mask_to_config[i]) != 0)
    {
      switch (gamepad_config_map[i])
      {
        case BUTTON_ID_MENU:
          input_turbo_stop();
          menu_result = menu();
          if (menu_result != 0)
            return menu_result;
          new_key = gba_keys_held;
          goto game_keys;

        case BUTTON_ID_LOADSTATE:
          input_turbo_stop();
          main_perf_bench_cancel();
          action_loadstate();
          return 1;

        case BUTTON_ID_SAVESTATE:
          main_perf_bench_cancel();
          action_savestate();
          new_key = gba_keys_held;
          goto game_keys;

        case BUTTON_ID_FASTFORWARD:
          break;

        case BUTTON_ID_FPS:
          psp_fps_debug ^= 1;
          new_key = gba_keys_held;
          goto game_keys;
      }
    }

    if ((buttons & button_psp_mask_to_config[i]) != 0)
    {
      button_id = gamepad_config_map[i];

      if (button_id < BUTTON_ID_MENU)
      {
        new_key |= button_id_to_gba_mask[button_id];
      }
    }
  }

  // Turbo follows emulated frames, so the reads of a link wait repeat the
  // same keys
  new_key = input_model_turbo(&turbo_state, inputs & ~hidden_inputs,
   turbo_frame, turbo_inputs, new_key, &turbo_flips);

  // Each press of a Toggle turbo says whether it is now on or off; the
  // newest notice drops those still queued, which may name an old state
  if (turbo_flips != 0)
    toast_clear();

  for (i = 0; turbo_flips != 0; i++, turbo_flips >>= 1)
  {
    if ((turbo_flips & 1) != 0)
      toast_show(((turbo_state.active >> i) & 1) != 0 ? MSG_TURBO_ON :
       MSG_TURBO_OFF, "ABLR"[gamepad_config_map[i] - BUTTON_ID_RAPIDFIRE_A]);
  }

  // A hotkey or a menu that changed no machine keeps the game's keys, but
  // its frame still runs: the input trace records or replays it too
game_keys:
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  new_key = input_trace_apply(&diagnostic_trace, diagnostic_trace_frame,
   (u16)new_key);
#endif

  // The unbound benchmark runs the loaded slot with the tilt sensor centred
  // and no keys held, or the keys of the trace it replays
  if (perf_monitor_bench_running())
  {
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
    if (diagnostic_trace.state != INPUT_TRACE_REPLAYING)
      new_key = 0;
#else
    new_key = 0;
#endif
    tilt_sensorX = 0x800;
    tilt_sensorY = 0x800;
  }

  // Only a newly pressed key can trigger the keypad IRQ
  if ((new_key | gba_keys_held) != gba_keys_held)
    trigger_key(new_key);

  gba_keys_held = new_key;
  // KEYINPUT is active low
  pIO_REG(REG_P1) = (~gba_keys_held) & 0x3FF;

  return 0;
}


/* Startup controller setup with the user-mode read calls;
   init_input_kernel() is the kubridge alternative. */
void init_input(void)
{
  // Sample once per display frame, with the analog stick on
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  rebuild_fast_forward_button_mask();

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  input_trace_init(&diagnostic_trace, diagnostic_trace_events,
   DIAGNOSTIC_TRACE_EVENT_CAPACITY);
  diagnostic_trace_frame = 0;
#endif

  __sceCtrlPeekBufferPositive = sceCtrlPeekBufferPositive;
  __sceCtrlReadBufferPositive = sceCtrlReadBufferPositive;
}

/* Switches pad sampling and reads to the ku_bridge kernel calls, which
   fail until init_ku_bridge. Skips init_input's mask and trace setup;
   nothing calls it at present. */
void init_input_kernel(void)
{
  kuCtrlSetSamplingCycle(0);
  kuCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  __sceCtrlPeekBufferPositive = kuCtrlPeekBufferPositive;
  __sceCtrlReadBufferPositive = kuCtrlReadBufferPositive;
}

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
/* GUI side of the one diagnostic trace. These pass the trace's frame count
   (input_diagnostic_trace_frame_end steps it) and the loaded ROM's header,
   which tags the trace with its game code and revision. */
int input_diagnostic_trace_begin_record(void)
{
  return input_trace_begin_record_from_header(&diagnostic_trace,
   diagnostic_trace_frame, gamepak_header, gamepak_file_size,
   (u16)gba_keys_held);
}

/* Names the .svs the recording starts from; rtc_base is Unix seconds. */
int input_diagnostic_trace_set_companion(u32 state_crc32, u32 state_size,
 u32 rtc_base)
{
  return input_trace_set_companion(&diagnostic_trace, state_crc32,
   state_size, rtc_base);
}

int input_diagnostic_trace_finish_record(void)
{
  return input_trace_finish_record(&diagnostic_trace,
   diagnostic_trace_frame);
}

int input_diagnostic_trace_encode(u8 *output, size_t output_size,
 size_t *written)
{
  return input_trace_encode(&diagnostic_trace, output, output_size, written);
}

/* Starts at the current frame; fails with an IDENTITY error unless the
   trace was recorded with this ROM's game code and revision. */
int input_diagnostic_trace_begin_replay(const u8 *encoded,
 size_t encoded_size)
{
  return input_trace_begin_replay_from_header(&diagnostic_trace, encoded,
   encoded_size, gamepak_header, gamepak_file_size, diagnostic_trace_frame);
}

/* After a replay begins: refuses a .svs other than the one a v2 trace
   names. */
int input_diagnostic_trace_check_state(u32 state_crc32, u32 state_size)
{
  return input_trace_check_state(&diagnostic_trace, state_crc32, state_size);
}

/* 1 or 2 for a loaded trace, 0 when idle. */
u32 input_diagnostic_trace_version(void)
{
  return diagnostic_trace.version;
}

/* The unbound benchmark replays the trace held here (just recorded, being
   replayed or finished) from its first frame, once it matches this ROM.
   Returns the frames the trace replays, or 0 when there is none. */
u32 input_diagnostic_trace_begin_bench(void)
{
  /* The bench replays from the machine as it is, not from the trace's
     .svs: hashes of it would be no replay to compare, and would slow it. */
  input_diagnostic_trace_hashes_free();
  return input_trace_restart_replay(&diagnostic_trace, gamepak_header,
   gamepak_file_size, diagnostic_trace_frame);
}

/* A cancelled benchmark stops replaying the trace, so the pad works again;
   the trace stays held for the next one, as after a finished replay. */
void input_diagnostic_trace_end_bench(void)
{
  input_trace_stop_replay(&diagnostic_trace);
}

/* The checksum field of the held trace's file, for the benchmark report. */
u32 input_diagnostic_trace_checksum(void)
{
  return diagnostic_trace.checksum;
}

void input_diagnostic_trace_cancel(void)
{
  input_trace_cancel(&diagnostic_trace);
  input_diagnostic_trace_hashes_free();
}

InputTraceState input_diagnostic_trace_state(void)
{
  return diagnostic_trace.state;
}

InputTraceError input_diagnostic_trace_error(void)
{
  return diagnostic_trace.error;
}

u32 input_diagnostic_trace_event_count(void)
{
  return diagnostic_trace.event_count;
}

/* A trace with every event slot used: the GUI's encode buffer size and
   the largest trace file it will load. */
size_t input_diagnostic_trace_max_encoded_size(void)
{
  return INPUT_TRACE_HEADER_SIZE +
   DIAGNOSTIC_TRACE_EVENT_CAPACITY * INPUT_TRACE_EVENT_SIZE;
}

/* The replay's last frame, relative to its first: it replays duration + 1
   frames. */
u32 input_diagnostic_trace_duration(void)
{
  return diagnostic_trace.duration_frames;
}

/* Call after a replay begins: the records carry its game, revision and
   first frame. */
void input_diagnostic_trace_hashes_attach(StateHashRecord *records,
 u32 capacity)
{
  input_diagnostic_trace_hashes_free();
  if(!state_hash_log_init(&diagnostic_hashes, records, capacity,
      diagnostic_hash_names, GBA_RUSH_BUILD_ID, diagnostic_trace.game_code,
      diagnostic_trace.rom_revision, diagnostic_trace.start_frame))
  {
    free(records);
    return;
  }
  sound_diagnostic_audio_enable(1);
}

const StateHashLog *input_diagnostic_trace_hashes(void)
{
  return diagnostic_hashes.records != NULL ? &diagnostic_hashes : NULL;
}

/* Also ends a perturbation: only the run that asked for it flips a bit. */
void input_diagnostic_trace_hashes_free(void)
{
  free(diagnostic_hashes.records);
  diagnostic_hashes.records = NULL;
  diagnostic_perturb = 0;
  sound_diagnostic_audio_enable(0);
}

/* Flips bit 0 of the first IWRAM byte at this frame of the replay
   (relative to its first), for a run that must diverge there. */
void input_diagnostic_trace_hashes_perturb(u32 frame)
{
  diagnostic_perturb = 1;
  diagnostic_perturb_frame = frame;
}
#endif


// type = READ / WRITE_MEM
#define INPUT_SAVESTATE_BODY(type)                                            \
{                                                                             \
  FILE_##type##_VARIABLE(savestate_file, gba_keys_held);                      \
}                                                                             \

void input_read_savestate(SceUID savestate_file)
{
  INPUT_SAVESTATE_BODY(READ);
}

void input_write_mem_savestate(SceUID savestate_file)
{
  INPUT_SAVESTATE_BODY(WRITE_MEM);
}
