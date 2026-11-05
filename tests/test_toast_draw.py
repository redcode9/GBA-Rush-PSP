"""Check the in-game toast glue in gui.c and main.c on the host."""

from pathlib import Path
import unittest

# Extracts a C function from a source file and builds a spliced test.
from tests.test_startup import build_and_run, function


SRC = (Path(__file__).resolve().parents[1] / "source/src").as_posix()

# The real toast_show() between stand-ins for MSG and the PSP clock, with
# the real toast ring. Message 1 is 40 GBK characters (80 bytes), longer
# than a toast.
STUBS = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "@SRC@/menu_layout.c"
#include "@SRC@/toast.c"
typedef uint32_t u32;
static const char *messages[] = { "Slot %u: %s", "@LONG@" };
#define MSG messages
static u32 now_us = 1000;
u32 sceKernelGetSystemTimeLow(void) { return now_us; }
'''.replace("@SRC@", SRC).replace("@LONG@", "\\xB0\\xA1" * 40)

# A formatted message is queued as GBK for 3 s from now, and a long one is
# cut to whole double-byte characters.
SCENARIOS = r'''
int main(void) {
  const Toast *toast;
  toast_show(0, 3u, "AUTO");
  toast = toast_current(now_us);
  assert(toast != NULL && strcmp(toast->text, "Slot 3: AUTO") == 0);
  assert(toast->sjis == 0 && toast->start_us == now_us);
  assert(toast->duration_us == 3000000u);
  toast_clear();
  toast_show(1);
  toast = toast_current(now_us);
  assert(toast != NULL && strlen(toast->text) == 62);
  assert(memcmp(toast->text, messages[1], 62) == 0);
  puts("toast_show passed");
  return 0;
}
'''


class ToastDrawTests(unittest.TestCase):
    def test_toast_show_formats_and_queues(self):
        source = STUBS + function("source/src/gui.c", "void toast_show(u32 msg_id, ...)")
        source += SCENARIOS
        build_and_run(source, "toast_show")

    def test_toast_drawn_on_presented_frames_only(self):
        # synchronize() cannot be built on the host: the only toast_current()
        # call must sit in its presented-frame block, and a reset (every ROM
        # load) must drop the queue.
        body = function("source/src/main.c", "static void synchronize(void)")
        presented = body[body.index("  if (!skip_next_frame)\n  {"):]
        presented = presented[:presented.index("\n  }\n")]
        self.assertEqual(body.count("toast_current("), 1)
        self.assertIn("toast_current(sceKernelGetSystemTimeLow())", presented)
        self.assertIn("toast_clear();", function("source/src/main.c", "void reset_gba(void)"))


if __name__ == "__main__":
    unittest.main()
