"""Check that custom menu colours survive a reboot, with the real functions."""

import re
import unittest

# Extracts a C function from a source file and builds a spliced test.
from tests.test_startup import build_and_run, function


# Stub preset k, colour i is (k + 1) * 0x100 + i: every value differs from
# the custom colours 0x7000 + i.
PRESETS = ",\n".join(
    "  {" + ", ".join(hex((k + 1) * 0x100 + i) for i in range(11)) + "}"
    for k in range(9))

# Host stand-ins for the theme functions, load_setting_cfg() and
# set_default_directories(). tempgba_theme.cfg is one in-memory file
# (file_size < 0: absent); dir.ini and roms/ are absent.
STUBS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef int SceUID;
#define MAX_PATH 512
#define MAX_FILE (MAX_PATH + 8)
#define GPSP_THEME_FILENAME "tempgba_theme.cfg"
char main_path[64] = "ms0:/PSP/GAME/X/";
char dir_roms[MAX_PATH], dir_save[MAX_PATH], dir_state[MAX_PATH];
char dir_cfg[MAX_PATH], dir_snap[MAX_PATH], dir_cheat[MAX_PATH];
char dir_boxart[MAX_PATH], dir_boxart_folders[MAX_PATH];
u16 color_bg, color_rom_info, color_active_item, color_inactive_item;
u16 color_tooltip_text, color_help_text, color_inactive_dir, color_scroll_bar;
u16 color_batt_normal, color_batt_low, color_batt_charg;
typedef struct {
    u16 bg, rom_info, active_item, inactive_item;
    u16 tooltip_text, help_text, inactive_dir, scroll_bar;
    u16 batt_normal, batt_low, batt_charg;
} ThemeColors;
static const ThemeColors theme_presets[9] = {
''' + PRESETS + r'''
};
static unsigned char file_data[64];
static int file_size = -1, open_files, power_locks;
static void check_path(const char *p) {
  assert(strcmp(p, "ms0:/PSP/GAME/X/tempgba_theme.cfg") == 0);
}
static SceUID file_open(const char *p, const char *mode) {
  check_path(p);
  if(strcmp(mode, "WRITE") == 0) file_size = 0;
  else if(file_size < 0) return -1;
  open_files++; return 3;
}
static int file_read(SceUID fd, void *b, int n) {
  assert(fd == 3 && n >= 0);
  if(n > file_size) n = file_size;
  memcpy(b, file_data, n); return n;
}
static int file_write(SceUID fd, const void *b, int n) {
  assert(fd == 3 && n >= 0 && n <= (int)sizeof(file_data));
  memcpy(file_data, b, n); file_size = n; return n;
}
static int file_close(SceUID fd) { assert(fd == 3); open_files--; return 0; }
#define FILE_OPEN(fd, path, mode) fd = file_open(path, #mode)
#define FILE_CHECK_VALID(fd) ((fd) >= 0)
#define FILE_READ(fd, buffer, size) file_read(fd, buffer, size)
#define FILE_WRITE(fd, buffer, size) file_write(fd, buffer, size)
#define FILE_CLOSE(fd) file_close(fd)
int sceIoRemove(const char *p) { check_path(p); file_size = -1; return 0; }
void scePowerLock(int u) { (void)u; power_locks++; }
void scePowerUnlock(int u) { (void)u; power_locks--; }
int load_dir_cfg(char *name) { (void)name; return -1; }
SceUID sceIoDopen(const char *p) { (void)p; return -1; }
int sceIoDclose(SceUID d) { (void)d; return 0; }
'''

# boot() follows setup_main(): load_config_file() applies the preset from
# tempgba.cfg, load_theme_config() the custom colours over it, then
# load_setting_cfg() runs the dir.ini-absent path. The custom colours must
# survive it with every preset; only picking a preset in the menu
# (menu_passive_theme, menu_theme_default) deletes them.
SCENARIOS = r'''
static u16 *const colors[11] = {&color_bg, &color_rom_info,
 &color_active_item, &color_inactive_item, &color_tooltip_text,
 &color_help_text, &color_inactive_dir, &color_scroll_bar,
 &color_batt_normal, &color_batt_low, &color_batt_charg};
#define PRESET(k) (((k) + 1) * 0x100)
#define CUSTOM 0x7000
static void set_colors(u32 base) {
  u32 i;
  for(i = 0; i < 11; i++) *colors[i] = base + i;
}
static void check_colors(u32 base) {
  u32 i;
  for(i = 0; i < 11; i++) assert(*colors[i] == base + i);
}
static void boot(u32 theme) {
  apply_theme(theme);
  load_theme_config();
  load_setting_cfg();
  assert(strcmp(dir_roms, main_path) == 0);
  assert(open_files == 0 && power_locks == 0);
}
int main(void) {
  u32 k;
  boot(3); check_colors(PRESET(3));
  set_colors(CUSTOM); assert(save_theme_config() == 0 && file_size == 22);
  for(k = 0; k < 9; k++) {
    boot(k); check_colors(CUSTOM); assert(file_size == 22);
  }
  select_theme_preset(5); check_colors(PRESET(5)); assert(file_size < 0);
  boot(5); check_colors(PRESET(5));
  set_colors(CUSTOM); assert(save_theme_config() == 0);
  select_theme_preset(0); check_colors(PRESET(0)); assert(file_size < 0);
  assert(open_files == 0 && power_locks == 0);
  puts("theme boot regressions passed");
  return 0;
}
'''


class ThemeBootTests(unittest.TestCase):
    def test_custom_colours_survive_reboot(self):
        source = STUBS
        for signature in ("static void apply_theme(u32 theme)",
                          "static void select_theme_preset(u32 theme)",
                          "s32 save_theme_config(void)",
                          "s32 load_theme_config(void)",
                          "void set_default_directories(void)"):
            source += function("source/src/gui.c", signature)
        source += function("source/src/main.c", "static void load_setting_cfg(void)")
        source += SCENARIOS
        build_and_run(source, "theme_boot")

    def test_boot_keeps_the_custom_colours(self):
        # Boot calls apply_theme() before load_theme_config(), and the real
        # load_dir_cfg() (stubbed above) after it.
        self.assertNotIn("sceIoRemove",
                         function("source/src/gui.c", "static void apply_theme(u32 theme)"))
        self.assertNotIn("apply_theme",
                         function("source/src/gui.c", "s32 load_dir_cfg(char *file_name)"))
        # The preset rows' callbacks are nested in menu() with an indented
        # brace, so function() cannot cut them out; "\s*\{" skips their
        # forward declarations.
        menu = function("source/src/gui.c", "u32 menu(void)")
        for name in ("menu_passive_theme", "menu_theme_default"):
            body = re.search(r"void %s\(void\)\s*\{([^}]*)\}" % name, menu)
            self.assertIsNotNone(body, name)
            self.assertIn("select_theme_preset(option_theme);", body.group(1))
            self.assertNotIn("apply_theme", body.group(1))
        self.assertIn("STRING_SELECTION_OPTION(menu_passive_theme,", menu)
        self.assertIn("ACTION_OPTION(menu_theme_default,", menu)


if __name__ == "__main__":
    unittest.main()
