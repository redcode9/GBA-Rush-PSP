"""Exercise production startup, auto-state and diagnostic autorun paths
with host API stubs."""

import os
from pathlib import Path
import re
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
typedef int32_t s32;
#define MAX_FILE 512
#define COLOR32_BLACK 0
#define CONFIRMATION_QUIT 1
#define CONFIRMATION_CONT 2
#define MSG_ERR_LOAD_GAMEPACK 0
#define LOAD_GAMEPAK_CANCELLED (-3)
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
int error_calls, flush_calls, browser_calls, menu_calls;
int popup_value, active_link, state_exists, dialog_result, restored_slot;
int first_run, toast_calls, queued, queued_at_run;
int prompt_name, restored_name_length;
jmp_buf stopped;
u32 setup_main(const char *p) { (void)p; return first_run; }
u32 sceKernelGetSystemTimeLow(void) { return 77; }
void toast_push(const char *text, uint32_t duration_us, int sjis, uint32_t now) {
  assert(strcmp(text, "menu hint") == 0 && duration_us == 6000000);
  assert(sjis == 0 && now == 77); toast_calls++; queued++;
}
int path_is_regular_file(const char *p) { (void)p; return regular_file; }
int load_gamepak(char *p) {
  if(load_result < 0) return load_result;
  strcpy(gamepak_filename, p); gamepak_file_size = 1024; return 0;
}
void clear_screen(int c) { (void)c; }
void error_msg(const char *m, int c) { (void)m; (void)c; error_calls++; }
void quit(void) { quit_calls++; longjmp(stopped, 1); }
u32 menu(void) {
  menu_calls++;
  if(menu_result) {
    strcpy(gamepak_filename, "menu.gba"); gamepak_file_size = 1024;
    reg[0] = 0xFEED; queued = 0;
  }
  return menu_result;
}
int load_file(const char **e, char *p, char *d, u32 r) {
  (void)e; (void)d; (void)r; browser_calls++; strcpy(p, "browser.gba");
  return browser_result;
}
void add_recent_rom(const char *p) { (void)p; recent_calls++; }
void reset_gba(void) { reset_calls++; reg[0] = 0xCAFE; queued = 0; }
#ifdef ENABLE_DIAGNOSTIC_AUTORUN
/* Like the real one: a ROM it starts is loaded and reset. */
int autorun_result;
int main_autorun_start(void) {
  if(autorun_result) {
    strcpy(gamepak_filename, "autorun.gba"); gamepak_file_size = 1024;
    reset_gba();
  }
  return autorun_result;
}
#endif
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
  (void)p; assert(queued == 0); prompt_calls++;
  prompt_name = gamepak_filename[0]; return dialog_result;
}
u32 action_loadstate_slot(u32 slot) {
  restored_slot = slot; restored_name_length = (int)strlen(gamepak_filename);
  return 1;
}
'''


# Launch orders in user_main(): a ROM path argument (argc 2), else
# roms/game.gba (regular_file), else the file browser, else the menu. A ROM
# picked in the menu runs without reset_gba() or the auto-state prompt; any
# other loaded ROM is reset and offers the auto state (slot 10). Every
# game saves the runtime log once before it runs. A failed load (-1, or
# -2 for a zip too big for the ROM buffer) shows the load error; one the
# player cancelled at the unreadable-save prompt goes on the same way
# without it. The last lines check that the prompt is skipped with no
# ROM, during a link session and without a saved state, and that the ROM
# name is blank while the prompt is open (a suspend then must not write
# the AUTO state) and back after either answer, before the state loads.
# With first_run set, every path that runs a game queues the 6 s menu hint
# once, after the reset and the auto-state prompt; the quit path and a
# loaded tempgba.cfg queue nothing.
SCENARIOS = r'''
static void clear_test(void) {
  regular_file = menu_result = active_link = 0;
  browser_result = -1; load_result = 0;
  reset_calls = prompt_calls = execute_calls = quit_calls = recent_calls = 0;
  error_calls = flush_calls = browser_calls = menu_calls = 0;
  state_exists = 1; dialog_result = 0; restored_slot = -1; popup_value = -1;
  prompt_name = restored_name_length = -1;
  gamepak_filename[0] = 0; sound_pause = 1;
  first_run = toast_calls = queued = 0; queued_at_run = -1;
#ifdef ENABLE_DIAGNOSTIC_AUTORUN
  autorun_result = 0;
#endif
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
  assert(execute_calls == 1 && reset_calls == 0 && error_calls == 1);
  clear_test(); browser_result = 0; load_result = -1; menu_result = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 0 && error_calls == 1);
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
  assert(execute_calls == 0 && quit_calls == 1 && error_calls == 1);
  clear_test(); load_result = -2; start(2);
  assert(execute_calls == 0 && quit_calls == 1 && error_calls == 1);
  clear_test(); load_result = LOAD_GAMEPAK_CANCELLED; start(2);
  assert(execute_calls == 0 && quit_calls == 1 && error_calls == 0);
  clear_test(); regular_file = 1; load_result = LOAD_GAMEPAK_CANCELLED;
  menu_result = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 0 && error_calls == 0);
  clear_test(); browser_result = 0; load_result = LOAD_GAMEPAK_CANCELLED;
  menu_result = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 0 && error_calls == 0);
  clear_test(); maybe_load_auto_savestate(); assert(prompt_calls == 0);
  strcpy(gamepak_filename, "game.gba"); active_link = 1;
  maybe_load_auto_savestate(); assert(prompt_calls == 0);
  active_link = 0; state_exists = 0;
  maybe_load_auto_savestate(); assert(prompt_calls == 0);
  state_exists = 1; dialog_result = 1;
  maybe_load_auto_savestate(); assert(prompt_calls == 1 && restored_slot == -1);
  assert(prompt_name == 0 && strcmp(gamepak_filename, "game.gba") == 0);
  prompt_name = -1; dialog_result = 0;
  maybe_load_auto_savestate(); assert(prompt_calls == 2 && restored_slot == 10);
  assert(prompt_name == 0 && restored_name_length == 8);
  assert(strcmp(gamepak_filename, "game.gba") == 0);
