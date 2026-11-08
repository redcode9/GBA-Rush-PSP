"""Check the built-in BIOS and the production code that picks and records it."""

import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

# Extracts a C function from a source file (see tests/test_startup.py).
from tests.test_startup import function


ROOT = Path(__file__).resolve().parents[1]
MEMORY = "source/src/memory.c"
# bios/open_gba_bios.bin of libretro/gpsp 5819380 (see source/bios/README.md).
OPEN_BIOS_SHA256 = "661a9afb93624f2c5e77d07dab137bd8d23cff79e8639c62fe621b49bb064749"


def open_bios_bytes():
    text = (ROOT / "source/src/open_bios.h").read_text()
    start = text.index("{", text.index("open_bios_rom[0x4000] ="))
    return bytes(int(value, 16) for value in
                 re.findall(r"0x([0-9a-f]{2})", text[start:text.rindex("}")]))


def savestate_layout():
    """memory.c's one-line SAVESTATE_* macros (the savestate offsets) and
    the savestate writer's static variables."""
    lines = (ROOT / MEMORY).read_text().splitlines()
    return "".join(line + "\n" for line in lines
                   if (line.startswith("#define SAVESTATE_") and not line.endswith("\\"))
                   or re.match(r"static [^(]*savestate_\w+(\[\w+\])?;$", line))


# Host stand-ins for what the BIOS and savestate functions use. The open file
# is file_data[0, file_size); file_size < 0 means it does not exist. The
# message popup must never appear while the power lock is held. Every state
# load recovers the slot under the power lock before it opens the file.
STUBS = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "gba_sio.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;
typedef int SceUID;
#include "open_bios.h"
#include "save_store.h"
#define MAX_IDLE_LOOPS 8
#define MAX_FILE 512
#define MAX_PATH 512
#define GBA_SCREEN_SIZE (240 * 160 * 2)
#define CHANGED_PC_STATUS 3
#define CONFIRMATION_CONT 2
enum { MSG_ERR_BIOS_INVALID, MSG_ERR_STATE_NEEDS_BIOS_FILE,
 MSG_ERR_STATE_NEEDS_BUILTIN_BIOS };
const char *MSG[] = {"invalid gba_bios.bin", "needs gba_bios.bin",
 "needs the built-in BIOS"};
