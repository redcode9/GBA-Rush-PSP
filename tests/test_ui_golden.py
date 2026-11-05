"""Compare menu screens in every language and theme with golden hashes.

tests/test_ui_golden.c draws the screens with production code cut out of
gui.c, main.c, psp_video.c, ch_print.c and mh_print.c (the method of
tests/test_startup.py) through ui_batch and tests/ui_raster.c, and prints a
hash of each image. The hashes must match tests/ui_golden_hashes.txt.

UPDATE_UI_GOLDEN=1 rewrites that file and writes the images to
build/ui_golden/*.ppm; images never go in the repository. UI_GOLDEN_CFLAGS
adds compiler flags (make test-sanitize passes the sanitizer flags there).
"""

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
HASHES = ROOT / "tests/ui_golden_hashes.txt"
IMAGES = ROOT / "build/ui_golden"
SCREEN_WIDTH, SCREEN_HEIGHT = 480, 272
SCREENS, LANGUAGES, THEMES = 3, 5, 9
GUI = "source/src/gui.c"
HEADER = """\
# Golden hashes of menu screens: <screen> <language> <theme> <hash>.
# tests/test_ui_golden.py checks them. After an intended change to the look
# of these screens, run it with UPDATE_UI_GOLDEN=1 and look at the images it
# writes to build/ui_golden/ before committing the new hashes.
"""
TYPES = """\
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;
"""
GLYPH_FUNCTIONS = (
    "static u8 iskanji_hi(u8 c)",
    "static u8 iskanji_lo(u8 c)",
    "static s32 idx_hankaku(u8 ch)",
    "static s32 idx_zenkaku(u8 hi, u8 lo)",
    "static void draw_char(s32 chr, u16 x, u16 y, u16 col, s16 bg_col, u16 *base_vram, u16 bufferwidth)",
)
LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')


def read(source):
    return (ROOT / source).read_text()


def through(source, start, end):
    """The text of a repository file from start to the end of end, verbatim."""
    text = read(source)
    first = text.index(start)
    return text[first:text.index(end, first) + len(end)]


def defines(source, *names):
    text = read(source)
    return "".join(re.search(rf"^#define {name}\b.*\n", text, re.M).group()
                   for name in names)


def defines_header():
    return (defines("source/src/video.h", "PSP_SCREEN_WIDTH", "PSP_SCREEN_HEIGHT",
                    "PSP_LINE_SIZE", "COLOR15", "COLOR15_GREEN", "COLOR15_YELLOW",
                    "COL15_GET_R8", "COL15_GET_G8", "COL15_GET_B8", "COLOR32",
                    "COLOR15_TO_32", "X_POS_CENTER")
            + defines("source/src/common.h", "MAX_PATH", "ALIGN_DATA")
            + defines(GUI, "GPSP_THEME_FILENAME", "PREVIEW_PANEL_X", "PREVIEW_PANEL_Y",
                      "PREVIEW_PANEL_W", "PREVIEW_PANEL_H", "BATT_STATUS_POS_X",
                      "TIME_STATUS_POS_X"))


def drawing_code():
    """The popup, theme, preview, status and dialog code, callees first."""
    popup = function("source/src/psp_video.c",
                     "void draw_popup_frame_auto(int x, int y, int w, int h)")
    parts = [
        through("source/src/psp_video.c", "static void color15_to_rgb(", popup),
        through(GUI, "/* Runtime theme colors", "u32 color_picker_item_index = 0;"),
        through(GUI, "typedef struct {\n    u16 bg,", "\n};"),
    ]
    parts += [function(GUI, signature) for signature in (
        "static void apply_theme(u32 theme)",
        "static void copy_swap_aware(const char *src, char *buf, size_t size)",
        "void print_swap_aware(const char *src, u32 x, u32 y, u16 color, u16 bg)",
        "static void draw_theme_preview(u16 editing_color_ptr_value)",
        "static void get_timestamp_string(char *buffer, u16 msg_id, ScePspDateTime *msg_time, int day_of_week)",
        "static void update_status_string_gbk(char *time_str, char *batt_str, u16 *color_batt)",
        "static void draw_status_bar(void)",
    )]
    parts.append(function("source/src/main.c", "u32 yesno_dialog(const char *text)"))
    return "\n\n".join(parts) + "\n"


def font(source, header, data, entry):
    """ch_print.c or mh_print.c's drawing functions with their font."""
    code = TYPES + f'#include "ui_golden_defines.h"\n#include "{header}"\n#include "{data}"\n'
    signatures = GLYPH_FUNCTIONS + (
        f"void {entry}(const char *str, u16 x, u16 y, u16 col, s16 bg_col, u16 *base_vram, u16 bufferwidth)",)
    return code + "\n".join(function(source, signature) for signature in signatures) + "\n"


