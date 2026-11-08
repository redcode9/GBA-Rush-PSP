"""Exercise production startup and auto-state paths with host API stubs."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    """Return one C function from a repository file, verbatim.

    The signature must be followed by a newline and "{" (the repo's style);
    the body ends at the matching brace, so braces inside string or
    character literals would throw the count off.
    """
    text = (ROOT / source).read_text()
    start = text.index(signature + "\n{")
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


# Host stand-ins for everything user_main() and maybe_load_auto_savestate()
# call. Counters record what ran, and quit() longjmps back to the scenario
# instead of exiting. first_run plays setup_main()'s result (tempgba.cfg did
# not load); queued counts toasts in the ring, which reset_gba() and a ROM
# loaded from menu() empty.
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
#define MSG_ERR_LOAD_GAMEPACK 0
#define MSG_AUTO_SAVESTATE_LOAD_PROMPT 1
#define MSG_FIRST_RUN_MENU_HINT 2
#define RUNTIME_LOG_FLUSH_START 1
const char *messages[] = {"load error", "restore state", "menu hint"};
#define MSG messages
char gamepak_filename[512];
char dir_roms[] = "roms/";
u32 gamepak_file_size, reg[1], option_clock_speed, sound_pause;
u32 enable_home_menu;
int regular_file, browser_result, load_result, menu_result;
int reset_calls, prompt_calls, execute_calls, quit_calls, recent_calls;
int flush_calls;
int popup_value, active_link, state_exists, dialog_result, restored_slot;
int first_run, toast_calls, queued, queued_at_run;
jmp_buf stopped;
u32 setup_main(const char *p) { (void)p; return first_run; }
u32 sceKernelGetSystemTimeLow(void) { return 77; }
void toast_push(const char *text, uint32_t duration_us, int sjis, uint32_t now) {
  assert(strcmp(text, "menu hint") == 0 && duration_us == 6000000);
  assert(sjis == 0 && now == 77); toast_calls++; queued++;
}
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
    reg[0] = 0xFEED; queued = 0;
  }
  return menu_result;
}
int load_file(const char **e, char *p, char *d, u32 r) {
  (void)e; (void)d; (void)r; strcpy(p, "browser.gba"); return browser_result;
}
void add_recent_rom(const char *p) { (void)p; recent_calls++; }
void reset_gba(void) { reset_calls++; reg[0] = 0xCAFE; queued = 0; }
void maybe_load_auto_savestate(void);
int sceImposeSetHomePopup(u32 p) { popup_value = p; return 0; }
void set_cpu_clock(u32 c) { (void)c; }
void sceDisplayWaitVblankStart(void) {}
void video_resolution_small(void) {}
int runtime_log_psp_flush(int r) {
  assert(r == RUNTIME_LOG_FLUSH_START); flush_calls++; return 1;
}
void main_run_cpu(void) {
  assert(reg[0] == (menu_result ? 0xFEEDU : 0xCAFEU)); execute_calls++;
  queued_at_run = queued;
  assert(flush_calls == 1);
}
int main_link_active(void) { return active_link; }
u32 auto_savestate_exists(void) { return state_exists; }
u32 yesno_dialog(const char *p) {
  (void)p; assert(queued == 0); prompt_calls++; return dialog_result;
}
u32 action_loadstate_slot(u32 slot) { restored_slot = slot; return 1; }
'''


# Launch orders in user_main(): a ROM path argument (argc 2), else
# roms/game.gba (regular_file), else the file browser, else the menu. A ROM
# picked in the menu runs without reset_gba() or the auto-state prompt; any
# other loaded ROM is reset and offers the auto state (slot 10). Every
# game saves the runtime log once before it runs. The last
# lines check that the prompt is skipped with no ROM, during a link session
# and without a saved state. With first_run set, every path that runs a
# game queues the 6 s menu hint once, after the reset and the auto-state
# prompt; the quit path and a loaded tempgba.cfg queue nothing.
SCENARIOS = r'''
static void clear_test(void) {
  regular_file = menu_result = active_link = 0;
  browser_result = -1; load_result = 0;
  reset_calls = prompt_calls = execute_calls = quit_calls = recent_calls = 0;
  flush_calls = 0;
  state_exists = 1; dialog_result = 0; restored_slot = -1; popup_value = -1;
  gamepak_filename[0] = 0; sound_pause = 1;
  first_run = toast_calls = queued = 0; queued_at_run = -1;
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
  clear_test(); first_run = 1; start(2);
  assert(execute_calls == 1 && prompt_calls == 1 && queued_at_run == 1);
  clear_test(); first_run = 1; regular_file = 1; start(1);
  assert(execute_calls == 1 && queued_at_run == 1);
  clear_test(); first_run = 1; browser_result = 0; start(1);
  assert(execute_calls == 1 && queued_at_run == 1);
  clear_test(); first_run = 1; menu_result = 1; start(1);
  assert(execute_calls == 1 && queued_at_run == 1 && toast_calls == 1);
  clear_test(); first_run = 1; start(1);
  assert(execute_calls == 0 && quit_calls == 1 && toast_calls == 0);
  clear_test(); first_run = 1; load_result = -1; start(2);
  assert(execute_calls == 0 && toast_calls == 0);
  clear_test(); browser_result = 0; start(1);
  assert(execute_calls == 1 && queued_at_run == 0 && toast_calls == 0);
  puts("startup and auto-state regressions passed");
  return 0;
}
'''


# Host stand-ins for what setup_main() calls. Only load_config_file()
# matters here (config_result plays its 0 or -1), so the other calls
# compile to nothing.
SETUP_STUBS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned u32;
typedef int s32;
typedef int FastForwardSettings;
typedef void hook(void);
FastForwardSettings fast_forward_policy;
hook *__scePowerSetClockFrequency, *__draw_volume_status;
void scePowerSetClockFrequency(void) {}
void scePowerSetClockFrequency371(void) {}
void draw_volume_status_null(void) {}
u32 enable_home_menu, sound_pause;
s32 config_result;
int config_calls;
void fast_forward_settings_defaults(FastForwardSettings *s) { *s = 0; }
void fast_forward_policy_init(FastForwardSettings *p,
 const FastForwardSettings *s) { *p = *s; }
int sceKernelDevkitVersion(void) { return 0x06060010; }
void find_main_path(const char *p) { assert(strcmp(p, "EBOOT.PBP") == 0); }
s32 load_config_file(void) { config_calls++; return config_result; }
''' + "".join("#define %s(...) ((void)0)\n" % name for name in """
 scePowerLock initExceptionHandler init_input perf_monitor_init init_video
 video_resolution_large runtime_log_psp_init sceKernelRegisterSubIntrHandler
 sceKernelEnableSubIntr sceUtilityGetSystemParamInt
 rebuild_fast_forward_button_mask load_theme_config load_recent_roms
 sceImposeSetHomePopup load_setting_cfg load_bios_file init_gamepak_buffer
 set_sound_volume init_sound setup_callbacks scePowerUnlock""".split())


# Only a regular quit writes tempgba.cfg, so a config that does not load
# (-1) is a first run and one that loads (0) is not.
SETUP_SCENARIOS = r'''
static u32 run(s32 result) {
  u32 first_run;
  config_result = result; config_calls = 0;
  first_run = setup_main("EBOOT.PBP");
  assert(config_calls == 1);
  return first_run;
}
int main(void) {
  u32 missing = run(-1), present = run(0);
  assert(missing == 1 && present == 0);
  puts("first-run regressions passed");
  return 0;
}
'''


# Host stand-ins for load_setting_cfg() and set_default_directories().
# load_dir_cfg() is not spliced (its nested functions do not build on the
# host); dir_ini_result plays its return value. sceIoDopen() opens only the
# roms/ folder, and only when roms_present is set.
DIR_STUBS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH 512
#define MAX_FILE (MAX_PATH + 8) /* holds main_path + dir.ini without -Wformat-overflow */
#define CONFIRMATION_CONT 2
#define MSG_ERR_LOAD_DIR_INI 0
typedef int SceUID;
const char *messages[] = {"%sdir.ini is missing"};
#define MSG messages
char main_path[MAX_PATH] = "ms0:/PSP/GAME/X/";
char dir_roms[MAX_PATH], dir_save[MAX_PATH], dir_state[MAX_PATH];
char dir_cfg[MAX_PATH], dir_snap[MAX_PATH], dir_cheat[MAX_PATH];
char dir_boxart[MAX_PATH], dir_boxart_folders[MAX_PATH];
int dir_ini_result, roms_present, error_calls, open_dirs, dopen_calls;
int load_dir_cfg(char *name) {
  size_t n = strlen(main_path);
  assert(strncmp(name, main_path, n) == 0 && strcmp(name + n, "dir.ini") == 0);
  return dir_ini_result;
}
void error_msg(const char *m, int c) { (void)m; (void)c; error_calls++; }
SceUID sceIoDopen(const char *p) {
  dopen_calls++;
  if(!roms_present || strcmp(p, "ms0:/PSP/GAME/X/roms/") != 0) return -1;
  open_dirs++; return 3;
}
int sceIoDclose(SceUID d) { assert(d == 3); open_dirs--; return 0; }
'''


# 1.4.2 kept every folder in the EBOOT folder when dir.ini was missing, and
# saves and states must still be found there; only the ROM folder moves to
# roms/, when it exists. A dir.ini that loads keeps the folders it set.
DIR_SCENARIOS = r'''
static char *const dirs[] = {dir_roms, dir_save, dir_state, dir_cfg,
 dir_snap, dir_cheat, dir_boxart, dir_boxart_folders};
static void run(int result, int roms) {
  unsigned i;
  for(i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) strcpy(dirs[i], "set/");
  dir_ini_result = result; roms_present = roms; error_calls = open_dirs = dopen_calls = 0;
  load_setting_cfg();
  assert(error_calls == 0 && open_dirs == 0);
  for(i = 1; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    assert(strcmp(dirs[i], result < 0 ? main_path : "set/") == 0);
}
int main(void) {
  run(-1, 1); assert(strcmp(dir_roms, "ms0:/PSP/GAME/X/roms/") == 0);
  run(-1, 0); assert(strcmp(dir_roms, main_path) == 0);
  run(0, 1); assert(strcmp(dir_roms, "set/") == 0 && dopen_calls == 0);
  /* main_path + roms/ would not fit: keep main_path, open nothing. */
  memset(main_path, 'a', MAX_PATH - 5); main_path[MAX_PATH - 5] = '\0';
  run(-1, 1); assert(strcmp(dir_roms, main_path) == 0 && dopen_calls == 0);
  puts("dir.ini default regressions passed");
  return 0;
}
'''


def build_and_run(source, name):
    with tempfile.TemporaryDirectory(prefix="gbarush-startup-") as directory:
        path = Path(directory)
        (path / (name + ".c")).write_text(source)
        command = shlex.split(os.environ.get("HOST_CC", "cc"))
        subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                 str(path / (name + ".c")), "-o", str(path / name)],
                       check=True, capture_output=True, text=True)
        subprocess.run([str(path / name)], check=True)


class StartupTests(unittest.TestCase):
    def test_production_startup_paths(self):
        # Splice the real functions between the stubs and the scenarios.
        source = STUBS + function("source/src/gui.c",
                                 "void maybe_load_auto_savestate(void)")
        source += function("source/src/main.c", "int user_main(int argc, char *argv[])")
        source += SCENARIOS
        build_and_run(source, "startup")

    def test_missing_config_is_first_run(self):
        source = SETUP_STUBS + function("source/src/main.c",
                                       "static u32 setup_main(const char *eboot_path)")
        source += SETUP_SCENARIOS
        build_and_run(source, "first_run")

    def test_missing_dir_ini_needs_no_dialog(self):
        source = DIR_STUBS + function("source/src/gui.c",
                                     "void set_default_directories(void)")
        source += function("source/src/main.c", "static void load_setting_cfg(void)")
        source += DIR_SCENARIOS
        build_and_run(source, "dir_defaults")


if __name__ == "__main__":
    unittest.main()
