"""Check the built-in BIOS, the production code that picks and records it, and
savestates across the two BIOSes and the two CPU cores."""

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
    """memory.c's one-line SAVESTATE_* macros: the savestate offsets."""
    lines = (ROOT / MEMORY).read_text().splitlines()
    return "".join(line + "\n" for line in lines
                   if line.startswith("#define SAVESTATE_") and not line.endswith("\\"))


def macro(source, name):
    """One #define NAME(...) from a repository file, continuation lines included."""
    lines = (ROOT / source).read_text().splitlines()
    end = start = next(i for i, line in enumerate(lines)
                       if line.startswith("#define " + name + "("))
    while lines[end].endswith("\\"):
        end += 1
    return "\n".join(lines[start:end + 1]) + "\n"


# Host stand-ins for what the BIOS and savestate functions use. The open file
# is file_data[0, file_size); file_size < 0 means it does not exist. The
# message popup must never appear while the power lock is held.
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
#define SAVESTATE_BLOCK(type) blocks_##type++;
struct BiosData { u8 rom[0x4000]; u16 metadata[0x4000]; } bios;
const u8 *file_data;
s32 file_size;
size_t file_pos;
const char *shown;
int closes, power_locked, blocks_read;
u8 *write_mem_ptr;
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
  file_pos = 0; return file_size < 0 ? -1 : 3;
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
SCENARIOS = r'''
static u8 image[0x5000];
static u8 state[SAVESTATE_SIZE];
static void bios_file(s32 size, const char *vector) {
  memset(image, 0x5A, sizeof(image));
  memcpy(image, vector, 4);
  file_data = image; file_size = size;
}
int main(void) {
  u32 word;

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

  file_data = state; file_size = SAVESTATE_SIZE;
  write_mem_ptr = state + SAVESTATE_BIOS_OFFSET;
  bios_builtin = 1; assert(write_savestate_tail(state) == 1);
  memcpy(&word, state + SAVESTATE_BIOS_OFFSET, sizeof(word));
  assert(word == SAVESTATE_BIOS_BUILTIN && state[SAVESTATE_SIO_OFFSET] == 0xA5);
  assert(savestate_bios_matches(3) == 1);
  bios_builtin = 0; assert(savestate_bios_matches(3) == 0);
  show_savestate_bios_error();
  assert(shown == MSG[MSG_ERR_STATE_NEEDS_BUILTIN_BIOS]);
  assert(write_savestate_tail(state) == 1 && savestate_bios_matches(3) == 1);
  memset(state, 0, sizeof(state));
  assert(savestate_bios_matches(3) == 1);
  bios_builtin = 1; assert(savestate_bios_matches(3) == 0);
  show_savestate_bios_error();
  assert(shown == MSG[MSG_ERR_STATE_NEEDS_BIOS_FILE]);
  memset(state + SAVESTATE_BIOS_OFFSET, 0xFF, 4);
  assert(savestate_bios_matches(3) == 0);
  bios_builtin = 0; assert(savestate_bios_matches(3) == 1);
  write_mem_ptr = state + SAVESTATE_BIOS_OFFSET + 1;
  assert(write_savestate_tail(state) == 0);

  write_mem_ptr = state; bios_builtin = 1;
  assert(write_savestate_tail(state) == 1);
  bios_builtin = 0; shown = NULL; closes = 0; blocks_read = 0;
  assert(load_state_silent("s.svs") == 0 && blocks_read == 0 && closes == 1);
  assert(shown == MSG[MSG_ERR_STATE_NEEDS_BUILTIN_BIOS] && !power_locked);
  bios_builtin = 1; shown = NULL; closes = 0;
  assert(load_state_silent("s.svs") == 1 && blocks_read == 1 && closes == 1);
  assert(shown == NULL && !power_locked);
  file_size = -1; closes = 0;
  assert(load_state_silent("s.svs") == 0 && closes == 0 && shown == NULL);

  puts("BIOS fallback and savestate BIOS regressions passed");
  return 0;
}
'''


