"""Exercise production startup, auto-state and diagnostic autorun paths
with host API stubs."""

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
# instead of exiting.
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
#define RUNTIME_LOG_FLUSH_START 1
const char *messages[] = {"load error", "restore state"};
#define MSG messages
char gamepak_filename[512];
char dir_roms[] = "roms/";
u32 gamepak_file_size, reg[1], option_clock_speed, sound_pause;
u32 enable_home_menu;
int regular_file, browser_result, load_result, menu_result;
int reset_calls, prompt_calls, execute_calls, quit_calls, recent_calls;
int flush_calls, browser_calls, menu_calls;
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
  menu_calls++;
  if(menu_result) {
    strcpy(gamepak_filename, "menu.gba"); gamepak_file_size = 1024;
    reg[0] = 0xFEED;
  }
  return menu_result;
}
int load_file(const char **e, char *p, char *d, u32 r) {
  (void)e; (void)d; (void)r; browser_calls++; strcpy(p, "browser.gba");
  return browser_result;
}
void add_recent_rom(const char *p) { (void)p; recent_calls++; }
void reset_gba(void) { reset_calls++; reg[0] = 0xCAFE; }
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
  assert(flush_calls == 1);
}
int main_link_active(void) { return active_link; }
u32 auto_savestate_exists(void) { return state_exists; }
u32 yesno_dialog(const char *p) { (void)p; prompt_calls++; return dialog_result; }
u32 action_loadstate_slot(u32 slot) { restored_slot = slot; return 1; }
'''


# Launch orders in user_main(): a ROM path argument (argc 2), else
# roms/game.gba (regular_file), else the file browser, else the menu. A ROM
# picked in the menu runs without reset_gba() or the auto-state prompt; any
# other loaded ROM is reset and offers the auto state (slot 10). Every
# game saves the runtime log once before it runs. The last
# lines check that the prompt is skipped with no ROM, during a link session
# and without a saved state.
SCENARIOS = r'''
static void clear_test(void) {
  regular_file = menu_result = active_link = 0;
  browser_result = -1; load_result = 0;
  reset_calls = prompt_calls = execute_calls = quit_calls = recent_calls = 0;
  flush_calls = browser_calls = menu_calls = 0;
  state_exists = 1; dialog_result = 0; restored_slot = -1; popup_value = -1;
  gamepak_filename[0] = 0; sound_pause = 1;
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
  puts("startup and auto-state regressions passed");
  return 0;
}
'''


def autorun_block():
    """Return main.c's diagnostic autorun block, from its #ifdef to #endif."""
    text = (ROOT / "source/src/main.c").read_text()
    start = text.index("#ifdef ENABLE_DIAGNOSTIC_AUTORUN\n/* ---- Diagnostic autorun")
    return text[start:text.index("\n#endif\n", start) + len("\n#endif\n")]


# Host stand-ins for what the autorun block calls. The file is read back in
# pieces of at most 1000 bytes, and quit() returns instead of exiting.
AUTORUN_STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "autorun_config.h"
#include "runtime_log.h"
#include "state_hash.h"
typedef uint8_t u8;
typedef int32_t s32;
typedef uint32_t u32;
typedef int SceUID;
#define MAX_PATH 512
#define GBA_RUSH_BUILD_ID "abc123"
#define FILE_OPEN(tag, name, mode) tag = stub_open(name, #mode)
#define FILE_CHECK_VALID(tag) ((tag) >= 0)
#define FILE_CLOSE(tag) stub_close(tag)
#define FILE_READ(tag, buffer, size) stub_read(tag, buffer, size)
#define FILE_WRITE(tag, buffer, size) stub_write(tag, buffer, size)
char main_path[MAX_PATH] = "ms0:/GAME/";
char gamepak_filename[MAX_PATH];
u32 cpu_ticks, gamepak_file_size;
static char file_text[8192], result[512], loaded[MAX_PATH], traced[MAX_PATH];
static size_t file_size, file_at;
static int file_exists, read_error, open_files, load_result, trace_result;
static int results, events, flushes, quits, resets;
static u32 event_flags, event_values[3];
static SceUID stub_open(const char *name, const char *mode) {
  assert(strcmp(name, strcmp(mode, "READ") == 0 ?
   "ms0:/GAME/diag-autorun.txt" : "ms0:/GAME/diag-autorun.result") == 0);
  if(strcmp(mode, "READ") == 0 && !file_exists) return -1;
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
  assert(file == 3 && size < sizeof(result));
  memcpy(result, buffer, size); result[size] = '\0'; results++;
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
s32 load_gamepak(char *name) {
  strcpy(loaded, name);
  if(load_result > 0) { strcpy(gamepak_filename, name); gamepak_file_size = 1; }
  return load_result;
}
void reset_gba(void) { resets++; }
int gui_trace_replay_file(const char *name) { strcpy(traced, name); return trace_result; }
static StateHashLog hash_log;
static char hashed[MAX_PATH];
static int hashes_attached, hash_result, cancels, perturbs;
static u32 perturb_frame, trace_duration;
const StateHashLog *input_diagnostic_trace_hashes(void) {
  return hashes_attached ? &hash_log : NULL;
}
void input_diagnostic_trace_cancel(void) { cancels++; hashes_attached = 0; }
u32 input_diagnostic_trace_duration(void) { return trace_duration; }
void input_diagnostic_trace_hashes_perturb(u32 frame) {
  assert(hashes_attached); perturbs++; perturb_frame = frame;
}
int gui_trace_write_hashes(const char *name) {
  assert(hashes_attached); strcpy(hashed, name); hashes_attached = 0;
  return hash_result;
}
void quit(void) { quits++; }
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
  main_autorun_running = main_autorun_frames = 0;
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
  clear_test(); file_exists = 0;
  assert(start("") == 0 && results == 0 && events == 0 && flushes == 0);

  clear_test();
  assert(start("version=1\nbuild=abc124\nrom=a.gba\nframes=5\nexit=1\n") == 0);
  assert(loaded[0] == '\0' && quits == 0 && flushes == 1);
  expect(AUTORUN_STATUS_IGNORED, "build-mismatch", 2, 0);

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
  frames(10); assert(results == 1);

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

  clear_test(); trace_result = 0;
  assert(start(HEAD "rom=b.gba\ntrace=t.gbit\n") == 0);
  assert(resets == 1 && quits == 0 && flushes == 1);
  assert(gamepak_filename[0] == '\0' && gamepak_file_size == 0);
  expect(AUTORUN_STATUS_FAILED, "trace", 0, 0);
  clear_test(); trace_result = -1;
  assert(start(HEAD "rom=b.gba\ntrace=t.gbit\nframes=9\nexit=1\n") == 0);
  assert(quits == 1);
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
  puts("diagnostic autorun regressions passed");
  return 0;
}
'''


def build_and_run(name, source, flags):
    """Build one C file with HOST_CC and the given flags, then run it."""
    with tempfile.TemporaryDirectory(prefix="gbarush-startup-") as directory:
        path = Path(directory)
        (path / (name + ".c")).write_text(source)
        command = shlex.split(os.environ.get("HOST_CC", "cc"))
        subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror"]
                       + flags + [str(path / (name + ".c")),
                                  "-o", str(path / name)],
                       check=True, capture_output=True, text=True)
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
                build_and_run("startup", source, defines)

    def test_diagnostic_autorun_glue(self):
        # main.c's autorun block with the real parser behind it.
        build_and_run("autorun", AUTORUN_STUBS + autorun_block() + AUTORUN_SCENARIOS,
                      ["-DENABLE_DIAGNOSTIC_AUTORUN", "-I" + str(ROOT / "source/src"),
                       str(ROOT / "source/src/autorun_config.c")])


if __name__ == "__main__":
    unittest.main()
