"""Exercise production startup and auto-state paths with host API stubs."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    text = (ROOT / source).read_text()
    start = text.index(signature + "\n{")
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


STUBS = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
#define MAX_FILE 512
#define COLOR32_BLACK 0
#define CONFIRMATION_QUIT 1
#define CONFIRMATION_CONT 2
#define EXECUTE_CYCLES 0
#define MSG_ERR_LOAD_GAMEPACK 0
#define MSG_AUTO_SAVESTATE_LOAD_PROMPT 1
const char *messages[] = {"load error", "restore state"};
#define MSG messages
char gamepak_filename[512];
char dir_roms[] = "roms/";
u32 gamepak_file_size, reg[1], option_clock_speed, sound_pause;
u32 enable_home_menu;
int regular_file, browser_result, load_result, menu_result;
int reset_calls, prompt_calls, execute_calls, quit_calls, recent_calls;
int popup_value, active_link, state_exists, dialog_result, restored_slot;
jmp_buf stopped;
void setup_main(const char *p) { (void)p; }
int path_is_regular_file(const char *p) { (void)p; return regular_file; }
int load_gamepak(char *p) {
  if(load_result < 0) return -1;
  strcpy(gamepak_filename, p); gamepak_file_size = 1024; return 0;
}
void clear_screen(int c) { (void)c; }
void error_msg(const char *m, int c) { (void)m; (void)c; }
void quit(void) { quit_calls++; longjmp(stopped, 1); }
u32 menu(void) {
  if(menu_result) {
    strcpy(gamepak_filename, "menu.gba"); gamepak_file_size = 1024;
    reg[0] = 0xFEED;
  }
  return menu_result;
}
int load_file(const char **e, char *p, char *d, u32 r) {
  (void)e; (void)d; (void)r; strcpy(p, "browser.gba"); return browser_result;
}
void add_recent_rom(const char *p) { (void)p; recent_calls++; }
void reset_gba(void) { reset_calls++; reg[0] = 0xCAFE; }
void maybe_load_auto_savestate(void);
int sceImposeSetHomePopup(u32 p) { popup_value = p; return 0; }
void set_cpu_clock(u32 c) { (void)c; }
void sceDisplayWaitVblankStart(void) {}
void video_resolution_small(void) {}
void execute_arm_translate(u32 c) {
  assert(c == (menu_result ? 0xFEEDU : 0xCAFEU)); execute_calls++;
}
int main_link_active(void) { return active_link; }
u32 auto_savestate_exists(void) { return state_exists; }
u32 yesno_dialog(const char *p) { (void)p; prompt_calls++; return dialog_result; }
u32 action_loadstate_slot(u32 slot) { restored_slot = slot; return 1; }
'''


SCENARIOS = r'''
static void clear_test(void) {
  regular_file = menu_result = active_link = 0;
  browser_result = -1; load_result = 0;
  reset_calls = prompt_calls = execute_calls = quit_calls = recent_calls = 0;
  state_exists = 1; dialog_result = 0; restored_slot = -1; popup_value = -1;
  gamepak_filename[0] = 0; sound_pause = 1;
}
static void start(int argc) {
  char *argv[] = {"EBOOT.PBP", "direct.gba"};
  if(!setjmp(stopped)) user_main(argc, argv);
}
int main(void) {
  clear_test(); menu_result = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 0 && prompt_calls == 0);
  assert(sound_pause == 0 && quit_calls == 1);
  clear_test(); regular_file = 1; load_result = -1; menu_result = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 0);
  clear_test(); browser_result = 0; load_result = -1; menu_result = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 0);
  clear_test(); start(1);
  assert(execute_calls == 0 && reset_calls == 0 && quit_calls == 1);
  clear_test(); start(2);
  assert(execute_calls == 1 && reset_calls == 1 && prompt_calls == 1);
  assert(restored_slot == 10 && recent_calls == 0 && popup_value == 1);
  clear_test(); regular_file = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 1 && prompt_calls == 1);
  assert(recent_calls == 0);
  clear_test(); browser_result = 0; start(1);
  assert(execute_calls == 1 && reset_calls == 1 && prompt_calls == 1);
  assert(recent_calls == 1);
  clear_test(); load_result = -1; start(2);
  assert(execute_calls == 0 && quit_calls == 1);
  clear_test(); maybe_load_auto_savestate(); assert(prompt_calls == 0);
  strcpy(gamepak_filename, "game.gba"); active_link = 1;
  maybe_load_auto_savestate(); assert(prompt_calls == 0);
  active_link = 0; state_exists = 0;
  maybe_load_auto_savestate(); assert(prompt_calls == 0);
  state_exists = 1; dialog_result = 1;
  maybe_load_auto_savestate(); assert(prompt_calls == 1 && restored_slot == -1);
  puts("startup and auto-state regressions passed");
  return 0;
}
'''


class StartupTests(unittest.TestCase):
    def test_production_startup_paths(self):
        source = STUBS + function("source/src/gui.c",
                                 "void maybe_load_auto_savestate(void)")
        source += function("source/src/main.c", "int user_main(int argc, char *argv[])")
        source += SCENARIOS
        with tempfile.TemporaryDirectory(prefix="gbarush-startup-") as directory:
            path = Path(directory)
            (path / "startup.c").write_text(source)
            command = shlex.split(os.environ.get("HOST_CC", "cc"))
            subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                     str(path / "startup.c"), "-o", str(path / "startup")],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(path / "startup")], check=True)


if __name__ == "__main__":
    unittest.main()
