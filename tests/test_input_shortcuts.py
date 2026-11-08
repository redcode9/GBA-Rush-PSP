"""Check input.c's update_input on the host: the shortcut chords, turbo with
its settings and toasts, and the stick; and gui.c's video cycle, which a
chord runs."""

from pathlib import Path
import unittest

# Builds and runs a spliced C test with HOST_CC.
from tests.test_startup import ROOT, build_and_run, function


SRC = (Path(__file__).resolve().parents[1] / "source/src").as_posix()

# Stand-ins for the PSP SDK and for what update_input calls, with the real
# chord and input model modules. Counters record the actions that ran, and
# the toasts shown.
STUBS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "@SRC@/input_chord.c"
#include "@SRC@/input_model.c"
#include "@SRC@/config_model.h"
#include "@SRC@/message.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int SceUID;
typedef struct
{
  unsigned int TimeStamp;
  unsigned int Buttons;
  unsigned char Lx;
  unsigned char Ly;
  unsigned char Rsrv[6];
} SceCtrlData;
#include "@SRC@/input.h"
#define ALIGN_DATA
#define PSP_CTRL_SELECT 0x000001
#define PSP_CTRL_START 0x000008
#define PSP_CTRL_UP 0x000010
#define PSP_CTRL_RIGHT 0x000020
#define PSP_CTRL_DOWN 0x000040
#define PSP_CTRL_LEFT 0x000080
#define PSP_CTRL_LTRIGGER 0x000100
#define PSP_CTRL_RTRIGGER 0x000200
#define PSP_CTRL_TRIANGLE 0x001000
#define PSP_CTRL_CIRCLE 0x002000
#define PSP_CTRL_CROSS 0x004000
#define PSP_CTRL_SQUARE 0x008000
#define PSP_CTRL_HOME 0x010000
#define PSP_CTRL_HOLD 0x020000
#define IRQ_KEYPAD 0x1000
#define REG_P1 0
#define REG_P1CNT 1
#define REG_IF 2
#define CPU_HALT_STATE 0
#define CPU_STOP 2
static u16 io_registers[3];
#define pIO_REG(offset) io_registers[offset]
static u32 reg[1];
u32 option_swap_confirm_buttons = 0;
u32 option_enable_analog = 0;
u32 option_analog_sensitivity = 4;
u32 enable_home_menu = 1;
u32 psp_fps_debug = 0;
static u32 now_us = 1000000;
static int menus, loads, saves, ff_presses, ff_held;
u32 sceKernelGetSystemTimeLow(void) { return now_us; }
int sceCtrlReadBufferPositive(SceCtrlData *data, int count);
int sceCtrlReadBufferPositive(SceCtrlData *data, int count)
{ (void)data; return count; }
void main_power_service(void) {}
void main_fast_forward_input(u32 pressed, u32 held)
{ ff_presses += pressed != 0; ff_held = held != 0; }
u32 menu(void) { menus++; return 7; }
u32 action_loadstate(void) { loads++; return 0; }
u32 action_savestate(void) { saves++; return 0; }
int video_cycles;
void menu_cycle_video_mode(void) { video_cycles++; }
static u32 toast_ids[8];
static int toast_letters[8];
static int toasts;
void toast_show(u32 msg_id, ...);
void toast_show(u32 msg_id, ...)
{
  va_list args;

  va_start(args, msg_id);
  if(toasts < 8)
  {
    toast_ids[toasts] = msg_id;
    toast_letters[toasts] = va_arg(args, int);
  }
  toasts++;
  va_end(args);
}
'''.replace("@SRC@", SRC)

# GBA keys (KEYINPUT bits, active low in REG_P1) and the shortcut masks.
SCENARIOS = r'''
#define GBA_A 0x01
#define GBA_SELECT 0x04
#define GBA_START 0x08
#define GBA_LEFT 0x20
#define GBA_UP 0x40
#define GBA_DOWN 0x80
#define GBA_L 0x200
#define GBA_R 0x100
#define SEL PSP_CTRL_SELECT
#define START PSP_CTRL_START
#define LT PSP_CTRL_LTRIGGER
#define RT PSP_CTRL_RTRIGGER
static u32 pad;
static unsigned char stick_y = 128;