#ifdef ENABLE_DIAGNOSTIC_AUTORUN
  clear_test(); autorun_result = 1; start(2);
  assert(execute_calls == 1 && reset_calls == 1 && prompt_calls == 0);
  assert(browser_calls == 0 && menu_calls == 0 && recent_calls == 0);
  assert(strcmp(gamepak_filename, "autorun.gba") == 0);
  assert(sound_pause == 0 && popup_value == 1 && quit_calls == 1);
  clear_test(); autorun_result = 1; start(1);
  assert(execute_calls == 1 && reset_calls == 1 && prompt_calls == 0);
  assert(browser_calls == 0 && menu_calls == 0);
#endif
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
void *backup_io;
int gui_save_store_io;
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
 set_sound_volume init_sound setup_callbacks scePowerUnlock set_cpu_clock
 main_memory_sample""".split())


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


# The menu rereads the AUTO state once per write, and only once the suspend
# that wrote it has resumed and the state folder opens again.
REFRESH_STUBS = r'''
#include <assert.h>
#include <stdint.h>
typedef uint32_t u32;
typedef int SceUID;
char dir_state[] = "ms0:/PSP/GAME/X/";
static u32 auto_savestate_written;
volatile u32 sleep_auto_saved;
static int dopen_result, dopens, dcloses;
SceUID sceIoDopen(const char *path) { (void)path; dopens++; return dopen_result; }
int sceIoDclose(SceUID dir) { assert(dir == dopen_result); dcloses++; return 0; }
'''

REFRESH_SCENARIOS = r'''
int main(void) {
  dopen_result = 4;
  assert(!auto_savestate_refresh_due() && dopens == 0);
  /* Written by a suspend that has not resumed yet. */
  auto_savestate_written = 1; sleep_auto_saved = 1;
  assert(!auto_savestate_refresh_due() && dopens == 0 && auto_savestate_written);
  /* Resumed, but the Memory Stick is not back. */
  sleep_auto_saved = 0; dopen_result = -1;
  assert(!auto_savestate_refresh_due() && dopens == 1 && dcloses == 0);
  assert(auto_savestate_written);
  /* Back: one refresh, the folder closed, then nothing until the next write. */
  dopen_result = 4;
  assert(auto_savestate_refresh_due() && dcloses == 1 && !auto_savestate_written);
  assert(!auto_savestate_refresh_due() && dopens == 2);
  return 0;
}
'''


def autorun_block():
    """Return main.c's diagnostic autorun block, from its #ifdef to #endif."""
    text = (ROOT / "source/src/main.c").read_text()
    start = text.index("#ifdef ENABLE_DIAGNOSTIC_AUTORUN\n/* ---- Diagnostic autorun")
    return text[start:text.index("\n#endif\n", start) + len("\n#endif\n")]