def gbk(table):
    """A message table's literals with each non-ASCII character as its GBK
    bytes, as source/Makefile's -fexec-charset=GBK compiles them. Octal
    escapes take at most three digits, so the next character never joins
    them."""
    def encode(literal):
        return re.sub(r"[^\x00-\x7f]", lambda c: "".join(
            f"\\{byte:03o}" for byte in c.group().encode("gbk")), literal.group())
    return LITERAL.sub(encode, table)


def messages():
    """message.c's table, built from the five message_*.h in GBK."""
    text = read("source/src/message.c")
    table = text[text.index("const char *message[5][MSG_END]"):]
    return TYPES + '#include "ch_print.h"\n#include "mh_print.h"\n#include "message.h"\n' + re.sub(
        r'#include "(text/message_\w+\.h)"\n',
        lambda m: gbk((ROOT / "source/src" / m.group(1)).read_text(encoding="utf-8")),
        table)


def render(pixels):
    """Builds and runs the screens; returns their program's output lines."""
    generated = {
        "ui_golden_defines.h": defines_header(),
        "ui_golden_extracted.c": drawing_code(),
        "ui_golden_messages.c": messages(),
        "ui_golden_gbk.c": font("source/src/ch_print.c", "ch_print.h", "gbkfont.c", "ch_print"),
        "ui_golden_sjis.c": font("source/src/mh_print.c", "mh_print.h", "shinonomefont.c", "mh_print"),
    }
    with tempfile.TemporaryDirectory(prefix="gbarush-ui-golden-") as directory:
        path = Path(directory)
        for name, code in generated.items():
            (path / name).write_text(code, encoding="utf-8")
        command = shlex.split(os.environ.get("HOST_CC", "cc"))
        flags = shlex.split(os.environ.get("UI_GOLDEN_CFLAGS", ""))
        if pixels:
            flags.append("-DUI_GOLDEN_PIXELS=1")
        sources = [ROOT / "tests/test_ui_golden.c", path / "ui_golden_messages.c",
                   path / "ui_golden_gbk.c", path / "ui_golden_sjis.c",
                   ROOT / "source/src/ui_batch.c", ROOT / "tests/ui_raster.c"]
        build = subprocess.run(command + ["-std=c99", "-Wall", "-Wextra", "-Werror", *flags,
                                          "-I", str(path), "-I", str(ROOT / "source/src"),
                                          "-I", str(ROOT / "tests"), *map(str, sources),
                                          "-o", str(path / "ui_golden")],
                               capture_output=True, text=True)
        if build.returncode != 0:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(path / "ui_golden")], capture_output=True, text=True)
        if run.returncode != 0:
            raise AssertionError(f"screens exited with {run.returncode}: {run.stderr}")
        return run.stdout.splitlines()


def write_golden(lines):
    """Rewrites the hash file and saves every image as a .ppm."""
    hashes = []
    IMAGES.mkdir(parents=True, exist_ok=True)
    rows = iter(lines)
    for line in rows:
        hashes.append(line + "\n")
        image = b"".join(bytes.fromhex(next(rows)) for _ in range(SCREEN_HEIGHT))
        (IMAGES / ("-".join(line.split()[:3]) + ".ppm")).write_bytes(
            b"P6 %d %d 255\n" % (SCREEN_WIDTH, SCREEN_HEIGHT) + image)
    HASHES.write_text(HEADER + "".join(hashes))


def parse(lines):
    hashes = {}
    for line in lines:
        if line and not line.startswith("#"):
            screen, language, theme, value = line.split()
            hashes[(screen, language, theme)] = value
    return hashes


class UiGoldenTests(unittest.TestCase):
    def test_screens_match_golden_hashes(self):
        if os.environ.get("UPDATE_UI_GOLDEN") == "1":
            write_golden(render(pixels=True))
        hashes = parse(render(pixels=False))
        self.assertEqual(len(hashes), SCREENS * LANGUAGES * THEMES)
        expected = parse(HASHES.read_text().splitlines())
        changed = [" ".join(key) for key in sorted(set(hashes) | set(expected))
                   if hashes.get(key) != expected.get(key)]
        self.assertFalse(changed, "screens that differ from tests/ui_golden_hashes.txt "
                         "(UPDATE_UI_GOLDEN=1 rewrites it): " + ", ".join(changed))


if __name__ == "__main__":
    unittest.main()