static int fake_peek(SceCtrlData *data, int count)
{
  memset(data, 0, sizeof(*data));
  data->Buttons = pad;
  data->Lx = 128;
  data->Ly = stick_y;
  return count;
}

static u32 read_pad(u32 buttons, u32 elapsed_ms)
{
  pad = buttons;
  now_us += elapsed_ms * 1000;
  return update_input();
}

static int game_has(u32 key)
{
  return (io_registers[REG_P1] & key) == 0;
}

static void set_shortcuts(u32 save, u32 load, u32 open_menu, u32 ff,
 u32 fps)
{
  memset(option_shortcut, 0, sizeof(u32) * CONFIG_MODEL_SHORTCUT_COUNT);
  option_shortcut[0] = save;
  option_shortcut[1] = load;
  option_shortcut[2] = open_menu;
  option_shortcut[3] = ff;
  option_shortcut[4] = fps;
  rebuild_shortcut_chords();
  read_pad(0, 100);
  read_pad(0, 100);
  menus = loads = saves = ff_presses = 0;
}

int main(void)
{
  int i;
  u32 fps;
  u16 keys_before;

  (void)__sceCtrlReadBufferPositive;
  __sceCtrlPeekBufferPositive = fake_peek;
  rebuild_fast_forward_button_mask();
  set_shortcuts(0x0410, 0x0420, 0x0C00, 0, 0);

  /* SELECT+L saves once and the game never gets that L, even after
     SELECT is let go; a later L alone reaches the game. */
  assert(read_pad(SEL, 16) == 0 && game_has(GBA_SELECT));
  assert(read_pad(SEL | LT, 16) == 0 && saves == 1);
  assert(read_pad(SEL | LT, 16) == 0 && saves == 1);
  assert(game_has(GBA_SELECT) && !game_has(GBA_L));
  assert(read_pad(LT, 16) == 0 && !game_has(GBA_L) && saves == 1);
  read_pad(0, 16);
  assert(read_pad(LT, 16) == 0 && game_has(GBA_L) && saves == 1);
  /* L first, then SELECT, is no shortcut. */
  assert(read_pad(LT | SEL, 16) == 0 && saves == 1);
  read_pad(0, 16);

  /* SELECT+R loads and stops the pass. */
  read_pad(SEL, 16);
  assert(read_pad(SEL | RT, 16) == 1 && loads == 1);
  read_pad(SEL | RT, 16);
  assert(!game_has(GBA_R) && loads == 1);
  read_pad(0, 16);

  /* START+SELECT opens the menu after 500 ms, whatever the read count;
     START reaches the game meanwhile. */
  read_pad(START | SEL, 16);
  for(i = 0; i < 49; i++)
    assert(read_pad(START | SEL, 10) == 0);
  assert(game_has(GBA_START) && menus == 0);
  assert(read_pad(START | SEL, 10) == 7 && menus == 1);
  assert(read_pad(START | SEL, 600) == 0 && menus == 1);
  read_pad(0, 16);
  menus = 0;

  /* Not with GBA A (Circle) or B (Cross) held: A+B+START+SELECT is the
     game's reset. */
  for(i = 0; i < 3; i++)
  {
    u32 blocking[] = { PSP_CTRL_CIRCLE | PSP_CTRL_CROSS, PSP_CTRL_CIRCLE,
     PSP_CTRL_CROSS };

    read_pad(blocking[i] | START | SEL, 16);
    assert(read_pad(blocking[i] | START | SEL, 1000) == 0 && menus == 0);
    read_pad(0, 16);
  }

  /* Rapid-fire A presses GBA A too, so it blocks the hold as well. */
  gamepad_config_map[1] = BUTTON_ID_RAPIDFIRE_A;
  rebuild_fast_forward_button_mask();
  read_pad(PSP_CTRL_CIRCLE | START | SEL, 16);
  assert(read_pad(PSP_CTRL_CIRCLE | START | SEL, 1000) == 0 && menus == 0);
  read_pad(0, 16);
  gamepad_config_map[1] = BUTTON_ID_A;
  rebuild_fast_forward_button_mask();

  /* Single-key mappings still work: Square toggles the FPS overlay,
     Triangle is fast-forward, HOME opens the menu. */
  read_pad(PSP_CTRL_SQUARE, 16);
  assert(psp_fps_debug == 1);
  read_pad(PSP_CTRL_TRIANGLE, 16);
  assert(ff_presses == 1);
  read_pad(0, 16);
  assert(read_pad(PSP_CTRL_HOME, 16) == 7 && menus == 1);
  read_pad(0, 16);

  /* A trigger hidden by a fired chord is no fast-forward press either. */
  set_shortcuts(0x0401, 0x0420, 0x0C00, 0, 0);
  read_pad(SEL, 16);
  assert(read_pad(SEL | PSP_CTRL_TRIANGLE, 16) == 0 && saves == 1);
  assert(ff_presses == 0);
  read_pad(0, 16);

  /* SELECT+Up is fast-forward: one press when it fires, held until a
     key of it is let go, and the game never gets that Up. Up first,
     then SELECT, is no shortcut. */
  set_shortcuts(0x0410, 0x0420, 0x0C00, 0x0500, 0x0440);
  read_pad(SEL, 16);
  assert(ff_presses == 0 && !ff_held);
  assert(read_pad(SEL | PSP_CTRL_UP, 16) == 0 && ff_presses == 1 && ff_held);
  assert(!game_has(GBA_UP));
  read_pad(SEL | PSP_CTRL_UP, 16);
  assert(ff_presses == 1 && ff_held && !game_has(GBA_UP));
  read_pad(PSP_CTRL_UP, 16);
  assert(!ff_held);
  read_pad(0, 16);
  read_pad(PSP_CTRL_UP, 16);
  read_pad(PSP_CTRL_UP | SEL, 16);
  assert(ff_presses == 1 && !ff_held && game_has(GBA_UP));
  read_pad(0, 16);

  /* SELECT+Down toggles the FPS overlay once per press. */
  fps = psp_fps_debug;
  read_pad(SEL, 16);
  assert(read_pad(SEL | PSP_CTRL_DOWN, 16) == 0 && psp_fps_debug == (fps ^ 1));
  read_pad(SEL | PSP_CTRL_DOWN, 16);
  assert(psp_fps_debug == (fps ^ 1) && !game_has(GBA_DOWN));
  read_pad(0, 16);
  assert(ff_presses == 1 && saves == 0 && loads == 0 && menus == 0);

  /* The read that saves stops there, as the save button does: a key
     pressed with it reaches the game on the next read. */
  read_pad(SEL, 16);
  keys_before = io_registers[REG_P1];
  assert(read_pad(SEL | LT | PSP_CTRL_CIRCLE, 16) == 0 && saves == 1);
  assert(io_registers[REG_P1] == keys_before);
  read_pad(SEL | LT | PSP_CTRL_CIRCLE, 16);
  assert(game_has(GBA_A) && !game_has(GBA_L));
  read_pad(0, 16);

  /* With the stick on, its directions are chord keys: SELECT+stick up. */
  option_enable_analog = 1;
  set_shortcuts(0x1400, 0x0420, 0x0C00, 0, 0);
  read_pad(SEL, 16);
  stick_y = 0;
  assert(read_pad(SEL, 16) == 0 && saves == 1 && !game_has(GBA_UP));
  stick_y = 128;
  read_pad(0, 16);
  option_enable_analog = 0;

  /* The video cycle has no chord until one is set; SELECT+Left then
     runs it once per press, and the game never gets that Left. */
  set_shortcuts(0x0410, 0x0420, 0x0C00, 0, 0);
  read_pad(SEL, 16);
  read_pad(SEL | PSP_CTRL_LEFT, 16);
  assert(video_cycles == 0 && game_has(GBA_LEFT));
  read_pad(0, 16);
  option_shortcut[INPUT_CHORD_VIDEO_CYCLE] = 0x0480;
  rebuild_shortcut_chords();
  read_pad(SEL, 16);
  assert(read_pad(SEL | PSP_CTRL_LEFT, 16) == 0 && video_cycles == 1);
  read_pad(SEL | PSP_CTRL_LEFT, 16);
  assert(video_cycles == 1 && !game_has(GBA_LEFT));
  read_pad(SEL, 16);
  assert(read_pad(SEL | PSP_CTRL_LEFT, 16) == 0 && video_cycles == 2);
  read_pad(0, 16);
  assert(saves == 0 && loads == 0 && menus == 0);

  /* Two slots with the same chord: the default chords apply instead. */
  set_shortcuts(0x0003, 0x0003, 0, 0, 0);
  read_pad(PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE, 16);
  assert(saves == 0 && loads == 0);
  read_pad(0, 16);
  read_pad(SEL, 16);
  assert(read_pad(SEL | LT, 16) == 0 && saves == 1);

  puts("update_input shortcuts passed");
  return 0;
}
'''

# Turbo on Circle (A), Cross (B) and Square (R), and the stick, read once
# per emulated frame as update_gba does, plus the extra reads of link waits.
TURBO_SCENARIOS = r'''
#define GBA_A 0x01
#define GBA_B 0x02
#define GBA_UP 0x40
#define GBA_R 0x100
#define GBA_L 0x200
#define SEL PSP_CTRL_SELECT
#define START PSP_CTRL_START
#define LT PSP_CTRL_LTRIGGER
#define RT PSP_CTRL_RTRIGGER
#define TURBO_R PSP_CTRL_SQUARE
static u32 pad;
static unsigned char stick_y = 128;