def screen_defines():
    """main.h's screen scale and filter values, so the autorun block's
    check of the testpat= modes reads the real ones."""
    text = (ROOT / "source/src/main.h").read_text()
    return "\n".join(re.findall(
        r"^#define (?:SCALED_|SCREEN_SCALE_COUNT|FILTER_)\w* .*$", text,
        re.M)) + "\n"


# Host stand-ins for what the autorun block calls. The file is read back in
# pieces of at most 1000 bytes, each result file is kept whole, and quit()
# returns instead of exiting. The presenter names follow psp_video.c: x15-sw
# scales on the CPU, 16x9-sharp and 16x9-auto (at its start) are sharp, and
# int2 always samples nearest.
AUTORUN_STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "autorun_config.h"
#include "input_trace.h"
#include "runtime_log.h"
#include "state_hash.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int SceUID;
#define MAX_PATH 512
#define LOAD_GAMEPAK_CANCELLED (-3)
#define GBA_RUSH_BUILD_ID "abc123"
#define GBA_SCREEN_WIDTH (240)
#define GBA_SCREEN_HEIGHT (160)
#define GBA_LINE_SIZE (256)
#define FILE_OPEN(tag, name, mode) tag = stub_open(name, #mode)
#define FILE_CHECK_VALID(tag) ((tag) >= 0)
#define FILE_CLOSE(tag) stub_close(tag)
#define FILE_READ(tag, buffer, size) stub_read(tag, buffer, size)
#define FILE_WRITE(tag, buffer, size) stub_write(tag, buffer, size)
char main_path[MAX_PATH] = "ms0:/GAME/";
char gamepak_filename[MAX_PATH];
u32 cpu_ticks, gamepak_file_size;
/* main.c declares it outside the autorun block, for update_gba. */
static u32 main_autorun_running;
static char file_text[8192], result[2048], loaded[MAX_PATH], traced[MAX_PATH];
static size_t file_size, file_at, result_length;
static int file_exists, read_error, open_files, load_result, trace_result;
static int results, events, flushes, quits, resets;
static u32 event_flags, event_values[3];
static SceUID stub_open(const char *name, const char *mode) {
  assert(strcmp(name, strcmp(mode, "READ") == 0 ?
   "ms0:/GAME/diag-autorun.txt" : "ms0:/GAME/diag-autorun.result") == 0);
  if(strcmp(mode, "READ") == 0 && !file_exists) return -1;
  if(strcmp(mode, "WRITE") == 0) { results++; result_length = 0; result[0] = '\0'; }
  open_files++; file_at = 0; return 3;
}
static void stub_close(SceUID file) { assert(file == 3); open_files--; }
static int stub_read(SceUID file, char *buffer, size_t size) {
  size_t n = 0;
  assert(file == 3);
  if(read_error) return -1;
  while(n < size && n < 1000 && file_at < file_size)
    buffer[n++] = file_text[file_at++];
  return (int)n;
}
static int stub_write(SceUID file, const char *buffer, size_t size) {
  assert(file == 3 && result_length + size < sizeof(result));
  memcpy(result + result_length, buffer, size);
  result_length += size; result[result_length] = '\0';
  return (int)size;
}
void runtime_log_psp_main(RuntimeLogEventType type, uint32_t cycle,
 uint8_t flags, uint32_t value0, uint32_t value1, uint32_t value2) {
  (void)cycle; assert(type == RUNTIME_LOG_EVENT_DIAG_AUTORUN); events++;
  event_flags = flags; event_values[0] = value0;
  event_values[1] = value1; event_values[2] = value2;
}
int runtime_log_psp_flush(RuntimeLogFlushReason reason) {
  assert(reason == RUNTIME_LOG_FLUSH_START); flushes++; return 1;
}
/* load_gamepak's .sav prompts and load_state_silent's popup for a .svs
   made with the other BIOS are off while the ROM loads and the trace
   starts: loaded_quiet and traced_quiet say whether they were. */
