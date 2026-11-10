"""Check gui.c's shared sceIo binding with a scripted fake sceIo layer."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
import zlib

# Extracts a C function from a source file (see tests/test_startup.py).
from tests.test_startup import function


ROOT = Path(__file__).resolve().parents[1]
FILE_SIZE = 40000
# The fake file's bytes, filled the same way in the C below.
CONTENT = bytes((i * 7 + 3) & 0xFF for i in range(FILE_SIZE))

# PSPSDK and common.h stand-ins, and one fake file behind scripted sceIo
# calls. Each read or write moves at most `chunk` bytes; the call numbered
# fail_call since the last open (or reset) returns fail_value instead. Only
# the names in stat_names stat. crc32() is zlib's, bit by bit.
STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "save_store.h"
typedef uint8_t u8;
typedef int32_t s32;
typedef uint64_t u64;
typedef unsigned long uLong;
typedef unsigned int uInt;
typedef int SceUID;
typedef int SceMode;
typedef unsigned int SceSize;
typedef int64_t SceOff;
typedef struct { int st_mode; SceOff st_size; } SceIoStat;
#define Z_NULL 0
#define MAX_PATH 512
#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_CREAT 0x0200
#define PSP_O_TRUNC 0x0400
#define PSP_SEEK_SET 0
#define PSP_SEEK_END 2
#define PSP_FILE_OPEN_READ "r"
#define FILE_OPEN(tag, name, mode) tag = psp_fopen(name, PSP_FILE_OPEN_##mode)
#define FILE_CHECK_VALID(tag) ((tag) >= 0)
#define FILE_CLOSE(tag) psp_fclose(tag)
#define FILE_READ(tag, buffer, size) sceIoRead(tag, buffer, size)
#define FILE_SEEK(tag, offset, type) sceIoLseek(tag, offset, PSP_##type)
char main_path[] = "ms0:/PSP/GAME/GBARush/";
u8 file_data[FILE_SIZE], written[64];
size_t file_length, position, chunk, written_length;
int open_result, open_flags, closes, close_result, calls, fail_call;
int fail_value, syncs, sync_result, renames, rename_result, removes;
int remove_result, dopen_result, dcloses, seek_fail;
char synced[16], opened_folder[MAX_PATH];
const char *stat_names[2];

static void reset(void) {
  open_result = 3; open_flags = closes = close_result = 0;
  calls = fail_call = fail_value = 0;
  file_length = FILE_SIZE; position = 0; chunk = FILE_SIZE;
  written_length = 0; syncs = sync_result = 0; synced[0] = '\0';
  renames = rename_result = removes = remove_result = 0;
  stat_names[0] = stat_names[1] = NULL;
  dopen_result = 5; dcloses = 0; opened_folder[0] = '\0'; seek_fail = 0;
}
uLong crc32(uLong crc, const u8 *data, uInt length) {
  int bit;
  if(data == Z_NULL) return 0;
  crc = ~crc & 0xFFFFFFFFul;
  while(length--) {
    crc ^= *data++;
    for(bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320ul & (0ul - (crc & 1)));
  }
  return ~crc & 0xFFFFFFFFul;
}
SceUID sceIoOpen(const char *path, int flags, SceMode mode) {
  (void)path; (void)mode;
  open_flags = flags; position = 0; calls = 0;
  return open_result;
}
int sceIoClose(SceUID file) {
  assert(file == open_result); closes++; return close_result;
}
int sceIoWrite(SceUID file, const void *data, SceSize size) {
  size_t moved = size < chunk ? size : chunk;
  (void)file;
  if(++calls == fail_call) return fail_value;
  assert(written_length + moved <= sizeof(written));
  memcpy(written + written_length, data, moved);
  written_length += moved;
  return (int)moved;
}
int sceIoRead(SceUID file, void *data, SceSize size) {
  size_t moved = size < chunk ? size : chunk;
  (void)file;
  if(++calls == fail_call) return fail_value;
  if(moved > file_length - position) moved = file_length - position;
  memcpy(data, file_data + position, moved);
  position += moved;
  return (int)moved;
}
SceOff sceIoLseek(SceUID file, SceOff offset, int whence) {
  (void)file;
  if(whence == PSP_SEEK_END) {
    if(seek_fail == 1) return -1;
    position = file_length; return (SceOff)file_length;
  }
  assert(whence == PSP_SEEK_SET && offset == 0);
  if(seek_fail == 2) return -1;
  position = 0; return 0;
}
int sceIoSync(const char *device, unsigned int unknown) {
  (void)unknown; syncs++; strcpy(synced, device); return sync_result;
}
int sceIoRename(const char *source, const char *target) {
  (void)source; (void)target; renames++; return rename_result;
}
int sceIoGetstat(const char *path, SceIoStat *stat) {
  int i;
  (void)stat;
  for(i = 0; i < 2; i++)
    if(stat_names[i] != NULL && strcmp(path, stat_names[i]) == 0) return 0;
  return -1;
}
int sceIoRemove(const char *path) { (void)path; removes++; return remove_result; }
SceUID sceIoDopen(const char *path) { strcpy(opened_folder, path); return dopen_result; }
int sceIoDclose(SceUID dir) { assert(dir == dopen_result); dcloses++; return 0; }
SceUID psp_fopen(const char *path, const char *mode) {
  assert(strcmp(mode, "r") == 0); return sceIoOpen(path, PSP_O_RDONLY, 0777);
}
void psp_fclose(SceUID file) { if(file >= 0) sceIoClose(file); }
'''

# Through the table memory.c uses: every negative sceIo result on the write
# side, and a write that moves nothing, must come back as a failure. A read
# error or an early end of file only ends the read short, and a folder that
# will not open counts as present.
SCENARIOS = r'''
int main(void) {
  const SaveStoreIo *io = &gui_save_store_io;
  const uint8_t data[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  static u8 buffer[FILE_SIZE];
  char long_path[MAX_PATH + 8];
  uint32_t crc;
  size_t i, size, file_size;

  for(i = 0; i < FILE_SIZE; i++) file_data[i] = (u8)(i * 7 + 3);

  reset();
  assert(io->open_write(NULL, "ms0:/x.sav") == 3);
  assert(open_flags == (PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC));
  open_result = -1; assert(io->open_write(NULL, "ms0:/x.sav") < 0);

  reset(); chunk = 3;
  assert(io->write(NULL, 3, data, 10) == 10 && calls == 4);
  assert(written_length == 10 && memcmp(written, data, 10) == 0);
  reset(); chunk = 3; fail_call = 2;
  assert(io->write(NULL, 3, data, 10) == 0 && calls == 2);
  reset(); chunk = 3; fail_call = 3; fail_value = -1;
  assert(io->write(NULL, 3, data, 10) == 0 && calls == 3);

  reset(); assert(io->close(NULL, 3) == 1 && closes == 1);
  close_result = -1; assert(io->close(NULL, 3) == 0);

  reset(); assert(io->flush(NULL, "ef0:/PSP/SAVEDATA/x.sav") == 1);
  assert(strcmp(synced, "ef0:") == 0);
  reset(); assert(io->flush(NULL, "x.sav") == 1 && strcmp(synced, "ms0:") == 0);
  reset(); assert(io->flush(NULL, "abcdef:/x.sav") == 1);
  assert(strcmp(synced, "abcdef:") == 0);
  reset(); assert(io->flush(NULL, "abcdefg:/x.sav") == 0 && syncs == 0);
  reset(); sync_result = -1;
  assert(io->flush(NULL, "ms0:/x.sav") == 0 && syncs == 1);
  reset(); main_path[3] = '_';
  assert(io->flush(NULL, "x.sav") == 0 && syncs == 0);
  main_path[3] = ':';

  reset(); stat_names[0] = "b";
  assert(io->move(NULL, "a", "b") == 1 && renames == 1);
  rename_result = -1; assert(io->move(NULL, "a", "b") == 0);
  reset(); assert(io->move(NULL, "a", "b") == 0 && renames == 1);
  reset(); stat_names[0] = "a"; stat_names[1] = "b";
  assert(io->move(NULL, "a", "b") == 0);

  reset(); assert(io->remove(NULL, "a") == 1 && removes == 0);
  stat_names[0] = "a"; assert(io->remove(NULL, "a") == 1 && removes == 1);
  remove_result = -1; assert(io->remove(NULL, "a") == 0);

  reset(); stat_names[0] = "ms0:/SAVE/x.sav";
  assert(io->exists(NULL, "ms0:/SAVE/x.sav") == 1);
  reset(); assert(io->exists(NULL, "ms0:/SAVE/x.sav") == 0 && dcloses == 1);
  assert(strcmp(opened_folder, "ms0:/SAVE/") == 0);
  reset(); dopen_result = -1;
  assert(io->exists(NULL, "ms0:/SAVE/x.sav") == 1 && dcloses == 0);
  reset(); assert(io->exists(NULL, "x.sav") == 0 && opened_folder[0] == '\0');
  /* A folder name that does not fit MAX_PATH counts as there. */
  memset(long_path, 'a', sizeof(long_path) - 1);
  long_path[sizeof(long_path) - 1] = '\0'; long_path[MAX_PATH - 1] = '/';
  reset(); assert(io->exists(NULL, long_path) == 1 && opened_folder[0] == '\0');

  reset(); open_result = -1;
  assert(io->read(NULL, "s", buffer, FILE_SIZE, &size, &file_size) == 0);
  reset(); seek_fail = 1;
  assert(io->read(NULL, "s", buffer, FILE_SIZE, &size, &file_size) == 0);
  assert(closes == 1);
  reset(); seek_fail = 2;
  assert(io->read(NULL, "s", buffer, FILE_SIZE, &size, &file_size) == 0);
  assert(closes == 1);
  reset(); chunk = 1000;
  assert(io->read(NULL, "s", buffer, FILE_SIZE, &size, &file_size) == 1);
  assert(open_flags == PSP_O_RDONLY && closes == 1);
  assert(size == FILE_SIZE && file_size == FILE_SIZE);
  assert(memcmp(buffer, file_data, FILE_SIZE) == 0);
  /* A read error stops early: the short count tells the caller. */
  reset(); chunk = 1000; fail_call = 3; fail_value = -1;
  assert(io->read(NULL, "s", buffer, FILE_SIZE, &size, &file_size) == 1);
  assert(size == 2000 && file_size == FILE_SIZE && closes == 1);
  /* So does an end of file before the length the seek gave. */
  reset(); chunk = 1000; fail_call = 3; fail_value = 0;
  assert(io->read(NULL, "s", buffer, FILE_SIZE, &size, &file_size) == 1);
  assert(size == 2000 && closes == 1);
  reset(); memset(buffer, 0, FILE_SIZE);
  assert(io->read(NULL, "s", buffer, 100, &size, &file_size) == 1);
  assert(size == 100 && file_size == FILE_SIZE && buffer[100] == 0);
  assert(memcmp(buffer, file_data, 100) == 0);

  /* 16 KiB blocks: three reads and the one that finds the end. */
  reset(); assert(io->crc32(NULL, "s", &crc, &size) == 1);
  assert(crc == EXPECTED_CRC && size == FILE_SIZE && calls == 4);
  assert(open_flags == PSP_O_RDONLY && closes == 1);
  reset(); open_result = -1; assert(io->crc32(NULL, "s", &crc, &size) == 0);
  reset(); fail_call = 2; fail_value = -1;
  assert(io->crc32(NULL, "s", &crc, &size) == 0 && closes == 1);
  reset(); close_result = -1; assert(io->crc32(NULL, "s", &crc, &size) == 0);

  reset(); chunk = 4;
  assert(config_io_write(NULL, "c", data, 10) == 1 && written_length == 10);
  assert(closes == 1);
  reset(); open_result = -1;
  assert(config_io_write(NULL, "c", data, 10) == 0 && calls == 0);
  assert(closes == 0);
  reset(); chunk = 4; fail_call = 2;
  assert(config_io_write(NULL, "c", data, 10) == 0 && closes == 1);
  reset(); close_result = -1;
  assert(config_io_write(NULL, "c", data, 10) == 0 && written_length == 10);
  puts("sceIo binding regressions passed");
  return 0;
}
'''

FUNCTIONS = [
    "static int32_t save_io_open_write(void *context, const char *path)",
    "static int save_io_write(void *context, int32_t file, const uint8_t *data,\n size_t size)",
    "static int save_io_close(void *context, int32_t file)",
    "static int save_io_flush(void *context, const char *path)",
    "static int config_io_write(void *context, const char *path,\n const uint8_t *data, size_t size)",
    "static int config_io_move(void *context, const char *source,\n const char *target)",
    "static int config_io_remove(void *context, const char *path)",
    "static int save_io_exists(void *context, const char *path)",
    "static int save_io_read(void *context, const char *path, uint8_t *data,\n size_t capacity, size_t *size, size_t *file_size)",
    "static int save_io_crc32(void *context, const char *path, uint32_t *crc,\n size_t *size)",
    "const SaveStoreIo gui_save_store_io =",
]


class SaveIoTests(unittest.TestCase):
    def test_production_sceio_binding(self):
        # The real gui.c functions and table, between the stubs and scenarios.
        source = (f"#define FILE_SIZE {FILE_SIZE}\n"
                  f"#define EXPECTED_CRC 0x{zlib.crc32(CONTENT):08X}u\n" + STUBS)
        for signature in FUNCTIONS:
            source += function("source/src/gui.c", signature) + "\n"
        source += ";\n" + SCENARIOS
        with tempfile.TemporaryDirectory(prefix="gbarush-save-io-") as directory:
            path = Path(directory)
            (path / "save_io.c").write_text(source)
            command = shlex.split(os.environ.get("HOST_CC", "cc"))
            result = subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                               "-I", str(ROOT / "source/src"),
                                               str(path / "save_io.c"),
                                               "-o", str(path / "save_io")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(path / "save_io")], check=True)


if __name__ == "__main__":
    unittest.main()