static int fake_peek(SceCtrlData *data, int count)
{
  memset(data, 0, sizeof(*data));
  data->Buttons = pad;
  data->Lx = 128;
  data->Ly = stick_y;
  return count;
}

/* One emulated frame: the tick, then update_gba's read. */
static u32 frame(u32 buttons)
{
  pad = buttons;
  now_us += 16667;
  input_frame_tick();
  return update_input();
}

/* A read of a link wait, which does not tick. */
static u32 wait_read(u32 buttons)
{
  pad = buttons;
  now_us += 1000;
  return update_input();
}

static int game_has(u32 key)
{
  return (io_registers[REG_P1] & key) == 0;
}

static u32 press_home(void)
{
  return frame(TURBO_R | PSP_CTRL_HOME);
}

static u32 load_chord(void)
{
  frame(TURBO_R | SEL);
  return frame(TURBO_R | SEL | RT);
}

static u32 menu_hold(void)
{
  u32 result = 0;
  int i;

  for(i = 0; i < 40 && result == 0; i++)
    result = frame(TURBO_R | START | SEL);
  return result;
}

static u32 press_triangle(void)
{
  return frame(TURBO_R | PSP_CTRL_TRIANGLE);
}

static u32 sleep_stop(void)
{
  input_turbo_stop();
  return 0;
}