u32 dialogs_unattended;
static u32 loaded_quiet, traced_quiet;
s32 load_gamepak(char *name) {
  strcpy(loaded, name); loaded_quiet = dialogs_unattended;
  if(load_result > 0) { strcpy(gamepak_filename, name); gamepak_file_size = 1; }
  return load_result;
}
void reset_gba(void) { resets++; }
/* A trace that starts is replaying until it finishes or is cancelled. */
static InputTraceState trace_state;
int gui_trace_replay_file(const char *name) {
  strcpy(traced, name); traced_quiet = dialogs_unattended;
  if(trace_result > 0) trace_state = INPUT_TRACE_REPLAYING;
  return trace_result;
}
InputTraceState input_diagnostic_trace_state(void) { return trace_state; }
static StateHashLog hash_log;
static char hashed[MAX_PATH];
static int hashes_attached, hash_result, cancels, perturbs;
static u32 perturb_frame, trace_duration;
const StateHashLog *input_diagnostic_trace_hashes(void) {
  return hashes_attached ? &hash_log : NULL;
}
void input_diagnostic_trace_cancel(void) {
  cancels++; hashes_attached = 0; trace_state = INPUT_TRACE_IDLE;
}
u32 input_diagnostic_trace_duration(void) { return trace_duration; }
void input_diagnostic_trace_hashes_perturb(u32 frame) {
  assert(hashes_attached); perturbs++; perturb_frame = frame;
}
int gui_trace_write_hashes(const char *name) {
  assert(hashes_attached); strcpy(hashed, name); hashes_attached = 0;
  return hash_result;
}
u32 option_screen_scale, option_screen_filter, option_screen_mag;
static u32 quit_scale, quit_filter, quit_mag;
void quit(void) {
  quits++; quit_scale = option_screen_scale; quit_filter = option_screen_filter;
  quit_mag = option_screen_mag;
}
static u16 texture[GBA_LINE_SIZE * (GBA_SCREEN_HEIGHT + 1)];
u16 *screen_texture = texture;
static u32 shown_scale[16], shown_filter[16], results_at_mode[16];
/* The result file as it was when each mode began. */
static char result_at_mode[16][1024];
static u32 modes_shown, presents, flips, ticks;
#define PSP_POWER_TICK_ALL 0
#define PSP_POWER_TICK_DISPLAY 6
/* Every tick is ALL: DISPLAY holds the backlight only, not Auto Sleep. A
   testpat= run ticks before each frame it presents. */
