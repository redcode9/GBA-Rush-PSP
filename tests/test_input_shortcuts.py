"""Check the shortcut chords in input.c's update_input on the host."""

from pathlib import Path
import unittest

# Builds and runs a spliced C test with HOST_CC.
from tests.test_startup import ROOT, build_and_run, function


SRC = (Path(__file__).resolve().parents[1] / "source/src").as_posix()

# Stand-ins for the PSP SDK and for what update_input calls, with the real
# chord module. Counters record the actions that ran.
STUBS = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "@SRC@/input_chord.c"
#include "@SRC@/config_model.h"
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
static int menus, loads, saves, ff_presses;
u32 sceKernelGetSystemTimeLow(void) { return now_us; }
int sceCtrlReadBufferPositive(SceCtrlData *data, int count);
int sceCtrlReadBufferPositive(SceCtrlData *data, int count)
{ (void)data; return count; }
void main_power_service(void) {}
void main_fast_forward_input(u32 pressed, u32 held)
{ (void)held; ff_presses += pressed != 0; }
u32 menu(void) { menus++; return 7; }
u32 action_loadstate(void) { loads++; return 0; }
u32 action_savestate(void) { saves++; return 0; }
'''.replace("@SRC@", SRC)

# GBA keys (KEYINPUT bits, active low in REG_P1) and the shortcut masks.
SCENARIOS = r'''
#define GBA_SELECT 0x04
#define GBA_START 0x08
#define GBA_L 0x200
#define GBA_R 0x100
#define SEL PSP_CTRL_SELECT
#define START PSP_CTRL_START
#define LT PSP_CTRL_LTRIGGER
#define RT PSP_CTRL_RTRIGGER
static u32 pad;

static int fake_peek(SceCtrlData *data, int count)
{
  memset(data, 0, sizeof(*data));
  data->Buttons = pad;
  data->Lx = 128;
  data->Ly = 128;
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

static void set_shortcuts(u32 save, u32 load, u32 open_menu)
{
  memset(option_shortcut, 0, sizeof(u32) * CONFIG_MODEL_SHORTCUT_COUNT);
  option_shortcut[0] = save;
  option_shortcut[1] = load;
  option_shortcut[2] = open_menu;
  rebuild_shortcut_chords();
  read_pad(0, 100);
  read_pad(0, 100);
  menus = loads = saves = ff_presses = 0;
}

int main(void)
{
  int i;

  (void)__sceCtrlReadBufferPositive;
  __sceCtrlPeekBufferPositive = fake_peek;
  rebuild_fast_forward_button_mask();
  set_shortcuts(0x0410, 0x0420, 0x0C00);

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
  set_shortcuts(0x0401, 0x0420, 0x0C00);
  read_pad(SEL, 16);
  assert(read_pad(SEL | PSP_CTRL_TRIANGLE, 16) == 0 && saves == 1);
  assert(ff_presses == 0);
  read_pad(0, 16);

  /* Two slots with the same chord: the default chords apply instead. */
  set_shortcuts(0x0003, 0x0003, 0);
  read_pad(PSP_CTRL_TRIANGLE | PSP_CTRL_CIRCLE, 16);
  assert(saves == 0 && loads == 0);
  read_pad(0, 16);
  read_pad(SEL, 16);
  assert(read_pad(SEL | LT, 16) == 0 && saves == 1);

  puts("update_input shortcuts passed");
  return 0;
}
'''


def input_region():
    """input.c from its PSP button masks to the end of update_input."""
    text = (ROOT / "source/src/input.c").read_text()
    start = text.index("/* The digital buttons; drops HOME")
    return text[start:text.index("/* Startup controller setup")]


class InputShortcutTests(unittest.TestCase):
    def test_update_input_runs_shortcut_chords(self):
        source = STUBS + input_region() + SCENARIOS
        build_and_run(source, "input_shortcuts")

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


if __name__ == "__main__":
    unittest.main()
