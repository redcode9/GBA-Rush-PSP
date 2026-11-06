"""Check how video_me.c reads media_engine.cfg: decimal, or hex after 0x."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

# Extracts a C function from a source file (see tests/test_startup.py).
from tests.test_startup import function

# Host stand-ins for the file calls: the file holds cfg_text, or is missing
# when it is NULL.
STUBS = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32;
typedef int SceUID;
#define MAX_PATH 512
#define PSP_O_RDONLY 1
char main_path[MAX_PATH] = "ms0:/PSP/GAME/GBARUSH/";
const char *cfg_text;
SceUID sceIoOpen(const char *path, int flags, int mode) {
  (void)path; (void)flags; (void)mode; return cfg_text ? 3 : -1;
}
int sceIoRead(SceUID file, void *data, unsigned size) {
  size_t bytes = strlen(cfg_text);
  (void)file;
  if(bytes > size) bytes = size;
  memcpy(data, cfg_text, bytes);
  return (int)bytes;
}
int sceIoClose(SceUID file) { (void)file; return 0; }
'''

CASES = r'''
static u32 switches_in(const char *text) { cfg_text = text; return read_switches(); }
int main(void) {
  assert(switches_in(NULL) == 0);
  assert(switches_in("") == 0);
  assert(switches_in("16\n") == 0x10);
  assert(switches_in("010") == 10);
  assert(switches_in("09\r\n") == 9);
  assert(switches_in("0x28") == 0x28);
  assert(switches_in("0X3f\n") == 0x3F);
  assert(switches_in("255") == 255);
  puts("media_engine.cfg checks passed");
  return 0;
}
'''


class MediaEngineCfgTests(unittest.TestCase):
    def test_switches_are_decimal_or_hex(self):
        source = STUBS + function("source/src/video_me.c", "static u32 read_switches(void)")
        source += CASES
        with tempfile.TemporaryDirectory(prefix="gbarush-me-cfg-") as directory:
            path = Path(directory)
            (path / "cfg.c").write_text(source)
            command = shlex.split(os.environ.get("HOST_CC", "cc"))
            subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                     str(path / "cfg.c"), "-o", str(path / "cfg")],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(path / "cfg")], check=True)


if __name__ == "__main__":
    unittest.main()