/* Presses of key the game sees over 60 frames with buttons held, from a
   new press; the buttons are let go after. */
static int presses(u32 buttons, u32 key)
{
  int i, count = 0, was_down = 0;

  frame(0);
  for(i = 0; i < 60; i++)
  {
    int down;

    frame(buttons);
    down = game_has(key);
    count += down && !was_down;
    was_down = down;
  }
  frame(0);
  return count;
}

/* Turbo R runs, then action opens the menu, loads a state or sleeps:
   Square, still held, pulses again only after a new press. */
static void check_stop(u32 (*action)(void), u32 expected)
{
  int i;

  frame(0);
  frame(TURBO_R);
  assert(game_has(GBA_R));
  assert(action() == expected);
  for(i = 0; i < 4; i++)
  {
    frame(TURBO_R);
    assert(!game_has(GBA_R));
  }
  frame(0);
  frame(TURBO_R);
  assert(game_has(GBA_R));
  frame(0);
}

int main(void)
{
  int i, j, a_on, b_on;

  (void)__sceCtrlReadBufferPositive;
  __sceCtrlPeekBufferPositive = fake_peek;
  option_shortcut[0] = 0x0410;
  option_shortcut[1] = 0x0420;
  option_shortcut[2] = 0x0C00;
  rebuild_shortcut_chords();
  gamepad_config_map[1] = BUTTON_ID_RAPIDFIRE_A;
  gamepad_config_map[2] = BUTTON_ID_RAPIDFIRE_B;
  gamepad_config_map[3] = BUTTON_ID_RAPIDFIRE_R;
  rebuild_fast_forward_button_mask();
  frame(0);

  /* Turbo A and B held together each press 30 times in 60 frames, on in
     the frame of the press; the reads of a link wait repeat the keys of
     their frame. */
  a_on = b_on = 0;
  for(i = 0; i < 60; i++)
  {
    u16 keys;

    assert(frame(PSP_CTRL_CIRCLE | PSP_CTRL_CROSS) == 0);
    keys = io_registers[REG_P1];
    assert(i != 0 || (game_has(GBA_A) && game_has(GBA_B)));
    a_on += game_has(GBA_A);
    b_on += game_has(GBA_B);
    for(j = 0; j < i % 4; j++)
    {
      assert(wait_read(PSP_CTRL_CIRCLE | PSP_CTRL_CROSS) == 0);
      assert(io_registers[REG_P1] == keys);
    }
  }
  assert(a_on == 30 && b_on == 30);
  frame(0);
  assert(!game_has(GBA_A) && !game_has(GBA_B));

  /* Triangle on plain A keeps A held through turbo A's off frames. */
  gamepad_config_map[0] = BUTTON_ID_A;
  rebuild_fast_forward_button_mask();
  for(i = 0; i < 4; i++)
  {
    frame(PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE);
    assert(game_has(GBA_A));
  }
  frame(0);

  /* HOME, the load chord, the menu hold, the menu and load buttons and
     sleep all stop turbo. */
  check_stop(press_home, 7);
  check_stop(load_chord, 1);
  check_stop(menu_hold, 7);
  gamepad_config_map[0] = BUTTON_ID_MENU;
  rebuild_fast_forward_button_mask();
  check_stop(press_triangle, 7);
  gamepad_config_map[0] = BUTTON_ID_LOADSTATE;
  rebuild_fast_forward_button_mask();
  check_stop(press_triangle, 1);
  check_stop(sleep_stop, 0);
  assert(menus == 3 && loads == 2 && saves == 0);
  assert(ff_presses == 0 && !ff_held);

  /* Turbo L on the L of the SELECT+L save: the L hidden by the chord
     starts no train while it stays held. */
  gamepad_config_map[4] = BUTTON_ID_RAPIDFIRE_L;
  rebuild_fast_forward_button_mask();
  frame(SEL);
  assert(frame(SEL | LT) == 0 && saves == 1);
  for(i = 0; i < 4; i++)
  {
    frame(i < 2 ? SEL | LT : LT);
    assert(!game_has(GBA_L));
  }
  frame(0);
  frame(LT);
  assert(game_has(GBA_L));
  frame(0);

  /* Sensitivity 4: stick up presses below 60, stays held up to 75 and
     lets go from 76; turning the stick off forgets it. */
  option_enable_analog = 1;
  stick_y = 59;
  frame(0);
  assert(game_has(GBA_UP));
  stick_y = 75;
  frame(0);
  assert(game_has(GBA_UP));
  stick_y = 76;
  frame(0);
  assert(!game_has(GBA_UP));
  stick_y = 60;
  frame(0);
  assert(!game_has(GBA_UP));
  stick_y = 59;
  frame(0);
  option_enable_analog = 0;
  frame(0);
  assert(!game_has(GBA_UP));
  option_enable_analog = 1;
  stick_y = 70;
  frame(0);
  assert(!game_has(GBA_UP));
  option_enable_analog = 0;
  stick_y = 128;

  /* The speed of each turbo comes from the settings: A at 15 and B at 10
     presses per second, also held together. Hold mode shows no toast. */
  option_turbo_speed[0] = INPUT_MODEL_TURBO_15;
  option_turbo_speed[1] = INPUT_MODEL_TURBO_10;
  rebuild_fast_forward_button_mask();
  toasts = 0;
  assert(presses(PSP_CTRL_CIRCLE, GBA_A) == 15);
  assert(presses(PSP_CTRL_CROSS, GBA_B) == 10);
  assert(presses(PSP_CTRL_CIRCLE | PSP_CTRL_CROSS, GBA_A) == 15);
  assert(presses(PSP_CTRL_CIRCLE | PSP_CTRL_CROSS, GBA_B) == 10);
  assert(presses(TURBO_R, GBA_R) == 30);
  assert(toasts == 0);

  /* Toggle on R and L: a press starts or stops the train and shows
     "Turbo R" or "Turbo L" on or off once; holding, letting go or a
     link wait's read shows nothing. */
  option_turbo_mode[3] = 1;
  option_turbo_mode[2] = 1;
  rebuild_fast_forward_button_mask();
  frame(0);
  frame(TURBO_R);
  assert(toasts == 1 && toast_ids[0] == MSG_TURBO_ON);
  assert(toast_letters[0] == 'R' && game_has(GBA_R));
  frame(TURBO_R);
  wait_read(TURBO_R);
  frame(0);
  assert(game_has(GBA_R));
  wait_read(0);
  frame(0);
  assert(toasts == 1 && !game_has(GBA_R));
  frame(TURBO_R);
  assert(toasts == 2 && toast_ids[1] == MSG_TURBO_OFF);
  assert(toast_letters[1] == 'R');
  for(i = 0; i < 4; i++)
  {
    frame(0);
    assert(!game_has(GBA_R));
  }
  frame(LT);
  assert(toasts == 3 && toast_ids[2] == MSG_TURBO_ON);
  assert(toast_letters[2] == 'L' && game_has(GBA_L));
  frame(0);
  frame(LT);
  assert(toasts == 4 && toast_ids[3] == MSG_TURBO_OFF);
  assert(toast_letters[3] == 'L');
  frame(0);

  puts("update_input turbo and stick passed");
  return 0;
}
'''


# The real menu_cycle_video_mode() with the real toast ring, the SCALED_
# values of main.h and the English mode names keyed by their MSG ids; the
# stand-ins record the scale that video_apply_screen_scale and the per-game
# save saw. video_resolution_small must not run: a link wait can call the
# cycle after the frame was presented, and its clear would show black.
CYCLE_STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "@SRC@/menu_layout.c"
#include "@SRC@/toast.c"
#include "@SRC@/message.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t s32;
@SCALED@
#undef MSG
static const char *messages[MSG_END] =
{
  [MSG_SCN_SCALED_NONE] = "100% GU",
  [MSG_SCN_SCALED_X15_GU] = "150% GU",
  [MSG_SCN_SCALED_X15_SW] = "150% SW",
  [MSG_SCN_SCALED_USER] = "USER GU",
  [MSG_SCN_SCALED_16X9_GU] = "16:9 GU",
  [MSG_SCN_SCALED_16X9_SHARP] = "16:9 Sharp",
  [MSG_SCN_SCALED_16X9_AUTO] = "16:9 Auto"
};
#define MSG messages
u32 option_screen_scale = SCALED_16X9_AUTO;
static u32 now_us = 5000;
static u32 presented_scale = 99;
static u32 saved_scale = 99;
static int presents, saves;
u32 sceKernelGetSystemTimeLow(void) { return now_us; }
void video_apply_screen_scale(void)
{ presents++; presented_scale = option_screen_scale; }
void video_resolution_small(void) { assert(!"clears the presented frame"); }
static s32 save_game_config_file(void)
{ saves++; saved_scale = option_screen_scale; return 0; }
void menu_cycle_video_mode(void);
'''

