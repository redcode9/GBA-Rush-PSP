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
   frame, raises the keypad IRQ and handles hotkeys (menu, states, FPS,
   fast-forward); get_gui_input turns the pad into menu actions with key
   repeat. */

#include "common.h"

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
#include "input_trace.h"
#define DIAGNOSTIC_TRACE_EVENT_CAPACITY 16384U

static InputTraceEvent diagnostic_trace_events[DIAGNOSTIC_TRACE_EVENT_CAPACITY];
static InputTrace diagnostic_trace;
static u32 diagnostic_trace_frame;
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

static u32 fast_forward_button_mask = PSP_CTRL_TRIANGLE;

/* Collects the PSP buttons mapped to fast-forward; call after
   gamepad_config_map changes. */
void rebuild_fast_forward_button_mask(void)
{
  u32 i;

  fast_forward_button_mask = 0;
  for(i = 0; i < 16; i++)
  {
    if(gamepad_config_map[i] == BUTTON_ID_FASTFORWARD)
      fast_forward_button_mask |= button_psp_mask_to_config[i];
  }
}

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

/* Flips on every rapid-fire key handled, so rapid-fire keys alternate
   between pressed and released from frame to frame. */
u32 rapidfire_flag = 1;

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

/* Called at every GBA V-blank and during link waits. Returns nonzero when
   the menu or a state load changed the machine and update_gba must not
   continue its pass. */
u32 update_input(void)
{
  SceCtrlData ctrl_data = { 0, };
  u32 buttons;
  u32 non_repeat_buttons;
  u32 button_id = 0;
  u32 i;
  u32 new_key = 0;
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
      if (ctrl_data.Lx < analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_LEFT;

      if (ctrl_data.Lx > inv_analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_RIGHT;

      if (ctrl_data.Ly < analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_UP;

      if (ctrl_data.Ly > inv_analog_sensitivity)
        buttons |= PSP_CTRL_ANALOG_DOWN;
    }
  }

  non_repeat_buttons = (last_buttons ^ buttons) & buttons;
  last_buttons = buttons;

  main_fast_forward_input(non_repeat_buttons & fast_forward_button_mask,
   buttons & fast_forward_button_mask);

  if ((enable_home_menu != 0) && ((non_repeat_buttons & PSP_CTRL_HOME) != 0))
    return menu();

  for (i = 0; i < 16; i++)
  {
    if ((non_repeat_buttons & button_psp_mask_to_config[i]) != 0)
    {
      switch (gamepad_config_map[i])
      {
        case BUTTON_ID_MENU:
          return menu();

        case BUTTON_ID_LOADSTATE:
          action_loadstate();
          return 1;

        case BUTTON_ID_SAVESTATE:
          action_savestate();
          return 0;

        case BUTTON_ID_FASTFORWARD:
          break;

        case BUTTON_ID_FPS:
          psp_fps_debug ^= 1;
          return 0;
      }
    }

    if ((buttons & button_psp_mask_to_config[i]) != 0)
    {
      button_id = gamepad_config_map[i];

      if (button_id < BUTTON_ID_MENU)
      {
        new_key |= button_id_to_gba_mask[button_id];
      }
      else
      {
        if ((button_id >= BUTTON_ID_RAPIDFIRE_A) && (button_id <= BUTTON_ID_RAPIDFIRE_R))
        {
          rapidfire_flag ^= 1;

          if (rapidfire_flag != 0)
          {
            new_key |= button_id_to_gba_mask[button_id - BUTTON_ID_RAPIDFIRE_A + BUTTON_ID_A];
          }
          else
          {
            new_key &= ~button_id_to_gba_mask[button_id - BUTTON_ID_RAPIDFIRE_A + BUTTON_ID_A];
          }
        }
      }
    }
  }

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  new_key = input_trace_apply(&diagnostic_trace, diagnostic_trace_frame,
   (u16)new_key);
  diagnostic_trace_frame++;
#endif

  // Only a newly pressed key can trigger the keypad IRQ
  if ((new_key | gba_keys_held) != gba_keys_held)
    trigger_key(new_key);

  gba_keys_held = new_key;
  // KEYINPUT is active low
  pIO_REG(REG_P1) = (~gba_keys_held) & 0x3FF;

  return 0;
}


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

void init_input_kernel(void)
{
  kuCtrlSetSamplingCycle(0);
  kuCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  __sceCtrlPeekBufferPositive = kuCtrlPeekBufferPositive;
  __sceCtrlReadBufferPositive = kuCtrlReadBufferPositive;
}

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
int input_diagnostic_trace_begin_record(void)
{
  return input_trace_begin_record_from_header(&diagnostic_trace,
   diagnostic_trace_frame, gamepak_header, gamepak_file_size,
   (u16)gba_keys_held);
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

int input_diagnostic_trace_begin_replay(const u8 *encoded,
 size_t encoded_size)
{
  return input_trace_begin_replay_from_header(&diagnostic_trace, encoded,
   encoded_size, gamepak_header, gamepak_file_size, diagnostic_trace_frame);
}

void input_diagnostic_trace_cancel(void)
{
  input_trace_cancel(&diagnostic_trace);
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

size_t input_diagnostic_trace_max_encoded_size(void)
{
  return INPUT_TRACE_HEADER_SIZE +
   DIAGNOSTIC_TRACE_EVENT_CAPACITY * INPUT_TRACE_EVENT_SIZE;
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