# The savestate path with both CPU cores: the real save_state_silent and
# load_state_silent around the real CPU block (cpu.c) and the new core's
# register copies (arm_gba.c), on the real reg[] layout. The other modules
# write nothing. The one savestate file lives in memory.
CORE_STUBS = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;
typedef uint64_t u64;
typedef int SceUID;
#define MAX_FILE 512
#define MAX_PATH 512
#include "cpu.h"
#include "memory.h"
#include "main.h"
#include "arm_mips.h"
#define GBA_SCREEN_SIZE (240 * 160 * 2)
enum { MSG_ERR_STATE_NEEDS_BIOS_FILE, MSG_ERR_STATE_NEEDS_BUILTIN_BIOS };
const char *MSG[] = {"needs gba_bios.bin", "needs the built-in BIOS"};
u32 reg[64], reg_mode[7][7], spsr[7];
s32 idle_loop_targets;
u32 idle_loop_target_pc[MAX_IDLE_LOOPS];
u32 bios_builtin, main_cpu_core, sound_pause, oam_update, gbc_sound_update;
u16 io_registers[0x400], palette_ram[0x200], oam_ram[0x200];
u8 ewram[0x40000], iwram[0x8000], *gamepak_rom, *write_mem_ptr;
u32 gamepak_size, gamepak_ram_buffer_size;
SceUID gamepak_file_large = GAMEPAK_FILE_NONE;
char dir_state[MAX_PATH] = "states/";
GbaSio gba_sio;
static ArmMips *jit;
static u32 cheat_hook;
static u8 file[0x80000];
static size_t file_pos;
static const char *shown;
#define FILE_OPEN(f, name, mode) f = stub_open(name)
#define FILE_CHECK_VALID(f) ((f) >= 0)
#define FILE_SEEK(f, offset, type) ((void)(f), file_pos = (offset))
#define FILE_READ(f, buffer, size) ((void)(f), stub_copy(buffer, file, size))
#define FILE_WRITE(f, buffer, size) ((void)(f), stub_copy(file, buffer, size))
#define FILE_CLOSE(f) ((void)(f))
static SceUID stub_open(const char *name) {
  assert(!strcmp(name, "states/s.svs")); file_pos = 0; return 3;
}
static s32 stub_copy(void *to, const void *from, size_t size) {
  assert(file_pos + size <= sizeof(file));
  memcpy((u8 *)to + (to == file ? file_pos : 0),
   (const u8 *)from + (from == file ? file_pos : 0), size);
  file_pos += size; return (s32)size;
}
#define DEFINE_NO_SAVESTATE(module) \
 void module##_read_savestate(SceUID f) { (void)f; } \
 void module##_write_mem_savestate(SceUID f) { (void)f; }
DEFINE_NO_SAVESTATE(input)
DEFINE_NO_SAVESTATE(main)
DEFINE_NO_SAVESTATE(sound)
DEFINE_NO_SAVESTATE(video)
static void memory_read_savestate(SceUID f) { (void)f; }
static void memory_write_mem_savestate(SceUID f) { (void)f; }
static void read_gba_sio_snapshot(SceUID f) { (void)f; }
void gba_sio_save_snapshot(const GbaSio *sio, GbaSioSnapshot *snapshot) {
  (void)sio; memset(snapshot, 0, sizeof(*snapshot));
}
int main_link_active(void) { return 0; }
static void scePowerLock(int x) { (void)x; }
static void scePowerUnlock(int x) { (void)x; }
void error_msg(const char *text, u8 confirm) {
  assert(confirm == CONFIRMATION_CONT); shown = text;
}
u64 ticker(void) { return 0; }
void *safe_malloc(size_t size) { return malloc(size); }
void clear_metadata_area(METADATA_AREA_TYPE area, METADATA_CLEAR_REASON_TYPE why) {
  (void)area; (void)why;
}
void process_cheats_at_hook(u32 pc) { (void)pc; }
"""

# Every register of every mode as the game sees it. A state saved by either
# core, with either BIOS, loads into either core with the same BIOS and
# reads back the same; with the other BIOS it is refused, naming the BIOS
# it needs. The saved modes are IRQ (in Thumb) and system; FIQ, which no
# GBA game uses, is only checked as a bank.
CORE_SCENARIOS = r"""
typedef struct {
  u32 r[16], cpsr, r13[MODE_INVALID], r14[MODE_INVALID], spsr[MODE_INVALID];
  u32 fiq[5];
} View;

static unsigned mode_of(u32 cpsr) {
  unsigned m;
  for(m = 0; m < MODE_INVALID; m++)
    if(arm_modes[m] == (cpsr & ARM_CPSR_MODE))
      return m;
  return MODE_USER;
}

static void make_view(View *s, u32 cpsr, u32 seed) {
  unsigned i, cur = mode_of(cpsr);
  memset(s, 0, sizeof(*s));
  for(i = 0; i < 16; i++) s->r[i] = seed * 0x01010101u + i;
  s->r[15] = 0x08000100u + seed * 4;
  s->cpsr = cpsr;
  for(i = 0; i < MODE_INVALID; i++) {
    s->r13[i] = 0x03007000u + seed * 0x100 + i * 0x10;
    s->r14[i] = 0x08001000u + seed * 0x100 + i * 0x10;
    s->spsr[i] = i == MODE_USER ? 0 : ARM_MODE_SYSTEM | (i << 28);
  }
  for(i = 0; i < 5; i++) s->fiq[i] = 0xF1000000u + seed * 0x10 + i;
  s->r[13] = s->r13[cur];
  s->r[14] = s->r14[cur];
}