# 16:9 Auto, 100%, 150% GU, 16:9 Sharp and around again; the other modes
# go to 16:9 Auto. Each step presents, names and saves the new mode, and
# the newest name replaces a toast still queued.
CYCLE_SCENARIOS = r'''
static void check_step(u32 scale, const char *name, int step)
{
  const Toast *toast = toast_current(now_us);

  assert(option_screen_scale == scale && presented_scale == scale);
  assert(saved_scale == scale && presents == step && saves == step);
  assert(toast != NULL && strcmp(toast->text, name) == 0);
  assert(toast->duration_us == 3000000u && toast->sjis == 0);
}

int main(void)
{
  static const u32 others[] = { SCALED_X15_SW, SCALED_USER, SCALED_16X9_GU };
  int i;

  toast_push("Turbo A: on", 3000000u, 0, now_us);
  menu_cycle_video_mode();
  check_step(SCALED_NONE, "100% GU", 1);
  menu_cycle_video_mode();
  check_step(SCALED_X15_GU, "150% GU", 2);
  menu_cycle_video_mode();
  check_step(SCALED_16X9_SHARP, "16:9 Sharp", 3);
  menu_cycle_video_mode();
  check_step(SCALED_16X9_AUTO, "16:9 Auto", 4);
  menu_cycle_video_mode();
  check_step(SCALED_NONE, "100% GU", 5);
  now_us += 3000000u;
  assert(toast_current(now_us) == NULL);

  for(i = 0; i < 3; i++)
  {
    option_screen_scale = others[i];
    menu_cycle_video_mode();
    check_step(SCALED_16X9_AUTO, "16:9 Auto", 6 + i);
  }

  puts("menu_cycle_video_mode passed");
  return 0;
}
'''

