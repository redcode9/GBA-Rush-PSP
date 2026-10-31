"""Check production filename clipping and scrolling with host draw stubs."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

from tests.test_startup import function


ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "menu_layout.h"
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t u16;
u32 option_language;
#define LANGUAGE_JAPANESE 0
char drawn[1024];
int font;
void print_string(const char *s, u32 x, u32 y, u16 c, u16 b) {
  (void)x; (void)y; (void)c; (void)b; strcpy(drawn, s); font = 0;
}
void print_string_gbk(const char *s, u32 x, u32 y, u16 c, u16 b) {
  print_string(s, x, y, c, b); font = 1;
}
'''

SCENARIOS = r'''
int main(void) {
  char pair[] = { (char)0x81, (char)0x40, 'A', 'B', 0 };
  char kana[] = { (char)0xB1, (char)0x82, (char)0xA0, 'A', 0 };
  option_language = 2;
  print_string_clipped(pair, 0, 0, 0, 0, 1);
  assert(strlen(drawn) == 0 && font == 1);
  print_string_clipped(pair, 0, 0, 0, 0, 3);
  assert(strlen(drawn) == 3 && memcmp(drawn, pair, 3) == 0);
  print_string_scroll(pair, 0, 0, 0, 0, 2, 1);
  assert(strlen(drawn) == 2 && memcmp(drawn, pair, 2) == 0);
  print_string_scroll(pair, 0, 0, 0, 0, 2, 2);
  assert(strcmp(drawn, "AB") == 0);
  print_string_scroll(pair, 0, 0, 0, 0, 2, -1);
  assert(memcmp(drawn, pair, 2) == 0);
  option_language = 0;
  print_string_clipped(kana, 0, 0, 0, 0, 2);
  assert(strlen(drawn) == 1 && font == 0);
  print_string_scroll(kana, 0, 0, 0, 0, 2, 2);
  assert(strlen(drawn) == 2 && memcmp(drawn, kana + 1, 2) == 0);
  puts("browser clipping and scrolling regressions passed");
  return 0;
}
'''


class BrowserTextTests(unittest.TestCase):
    def test_production_browser_paths(self):
        source = STUBS
        source += function("source/src/gui.c",
                           "static void print_string_clipped(const char *str, u32 x, u32 y, u16 color, u16 bg, u32 max_chars)")
        source += function("source/src/gui.c",
                           "static void print_string_scroll(const char *str, u32 x, u32 y, u16 color, u16 bg, u32 max_chars, s32 scroll_offset)")
        source += SCENARIOS
        with tempfile.TemporaryDirectory(prefix="gbarush-browser-") as directory:
            path = Path(directory)
            (path / "browser.c").write_text(source)
            command = shlex.split(os.environ.get("HOST_CC", "cc"))
            result = subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror",
                                               "-I", str(ROOT / "source/src"),
                                               str(path / "browser.c"),
                                               str(ROOT / "source/src/menu_layout.c"),
                                               "-o", str(path / "browser")],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(path / "browser")], check=True)


if __name__ == "__main__":
    unittest.main()
