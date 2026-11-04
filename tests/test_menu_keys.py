"""Check that keys held as the menu closes stay away from the game."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

# Extracts a C function from a source file (see tests/test_startup.py).
from tests.test_startup import function


ROOT = Path(__file__).resolve().parents[1]
# A fake pad behind the controller pointer, Cross mapped to GBA A, and
# counters for the menu and the hotkeys update_input can start.
STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef int SceUID;
#include "input.h"
typedef struct
{
  unsigned int TimeStamp;
  unsigned int Buttons;
  unsigned char Lx;
  unsigned char Ly;
} SceCtrlData;
#define PSP_CTRL_CIRCLE 0x002000
#define PSP_CTRL_CROSS 0x004000
#define PSP_CTRL_HOME 0x010000
#define PSP_CTRL_HOLD 0x020000
#define PSP_CTRL_ANALOG_UP (1u << 28)
#define PSP_CTRL_ANALOG_DOWN (1u << 29)
#define PSP_CTRL_ANALOG_LEFT (1u << 30)
#define PSP_CTRL_ANALOG_RIGHT (1u << 31)
#define REG_P1 0x98
#define pIO_REG(offset) *(io_registers + (offset))
u16 io_registers[0x200];
static unsigned int pad;
static int fake_peek(SceCtrlData *pad_data, int count)
{
  (void)count;
  pad_data->Buttons = pad;
  pad_data->Lx = 128;
  pad_data->Ly = 128;
  return 1;
}
static int (*__sceCtrlPeekBufferPositive)(SceCtrlData *pad_data, int count) =
 fake_peek;
u32 last_buttons;
static u32 menu_held_buttons;
static u32 fast_forward_button_mask;
u32 gba_keys_held;
u32 tilt_sensorX, tilt_sensorY, enable_tilt_sensor;
u32 option_analog_sensitivity, option_enable_analog, enable_home_menu = 1;
u32 psp_fps_debug, rapidfire_flag = 1;
u32 gamepad_config_map[16] = { BUTTON_ID_NONE, BUTTON_ID_B, BUTTON_ID_A };
const u32 button_psp_mask_to_config[16] = { 0, PSP_CTRL_CIRCLE, PSP_CTRL_CROSS };
const u32 button_id_to_gba_mask[BUTTON_ID_MENU] = {
  [BUTTON_ID_A] = BUTTON_A, [BUTTON_ID_B] = BUTTON_B
};
static int menu_calls, hotkey_calls;
u32 menu(void) { menu_calls++; return 1; }
void main_fast_forward_input(u32 pressed, u32 held) { (void)pressed; (void)held; }
u32 action_loadstate(void) { hotkey_calls++; return 0; }
u32 action_savestate(void) { hotkey_calls++; return 0; }
static void trigger_key(u32 keys_down) { (void)keys_down; }
'''

SCENARIOS = r'''
static void frames(unsigned int buttons, int count, u32 expected)
{
  int i;
  pad = buttons;
  for (i = 0; i < count; i++)
  {
    update_input();
    assert(gba_keys_held == expected);
    assert((pIO_REG(REG_P1) & 0x3FF) == ((~expected) & 0x3FF));
  }
}

int main(void) {
  // Cross closes the menu and stays down: the game never sees A.
  input_hold_until_released(PSP_CTRL_CROSS);
  frames(PSP_CTRL_CROSS, 5, 0);
  // Released, then pressed again: A at once.
  frames(0, 1, 0);
  frames(PSP_CTRL_CROSS, 1, BUTTON_A);
  frames(0, 1, 0);
  // Another key pressed while the held one is still down passes at once.
  input_hold_until_released(PSP_CTRL_CROSS);
  frames(PSP_CTRL_CROSS, 2, 0);
  frames(PSP_CTRL_CROSS | PSP_CTRL_CIRCLE, 1, BUTTON_B);
  frames(0, 1, 0);
  // A held HOME does not reopen the menu until released and pressed again.
  input_hold_until_released(PSP_CTRL_HOME);
  frames(PSP_CTRL_HOME, 5, 0);
  assert(menu_calls == 0);
  frames(0, 1, 0);
  assert(menu_calls == 0);
  pad = PSP_CTRL_HOME;
  assert(update_input() == 1 && menu_calls == 1);
  assert(hotkey_calls == 0 && psp_fps_debug == 0);
  puts("menu key masking regressions passed");
  return 0;
}
'''


class MenuKeysTests(unittest.TestCase):
    def test_keys_held_at_menu_close(self):
        # The real input.c functions, compiled between the stubs and scenarios.
        source = STUBS
        source += function("source/src/input.c",
                           "void input_hold_until_released(u32 buttons)")
        source += function("source/src/input.c", "u32 update_input(void)")
        source += SCENARIOS
        with tempfile.TemporaryDirectory(prefix="gbarush-menu-keys-") as directory:
            path = Path(directory)
            (path / "menu_keys.c").write_text(source)
            command = shlex.split(os.environ.get("HOST_CC", "cc"))
            result = subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                               "-I", str(ROOT / "source/src"),
                                               str(path / "menu_keys.c"),
                                               "-o", str(path / "menu_keys")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(path / "menu_keys")], check=True)


if __name__ == "__main__":
    unittest.main()