# psp_video.c's scale functions with stand-ins for the GU: both pick the
# presenter, restart the governor and set the filter in one direct list,
# but only video_resolution_small clears the draw buffer.
SCALE_STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "@SRC@/video_adaptive.h"
typedef uint32_t u32;
@SCALED@
#define GU_DIRECT 0
#define GU_NEAREST 0
#define GU_COLOR_BUFFER_BIT 1
#define GU_FAST_CLEAR_BIT 16
u32 option_screen_scale = SCALED_NONE;
u32 option_screen_filter = 1;
static u32 display_list[4];
static u32 adaptive_last_frame_time = 7;
static int adaptive_has_last_frame_time = 1;
static VideoAdaptiveState adaptive_video;
static int syncs, presenters, clears, filters, list_open;
static void video_sync_present(void) { syncs++; }
void video_adaptive_set_enabled(VideoAdaptiveState *state, int enabled,
 VideoAdaptiveQuality manual_quality)
{ (void)enabled; state->quality = manual_quality; }
static VideoAdaptiveQuality selected_manual_quality(void)
{ return VIDEO_ADAPTIVE_QUALITY_SMOOTH; }
static void set_gba_resolution(void) { presenters++; }
static void sceGuStart(int type, void *list)
{ assert(type == GU_DIRECT && list == display_list && !list_open); list_open = 1; }
static void sceGuClearColor(u32 color) { assert(list_open && color == 0); }
static void sceGuClear(int flags)
{ assert(list_open && flags == (GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT)); clears++; }
static void sceGuTexFilter(int min, int mag)
{ assert(list_open && min == 1 && mag == 1); filters++; }
static void sceGuFinish(void) { assert(list_open); list_open = 0; }
static void video_ge_sync_direct(void) { assert(!list_open); }
'''

SCALE_SCENARIOS = r'''
int main(void)
{
  video_apply_screen_scale();
  assert(syncs == 1 && presenters == 1 && filters == 1 && clears == 0);
  assert(adaptive_has_last_frame_time == 0 && adaptive_last_frame_time == 0);
  video_resolution_small();
  assert(syncs == 2 && presenters == 2 && filters == 2 && clears == 1);
  puts("screen scale functions passed");
  return 0;
}
'''


def scaled_defines():
    """main.h's SCALED_ values, so the test uses the real ones."""
    lines = (ROOT / "source/src/main.h").read_text().splitlines()
    return "\n".join(l for l in lines if l.startswith("#define SCALED_"))


def input_region():
    """input.c from its PSP button masks to the end of update_input."""
    text = (ROOT / "source/src/input.c").read_text()
    start = text.index("/* The digital buttons; drops HOME")
    return text[start:text.index("/* Startup controller setup")]


class InputShortcutTests(unittest.TestCase):
    def test_update_input_runs_shortcut_chords(self):
        source = STUBS + input_region() + SCENARIOS
        build_and_run(source, "input_shortcuts")

    def test_update_input_runs_turbo_and_stick_model(self):
        source = STUBS + input_region() + TURBO_SCENARIOS
        build_and_run(source, "input_turbo")

    def test_video_cycle_steps_presents_names_and_saves(self):
        stubs = CYCLE_STUBS.replace("@SRC@", SRC).replace("@SCALED@",
                                                           scaled_defines())
        source = stubs + function("source/src/gui.c",
                                  "void menu_cycle_video_mode(void)")
        build_and_run(source + CYCLE_SCENARIOS, "video_cycle")
        # The game's .cfg holds the scale, which a per-game load restores.
        self.assertIn("model->screen_scale = option_screen_scale;",
                      function("source/src/gui.c",
                               "static void capture_game_config(ConfigModel *model)"))
        self.assertIn("option_screen_scale = model->screen_scale;",
                      function("source/src/gui.c",
                               "static void apply_game_config(const ConfigModel *model)"))

    def test_video_cycle_keeps_the_presented_frame(self):
        source = SCALE_STUBS.replace("@SRC@", SRC).replace("@SCALED@",
                                                            scaled_defines())
        for signature in ("static void apply_screen_scale(int clear)",
                          "void video_resolution_small(void)",
                          "void video_apply_screen_scale(void)"):
            source += function("source/src/psp_video.c", signature)
        build_and_run(source + SCALE_SCENARIOS, "screen_scale")

    def test_update_gba_ticks_turbo_outside_link_waits(self):
        # main.c cannot be built on the host: the V-blank read ticks the
        # frame count, the link wait loop does not, and sleep stops turbo.
        update = function("source/src/main.c", "u32 update_gba(void)")
        self.assertEqual(update.count("input_frame_tick();"), 1)
        self.assertLess(update.index("input_frame_tick();"),
                        update.index("if (update_input() != 0)"))
        link = function("source/src/main.c",
                        "static int main_link_service(IRQ_TYPE *irq_raised)")
        self.assertIn("update_input()", link)
        self.assertNotIn("input_frame_tick", link)
        self.assertIn("input_turbo_stop();",
                      function("source/src/main.c",
                               "static void psp_sleep_loop(void)"))

    def test_settings_carry_the_shortcuts(self):
        # gui.c's settings glue cannot be built on the host: both captures
        # store the masks, and both applies load them before the chords.
        for name in ("capture_global_config", "capture_game_config"):
            body = function("source/src/gui.c",
                            "static void %s(ConfigModel *model)" % name)
            self.assertIn("model->shortcut[i] = option_shortcut[i];", body)
        for name in ("apply_global_config", "apply_game_config"):
            body = function("source/src/gui.c",
                            "static void %s(const ConfigModel *model)" % name)
            self.assertLess(body.index("option_shortcut[i] = model->shortcut[i];"),
                            body.index("rebuild_shortcut_chords();"))

    def test_settings_carry_the_turbo(self):
        # Per game and globally: both captures store the turbo speed and
        # mode, and both applies load them before the turbo inputs are
        # rebuilt from them.
        speed = "model->turbo_speed[i] = option_turbo_speed[i];"
        mode = "model->turbo_mode[i] = option_turbo_mode[i];"
        for name in ("capture_global_config", "capture_game_config"):
            body = function("source/src/gui.c",
                            "static void %s(ConfigModel *model)" % name)
            self.assertIn(speed, body)
            self.assertIn(mode, body)
        for name in ("apply_global_config", "apply_game_config"):
            body = function("source/src/gui.c",
                            "static void %s(const ConfigModel *model)" % name)
            rebuild = body.index("rebuild_fast_forward_button_mask();")
            self.assertLess(body.index("option_turbo_speed[i] = model->turbo_speed[i];"),
                            rebuild)
            self.assertLess(body.index("option_turbo_mode[i] = model->turbo_mode[i];"),
                            rebuild)


if __name__ == "__main__":
    unittest.main()