enum { METADATA_AREA_EWRAM, METADATA_AREA_IWRAM, METADATA_AREA_VRAM };
enum { CLEAR_REASON_LOADING_STATE };
#define FILE_OPEN(f, name, mode) f = stub_open(name)
#define FILE_CHECK_VALID(f) ((f) >= 0)
#define FILE_SEEK(f, offset, type) ((void)(f), file_pos = (offset))
#define FILE_READ(f, buffer, size) ((void)(f), stub_read(buffer, size))
#define FILE_CLOSE(f) ((void)(f), closes++)
#define SAVESTATE_BLOCK(type) stub_block_##type(savestate_file);
struct BiosData { u8 rom[0x4000]; u16 metadata[0x4000]; } bios;
const u8 *file_data;
s32 file_size;
size_t file_pos;
const char *shown;
int closes, power_locked, blocks_read, recovers, state_opens;
static SaveStoreIo stub_io;
const SaveStoreIo *backup_io = &stub_io;
#define TICKS 0x0123456789ABCDEFull
static u64 ticker(void) { return TICKS; }
static void stub_block_read(SceUID f) { (void)f; blocks_read++; }
static void stub_block_write_mem(SceUID f);
GbaSio gba_sio;
u32 idle_loop_targets, idle_loop_target_pc[MAX_IDLE_LOOPS];
u32 smc_gate_targets, iwram_stack_optimize, bios_builtin;
u32 sound_pause, oam_update, gbc_sound_update, reg[32];
char dir_state[] = "states/";
char main_path[] = "";
static void add_smc_gate(u32 pc) { (void)pc; }
static void error_msg(const char *text, int confirm) {
  assert(confirm == CONFIRMATION_CONT && !power_locked); shown = text;
}
void gba_sio_save_snapshot(const GbaSio *sio, GbaSioSnapshot *snapshot) {
  (void)sio; memset(snapshot, 0xA5, sizeof(*snapshot));
}
static int main_link_active(void) { return 0; }
static void scePowerLock(int x) { (void)x; power_locked = 1; }
static void scePowerUnlock(int x) { (void)x; power_locked = 0; }
static void clear_metadata_area(int area, int reason) { (void)area; (void)reason; }
static void read_gba_sio_snapshot(SceUID f) { (void)f; }
static SceUID stub_open(const char *name) {
  assert(!strcmp(name, "gba_bios.bin") || !strcmp(name, "states/s.svs"));
  if (!strcmp(name, "states/s.svs")) { state_opens++; assert(recovers == state_opens); }
  file_pos = 0; return file_size < 0 ? -1 : 3;
}
SaveStoreResult save_store_recover(const SaveStoreIo *io, const char *dir,
 const char *name) {
  assert(power_locked && io == backup_io && !strcmp(dir, "states/"));
  assert(!strcmp(name, "s.svs"));
  recovers++; return SAVE_STORE_OK;
}
static s32 stub_read(void *buffer, size_t size) {
  size_t left = (file_size > (s32)file_pos) ? (size_t)file_size - file_pos : 0;
  size_t n = size < left ? size : left;
  memcpy(buffer, file_data + file_pos, n); file_pos += n;
  return (s32)n;
}
'''

# A 16 KB (or longer) file that starts with the GBA reset vector is used as
# is. A missing file and a copy of the built-in image (-1) load the built-in
# BIOS silently; any other file (-2) loads it and load_bios_file reports it.
# The game_config.txt BIOS patches touch gba_bios.bin only (0x2C is IRQ
# handler code in the built-in BIOS). A savestate records the BIOS kind in
# the word before the link snapshot, and load_state_silent refuses one made
# with the other BIOS: it closes the file unread and reports it after the
# power lock is released. Zero padding (older savestates) means gba_bios.bin.
# A savestate streams through the store in 16 KiB blocks and is refused,
# the old file kept, when the module data reaches the BIOS word; any store
# failure fails the save the same way.
SCENARIOS = r'''
static u8 image[0x5000];
static u8 state[SAVESTATE_SIZE];
static u8 store_tmp[SAVESTATE_SIZE];
static u16 screen[GBA_SCREEN_SIZE / 2];
static u8 piece[0x40000];
static size_t modules;
static int begins, commits;
#define MODULES_FULL (SAVESTATE_BIOS_OFFSET - GBA_SCREEN_SIZE - sizeof(u64))
/* The store as save_state_file uses it: the appends gather in a .tmp that a
   commit without a failure makes the file (state); else the file stays. */
SaveStoreResult save_store_begin(SaveStoreWrite *transaction,
 const SaveStoreIo *io, const char *dir, const char *name) {
  assert(power_locked && io == backup_io && !strcmp(dir, "states/"));
  assert(!strcmp(name, "s.svs") && begins == commits);
  begins++; transaction->size = 0; transaction->result = SAVE_STORE_OK;
  return SAVE_STORE_OK;
}
SaveStoreResult save_store_append(SaveStoreWrite *transaction,
 const uint8_t *data, size_t size) {
  assert(begins == commits + 1 && size <= SAVESTATE_CHUNK);
  if (transaction->result == SAVE_STORE_OK) {
    assert(transaction->size + size <= sizeof(store_tmp));
    memcpy(store_tmp + transaction->size, data, size);
    transaction->size += size;
  }
  return transaction->result;
}
static SaveStoreResult commit_result; /* a failure after the appends */
SaveStoreResult save_store_commit(SaveStoreWrite *transaction) {
  commits++; assert(power_locked && commits == begins);
  if (transaction->result == SAVE_STORE_OK) {
    assert(transaction->size == SAVESTATE_SIZE);
    transaction->result = commit_result;
  }
  if (transaction->result == SAVE_STORE_OK)
    memcpy(state, store_tmp, SAVESTATE_SIZE);
  return transaction->result;
}
/* The modules' data: a counting pattern, in pieces (one as large as EWRAM)
   that straddle the 16 KiB blocks. */
static void stub_block_write_mem(SceUID f) {
  static const size_t sizes[] = {1, 0x40000, 7, 0x4001};
  size_t done = 0, n, i, k = 0;
  (void)f;
  while (done < modules) {
    n = sizes[k++ % 4];
    if (n > modules - done) n = modules - done;
    for (i = 0; i < n; i++) piece[i] = (u8)((done + i) % 251);
    savestate_write_mem(piece, n);
    done += n;
  }
}
/* A state save_state_file wrote: screenshot, timestamp, module data, zeros
   up to the BIOS word, the word, then the link snapshot to the end. */