/* The Classic core's arrays, as they are when update_gba() saves. */
static void classic_set(const View *s) {
  unsigned m, cur = mode_of(s->cpsr);
  memcpy(reg, s->r, sizeof(s->r));
  reg[REG_CPSR] = s->cpsr;
  reg[REG_N_FLAG] = s->cpsr >> 31 & 1;
  reg[REG_Z_FLAG] = s->cpsr >> 30 & 1;
  reg[REG_C_FLAG] = s->cpsr >> 29 & 1;
  reg[REG_V_FLAG] = s->cpsr >> 28 & 1;
  reg[CPU_MODE] = cur;
  for(m = 0; m < MODE_INVALID; m++) {
    reg_mode[m][5] = s->r13[m];
    reg_mode[m][6] = s->r14[m];
    spsr[m] = s->spsr[m];
  }
  memcpy(reg_mode[MODE_FIQ], s->fiq, sizeof(s->fiq));
}

static View classic_view(void) {
  View v;
  unsigned m, cur = reg[CPU_MODE];
  memset(&v, 0, sizeof(v));
  memcpy(v.r, reg, sizeof(v.r));
  v.cpsr = reg[REG_CPSR];
  assert(reg[REG_N_FLAG] == (v.cpsr >> 31 & 1) && reg[REG_Z_FLAG] == (v.cpsr >> 30 & 1));
  assert(reg[REG_C_FLAG] == (v.cpsr >> 29 & 1) && reg[REG_V_FLAG] == (v.cpsr >> 28 & 1));
  assert(cur == mode_of(v.cpsr));
  for(m = 0; m < MODE_INVALID; m++) {
    v.r13[m] = m == cur ? reg[13] : reg_mode[m][5];
    v.r14[m] = m == cur ? reg[14] : reg_mode[m][6];
    v.spsr[m] = m == MODE_USER ? 0 : spsr[m];
  }
  memcpy(v.fiq, reg_mode[MODE_FIQ], sizeof(v.fiq));
  return v;
}

static void new_set(const View *s) {
  ArmCore *core = &jit->core;
  unsigned m, r;
  arm_core_reset(core);
  arm_core_set_cpsr(core, s->cpsr);
  memcpy(core->v, s->r, sizeof(s->r));
  for(m = 0; m < MODE_INVALID; m++) {
    *arm_core_register(core, arm_modes[m], 13) = s->r13[m];
    *arm_core_register(core, arm_modes[m], 14) = s->r14[m];
    if(m != MODE_USER)
      *arm_core_register(core, arm_modes[m], 16) = s->spsr[m];
  }
  for(r = 0; r < 5; r++)
    *arm_core_register(core, ARM_MODE_FIQ, 8 + r) = s->fiq[r];
}

static View new_view(void) {
  ArmCore *core = &jit->core;
  View v;
  unsigned m, r;
  memset(&v, 0, sizeof(v));
  memcpy(v.r, core->v, sizeof(v.r));
  v.cpsr = arm_core_cpsr(core);
  for(m = 0; m < MODE_INVALID; m++) {
    v.r13[m] = *arm_core_register(core, arm_modes[m], 13);
    v.r14[m] = *arm_core_register(core, arm_modes[m], 14);
    if(m != MODE_USER)
      v.spsr[m] = *arm_core_register(core, arm_modes[m], 16);
  }
  for(r = 0; r < 5; r++)
    v.fiq[r] = *arm_core_register(core, ARM_MODE_FIQ, 8 + r);
  return v;
}