int scePowerTick(int type) {
  assert(type == PSP_POWER_TICK_ALL && (modes_shown == 0 || ticks == presents));
  ticks++; return 0;
}
void video_sync_present(void) {}
void video_resolution_small(void) {
  assert(modes_shown < 16 && option_screen_mag == 170);
  shown_scale[modes_shown] = option_screen_scale;
  shown_filter[modes_shown] = option_screen_filter;
  assert(result_length < sizeof(result_at_mode[0]));
  strcpy(result_at_mode[modes_shown], result);
  results_at_mode[modes_shown++] = (u32)results;
}
static void present(void) { presents++; }
void (*update_screen)(void) = present;
void flip_screen(u32 vsync) { assert(vsync == 1 && flips < presents); flips++; }
void video_presenter_names(const char **presenter, const char **filter) {
  u32 sharp = option_screen_scale == 5 || option_screen_scale == 6;
  *presenter = option_screen_scale == 2 ? "sw" : sharp ? "sharp" : "gu";
  *filter = option_screen_scale == 2 ? "none" :
   sharp || option_screen_scale == 7 || option_screen_filter == 0 ?
   "nearest" : "linear";
}
static const NetFaultProfile *fault_profile;
static int fault_sets;
void psp_adhoc_set_fault(const NetFaultProfile *profile) {
  fault_profile = profile; fault_sets++;
}
int main_autorun_start(void);
void main_autorun_frame(void);
'''


# No file; refused files (another build, 4097 bytes, a read error); runs
# with and without frames= and exit=1, with relative and absolute paths;
# ROM and trace failures. A refused or failed file stores the log at once.
AUTORUN_SCENARIOS = r'''
#define HEAD "version=1\nbuild=abc123\n"
static void clear_test(void) {
  file_exists = 1; read_error = 0; load_result = trace_result = 1;
  results = events = flushes = quits = resets = 0;
  hashes_attached = hash_result = 1; cancels = perturbs = 0;
  perturb_frame = 0; trace_duration = 9; hashed[0] = '\0';
  result[0] = loaded[0] = traced[0] = gamepak_filename[0] = '\0';
  gamepak_file_size = 1;
  loaded_quiet = traced_quiet = dialogs_unattended = 0;
  trace_state = INPUT_TRACE_IDLE;
  main_autorun_running = main_autorun_frames = 0;
  main_testpat_length = 0; modes_shown = presents = flips = ticks = 0;
  fault_profile = &net_fault_profiles[0]; fault_sets = 0;
  quit_scale = quit_filter = quit_mag = 99;
}
static int start_size(size_t size) {
  int started;
  file_size = size;
  started = main_autorun_start();
  assert(open_files == 0);
  return started;
}
static int start(const char *text) {
  strcpy(file_text, text);
  return start_size(strlen(text));
}
static void frames(int count) { while(count-- > 0) main_autorun_frame(); }
static void expect(AutorunStatus status, const char *reason, u32 line,
 u32 frames_run) {
  static const char *names[] = { "ignored", "failed", "started", "done" };
  char text[256];
  snprintf(text, sizeof(text), "version=1\nbuild=abc123\nstatus=%s\n"
   "reason=%s\nline=%u\nframes=%u\n", names[status], reason,
   (unsigned)line, (unsigned)frames_run);
  assert(strcmp(result, text) == 0);
  assert(event_flags == (u32)status && event_values[1] == line);
  assert(event_values[2] == frames_run);
  assert(strcmp(autorun_config_error_name((AutorunError)event_values[0]),
   reason) == 0);
}
int main(void) {
  char text[2048];
  u32 i;

  clear_test(); file_exists = 0;
  assert(start("") == 0 && results == 0 && events == 0 && flushes == 0);

  clear_test();
  assert(start("version=1\nbuild=abc124\nrom=a.gba\nframes=5\nexit=1\n") == 0);
  assert(loaded[0] == '\0' && quits == 0 && flushes == 1 && fault_sets == 0);
  expect(AUTORUN_STATUS_IGNORED, "build-mismatch", 2, 0);
  clear_test();
  assert(start("version=1\nbuild=abc124\nnet_profile=noisy\n") == 0);
  assert(fault_sets == 0);

  /* net_profile= reaches the ad-hoc worker before any link starts, with or
     without a ROM to run. */
  clear_test(); assert(start(HEAD "rom=a.gba\nnet_profile=noisy\n"));
  assert(fault_sets == 1 && fault_profile == &net_fault_profiles[2]);
  clear_test(); assert(start(HEAD "net_profile=dropouts\n") == 0);
  assert(fault_sets == 1 && fault_profile == &net_fault_profiles[5]);
  expect(AUTORUN_STATUS_STARTED, "none", 0, 0);
  clear_test(); assert(start(HEAD "rom=a.gba\n"));
  assert(fault_sets == 1 && fault_profile == NULL);
  clear_test(); assert(start(HEAD "net_profile=Noisy\n") == 0);
  assert(fault_sets == 0);
  expect(AUTORUN_STATUS_IGNORED, "bad-value", 3, 0);

  clear_test(); memset(file_text, '\n', 4097); memcpy(file_text, HEAD, sizeof(HEAD) - 1);
  assert(start_size(4096) == 0 && flushes == 0);
  expect(AUTORUN_STATUS_STARTED, "none", 0, 0);
  clear_test(); assert(start_size(4097) == 0 && flushes == 1);
  expect(AUTORUN_STATUS_IGNORED, "too-large", 0, 0);
  clear_test(); read_error = 1; assert(start(HEAD) == 0 && flushes == 1);
  expect(AUTORUN_STATUS_IGNORED, "read", 0, 0);

  clear_test();
  assert(start(HEAD "rom=roms/a.gba\ntrace=traces/t.gbit\nframes=3\nexit=1\n"));
  assert(strcmp(loaded, "ms0:/GAME/roms/a.gba") == 0 && resets == 1);
  assert(strcmp(traced, "ms0:/GAME/traces/t.gbit") == 0 && flushes == 0);
  assert(loaded_quiet == 1 && traced_quiet == 1 && dialogs_unattended == 0);
  expect(AUTORUN_STATUS_STARTED, "none", 0, 0);
  frames(2); assert(results == 1 && quits == 0);
  frames(1); assert(results == 2 && quits == 1);
  expect(AUTORUN_STATUS_DONE, "none", 0, 3);
  frames(5); assert(results == 2 && quits == 1);

  clear_test(); assert(start(HEAD "rom=ms0:/ROMS/b.gba\nframes=2\n"));
  assert(strcmp(loaded, "ms0:/ROMS/b.gba") == 0 && traced[0] == '\0');
  frames(10); assert(results == 2 && quits == 0);
  expect(AUTORUN_STATUS_DONE, "none", 0, 2);
  clear_test(); assert(start(HEAD "rom=b.gba\n"));
  frames(10); assert(results == 1 && ticks == 0);
  /* Nobody presses a button: from its first frame to its last, a frames=
     run keeps Auto Sleep away about once a second. */
  clear_test(); assert(start(HEAD "rom=b.gba\nframes=130\n") && ticks == 0);
  frames(1); assert(ticks == 1);
  frames(59); assert(ticks == 1);
  frames(1); assert(ticks == 2);
  frames(69); assert(ticks == 3 && results == 2);
  frames(100); assert(ticks == 3);
  expect(AUTORUN_STATUS_DONE, "none", 0, 130);

  clear_test(); load_result = -1; assert(start(HEAD "rom=b.gba\n") == 0);
  assert(resets == 0 && quits == 0 && flushes == 1);
  assert(gamepak_filename[0] == '\0' && gamepak_file_size == 0);
  expect(AUTORUN_STATUS_FAILED, "rom", 0, 0);
  clear_test(); load_result = 0; assert(start(HEAD "rom=b.gba\n") == 0);
  expect(AUTORUN_STATUS_FAILED, "rom", 0, 0);
  clear_test(); assert(start(HEAD "rom=roms/game\n") == 0 && loaded[0] == '\0');
  expect(AUTORUN_STATUS_FAILED, "rom", 0, 0);
  clear_test(); load_result = -1;
  assert(start(HEAD "rom=b.gba\nframes=9\nexit=1\n") == 0 && quits == 1);
  /* A .sav that will not read: load_gamepak refuses it without its
     prompts, so the ROM fails and exit=1 quits. */
  clear_test(); load_result = LOAD_GAMEPAK_CANCELLED;
  assert(start(HEAD "rom=b.gba\nframes=9\nexit=1\n") == 0 && quits == 1);
  assert(loaded_quiet == 1 && dialogs_unattended == 0 && resets == 0);
  expect(AUTORUN_STATUS_FAILED, "rom", 0, 0);

  clear_test(); trace_result = 0;
  assert(start(HEAD "rom=b.gba\ntrace=t.gbit\n") == 0);
  assert(resets == 1 && quits == 0 && flushes == 1);
  assert(gamepak_filename[0] == '\0' && gamepak_file_size == 0);
  expect(AUTORUN_STATUS_FAILED, "trace", 0, 0);
  clear_test(); trace_result = -1;
  assert(start(HEAD "rom=b.gba\ntrace=t.gbit\nframes=9\nexit=1\n") == 0);
  assert(quits == 1);
  expect(AUTORUN_STATUS_FAILED, "trace", 0, 0);
  /* A .svs made with the other BIOS: the load fails with no popup to wait
     on, so the trace fails and exit=1 quits. */
  clear_test(); trace_result = 0;
  assert(start(HEAD "rom=b.gba\ntrace=t.gbit\nframes=9\nexit=1\n") == 0);
  assert(traced_quiet == 1 && dialogs_unattended == 0 && quits == 1);
  expect(AUTORUN_STATUS_FAILED, "trace", 0, 0);

  /* hash= writes the replay's hashes once, when frames= runs out;
     perturb= reaches input.c before the first frame. */
  clear_test();
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=3\nexit=1\n"
               "hash=out/a.gbsh\nperturb=2\n"));
  assert(perturbs == 1 && perturb_frame == 2 && cancels == 0);
  expect(AUTORUN_STATUS_STARTED, "none", 0, 0);
  frames(2); assert(hashed[0] == '\0' && results == 1);
  frames(1); assert(strcmp(hashed, "ms0:/GAME/out/a.gbsh") == 0);
  assert(results == 2 && quits == 1 && flushes == 0);
  expect(AUTORUN_STATUS_DONE, "none", 0, 3);
  clear_test();
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=2\nhash=ms0:/h.gbsh\n"));
  assert(perturbs == 0);
  frames(2); assert(strcmp(hashed, "ms0:/h.gbsh") == 0 && quits == 0);
  expect(AUTORUN_STATUS_DONE, "none", 0, 2);
  /* Without hash= nothing is written, with or without hashes. */
  clear_test(); hashes_attached = 0;
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=2\n"));
  frames(2); assert(hashed[0] == '\0' && cancels == 0);
  expect(AUTORUN_STATUS_DONE, "none", 0, 2);

  clear_test(); hash_result = 0;
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=2\nexit=1\nhash=h.gbsh\n"));
  frames(2); assert(hashed[0] != '\0' && quits == 1 && flushes == 1);
  expect(AUTORUN_STATUS_FAILED, "hash", 0, 2);
  clear_test(); hashes_attached = 0;
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=2\nexit=1\nhash=h.gbsh\n"
               "perturb=1\n") == 0);
  assert(cancels == 1 && perturbs == 0 && quits == 1 && flushes == 1);
  assert(gamepak_filename[0] == '\0' && gamepak_file_size == 0);
  expect(AUTORUN_STATUS_FAILED, "no-hash-memory", 0, 0);

  /* A suspend cancels the replay (psp_sleep_loop): the run fails at the
     next frame, with the frames run before it, and writes no hashes. A
     replay that ends before frames= runs out is done. */
  clear_test();
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=5\nexit=1\nhash=h.gbsh\n"));
  frames(2); input_diagnostic_trace_cancel();
  frames(1); assert(results == 2 && quits == 1 && flushes == 1);
  assert(hashed[0] == '\0');
  expect(AUTORUN_STATUS_FAILED, "trace", 0, 2);
  frames(5); assert(results == 2 && quits == 1);
  clear_test(); assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=3\n"));
  frames(1); trace_state = INPUT_TRACE_FINISHED;
  frames(2); assert(results == 2 && quits == 0);
  expect(AUTORUN_STATUS_DONE, "none", 0, 3);

  /* perturb= at the trace's last frame flips; past it nothing would. */
  clear_test(); trace_duration = 4;
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=9\nhash=h.gbsh\n"
               "perturb=4\n"));
  assert(perturbs == 1 && perturb_frame == 4 && cancels == 0);
  clear_test(); trace_duration = 3;
  assert(start(HEAD "rom=a.gba\ntrace=t.gbit\nframes=9\nexit=1\n"
               "hash=h.gbsh\nperturb=4\n") == 0);
  assert(cancels == 1 && perturbs == 0 && quits == 1 && flushes == 1);
  assert(gamepak_filename[0] == '\0' && gamepak_file_size == 0);
  expect(AUTORUN_STATUS_FAILED, "perturb-past-trace", 0, 0);

  /* testpat=all: every scale, nearest first, the result rewritten before
     the first mode and after each one, started with the modes and frames
     so far until the last says done; the user's screen options are back
     when it quits. */
  clear_test(); option_screen_scale = 6; option_screen_filter = 1;
  option_screen_mag = 120; memset(texture, 0xAA, sizeof(texture));
  assert(start(HEAD "testpat=all\n") == 0);
  assert(modes_shown == AUTORUN_TESTPAT_MODES && loaded[0] == '\0');
  for(i = 0; i <= AUTORUN_TESTPAT_MODES; i++)
  {
    static const char *const lines[AUTORUN_TESTPAT_MODES] = {
     "testpat=none-nearest used=gu/nearest\n",
     "testpat=none-linear used=gu/linear\n",
     "testpat=x15-gu-nearest used=gu/nearest\n",
     "testpat=x15-gu-linear used=gu/linear\n",
     "testpat=x15-sw-nearest used=sw/none\n",
     "testpat=x15-sw-linear used=sw/none\n",
     "testpat=user-nearest used=gu/nearest\n",
     "testpat=user-linear used=gu/linear\n",
     "testpat=16x9-gu-nearest used=gu/nearest\n",
     "testpat=16x9-gu-linear used=gu/linear\n",
     "testpat=16x9-sharp-nearest used=sharp/nearest\n",
     "testpat=16x9-sharp-linear used=sharp/nearest\n",
     "testpat=16x9-auto-nearest used=sharp/nearest\n",
     "testpat=16x9-auto-linear used=sharp/nearest\n",
     "testpat=int2-nearest used=gu/nearest\n",
     "testpat=int2-linear used=gu/nearest\n" };
    u32 shown;

    snprintf(text, sizeof(text), "version=1\nbuild=abc123\nstatus=%s\n"
     "reason=none\nline=0\nframes=%u\n",
     i == AUTORUN_TESTPAT_MODES ? "done" : "started",
     (unsigned)(i * AUTORUN_TESTPAT_HOLD_FRAMES));
    for(shown = 0; shown < i; shown++)
      strcat(text, lines[shown]);
    if(i == AUTORUN_TESTPAT_MODES)
    {
      assert(strcmp(result, text) == 0);
      break;
    }
    assert(shown_scale[i] == i / 2 && shown_filter[i] == i % 2);
    assert(results_at_mode[i] == i + 1);
    assert(strcmp(result_at_mode[i], text) == 0);
  }
  assert(presents == AUTORUN_TESTPAT_MODES * AUTORUN_TESTPAT_HOLD_FRAMES);
  assert(flips == presents && ticks == presents);
  assert((u32)results == AUTORUN_TESTPAT_MODES + 1);
  assert(quits == 1 && quit_scale == 6 && quit_filter == 1 && quit_mag == 120);
  assert(flushes == 0 && event_flags == AUTORUN_STATUS_DONE);
  assert(event_values[2] == presents);
  /* Eight bars of 30 pixels on every row, nothing past the 240x160 frame. */
  for(i = 0; i < 8; i++)
  {
    static const u16 bars[8] =
     { 0x7FFF, 0x0000, 0x001F, 0x03E0, 0x7C00, 0x03FF, 0x7FE0, 0x7C1F };
    assert(texture[i * 30] == bars[i] && texture[i * 30 + 29] == bars[i]);
    assert(texture[159 * GBA_LINE_SIZE + i * 30 + 15] == bars[i]);
  }
  assert(texture[240] == 0xAAAA && texture[159 * GBA_LINE_SIZE + 255] == 0xAAAA);
  assert(texture[160 * GBA_LINE_SIZE] == 0xAAAA);

  /* One mode. */
  clear_test(); option_screen_scale = 1; option_screen_filter = 0;
  option_screen_mag = 100;
  assert(start(HEAD "testpat=16x9-auto-linear\n") == 0);
  assert(modes_shown == 1 && shown_scale[0] == 6 && shown_filter[0] == 1);
  assert(presents == AUTORUN_TESTPAT_HOLD_FRAMES && results == 2);
  assert(quits == 1 && quit_scale == 1 && quit_filter == 0 && quit_mag == 100);
  snprintf(text, sizeof(text), "version=1\nbuild=abc123\nstatus=done\n"
   "reason=none\nline=0\nframes=%u\n"
   "testpat=16x9-auto-linear used=sharp/nearest\n", (unsigned)presents);
  assert(strcmp(result, text) == 0);

  /* Refused: with a ROM run, or an unknown mode; nothing is shown. */
  clear_test();
  assert(start(HEAD "testpat=all\nrom=a.gba\nframes=9\nexit=1\n") == 0);
  assert(modes_shown == 0 && loaded[0] == '\0' && quits == 0 && flushes == 1);
  expect(AUTORUN_STATUS_IGNORED, "testpat-alone", 4, 0);
  clear_test(); assert(start(HEAD "testpat=x15\n") == 0 && modes_shown == 0);
  expect(AUTORUN_STATUS_IGNORED, "bad-value", 3, 0);
  puts("diagnostic autorun regressions passed");
  return 0;
}
'''


def build_and_run(source, name, flags=()):
    """Build one C file with HOST_CC and the given flags, then run it."""
    with tempfile.TemporaryDirectory(prefix="gbarush-startup-") as directory:
        path = Path(directory)
        (path / (name + ".c")).write_text(source)
        command = shlex.split(os.environ.get("HOST_CC", "cc"))
        build = subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror"]
                               + list(flags) + [str(path / (name + ".c")),
                                                "-o", str(path / name)],
                               capture_output=True, text=True)
        if build.returncode != 0:
            raise AssertionError(build.stdout + build.stderr)
        subprocess.run([str(path / name)], check=True)


class StartupTests(unittest.TestCase):
    def test_production_startup_paths(self):
        # Splice the real functions between the stubs and the scenarios,
        # once as the release builds them and once with the diagnostic
        # autorun, whose stub starts a ROM in the extra scenarios only.
        source = STUBS + function("source/src/gui.c",
                                 "void maybe_load_auto_savestate(void)")
        source += function("source/src/main.c", "int user_main(int argc, char *argv[])")
        source += SCENARIOS
        for defines in ([], ["-DENABLE_DIAGNOSTIC_AUTORUN"]):
            with self.subTest(defines=defines):
                build_and_run(source, "startup", defines)

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

    def test_auto_state_refresh_waits_for_resume(self):
        source = REFRESH_STUBS + function("source/src/gui.c",
                                             "static u32 auto_savestate_refresh_due(void)")
        build_and_run(source + REFRESH_SCENARIOS, "auto_refresh")

    def test_diagnostic_autorun_glue(self):
        # main.c's autorun block with the real parser behind it.
        build_and_run(screen_defines() + AUTORUN_STUBS
                      + autorun_block() + AUTORUN_SCENARIOS, "autorun",
                      ["-DENABLE_DIAGNOSTIC_AUTORUN",
                       "-DENABLE_DIAGNOSTIC_NET_INJECTION",
                       "-I" + str(ROOT / "source/src"),
                       str(ROOT / "source/src/autorun_config.c"),
                       str(ROOT / "source/src/net_fault.c")])


if __name__ == "__main__":
    unittest.main()