static void check_state(u32 word) {
  u64 time;
  u32 found;
  size_t i;
  assert(!power_locked && begins == commits);
  assert(memcmp(state, screen, GBA_SCREEN_SIZE) == 0);
  memcpy(&time, state + GBA_SCREEN_SIZE, sizeof(time));
  assert(time == TICKS);
  for (i = 0; i < modules; i++)
    assert(state[GBA_SCREEN_SIZE + sizeof(u64) + i] == (u8)(i % 251));
  for (i += GBA_SCREEN_SIZE + sizeof(u64); i < SAVESTATE_BIOS_OFFSET; i++)
    assert(state[i] == 0);
  memcpy(&found, state + SAVESTATE_BIOS_OFFSET, sizeof(found));
  assert(found == word);
  for (i = SAVESTATE_SIO_OFFSET; i < SAVESTATE_SIZE; i++)
    assert(state[i] == 0xA5);
}
static void bios_file(s32 size, const char *vector) {
  memset(image, 0x5A, sizeof(image));
  memcpy(image, vector, 4);
  file_data = image; file_size = size;
}
int main(void) {
  u32 word;
  size_t i;

  bios_file(0x4000, "\x18\x00\x00\xea");
  assert(load_bios("gba_bios.bin") == 0 && bios_builtin == 0);
  assert(memcmp(bios.rom, image, sizeof(bios.rom)) == 0);
  bios_file(0x5000, "\x18\x00\x00\xea"); assert(load_bios("gba_bios.bin") == 0);
  bios_file(-1, "\x18\x00\x00\xea");
  assert(load_bios("gba_bios.bin") == -1 && bios_builtin == 1);
  assert(memcmp(bios.rom, open_bios_rom, sizeof(bios.rom)) == 0);
  bios_file(0x3FFF, "\x18\x00\x00\xea");
  assert(load_bios("gba_bios.bin") == -2 && bios_builtin == 1);
  bios_file(0x4000, "\x18\x00\x00\xeb"); assert(load_bios("gba_bios.bin") == -2);
  assert(memcmp(bios.rom, open_bios_rom, sizeof(bios.rom)) == 0);
  memcpy(image, open_bios_rom, sizeof(open_bios_rom));
  assert(load_bios("gba_bios.bin") == -1 && bios_builtin == 1);

  shown = NULL; bios_file(-1, "\x18\x00\x00\xea"); load_bios_file();
  assert(shown == NULL);
  memcpy(image, open_bios_rom, sizeof(open_bios_rom)); file_size = 0x4000;
  load_bios_file(); assert(shown == NULL);
  bios_file(100, "\x18\x00\x00\xea"); load_bios_file();
  assert(shown == MSG[MSG_ERR_BIOS_INVALID]);

  bios_file(0x4000, "\x18\x00\x00\xea"); load_bios("gba_bios.bin");
  apply_game_config_option("bios_rom_hack_39", "yes");
  apply_game_config_option("bios_rom_hack_2C", "yes");
  assert(bios.rom[0x39] == 0xC0 && bios.rom[0x2C] == 0x02);
  reset_game_config_options();
  assert(bios.rom[0x39] == 0x00 && bios.rom[0x2C] == 0x00);
  bios_file(-1, "\x18\x00\x00\xea"); load_bios("gba_bios.bin");
  reset_game_config_options();
  apply_game_config_option("bios_rom_hack_39", "yes");
  apply_game_config_option("bios_rom_hack_2C", "yes");
  assert(memcmp(bios.rom, open_bios_rom, sizeof(bios.rom)) == 0);

  for (i = 0; i < GBA_SCREEN_SIZE / 2; i++) screen[i] = (u16)(i * 3 + 1);
  file_data = state; file_size = SAVESTATE_SIZE;
  modules = MODULES_FULL;
  bios_builtin = 1; assert(save_state_file("s.svs", screen) == 1);
  check_state(SAVESTATE_BIOS_BUILTIN);
  memcpy(&word, state + SAVESTATE_BIOS_OFFSET, sizeof(word));
  assert(word == SAVESTATE_BIOS_BUILTIN && state[SAVESTATE_SIO_OFFSET] == 0xA5);
  assert(savestate_bios_matches(3) == 1);
  bios_builtin = 0; assert(savestate_bios_matches(3) == 0);
  show_savestate_bios_error();
  assert(shown == MSG[MSG_ERR_STATE_NEEDS_BUILTIN_BIOS]);
  modules = MODULES_FULL - 1;
  assert(save_state_file("s.svs", screen) == 1 && savestate_bios_matches(3) == 1);
  check_state(0);
  memset(state, 0, sizeof(state));
  assert(savestate_bios_matches(3) == 1);
  bios_builtin = 1; assert(savestate_bios_matches(3) == 0);
  show_savestate_bios_error();
  assert(shown == MSG[MSG_ERR_STATE_NEEDS_BIOS_FILE]);
  memset(state + SAVESTATE_BIOS_OFFSET, 0xFF, 4);
  assert(savestate_bios_matches(3) == 0);
  bios_builtin = 0; assert(savestate_bios_matches(3) == 1);
  modules = MODULES_FULL + 1;
  assert(save_state_file("s.svs", screen) == 0 && !power_locked);
  assert(begins == commits && state[0] == 0);
  assert(state[SAVESTATE_BIOS_OFFSET] == 0xFF && state[SAVESTATE_SIO_OFFSET] == 0);

  modules = 1000; bios_builtin = 1;
  assert(save_state_file("s.svs", screen) == 1);
  check_state(SAVESTATE_BIOS_BUILTIN);
  bios_builtin = 0; shown = NULL; closes = 0; blocks_read = 0;
  assert(load_state_silent("s.svs") == 0 && blocks_read == 0 && closes == 1);
  assert(shown == MSG[MSG_ERR_STATE_NEEDS_BUILTIN_BIOS] && !power_locked);
  bios_builtin = 1; shown = NULL; closes = 0;
  assert(load_state_silent("s.svs") == 1 && blocks_read == 1 && closes == 1);
  assert(shown == NULL && !power_locked);
  file_size = -1; closes = 0;
  assert(load_state_silent("s.svs") == 0 && closes == 0 && shown == NULL);
  assert(recovers == 3);

  commit_result = SAVE_STORE_VERIFY_FAILED; modules = 2000;
  assert(save_state_file("s.svs", screen) == 0);
  commit_result = SAVE_STORE_OK; modules = 1000;
  check_state(SAVESTATE_BIOS_BUILTIN);

  puts("BIOS fallback and savestate BIOS regressions passed");
  return 0;
}
'''


class OpenBiosTests(unittest.TestCase):
    def test_embedded_bios_matches_upstream(self):
        data = open_bios_bytes()
        self.assertEqual(len(data), 0x4000)
        self.assertEqual(hashlib.sha256(data).hexdigest(), OPEN_BIOS_SHA256)

    def test_production_bios_paths(self):
        # Splice the real functions between the stubs and the scenarios.
        source = STUBS + savestate_layout()
        for signature in (
                "s32 load_bios(char *name)",
                "static void reset_game_config_options(void)",
                "static void apply_game_config_option(const char *current_variable, "
                "const char *current_value)",
                "static u32 savestate_bios_matches(SceUID savestate_file)",
                "static void show_savestate_bios_error(void)",
                "void savestate_write_mem(const void *data, size_t size)",
                "static int write_savestate_tail(void)",
                "static u32 save_state_file(char *savestate_filename, u16 *screen_capture)",
                "u32 load_state_silent(char *savestate_filename)"):
            source += function(MEMORY, signature) + "\n"
        source += function("source/src/main.c", "static void load_bios_file(void)") + "\n"
        source += SCENARIOS
        with tempfile.TemporaryDirectory(prefix="gbarush-bios-") as directory:
            path = Path(directory)
            (path / "bios.c").write_text(source)
            command = shlex.split(os.environ.get("HOST_CC", "cc"))
            # The spliced functions call sprintf, which the macOS SDK marks
            # deprecated.
            result = subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                               "-Wno-deprecated-declarations",
                                               "-I", str(ROOT / "source/src"),
                                               str(path / "bios.c"), "-o", str(path / "bios")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(path / "bios")], check=True)


if __name__ == "__main__":
    unittest.main()