int main(void) {
  static const ArmBus no_bus;
  static const u32 cpsrs[2] = {
    ARM_MODE_IRQ | ARM_CPSR_I | ARM_CPSR_T | ARM_CPSR_N | ARM_CPSR_C,
    ARM_MODE_SYSTEM | ARM_CPSR_Z | ARM_CPSR_V
  };
  static u16 screen[GBA_SCREEN_SIZE / 2];
  unsigned c, saver, made, loader, with, loads = 0;

  jit = calloc(1, sizeof(*jit));
  assert(jit && sizeof(file) == SAVESTATE_SIZE);
  arm_core_init(&jit->core, &no_bus);
  for(c = 0; c < 2; c++)
  for(saver = CPU_CORE_CLASSIC; saver <= CPU_CORE_NEW; saver++)
  for(made = 0; made < 2; made++) {
    View s, got;
    make_view(&s, cpsrs[c], 1 + c * 4 + saver * 2 + made);
    memset(reg, 0xEE, sizeof(reg)); memset(reg_mode, 0xEE, sizeof(reg_mode));
    memset(spsr, 0xEE, sizeof(spsr));
    if(saver == CPU_CORE_NEW) new_set(&s); else classic_set(&s);
    main_cpu_core = saver; bios_builtin = made;
    assert(save_state_silent("s.svs", screen) == 1);
    for(loader = CPU_CORE_CLASSIC; loader <= CPU_CORE_NEW; loader++)
    for(with = 0; with < 2; with++) {
      memset(reg, 0xEE, sizeof(reg)); memset(reg_mode, 0xEE, sizeof(reg_mode));
      memset(spsr, 0xEE, sizeof(spsr)); arm_core_reset(&jit->core);
      main_cpu_core = loader; bios_builtin = with; shown = NULL;
      if(with != made) {
        assert(load_state_silent("s.svs") == 0);
        assert(shown == MSG[made ? MSG_ERR_STATE_NEEDS_BUILTIN_BIOS :
         MSG_ERR_STATE_NEEDS_BIOS_FILE]);
        continue;
      }
      assert(load_state_silent("s.svs") == 1 && shown == NULL);
      assert(reg[CHANGED_PC_STATUS] == 1);
      if(loader == CPU_CORE_NEW) { load_registers(); got = new_view(); }
      else got = classic_view();
      assert(memcmp(&got, &s, sizeof(s)) == 0);
      loads++;
    }
  }
  assert(loads == 16);
  puts("savestates load across the two cores and are refused across BIOSes");
  return 0;
}
"""


def compile_and_run(test, name, source, extra=(), flags=()):
    """Builds source with HOST_CC (and extra repository files) and runs it."""
    with tempfile.TemporaryDirectory(prefix="gbarush-bios-") as directory:
        path = Path(directory)
        (path / (name + ".c")).write_text(source)
        command = shlex.split(os.environ.get("HOST_CC", "cc"))
        # The spliced functions call sprintf, which the macOS SDK marks
        # deprecated.
        result = subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                           "-Wno-deprecated-declarations",
                                           "-I", str(ROOT / "source/src")] + list(flags) +
                                [str(path / (name + ".c"))] +
                                [str(ROOT / f) for f in extra] +
                                ["-o", str(path / name)],
                                capture_output=True, text=True)
        test.assertEqual(result.returncode, 0, result.stderr)
        subprocess.run([str(path / name)], check=True)


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
                "static int write_savestate_tail(u8 *savestate_buffer)",
                "u32 load_state_silent(char *savestate_filename)"):
            source += function(MEMORY, signature) + "\n"
        source += function("source/src/main.c", "static void load_bios_file(void)") + "\n"
        source += SCENARIOS
        compile_and_run(self, "bios", source)

    def test_savestates_across_cores(self):
        source = CORE_STUBS + savestate_layout()
        for name in ("FILE_WRITE_MEM", "FILE_WRITE_MEM_VARIABLE",
                     "FILE_WRITE_MEM_ARRAY", "FILE_READ_ARRAY"):
            source += macro("source/src/common.h", name)
        source += macro(MEMORY, "SAVESTATE_BLOCK") + macro("source/src/cpu.c", "CPU_SAVESTATE_BODY")
        source += function("source/src/arm_gba.c", "static const u8 arm_modes[MODE_INVALID] =") + ";\n"
        for signature in (
                "static void update_irq_line(void)",
                "static void map_region(ArmCore *core, unsigned region, const void *read,\n"
                " void *write, u32 mask)",
                "static void map_memory(ArmCore *core)",
                "static void run_cheat_hook(ArmCore *core)",
                "static u32 *banked(ArmCore *core, unsigned mode, unsigned r)",
                "static void load_registers(void)",
                "void arm_gba_save_registers(void)"):
            source += function("source/src/arm_gba.c", signature) + "\n"
        for signature in ("void cpu_read_savestate(SceUID savestate_file)",
                          "void cpu_write_mem_savestate(SceUID savestate_file)"):
            source += function("source/src/cpu.c", signature) + "\n"
        for signature in (
                "static u32 savestate_bios_matches(SceUID savestate_file)",
                "static void show_savestate_bios_error(void)",
                "static int write_savestate_tail(u8 *savestate_buffer)",
                "u32 load_state_silent(char *savestate_filename)",
                "u32 save_state_silent(char *savestate_filename, u16 *screen_capture)"):
            source += function(MEMORY, signature) + "\n"
        source += CORE_SCENARIOS
        # FILE_WRITE_MEM leaves its file argument unused.
        compile_and_run(self, "cores", source,
                        ("source/src/arm_decode.c", "source/src/arm_interp.c"),
                        ("-Wno-unused-parameter",))


if __name__ == "__main__":
    unittest.main()
