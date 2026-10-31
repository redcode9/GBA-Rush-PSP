/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2007 takka <takka@tfact.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/* PSP user interface, called from main.c and input.c. In file order:
 * themes and the colour picker, recent ROMs, text helpers, the menu option
 * macros, the file browser (load_file, list and carousel views), savestate
 * slots, cheat and diagnostics callbacks, the in-game menu (menu(), whose
 * screens are nested functions), the status bar, and loading and saving of
 * the global, per-game and directory settings files. */

#include "common.h"
#include "boxart.h"   // carousel / boxart support
#include "config_store.h"
#include "menu_layout.h"
#include "menu_model.h"
#include "psp_adhoc.h"

extern u32 option_swap_confirm_buttons;

/* Both live in main_path. The theme file holds the 11 custom colours as raw
   u16 values in color_items[] order; the config file is the versioned
   ConfigModel blob (config_model.c), saved through config_store.c. */
#define GPSP_THEME_FILENAME             "tempgba_theme.cfg"
#define GPSP_CONFIG_FILENAME            "tempgba.cfg"

/* ------------------------------------------------------------------
   Themes
   ------------------------------------------------------------------ */

/* Runtime theme colors — initialized to OG theme defaults */
u16 color_bg            = COLOR15( 3,  5,  8);
u16 color_rom_info      = COLOR15(22, 18, 26);
u16 color_active_item   = COLOR15(31, 31, 31);
u16 color_inactive_item = COLOR15(13, 20, 18);
u16 color_tooltip_text  = COLOR15(18, 18, 18);
u16 color_help_text     = COLOR15(16, 20, 24);
u16 color_inactive_dir  = COLOR15(13, 22, 22);
u16 color_scroll_bar    = COLOR15( 7,  9, 11);
u16 color_batt_normal   = COLOR15(16, 20, 24);
u16 color_batt_low      = COLOR15_YELLOW;
u16 color_batt_charg    = COLOR15_GREEN;

/* Global index for color picker — set before entering picker, read by file-scope function */
u32 color_picker_item_index = 0;

typedef struct {
    u16 bg, rom_info, active_item, inactive_item;
    u16 tooltip_text, help_text, inactive_dir, scroll_bar;
    u16 batt_normal, batt_low, batt_charg;
} ThemeColors;

static const ThemeColors theme_presets[9] = {
    /* 0 OG   */     { COLOR15(3,5,8), COLOR15(22,18,26), COLOR15(31,31,31), COLOR15(13,20,18), COLOR15(18,18,18), COLOR15(16,20,24), COLOR15(13,22,22), COLOR15(7,9,11), COLOR15(16, 20, 24), COLOR15_YELLOW, COLOR15_GREEN },
    /* 1 Dark */     { COLOR15(0,0,0), COLOR15(22,18,26), COLOR15(31,31,31), COLOR15(13,20,18), COLOR15(18,18,18), COLOR15(16,20,24), COLOR15(13,22,22), COLOR15(7,9,11), COLOR15(16,20,24), COLOR15_YELLOW, COLOR15_GREEN },
    /* 2 Light */    { COLOR15(31,31,31), COLOR15(10,8,12), COLOR15(0,0,0), COLOR15(18,18,18), COLOR15(12,12,12), COLOR15(8,12,16), COLOR15(16,20,20), COLOR15(20,22,24), COLOR15(8,12,16), COLOR15(31,0,0), COLOR15(0,24,0) },
    /* 3 Blue */     { COLOR15(0,0,8), COLOR15(20,18,26), COLOR15(31,31,31), COLOR15(10,15,22), COLOR15(12,16,22), COLOR15(12,18,26), COLOR15(10,18,24), COLOR15(6,10,14), COLOR15(12,18,26), COLOR15(31,31,0), COLOR15(0,31,0) },
    /* 4 Green */    { COLOR15(0,6,0), COLOR15(18,26,18), COLOR15(31,31,31), COLOR15(10,20,10), COLOR15(12,22,12), COLOR15(12,24,12), COLOR15(10,22,10), COLOR15(6,12,6), COLOR15(12,24,12), COLOR15(31,31,0), COLOR15(0,31,0) },
    /* 5 Red */      { COLOR15(8,0,0), COLOR15(26,18,18), COLOR15(31,31,31), COLOR15(20,10,10), COLOR15(24,12,12), COLOR15(24,14,14), COLOR15(22,10,10), COLOR15(14,6,6), COLOR15(24,14,14), COLOR15(31,31,0), COLOR15(0,31,0) },
    /* 6 Purple */   { COLOR15(6,0,8), COLOR15(22,18,26), COLOR15(31,31,31), COLOR15(18,12,20), COLOR15(20,14,24), COLOR15(20,14,24), COLOR15(18,12,20), COLOR15(10,6,12), COLOR15(20,14,24), COLOR15(31,31,0), COLOR15(0,31,0) },
    /* 7 Hi-Con */   { COLOR15(0,0,0), COLOR15(31,31,31), COLOR15(31,31,0), COLOR15(31,31,31), COLOR15(0,31,31), COLOR15(0,31,31), COLOR15(31,31,31), COLOR15(31,31,31), COLOR15(0,31,31), COLOR15(31,0,0), COLOR15(0,31,0) },
    /* 8 Retro */    { COLOR15(0,0,0), COLOR15(0,22,0), COLOR15(0,31,0), COLOR15(0,18,0), COLOR15(0,20,0), COLOR15(0,24,0), COLOR15(0,18,0), COLOR15(0,10,0), COLOR15(0,24,0), COLOR15(31,31,0), COLOR15(0,31,0) },
};

/* item_index follows the color_items[] / ThemeColors field order. */
static u16 get_preset_color(u32 theme, u32 item_index)
{
    if (theme >= 9) theme = 0;
    switch (item_index)
    {
        case 0:  return theme_presets[theme].bg;
        case 1:  return theme_presets[theme].rom_info;
        case 2:  return theme_presets[theme].active_item;
        case 3:  return theme_presets[theme].inactive_item;
        case 4:  return theme_presets[theme].tooltip_text;
        case 5:  return theme_presets[theme].help_text;
        case 6:  return theme_presets[theme].inactive_dir;
        case 7:  return theme_presets[theme].scroll_bar;
        case 8:  return theme_presets[theme].batt_normal;
        case 9:  return theme_presets[theme].batt_low;
        case 10: return theme_presets[theme].batt_charg;
    }
    return 0;
}

static void apply_theme(u32 theme)
{
    if (theme >= 9) theme = 0;

    color_bg            = theme_presets[theme].bg;
    color_rom_info      = theme_presets[theme].rom_info;
    color_active_item   = theme_presets[theme].active_item;
    color_inactive_item = theme_presets[theme].inactive_item;
    color_tooltip_text  = theme_presets[theme].tooltip_text;
    color_help_text     = theme_presets[theme].help_text;
    color_inactive_dir  = theme_presets[theme].inactive_dir;
    color_scroll_bar    = theme_presets[theme].scroll_bar;
    color_batt_normal   = theme_presets[theme].batt_normal;
    color_batt_low      = theme_presets[theme].batt_low;
    color_batt_charg    = theme_presets[theme].batt_charg;

    /* When a preset is chosen, delete custom theme so preset isn't overridden on boot */
    {
        char path[MAX_PATH];
        sprintf(path, "%s%s", main_path, GPSP_THEME_FILENAME);
        sceIoRemove(path);
    }
}


/* ------------------------------------------------------------------
   Custom Color Picker
   ------------------------------------------------------------------ */

typedef struct {
    u32 msg_index;
    u16 *color_ptr;
} ColorItem;

static const ColorItem color_items[] = {
    { MSG_CUSTOM_COLOR_BG,            &color_bg },
    { MSG_CUSTOM_COLOR_ROM_INFO,      &color_rom_info },
    { MSG_CUSTOM_COLOR_ACTIVE_ITEM,   &color_active_item },
    { MSG_CUSTOM_COLOR_INACTIVE_ITEM, &color_inactive_item },
    { MSG_CUSTOM_COLOR_TOOLTIP_TEXT,  &color_tooltip_text },
    { MSG_CUSTOM_COLOR_HELP_TEXT,     &color_help_text },
    { MSG_CUSTOM_COLOR_INACTIVE_DIR,  &color_inactive_dir },
    { MSG_CUSTOM_COLOR_SCROLL_BAR,    &color_scroll_bar },
    { MSG_CUSTOM_COLOR_BATT_NORMAL,   &color_batt_normal },
    { MSG_CUSTOM_COLOR_BATT_LOW,      &color_batt_low },
    { MSG_CUSTOM_COLOR_BATT_CHARG,    &color_batt_charg },
};

#define NUM_COLOR_ITEMS (sizeof(color_items) / sizeof(color_items[0]))

#define PICKER_SV_X      10
#define PICKER_SV_Y      40
#define PICKER_SV_GRID   16
#define PICKER_SV_CELL   8
#define PICKER_SV_SIZE   (PICKER_SV_GRID * PICKER_SV_CELL)

#define PICKER_HUE_X     (PICKER_SV_X + PICKER_SV_SIZE + 10)
#define PICKER_HUE_Y     PICKER_SV_Y
#define PICKER_HUE_W     16
#define PICKER_HUE_SEGS  32
#define PICKER_HUE_CELL  4
#define PICKER_HUE_H     (PICKER_HUE_SEGS * PICKER_HUE_CELL)

#define PICKER_PREV_X    (PICKER_HUE_X + PICKER_HUE_W + 10)
#define PICKER_PREV_Y    PICKER_SV_Y
#define PICKER_PREV_SIZE 36

/* Theme preview panel — a miniature menu mockup drawn in the same place and
   size as the game screenshot window (228,44, 240x160); literals are used
   because the SCREEN_IMAGE_POS macros are defined later in this file. */
#define PREVIEW_PANEL_X  228
#define PREVIEW_PANEL_Y  44
#define PREVIEW_PANEL_W  240
#define PREVIEW_PANEL_H  160


/* ------------------------------------------------------------------
   Global choice arrays (mutable pointers so they refresh with language)
   ------------------------------------------------------------------ */
static const char *scale_options[SCREEN_SCALE_COUNT];
static const char *frameskip_options[3];
static const char *stack_optimize_options[2];
static const char *update_backup_options[2];
static const char *yes_no_options[2];
static const char *on_off_options[2];
static const char *enable_disable_options[2];
static const char *clock_speed_options[4];
static const char *sound_volume_options[11];
static const char *image_format_options[2];
static const char *video_renderer_options[2];
static const char *ram_dynarec_options[3];
static const char *swap_button_options[2];
static const char *theme_preset_options[9];
static const char *language_option[5];
static const char *gamepad_config_buttons[20];
static const char *fast_forward_profile_options[FAST_FORWARD_PROFILE_COUNT];
static const char *fast_forward_override_options[2];
static const char *fast_forward_activation_options[
 FAST_FORWARD_ACTIVATION_COUNT];
static const char *fast_forward_speed_options[FAST_FORWARD_SPEED_COUNT];
static const char *fast_forward_presentation_options[
 FAST_FORWARD_PRESENTATION_COUNT];
static const char *fast_forward_audio_options[FAST_FORWARD_AUDIO_COUNT];

static void init_choice_arrays(void)
{
    scale_options[0] = MSG[MSG_SCN_SCALED_NONE];
    scale_options[1] = MSG[MSG_SCN_SCALED_X15_GU];
    scale_options[2] = MSG[MSG_SCN_SCALED_X15_SW];
    scale_options[3] = MSG[MSG_SCN_SCALED_USER];
    scale_options[4] = MSG[MSG_SCN_SCALED_16X9_GU];
    scale_options[5] = MSG[MSG_SCN_SCALED_16X9_SHARP];
    scale_options[6] = MSG[MSG_SCN_SCALED_16X9_AUTO];

    frameskip_options[0] = MSG[MSG_AUTO];
    frameskip_options[1] = MSG[MSG_MANUAL];
    frameskip_options[2] = MSG[MSG_OFF];

    stack_optimize_options[0] = MSG[MSG_OFF];
    stack_optimize_options[1] = MSG[MSG_AUTO];

    update_backup_options[0] = MSG[MSG_EXITONLY];
    update_backup_options[1] = MSG[MSG_AUTO];

    yes_no_options[0] = MSG[MSG_NO];
    yes_no_options[1] = MSG[MSG_YES];

    on_off_options[0] = MSG[MSG_OFF];
    on_off_options[1] = MSG[MSG_ON];

    enable_disable_options[0] = MSG[MSG_DISABLED];
    enable_disable_options[1] = MSG[MSG_ENABLED];

    clock_speed_options[0] = MSG[MSG_CLOCK_222];
    clock_speed_options[1] = MSG[MSG_CLOCK_266];
    clock_speed_options[2] = MSG[MSG_CLOCK_300];
    clock_speed_options[3] = MSG[MSG_CLOCK_333];

    sound_volume_options[0]  = MSG[MSG_VOL_0];
    sound_volume_options[1]  = MSG[MSG_VOL_10];
    sound_volume_options[2]  = MSG[MSG_VOL_20];
    sound_volume_options[3]  = MSG[MSG_VOL_30];
    sound_volume_options[4]  = MSG[MSG_VOL_40];
    sound_volume_options[5]  = MSG[MSG_VOL_50];
    sound_volume_options[6]  = MSG[MSG_VOL_60];
    sound_volume_options[7]  = MSG[MSG_VOL_70];
    sound_volume_options[8]  = MSG[MSG_VOL_80];
    sound_volume_options[9]  = MSG[MSG_VOL_90];
    sound_volume_options[10] = MSG[MSG_VOL_100];

    fast_forward_profile_options[FAST_FORWARD_PROFILE_MODERN] =
     MSG[MSG_FF_PROFILE_MODERN];
    fast_forward_profile_options[FAST_FORWARD_PROFILE_CLASSIC] =
     MSG[MSG_FF_PROFILE_CLASSIC];
    fast_forward_profile_options[FAST_FORWARD_PROFILE_CUSTOM] =
     MSG[MSG_FF_PROFILE_CUSTOM];
    fast_forward_override_options[0] = MSG[MSG_FF_USE_GLOBAL];
    fast_forward_override_options[1] = MSG[MSG_FF_USE_OVERRIDE];
    fast_forward_activation_options[FAST_FORWARD_ACTIVATION_HOLD] =
     MSG[MSG_FF_HOLD];
    fast_forward_activation_options[FAST_FORWARD_ACTIVATION_TOGGLE] =
     MSG[MSG_FF_TOGGLE];
    fast_forward_speed_options[FAST_FORWARD_SPEED_2X] = MSG[MSG_FF_SPEED_2X];
    fast_forward_speed_options[FAST_FORWARD_SPEED_3X] = MSG[MSG_FF_SPEED_3X];
    fast_forward_speed_options[FAST_FORWARD_SPEED_MAXIMUM] =
     MSG[MSG_FF_SPEED_MAXIMUM];
    fast_forward_presentation_options[FAST_FORWARD_PRESENTATION_SMOOTH] =
     MSG[MSG_FF_PRESENT_SMOOTH];
    fast_forward_presentation_options[FAST_FORWARD_PRESENTATION_BALANCED] =
     MSG[MSG_FF_PRESENT_BALANCED];
    fast_forward_presentation_options[FAST_FORWARD_PRESENTATION_PERFORMANCE] =
     MSG[MSG_FF_PRESENT_PERFORMANCE];
    fast_forward_audio_options[FAST_FORWARD_AUDIO_SILENT] =
     MSG[MSG_FF_AUDIO_SILENT];
    fast_forward_audio_options[FAST_FORWARD_AUDIO_ACCELERATED] =
     MSG[MSG_FF_AUDIO_ACCELERATED];

    image_format_options[0] = MSG[MSG_FMT_PNG];
    image_format_options[1] = MSG[MSG_FMT_BMP];

    video_renderer_options[0] = MSG[MSG_RENDERER_OLD];
    video_renderer_options[1] = MSG[MSG_RENDERER_NEW];

    ram_dynarec_options[0] = MSG[MSG_RAM_DYNAREC_FULL_FLUSH];
    ram_dynarec_options[1] = MSG[MSG_RAM_DYNAREC_PARTIAL_NO_REUSE];
    ram_dynarec_options[2] = MSG[MSG_RAM_DYNAREC_PARTIAL_WITH_REUSE];

    swap_button_options[0] = MSG[MSG_SWAP_BUTTONS_O_CONFIRMS];
    swap_button_options[1] = MSG[MSG_SWAP_BUTTONS_X_CONFIRMS];

    theme_preset_options[0] = MSG[MSG_THEMES_OG];
    theme_preset_options[1] = MSG[MSG_THEMES_DARK];
    theme_preset_options[2] = MSG[MSG_THEMES_LIGHT];
    theme_preset_options[3] = MSG[MSG_THEMES_BLUE];
    theme_preset_options[4] = MSG[MSG_THEMES_GREEN];
    theme_preset_options[5] = MSG[MSG_THEMES_RED];
    theme_preset_options[6] = MSG[MSG_THEMES_PURPLE];
    theme_preset_options[7] = MSG[MSG_THEMES_HIGH_CONTRAST];
    theme_preset_options[8] = MSG[MSG_THEMES_RETRO];

    language_option[0] = MSG[MSG_LANG_JAPANESE];
    language_option[1] = MSG[MSG_LANG_ENGLISH];
    language_option[2] = MSG[MSG_LANG_CHS];
    language_option[3] = MSG[MSG_LANG_CHT];
    language_option[4] = MSG[MSG_LANG_ITA];

    gamepad_config_buttons[0]  = MSG[MSG_PAD_MENU_CFG_0];
    gamepad_config_buttons[1]  = MSG[MSG_PAD_MENU_CFG_1];
    gamepad_config_buttons[2]  = MSG[MSG_PAD_MENU_CFG_2];
    gamepad_config_buttons[3]  = MSG[MSG_PAD_MENU_CFG_3];
    gamepad_config_buttons[4]  = MSG[MSG_PAD_MENU_CFG_4];
    gamepad_config_buttons[5]  = MSG[MSG_PAD_MENU_CFG_5];
    gamepad_config_buttons[6]  = MSG[MSG_PAD_MENU_CFG_6];
    gamepad_config_buttons[7]  = MSG[MSG_PAD_MENU_CFG_7];
    gamepad_config_buttons[8]  = MSG[MSG_PAD_MENU_CFG_8];
    gamepad_config_buttons[9]  = MSG[MSG_PAD_MENU_CFG_9];
    gamepad_config_buttons[10] = MSG[MSG_PAD_MENU_CFG_10];
    gamepad_config_buttons[11] = MSG[MSG_PAD_MENU_CFG_11];
    gamepad_config_buttons[12] = MSG[MSG_PAD_MENU_CFG_12];
    gamepad_config_buttons[13] = MSG[MSG_PAD_MENU_CFG_13];
    gamepad_config_buttons[14] = MSG[MSG_PAD_MENU_CFG_14];
    gamepad_config_buttons[15] = MSG[MSG_PAD_MENU_CFG_15];
    gamepad_config_buttons[16] = MSG[MSG_PAD_MENU_CFG_16];
    gamepad_config_buttons[17] = MSG[MSG_PAD_MENU_CFG_17];
    gamepad_config_buttons[18] = MSG[MSG_PAD_MENU_CFG_18];
    gamepad_config_buttons[19] = MSG[MSG_PAD_MENU_CFG_19];
}

static void refresh_choice_arrays(void)
{
    init_choice_arrays();
}

/* Colours are PSP 15-bit 5551: red in bits 0-4, green 5-9, blue 10-14.
   The HSV helpers below work on 8-bit channels, H in degrees 0-359. */
static void color15_to_rgb(u16 c, u8 *r, u8 *g, u8 *b)
{
    *r = (c >> 0) & 0x1F;
    *g = (c >> 5) & 0x1F;
    *b = (c >> 10) & 0x1F;
}

static u16 rgb_to_color15(u8 r, u8 g, u8 b)
{
    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10);
}

#define C5TO8(v) (((v) << 3) | ((v) >> 2))
#define C8TO5(v) ((v) >> 3)

static void rgb15_to_hsv(u16 c, u32 *h, u32 *s, u32 *v)
{
    u8 r5, g5, b5;
    color15_to_rgb(c, &r5, &g5, &b5);

    u8 r = C5TO8(r5);
    u8 g = C5TO8(g5);
    u8 b = C5TO8(b5);

    u8 max = r, min = r;
    if (g > max) max = g;
    if (b > max) max = b;
    if (g < min) min = g;
    if (b < min) min = b;

    *v = max;

    u8 delta = max - min;
    if (max == 0) {
        *s = 0;
        *h = 0;
        return;
    }
    *s = (delta * 255) / max;

    if (delta == 0) {
        *h = 0;
    } else if (max == r) {
        *h = (60 * ((int)g - (int)b) / delta + 360) % 360;
    } else if (max == g) {
        *h = (60 * ((int)b - (int)r) / delta + 120) % 360;
    } else {
        *h = (60 * ((int)r - (int)g) / delta + 240) % 360;
    }
}

static u16 hsv_to_color15(u32 h, u32 s, u32 v)
{
    u8 r, g, b;
    h = h % 360;

    if (s == 0) {
        r = g = b = v;
    } else {
        u32 hh = h / 60;
        u32 f  = ((h % 60) * 255) / 60;
        u32 p  = (v * (255 - s)) / 255;
        u32 q  = (v * (255 - (s * f) / 255)) / 255;
        u32 t  = (v * (255 - (s * (255 - f)) / 255)) / 255;

        switch (hh) {
            case 0:  r = v; g = t; b = p; break;
            case 1:  r = q; g = v; b = p; break;
            case 2:  r = p; g = v; b = t; break;
            case 3:  r = p; g = q; b = v; break;
            case 4:  r = t; g = p; b = v; break;
            default: r = v; g = p; b = q; break;
        }
    }
    return rgb_to_color15(C8TO5(r), C8TO5(g), C8TO5(b));
}

/* Dim a 15-bit colour by a brightness percentage (50, 75, or 100).
 * Used to apply depth shading to folder frames and placeholders. */
static u16 dim_color15(u16 color, u8 brightness)
{
    if (brightness == 100)
        return color;

    u16 r = (color >> 0) & 0x1F;
    u16 g = (color >> 5) & 0x1F;
    u16 b = (color >> 10) & 0x1F;

    if (brightness == 50)
    {
        r >>= 1; g >>= 1; b >>= 1;
    }
    else if (brightness == 75)
    {
        r = (r >> 1) + (r >> 2);
        g = (g >> 1) + (g >> 2);
        b = (b >> 1) + (b >> 2);
    }

    return (r & 0x1F) | ((g & 0x1F) << 5) | ((b & 0x1F) << 10);
}

/* ------------------------------------------------------------------
   Recent ROMs management
   ------------------------------------------------------------------ */

/* recent.cfg in main_path: u32 count, then for each entry a u32 length and
   the absolute path bytes without a terminator. Newest entry first. */
#define MAX_RECENT_ROMS     5
#define RECENT_CFG_FILENAME "recent.cfg"

typedef struct {
    char path[MAX_PATH];
} RecentRomEntry;

static RecentRomEntry recent_roms[MAX_RECENT_ROMS];
static u32 num_recent_roms = 0;

/* Forward declarations */
s32 load_file(const char **wildcards, char *result, char *default_dir_name, u32 show_recent);
void load_recent_roms(void);
static void save_recent_roms(void);
void add_recent_rom(const char *filename);
static void validate_recent_roms(void);
static u32 is_recent_roms_empty(void);

void load_recent_roms(void)
{
    SceUID fd;
    char path[MAX_PATH];
    u32 i;

    num_recent_roms = 0;
    memset(recent_roms, 0, sizeof(recent_roms));

    sprintf(path, "%s%s", main_path, RECENT_CFG_FILENAME);

    FILE_OPEN(fd, path, READ);
    if (FILE_CHECK_VALID(fd))
    {
        u32 count = 0;
        if (FILE_READ(fd, &count, sizeof(count)) == sizeof(count))
        {
            if (count > MAX_RECENT_ROMS)
                count = MAX_RECENT_ROMS;

            for (i = 0; i < count; i++)
            {
                u32 len = 0;
                if (FILE_READ(fd, &len, sizeof(len)) != sizeof(len))
                    break;
                if (len >= MAX_PATH)
                    break;
                if (FILE_READ(fd, recent_roms[i].path, len) != (s32)len)
                    break;
                recent_roms[i].path[len] = '\0';
                num_recent_roms++;
            }
        }
        FILE_CLOSE(fd);
    }

    /* Validate: remove entries pointing to deleted ROMs */
    validate_recent_roms();
}

static void save_recent_roms(void)
{
    SceUID fd;
    char path[MAX_PATH];
    u32 i;

    sprintf(path, "%s%s", main_path, RECENT_CFG_FILENAME);

    scePowerLock(0);
    FILE_OPEN(fd, path, WRITE);
    if (FILE_CHECK_VALID(fd))
    {
        FILE_WRITE(fd, &num_recent_roms, sizeof(num_recent_roms));
        for (i = 0; i < num_recent_roms; i++)
        {
            u32 len = strlen(recent_roms[i].path);
            FILE_WRITE(fd, &len, sizeof(len));
            FILE_WRITE(fd, recent_roms[i].path, len);
        }
        FILE_CLOSE(fd);
    }
    scePowerUnlock(0);
}

static void validate_recent_roms(void)
{
    u32 i, j;
    SceIoStat stat;

    for (i = 0; i < num_recent_roms; )
    {
        /* Check if file still exists */
        if (sceIoGetstat(recent_roms[i].path, &stat) < 0 || !FIO_S_ISREG(stat.st_mode))
        {
            /* Remove this entry by shifting remaining entries down */
            for (j = i; j < num_recent_roms - 1; j++)
            {
                strcpy(recent_roms[j].path, recent_roms[j + 1].path);
            }
            num_recent_roms--;
            /* Don't increment i, check the new entry at this position */
        }
        else
        {
            i++;
        }
    }
}

static u32 is_recent_roms_empty(void)
{
    return (num_recent_roms == 0);
}

void add_recent_rom(const char *filename)
{
    u32 i;
    char full_path[MAX_PATH];
    char current_dir[MAX_PATH];

    /* Build full path */
    getcwd(current_dir, MAX_PATH);
    if (filename[0] == '/')
        snprintf(full_path, sizeof(full_path), "%s", filename);
    else
        snprintf(full_path, sizeof(full_path), "%s/%s", current_dir, filename);

    /* Check if already in list — if so, remove old entry */
    for (i = 0; i < num_recent_roms; i++)
    {
        if (strcasecmp(recent_roms[i].path, full_path) == 0)
        {
            /* Shift entries down to remove this one */
            for (; i < num_recent_roms - 1; i++)
            {
                strcpy(recent_roms[i].path, recent_roms[i + 1].path);
            }
            num_recent_roms--;
            break;
        }
    }

    /* Shift all entries down by one to make room at top */
    for (i = (num_recent_roms < MAX_RECENT_ROMS ? num_recent_roms : MAX_RECENT_ROMS - 1); i > 0; i--)
    {
        strcpy(recent_roms[i].path, recent_roms[i - 1].path);
    }

    /* Add new entry at top */
    strcpy(recent_roms[0].path, full_path);
    if (num_recent_roms < MAX_RECENT_ROMS)
        num_recent_roms++;

    save_recent_roms();
}


/* Forward declarations for status bar functions */
static void update_status_string_gbk(char *time_str, char *batt_str, u16 *color_batt);
static void draw_status_bar(void);

/* Forward declaration for color reset */
void menu_reset_single_color(u32 idx);

/* Forward declarations for theme and color picker */
void menu_pick_color(void);

/* Forward declarations for savestate slot helpers */
static u32 action_savestate_slot(u32 slot);
static u32 action_loadstate_slot(u32 slot);

/* Menu screen layout in pixels on the 480x272 PSP screen. Row 2 holds the
   title and status bar, the option list runs down the left side, the game
   screenshot sits at the right, the three guide lines start at
   TEXT_TOOLTIP_POS_Y and the button help line is drawn at y = 258. */
#define TEXT_TOOLTIP_POS_Y  (210)
#define MENU_LIST_POS_X     (10) //18 default
#define MENU_LIST_POS_Y     (28)
#define MENU_LIST_LAST_Y    (196)
#define MENU_LIST_LINES     (((MENU_LIST_LAST_Y - MENU_LIST_POS_Y) / FONTHEIGHT) + 1)

#define SCREEN_IMAGE_POS_X  (228)
#define SCREEN_IMAGE_POS_Y  (44)

#define BATT_STATUS_POS_X   (PSP_SCREEN_WIDTH - (FONTWIDTH * 14))  // 396
#define TIME_STATUS_POS_X   (BATT_STATUS_POS_X - (FONTWIDTH * 22)) // 264
#define DIR_NAME_LENGTH     ((TIME_STATUS_POS_X / FONTWIDTH) - 2)  // 42


/* Draw the theme mockup with the current colours. editing_color_ptr_value
   is 0xFFFF outside the colour picker; inside it, the battery sample shows
   the low or charging state when that colour is the one being edited. */
static void draw_theme_preview(u16 editing_color_ptr_value)
{
    u32 px = PREVIEW_PANEL_X;
    u32 py = PREVIEW_PANEL_Y;
    u32 pw = PREVIEW_PANEL_W;   /* 240 */
    u32 ph = PREVIEW_PANEL_H;   /* 160 */

    /* Background fills the whole preview area */
    draw_box_fill(px, py, px + pw - 1, py + ph - 1, color_bg);
    draw_box_line(px - 1, py - 1, px + pw, py + ph, color_inactive_item);

    /* Title bar at top */
    print_string_gbk(MSG[MSG_PREVIEW_TITLE], px + 6, py + 4, color_rom_info, BG_NO_FILL);

    /* Menu items — simulate a list with one active, rest inactive */
    /* Scaled for 240x160: 5 items, ~20px each */
    u32 item_y = py + 20;
    u32 item_h = 18;
    const char *fake_items[] = {
        MSG[MSG_PREVIEW_ITEM_0],
        MSG[MSG_PREVIEW_ITEM_1],
        MSG[MSG_PREVIEW_ITEM_2],
        MSG[MSG_PREVIEW_ITEM_3],
        MSG[MSG_PREVIEW_ITEM_4]
    };
    u32 num_fake = 5;
    u32 active_idx = 2;  /* "Settings" is highlighted */

    for (u32 i = 0; i < num_fake; i++)
    {
        u16 item_color = (i == active_idx) ? color_active_item : color_inactive_item;
        print_string_gbk(fake_items[i], px + 10, item_y + i * item_h, item_color, BG_NO_FILL);
    }

    /* Scroll bar on the left edge */
    draw_box_line(px + 2, py + 18, px + 6, py + ph - 18, color_scroll_bar);
    draw_box_fill(px + 4, py + 20, px + 4, py + ph - 70, color_scroll_bar);

    /* Battery indicator in top-right corner — reflects the state being edited */
    {
        u16 batt_color = color_batt_normal;
        const char *batt_text = FONT_BATTERY2_GBK " 69%";

        if (editing_color_ptr_value != 0xFFFF)
        {
            if (color_picker_item_index == 9) {          /* Battery low */
                batt_color = color_batt_low;
                batt_text = FONT_BATTERY0_GBK " 12%";
            } else if (color_picker_item_index == 10) {  /* Battery charging */
                batt_color = color_batt_charg;
                batt_text = FONT_BATTERY3_GBK " 96%";
            } else {
                batt_color = color_batt_normal;
            }
        }

        print_string_gbk(batt_text, px + pw - 50, py + 4, batt_color, BG_NO_FILL);
    }

    /* Directory entries on the right side under battery */
    u32 dir_x = px + pw - 70;
    u32 dir_y = py + 20;
    if (option_language == LANGUAGE_JAPANESE)
    {
        print_string("..", dir_x, dir_y, color_inactive_dir, BG_NO_FILL);
        print_string("ROMs", dir_x, dir_y + 14, color_inactive_dir, BG_NO_FILL);
    }
    else
    {
        print_string_gbk("..", dir_x, dir_y, color_inactive_dir, BG_NO_FILL);
        print_string_gbk("ROMs", dir_x, dir_y + 14, color_inactive_dir, BG_NO_FILL);
    }

    /* Tooltip */
    u32 tooltip_y = py + ph - 40;
    print_string_gbk(MSG[MSG_PREVIEW_ITEM_TOOLTIP], px + 10, tooltip_y, color_tooltip_text, BG_NO_FILL);

    /* Help bar at bottom — button symbols embedded in MSG, print_swap_aware swaps them */
    print_swap_aware(MSG[MSG_PREVIEW_ITEM_HELP], px + 15, py + ph - 16, color_help_text, BG_NO_FILL);
}


/* Copy text while applying the configured confirm/cancel button symbols.
   A1 F0 and A1 C1 are the Circle and Cross glyphs of the GBK font
   (FONT_BTN_CIRCLE_GBK / FONT_BTN_CROSS_GBK); they trade places when
   option_swap_confirm_buttons is set. */
static void copy_swap_aware(const char *src, char *buf, size_t size)
{
    char *dst = buf;
    const char *s = src;

    if (size < 3)
    {
        if (size != 0)
            buf[0] = '\0';
        return;
    }

    while (*s != '\0' && dst < buf + size - 3)
    {
        if ((u8)s[0] == 0xA1 && (u8)s[1] == 0xF0)
        {
            *dst++ = '\xA1';
            *dst++ = option_swap_confirm_buttons ? '\xC1' : '\xF0';
            s += 2;
        }
        else if ((u8)s[0] == 0xA1 && (u8)s[1] == 0xC1)
        {
            *dst++ = '\xA1';
            *dst++ = option_swap_confirm_buttons ? '\xF0' : '\xC1';
            s += 2;
        }
        else
        {
            *dst++ = *s++;
        }
    }
    *dst = '\0';
}

/* Swap-aware print: copies src to a temp buffer while swapping GBK Circle
   and Cross symbols based on option_swap_confirm_buttons. */
void print_swap_aware(const char *src, u32 x, u32 y, u16 color, u16 bg)
{
    char buf[256];

    copy_swap_aware(src, buf, sizeof(buf));

    print_string_gbk(buf, x, y, color, bg);
}

/* ------------------------------------------------------------------
   Scrolling text helpers — ping-pong character-stepped
   ------------------------------------------------------------------ */
/* Timing in redraws: one character every SCROLL_SPEED_FRAME frames, with a
   SCROLL_EDGE_PAUSE pause at each end. option_language 0 (Japanese) prints
   Shift-JIS with print_string(); every other language uses the GBK font. */
#define SCROLL_SPEED_FRAME  2
#define SCROLL_EDGE_PAUSE   60

#define SCROLL_MENU_MAX_CHARS   ((SCREEN_IMAGE_POS_X - MENU_LIST_POS_X) / FONTWIDTH)
#define SCROLL_TITLE_MAX_CHARS  ((PSP_SCREEN_WIDTH - 228) / FONTWIDTH)
#define SCROLL_FILE_MAX_CHARS   ((FILE_LIST_RIGHT_EDGE - FILE_LIST_POS_X) / FONTWIDTH)
#define SCROLL_DIR_MAX_CHARS    ((PSP_SCREEN_WIDTH - DIR_LIST_POS_X) / FONTWIDTH)

typedef struct {
    u32 menu_tick;
    u32 last_menu_hash;
    u32 file_tick;
    u32 last_file_hash;
    u32 title_tick;
    u32 last_title_hash;
} ScrollState;

static ScrollState g_scroll = {0, ~0U, 0, ~0U, 0, ~0U};

/* The update_*_scroll() helpers restart a scroller when the hash of what it
   shows changes, i.e. when the selection moves. */
static u32 hash_string(const char *s)
{
    u32 h = 0;
    while (*s) {
        h = h * 31 + (u8)*s++;
    }
    return h;
}

/* First character to show at this tick: pause, scroll to the end, pause,
   scroll back. Returns 0 when the string already fits. */
static s32 compute_scroll_offset(u32 tick, u32 str_len, u32 max_chars)
{
    if (str_len <= max_chars)
        return 0;

    u32 scroll_len = str_len - max_chars;
    u32 half_cycle = SCROLL_EDGE_PAUSE + (scroll_len * SCROLL_SPEED_FRAME);
    u32 full_cycle = half_cycle * 2;
    u32 t = tick % full_cycle;

    if (t < SCROLL_EDGE_PAUSE)
        return 0;
    t -= SCROLL_EDGE_PAUSE;
    if (t < scroll_len * SCROLL_SPEED_FRAME)
        return (s32)(t / SCROLL_SPEED_FRAME);
    t -= scroll_len * SCROLL_SPEED_FRAME;
    if (t < SCROLL_EDGE_PAUSE)
        return (s32)scroll_len;
    t -= SCROLL_EDGE_PAUSE;
    return (s32)(scroll_len - (t / SCROLL_SPEED_FRAME));
}

static void update_menu_scroll(u32 hash)
{
    if (hash != g_scroll.last_menu_hash) {
        g_scroll.last_menu_hash = hash;
        g_scroll.menu_tick = 0;
    } else {
        g_scroll.menu_tick++;
    }
}

static void update_file_scroll(u32 hash)
{
    if (hash != g_scroll.last_file_hash) {
        g_scroll.last_file_hash = hash;
        g_scroll.file_tick = 0;
    } else {
        g_scroll.file_tick++;
    }
}

static void update_title_scroll(u32 hash)
{
    if (hash != g_scroll.last_title_hash) {
        g_scroll.last_title_hash = hash;
        g_scroll.title_tick = 0;
    } else {
        g_scroll.title_tick++;
    }
}

static void print_string_scroll(const char *str, u32 x, u32 y, u16 color, u16 bg, u32 max_chars, s32 scroll_offset)
{
    u32 len = strlen(str);
    if (len == 0) return;

    if (len <= max_chars) {
        if (option_language == LANGUAGE_JAPANESE)
            print_string(str, x, y, color, bg);
        else
            print_string_gbk(str, x, y, color, bg);
        return;
    }

    MenuTextEncoding encoding = option_language == LANGUAGE_JAPANESE ?
        MENU_TEXT_SHIFT_JIS : MENU_TEXT_GBK;
    u32 start = menu_text_scroll_start(str,
        scroll_offset > 0 ? (u32)scroll_offset : 0, encoding);
    if (max_chars >= 256) max_chars = 255;
    u32 clip_len = menu_text_clip_length_for_encoding(str + start,
        max_chars, encoding);

    char buf[256];
    memcpy(buf, str + start, clip_len);
    buf[clip_len] = '\0';

    if (option_language == LANGUAGE_JAPANESE)
        print_string(buf, x, y, color, bg);
    else
        print_string_gbk(buf, x, y, color, bg);
}

/* Clip any string to max_chars width — used for non-selected items */
static void print_string_clipped(const char *str, u32 x, u32 y, u16 color, u16 bg, u32 max_chars)
{
    u32 len = strlen(str);
    if (len == 0) return;

    if (len <= max_chars) {
        if (option_language == LANGUAGE_JAPANESE)
            print_string(str, x, y, color, bg);
        else
            print_string_gbk(str, x, y, color, bg);
        return;
    }

    if (max_chars >= 256) max_chars = 255;

    char buf[256];
    u32 clip_len = menu_text_clip_length_for_encoding(str, max_chars,
        option_language == LANGUAGE_JAPANESE ?
        MENU_TEXT_SHIFT_JIS : MENU_TEXT_GBK);
    memcpy(buf, str, clip_len);
    buf[clip_len] = '\0';

    if (option_language == LANGUAGE_JAPANESE)
        print_string(buf, x, y, color, bg);
    else
        print_string_gbk(buf, x, y, color, bg);
}

static void draw_color_picker(u32 h, u32 s, u32 v, s32 cx, s32 cy, s32 hue_seg,
                              u16 original_color, u16 current_color, u32 title_msg_idx)
{
    u32 gx, gy, px, py;
    u16 c;

    clear_screen(COLOR15_TO_32(color_bg));

    /* Title */
    print_string_gbk(MSG[title_msg_idx], 10, 10, color_active_item, BG_NO_FILL);

    /* SV square (Saturation x Value) */
    for (gy = 0; gy < PICKER_SV_GRID; gy++)
    {
        for (gx = 0; gx < PICKER_SV_GRID; gx++)
        {
            u32 gs = (gx * 255) / (PICKER_SV_GRID - 1);
            u32 gv = ((PICKER_SV_GRID - 1 - gy) * 255) / (PICKER_SV_GRID - 1);
            c  = hsv_to_color15(h, gs, gv);
            px = PICKER_SV_X + gx * PICKER_SV_CELL;
            py = PICKER_SV_Y + gy * PICKER_SV_CELL;
            draw_box_fill(px, py, px + PICKER_SV_CELL - 1, py + PICKER_SV_CELL - 1, c);
        }
    }

    draw_box_line(PICKER_SV_X - 1, PICKER_SV_Y - 1,
                  PICKER_SV_X + PICKER_SV_SIZE, PICKER_SV_Y + PICKER_SV_SIZE,
                  COLOR15_WHITE);

    /* Cursor on SV square */
    px = PICKER_SV_X + cx * PICKER_SV_CELL;
    py = PICKER_SV_Y + cy * PICKER_SV_CELL;
    draw_box_line(px - 1, py - 1, px + PICKER_SV_CELL,     py + PICKER_SV_CELL,     COLOR15_WHITE);
    draw_box_line(px - 2, py - 2, px + PICKER_SV_CELL + 1, py + PICKER_SV_CELL + 1, 0);

    /* Hue bar */
    for (gy = 0; gy < PICKER_HUE_SEGS; gy++)
    {
        u32 hh = (gy * 360) / PICKER_HUE_SEGS;
        c  = hsv_to_color15(hh, 255, 255);
        py = PICKER_HUE_Y + gy * PICKER_HUE_CELL;
        draw_box_fill(PICKER_HUE_X, py, PICKER_HUE_X + PICKER_HUE_W - 1, py + PICKER_HUE_CELL - 1, c);
    }

    draw_box_line(PICKER_HUE_X - 1, PICKER_HUE_Y - 1,
                  PICKER_HUE_X + PICKER_HUE_W, PICKER_HUE_Y + PICKER_HUE_H,
                  COLOR15_WHITE);

    /* Hue cursor */
    py = PICKER_HUE_Y + hue_seg * PICKER_HUE_CELL;
    draw_hline(PICKER_HUE_X - 4, PICKER_HUE_X + PICKER_HUE_W + 3, py, COLOR15_WHITE);
    draw_hline(PICKER_HUE_X - 4, PICKER_HUE_X + PICKER_HUE_W + 3, py + PICKER_HUE_CELL - 1, COLOR15_WHITE);

    /* Preview: original */
    draw_box_fill(PICKER_PREV_X, PICKER_PREV_Y,
                  PICKER_PREV_X + PICKER_PREV_SIZE - 1,
                  PICKER_PREV_Y + PICKER_PREV_SIZE - 1, original_color);
    draw_box_line(PICKER_PREV_X - 1, PICKER_PREV_Y - 1,
                  PICKER_PREV_X + PICKER_PREV_SIZE,
                  PICKER_PREV_Y + PICKER_PREV_SIZE, COLOR15_WHITE);

    /* Preview: current */
    draw_box_fill(PICKER_PREV_X, PICKER_PREV_Y + PICKER_PREV_SIZE + 20,
                  PICKER_PREV_X + PICKER_PREV_SIZE - 1,
                  PICKER_PREV_Y + PICKER_PREV_SIZE * 2 + 19, current_color);
    draw_box_line(PICKER_PREV_X - 1, PICKER_PREV_Y + PICKER_PREV_SIZE + 19,
                  PICKER_PREV_X + PICKER_PREV_SIZE,
                  PICKER_PREV_Y + PICKER_PREV_SIZE * 2 + 20, COLOR15_WHITE);

    /* Labels */
    if (option_language == LANGUAGE_JAPANESE)
    {
        print_string_gbk(MSG[MSG_PICKER_OLD], PICKER_PREV_X, PICKER_PREV_Y + PICKER_PREV_SIZE + 3, color_help_text, BG_NO_FILL);
        print_string_gbk(MSG[MSG_PICKER_NEW], PICKER_PREV_X, PICKER_PREV_Y + PICKER_PREV_SIZE * 2 + 23, color_help_text, BG_NO_FILL);
        print_string_gbk(MSG[MSG_PICKER_CONTROLS], 25, 180, color_help_text, BG_NO_FILL);
        print_string_gbk(MSG[MSG_CUSTOM_COLOR_HELP_ITEM], 30, 258, color_help_text, BG_NO_FILL);
    }
    else
    {
        print_string_gbk(MSG[MSG_PICKER_OLD], PICKER_PREV_X, PICKER_PREV_Y + PICKER_PREV_SIZE + 3, color_help_text, BG_NO_FILL);
        print_string_gbk(MSG[MSG_PICKER_NEW], PICKER_PREV_X, PICKER_PREV_Y + PICKER_PREV_SIZE * 2 + 23, color_help_text, BG_NO_FILL);
        print_string_gbk(MSG[MSG_PICKER_CONTROLS], 25, 180, color_help_text, BG_NO_FILL);
        print_string_gbk(MSG[MSG_CUSTOM_COLOR_HELP_ITEM], 30, 258, color_help_text, BG_NO_FILL);
    }

    /* Live theme preview panel */
    draw_theme_preview(current_color);
}

/* Clock and battery at the top right; the strings refresh every 30 frames. */
static void draw_status_bar(void)
{
    static char time_str[40];
    static char batt_str[40];
    u16 color_batt_life = color_batt_normal;
    static u32 status_counter = 0;

    if ((status_counter % 30) == 0)
    {
        update_status_string_gbk(time_str, batt_str, &color_batt_life);
    }
    status_counter++;

    if (option_language == LANGUAGE_JAPANESE)
    {
        print_string_gbk(time_str, TIME_STATUS_POS_X, 2, color_help_text, BG_NO_FILL);
        print_string_gbk(batt_str, BATT_STATUS_POS_X, 2, color_batt_life, BG_NO_FILL);
    }
    else
    {
        print_string_gbk(time_str, TIME_STATUS_POS_X, 2, color_help_text, BG_NO_FILL);
        print_string_gbk(batt_str, BATT_STATUS_POS_X, 2, color_batt_life, BG_NO_FILL);
    }
}

/* Modal HSV picker for one theme colour. The SV square is a 16x16 grid
   (cx = saturation, cy = inverted value) and the hue bar has 32 steps. The
   colour is written to *color_ptr live so the preview updates. Returns the
   chosen colour on confirm and the original colour on cancel. */
static u16 run_color_picker(u16 *color_ptr, u32 title_msg_idx)
{
    u32 h, s, v;
    rgb15_to_hsv(*color_ptr, &h, &s, &v);

    s32 cx = (s * (PICKER_SV_GRID - 1)) / 255;
    s32 cy = ((255 - v) * (PICKER_SV_GRID - 1)) / 255;
    s32 hue_seg = (h * PICKER_HUE_SEGS) / 360;

    u16 original_color = *color_ptr;
    u16 current_color  = *color_ptr;
    GUI_ACTION_TYPE gui_action;
    u32 repeat = 1;

    while (repeat)
    {
        h = (hue_seg * 360) / PICKER_HUE_SEGS;
        s = (cx * 255) / (PICKER_SV_GRID - 1);
        v = ((PICKER_SV_GRID - 1 - cy) * 255) / (PICKER_SV_GRID - 1);
        current_color = hsv_to_color15(h, s, v);

        /* Live preview: write to global immediately so UI elements update */
        *color_ptr = current_color;

        draw_color_picker(h, s, v, cx, cy, hue_seg, original_color, current_color, title_msg_idx);
        draw_status_bar();
        flip_screen(1);

        gui_action = get_gui_input();

        switch (gui_action)
        {
            case CURSOR_LEFT:
                if (cx > 0) cx--;
                break;
            case CURSOR_RIGHT:
                if (cx < PICKER_SV_GRID - 1) cx++;
                break;
            case CURSOR_UP:
                if (cy > 0) cy--;
                break;
            case CURSOR_DOWN:
                if (cy < PICKER_SV_GRID - 1) cy++;
                break;
            case CURSOR_LTRIGGER:
                if (hue_seg > 0) hue_seg--;
                else hue_seg = PICKER_HUE_SEGS - 1;
                break;
            case CURSOR_RTRIGGER:
                hue_seg = (hue_seg + 1) % PICKER_HUE_SEGS;
                break;
            case CURSOR_SELECT:
                *color_ptr = current_color;
                return current_color;
            case CURSOR_EXIT:
                return original_color;
            case CURSOR_BACK:
                /* Square: reset to preset default instead of canceling */
                {
                  u16 preset = get_preset_color(option_theme, color_picker_item_index);
                  *color_ptr = preset;
                  rgb15_to_hsv(preset, &h, &s, &v);
                  cx = (s * (PICKER_SV_GRID - 1)) / 255;
                  cy = ((255 - v) * (PICKER_SV_GRID - 1)) / 255;
                  hue_seg = (h * PICKER_HUE_SEGS) / 360;
                }
                break;
            default:
                break;
        }
    }

    *color_ptr = original_color;
    return original_color;
}

/* Reset single color to its preset default (Square / CURSOR_BACK in Custom Colors list) */
void menu_reset_single_color(u32 idx)
{
    if (idx >= NUM_COLOR_ITEMS)
        return;

    u16 preset_color = get_preset_color(option_theme, idx);
    *color_items[idx].color_ptr = preset_color;
    save_theme_config();
}

/* File-scope color picker entry — no nested function, uses global index */
void menu_pick_color(void)
{
    u16 *target = color_items[color_picker_item_index].color_ptr;
    u32 title   = color_items[color_picker_item_index].msg_index;
    run_color_picker(target, title);
    save_theme_config();
}

s32 save_theme_config(void)
{
    SceUID fd;
    char path[MAX_PATH];
    sprintf(path, "%s%s", main_path, GPSP_THEME_FILENAME);

    scePowerLock(0);
    FILE_OPEN(fd, path, WRITE);

    if (FILE_CHECK_VALID(fd))
    {
        u16 colors[11] = {
            color_bg, color_rom_info, color_active_item, color_inactive_item,
            color_tooltip_text, color_help_text, color_inactive_dir, color_scroll_bar,
            color_batt_normal, color_batt_low, color_batt_charg
        };
        FILE_WRITE(fd, colors, sizeof(colors));
        FILE_CLOSE(fd);
        scePowerUnlock(0);
        return 0;
    }

    scePowerUnlock(0);
    return -1;
}

s32 load_theme_config(void)
{
    SceUID fd;
    char path[MAX_PATH];
    sprintf(path, "%s%s", main_path, GPSP_THEME_FILENAME);

    scePowerLock(0);
    FILE_OPEN(fd, path, READ);

    if (FILE_CHECK_VALID(fd))
    {
        u16 colors[11];
        s32 read_len = FILE_READ(fd, colors, sizeof(colors));
        FILE_CLOSE(fd);
        scePowerUnlock(0);

        if (read_len == sizeof(colors))
        {
            color_bg            = colors[0];
            color_rom_info      = colors[1];
            color_active_item   = colors[2];
            color_inactive_item = colors[3];
            color_tooltip_text  = colors[4];
            color_help_text     = colors[5];
            color_inactive_dir  = colors[6];
            color_scroll_bar    = colors[7];
            color_batt_normal   = colors[8];
            color_batt_low      = colors[9];
            color_batt_charg    = colors[10];
            return 0;
        }
    }
    else
    {
        scePowerUnlock(0);
    }

    return -1;
}

/* File browser layout: files on the left, directories in a right-hand
   column starting at DIR_LIST_POS_X. L/R page by PAGE_SCROLL_NUM entries. */
#define FILE_LIST_ROWS        (20)
#define FILE_LIST_POS_X       (10) //18 default
#define FILE_LIST_POS_Y       (16)
#define DIR_LIST_POS_X        (340)
#define FILE_LIST_RIGHT_EDGE  (330)
#define PAGE_SCROLL_NUM       (10)



// scroll bar: outline box at the left edge of the file list. The *I macros
// are the filled thumb inside it, 2 px in from the outline. SBAR_T/B/H and
// SBAR_Y1I/Y2I are unused: load_file() computes the track itself because the
// list shrinks when the recent games section is shown.
#define SBAR_X1  (2)
#define SBAR_X2  (6) // 12 default
#define SBAR_Y1  (16)
#define SBAR_Y2  (255)

#define SBAR_T   (SBAR_Y1 + 2)
#define SBAR_B   (SBAR_Y2 - 2)
#define SBAR_H   (SBAR_B - SBAR_T)
#define SBAR_X1I (SBAR_X1 + 2)
#define SBAR_X2I (SBAR_X2 - 2)
#define SBAR_Y1I (SBAR_H * scroll_value[0] / num[0] + SBAR_T)
#define SBAR_Y2I (SBAR_H * (scroll_value[0] + FILE_LIST_ROWS) / num[0] + SBAR_T)


/* ------------------------------------------------------------------
   Menu definitions
   ------------------------------------------------------------------ */

/* A MenuType is one menu screen; its options are the rows. The option type
   bits combine, e.g. a savestate slot is a NUMBER_SELECTION_OPTION that is
   also an ACTION_OPTION. */
typedef enum
{
  NUMBER_SELECTION_OPTION = 0x01,
  STRING_SELECTION_OPTION = 0x02,
  SUBMENU_OPTION          = 0x04,
  ACTION_OPTION           = 0x08
} MENU_OPTION_TYPE_ENUM;

struct _MenuType
{
  void (*init_function)(void);
  void (*passive_function)(void);
  struct _MenuOptionType *options;
  u32 num_options;
  u32 selected_option;
  struct _MenuType *parent;
};

struct _MenuOptionType
{
  void (*action_function)(void);    // run on confirm
  void (*passive_function)(void);   // run after the value changes
  struct _MenuType *sub_menu;
  const char *display_string;       // label, may hold one %s or %d
  void *options;                    // value names for string selections
  u32 *current_option;              // value being edited
  u32 num_options;                  // value count; left/right wrap at it
  u32 help_string;                  // MSG index for the footer help line
  u32 line_number;                  // screen row, gaps give blank lines
  MENU_OPTION_TYPE_ENUM option_type;
  u32 tooltip_string;   // MSG index for tooltip, 0 = none
  u32 display_msg;      // MSG index for display_string, 0 = dynamic
  int (*available_function)(void);
  int (*visible_function)(void);
  u32 unavailable_string;
  u32 behavior_flags;
};

typedef struct _MenuOptionType MenuOptionType;
typedef struct _MenuType MenuType;

#define MAX_MENU_MODEL_ROWS 32
/* behavior_flags: a status line the cursor skips over. */
#define MENU_OPTION_PASSIVE_ROW (1U << 0)

static int legacy_menu_row_visible(void *context)
{
  MenuOptionType *option = (MenuOptionType *)context;

  return option->visible_function == NULL || option->visible_function();
}

static int legacy_menu_row_available(void *context)
{
  MenuOptionType *option = (MenuOptionType *)context;

  return option->available_function == NULL || option->available_function();
}

static int menu_option_visible(const MenuOptionType *option)
{
  return option->visible_function == NULL || option->visible_function();
}

static int menu_option_available(const MenuOptionType *option)
{
  return option->available_function == NULL || option->available_function();
}

/* Mirror a MenuType into MenuRows so menu_model.c drives cursor movement
   (skipping hidden and passive rows). Values are still changed by menu(). */
static void build_legacy_menu_model(MenuModel *model, MenuRow *rows,
 MenuType *menu)
{
  u32 i;
  u32 row_count = menu->num_options;

  if(row_count > MAX_MENU_MODEL_ROWS)
    row_count = MAX_MENU_MODEL_ROWS;

  memset(rows, 0, sizeof(*rows) * row_count);
  for(i = 0; i < row_count; i++)
  {
    MenuOptionType *option = &menu->options[i];
    MenuRow *row = &rows[i];

    row->id = option->display_string;
    row->label_id = option->display_msg;
    row->flags = MENU_ROW_WRAP_VALUE;
    if((option->behavior_flags & MENU_OPTION_PASSIVE_ROW) == 0)
      row->flags |= MENU_ROW_SELECTABLE;
    row->visible = legacy_menu_row_visible;
    row->available = legacy_menu_row_available;
    row->unavailable_reason_id = option->unavailable_string;
    row->help.summary_id = option->help_string;
    row->help.impact_id = option->tooltip_string;
    row->value = option->current_option;
    row->value_count = option->num_options;
    row->context = option;

    if((option->option_type & (NUMBER_SELECTION_OPTION |
                               STRING_SELECTION_OPTION)) != 0)
      row->kind = MENU_ROW_VALUE;
    else if((option->option_type & SUBMENU_OPTION) != 0)
      row->kind = MENU_ROW_SUBMENU;
    else
      row->kind = MENU_ROW_ACTION;
  }

  menu_model_init(model, rows, row_count);
  if(menu->selected_option < row_count)
  {
    model->selected_index = menu->selected_option;
    menu_model_reconcile(model);
  }
  if(model->selected_index < row_count)
    menu->selected_option = (u32)model->selected_index;
}

/* Option initializers, positional in struct _MenuOptionType field order.
   MAKE_MENU(foo, ...) defines foo_menu over the foo_options[] array. */
#define MAKE_MENU(name, init_function, passive_function)    \
  MenuType name##_menu =                                    \
  {                                                         \
    init_function,                                          \
    passive_function,                                       \
    name##_options,                                         \
    sizeof(name##_options) / sizeof(MenuOptionType),        \
    0,                                                      \
    NULL                                                    \
  }                                                         \

#define GAMEPAD_CONFIG_OPTION(display_string, number, display_msg)      \
{                                                                       \
  NULL,                                                                 \
  rebuild_fast_forward_button_mask,                                     \
  NULL,                                                                 \
  display_string,                                                       \
  gamepad_config_buttons,                                               \
  gamepad_config_map + gamepad_config_line_to_button[number],           \
  sizeof(gamepad_config_buttons) / sizeof(gamepad_config_buttons[0]),   \
  MSG_PAD_MENU_HELP_0,                                                  \
  number,                                                               \
  STRING_SELECTION_OPTION,                                              \
  0,                                                                    \
  display_msg                                                           \
}                                                                       \

#define ANALOG_CONFIG_OPTION(display_string, number, display_msg)       \
{                                                                       \
  NULL,                                                                 \
  rebuild_fast_forward_button_mask,                                     \
  NULL,                                                                 \
  display_string,                                                       \
  gamepad_config_buttons,                                               \
  gamepad_config_map + number + 12,                                     \
  sizeof(gamepad_config_buttons) / sizeof(gamepad_config_buttons[0]),   \
  MSG_PAD_MENU_HELP_0,                                                  \
  number,                                                               \
  STRING_SELECTION_OPTION,                                              \
  0,                                                                    \
  display_msg                                                           \
}                                                                       \

/* Cheat rows are re-pointed at the current page by reload_cheats_page(). */
#define CHEAT_OPTION(number)        \
{                                   \
  NULL,                             \
  NULL,                             \
  NULL,                             \
  cheat_format_str[number],         \
  enable_disable_options,           \
  &(cheats[number].cheat_active),   \
  2,                                \
  MSG_CHEAT_MENU_HELP_0,            \
  (number) % 10,                    \
  STRING_SELECTION_OPTION,          \
  0,                                \
  0                                 \
}                                   \

/* Slot rows share savestate_action: left/right toggles Load and Save. */
#define SAVESTATE_OPTION(number)              \
{                                             \
  NULL,                                       \
  NULL,                                       \
  NULL,                                       \
  savestate_timestamps[number],               \
  NULL,                                       \
  &savestate_action,                          \
  2,                                          \
  MSG_STATE_MENU_HELP_0,                      \
  number,                                     \
  NUMBER_SELECTION_OPTION | ACTION_OPTION,    \
  0,                                          \
  0                                           \
}                                             \

#define ACTION_OPTION(action_function, passive_function, display_string, help_string, line_number, display_msg)   \
{                                                                                                                 \
  action_function,                                                                                                \
  passive_function,                                                                                               \
  NULL,                                                                                                           \
  display_string,                                                                                                 \
  NULL,                                                                                                           \
  NULL,                                                                                                           \
  0,                                                                                                              \
  help_string,                                                                                                    \
  line_number,                                                                                                    \
  ACTION_OPTION,                                                                                                  \
  0,                                                                                                              \
  display_msg                                                                                                     \
}                                                                                                                 \

#define SUBMENU_OPTION(sub_menu, display_string, help_string, line_number, display_msg)   \
{                                                                                         \
  NULL,                                                                                   \
  NULL,                                                                                   \
  sub_menu,                                                                               \
  display_string,                                                                         \
  NULL,                                                                                   \
  NULL,                                                                                   \
  sizeof(sub_menu) / sizeof(MenuOptionType),                                              \
  help_string,                                                                            \
  line_number,                                                                            \
  SUBMENU_OPTION,                                                                         \
  0,                                                                                      \
  display_msg                                                                             \
}                                                                                         \

#define ACTION_SUBMENU_OPTION(sub_menu, action_function, display_string, help_string, line_number, display_msg)   \
{                                                                                                                 \
  action_function,                                                                                                \
  NULL,                                                                                                           \
  sub_menu,                                                                                                       \
  display_string,                                                                                                 \
  NULL,                                                                                                           \
  NULL,                                                                                                           \
  sizeof(sub_menu) / sizeof(MenuOptionType),                                                                      \
  help_string,                                                                                                    \
  line_number,                                                                                                    \
  SUBMENU_OPTION | ACTION_OPTION,                                                                                 \
  0,                                                                                                              \
  display_msg                                                                                                     \
}                                                                                                                 \

#define SELECTION_OPTION(passive_function, display_string, options, option_ptr, num_options, help_string, line_number, type, display_msg)   \
{                                                                                                                                           \
  NULL,                                                                                                                                     \
  passive_function,                                                                                                                         \
  NULL,                                                                                                                                     \
  display_string,                                                                                                                           \
  options,                                                                                                                                  \
  option_ptr,                                                                                                                               \
  num_options,                                                                                                                              \
  help_string,                                                                                                                              \
  line_number,                                                                                                                              \
  type,                                                                                                                                     \
  0,                                                                                                                                        \
  display_msg                                                                                                                               \
}                                                                                                                                           \

#define ACTION_SELECTION_OPTION(action_function, passive_function, display_string, options, option_ptr, num_options, help_string, line_number, type, display_msg)   \
{                                                                                                                                                                   \
  action_function,                                                                                                                                                  \
  passive_function,                                                                                                                                                 \
  NULL,                                                                                                                                                             \
  display_string,                                                                                                                                                   \
  options,                                                                                                                                                          \
  option_ptr,                                                                                                                                                       \
  num_options,                                                                                                                                                      \
  help_string,                                                                                                                                                      \
  line_number,                                                                                                                                                      \
  type | ACTION_OPTION,                                                                                                                                             \
  0,                                                                                                                                                                \
  display_msg                                                                                                                                                       \
}                                                                                                                                                                   \


#define STRING_SELECTION_OPTION(passive_function, display_string, options, option_ptr, num_options, help_string, line_number, display_msg) \
  SELECTION_OPTION(passive_function, display_string, options, option_ptr, num_options, help_string, line_number, STRING_SELECTION_OPTION, display_msg)

#define NUMERIC_SELECTION_OPTION(passive_function, display_string, option_ptr, num_options, help_string, line_number, display_msg) \
  SELECTION_OPTION(passive_function, display_string, NULL, option_ptr, num_options, help_string, line_number, NUMBER_SELECTION_OPTION, display_msg)

#define STRING_SELECTION_ACTION_OPTION(action_function, passive_function, display_string, options, option_ptr, num_options, help_string, line_number, display_msg) \
  ACTION_SELECTION_OPTION(action_function, passive_function, display_string,  options, option_ptr, num_options, help_string, line_number, STRING_SELECTION_OPTION, display_msg)

#define NUMERIC_SELECTION_ACTION_OPTION(action_function, passive_function, display_string, option_ptr, num_options, help_string, line_number, display_msg) \
  ACTION_SELECTION_OPTION(action_function, passive_function, display_string,  NULL, option_ptr, num_options, help_string, line_number, NUMBER_SELECTION_OPTION, display_msg)

#define NUMERIC_SELECTION_ACTION_HIDE_OPTION(action_function, passive_function, display_string, option_ptr, num_options, help_string, line_number, display_msg) \
  ACTION_SELECTION_OPTION(action_function, passive_function, display_string, NULL, option_ptr, num_options, help_string, line_number, NUMBER_SELECTION_OPTION, display_msg)

/* --------------------------------------------------------------------------
   Tooltip macros (_TT variants).
   These are identical to the originals but add a `tooltip` MSG index.
   -------------------------------------------------------------------------- */
#define ACTION_OPTION_TT(action_function, passive_function, display_string, help_string, line_number, tooltip)    \
{                                                                                                                 \
  action_function,                                                                                                \
  passive_function,                                                                                               \
  NULL,                                                                                                           \
  display_string,                                                                                                 \
  NULL,                                                                                                           \
  NULL,                                                                                                           \
  0,                                                                                                              \
  help_string,                                                                                                    \
  line_number,                                                                                                    \
  ACTION_OPTION,                                                                                                  \
  tooltip                                                                                                         \
}                                                                                                                 \

#define SUBMENU_OPTION_TT(sub_menu, display_string, help_string, line_number, tooltip)    \
{                                                                                         \
  NULL,                                                                                   \
  NULL,                                                                                   \
  sub_menu,                                                                               \
  display_string,                                                                         \
  NULL,                                                                                   \
  NULL,                                                                                   \
  sizeof(sub_menu) / sizeof(MenuOptionType),                                              \
  help_string,                                                                            \
  line_number,                                                                            \
  SUBMENU_OPTION,                                                                         \
  tooltip                                                                                 \
}                                                                                         \

#define SELECTION_OPTION_TT(passive_function, display_string, options, option_ptr, num_options, help_string, line_number, type, tooltip, display_msg)   \
{                                                                                                                                                       \
  NULL,                                                                                                                                                 \
  passive_function,                                                                                                                                     \
  NULL,                                                                                                                                                 \
  display_string,                                                                                                                                       \
  options,                                                                                                                                              \
  option_ptr,                                                                                                                                           \
  num_options,                                                                                                                                          \
  help_string,                                                                                                                                          \
  line_number,                                                                                                                                          \
  type,                                                                                                                                                 \
  tooltip,                                                                                                                                              \
  display_msg                                                                                                                                           \
}                                                                                                                                                       \

#define STRING_SELECTION_OPTION_TT(passive_function, display_string, options, option_ptr, num_options, help_string, line_number, tooltip, display_msg) \
  SELECTION_OPTION_TT(passive_function, display_string, options, option_ptr, num_options, help_string, line_number, STRING_SELECTION_OPTION, tooltip, display_msg)

#define NUMERIC_SELECTION_OPTION_TT(passive_function, display_string, option_ptr, num_options, help_string, line_number, tooltip, display_msg) \
  SELECTION_OPTION_TT(passive_function, display_string, NULL, option_ptr, num_options, help_string, line_number, NUMBER_SELECTION_OPTION, tooltip, display_msg)

char dir_roms[MAX_PATH];
char dir_save[MAX_PATH];
char dir_state[MAX_PATH];
char dir_cfg[MAX_PATH];
char dir_snap[MAX_PATH];
char dir_cheat[MAX_PATH];//cheat
char dir_boxart[MAX_PATH];
char dir_boxart_folders[MAX_PATH];

u32 menu_cheat_page = 0;
/* Button config menu row -> gamepad_config_map[] index (PSP button). The
   analog stick directions follow at indices 12-15. */
u32 ALIGN_DATA gamepad_config_line_to_button[] =
{
 8,
 6,
 7,
 9,
 1,
 2,
 3,
 0,
 4,
 5,
 11,
 10,
};

u32 savestate_slot = 0;


/* On MIPS, GCC calls _flush_cache() after writing a nested function's
   trampoline to the stack (this file relies on nested functions); the PSP
   C library does not provide it. */
void _flush_cache(void) __attribute__((used, externally_visible));

static int sort_function(const void *dest_str_ptr, const void *src_str_ptr);

static s32 save_game_config_file(void);

static void get_timestamp_string(char *buffer, u16 msg_id, ScePspDateTime *msg_time, int day_of_week);

static void get_savestate_info(char *filename, u16 *snapshot, char *timestamp);
static void get_savestate_filename(u32 slot, char *name_buffer);


/* ========================================================================
 * Savestate preview extraction for file browser
 *
 * Savestate files (.svs) begin with a 240x160 15-bit (PSP 5551) screenshot.
 * We extract it and scale to 141x141 for the boxart preview panel.
 * ======================================================================== */

/* 5-slot RAM cache for savestate previews in carousel mode */
typedef struct {
    u16 *buffer;
    char name[MAX_FILE];
    u32 valid;
} SavestatePreviewSlot;

/* Static solid-color texture for NO IMG carousel slots.
   Drawn through GE to avoid CPU cache vs. GE race on real PSP HW. */
static u16 noimg_texture[BOXART_W * BOXART_H];
static u16 noimg_texture_color = 0xFFFF; /* 0xFFFF = uninitialized */

static SavestatePreviewSlot savestate_preview_cache[CAROUSEL_SLOTS];

/* Nearest-neighbour downscale from 16-bit source to 141x141 */
static void scale_nearest_16bit(u16 *src, u16 *dst, u32 src_w, u32 src_h, u32 src_pitch)
{
    u32 x, y;
    for (y = 0; y < BOXART_H; y++)
    {
        u32 src_y = (y * src_h) / BOXART_H;
        for (x = 0; x < BOXART_W; x++)
        {
            u32 src_x = (x * src_w) / BOXART_W;
            dst[y * BOXART_W + x] = src[src_y * src_pitch + src_x];
        }
    }
}

/* Extract 141x141 preview from a .svs file.
 * Returns malloc'd buffer, or NULL on failure. */
static u16 *savestate_extract_preview(const char *path)
{
    SceUID fd;
    u16 *src = NULL;
    u16 *dst = NULL;

    fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return NULL;

    src = (u16 *)malloc(GBA_SCREEN_SIZE);
    if (!src)
        goto cleanup;

    s32 n = sceIoRead(fd, src, GBA_SCREEN_SIZE);
    if (n != (s32)GBA_SCREEN_SIZE)
        goto cleanup;

    dst = (u16 *)malloc(BOXART_W * BOXART_H * sizeof(u16));
    if (!dst)
        goto cleanup;

    scale_nearest_16bit(src, dst, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT, GBA_SCREEN_WIDTH);

    free(src);
    sceIoClose(fd);
    return dst;

cleanup:
    if (src) free(src);
    if (dst) free(dst);
    sceIoClose(fd);
    return NULL;
}

/* Look up a savestate preview in the carousel RAM cache.
 * If miss, extract from disk and store. Returns buffer or NULL. */
static u16 *savestate_preview_get(const char *name)
{
    u32 i;
    u32 empty_idx = (u32)-1;
    char path[MAX_PATH];

    if (!name || !name[0])
        return NULL;

    /* Search existing cache */
    for (i = 0; i < CAROUSEL_SLOTS; i++)
    {
        if (savestate_preview_cache[i].valid &&
            strcmp(savestate_preview_cache[i].name, name) == 0)
        {
            return savestate_preview_cache[i].buffer;
        }
        if (!savestate_preview_cache[i].valid && empty_idx == (u32)-1)
            empty_idx = i;
    }

    /* Miss — extract from file */
    sprintf(path, "%s%s", dir_state, name);
    u16 *loaded = savestate_extract_preview(path);

    /* Store in cache */
    if (empty_idx != (u32)-1)
    {
        savestate_preview_cache[empty_idx].buffer = loaded;
        savestate_preview_cache[empty_idx].valid = 1;
        strncpy(savestate_preview_cache[empty_idx].name, name, MAX_FILE - 1);
        savestate_preview_cache[empty_idx].name[MAX_FILE - 1] = '\0';
        return loaded;
    }

    /* No empty slots — evict slot 0, shift down */
    if (savestate_preview_cache[0].buffer)
        free(savestate_preview_cache[0].buffer);
    for (i = 1; i < CAROUSEL_SLOTS; i++)
        savestate_preview_cache[i - 1] = savestate_preview_cache[i];

    savestate_preview_cache[CAROUSEL_SLOTS - 1].buffer = loaded;
    savestate_preview_cache[CAROUSEL_SLOTS - 1].valid = 1;
    strncpy(savestate_preview_cache[CAROUSEL_SLOTS - 1].name, name, MAX_FILE - 1);
    savestate_preview_cache[CAROUSEL_SLOTS - 1].name[MAX_FILE - 1] = '\0';
    return loaded;
}

/* Clear the savestate preview cache */
static void savestate_preview_clear(void)
{
    u32 i;
    for (i = 0; i < CAROUSEL_SLOTS; i++)
    {
        if (savestate_preview_cache[i].buffer)
            free(savestate_preview_cache[i].buffer);
        savestate_preview_cache[i].buffer = NULL;
        savestate_preview_cache[i].valid = 0;
        savestate_preview_cache[i].name[0] = '\0';
    }
}

void _flush_cache(void)
{
  invalidate_all_cache();
}


/* ------------------------------------------------------------------
   File browser
   ------------------------------------------------------------------ */

/* qsort comparator for the browser lists: ".." first, then by name. */
static int sort_function(const void *dest_str_ptr, const void *src_str_ptr)
{
  char *dest_str = *((char **)dest_str_ptr);
  char *src_str  = *((char **)src_str_ptr);

  if (src_str[0] == '.')
    return 1;

  if (dest_str[0] == '.')
    return -1;

  if ((isalpha((int)src_str[0]) != 0) || (isalpha((int)dest_str[0]) != 0))
    return strcasecmp(dest_str, src_str);

  return strcmp(dest_str, src_str);
}

/* Modal file browser for files ending in one of the NULL-terminated
   wildcards. Starts in default_dir_name (or the current directory) and
   chdir()s as the user navigates. On success copies the bare file name to
   result, leaves the current directory set to its folder and returns 0;
   returns -1 if the user backs out. show_recent adds the recent ROMs list.
   SELECT toggles between the list view and the carousel view. */
s32 load_file(const char **wildcards, char *result, char *default_dir_name, u32 show_recent)
{
  char current_dir_name[MAX_PATH];
  char current_dir_short[81];
  u32  current_dir_length;

  SceUID current_dir;
  SceIoDirent current_file;

  u32 total_file_names_allocated;
  u32 total_dir_names_allocated;
  char **file_list;
  char **dir_list;
  char *ext_pos;

  #define FILE_LIST (0)
  #define DIR_LIST  (1)

  /* Per-column state, indexed by FILE_LIST / DIR_LIST: entry count, selected
     entry, first visible entry, and the cursor's row inside the window. */
  u32 column;
  u32 num[2];
  u32 selection[2];
  u32 scroll_value[2];
  u32 cursor_row[2];

  u32 current_file_number, current_dir_number;
  u16 current_line_color;

  u32 i;
  s32 return_value = 1;
  s32 repeat;

  /* Recent ROMs browser state */
  u32 in_recent_section = 0;
  u32 recent_selection = 0;
  u32 file_list_y_offset = 0;

  GUI_ACTION_TYPE gui_action;

  char time_str[40];
  char batt_str[40];
  u16 color_batt_life = color_batt_normal;
  u32 counter = 0;

  char current_boxart_rom[MAX_FILE];
  current_boxart_rom[0] = '\0';

  /* ------------------------------------------------------------------
     Carousel mode state
     ------------------------------------------------------------------ */
  u32 carousel_mode = 0;        /* 0 = list view, 1 = carousel view */
  u32 prev_select_state = 0;    /* For edge-detecting SELECT button */
  u32 carousel_sel = 0;         /* Unified selection: dirs first, then files */
  u32 total_items = 0;          /* num_dirs + num_files */
  u32 browsing_savestates = 0;  /* 1 = wildcards are .svs, use savestate screenshots */

  auto void filelist_term(void);
  auto void malloc_error(void);


  void filelist_term(void)
  {
    for (i = 0; i < num[FILE_LIST]; i++)
    {
      free(file_list[i]);
    }

    free(file_list);

    for (i = 0; i < num[DIR_LIST]; i++)
    {
      free(dir_list[i]);
    }

    free(dir_list);
  }

  void malloc_error(void)
  {
    clear_screen(COLOR32_BLACK);
    error_msg(MSG[MSG_ERR_MALLOC], CONFIRMATION_QUIT);
    quit();
  }

  #define CHECK_MEM_ALLOCATE(mem_block)   \
  {                                       \
    if (mem_block == NULL)                \
      malloc_error();                     \
  }                                       \


  if (default_dir_name != NULL)
  {
    chdir(default_dir_name);
  }

  /* Detect if we're browsing savestate files (.svs) */
  if (wildcards[0] != NULL && strcasecmp(wildcards[0], ".svs") == 0)
    browsing_savestates = 1;

  /* Load recent ROMs list and validate entries */
  if (show_recent)
    load_recent_roms();
  else
    num_recent_roms = 0;

  /* File browser is not the main menu; disable OS exit dialog here */
  sceImposeSetHomePopup(0);

  /* Outer loop: one pass per directory. Read and sort its entries, then run
     the input loop until the user picks a file or changes directory. */
  while (return_value == 1)
  {
    column = FILE_LIST;

    selection[FILE_LIST]    = 0;
    scroll_value[FILE_LIST] = 0;
    cursor_row[FILE_LIST]   = 0;

    selection[DIR_LIST]     = 0;
    scroll_value[DIR_LIST]  = 0;
    cursor_row[DIR_LIST]    = 0;

    memset(&current_file, 0, sizeof(current_file));

    total_file_names_allocated = 32;
    total_dir_names_allocated  = 32;

    file_list = (char **)safe_malloc(sizeof(char *) * total_file_names_allocated);
    dir_list  = (char **)safe_malloc(sizeof(char *) * total_dir_names_allocated);

    memset(file_list, 0, sizeof(char *) * total_file_names_allocated);
    memset(dir_list,  0, sizeof(char *) * total_dir_names_allocated);

    num[FILE_LIST] = 0;
    num[DIR_LIST]  = 0;

    getcwd(current_dir_name, MAX_PATH);
    {
      size_t dir_len = strlen(current_dir_name);
      if (dir_len > 0 && current_dir_name[dir_len - 1] != '/' &&
          dir_len + 1 < MAX_PATH)
      {
        current_dir_name[dir_len] = '/';
        current_dir_name[dir_len + 1] = '\0';
      }
    }

    /* A device root such as "ms0:/" has nothing above it, so no "..". */
    {
      char *root_check = strstr(current_dir_name, ":/");
      if (root_check == NULL || strlen(root_check) != 2)
      {
        dir_list[num[DIR_LIST]] = (char *)safe_malloc(strlen("..") + 1);
        sprintf(dir_list[num[DIR_LIST]], "%s", "..");
        num[DIR_LIST]++;
      }
    }

    scePowerLock(0);
    current_dir = sceIoDopen(current_dir_name);

    if (current_dir >= 0)
    {
    while (sceIoDread(current_dir, &current_file) > 0)
    {
      if (current_file.d_name[0] == '.')
        continue;

      if (FIO_S_ISDIR(current_file.d_stat.st_mode) != 0)
      {
        dir_list[num[DIR_LIST]] = (char *)safe_malloc(strlen(current_file.d_name) + 1);

        sprintf(dir_list[num[DIR_LIST]], "%s", current_file.d_name);
        num[DIR_LIST]++;
      }
      else
      {
        if ((ext_pos = strrchr(current_file.d_name, '.')) != NULL)
        {
          for (i = 0; wildcards[i] != NULL; i++)
          {
            if (strcasecmp(ext_pos, wildcards[i]) == 0)
            {
              file_list[num[FILE_LIST]] = (char *)safe_malloc(strlen(current_file.d_name) + 1);

              sprintf(file_list[num[FILE_LIST]], "%s", current_file.d_name);
              num[FILE_LIST]++;
              break;
            }
          }
        }
      }

      if (num[FILE_LIST] == total_file_names_allocated)
      {
        file_list = (char **)realloc(file_list, sizeof(char *) * (total_file_names_allocated << 1));
        CHECK_MEM_ALLOCATE(file_list);
        memset(file_list + total_file_names_allocated, 0, sizeof(char *) * total_file_names_allocated);

        total_file_names_allocated <<= 1;
      }

      if (num[DIR_LIST] == total_dir_names_allocated)
      {
        dir_list = (char **)realloc(dir_list, sizeof(char *) * (total_dir_names_allocated << 1));
        CHECK_MEM_ALLOCATE(dir_list);
        memset(dir_list + total_dir_names_allocated, 0, sizeof(char *) * total_dir_names_allocated);

        total_dir_names_allocated <<= 1;
      }

    } /* end of while */

    sceIoDclose(current_dir);
    } /* end of valid sceIoDopen */
    scePowerUnlock(0);

    qsort((void *)file_list, num[FILE_LIST], sizeof(char *), sort_function);
    qsort((void *)dir_list,  num[DIR_LIST],  sizeof(char *), sort_function);


    current_dir_length = strlen(current_dir_name);

    if (current_dir_length > DIR_NAME_LENGTH)
    {
      memcpy(current_dir_short, "...", 3);
      memcpy(current_dir_short + 3, current_dir_name + (current_dir_length - (DIR_NAME_LENGTH - 3)), DIR_NAME_LENGTH - 3);
      current_dir_short[DIR_NAME_LENGTH] = 0;
    }
    else
    {
      memcpy(current_dir_short, current_dir_name, current_dir_length + 1);
    }


    repeat = 1;

    if (num[FILE_LIST] == 0)
    {
      column = DIR_LIST;
    }

    /* When entering a directory in carousel mode, land on first real item */
    if (carousel_mode)
    {
      total_items = num[DIR_LIST] + num[FILE_LIST];
      /* At root there is no "..", so start at index 0.
         In subdirectories, index 0 is "..", so start at index 1. */
      if (total_items > 0)
      {
        char *root_check = strstr(current_dir_name, ":/");
        u32 at_root = (root_check != NULL && strlen(root_check) == 2);
        carousel_sel = at_root ? 0 : ((total_items > 1) ? 1 : 0);
      }
      else
      {
        carousel_sel = 0;
      }
    }

    while (repeat)
    {
      clear_screen(COLOR15_TO_32(color_bg));

      /* Update file browser scroll state */
      {
          u32 file_hash = (column << 24) ^ selection[column];
          update_file_scroll(file_hash);
      }

      if (option_language == LANGUAGE_JAPANESE)
        print_string(current_dir_short, 6, 2, color_help_text, BG_NO_FILL);
      else
        print_string_gbk(current_dir_short, 6, 2, color_help_text, BG_NO_FILL);
      if ((counter % 30) == 0)
      {
        update_status_string_gbk(time_str, batt_str, &color_batt_life);
      }
      counter++;
      print_string_gbk(time_str, TIME_STATUS_POS_X, 2, color_help_text, BG_NO_FILL);

      print_string_gbk(batt_str, BATT_STATUS_POS_X, 2, color_batt_life, BG_NO_FILL);

      /* At root, Square/Back does nothing — show simplified hint. */
      {
        char *root_check = strstr(current_dir_name, ":/");
        if (root_check != NULL && strlen(root_check) == 2)
          print_swap_aware(MSG[MSG_BROWSER_HELP_ROOT], 30, 258,
           color_help_text, BG_NO_FILL);
        else
          print_swap_aware(MSG[MSG_BROWSER_HELP], 30, 258,
           color_help_text, BG_NO_FILL);
      }
      // PSP controller - hold
      if (get_pad_input(PSP_CTRL_HOLD) != 0)
		{
		print_string_gbk(FONT_KEY_ICON_GBK, 6, 258, color_batt_low, BG_NO_FILL);
		}
      /* Compute visible rows before drawing scroll bar */
      u32 file_list_visible_rows = FILE_LIST_ROWS;
      if (!is_recent_roms_empty())
      {
          file_list_visible_rows = FILE_LIST_ROWS - (num_recent_roms + 2);
          if (file_list_visible_rows < 5)
              file_list_visible_rows = 5;
      }

      // draw scroll bar (hidden in carousel mode)
      if (!carousel_mode && num[FILE_LIST] > file_list_visible_rows)
      {
        u32 frame_top, frame_bottom, track_top, track_bottom, track_height;
        u32 thumb_top, thumb_bottom;

        if (is_recent_roms_empty())
        {
          frame_top    = SBAR_Y1;
          frame_bottom = SBAR_Y2;
        }
        else
        {
          frame_top    = FILE_LIST_POS_Y + file_list_y_offset;
          frame_bottom = frame_top + file_list_visible_rows * FONTHEIGHT;
        }

        /* The thumb spans the visible rows' share of the track height. */
        track_top    = frame_top + 2;
        track_bottom = frame_bottom - 2;
        track_height = track_bottom - track_top;
        thumb_top    = track_height * scroll_value[FILE_LIST] / num[FILE_LIST] + track_top;
        thumb_bottom = track_height * (scroll_value[FILE_LIST] + file_list_visible_rows) / num[FILE_LIST] + track_top;

        draw_box_line(SBAR_X1,  frame_top, SBAR_X2,  frame_bottom, color_scroll_bar);
        draw_box_fill(SBAR_X1I, thumb_top, SBAR_X2I, thumb_bottom, color_scroll_bar);
      }

      /* ================================================================
         CAROUSEL: 5-slot horizontal cover flow
         ================================================================ */
      if (carousel_mode)
      {
        /* Unified index: 0..num_dirs-1 = dirs, num_dirs..end = files */
        total_items = num[DIR_LIST] + num[FILE_LIST];
        u32 sel = carousel_sel;

        /* Symmetric layout: mirror left/right around screen center (240)
         * Draw order (z-index, back to front): outerL, outerR, innerL, innerR, center */
        const u32 slot_draw_order[CAROUSEL_SLOTS] = { 0, 4, 1, 3, 2 };
        const u32 slot_x[CAROUSEL_SLOTS] = {
          CAROUSEL_X_OUTER_L,
          CAROUSEL_X_INNER_L,
          CAROUSEL_X_CENTER,
          CAROUSEL_X_INNER_R,
          CAROUSEL_X_OUTER_R
        };
        const u32 slot_y[CAROUSEL_SLOTS] = {
          CAROUSEL_Y_OUTER_L,
          CAROUSEL_Y_INNER_L,
          CAROUSEL_Y_CENTER,
          CAROUSEL_Y_INNER_R,
          CAROUSEL_Y_OUTER_R
        };
        const u32 slot_w[CAROUSEL_SLOTS] = {
          CAROUSEL_W_OUTER, CAROUSEL_W_INNER, CAROUSEL_W_CENTER,
          CAROUSEL_W_INNER, CAROUSEL_W_OUTER
        };
        const u32 slot_h[CAROUSEL_SLOTS] = {
          CAROUSEL_H_OUTER, CAROUSEL_H_INNER, CAROUSEL_H_CENTER,
          CAROUSEL_H_INNER, CAROUSEL_H_OUTER
        };
        const u8 slot_bright[CAROUSEL_SLOTS] = {
          CAROUSEL_BRIGHT_OUTER, CAROUSEL_BRIGHT_INNER, CAROUSEL_BRIGHT_CENTER,
          CAROUSEL_BRIGHT_INNER, CAROUSEL_BRIGHT_OUTER
        };

        /* Draw slots back-to-front for proper depth (outer → inner → center) */
        for (i = 0; i < CAROUSEL_SLOTS; i++)
        {
          u32 slot = slot_draw_order[i];
          s32 idx = (s32)sel + (s32)slot - (s32)CAROUSEL_CENTER;

          if (total_items > 0)
          {
            if (total_items >= CAROUSEL_SLOTS)
            {
              /* 5+ items: infinite wrap */
              idx = ((idx % (s32)total_items) + (s32)total_items) % (s32)total_items;
            }
            else
            {
              /* < 5 items: no wrap, only draw valid indices */
              if (idx < 0 || idx >= (s32)total_items)
                continue;
            }
          }
          else
          {
            idx = -1; /* nothing to draw */
          }

          if (idx >= 0 && total_items > 0)
          {
            u32 is_dir = (idx < (s32)num[DIR_LIST]);
            const char *name = is_dir ? dir_list[idx] : file_list[idx - num[DIR_LIST]];
            u8 bright = slot_bright[slot];
            u16 border_color = (slot == CAROUSEL_CENTER) ? color_active_item
                                                         : dim_color15(color_inactive_item, bright);

            if (is_dir)
            {
              /* Folder: draw frame first, then optional inset PNG */
              u16 folder_fill = dim_color15(color_inactive_dir, bright);
              boxart_carousel_draw_folder_frame(slot_x[slot], slot_y[slot],
                                                slot_w[slot], slot_h[slot],
                                                folder_fill, border_color);

              /* Try to load custom folder art (skipped automatically for ".." or missing dir) */
              u16 *folder_buf = boxart_carousel_get(name, 1);
              if (folder_buf)
              {
                /* Folder body is 75% of slot height, bottom-aligned.
                   Icon must be inset inside the actual folder body, below the tab. */
                u16 folder_h = (slot_h[slot] * 3) / 4;
                u16 folder_y = slot_y[slot] + slot_h[slot] - folder_h;
                u16 tab_h = folder_h / 5;
                u16 inset_x = slot_x[slot] + 2;
                u16 inset_y = folder_y + tab_h + 2;
                u16 inset_w = (slot_w[slot] > 4) ? slot_w[slot] - 4 : 1;
                u16 inset_h = (folder_h > tab_h + 4) ? folder_h - tab_h - 4 : 1;
                boxart_carousel_draw(folder_buf, inset_x, inset_y, inset_w, inset_h, bright);
              }

            }
            else
            {
              /* ROM / savestate: preview or "NO IMG" placeholder */
              u16 *buf = NULL;

              if (browsing_savestates)
                buf = savestate_preview_get(name);
              else
                buf = boxart_carousel_get(name, 0);

              if (buf)
              {
                boxart_carousel_draw(buf, slot_x[slot], slot_y[slot], slot_w[slot], slot_h[slot], bright);
              }
              else
              {
                u16 noimg_base = color_scroll_bar;
                u32 tex_i;

                /* Rebuild texture if theme color changed */
                if (noimg_texture_color != noimg_base)
                {
                  for (tex_i = 0; tex_i < BOXART_W * BOXART_H; tex_i++)
                    noimg_texture[tex_i] = noimg_base;
                  noimg_texture_color = noimg_base;
                }

                /* Draw through GE pipeline — same as image slots */
                boxart_carousel_draw(noimg_texture, slot_x[slot], slot_y[slot],
                                     slot_w[slot], slot_h[slot], bright);

                /* Text label */
                u16 noimg_text = dim_color15(color_inactive_item, bright);
                const char *ph = "NO IMG";
                u32 ph_x = slot_x[slot] + (slot_w[slot] - (strlen(ph) * FONTWIDTH)) / 2;
                u32 ph_y = slot_y[slot] + (slot_h[slot] - FONTHEIGHT) / 2;
                print_string(ph, ph_x, ph_y, noimg_text, BG_NO_FILL);
              }

              /* Border around center slot only — non-center slots must not
                 draw outside their bounds to avoid flicker on real PSP HW */
              if (slot == CAROUSEL_CENTER)
              {
                draw_box_line(slot_x[slot] - 1, slot_y[slot] - 1,
                              slot_x[slot] + slot_w[slot], slot_y[slot] + slot_h[slot], border_color);
              }
            }
          }
        }

        /* Selected item name centered below center slot */
        if (total_items > 0 && sel < total_items)
        {
          const char *sel_name = (sel < num[DIR_LIST]) ? dir_list[sel] : file_list[sel - num[DIR_LIST]];
          u32 name_y = CAROUSEL_Y_CENTER + CAROUSEL_H_CENTER + 10;
          print_string(sel_name, X_POS_CENTER, name_y, color_active_item, BG_NO_FILL);
        }
      }
      else
      {
      /* --- Recent Games Section --- */
      file_list_y_offset = 0;
      if (!is_recent_roms_empty())
      {
        u32 recent_y = FILE_LIST_POS_Y;
        u16 header_color = color_help_text;

        /* Header */
        print_string_gbk(MSG[MSG_BROWSER_RECENT_GAMES], FILE_LIST_POS_X, recent_y, header_color, BG_NO_FILL);
        recent_y += FONTHEIGHT;

        /* Recent ROM entries */
        for (i = 0; i < num_recent_roms; i++)
        {
          const char *rom_name = strrchr(recent_roms[i].path, '/');
          if (rom_name == NULL)
            rom_name = recent_roms[i].path;
          else
            rom_name++;

          u16 line_color;
          if (in_recent_section && i == recent_selection)
          {
            line_color = color_active_item;
            s32 off = compute_scroll_offset(g_scroll.file_tick, strlen(rom_name), SCROLL_FILE_MAX_CHARS);
            print_string_scroll(rom_name, FILE_LIST_POS_X, recent_y, line_color, BG_NO_FILL, SCROLL_FILE_MAX_CHARS, off);
          }
          else
          {
            line_color = color_inactive_item;
            print_string_clipped(rom_name, FILE_LIST_POS_X, recent_y, line_color, BG_NO_FILL, SCROLL_FILE_MAX_CHARS);
          }
          recent_y += FONTHEIGHT;
        }

        /* Separator */
        print_string_gbk(MSG[MSG_BROWSER_ALL_GAMES], FILE_LIST_POS_X, recent_y, header_color, BG_NO_FILL);

        file_list_y_offset = (num_recent_roms + 2) * FONTHEIGHT;
      }

        /* ================================================================
           LIST VIEW: original file browser rendering
           ================================================================ */
      for (i = 0; i < file_list_visible_rows; i++)
      {
        current_file_number = i + scroll_value[FILE_LIST];

        if (current_file_number < num[FILE_LIST])
        {
          u32 is_selected = ((current_file_number == selection[FILE_LIST]) && (column == FILE_LIST) && !in_recent_section);
          if (is_selected)
            current_line_color = color_active_item;
          else
            current_line_color = color_inactive_item;

          if (is_selected) {
              s32 off = compute_scroll_offset(g_scroll.file_tick, strlen(file_list[current_file_number]), SCROLL_FILE_MAX_CHARS);
              print_string_scroll(file_list[current_file_number], FILE_LIST_POS_X, FILE_LIST_POS_Y + file_list_y_offset + (i * FONTHEIGHT), current_line_color, BG_NO_FILL, SCROLL_FILE_MAX_CHARS, off);
          } else {
              print_string_clipped(file_list[current_file_number], FILE_LIST_POS_X, FILE_LIST_POS_Y + file_list_y_offset + (i * FONTHEIGHT), current_line_color, BG_NO_FILL, SCROLL_FILE_MAX_CHARS);
          }
        }
      }
      for (i = 0; i < FILE_LIST_ROWS; i++)
      {
        current_dir_number = i + scroll_value[DIR_LIST];

        if (current_dir_number < num[DIR_LIST])
        {
          u32 is_selected = ((current_dir_number == selection[DIR_LIST]) && (column == DIR_LIST));
          if (is_selected)
            current_line_color = color_active_item;
          else
            current_line_color = color_inactive_dir;

          if (is_selected) {
              s32 off = compute_scroll_offset(g_scroll.file_tick, strlen(dir_list[current_dir_number]), SCROLL_DIR_MAX_CHARS);
              print_string_scroll(dir_list[current_dir_number], DIR_LIST_POS_X, FILE_LIST_POS_Y + (i * FONTHEIGHT), current_line_color, color_bg, SCROLL_DIR_MAX_CHARS, off);
          } else {
              print_string_clipped(dir_list[current_dir_number], DIR_LIST_POS_X, FILE_LIST_POS_Y + (i * FONTHEIGHT), current_line_color, color_bg, SCROLL_DIR_MAX_CHARS);
          }
        }
      }
      if (num[DIR_LIST] > FILE_LIST_ROWS)
      {
        if (scroll_value[DIR_LIST] != 0)
          print_string(FONT_CURSOR_UP_FILL, PSP_SCREEN_WIDTH - (FONTWIDTH * 2), FILE_LIST_POS_Y, color_scroll_bar, color_bg);

        if (num[DIR_LIST] > (scroll_value[DIR_LIST] + FILE_LIST_ROWS))
          print_string(FONT_CURSOR_DOWN_FILL, PSP_SCREEN_WIDTH - (FONTWIDTH * 2), FILE_LIST_POS_Y + ((FILE_LIST_ROWS - 1) * FONTHEIGHT), color_scroll_bar, color_bg);
      }


        /* Draw boxart or savestate preview for selected item (list view only).
           +108/+70 puts the 141x141 image in the bottom-right corner of
           the screen. */
        if (browsing_savestates && column == FILE_LIST && num[FILE_LIST] > 0)
        {
          u16 *svs_buf = savestate_preview_get(file_list[selection[FILE_LIST]]);
          if (svs_buf)
          {
            blit_to_screen(svs_buf, BOXART_W, BOXART_H, SCREEN_IMAGE_POS_X+108, SCREEN_IMAGE_POS_Y+70);
            draw_box_line(SCREEN_IMAGE_POS_X+108 - 1, SCREEN_IMAGE_POS_Y+70 - 1,
                          SCREEN_IMAGE_POS_X+108 + BOXART_W, SCREEN_IMAGE_POS_Y+70 + BOXART_H,
                          color_inactive_item);
          }
        }
        else
        {
          boxart_draw(SCREEN_IMAGE_POS_X+108, SCREEN_IMAGE_POS_Y+70, color_inactive_item);
        }
      }

      __draw_volume_status(1);
      flip_screen(1);

      /* CAROUSEL: SELECT button toggles between list and carousel views */
      {
        u32 curr_select_state = get_pad_input(PSP_CTRL_SELECT);
        if (curr_select_state && !prev_select_state)
        {
          if (!carousel_mode)
          {
            /* List → Carousel: translate current selection to unified index */
            if (column == FILE_LIST)
              carousel_sel = num[DIR_LIST] + selection[FILE_LIST];
            else
              carousel_sel = selection[DIR_LIST];
            carousel_mode = 1;
          }
          else
          {
            /* Carousel → List: land on top of file list if files exist, else dir list */
            carousel_mode = 0;
            if (num[FILE_LIST] > 0)
            {
              column = FILE_LIST;
              selection[FILE_LIST] = 0;
              scroll_value[FILE_LIST] = 0;
              cursor_row[FILE_LIST] = 0;
            }
            else
            {
              column = DIR_LIST;
              selection[DIR_LIST] = 0;
              scroll_value[DIR_LIST] = 0;
              cursor_row[DIR_LIST] = 0;
            }
          }
          /* Clear the old boxart state when switching modes */
          current_boxart_rom[0] = '\0';
          boxart_free();
          savestate_preview_clear();
          if (!carousel_mode)
            boxart_carousel_clear();
        }
        prev_select_state = curr_select_state;
      }

      gui_action = get_gui_input();

      /* Per-column visible rows: FILE_LIST shrinks when recent games shown */
      u32 visible_rows[2];
      visible_rows[FILE_LIST] = file_list_visible_rows;
      visible_rows[DIR_LIST]  = FILE_LIST_ROWS;

      switch (gui_action)
      {
        case CURSOR_DOWN:
          if (carousel_mode)
          {
            if (total_items > 0)
              carousel_sel = (carousel_sel + 1) % total_items;
          }
          else if (in_recent_section)
          {
            if (recent_selection < num_recent_roms - 1)
            {
              recent_selection++;
            }
            else
            {
              /* Move from recent section to file list */
              in_recent_section = 0;
              column = FILE_LIST;
              selection[FILE_LIST] = 0;
              scroll_value[FILE_LIST] = 0;
              cursor_row[FILE_LIST] = 0;
            }
          }
          else if (column == FILE_LIST)
          {
            if (selection[FILE_LIST] < (num[FILE_LIST] - 1))
            {
              selection[FILE_LIST]++;

              if (cursor_row[FILE_LIST] == (visible_rows[FILE_LIST] - 1))
                scroll_value[FILE_LIST]++;
              else
                cursor_row[FILE_LIST]++;
            }
          }
          else /* DIR_LIST */
          {
            if (selection[column] < (num[column] - 1))
            {
              selection[column]++;

              if (cursor_row[column] == (FILE_LIST_ROWS - 1))
                scroll_value[column]++;
              else
                cursor_row[column]++;
            }
          }
          break;

        case CURSOR_RTRIGGER:
          if (carousel_mode)
          {
            if (total_items > 0)
              carousel_sel = (carousel_sel + 3) % total_items;
          }
          else if (in_recent_section)
          {
            /* Ignore page-scroll in Recent ROMs section */
            break;
          }
          else if (num[column] > PAGE_SCROLL_NUM)
          {
            if (selection[column] < (num[column] - PAGE_SCROLL_NUM))
            {
              selection[column] += PAGE_SCROLL_NUM;

              if (cursor_row[column] >= (visible_rows[column] - PAGE_SCROLL_NUM))
              {
                scroll_value[column] += PAGE_SCROLL_NUM;

                if (scroll_value[column] > (num[column] - visible_rows[column]))
                {
                  scroll_value[column] = num[column] - visible_rows[column];
                  cursor_row[column] = selection[column] - scroll_value[column];
                }
              }
              else
              {
                cursor_row[column] += PAGE_SCROLL_NUM;
              }
            }
            else
            {
              selection[column] = num[column] - 1;
              cursor_row[column] += PAGE_SCROLL_NUM;

              if (cursor_row[column] >= (visible_rows[column] - 1))
              {
                if (num[column] > (visible_rows[column] - 1))
                {
                  cursor_row[column] = visible_rows[column] - 1;
                  scroll_value[column] = num[column] - visible_rows[column];
                }
                else
                {
                  cursor_row[column] = num[column] - 1;
                }
              }
            }
          }
          else
          {
            selection[column] = num[column] - 1;
            cursor_row[column] = num[column] - 1;
          }
          break;

        case CURSOR_UP:
          if (carousel_mode)
          {
            if (total_items > 0)
              carousel_sel = (carousel_sel + total_items - 1) % total_items;
          }
          else if (!in_recent_section && column == FILE_LIST && selection[FILE_LIST] == 0 && !is_recent_roms_empty())
          {
            /* Move from file list to recent section */
            in_recent_section = 1;
            recent_selection = num_recent_roms - 1;
          }
          else if (in_recent_section)
          {
            if (recent_selection > 0)
              recent_selection--;
          }
          else if (column == FILE_LIST)
          {
            if (selection[FILE_LIST] != 0)
            {
              selection[FILE_LIST]--;

              if (cursor_row[FILE_LIST] == 0)
                scroll_value[FILE_LIST]--;
              else
                cursor_row[FILE_LIST]--;
            }
          }
          else /* DIR_LIST */
          {
            if (selection[column] != 0)
            {
              selection[column]--;

              if (cursor_row[column] == 0)
                scroll_value[column]--;
              else
                cursor_row[column]--;
            }
          }
          break;

        case CURSOR_LTRIGGER:
          if (carousel_mode)
          {
            if (total_items > 0)
              carousel_sel = (carousel_sel + total_items - 3) % total_items;
          }
          else if (in_recent_section)
          {
            /* Ignore page-scroll in Recent ROMs section */
            break;
          }
          else if (selection[column] >= PAGE_SCROLL_NUM)
          {
            selection[column] -= PAGE_SCROLL_NUM;

            if (cursor_row[column] < PAGE_SCROLL_NUM)
            {
              if (scroll_value[column] >= PAGE_SCROLL_NUM)
              {
                scroll_value[column] -= PAGE_SCROLL_NUM;
              }
              else
              {
                scroll_value[column] = 0;
                cursor_row[column] = selection[column];
              }
            }
            else
            {
              cursor_row[column] -= PAGE_SCROLL_NUM;
            }
          }
          else
          {
            selection[column] = 0;
            cursor_row[column] = 0;
            scroll_value[column] = 0;
          }
          break;

        case CURSOR_RIGHT:
          if (carousel_mode)
          {
            /* CAROUSEL: scroll right through unified strip (infinite wrap) */
            if (total_items > 0)
              carousel_sel = (carousel_sel + 1) % total_items;
          }
          else if (column == FILE_LIST)
          {
            if (num[DIR_LIST] != 0)
            {
              column = DIR_LIST;
              in_recent_section = 0;
              /* Clamp directory scroll to new full-height view */
              if (scroll_value[DIR_LIST] > num[DIR_LIST] - 20)
                scroll_value[DIR_LIST] = (num[DIR_LIST] > 20) ? num[DIR_LIST] - 20 : 0;
            }
          }
          break;

        case CURSOR_LEFT:
          if (carousel_mode)
          {
            /* CAROUSEL: scroll left through unified strip (infinite wrap) */
            if (total_items > 0)
              carousel_sel = (carousel_sel + total_items - 1) % total_items;
          }
          else if (column == DIR_LIST)
          {
            /* Allow LEFT if there are files OR recent games to go back to */
            if (num[FILE_LIST] != 0 || !is_recent_roms_empty())
            {
              column = FILE_LIST;

              /* If recent games exist, land in the recent section on the FIRST item */
              if (!is_recent_roms_empty())
              {
                in_recent_section = 1;
                recent_selection = 0;
              }
              else
              {
                in_recent_section = 0;
              }

              /* Clamp directory scroll to shrunk view when returning to files */
              if (scroll_value[DIR_LIST] > num[DIR_LIST] - 8)
                scroll_value[DIR_LIST] = (num[DIR_LIST] > 8) ? num[DIR_LIST] - 8 : 0;
              if (cursor_row[DIR_LIST] >= 8)
                cursor_row[DIR_LIST] = 7;
            }
          }
          break;

        case CURSOR_SELECT:
          if (carousel_mode)
          {
            if (carousel_sel < num[DIR_LIST])
            {
              /* Folder selected in carousel — chdir and stay in carousel */
              repeat = 0;
              current_boxart_rom[0] = '\0';
              chdir(dir_list[carousel_sel]);
            }
            else if (num[FILE_LIST] != 0)
            {
              /* ROM selected in carousel */
              repeat = 0;
              return_value = 0;
              strcpy(result, file_list[carousel_sel - num[DIR_LIST]]);
            }
          }
          else if (in_recent_section)
          {
            /* Load selected recent ROM */
            char *recent_path = recent_roms[recent_selection].path;
            char *last_slash = strrchr(recent_path, '/');

            if (last_slash != NULL)
            {
              size_t dir_len = last_slash - recent_path;
              char recent_dir[MAX_PATH];
              memcpy(recent_dir, recent_path, dir_len);
              recent_dir[dir_len] = '\0';
              chdir(recent_dir);
              strcpy(result, last_slash + 1);
            }
            else
            {
              strcpy(result, recent_path);
            }
            repeat = 0;
            return_value = 0;
          }
          else if (column == DIR_LIST)
          {
            repeat = 0;
            current_boxart_rom[0] = '\0';
            chdir(dir_list[selection[DIR_LIST]]);
          }
          else
          {
            if (num[FILE_LIST] != 0)
            {
              repeat = 0;
              return_value = 0;
              strcpy(result, file_list[selection[FILE_LIST]]);
            }
          }
          break;

        case CURSOR_BACK:
          // ROOT
          if (strlen(strstr(current_dir_name, ":/")) == 2)
            break;

          repeat = 0;
          current_boxart_rom[0] = '\0';
          chdir("..");
          break;

        case CURSOR_EXIT:
          return_value = -1;
          repeat = 0;
          break;

        case CURSOR_DEFAULT:
          break;

        case CURSOR_NONE:
          break;
      }

      /* Load boxart / savestate preview for currently selected item */
      if (carousel_mode)
      {
        /* CAROUSEL: pre-load all 5 visible slots into RAM cache */
        if (total_items > 0)
        {
          s32 offset;
          for (offset = -CAROUSEL_CENTER; offset <= CAROUSEL_CENTER; offset++)
          {
            s32 idx = (s32)carousel_sel + offset;
            if (total_items >= CAROUSEL_SLOTS)
            {
              idx = ((idx % (s32)total_items) + (s32)total_items) % (s32)total_items;
            }
            else
            {
              if (idx < 0 || idx >= (s32)total_items) continue;
            }
            if (idx < (s32)num[DIR_LIST])
            {
              if (strcmp(dir_list[idx], "..") != 0)
                boxart_carousel_get(dir_list[idx], 1);
            }
            else
            {
              const char *fname = file_list[idx - num[DIR_LIST]];
              if (browsing_savestates)
                savestate_preview_get(fname);
              else
                boxart_carousel_get(fname, 0);
            }
          }
        }
      }
      /* Recent ROMs section: load boxart for selected recent game */
      else if (in_recent_section && num_recent_roms > 0)
      {
        const char *recent_path = recent_roms[recent_selection].path;
        const char *rom_name = strrchr(recent_path, '/');
        if (rom_name != NULL)
          rom_name++;
        else
          rom_name = recent_path;

        if (strcmp(current_boxart_rom, rom_name) != 0)
        {
          strncpy(current_boxart_rom, rom_name, sizeof(current_boxart_rom) - 1);
          current_boxart_rom[sizeof(current_boxart_rom) - 1] = '\0';
          boxart_load(rom_name);
        }
      }
      else if (column == FILE_LIST && num[FILE_LIST] > 0)
      {
        const char *sel_file = file_list[selection[FILE_LIST]];
        if (strcmp(current_boxart_rom, sel_file) != 0)
        {
          strncpy(current_boxart_rom, sel_file, sizeof(current_boxart_rom) - 1);
          current_boxart_rom[sizeof(current_boxart_rom) - 1] = '\0';
          if (browsing_savestates)
          {
            boxart_free();
            /* Savestate preview is loaded on-demand in the draw block */
          }
          else
          {
            boxart_load(sel_file);
          }
        }
      }
      else
      {
        if (current_boxart_rom[0] != '\0')
        {
          current_boxart_rom[0] = '\0';
          boxart_free();
        }
      }
    } /* end while (repeat) */

    boxart_free();
    boxart_carousel_clear();
    savestate_preview_clear();
    filelist_term();

  } /* end while (return_value == 1) */

  sceImposeSetHomePopup(1);
  return return_value;
}


/* ------------------------------------------------------------------
   Savestate slots
   ------------------------------------------------------------------ */

/* Read a savestate's screenshot (if snapshot is not NULL) and format its
   save time into timestamp. A missing file gives a "no state" placeholder.
   The .svs file starts with the screenshot, followed by a u64 UTC tick. */
static void get_savestate_info(char *filename, u16 *snapshot, char *timestamp)
{
  SceUID savestate_file;
  char savestate_path[MAX_PATH];

  sprintf(savestate_path, "%s%s", dir_state, filename);

  scePowerLock(0);

  FILE_OPEN(savestate_file, savestate_path, READ);

  if (FILE_CHECK_VALID(savestate_file))
  {
    u64 savestate_tick_utc;
    u64 savestate_tick_local;

    ScePspDateTime savestate_time = { 0 };

    if (snapshot != NULL)
      FILE_READ(savestate_file, snapshot, GBA_SCREEN_SIZE);
    else
      FILE_SEEK(savestate_file, GBA_SCREEN_SIZE, SEEK_SET);

    FILE_READ_VARIABLE(savestate_file, savestate_tick_utc);

    FILE_CLOSE(savestate_file);

    sceRtcConvertUtcToLocalTime(&savestate_tick_utc, &savestate_tick_local);
    sceRtcSetTick(&savestate_time, &savestate_tick_local);
    int day_of_week = sceRtcGetDayOfWeek(savestate_time.year, savestate_time.month, savestate_time.day);

    get_timestamp_string(timestamp, MSG_STATE_MENU_DATE_FMT_0, &savestate_time, day_of_week);
  }
  else
  {
    if (snapshot != NULL)
    {
      memset(snapshot, 0, GBA_SCREEN_SIZE);
		print_string_ext_gbk(MSG[MSG_STATE_MENU_STATE_NONE], X_POS_CENTER, 74, COLOR15_WHITE, BG_NO_FILL, snapshot, GBA_SCREEN_WIDTH);
    }

    sprintf(timestamp, "%s", MSG[(date_format == 0) ? MSG_STATE_MENU_DATE_NONE_0 : MSG_STATE_MENU_DATE_NONE_1]);
  }

  scePowerUnlock(0);
}

/* Slots 0-9 are the numbered savestates ("<rom>_N.svs"); the extra slot is
   the automatic one ("<rom>_auto.svs") written on exit, sleep, ROM change
   and before overwriting progress with a load. */
#define AUTO_SAVESTATE_SLOT   10
#define SAVESTATE_SLOT_COUNT  11

/* Savestate file name (no directory) for a slot of the running ROM. */
static void get_savestate_filename(u32 slot, char *name_buffer)
{
  if (slot == AUTO_SAVESTATE_SLOT)
    change_ext(gamepak_filename, name_buffer, "_auto.svs");
  else
  {
    char savestate_ext[16];
    sprintf(savestate_ext, "_%d.svs", (int)slot);
    change_ext(gamepak_filename, name_buffer, savestate_ext);
  }
}

void auto_savestate_sleep(void)
{
    if (gamepak_filename[0] == '\0')
        return;
    action_savestate_slot(AUTO_SAVESTATE_SLOT);
}

static u32 savestate_file_exists(u32 slot)
{
    char savestate_filename[MAX_FILE];
    char savestate_path[MAX_PATH];
    SceIoStat stat;

    get_savestate_filename(slot, savestate_filename);
    snprintf(savestate_path, sizeof(savestate_path), "%s%s", dir_state, savestate_filename);
    return (sceIoGetstat(savestate_path, &stat) >= 0);
}


u32 action_loadstate(void)
{
  char savestate_filename[MAX_FILE];

  get_savestate_filename(savestate_slot, savestate_filename);
  return load_state(savestate_filename);
}

u32 action_savestate(void)
{
  char savestate_filename[MAX_FILE];
  u16 *current_screen;
  u32 result;

  current_screen = copy_screen();

  get_savestate_filename(savestate_slot, savestate_filename);
  result = save_state(savestate_filename, current_screen);

  free(current_screen);
  return result;
}

/* Save/load to a specific slot without disturbing the global savestate_slot */
static u32 action_savestate_slot(u32 slot)
{
  char savestate_filename[MAX_FILE];
  u16 *current_screen;
  u32 result;
  u32 old_slot = savestate_slot;

  savestate_slot = slot;
  current_screen = copy_screen();
  get_savestate_filename(savestate_slot, savestate_filename);
  result = save_state_silent(savestate_filename, current_screen);
  free(current_screen);
  savestate_slot = old_slot;
  return result;
}

static u32 action_loadstate_slot(u32 slot)
{
  char savestate_filename[MAX_FILE];
  u32 result;
  u32 old_slot = savestate_slot;

  savestate_slot = slot;
  get_savestate_filename(savestate_slot, savestate_filename);
  result = load_state_silent(savestate_filename);
  savestate_slot = old_slot;
  return result;
}

static u32 auto_savestate_exists(void)
{
  char savestate_filename[MAX_FILE];
  char savestate_path[MAX_PATH];
  SceUID fd;
  u32 old_slot = savestate_slot;

  savestate_slot = AUTO_SAVESTATE_SLOT;
  get_savestate_filename(AUTO_SAVESTATE_SLOT, savestate_filename);
  snprintf(savestate_path, sizeof(savestate_path), "%s%s", dir_state, savestate_filename);
  FILE_OPEN(fd, savestate_path, READ);
  savestate_slot = old_slot;

  if (FILE_CHECK_VALID(fd))
  {
    FILE_CLOSE(fd);
    return 1;
  }
  return 0;
}

/* Last RAM dynarec policy seen by the menu; ~0 means "not sampled yet". */
static u32 ram_dynarec_policy_menu_prev = ~(u32)0;

/* After a ROM loads, offer to resume from its auto savestate. The slot is
   spelled out because tests/test_startup.py compiles this function alone. */
void maybe_load_auto_savestate(void)
{
  if (gamepak_filename[0] == '\0' || main_link_active() ||
      !auto_savestate_exists())
    return;

  if (yesno_dialog(MSG[MSG_AUTO_SAVESTATE_LOAD_PROMPT]) == 0)
    action_loadstate_slot(10);
}

/* Translated RAM code was built under the old policy, so a change flushes
   the writable-region translation cache. */
static void menu_passive_ram_dynarec_policy(void)
{
  if (ram_dynarec_policy_menu_prev == ~(u32)0)
  {
    ram_dynarec_policy_menu_prev = option_ram_dynarec_policy;
    return;
  }

  if (option_ram_dynarec_policy != ram_dynarec_policy_menu_prev)
  {
    ram_dynarec_policy_menu_prev = option_ram_dynarec_policy;
    flush_translation_cache(TRANSLATION_REGION_WRITABLE, FLUSH_REASON_INITIALIZING);
  }
}


/* ------------------------------------------------------------------
   Cheat menu page refresh -- file scope to avoid GCC nested-function
   trampoline crash when used as a menu passive_function callback.
   ------------------------------------------------------------------ */
static char cheat_format_str[MAX_CHEATS][CHEAT_NAME_LENGTH + 16];
static char cheat_page_format_str[64];
static u32 inactive_cheat_dummy = 0;
static MenuOptionType *cheats_misc_options_ptr = NULL;

static void menu_passive_cheat_toggle(void)
{
    rebuild_active_cheat_indexes();
}

static void refresh_cheat_format_strings(void)
{
    u32 i;

    for (i = 0; i < num_cheats; i++)
        snprintf(cheat_format_str[i], sizeof(cheat_format_str[i]),
                 MSG[MSG_CHEAT_MENU_0], (int)(i + 1),
                 cheats[i].cheat_name);
}

/* Point option rows 0-9 of the cheat menu at the cheats on the current page
   (ten per page) and refresh the page selector in row 10. */
static void reload_cheats_page(void)
{
    u32 i;
    u32 page_count = (num_cheats + 9) / 10;
    const char *page_format = MSG[MSG_CHEAT_MENU_3];
    const char *page_value = strstr(page_format, "%d");

    if (page_count == 0)
        page_count = 1;
    if (menu_cheat_page >= page_count)
        menu_cheat_page = page_count - 1;

    for (i = 0; i < 10; i++)
    {
        u32 idx = (10 * menu_cheat_page) + i;

        if (idx < num_cheats)
        {
            cheats_misc_options_ptr[i].display_string = cheat_format_str[idx];
            cheats_misc_options_ptr[i].options = enable_disable_options;
            cheats_misc_options_ptr[i].current_option =
                &(cheats[idx].cheat_active);
            cheats_misc_options_ptr[i].num_options = 2;
            cheats_misc_options_ptr[i].passive_function =
                menu_passive_cheat_toggle;
            cheats_misc_options_ptr[i].option_type = STRING_SELECTION_OPTION;
        }
        else
        {
            cheats_misc_options_ptr[i].display_string =
                (num_cheats == 0 && i == 0) ?
                MSG[MSG_CHEAT_MENU_NON_LOAD] : "";
            cheats_misc_options_ptr[i].options = NULL;
            cheats_misc_options_ptr[i].current_option = &inactive_cheat_dummy;
            cheats_misc_options_ptr[i].num_options = 0;
            cheats_misc_options_ptr[i].passive_function = NULL;
            cheats_misc_options_ptr[i].option_type = ACTION_OPTION;
        }
    }

    if (page_value != NULL)
        snprintf(cheat_page_format_str, sizeof(cheat_page_format_str),
                 "%.*s%lu/%lu", (int)(page_value - page_format), page_format,
                 (unsigned long)(menu_cheat_page + 1),
                 (unsigned long)page_count);
    else
        snprintf(cheat_page_format_str, sizeof(cheat_page_format_str),
                 "%s %lu/%lu", page_format,
                 (unsigned long)(menu_cheat_page + 1),
                 (unsigned long)page_count);

    cheats_misc_options_ptr[10].display_string = cheat_page_format_str;
    cheats_misc_options_ptr[10].num_options = page_count;
}

/* Global array of all menus — filled on each menu() call (the tables live
   in its stack frame, so it is only valid while menu() runs), then used by
   file-scope refresh to avoid nested functions. */
#define MAX_MENUS 16
static MenuType *all_menus[MAX_MENUS];
static u32 num_all_menus = 0;
static char diagnostics_status_line[96];
static char diagnostics_report_line[96];
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
static char diagnostics_trace_status_line[96];
#endif

/* Menu row callbacks: *_available() enables a row, *_visible() shows it,
   the rest are actions. */
static int menu_game_available(void)
{
    return gamepak_filename[0] != '\0';
}

static int menu_diagnostics_start_available(void)
{
    PerfMetricsSnapshot snapshot;

    main_perf_snapshot(&snapshot);
    return menu_game_available() && snapshot.state != PERF_METRICS_ACTIVE;
}

static int menu_diagnostics_stop_available(void)
{
    PerfMetricsSnapshot snapshot;

    main_perf_snapshot(&snapshot);
    return snapshot.state == PERF_METRICS_ACTIVE;
}

static int menu_diagnostics_report_available(void)
{
    PerfMetricsSnapshot snapshot;

    main_perf_snapshot(&snapshot);
    return snapshot.state == PERF_METRICS_STOPPED ||
           snapshot.state == PERF_METRICS_INVALIDATED;
}

static int menu_diagnostics_report_visible(void)
{
    return diagnostics_report_line[0] != '\0';
}

static void menu_diagnostics_start(void)
{
    diagnostics_report_line[0] = '\0';
    main_perf_start();
}

static void menu_diagnostics_stop(void)
{
    main_perf_stop();
}

static void menu_diagnostics_write_report(void)
{
    char report_path[MAX_PATH];
    const char *filename;

    if(main_perf_write_report(report_path, sizeof(report_path)) != 0)
    {
        snprintf(diagnostics_report_line, sizeof(diagnostics_report_line),
         "%s", MSG[MSG_DIAGNOSTICS_REPORT_FAILED]);
        return;
    }

    filename = strrchr(report_path, '/');
    if(filename != NULL)
        filename++;
    else
        filename = report_path;
    snprintf(diagnostics_report_line, sizeof(diagnostics_report_line),
     "%s: %.56s", MSG[MSG_DIAGNOSTICS_REPORT_SAVED], filename);
}

static void menu_quit_action(void)
{
    if(menu_game_available())
        action_savestate_slot(AUTO_SAVESTATE_SLOT);
    quit();
}

#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
/* <main_path>traces/input-<game code>-r<version>.gbit, named from the GBA
   cartridge header: game code at 0xAC, software version at 0xBC; the header
   is 0xC0 bytes long. */
static int menu_trace_path(char *path, size_t path_size, int create_directory)
{
    char directory[MAX_PATH];
    char game_code[5];
    SceIoStat stat;
    size_t i;
    int result;

    if(!menu_game_available() || gamepak_file_size < 0xC0U)
        return 0;

    result = snprintf(directory, sizeof(directory), "%straces", main_path);
    if(result < 0 || (size_t)result >= sizeof(directory))
        return 0;

    if(create_directory && sceIoGetstat(directory, &stat) < 0 &&
       sceIoMkdir(directory, 0777) < 0)
        return 0;

    for(i = 0; i < 4; i++)
    {
        u8 value = gamepak_rom[0xAC + i];
        game_code[i] = ((value >= 'A' && value <= 'Z') ||
                        (value >= '0' && value <= '9')) ? (char)value : '_';
    }
    game_code[4] = '\0';

    result = snprintf(path, path_size, "%s/input-%s-r%u.gbit", directory,
     game_code, (unsigned int)gamepak_rom[0xBC]);
    return result >= 0 && (size_t)result < path_size;
}

static int menu_trace_write_all(SceUID file, const u8 *data, size_t size)
{
    size_t written = 0;

    while(written < size)
    {
        s32 result = FILE_WRITE(file, data + written, size - written);
        if(result <= 0)
            return 0;
        written += (size_t)result;
    }
    return 1;
}

static int menu_trace_read_all(SceUID file, u8 *data, size_t size)
{
    size_t read = 0;

    while(read < size)
    {
        s32 result = FILE_READ(file, data + read, size - read);
        if(result <= 0)
            return 0;
        read += (size_t)result;
    }
    return 1;
}

static int menu_trace_start_available(void)
{
    InputTraceState state = input_diagnostic_trace_state();

    return menu_game_available() && state != INPUT_TRACE_RECORDING &&
           state != INPUT_TRACE_REPLAYING;
}

static int menu_trace_finish_available(void)
{
    return input_diagnostic_trace_state() == INPUT_TRACE_RECORDING;
}

static int menu_trace_replay_available(void)
{
    InputTraceState state = input_diagnostic_trace_state();

    return menu_game_available() && state != INPUT_TRACE_RECORDING &&
           state != INPUT_TRACE_REPLAYING;
}

static int menu_trace_cancel_available(void)
{
    return input_diagnostic_trace_state() != INPUT_TRACE_IDLE;
}

static void menu_trace_start(void)
{
    diagnostics_report_line[0] = '\0';
    if(!input_diagnostic_trace_begin_record())
        snprintf(diagnostics_report_line, sizeof(diagnostics_report_line),
         "%s", MSG[MSG_TRACE_FAILED]);
}

static void menu_trace_finish_save(void)
{
    u8 *encoded;
    size_t capacity;
    size_t written;
    char path[MAX_PATH];
    SceUID file;
    int success = 0;

    if(!input_diagnostic_trace_finish_record())
        goto finish;

    capacity = input_diagnostic_trace_max_encoded_size();
    encoded = (u8 *)safe_malloc(capacity);
    if(!input_diagnostic_trace_encode(encoded, capacity, &written) ||
       !menu_trace_path(path, sizeof(path), 1))
    {
        free(encoded);
        goto finish;
    }

    FILE_OPEN(file, path, WRITE);
    if(FILE_CHECK_VALID(file))
    {
        success = menu_trace_write_all(file, encoded, written);
        FILE_CLOSE(file);
        if(!success)
            sceIoRemove(path);
    }
    free(encoded);

finish:
    snprintf(diagnostics_report_line, sizeof(diagnostics_report_line), "%s",
     MSG[success ? MSG_TRACE_SAVED : MSG_TRACE_FAILED]);
}

static void menu_trace_replay(void)
{
    char path[MAX_PATH];
    SceIoStat stat;
    SceUID file;
    u8 *encoded;
    size_t size;
    int success = 0;

    if(!menu_trace_path(path, sizeof(path), 0) ||
       sceIoGetstat(path, &stat) < 0 || stat.st_size <= 0 ||
       (u64)stat.st_size > input_diagnostic_trace_max_encoded_size())
        goto finish;

    size = (size_t)stat.st_size;
    encoded = (u8 *)safe_malloc(size);
    FILE_OPEN(file, path, READ);
    if(FILE_CHECK_VALID(file))
    {
        success = menu_trace_read_all(file, encoded, size);
        FILE_CLOSE(file);
    }
    if(success)
        success = input_diagnostic_trace_begin_replay(encoded, size);
    free(encoded);

finish:
    if(!success)
        snprintf(diagnostics_report_line, sizeof(diagnostics_report_line),
         "%s", MSG[MSG_TRACE_FAILED]);
    else
        diagnostics_report_line[0] = '\0';
}

static void menu_trace_cancel(void)
{
    input_diagnostic_trace_cancel();
    diagnostics_report_line[0] = '\0';
}
#endif

/* A link session needs a game: loading another one closes the link.
   Starting a session pins the CPU at 333 MHz; when it fails or ends, the
   menu's usual 222 MHz is restored. */
static void menu_link_host_action(void)
{
    if (gamepak_filename[0] == '\0' || !main_link_host())
        set_cpu_clock(PSP_CLOCK_222);
}

static void menu_link_join_action(void)
{
    if (gamepak_filename[0] == '\0' || !main_link_join())
        set_cpu_clock(PSP_CLOCK_222);
}

static void menu_link_disconnect_action(void)
{
    main_link_disconnect(MAIN_LINK_END_USER);
    set_cpu_clock(PSP_CLOCK_222);
}

/* The other console's four-letter game code, safe to print. */
static void menu_link_peer_code(char code[5])
{
    u32 index;

    for(index = 0; index < 4; index++)
    {
        u8 value = (u8)gba_link.peer_game[index];
        code[index] = value >= 0x20u && value <= 0x7Eu ? (char)value : '-';
    }
    code[4] = '\0';
}

/* ------------------------------------------------------------------
   Menu text rendering
   ------------------------------------------------------------------ */

/* True if the string has a double-byte lead byte; such text needs the GBK
   font even when the menu language is Japanese. */
static int menu_string_has_gbk(const char *str)
{
    const u8 *p = (const u8 *)str;

    if (str == NULL)
        return 0;

    while (*p != '\0')
    {
        if (*p >= 0x81 && *p <= 0xFE)
            return 1;
        p++;
    }
    return 0;
}

static void print_menu_line(const char *str, s16 x, s16 y, u16 fg, s16 bg)
{
    if (option_language == LANGUAGE_JAPANESE && !menu_string_has_gbk(str))
        print_string(str, x, y, fg, bg);
    else
        print_string_gbk(str, x, y, fg, bg);
}

static void print_menu_line_scroll(const char *str, s16 x, s16 y, u16 fg, s16 bg, u32 max_chars, s32 scroll_offset)
{
    if (menu_string_has_gbk(str)) {
        print_menu_line(str, x, y, fg, bg);
        return;
    }

    u32 len = strlen(str);
    if (len == 0) return;

    if (len <= max_chars) {
        print_menu_line(str, x, y, fg, bg);
        return;
    }

    u32 start = (u32)scroll_offset;
    if (start >= len) start = len - 1;
    u32 clip_len = len - start;
    if (clip_len > max_chars) clip_len = max_chars;
    if (clip_len >= 256) clip_len = 255;

    char buf[256];
    memcpy(buf, str + start, clip_len);
    buf[clip_len] = '\0';

    print_menu_line(buf, x, y, fg, bg);
}

static void print_menu_line_clipped(const char *str, s16 x, s16 y, u16 fg, s16 bg, u32 max_chars)
{
    size_t len = strlen(str);
    size_t clip_len;

    if (len == 0) return;

    clip_len = menu_text_clip_length(str, max_chars);
    if (clip_len == len) {
        print_menu_line(str, x, y, fg, bg);
        return;
    }

    if (clip_len >= 256) clip_len = 255;

    char buf[256];
    memcpy(buf, str, clip_len);
    buf[clip_len] = '\0';

    print_menu_line(buf, x, y, fg, bg);
}

static void print_swap_aware_clipped(const char *src, s16 x, s16 y,
 u16 color, s16 bg, u32 max_chars)
{
    char buf[256];

    copy_swap_aware(src, buf, sizeof(buf));
    print_menu_line_clipped(buf, x, y, color, bg, max_chars);
}

/* Print a menu option by splitting the format string at %s or %d.
   Label is printed fixed. Value is printed with scroll/clip.
   This keeps long values independently scrollable. */
static void print_menu_option_split(const MenuOptionType *opt, s16 x, s16 y, u16 fg, u16 bg, u32 max_chars, s32 scroll_offset)
{
    const char *fmt = opt->display_string;
    if (fmt == NULL) return;

    /* Check for %s or %d in format string */
    const char *spec_s = opt->current_option != NULL ? strstr(fmt, "%s") : NULL;
    const char *spec_d = opt->current_option != NULL ? strstr(fmt, "%d") : NULL;
    const char *spec = NULL;
    u32 is_numeric = 0;

    if (spec_s != NULL && spec_d != NULL) {
        /* Both present — use the first one */
        spec = (spec_s < spec_d) ? spec_s : spec_d;
        is_numeric = (spec_d < spec_s) ? 1 : 0;
    } else if (spec_s != NULL) {
        spec = spec_s;
        is_numeric = 0;
    } else if (spec_d != NULL) {
        spec = spec_d;
        is_numeric = 1;
    } else {
        /* No %s or %d — scroll/clip whole string */
        if (scroll_offset >= 0) {
            print_menu_line_scroll(fmt, x, y, fg, bg, max_chars, scroll_offset);
        } else {
            print_menu_line_clipped(fmt, x, y, fg, bg, max_chars);
        }
        return;
    }

    /* Print label (before %s or %d) */
    u32 prefix_len = (u32)(spec - fmt);
    if (prefix_len >= 256) prefix_len = 255;

    char label_buf[256];
    memcpy(label_buf, fmt, prefix_len);
    label_buf[prefix_len] = '\0';

    print_menu_line(label_buf, x, y, fg, bg);

    /* Calculate value position */
    u32 label_width = prefix_len * FONTWIDTH;
    s16 value_x = x + (s16)label_width;

    /* Get value string */
    char value_buf[32];
    const char *value = "";
    u32 value_len = 0;

    if (is_numeric) {
        /* %d — format the numeric value */
        sprintf(value_buf, "%lu", *(opt->current_option));
        value = value_buf;
        value_len = strlen(value);
    } else {
        /* %s — look up in choice array */
        const char **choices = (const char **)opt->options;
        u32 idx = *(opt->current_option);
        if (choices != NULL && idx < opt->num_options && choices[idx] != NULL) {
            value = choices[idx];
            value_len = strlen(value);
        }
    }

    u32 value_max = (max_chars > prefix_len) ? (max_chars - prefix_len) : 0;
    if (value_max == 0) return;

    if (value_len <= value_max) {
        /* Value fits — print normally */
        print_menu_line(value, value_x, y, fg, bg);
    } else if (scroll_offset >= 0) {
        /* Value scrolls */
        u32 start = (u32)scroll_offset;
        if (start >= value_len) start = value_len - 1;
        u32 clip_len = value_len - start;
        if (clip_len > value_max) clip_len = value_max;
        if (clip_len >= 256) clip_len = 255;

        char val_clip[256];
        memcpy(val_clip, value + start, clip_len);
        val_clip[clip_len] = '\0';
        print_menu_line(val_clip, value_x, y, fg, bg);
    } else {
        /* Value clipped (not selected) */
        u32 clip = value_max;
        if (clip >= 256) clip = 255;
        char val_clip[256];
        memcpy(val_clip, value, clip);
        val_clip[clip] = '\0';
        print_menu_line(val_clip, value_x, y, fg, bg);
    }
}

/* Re-read every translated label after option_language changes. */
static void menu_refresh_language(void)
{
    u32 i, j;
    refresh_choice_arrays();
    for (i = 0; i < num_all_menus; i++)
    {
        MenuType *menu = all_menus[i];
        if (!menu || !menu->options)
            continue;
        for (j = 0; j < menu->num_options; j++)
        {
            MenuOptionType *opt = &menu->options[j];
            if (opt->display_msg != 0)
                opt->display_string = MSG[opt->display_msg];
        }
    }

    if (cheats_misc_options_ptr != NULL)
    {
        refresh_cheat_format_strings();
        reload_cheats_page();
    }
}


/* ------------------------------------------------------------------
   In-game menu
   ------------------------------------------------------------------ */

/* Runs the menu until the user leaves it. Returns 1 when it leaves through
   an action on the emulated machine (ROM load, reset, savestate save or
   load), 0 on a plain resume. The menu tables and screen handlers below are
   locals and nested functions so they can share this frame's state. */
u32 menu(void)
{

  main_fast_forward_stop();

  init_choice_arrays();
  u32 i;

  u32 repeat = 1;
  u32 return_value = 0;

  u32 first_load = 0;           // 1 = no ROM loaded yet
  u32 preview_language = option_language;

  GUI_ACTION_TYPE gui_action;
  SceCtrlData ctrl_data;

  char game_title[MAX_FILE];
  u16 *screen_image_ptr = NULL;
  u16 *current_screen   = NULL;
  u16 *savestate_screen = NULL;

  u32 savestate_action = 0;     // slot rows: 0 = load, 1 = save
  static char savestate_timestamps[SAVESTATE_SLOT_COUNT][40];

  char time_str[40];
  char batt_str[40];
  u16 color_batt_life = color_batt_normal;
  u32 counter = 0;

  char filename_buffer[MAX_PATH];

  char line_buffer[80];
  char link_status_line[96];
  char link_peer_line[96];
  char link_detail_line[96];


  MenuType *current_menu;
  MenuOptionType *current_option;
  MenuOptionType *display_option;
  MenuOptionType *fast_forward_profile_option = NULL;
  MenuOptionType *fast_forward_activation_option = NULL;
  MenuOptionType *fast_forward_speed_option = NULL;
  MenuOptionType *fast_forward_presentation_option = NULL;
  MenuOptionType *fast_forward_audio_option = NULL;
  MenuModel navigation_model;
  MenuRow navigation_rows[MAX_MENU_MODEL_ROWS];

  u32 menu_init_flag = 0;       // set by menu_init(): next screen redraw
                                // runs its one-time setup

  u32 current_option_num = 0;
  u32 menu_main_option_num = 0;





  auto void choose_menu(MenuType *new_menu);

  auto void menu_init(void);
  auto void menu_term(void);
  auto void menu_exit(void);
  auto void menu_reset(void);
  auto void menu_suspend(void);

  auto void menu_change_state(void);
  auto u32 menu_save_state(void);
  auto u32 menu_load_state(void);
  auto void menu_load_state_file(void);
  auto void menu_delete_state(void);

  auto void menu_load_cheat_file(void);
  auto void submenu_cheats_misc(void);

  auto void menu_load_file(void);

  auto void submenu_emulator(void);
  auto void menu_fast_forward_bind_options(void);
  auto void menu_fast_forward_changed(void);
  auto void menu_fast_forward_override_changed(void);
  auto int menu_fast_forward_custom_available(void);
  auto int menu_fast_forward_game_available(void);
  auto void submenu_graphics(void);
  auto void submenu_gamepad(void);
  auto void submenu_analog(void);
  auto void submenu_savestate(void);
  auto void submenu_main(void);
  auto void submenu_theme(void);
  auto void submenu_custom_colors(void);
  auto void submenu_link(void);
  auto void submenu_game(void);
  auto void submenu_audio(void);
  auto void submenu_controls(void);
  auto void submenu_diagnostics(void);
  auto void submenu_exit(void);
  auto void menu_passive_theme(void);
  auto void menu_theme_default(void);

  auto void draw_analog_pad_range(void);
  auto void load_savestate_timestamps(void);

  auto void gamepak_file_none(void);
  auto void gamepak_file_reopen(void);
  auto void menu_show_game_txt_debug(void);

  void menu_init(void)
  {
    menu_init_flag = 1;
  }

  void menu_term(void)
  {
    screen_image_ptr = NULL;

    if (savestate_screen != NULL)
    {
      free(savestate_screen);
      savestate_screen = NULL;
    }

    if (current_screen != NULL)
    {
      free(current_screen);
      current_screen = NULL;
    }
  }

  void menu_exit(void)
  {
    if (!first_load)
      repeat = 0;
  }

  void menu_suspend(void)
  {
    save_game_config_file();

    if (!first_load)
    {
      main_link_disconnect(MAIN_LINK_END_SUSPEND);
      update_backup_immediately();
      auto_savestate_sleep();
      sleep_auto_saved = 1;
    }

    scePowerTick(0);
    scePowerRequestSuspend();
  }

  void menu_load_file(void)
  {
    const char *file_ext[] = { ".zip", ".gba", ".bin", ".agb", ".gbz", NULL };

    save_game_config_file();

    if (!first_load)
    {
      main_link_disconnect(MAIN_LINK_END_ROM_CHANGE);
      update_backup_immediately();
      /* Auto-save current game to the auto slot before loading new ROM */
      action_savestate_slot(AUTO_SAVESTATE_SLOT);
    }

    if (load_file(file_ext, filename_buffer, dir_roms, 1) == 0)
    {
      if (load_gamepak(filename_buffer) < 0)
      {
        clear_screen(COLOR32_BLACK);
        error_msg(MSG[MSG_ERR_LOAD_GAMEPACK], CONFIRMATION_CONT);

        gamepak_file_none();

        menu_init();
        choose_menu(current_menu);
        counter = 0;

        return;
      }

      reset_gba();
      reg[CHANGED_PC_STATUS] = 1;

      /* Add to recent ROMs list */
      add_recent_rom(filename_buffer);

      maybe_load_auto_savestate();

      return_value = 1;
      repeat = 0;
    }
    else
    {
      menu_init();
      choose_menu(current_menu);
      counter = 0;
    }
  }

  void menu_reset(void)
  {
    if (!first_load)
    {
      reset_gba();
      reg[CHANGED_PC_STATUS] = 1;

      return_value = 1;
      repeat = 0;
    }
  }

  /* Show the selected slot's screenshot and refresh its timestamp row. */
  void menu_change_state(void)
  {
    get_savestate_filename(savestate_slot, filename_buffer);
    get_savestate_info(filename_buffer, savestate_screen, line_buffer);
    if (savestate_slot == AUTO_SAVESTATE_SLOT)
      sprintf(savestate_timestamps[savestate_slot], "AUTO: %s", line_buffer);
    else
      sprintf(savestate_timestamps[savestate_slot], "%d: %s", (int)savestate_slot, line_buffer);

    screen_image_ptr = savestate_screen;
  }

  u32 menu_save_state(void)
  {
    if (first_load)
      return 0;

    get_savestate_filename(savestate_slot, filename_buffer);
    if (save_state(filename_buffer, current_screen) == 0)
      return 0;

    get_savestate_info(filename_buffer, savestate_screen, line_buffer);
    sprintf(savestate_timestamps[savestate_slot], "%d: %s", (int)savestate_slot, line_buffer);
    return 1;
  }

  u32 menu_load_state(void)
  {
    if (first_load)
      return 0;

    get_savestate_filename(savestate_slot, filename_buffer);
    return load_state(filename_buffer);
  }

  /* Pick any .svs in dir_state. A state of the running ROM loads directly;
     one of another ROM first loads that ROM from dir_roms, named by the
     part of the file name before the last '_'. */
  void menu_load_state_file(void)
  {
    const char *file_ext[] = { ".svs", NULL };
    char rom_name[MAX_FILE];
    char rom_path[MAX_PATH];
    char dialog_msg[128];
    u32 same_rom = 0;
    u32 slot_parsed = 0;
    u32 file_browser_repeat = 1;

    if(main_link_active())
    {
      error_msg(MSG[MSG_LINK_SAVESTATE_BLOCKED], CONFIRMATION_CONT);
      return;
    }

    while (file_browser_repeat)
    {
      file_browser_repeat = 0;
      same_rom = 0;
      slot_parsed = 0;

      if (load_file(file_ext, filename_buffer, dir_state, 0) == 0)
      {
        /* Extract ROM base name from savestate filename.
           "RomName_5.svs" -> "RomName", "RomName_auto.svs" -> "RomName" */
        {
            char temp_name[MAX_FILE];
            strncpy(temp_name, filename_buffer, sizeof(temp_name) - 1);
            temp_name[sizeof(temp_name) - 1] = '\0';

            char *underscore = strrchr(temp_name, '_');
            if (underscore != NULL)
            {
                *underscore = '\0';
                strncpy(rom_name, temp_name, sizeof(rom_name) - 1);
                rom_name[sizeof(rom_name) - 1] = '\0';

                /* Parse slot number for same-ROM case */
                if (strncmp(underscore + 1, "auto.svs", 8) == 0)
                    slot_parsed = AUTO_SAVESTATE_SLOT;
                else
                    slot_parsed = (u32)atoi(underscore + 1);
            }
            else
            {
                strncpy(rom_name, temp_name, sizeof(rom_name) - 1);
                rom_name[sizeof(rom_name) - 1] = '\0';
            }
        }

        /* Check if this savestate belongs to the currently running ROM */
        if (!first_load && gamepak_filename[0] != '\0')
        {
            char current_base[MAX_FILE];
            change_ext(gamepak_filename, current_base, "");
            if (strcasecmp(current_base, rom_name) == 0)
                same_rom = 1;
        }

        if (same_rom)
        {
            /* Scenario 3: Same ROM — just load the savestate */
            savestate_slot = slot_parsed;
            snprintf(dialog_msg, sizeof(dialog_msg), "%s",
                     MSG[MSG_LOAD_STATE_FILE]);
            if (yesno_dialog(dialog_msg) == 0)
            {
                /* Auto-save current progress before loading */
                action_savestate_slot(AUTO_SAVESTATE_SLOT);
                if (load_state_silent(filename_buffer) != 0)
                {
                    return_value = 1;
                    repeat = 0;
                }
            }
            else
            {
                /* Cancelled — reopen file browser */
                file_browser_repeat = 1;
            }
        }
        else
        {
            /* Scenario 1 or 2: Different ROM or no ROM running */
            char display_name[36];
            if (strlen(rom_name) > 30)
            {
                strncpy(display_name, rom_name, 27);
                display_name[27] = '\0';
                strcat(display_name, "...");
            }
            else
            {
                strcpy(display_name, rom_name);
            }
            snprintf(dialog_msg, sizeof(dialog_msg),
                     MSG[MSG_LOAD_ROM_AND_STATE], display_name);
            if (yesno_dialog(dialog_msg) == 0)
            {
                /* Auto-save current ROM if one is running */
                if (!first_load)
                {
                    save_game_config_file();
                    update_backup_immediately();
                    action_savestate_slot(AUTO_SAVESTATE_SLOT);
                }

                /* Try to find and load the ROM file */
                {
                    const char *rom_exts[] = { ".gba", ".zip", ".bin", ".agb", ".gbz", NULL };
                    u32 rom_found = 0;
                    u32 ext_i;
                    char rom_file[MAX_FILE];

                    for (ext_i = 0; rom_exts[ext_i] != NULL; ext_i++)
                    {
                        /* Check existence with full path */
                        snprintf(rom_path, sizeof(rom_path), "%s%s%s",
                                 dir_roms, rom_name, rom_exts[ext_i]);
                        SceIoStat stat;
                        if (sceIoGetstat(rom_path, &stat) >= 0)
                        {
                            rom_found = 1;
                            /* load_gamepak() expects bare filename, not full path */
                            snprintf(rom_file, sizeof(rom_file), "%s%s",
                                     rom_name, rom_exts[ext_i]);
                            break;
                        }
                    }

                    if (!rom_found)
                    {
                        error_msg(MSG[MSG_ERR_ROM_NOT_FOUND], CONFIRMATION_CONT);
                        /* Reopen file browser after error */
                        file_browser_repeat = 1;
                        continue;
                    }

                    /* load_file(dir_state) changed CWD to savestate dir;
                       load_gamepak() needs CWD to be the ROM directory */
                    chdir(dir_roms);

                    if (load_gamepak(rom_file) < 0)
                    {
                        clear_screen(COLOR32_BLACK);
                        error_msg(MSG[MSG_ERR_LOAD_GAMEPACK], CONFIRMATION_CONT);
                        gamepak_file_none();
                        /* Reopen file browser after error */
                        file_browser_repeat = 1;
                        continue;
                    }

                    reset_gba();
                    reg[CHANGED_PC_STATUS] = 1;
                }

                /* Load the selected savestate */
                savestate_slot = slot_parsed;
                if (load_state_silent(filename_buffer) != 0)
                {
                    return_value = 1;
                    repeat = 0;
                }
            }
            else
            {
                /* Cancelled — reopen file browser */
                file_browser_repeat = 1;
            }
        }
      }
      else
      {
        /* User cancelled file browser — return to savestate menu */
        menu_init();
        choose_menu(current_menu);
        counter = 0;
      }
    }
  }

  void menu_delete_state(void)
  {
    char savestate_filename[MAX_FILE];
    char savestate_path[MAX_PATH];
    char confirm_msg[80];

    if (first_load || current_option_num >= SAVESTATE_SLOT_COUNT)
      return;

    get_savestate_filename(savestate_slot, savestate_filename);
    snprintf(confirm_msg, sizeof(confirm_msg), "%s %s?",
             MSG[MSG_STATE_MENU_DELETE], savestate_timestamps[savestate_slot]);

    /* yesno_dialog: returns 0 = confirmed (O), 1 = cancel (X) */
    if (yesno_dialog(confirm_msg) == 0)
    {
      snprintf(savestate_path, sizeof(savestate_path), "%s%s", dir_state, savestate_filename);
      sceIoRemove(savestate_path);

      /* Refresh timestamp to empty */
      if (savestate_slot == AUTO_SAVESTATE_SLOT)
        sprintf(savestate_timestamps[savestate_slot], "AUTO: %s",
                MSG[(date_format == 0) ? MSG_STATE_MENU_DATE_NONE_0 : MSG_STATE_MENU_DATE_NONE_1]);
      else
        snprintf(savestate_timestamps[savestate_slot], 40, "%d: %s",
                 (int)savestate_slot,
                 MSG[(date_format == 0) ? MSG_STATE_MENU_DATE_NONE_0 : MSG_STATE_MENU_DATE_NONE_1]);

      /* Refresh screenshot to blank */
      memset(savestate_screen, 0, GBA_SCREEN_SIZE);
      print_string_ext_gbk(MSG[MSG_STATE_MENU_STATE_NONE], X_POS_CENTER, 74,
                         COLOR15_WHITE, BG_NO_FILL, savestate_screen, GBA_SCREEN_WIDTH);

      screen_image_ptr = savestate_screen;
    }
  }

  void menu_graphics_default(void)
  {
    option_screen_scale = SCALED_16X9_AUTO;
    option_screen_mag = 170;
    option_screen_filter = FILTER_BILINEAR;
    option_video_renderer = VIDEO_RENDERER_NEW;
    option_oam_hijacking_enabled = 0;
    option_psp_vsync = 0;
  }

  void menu_fast_forward_bind_options(void)
  {
    if(fast_forward_profile_option == NULL)
      return;

    if(option_fast_forward_game_override)
    {
      fast_forward_profile_option->current_option =
       &option_game_fast_forward_profile;
      fast_forward_activation_option->current_option =
       &option_game_fast_forward_activation;
      fast_forward_speed_option->current_option =
       &option_game_fast_forward_speed;
      fast_forward_presentation_option->current_option =
       &option_game_fast_forward_presentation;
      fast_forward_audio_option->current_option =
       &option_game_fast_forward_audio;
    }
    else
    {
      fast_forward_profile_option->current_option =
       &option_fast_forward_profile;
      fast_forward_activation_option->current_option =
       &option_fast_forward_activation;
      fast_forward_speed_option->current_option = &option_fast_forward_speed;
      fast_forward_presentation_option->current_option =
       &option_fast_forward_presentation;
      fast_forward_audio_option->current_option = &option_fast_forward_audio;
    }
  }

  void menu_fast_forward_changed(void)
  {
    main_fast_forward_apply_settings();
  }

  void menu_fast_forward_override_changed(void)
  {
    if(option_fast_forward_game_override)
    {
      option_game_fast_forward_profile = option_fast_forward_profile;
      option_game_fast_forward_activation = option_fast_forward_activation;
      option_game_fast_forward_speed = option_fast_forward_speed;
      option_game_fast_forward_presentation =
       option_fast_forward_presentation;
      option_game_fast_forward_audio = option_fast_forward_audio;
    }
    menu_fast_forward_bind_options();
    main_fast_forward_apply_settings();
  }

  int menu_fast_forward_custom_available(void)
  {
    u32 profile = option_fast_forward_game_override ?
     option_game_fast_forward_profile : option_fast_forward_profile;

    return profile == FAST_FORWARD_PROFILE_CUSTOM;
  }

  int menu_fast_forward_game_available(void)
  {
    return !first_load;
  }

  void menu_emulator_default(void)
  {
    option_frameskip_type = FRAMESKIP_AUTO;
    option_frameskip_value = 9;
    option_clock_speed = PSP_CLOCK_333;
    option_stack_optimize = 1;
    option_ram_dynarec_policy = RAM_DYNAREC_PARTIAL_WITH_REUSE;
    option_hblank_irq_window_start = 1;
    option_hblank_irq_window_end = 160;
    option_boot_mode = 0;
    if(option_fast_forward_game_override)
    {
      option_game_fast_forward_profile = FAST_FORWARD_PROFILE_MODERN;
      option_game_fast_forward_activation = FAST_FORWARD_ACTIVATION_HOLD;
      option_game_fast_forward_speed = FAST_FORWARD_SPEED_MAXIMUM;
      option_game_fast_forward_presentation =
       FAST_FORWARD_PRESENTATION_BALANCED;
      option_game_fast_forward_audio = FAST_FORWARD_AUDIO_SILENT;
    }
    else
    {
      option_fast_forward_profile = FAST_FORWARD_PROFILE_MODERN;
      option_fast_forward_activation = FAST_FORWARD_ACTIVATION_HOLD;
      option_fast_forward_speed = FAST_FORWARD_SPEED_MAXIMUM;
      option_fast_forward_presentation =
       FAST_FORWARD_PRESENTATION_BALANCED;
      option_fast_forward_audio = FAST_FORWARD_AUDIO_SILENT;
    }
    main_fast_forward_apply_settings();
    ram_dynarec_policy_menu_prev = ~(u32)0;
  }

  void menu_gamepad_default(void)
  {
    // Physical PSP buttons mapped to GBA buttons.
    gamepad_config_map[8]  = BUTTON_ID_UP;          /* PSP D-pad Up    → GBA Up */
    gamepad_config_map[6]  = BUTTON_ID_DOWN;        /* PSP D-pad Down  → GBA Down */
    gamepad_config_map[7]  = BUTTON_ID_LEFT;        /* PSP D-pad Left  → GBA Left */
    gamepad_config_map[9]  = BUTTON_ID_RIGHT;       /* PSP D-pad Right → GBA Right */
    gamepad_config_map[1]  = BUTTON_ID_A;           /* PSP Circle      → GBA A */
    gamepad_config_map[2]  = BUTTON_ID_B;           /* PSP Cross       → GBA B */
    gamepad_config_map[3]  = BUTTON_ID_FPS;         /* PSP Square      → EMU FPS */
    gamepad_config_map[0]  = BUTTON_ID_FASTFORWARD; /* PSP Triangle    → EMU FF */
    gamepad_config_map[4]  = BUTTON_ID_L;           /* PSP L trigger   → GBA L */
    gamepad_config_map[5]  = BUTTON_ID_R;           /* PSP R trigger   → GBA R */
    gamepad_config_map[11] = BUTTON_ID_START;       /* PSP Start       → GBA Start */
    gamepad_config_map[10] = BUTTON_ID_SELECT;      /* PSP Select      → GBA Select */ 
    rebuild_fast_forward_button_mask();
  }

  void menu_analog_default(void)
  {
    option_enable_analog = 0;
    option_analog_sensitivity = 4;
    /* Analog stick → GBA directions (only active when analog enabled) */
    gamepad_config_map[12] = BUTTON_ID_UP;       /* Analog Up */
    gamepad_config_map[13] = BUTTON_ID_DOWN;     /* Analog Down */
    gamepad_config_map[14] = BUTTON_ID_LEFT;     /* Analog Left */
    gamepad_config_map[15] = BUTTON_ID_RIGHT;    /* Analog Right */
    rebuild_fast_forward_button_mask();
  }
  
  void menu_theme_settings_default(void)
  {
    psp_fps_debug = 0;
    option_screen_capture_format = 0;
    option_sound_volume = 10;
    option_update_backup = 1;
  }

  void menu_load_cheat_file(void)
  {
    const char *file_ext[] = { ".cht", NULL };
    char load_filename[MAX_FILE];
    char load_directory[MAX_PATH];
    char load_path[MAX_PATH];
    char status_message[256];

    if(load_file(file_ext, load_filename, dir_cheat, 0) == 0)
    {
      CHEAT_LOAD_RESULT load_result;
      u32 error_line = 0;

      if(getcwd(load_directory, sizeof(load_directory)) == NULL)
        load_result = CHEAT_LOAD_ERROR_OPEN;
      else
      {
        snprintf(load_path, sizeof(load_path), "%s/%s",
                 load_directory, load_filename);
        load_result = load_cheats(load_path, &error_line);
      }

      if(load_result == CHEAT_LOAD_OK)
      {
        menu_cheat_page = 0;
        refresh_cheat_format_strings();
        reload_cheats_page();

        if(option_language == LANGUAGE_ITALIAN)
          snprintf(status_message, sizeof(status_message), "%s\n%lu trucchi caricati",
                   loaded_cheat_filename, (unsigned long)num_cheats);
        else
          snprintf(status_message, sizeof(status_message), "%s\n%lu cheats loaded",
                   loaded_cheat_filename, (unsigned long)num_cheats);
      }
      else
      {
        const char *reason;

        if(option_language == LANGUAGE_ITALIAN)
        {
          switch(load_result)
          {
            case CHEAT_LOAD_ERROR_OPEN:
              reason = "file non accessibile";
              break;
            case CHEAT_LOAD_ERROR_CAPACITY:
              reason = "lista oltre la capacita supportata";
              break;
            case CHEAT_LOAD_ERROR_EMPTY:
              reason = "file vuoto";
              break;
            default:
              reason = "formato non valido";
              break;
          }

          if(error_line)
            snprintf(status_message, sizeof(status_message),
                     "Caricamento trucchi fallito alla riga %lu:\n%s",
                     (unsigned long)error_line, reason);
          else
            snprintf(status_message, sizeof(status_message),
                     "Caricamento trucchi fallito:\n%s", reason);
        }
        else
        {
          switch(load_result)
          {
            case CHEAT_LOAD_ERROR_OPEN:
              reason = "file is not accessible";
              break;
            case CHEAT_LOAD_ERROR_CAPACITY:
              reason = "list exceeds supported capacity";
              break;
            case CHEAT_LOAD_ERROR_EMPTY:
              reason = "file is empty";
              break;
            default:
              reason = "invalid format";
              break;
          }

          if(error_line)
            snprintf(status_message, sizeof(status_message),
                     "Cheat load failed at line %lu:\n%s",
                     (unsigned long)error_line, reason);
          else
            snprintf(status_message, sizeof(status_message),
                     "Cheat load failed:\n%s", reason);
        }

        reload_cheats_page();
      }

      error_msg(status_message, CONFIRMATION_CONT);
      choose_menu(current_menu);
    }
  }

  /* Screen handlers: menu() calls the submenu_*() matching current_menu on
     every redraw to draw its title (icon + text at the top left) and any
     extra content. The _GBK variants use the GBK font's icons. */
  #define DRAW_TITLE(title)                                         \
   sprintf(line_buffer, "%s %s", FONT_GBA_ICON, MSG[title]);        \
   print_string(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  #define DRAW_TITLE_PSP(title)                                     \
   sprintf(line_buffer, "%s %s", FONT_PSP_ICON, MSG[title]);        \
   print_string(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  #define DRAW_TITLE_SAVESTATE(title)                               \
   sprintf(line_buffer, "%s %s", FONT_MSC_ICON, MSG[title]);        \
   print_string(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  #define DRAW_TITLE_GBK(title)                                         \
   sprintf(line_buffer, "%s %s", FONT_GBA_ICON_GBK, MSG[title]);        \
   print_string_gbk(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  #define DRAW_TITLE_PSP_GBK(title)                                     \
   sprintf(line_buffer, "%s %s", FONT_PSP_ICON_GBK, MSG[title]);        \
   print_string_gbk(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  #define DRAW_TITLE_SAVESTATE_GBK(title)                               \
   sprintf(line_buffer, "%s %s", FONT_MSC_ICON_GBK, MSG[title]);        \
   print_string_gbk(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  #define DRAW_TITLE_OPT_GBK(title)                                     \
   sprintf(line_buffer, "%s %s", FONT_OPT_ICON_GBK, MSG[title]);        \
   print_string_gbk(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  #define DRAW_TITLE_PAD_GBK(title)                                     \
   sprintf(line_buffer, "%s %s", FONT_PAD_ICON_GBK, MSG[title]);        \
   print_string_gbk(line_buffer, 6, 2, color_help_text, BG_NO_FILL);    \

  void submenu_emulator(void)
  {
    ram_dynarec_policy_menu_prev = ~(u32)0;
    menu_fast_forward_bind_options();
    DRAW_TITLE_OPT_GBK(MSG_OPTION_MENU_TITLE);
  }

  void submenu_graphics(void)
  {
    DRAW_TITLE_OPT_GBK(MSG_MAIN_MENU_3);
  }


  void submenu_cheats_misc(void)
  {
    DRAW_TITLE_PSP_GBK(MSG_CHEAT_MENU_TITLE);

    if(num_cheats != 0)
    {
      char cheat_header[64];

      snprintf(cheat_header, sizeof(cheat_header), "%s (%lu)",
               loaded_cheat_filename, (unsigned long)num_cheats);
      print_menu_line_clipped(cheat_header, 250, 2, color_help_text,
                              BG_NO_FILL, 27);
    }
  }

  void submenu_gamepad(void)
  {
    DRAW_TITLE_PAD_GBK(MSG_PAD_MENU_TITLE);
  }

  void submenu_analog(void)
  {
    DRAW_TITLE_PAD_GBK(MSG_A_PAD_MENU_TITLE);

    draw_analog_pad_range();
  }

  void submenu_savestate(void)
  {
    DRAW_TITLE_SAVESTATE_GBK(MSG_STATE_MENU_TITLE);

    if (menu_init_flag != 0)
    {
      savestate_action = 0;
      menu_change_state();

      current_option_num = savestate_slot;
      current_option = current_menu->options + current_option_num;
      navigation_model.selected_index = current_option_num;
      current_menu->selected_option = current_option_num;

      menu_init_flag = 0;
    }

    if (current_option_num < SAVESTATE_SLOT_COUNT)
    {
      if (savestate_slot != current_option_num)
      {
        savestate_slot = current_option_num;
        menu_change_state();
      }
      /* "Load"/"Save" tag after the slot text; 28 is MENU_LIST_POS_Y. */
      print_string_gbk(MSG[savestate_action ? MSG_SAVE : MSG_LOAD], MENU_LIST_POS_X + ((strlen(savestate_timestamps[current_option_num]) + 1) * FONTWIDTH), (current_option_num * FONTHEIGHT) + 28, color_active_item, BG_NO_FILL);
    }
  }

  void submenu_main(void)
  {
    DRAW_TITLE_GBK(MSG_MAIN_MENU_TITLE);

    if (menu_init_flag != 0)
    {
      screen_image_ptr = current_screen;
      menu_init_flag = 0;
    }

    if (menu_main_option_num != current_option_num)
      menu_main_option_num = current_option_num;
  }

  void submenu_game(void)
  {
    DRAW_TITLE_GBK(MSG_CATEGORY_GAME);
  }

  void submenu_audio(void)
  {
    DRAW_TITLE_OPT_GBK(MSG_CATEGORY_AUDIO);
  }

  void submenu_controls(void)
  {
    DRAW_TITLE_PAD_GBK(MSG_CATEGORY_CONTROLS);
  }

  void submenu_diagnostics(void)
  {
    PerfMetricsSnapshot snapshot;
    u32 status_message;

    DRAW_TITLE_OPT_GBK(MSG_DIAGNOSTICS_TITLE);
    main_perf_snapshot(&snapshot);
    switch(snapshot.state)
    {
      case PERF_METRICS_ACTIVE:
        status_message = MSG_DIAGNOSTICS_STATUS_ACTIVE;
        break;
      case PERF_METRICS_STOPPED:
        status_message = MSG_DIAGNOSTICS_STATUS_STOPPED;
        break;
      case PERF_METRICS_INVALIDATED:
        status_message = MSG_DIAGNOSTICS_STATUS_INVALID;
        break;
      default:
        status_message = MSG_DIAGNOSTICS_STATUS_IDLE;
        break;
    }
    snprintf(diagnostics_status_line, sizeof(diagnostics_status_line),
     "%s", MSG[status_message]);
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
    {
      u32 trace_state_message;

      switch(input_diagnostic_trace_state())
      {
        case INPUT_TRACE_RECORDING:
          trace_state_message = MSG_TRACE_STATE_RECORDING;
          break;
        case INPUT_TRACE_READY:
          trace_state_message = MSG_TRACE_STATE_READY;
          break;
        case INPUT_TRACE_REPLAYING:
          trace_state_message = MSG_TRACE_STATE_REPLAYING;
          break;
        case INPUT_TRACE_FINISHED:
          trace_state_message = MSG_TRACE_STATE_FINISHED;
          break;
        case INPUT_TRACE_ERROR:
          trace_state_message = MSG_TRACE_STATE_ERROR;
          break;
        default:
          trace_state_message = MSG_TRACE_STATE_IDLE;
          break;
      }
      snprintf(diagnostics_trace_status_line,
       sizeof(diagnostics_trace_status_line), MSG[MSG_TRACE_STATUS_FORMAT],
       MSG[trace_state_message],
       (unsigned long)input_diagnostic_trace_event_count());
    }
#endif
  }

  void submenu_exit(void)
  {
    DRAW_TITLE_PSP_GBK(MSG_CATEGORY_EXIT);
  }

  /* Live analog stick view over the screenshot area: the stick position and
     the dead zone set by option_analog_sensitivity. The 0-255 stick range is
     drawn at half scale (PAD_RANGE pixels), centred in the 240x160 window. */
  void draw_analog_pad_range(void)
  {
    char print_buffer[40];
    u32 lx, ly;
    u32 analog_sensitivity, inv_analog_sensitivity;

    #define PAD_RANGE (255 >> 1)
    #define BASE_X (SCREEN_IMAGE_POS_X + ((GBA_SCREEN_WIDTH  - PAD_RANGE) >> 1))
    #define BASE_Y (SCREEN_IMAGE_POS_Y + ((GBA_SCREEN_HEIGHT - PAD_RANGE) >> 1))

    sceCtrlPeekBufferPositive(&ctrl_data, 1);
    lx = ctrl_data.Lx;
    ly = ctrl_data.Ly;

    analog_sensitivity = 20 + (option_analog_sensitivity * 10);
    inv_analog_sensitivity = 255 - analog_sensitivity;

    draw_box_alpha(SCREEN_IMAGE_POS_X, SCREEN_IMAGE_POS_Y, SCREEN_IMAGE_POS_X + GBA_SCREEN_WIDTH - 1, SCREEN_IMAGE_POS_Y + GBA_SCREEN_HEIGHT - 1, 0xBF000000);

    sprintf(print_buffer, "Lx:%3d Ly:%3d", (int)lx, (int)ly);
    print_string(print_buffer, SCREEN_IMAGE_POS_X + 6, SCREEN_IMAGE_POS_Y + 2, COLOR15_WHITE, BG_NO_FILL);

    if (lx < analog_sensitivity)
      print_string(FONT_CURSOR_LEFT, BASE_X - (FONTWIDTH << 1), BASE_Y + ((PAD_RANGE - FONTHEIGHT) >> 1), COLOR15_WHITE, BG_NO_FILL);

    if (lx > inv_analog_sensitivity)
      print_string(FONT_CURSOR_RIGHT, BASE_X + PAD_RANGE, BASE_Y + ((PAD_RANGE - FONTHEIGHT) >> 1), COLOR15_WHITE, BG_NO_FILL);

    if (ly < analog_sensitivity)
      print_string(FONT_CURSOR_UP, BASE_X + (PAD_RANGE >> 1) - FONTWIDTH, BASE_Y - FONTHEIGHT, COLOR15_WHITE, BG_NO_FILL);

    if (ly > inv_analog_sensitivity)
      print_string(FONT_CURSOR_DOWN, BASE_X + (PAD_RANGE >> 1) - FONTWIDTH, BASE_Y + PAD_RANGE, COLOR15_WHITE, BG_NO_FILL);

    lx >>= 1;
    ly >>= 1;
    analog_sensitivity >>= 1;
    inv_analog_sensitivity >>= 1;

    draw_box_line(BASE_X, BASE_Y, BASE_X + PAD_RANGE, BASE_Y + PAD_RANGE, COLOR15_WHITE);

    // dead zone
    draw_box_alpha(BASE_X + analog_sensitivity, BASE_Y + analog_sensitivity, BASE_X + inv_analog_sensitivity, BASE_Y + inv_analog_sensitivity, 0x5F000000 | 255);
    draw_box_line(BASE_X + analog_sensitivity, BASE_Y + analog_sensitivity, BASE_X + inv_analog_sensitivity, BASE_Y + inv_analog_sensitivity, COLOR15_RED);

    // pointer
    draw_box_line(BASE_X + lx - 2, BASE_Y + ly - 2, BASE_X + lx + 2, BASE_Y + ly + 2, COLOR15_WHITE);
    draw_hline(BASE_X + lx - 5, BASE_X + lx + 5, BASE_Y + ly, COLOR15_WHITE);
    draw_vline(BASE_X + lx, BASE_Y + ly - 5, BASE_Y + ly + 5, COLOR15_WHITE);
  }

  void load_savestate_timestamps(void)
  {
    for (i = 0; i < SAVESTATE_SLOT_COUNT; i++)
    {
      get_savestate_filename(i, filename_buffer);
      get_savestate_info(filename_buffer, NULL, line_buffer);
      if (i == AUTO_SAVESTATE_SLOT)
        sprintf(savestate_timestamps[i], "AUTO: %s", line_buffer);
      else
        sprintf(savestate_timestamps[i], "%d: %s", (int)i, line_buffer);
    }
  }

  /* Forget the current ROM and show the "no game loaded" placeholder. */
  void gamepak_file_none(void)
  {
    gamepak_filename[0] = 0;
    gamepak_file_size = 0;
    game_title[0] = 0;

    first_load = 1;

    memset(current_screen, 0x00, GBA_SCREEN_SIZE);
	print_string_ext_gbk(MSG[MSG_NON_LOAD_GAME], X_POS_CENTER, 74, COLOR15_WHITE, BG_NO_FILL, current_screen, GBA_SCREEN_WIDTH);
  }

  /* Reopen the streamed ROM file closed on menu entry: five tries, 0.5 s
     apart, then a fatal error. */
  void gamepak_file_reopen(void)
  {
    for (i = 0; i < 5; i++)
    {
      FILE_OPEN(gamepak_file_large, gamepak_filename_raw, READ);

      if (FILE_CHECK_VALID(gamepak_file_large))
        return;

      sceKernelDelayThread(500 * 1000);
    }

    clear_screen(COLOR32_BLACK);
    error_msg(MSG[MSG_ERR_OPEN_GAMEPACK], CONFIRMATION_QUIT);
    quit();
  }

  /* Scrollable page with the ROM header bytes used to match
     game_config.txt and the idle loop / SMC gate hacks loaded for it. */
  void menu_show_game_txt_debug(void)
  {
    char buf[2400];
    char *p;
    char *end = buf + sizeof(buf);
    int i;
    GUI_ACTION_TYPE gui_action = CURSOR_NONE;
    s32 scroll_line = 0;
    s32 total_lines = 0;
    char *line_ptrs[128];
    s32 line_count = 0;

    if (gamepak_filename[0] == '\0')
    {
      error_msg(MSG[MSG_NON_LOAD_GAME], CONFIRMATION_CONT);
      return;
    }

    snprintf(buf, sizeof(buf),
      "ROM header: internal title @0xA0 (12 bytes),\n"
      "game_code @0xAC (4), maker @0xB0 (2).\n"
      "game_config.txt matches game_name, game_code,\n"
      "then vender_code or vendor_code.\n\n"
      "%s%s\n\n"
      "Hex 0xA0..0xB1 (18 bytes): ",
      main_path,
      "game_config.txt");

    p = buf + strlen(buf);
    for (i = 0; i < 18 && p < end - 6; i++)
    {
      p += snprintf(p, (size_t)(end - p), "%s%02X", (i > 0) ? " " : "", (unsigned)gamepak_rom[0xA0 + (unsigned)i]);
    }
    p += snprintf(p, (size_t)(end - p), "\n\n");

    p += snprintf(p, (size_t)(end - p), "idle_loop_eliminate_target (%d / %d):\n",
                  (int)idle_loop_targets, MAX_IDLE_LOOPS);
    if (idle_loop_targets <= 0)
      snprintf(p, (size_t)(end - p), "  (none loaded)\n");
    else
    {
      for (i = 0; i < idle_loop_targets && p < end - 48; i++)
      {
        p += snprintf(p, (size_t)(end - p), "  %2d: 0x%08X\n", i, (unsigned)idle_loop_target_pc[i]);
      }
    }

    p = buf + strlen(buf);
    p += snprintf(p, (size_t)(end - p), "\nSMC gates smc_gate / smc_cutpoint (%d / %d):\n",
                  (int)smc_gate_targets, MAX_SMC_GATES);
    if (smc_gate_targets <= 0)
      snprintf(p, (size_t)(end - p), "  (none loaded)\n");
    else
    {
      for (i = 0; i < smc_gate_targets && p < end - 48; i++)
      {
        p += snprintf(p, (size_t)(end - p), "  %2d: 0x%08X\n", i, (unsigned)smc_gate_pc[i]);
      }
    }

    i = (int)strlen(buf);
    snprintf(buf + i, sizeof(buf) - (size_t)i, "\n\n%s", MSG[MSG_ERR_CONT]);

    /* Count total lines and build line pointer array */
    {
      char *walk = buf;
      line_ptrs[0] = walk;
      line_count = 1;
      while (*walk != '\0' && line_count < 128)
      {
        char *nl = strchr(walk, '\n');
        if (nl != NULL)
        {
          walk = nl + 1;
          line_ptrs[line_count] = walk;
          line_count++;
        }
        else
        {
          break;
        }
      }
      /* line_count includes the empty line after last \n, so effective lines = line_count - 1 */
      total_lines = line_count - 1;
    }

    {
      s32 visible_lines = (PSP_SCREEN_HEIGHT - 6) / FONTHEIGHT;
      s32 max_scroll = (total_lines > visible_lines) ? (total_lines - visible_lines) : 0;

      do
      {
        clear_screen(COLOR15_TO_32(color_bg));

        {
          u16 ypos = 6;
          s32 line_idx;
          u32 max_chars = (PSP_SCREEN_WIDTH - 6 - 16) / FONTWIDTH;
          s32 lines_drawn = 0;

          for (line_idx = scroll_line; line_idx < total_lines && lines_drawn < visible_lines; line_idx++)
          {
            char line_buf[80];
            char *start = line_ptrs[line_idx];
            char *next_nl = strchr(start, '\n');
            size_t len;
            size_t offset = 0;

            if (next_nl != NULL)
              len = (size_t)(next_nl - start);
            else
              len = strlen(start);

            /* Empty line = spacing (draw nothing but advance) */
            if (len == 0)
            {
              ypos = (u16)(ypos + FONTHEIGHT);
              lines_drawn++;
              continue;
            }

            /* Wrap long lines across multiple display lines */
            while (offset < len && lines_drawn < visible_lines)
            {
              size_t chunk = len - offset;
              if (chunk > max_chars)
              {
                chunk = max_chars;
                /* Word-wrap: find last space within the chunk so we don't
                   split a hex number (or any token) across lines. */
                size_t break_at = chunk;
                while (break_at > 0 && start[offset + break_at - 1] != ' ')
                  break_at--;
                if (break_at > 0)
                  chunk = break_at;
              }
              if (chunk >= sizeof(line_buf))
                chunk = sizeof(line_buf) - 1;

              memcpy(line_buf, start + offset, chunk);
              line_buf[chunk] = '\0';

              print_string_gbk(line_buf, 6, ypos, color_active_item, color_bg);

              ypos = (u16)(ypos + FONTHEIGHT);
              lines_drawn++;
              offset += chunk;
            }
          }
        }

        /* Scroll indicators */
        if (scroll_line > 0)
          print_string(FONT_CURSOR_UP_FILL, PSP_SCREEN_WIDTH - 16, 6, color_scroll_bar, color_bg);
        if (scroll_line < max_scroll)
          print_string(FONT_CURSOR_DOWN_FILL, PSP_SCREEN_WIDTH - 16, PSP_SCREEN_HEIGHT - FONTHEIGHT - 2, color_scroll_bar, color_bg);

        /* Exit hint at bottom */
        print_swap_aware(MSG[MSG_ERR_CONT], X_POS_CENTER, PSP_SCREEN_HEIGHT - FONTHEIGHT - 2, color_help_text, color_bg);

        flip_screen(1);

        gui_action = get_gui_input();

        switch (gui_action)
        {
          case CURSOR_UP:
            if (scroll_line > 0)
              scroll_line--;
            break;
          case CURSOR_DOWN:
            if (scroll_line < max_scroll)
              scroll_line++;
            break;
          default:
            break;
        }
      } while (gui_action != CURSOR_SELECT && gui_action != CURSOR_EXIT && gui_action != CURSOR_BACK);
    }
  }


  /* ------------------------------------------------------------------
     Theme, custom colour and link screens
   ------------------------------------------------------------------ */

  void submenu_theme(void)
  {
    DRAW_TITLE_OPT_GBK(MSG_EMULATOR_MENU_TITLE);
  }

  void submenu_custom_colors(void)
  {
    DRAW_TITLE_OPT_GBK(MSG_OPTION_MENU_CUSTOM_COLORS);
  }

  void submenu_link(void)
  {
    const char *status;
    const char *reason = NULL;
    MainLinkEnd end = main_link_end_reason();
    char code[5];

    DRAW_TITLE_PSP_GBK(MSG_LINK_MENU_TITLE);
    link_detail_line[0] = '\0';

    if(main_link_connected())
    {
      status = MSG[gba_link.host ? MSG_LINK_STATUS_CONNECTED_P1 :
       MSG_LINK_STATUS_CONNECTED_P2];
      /* Technical line: which technique the cable chose for this pair of
         games, and the measured round trip. */
      snprintf(link_detail_line, sizeof(link_detail_line),
       "%s, %lu ms", gba_link.mode == LINK_CABLE_MODE_POKEMON ?
        "Pokemon frame link" : "Lockstep link",
       (unsigned long)((gba_link.net.rtt_us + 500u) / 1000u));
    }
    else if(main_link_active())
    {
      switch(psp_adhoc_state())
      {
        case PSP_ADHOC_STARTING:
          status = MSG[MSG_LINK_STATUS_STARTING];
          break;
        case PSP_ADHOC_HOSTING:
          status = MSG[MSG_LINK_STATUS_HOSTING];
          break;
        case PSP_ADHOC_SEARCHING:
          status = MSG[MSG_LINK_STATUS_SEARCHING];
          break;
        case PSP_ADHOC_CONNECTING:
          status = MSG[MSG_LINK_STATUS_CONNECTING];
          break;
        default:
          status = MSG[MSG_LINK_STATUS_NEGOTIATING];
          break;
      }
    }
    else if(!psp_adhoc_wlan_enabled())
      status = MSG[MSG_LINK_STATUS_WLAN_OFF];
    else if(first_load)
      status = MSG[MSG_LINK_STATUS_NO_GAME];
    else
    {
      status = MSG[MSG_LINK_STATUS_DISCONNECTED];
      switch(end)
      {
        case MAIN_LINK_END_PEER_LEFT:
          reason = MSG[MSG_LINK_REASON_PEER_CLOSED];
          break;
        case MAIN_LINK_END_LOST:
          reason = MSG[MSG_LINK_REASON_TIMEOUT];
          break;
        case MAIN_LINK_END_INCOMPATIBLE:
          reason = MSG[MSG_LINK_REASON_INCOMPATIBLE];
          break;
        case MAIN_LINK_END_SUSPEND:
          reason = MSG[MSG_LINK_REASON_SUSPEND];
          break;
        case MAIN_LINK_END_ROM_CHANGE:
          reason = MSG[MSG_LINK_REASON_ROM_CHANGE];
          break;
        case MAIN_LINK_END_NETWORK:
          status = MSG[MSG_LINK_STATUS_ERROR];
          reason = MSG[MSG_LINK_REASON_TRANSPORT];
          snprintf(link_detail_line, sizeof(link_detail_line),
           "%s 0x%08X", psp_adhoc_phase_name(psp_adhoc_failure_phase()),
           (unsigned int)psp_adhoc_last_error());
          break;
        default:
          break;
      }
    }

    if(reason != NULL)
      snprintf(link_status_line, sizeof(link_status_line), "%s: %s - %s",
       MSG[MSG_LINK_MENU_STATUS], status, reason);
    else
      snprintf(link_status_line, sizeof(link_status_line), "%s: %s",
       MSG[MSG_LINK_MENU_STATUS], status);

    if(main_link_connected())
    {
      menu_link_peer_code(code);
      snprintf(link_peer_line, sizeof(link_peer_line), "%s: %s",
       MSG[MSG_LINK_MENU_PEER], code);
    }
    else
      snprintf(link_peer_line, sizeof(link_peer_line), "%s: %s",
       MSG[MSG_LINK_MENU_PEER], MSG[MSG_LINK_PEER_NONE]);
  }

  void menu_passive_theme(void)
  {
    apply_theme(option_theme);
  }

  void menu_theme_default(void)
  {
    option_theme = 0;
    apply_theme(option_theme);
  }

  /* ------------------------------------------------------------------
     Menu tables, built on every menu() call. Submenus come before the
     menus that link to them; the parents are wired up after main_menu.
     An option with a NULL action is handled inline in the input switch
     below, by menu and line number.
   ------------------------------------------------------------------ */

  MenuOptionType custom_colors_options[] =
  {
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_BG],            MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 0, MSG_CUSTOM_COLOR_BG),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_ROM_INFO],      MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 1, MSG_CUSTOM_COLOR_ROM_INFO),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_ACTIVE_ITEM],   MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 2, MSG_CUSTOM_COLOR_ACTIVE_ITEM),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_INACTIVE_ITEM], MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 3, MSG_CUSTOM_COLOR_INACTIVE_ITEM),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_TOOLTIP_TEXT],  MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 4, MSG_CUSTOM_COLOR_TOOLTIP_TEXT),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_HELP_TEXT],     MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 5, MSG_CUSTOM_COLOR_HELP_TEXT),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_INACTIVE_DIR],  MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 6, MSG_CUSTOM_COLOR_INACTIVE_DIR),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_SCROLL_BAR],    MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 7, MSG_CUSTOM_COLOR_SCROLL_BAR),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_BATT_NORMAL],   MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 8, MSG_CUSTOM_COLOR_BATT_NORMAL),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_BATT_LOW],      MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 9, MSG_CUSTOM_COLOR_BATT_LOW),
    ACTION_OPTION(menu_pick_color, NULL, MSG[MSG_CUSTOM_COLOR_BATT_CHARG],    MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 10, MSG_CUSTOM_COLOR_BATT_CHARG),

    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 12, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(custom_colors, NULL, NULL);

  MenuOptionType theme_options[] =
  {
    STRING_SELECTION_OPTION(menu_passive_theme, MSG[MSG_OPTION_MENU_THEMES], theme_preset_options, &option_theme, 9, MSG_OPTION_MENU_HELP_THEMES, 0, MSG_OPTION_MENU_THEMES),
    SUBMENU_OPTION(&custom_colors_menu, MSG[MSG_OPTION_MENU_CUSTOM_COLORS], MSG_OPTION_MENU_HELP_CUSTOM_COLORS, 1, MSG_OPTION_MENU_CUSTOM_COLORS),

    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_SWAP_BUTTONS], swap_button_options, &option_swap_confirm_buttons, 2, MSG_OPTION_MENU_HELP_SWAP_BUTTONS, 3, MSG_OPTION_MENU_SWAP_BUTTONS),
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_SHOW_FPS], on_off_options, &psp_fps_debug, 2, MSG_OPTION_MENU_HELP_SHOW_FPS, 5, MSG_OPTION_MENU_SHOW_FPS),
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_SCREENSHOT], image_format_options, &option_screen_capture_format, 2, MSG_OPTION_MENU_HELP_7, 6, MSG_OPTION_MENU_SCREENSHOT),
    STRING_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_9], update_backup_options, &option_update_backup, 2, MSG_OPTION_MENU_HELP_9, 7, MSG_TOOLTIP_AUTO_BACKUP, MSG_OPTION_MENU_9), 

    STRING_SELECTION_ACTION_OPTION(NULL, menu_refresh_language, MSG[MSG_OPTION_MENU_10], language_option, &option_language, 5, MSG_OPTION_MENU_HELP_10, 9, MSG_OPTION_MENU_10), 

    ACTION_OPTION(menu_theme_default, NULL, MSG[MSG_OPTION_MENU_DEFAULT_THEME], MSG_OPTION_MENU_HELP_DEFAULT_THEME, 11, MSG_OPTION_MENU_DEFAULT_THEME),
    ACTION_OPTION(menu_theme_settings_default, NULL, MSG[MSG_OPTION_MENU_DEFAULT_SETTINGS], MSG_OPTION_MENU_HELP_DEFAULT, 12, MSG_OPTION_MENU_DEFAULT_SETTINGS),

    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 14, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(theme, NULL, NULL);

  MenuOptionType graphics_options[] =
  {
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_0], scale_options, &option_screen_scale, SCREEN_SCALE_COUNT, MSG_OPTION_MENU_HELP_0, 0, MSG_OPTION_MENU_0),
    NUMERIC_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_1], &option_screen_mag, 201, MSG_OPTION_MENU_HELP_1, 1,MSG_TOOLTIP_MAGNIFICATION, MSG_OPTION_MENU_1),
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_2], on_off_options, &option_screen_filter, 2, MSG_OPTION_MENU_HELP_2, 2, MSG_OPTION_MENU_2),

    STRING_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_VIDEORENDER], video_renderer_options, &option_video_renderer, 2, MSG_OPTION_MENU_HELP_7, 4, MSG_TOOLTIP_VIDEO_RENDERER, MSG_OPTION_MENU_VIDEORENDER),
    STRING_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_OAMHIJACKSUPPORT], on_off_options, &option_oam_hijacking_enabled, 2, MSG_OPTION_MENU_HELP_7, 5, MSG_TOOLTIP_OAM_HIJACKING, MSG_OPTION_MENU_OAMHIJACKSUPPORT),
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_VSYNCPSP], on_off_options, &option_psp_vsync, 2, MSG_OPTION_MENU_HELP_7, 6,MSG_OPTION_MENU_VSYNCPSP),
    
    ACTION_OPTION(menu_graphics_default, NULL, MSG[MSG_OPTION_MENU_DEFAULT], MSG_OPTION_MENU_HELP_DEFAULT, 8, MSG_OPTION_MENU_DEFAULT),

    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 10, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(graphics, NULL, NULL);

  MenuOptionType performance_advanced_options[] =
  {
    STRING_SELECTION_OPTION_TT(menu_passive_ram_dynarec_policy, MSG[MSG_OPTION_MENU_BLOCK_CHECKSUM_REUSE], ram_dynarec_options, &option_ram_dynarec_policy, 3, MSG_OPTION_MENU_HELP_BLOCK_CHECKSUM_REUSE, 0, MSG_TOOLTIP_RAM_DYNAREC_MODE, MSG_OPTION_MENU_BLOCK_CHECKSUM_REUSE),
    NUMERIC_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_HBLANK_IRQ_WIN_START], &option_hblank_irq_window_start, 228, MSG_OPTION_MENU_HELP_HBLANK_IRQ_WIN_START, 1, MSG_TOOLTIP_HBLANK_WIN_START, MSG_OPTION_MENU_HBLANK_IRQ_WIN_START),
    NUMERIC_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_HBLANK_IRQ_WIN_END], &option_hblank_irq_window_end, 228, MSG_OPTION_MENU_HELP_HBLANK_IRQ_WIN_END, 2, MSG_TOOLTIP_HBLANK_WIN_END, MSG_OPTION_MENU_HBLANK_IRQ_WIN_END),
    STRING_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_7], stack_optimize_options, &option_stack_optimize, 2, MSG_OPTION_MENU_HELP_7, 4, MSG_TOOLTIP_STACK_OPTIMIZE, MSG_OPTION_MENU_7),
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_8], yes_no_options, &option_boot_mode, 2, MSG_OPTION_MENU_HELP_8, 6, MSG_OPTION_MENU_8),
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 8, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(performance_advanced, NULL, NULL);

  MenuOptionType emulator_options[] =
  {
    STRING_SELECTION_OPTION(menu_fast_forward_changed, MSG[MSG_FF_PROFILE_LABEL], fast_forward_profile_options, &option_fast_forward_profile, FAST_FORWARD_PROFILE_COUNT, MSG_FF_HELP_PROFILE, 0, MSG_FF_PROFILE_LABEL),
    STRING_SELECTION_OPTION(menu_fast_forward_override_changed, MSG[MSG_FF_OVERRIDE_LABEL], fast_forward_override_options, &option_fast_forward_game_override, 2, MSG_FF_HELP_OVERRIDE, 1, MSG_FF_OVERRIDE_LABEL),
    STRING_SELECTION_OPTION(menu_fast_forward_changed, MSG[MSG_FF_ACTIVATION_LABEL], fast_forward_activation_options, &option_fast_forward_activation, FAST_FORWARD_ACTIVATION_COUNT, MSG_FF_HELP_ACTIVATION, 2, MSG_FF_ACTIVATION_LABEL),
    STRING_SELECTION_OPTION(menu_fast_forward_changed, MSG[MSG_FF_SPEED_LABEL], fast_forward_speed_options, &option_fast_forward_speed, FAST_FORWARD_SPEED_COUNT, MSG_FF_HELP_SPEED, 3, MSG_FF_SPEED_LABEL),
    STRING_SELECTION_OPTION(menu_fast_forward_changed, MSG[MSG_FF_PRESENTATION_LABEL], fast_forward_presentation_options, &option_fast_forward_presentation, FAST_FORWARD_PRESENTATION_COUNT, MSG_FF_HELP_PRESENTATION, 4, MSG_FF_PRESENTATION_LABEL),
    STRING_SELECTION_OPTION(menu_fast_forward_changed, MSG[MSG_FF_AUDIO_LABEL], fast_forward_audio_options, &option_fast_forward_audio, FAST_FORWARD_AUDIO_COUNT, MSG_FF_HELP_AUDIO, 5, MSG_FF_AUDIO_LABEL),
    STRING_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_3], frameskip_options, &option_frameskip_type, 3, MSG_OPTION_MENU_HELP_3, 7, MSG_TOOLTIP_FRAMESKIP_TYPE, MSG_OPTION_MENU_3),
    NUMERIC_SELECTION_OPTION_TT(NULL, MSG[MSG_OPTION_MENU_4], &option_frameskip_value, 10, MSG_OPTION_MENU_HELP_4, 8, MSG_TOOLTIP_FRAMESKIP_VALUE, MSG_OPTION_MENU_4),
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_5], clock_speed_options, &option_clock_speed, 4, MSG_OPTION_MENU_HELP_5, 9, MSG_OPTION_MENU_5),
    SUBMENU_OPTION(&performance_advanced_menu, MSG[MSG_FF_ADVANCED], MSG_FF_HELP_ADVANCED, 11, MSG_FF_ADVANCED),
    ACTION_OPTION(menu_emulator_default, NULL, MSG[MSG_OPTION_MENU_DEFAULT], MSG_OPTION_MENU_HELP_DEFAULT, 13, MSG_OPTION_MENU_DEFAULT),
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 15, MSG_OPTION_MENU_11)
  };

  fast_forward_profile_option = &emulator_options[0];
  fast_forward_activation_option = &emulator_options[2];
  fast_forward_speed_option = &emulator_options[3];
  fast_forward_presentation_option = &emulator_options[4];
  fast_forward_audio_option = &emulator_options[5];
  emulator_options[1].available_function = menu_fast_forward_game_available;
  emulator_options[1].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  emulator_options[2].available_function = menu_fast_forward_custom_available;
  emulator_options[3].available_function = menu_fast_forward_custom_available;
  emulator_options[4].available_function = menu_fast_forward_custom_available;
  emulator_options[5].available_function = menu_fast_forward_custom_available;
  emulator_options[2].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  emulator_options[3].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  emulator_options[4].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  emulator_options[5].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  menu_fast_forward_bind_options();

  MAKE_MENU(emulator, NULL, NULL);


  MenuOptionType cheats_misc_options[] =
  {
    CHEAT_OPTION((10 * menu_cheat_page) + 0),
    CHEAT_OPTION((10 * menu_cheat_page) + 1),
    CHEAT_OPTION((10 * menu_cheat_page) + 2),
    CHEAT_OPTION((10 * menu_cheat_page) + 3),
    CHEAT_OPTION((10 * menu_cheat_page) + 4),
    CHEAT_OPTION((10 * menu_cheat_page) + 5),
    CHEAT_OPTION((10 * menu_cheat_page) + 6),
    CHEAT_OPTION((10 * menu_cheat_page) + 7),
    CHEAT_OPTION((10 * menu_cheat_page) + 8),
    CHEAT_OPTION((10 * menu_cheat_page) + 9),

    NUMERIC_SELECTION_OPTION(reload_cheats_page, MSG[MSG_CHEAT_MENU_3], &menu_cheat_page, MAX_CHEATS_PAGE, MSG_CHEAT_MENU_HELP_3, 11, MSG_CHEAT_MENU_3),

    ACTION_OPTION(NULL, NULL, MSG[MSG_CHEAT_MENU_1], MSG_CHEAT_MENU_HELP_1, 13, MSG_CHEAT_MENU_1),

    SUBMENU_OPTION(NULL, MSG[MSG_CHEAT_MENU_2], MSG_CHEAT_MENU_HELP_2, 15, MSG_CHEAT_MENU_2)
  };

  cheats_misc_options_ptr = cheats_misc_options;

  MAKE_MENU(cheats_misc, NULL, NULL);

  MenuOptionType savestate_options[] =
  {
    SAVESTATE_OPTION(0),
    SAVESTATE_OPTION(1),
    SAVESTATE_OPTION(2),
    SAVESTATE_OPTION(3),
    SAVESTATE_OPTION(4),
    SAVESTATE_OPTION(5),
    SAVESTATE_OPTION(6),
    SAVESTATE_OPTION(7),
    SAVESTATE_OPTION(8),
    SAVESTATE_OPTION(9),
    SAVESTATE_OPTION(10),

    ACTION_OPTION(NULL, NULL, MSG[MSG_STATE_MENU_1], MSG_STATE_MENU_HELP_1, 12, MSG_STATE_MENU_1),

    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_STATE_MENU_2], MSG_STATE_MENU_HELP_2, 14, MSG_STATE_MENU_2)
  };

  MAKE_MENU(savestate, NULL, NULL);

  MenuOptionType gamepad_config_options[] =
  {
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_0], 0, MSG_PAD_MENU_0),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_1], 1, MSG_PAD_MENU_1),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_2], 2, MSG_PAD_MENU_2),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_3], 3, MSG_PAD_MENU_3),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_4], 4, MSG_PAD_MENU_4),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_5], 5, MSG_PAD_MENU_5),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_6], 6, MSG_PAD_MENU_6),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_7], 7, MSG_PAD_MENU_7),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_8], 8, MSG_PAD_MENU_8),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_9], 9, MSG_PAD_MENU_9),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_10], 10, MSG_PAD_MENU_10),
    GAMEPAD_CONFIG_OPTION(MSG[MSG_PAD_MENU_11], 11, MSG_PAD_MENU_11),

    ACTION_OPTION(menu_gamepad_default, NULL, MSG[MSG_OPTION_MENU_DEFAULT], MSG_OPTION_MENU_HELP_DEFAULT_KEYBIND, 13, MSG_OPTION_MENU_DEFAULT),

    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_PAD_MENU_12], MSG_PAD_MENU_HELP_1, 15, MSG_PAD_MENU_12)
  };

  MAKE_MENU(gamepad_config, NULL, NULL);

  MenuOptionType analog_config_options[] =
  {
    ANALOG_CONFIG_OPTION(MSG[MSG_A_PAD_MENU_0], 0, MSG_A_PAD_MENU_0),
    ANALOG_CONFIG_OPTION(MSG[MSG_A_PAD_MENU_1], 1, MSG_A_PAD_MENU_1),
    ANALOG_CONFIG_OPTION(MSG[MSG_A_PAD_MENU_2], 2, MSG_A_PAD_MENU_2),
    ANALOG_CONFIG_OPTION(MSG[MSG_A_PAD_MENU_3], 3, MSG_A_PAD_MENU_3),

    STRING_SELECTION_OPTION(NULL, MSG[MSG_A_PAD_MENU_4], yes_no_options, &option_enable_analog, 2, MSG_A_PAD_MENU_HELP_0, 5, MSG_A_PAD_MENU_4),
    NUMERIC_SELECTION_OPTION(NULL, MSG[MSG_A_PAD_MENU_5], &option_analog_sensitivity, 10, MSG_A_PAD_MENU_HELP_1, 6, MSG_A_PAD_MENU_5),
    
    ACTION_OPTION(menu_analog_default, NULL, MSG[MSG_OPTION_MENU_DEFAULT], MSG_OPTION_MENU_HELP_DEFAULTKEYBIND, 8, MSG_OPTION_MENU_DEFAULT),

    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_A_PAD_MENU_6], MSG_A_PAD_MENU_HELP_2, 10, MSG_A_PAD_MENU_6)
  };

  MAKE_MENU(analog_config, NULL, NULL);

  MenuOptionType link_options[] =
  {
    ACTION_OPTION(NULL, NULL, link_status_line, MSG_LINK_MENU_HELP_STATUS, 0, 0),
    ACTION_OPTION(NULL, NULL, link_peer_line, MSG_LINK_MENU_HELP_STATUS, 1, 0),
    ACTION_OPTION(NULL, NULL, link_detail_line, MSG_LINK_MENU_HELP_STATUS, 2, 0),

    ACTION_OPTION(menu_link_host_action, NULL, MSG[MSG_LINK_MENU_CREATE], MSG_LINK_MENU_HELP_CREATE, 4, MSG_LINK_MENU_CREATE),
    ACTION_OPTION(menu_link_join_action, NULL, MSG[MSG_LINK_MENU_JOIN], MSG_LINK_MENU_HELP_JOIN, 5, MSG_LINK_MENU_JOIN),
    ACTION_OPTION(menu_link_disconnect_action, NULL, MSG[MSG_LINK_MENU_DISCONNECT], MSG_LINK_MENU_HELP_DISCONNECT, 7, MSG_LINK_MENU_DISCONNECT),

    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 11, MSG_OPTION_MENU_11)
  };

  link_options[0].behavior_flags = MENU_OPTION_PASSIVE_ROW;
  link_options[1].behavior_flags = MENU_OPTION_PASSIVE_ROW;
  link_options[2].behavior_flags = MENU_OPTION_PASSIVE_ROW;

  MAKE_MENU(link, NULL, NULL);

  MenuOptionType game_options[] =
  {
    SUBMENU_OPTION(&cheats_misc_menu, MSG[MSG_MAIN_MENU_CHEAT], MSG_MAIN_MENU_HELP_CHEAT, 0, MSG_MAIN_MENU_CHEAT),
    ACTION_OPTION(menu_show_game_txt_debug, NULL, MSG[MSG_MAIN_MENU_GAMECONFIG], MSG_MAIN_MENU_HELP_GAMECONFIG, 1, MSG_MAIN_MENU_GAMECONFIG),
    ACTION_OPTION(NULL, NULL, MSG[MSG_MAIN_MENU_7], MSG_MAIN_MENU_HELP_7, 3, MSG_MAIN_MENU_7),
    ACTION_OPTION(NULL, NULL, MSG[MSG_MAIN_MENU_8], MSG_MAIN_MENU_HELP_8, 4, MSG_MAIN_MENU_8),
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 6, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(game, NULL, NULL);

  MenuOptionType audio_options[] =
  {
    STRING_SELECTION_OPTION(NULL, MSG[MSG_OPTION_MENU_6], sound_volume_options, &option_sound_volume, 11, MSG_OPTION_MENU_HELP_6, 0, MSG_OPTION_MENU_6),
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 2, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(audio, NULL, NULL);

  MenuOptionType controls_options[] =
  {
    SUBMENU_OPTION(&gamepad_config_menu, MSG[MSG_MAIN_MENU_5], MSG_MAIN_MENU_HELP_5, 0, MSG_MAIN_MENU_5),
    SUBMENU_OPTION(&analog_config_menu, MSG[MSG_MAIN_MENU_6], MSG_MAIN_MENU_HELP_6, 1, MSG_MAIN_MENU_6),
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 3, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(controls, NULL, NULL);

  MenuOptionType diagnostics_options[] =
  {
    ACTION_OPTION(NULL, NULL, diagnostics_status_line, MSG_DIAGNOSTICS_HELP_STATUS, 0, 0),
    ACTION_OPTION(NULL, NULL, diagnostics_report_line, MSG_DIAGNOSTICS_HELP_STATUS, 1, 0),
    ACTION_OPTION(menu_diagnostics_start, NULL, MSG[MSG_DIAGNOSTICS_START], MSG_DIAGNOSTICS_HELP_START, 3, MSG_DIAGNOSTICS_START),
    ACTION_OPTION(menu_diagnostics_stop, NULL, MSG[MSG_DIAGNOSTICS_STOP], MSG_DIAGNOSTICS_HELP_STOP, 4, MSG_DIAGNOSTICS_STOP),
    ACTION_OPTION(menu_diagnostics_write_report, NULL, MSG[MSG_DIAGNOSTICS_SAVE_REPORT], MSG_DIAGNOSTICS_HELP_SAVE_REPORT, 5, MSG_DIAGNOSTICS_SAVE_REPORT),
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
    ACTION_OPTION(NULL, NULL, diagnostics_trace_status_line, MSG_DIAGNOSTICS_HELP_STATUS, 8, 0),
    ACTION_OPTION(menu_trace_start, NULL, MSG[MSG_TRACE_START], MSG_TRACE_HELP_START, 10, MSG_TRACE_START),
    ACTION_OPTION(menu_trace_finish_save, NULL, MSG[MSG_TRACE_FINISH_SAVE], MSG_TRACE_HELP_FINISH_SAVE, 11, MSG_TRACE_FINISH_SAVE),
    ACTION_OPTION(menu_trace_replay, NULL, MSG[MSG_TRACE_REPLAY], MSG_TRACE_HELP_REPLAY, 12, MSG_TRACE_REPLAY),
    ACTION_OPTION(menu_trace_cancel, NULL, MSG[MSG_TRACE_CANCEL], MSG_TRACE_HELP_CANCEL, 13, MSG_TRACE_CANCEL),
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 15, MSG_OPTION_MENU_11)
#else
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 7, MSG_OPTION_MENU_11)
#endif
  };

  diagnostics_options[0].behavior_flags = MENU_OPTION_PASSIVE_ROW;
  diagnostics_options[1].behavior_flags = MENU_OPTION_PASSIVE_ROW;
  diagnostics_options[1].visible_function = menu_diagnostics_report_visible;
  diagnostics_options[2].available_function = menu_diagnostics_start_available;
  diagnostics_options[3].available_function = menu_diagnostics_stop_available;
  diagnostics_options[4].available_function = menu_diagnostics_report_available;
  diagnostics_options[2].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  diagnostics_options[3].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  diagnostics_options[4].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
#ifdef ENABLE_DIAGNOSTIC_INPUT_TRACE
  diagnostics_options[5].behavior_flags = MENU_OPTION_PASSIVE_ROW;
  diagnostics_options[6].available_function = menu_trace_start_available;
  diagnostics_options[7].available_function = menu_trace_finish_available;
  diagnostics_options[8].available_function = menu_trace_replay_available;
  diagnostics_options[9].available_function = menu_trace_cancel_available;
  diagnostics_options[6].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  diagnostics_options[7].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  diagnostics_options[8].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  diagnostics_options[9].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
#endif

  MAKE_MENU(diagnostics, NULL, NULL);

  MenuOptionType exit_options[] =
  {
    ACTION_OPTION(NULL, NULL, MSG[MSG_MAIN_MENU_10], MSG_MAIN_MENU_HELP_10, 0, MSG_MAIN_MENU_10),
    ACTION_OPTION(menu_quit_action, NULL, MSG[MSG_MAIN_MENU_11], MSG_MAIN_MENU_HELP_11, 2, MSG_MAIN_MENU_11),
    ACTION_SUBMENU_OPTION(NULL, NULL, MSG[MSG_OPTION_MENU_11], MSG_OPTION_MENU_HELP_11, 4, MSG_OPTION_MENU_11)
  };

  MAKE_MENU(exit, NULL, NULL);

  MenuOptionType main_options[] =
  {
    ACTION_OPTION(NULL, NULL, MSG[MSG_CATEGORY_CONTINUE], MSG_CATEGORY_HELP_CONTINUE, 0, MSG_CATEGORY_CONTINUE),
    SUBMENU_OPTION(&savestate_menu, MSG[MSG_CATEGORY_SAVE_LOAD], MSG_CATEGORY_HELP_SAVE_LOAD, 1, MSG_CATEGORY_SAVE_LOAD),
    SUBMENU_OPTION(&game_menu, MSG[MSG_CATEGORY_GAME], MSG_CATEGORY_HELP_GAME, 2, MSG_CATEGORY_GAME),
    SUBMENU_OPTION(&graphics_menu, MSG[MSG_CATEGORY_VIDEO], MSG_CATEGORY_HELP_VIDEO, 3, MSG_CATEGORY_VIDEO),
    SUBMENU_OPTION(&audio_menu, MSG[MSG_CATEGORY_AUDIO], MSG_CATEGORY_HELP_AUDIO, 4, MSG_CATEGORY_AUDIO),
    SUBMENU_OPTION(&emulator_menu, MSG[MSG_CATEGORY_PERFORMANCE], MSG_CATEGORY_HELP_PERFORMANCE, 5, MSG_CATEGORY_PERFORMANCE),
    SUBMENU_OPTION(&controls_menu, MSG[MSG_CATEGORY_CONTROLS], MSG_CATEGORY_HELP_CONTROLS, 6, MSG_CATEGORY_CONTROLS),
    SUBMENU_OPTION(&link_menu, MSG[MSG_CATEGORY_LINK_CABLE], MSG_CATEGORY_HELP_LINK_CABLE, 7, MSG_CATEGORY_LINK_CABLE),
    SUBMENU_OPTION(&theme_menu, MSG[MSG_CATEGORY_INTERFACE], MSG_CATEGORY_HELP_INTERFACE, 8, MSG_CATEGORY_INTERFACE),
    SUBMENU_OPTION(&diagnostics_menu, MSG[MSG_CATEGORY_DIAGNOSTICS], MSG_CATEGORY_HELP_DIAGNOSTICS, 9, MSG_CATEGORY_DIAGNOSTICS),
    SUBMENU_OPTION(&exit_menu, MSG[MSG_CATEGORY_EXIT], MSG_CATEGORY_HELP_EXIT, 10, MSG_CATEGORY_EXIT)
  };

  main_options[0].available_function = menu_game_available;
  main_options[0].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  game_options[0].available_function = menu_game_available;
  game_options[1].available_function = menu_game_available;
  game_options[3].available_function = menu_game_available;
  game_options[0].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  game_options[1].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;
  game_options[3].unavailable_string = MSG_UNAVAILABLE_CURRENT_STATE;


  MAKE_MENU(main, NULL, NULL);

  /* Populate global menu array for language refresh */
  all_menus[0] = &main_menu;
  all_menus[1] = &graphics_menu;
  all_menus[2] = &emulator_menu;
  all_menus[3] = &gamepad_config_menu;
  all_menus[4] = &analog_config_menu;
  all_menus[5] = &theme_menu;
  all_menus[6] = &custom_colors_menu;
  all_menus[7] = &savestate_menu;
  all_menus[8] = &cheats_misc_menu;
  all_menus[9] = &link_menu;
  all_menus[10] = &game_menu;
  all_menus[11] = &audio_menu;
  all_menus[12] = &controls_menu;
  all_menus[13] = &diagnostics_menu;
  all_menus[14] = &exit_menu;
  all_menus[15] = &performance_advanced_menu;
  num_all_menus = MAX_MENUS;

  savestate_menu.parent = &main_menu;
  graphics_menu.parent = &main_menu;
  emulator_menu.parent = &main_menu;
  game_menu.parent = &main_menu;
  audio_menu.parent = &main_menu;
  controls_menu.parent = &main_menu;
  link_menu.parent = &main_menu;
  theme_menu.parent = &main_menu;
  diagnostics_menu.parent = &main_menu;
  exit_menu.parent = &main_menu;
  cheats_misc_menu.parent = &game_menu;
  gamepad_config_menu.parent = &controls_menu;
  analog_config_menu.parent = &controls_menu;
  custom_colors_menu.parent = &theme_menu;
  performance_advanced_menu.parent = &emulator_menu;

  menu_refresh_language();

  /* Switch screens; NULL means back to the parent menu. */
  void choose_menu(MenuType *new_menu)
  {
    if (new_menu == NULL)
      new_menu = current_menu != NULL && current_menu->parent != NULL ?
       current_menu->parent : &main_menu;

    current_menu = new_menu;
    build_legacy_menu_model(&navigation_model, navigation_rows, new_menu);
    current_option_num = navigation_model.selected_index < new_menu->num_options ?
     (u32)navigation_model.selected_index : 0;
    current_option = new_menu->options + current_option_num;

    /* The PSP OS exit dialog is allowed on every menu screen; menu()
       turns it off again before returning to the game. */
    sceImposeSetHomePopup(1); // enable in all menus but not game
  }


  main_link_set_gameplay_active(0);
  sound_pause = 1;


  current_screen = copy_screen();
  savestate_screen = (u16 *)safe_malloc(GBA_SCREEN_SIZE);

  if (gamepak_filename[0] == 0)
    gamepak_file_none();
  else
    change_ext(gamepak_filename, game_title, "");

  screen_image_ptr = current_screen;

  load_savestate_timestamps();

  /* Close the streamed ROM file while the menu runs; the exit path
     sees GAMEPAK_FILE_MENU_CLOSED and reopens it (gamepak_file_reopen). */
  if (FILE_CHECK_VALID(gamepak_file_large))
  {
    FILE_CLOSE(gamepak_file_large);
    gamepak_file_large = GAMEPAK_FILE_MENU_CLOSED;
  }


  refresh_cheat_format_strings();
  reload_cheats_page();

  video_resolution_large();
  if(!main_link_active())
    set_cpu_clock(PSP_CLOCK_222);
  choose_menu(&main_menu);

  /* One iteration per frame: draw the screen, then handle one input. */
  while (repeat)
  {
    /* Redraw the "no game" placeholder in the newly selected language. */
    if (first_load && preview_language != option_language)
    {
      memset(current_screen, 0, GBA_SCREEN_SIZE);
      print_string_ext_gbk(MSG[MSG_NON_LOAD_GAME], X_POS_CENTER, 74,
       COLOR15_WHITE, BG_NO_FILL, current_screen, GBA_SCREEN_WIDTH);
      screen_image_ptr = current_screen;
      preview_language = option_language;
    }

    clear_screen(COLOR15_TO_32(color_bg));

    if ((counter % 30) == 0)
	{
	  update_status_string_gbk(time_str, batt_str, &color_batt_life);
	}
    counter++;
	print_string_gbk(time_str, TIME_STATUS_POS_X, 2, color_help_text, BG_NO_FILL);

	print_string_gbk(batt_str, BATT_STATUS_POS_X, 2, color_batt_life, BG_NO_FILL);

    {
        u32 title_hash = hash_string(game_title);
        update_title_scroll(title_hash);
        s32 off = compute_scroll_offset(g_scroll.title_tick, strlen(game_title), SCROLL_TITLE_MAX_CHARS);
        print_string_scroll(game_title, 228, 28, color_inactive_item, BG_NO_FILL, SCROLL_TITLE_MAX_CHARS, off);
    }

    /* In Theme or Custom Colors menus, show live theme preview instead of game screenshot */
    if (current_menu == &custom_colors_menu || current_menu == &theme_menu)
    {
        draw_theme_preview(0xFFFF);  /* 0xFFFF = no specific color being edited */
        draw_box_line(SCREEN_IMAGE_POS_X - 1, SCREEN_IMAGE_POS_Y - 1,
                      SCREEN_IMAGE_POS_X + GBA_SCREEN_WIDTH, SCREEN_IMAGE_POS_Y + GBA_SCREEN_HEIGHT,
                      color_inactive_item);
    }
    else
    {
        blit_to_screen(screen_image_ptr, GBA_SCREEN_WIDTH, GBA_SCREEN_HEIGHT, SCREEN_IMAGE_POS_X, SCREEN_IMAGE_POS_Y);
        // --- Border around game screenshot ---
        draw_box_line(SCREEN_IMAGE_POS_X - 1, SCREEN_IMAGE_POS_Y - 1,
                      SCREEN_IMAGE_POS_X + GBA_SCREEN_WIDTH, SCREEN_IMAGE_POS_Y + GBA_SCREEN_HEIGHT,
                      color_inactive_item);
        // --- End border ---
    }

    if (current_menu && current_menu->init_function != NULL)
    {
      current_menu->init_function();
    }

    if(menu_model_reconcile(&navigation_model) &&
       navigation_model.selected_index < current_menu->num_options)
    {
      current_option_num = (u32)navigation_model.selected_index;
      current_menu->selected_option = current_option_num;
      current_option = current_menu->options + current_option_num;
    }

    main_link_pump();

    if (current_menu == &savestate_menu)
      submenu_savestate();
    if (current_menu == &graphics_menu)
      submenu_graphics();
    if (current_menu == &theme_menu)
      submenu_theme();
    if (current_menu == &custom_colors_menu)
      submenu_custom_colors();
    if (current_menu == &emulator_menu ||
        current_menu == &performance_advanced_menu)
      submenu_emulator();
    if (current_menu == &gamepad_config_menu)
      submenu_gamepad();
    if (current_menu == &analog_config_menu)
      submenu_analog();
    if (current_menu == &cheats_misc_menu)
      submenu_cheats_misc();
    if (current_menu == &link_menu)
      submenu_link();
    if (current_menu == &game_menu)
      submenu_game();
    if (current_menu == &audio_menu)
      submenu_audio();
    if (current_menu == &controls_menu)
      submenu_controls();
    if (current_menu == &diagnostics_menu)
      submenu_diagnostics();
    if (current_menu == &exit_menu)
      submenu_exit();
    if (current_menu == &main_menu)
      submenu_main();

    if (current_menu && current_menu->options)
    {
      MenuViewport options_viewport = menu_viewport_for_selection(
       current_option != NULL ? current_option->line_number : 0,
       MENU_LIST_LINES);
      display_option = current_menu->options;
      u32 menu_hash = ((u32)current_menu) ^ current_option_num;
      update_menu_scroll(menu_hash);

      for (i = 0; i < current_menu->num_options; i++, display_option++)
      {
        u16 option_color;
        u32 option_y;

        if(!menu_option_visible(display_option))
          continue;
        if(!menu_viewport_baseline(&options_viewport,
                                   display_option->line_number,
                                   MENU_LIST_POS_Y, FONTHEIGHT, &option_y))
          continue;

        option_color = menu_option_available(display_option) ?
         color_inactive_item : color_inactive_dir;
        if (display_option == current_option) {
            /* Compute scroll offset based on VALUE length, not label+value */
            const char *fmt = display_option->display_string;
            const char *spec_s = (fmt != NULL && display_option->current_option != NULL) ? strstr(fmt, "%s") : NULL;
            const char *spec_d = (fmt != NULL && display_option->current_option != NULL) ? strstr(fmt, "%d") : NULL;
            u32 value_len = 0;
            u32 label_chars = 0;

            if (spec_s != NULL) {
                /* String option — look up choice */
                label_chars = (u32)(spec_s - fmt);
                const char **choices = (const char **)display_option->options;
                u32 idx = *(display_option->current_option);
                if (choices != NULL && idx < display_option->num_options && choices[idx] != NULL)
                    value_len = strlen(choices[idx]);
            } else if (spec_d != NULL) {
                /* Numeric option — format the number */
                label_chars = (u32)(spec_d - fmt);
                char num_buf[16];
                sprintf(num_buf, "%lu", *(display_option->current_option));
                value_len = strlen(num_buf);
            } else {
                /* No format spec — use full string */
                value_len = (fmt != NULL) ? strlen(fmt) : 0;
            }

            u32 value_max = (SCROLL_MENU_MAX_CHARS > label_chars) ? (SCROLL_MENU_MAX_CHARS - label_chars) : 0;
            s32 off = compute_scroll_offset(g_scroll.menu_tick, value_len, value_max);
            print_menu_option_split(display_option, MENU_LIST_POS_X,
                            (s16)option_y,
                            menu_option_available(display_option) ?
                             color_active_item : color_tooltip_text,
                            BG_NO_FILL, SCROLL_MENU_MAX_CHARS, off);
        } else {            print_menu_option_split(display_option, MENU_LIST_POS_X,
                            (s16)option_y,
                            option_color, BG_NO_FILL, SCROLL_MENU_MAX_CHARS, -1);
        }
      }
    }

  {
    char guide_line[192];
    const char *summary;
    u32 impact_message;
    u32 guide_max_chars = (u32)menu_layout_max_columns(
     PSP_SCREEN_WIDTH, MENU_LIST_POS_X, FONTWIDTH);
    u32 footer_max_chars = (u32)menu_layout_max_columns(
     PSP_SCREEN_WIDTH, 30, FONTWIDTH);
    u16 summary_color = color_tooltip_text;

    if(!menu_option_available(current_option) &&
       current_option->unavailable_string != 0)
    {
      summary = MSG[current_option->unavailable_string];
      summary_color = color_batt_low;
    }
    else if(current_option->tooltip_string != 0)
      summary = MSG[current_option->tooltip_string];
    else
      summary = MSG[current_option->help_string];

    snprintf(guide_line, sizeof(guide_line), "%s%s",
     MSG[MSG_GUIDE_SUMMARY_PREFIX], summary);
    print_menu_line_clipped(guide_line, MENU_LIST_POS_X,
     TEXT_TOOLTIP_POS_Y, summary_color, BG_NO_FILL, guide_max_chars);

    print_menu_line_clipped(MSG[MSG_GUIDE_RECOMMEND_DEFAULT],
     MENU_LIST_POS_X, TEXT_TOOLTIP_POS_Y + FONTHEIGHT,
     color_tooltip_text, BG_NO_FILL, guide_max_chars);

    if((current_option->option_type & (NUMBER_SELECTION_OPTION |
                                      STRING_SELECTION_OPTION)) != 0)
      impact_message = MSG_GUIDE_IMPACT_VALUE;
    else if((current_option->option_type & SUBMENU_OPTION) != 0)
      impact_message = MSG_GUIDE_IMPACT_SUBMENU;
    else
      impact_message = MSG_GUIDE_IMPACT_ACTION;

    snprintf(guide_line, sizeof(guide_line), "%s   %s",
     MSG[impact_message], MSG[MSG_GUIDE_RESTART_NONE]);
    print_menu_line_clipped(guide_line, MENU_LIST_POS_X,
     TEXT_TOOLTIP_POS_Y + (FONTHEIGHT * 2), color_tooltip_text,
     BG_NO_FILL, guide_max_chars);

    print_swap_aware_clipped(MSG[current_option->help_string], 30, 258,
     color_help_text, BG_NO_FILL, footer_max_chars);
  }

    // PSP controller - hold
    if (get_pad_input(PSP_CTRL_HOLD) != 0)
      print_string(FONT_KEY_ICON, 6, 258, color_batt_low, BG_NO_FILL);

    __draw_volume_status(1);
    flip_screen(1);


    /* Triangle: delete savestate when in savestate menu */
    if (current_menu == &savestate_menu && !first_load && current_option_num < SAVESTATE_SLOT_COUNT)
    {
      if (get_pad_input(PSP_CTRL_TRIANGLE) != 0)
      {
        /* Wait for release BEFORE opening the dialog, so the held
           button doesn't leak into yesno_dialog's input loop. */
        while (get_pad_input(PSP_CTRL_TRIANGLE) != 0)
          sceKernelDelayThread(5000);
        menu_delete_state();
      }
    }

    gui_action = get_gui_input();

    switch (gui_action)
    {
      case CURSOR_DOWN:
        if(menu_model_move(&navigation_model, 1))
        {
          current_option_num = (u32)navigation_model.selected_index;
          current_menu->selected_option = current_option_num;
          current_option = current_menu->options + current_option_num;
        }
        break;

      case CURSOR_UP:
        if(menu_model_move(&navigation_model, -1))
        {
          current_option_num = (u32)navigation_model.selected_index;
          current_menu->selected_option = current_option_num;
          current_option = current_menu->options + current_option_num;
        }
        break;

      case CURSOR_RIGHT:
        if(!menu_option_available(current_option))
          break;
        if ((current_option->option_type & (NUMBER_SELECTION_OPTION | STRING_SELECTION_OPTION)) != 0)
        {
          *(current_option->current_option) = (*current_option->current_option + 1) % current_option->num_options;

          if (current_option->passive_function != NULL)
            current_option->passive_function();
        }
        break;

      case CURSOR_LEFT:
        if(!menu_option_available(current_option))
          break;
        if ((current_option->option_type & (NUMBER_SELECTION_OPTION | STRING_SELECTION_OPTION)) != 0)
        {
          u32 current_option_val = *(current_option->current_option);

          if (current_option_val != 0)
            current_option_val--;
          else
            current_option_val = current_option->num_options - 1;

          *(current_option->current_option) = current_option_val;

          if (current_option->passive_function != NULL)
            current_option->passive_function();
        }
        break;

      /* Shoulder shortcuts: R opens the savestate menu, L opens the file
         browser that fits the current menu. */
      case CURSOR_RTRIGGER:
        if (current_menu == &main_menu)
        {
          menu_init();
          choose_menu(&savestate_menu);
        }
        break;

      case CURSOR_LTRIGGER:
        if (current_menu == &main_menu)
          menu_load_file();
        if (current_menu == &savestate_menu)
          menu_load_state_file();
        if (current_menu == &cheats_misc_menu)
          menu_load_cheat_file();
        break;

      case CURSOR_DEFAULT:
	  {
	}
        break;

      case CURSOR_EXIT:
        if (current_menu == &main_menu)
        {
          menu_exit();
        }
        else if (current_menu == &custom_colors_menu)
        {
          menu_init();
          choose_menu(&theme_menu);
        }
        else if (current_menu == &link_menu)
        {
          menu_init();
          choose_menu(&main_menu);
        }
        else
        {
          menu_init();
          choose_menu(current_menu->parent);
        }
        break;

      case CURSOR_SELECT:
        if(!menu_option_available(current_option))
          break;
        switch (current_option->option_type & (ACTION_OPTION | SUBMENU_OPTION))
        {
          case (ACTION_OPTION | SUBMENU_OPTION):
            if (current_option->action_function != NULL)
            {
              if (current_menu == &custom_colors_menu)
                color_picker_item_index = current_option_num;
              current_option->action_function();
            }
            choose_menu(current_option->sub_menu);
            break;

          case ACTION_OPTION:
            if (current_option->action_function != NULL)
            {
              /* For Custom Colors menu, pass the selected item index to file-scope picker */
              if (current_menu == &custom_colors_menu)
                color_picker_item_index = current_option_num;
              current_option->action_function();
            }
            else
            {
              // Handle menu actions inline to avoid corrupted function pointers
              // Check menu context since line numbers are per-menu
              if (current_menu == &main_menu)
              {
                if(current_option->line_number == 0)
                  menu_exit();
              }
              else if(current_menu == &game_menu)
              {
                if(current_option->line_number == 3)
                  menu_load_file();
                else if(current_option->line_number == 4)
                  menu_reset();
              }
              else if(current_menu == &exit_menu)
              {
                if(current_option->line_number == 0)
                  menu_suspend();
              }
              else if (current_menu == &savestate_menu)
              {
                switch (current_option->line_number)
                {
                  case 12: // Load State File
                    menu_load_state_file();
                    break;
                  default:
                    if (current_option->line_number < SAVESTATE_SLOT_COUNT)
                    {
                      /* Back up the current progress to the auto slot
                         before loading a numbered slot over it. */
                      if (savestate_action == 0 && current_option->line_number < AUTO_SAVESTATE_SLOT)
                      {
                        if (savestate_file_exists(current_option->line_number))
                        {
                          action_savestate_slot(AUTO_SAVESTATE_SLOT);
                          get_savestate_filename(AUTO_SAVESTATE_SLOT, filename_buffer);
                          get_savestate_info(filename_buffer, NULL, line_buffer);
                          sprintf(savestate_timestamps[AUTO_SAVESTATE_SLOT], "AUTO: %s", line_buffer);
                        }
                      }
                      if ((savestate_action != 0 ? menu_save_state() : menu_load_state()) != 0)
                      {
                        return_value = 1;
                        repeat = 0;
                      }
                    }
                    break;
                }
              }
              else if (current_menu == &cheats_misc_menu)
              {
                switch (current_option->line_number)
                {
                  case 13: // Load Cheat File
                    menu_load_cheat_file();
                    break;
                  default:
                    break;
                }
              }
            }
            break;

          case SUBMENU_OPTION:
            if(current_option->sub_menu == &savestate_menu)
              menu_init();
            choose_menu(current_option->sub_menu);
            break;

          default:
            break;
        }
        break;

      case CURSOR_BACK:
        /* Square: reset current color to preset default in Custom Colors list */
        if (current_menu == &custom_colors_menu)
        {
          if (current_option_num < NUM_COLOR_ITEMS)
          {
            menu_reset_single_color(current_option_num);
            break;
          }
        }
        /* fall through for other menus */
      case CURSOR_NONE:
        break;
    }

  } /* end while */

  /* Keep menu presence visible until all blocking resume work is complete. */
  main_link_pump();

  /* Restore HOME to emulator menu mode (disable OS popup) */
  sceImposeSetHomePopup(0);

  scePowerLock(0);

  if (gamepak_file_large == GAMEPAK_FILE_MENU_CLOSED)
    gamepak_file_reopen();

  /* Wait for every button up to HOME (PSP_CTRL bits 0-16) to be released so
     the press that closed the menu does not reach the game. */
  while (get_pad_input(0x0001FFFF) != 0);

  menu_term();

  set_sound_volume();
  if(!main_link_active())
    set_cpu_clock(option_clock_speed);

  sceDisplayWaitVblankStart();
  video_resolution_small();

  sound_pause = 0;

  scePowerUnlock(0);

  main_link_set_gameplay_active(1);
  main_link_pump();

  return return_value;
}


/*-----------------------------------------------------------------------------
  Status bar
-----------------------------------------------------------------------------*/



/* Format the clock and the battery text (icon, percent, then "charging" or
   the remaining h:mm) and pick the battery colour. Buffers are 40 bytes. */
static void update_status_string_gbk(char *time_str, char *batt_str, u16 *color_batt)
{
  ScePspDateTime current_time = { 0 };

  u32 i = 0;
  int batt_life_per;
  int batt_life_time;

  char batt_icon[4][4] =
  {
    FONT_BATTERY0_GBK "\0", // empty
    FONT_BATTERY1_GBK "\0",
    FONT_BATTERY2_GBK "\0",
    FONT_BATTERY3_GBK "\0", // full
  };

  sceRtcGetCurrentClockLocalTime(&current_time);
  int day_of_week = sceRtcGetDayOfWeek(current_time.year, current_time.month, current_time.day);

  get_timestamp_string(time_str, MSG_MENU_DATE_FMT_0, &current_time, day_of_week);


  batt_life_per = scePowerGetBatteryLifePercent();

  if (batt_life_per < 0)
  {
    sprintf(batt_str, "%3s --%%", batt_icon[0]);
  }
  else
  {
    if (batt_life_per > 66)      i = 3;
    else if (batt_life_per > 33) i = 2;
    else if (batt_life_per >  9) i = 1;
    else                         i = 0;

    sprintf(batt_str, "%3s%3d%%", batt_icon[i], batt_life_per);
  }

  if (scePowerIsPowerOnline() == 1)
  {
    char tmp[40];
    sprintf(tmp, "%s%s", batt_str, MSG[MSG_CHARGE]);
    strcpy(batt_str, tmp);
  }
  else
  {
    batt_life_time = scePowerGetBatteryLifeTime();

    if (batt_life_time < 0)
    {
      char tmp[40];
      sprintf(tmp, "%s%s", batt_str, "[--:--]");
      strcpy(batt_str, tmp);
    }
    else
    {
      char tmp[40];
      sprintf(tmp, "%s[%2d:%02d]", batt_str, (batt_life_time / 60) % 100, batt_life_time % 60);
      strcpy(batt_str, tmp);
    }
  }

  if (scePowerIsBatteryCharging() == 1)
  {
    *color_batt = color_batt_charg;
  }
  else
  {
    if (scePowerIsLowBattery() == 1)
      *color_batt = color_batt_low;
    else
      *color_batt = color_batt_normal;
  }
}


/* date_format is the PSP system setting. Date format messages come in
   pairs: msg_id is year-first, msg_id + 1 takes the other two orders. */
static void get_timestamp_string(char *buffer, u16 msg_id, ScePspDateTime *msg_time, int day_of_week)
{
  const char *week_str[] =
  {
    MSG[MSG_DAYW_0], MSG[MSG_DAYW_1], MSG[MSG_DAYW_2], MSG[MSG_DAYW_3], MSG[MSG_DAYW_4], MSG[MSG_DAYW_5], MSG[MSG_DAYW_6], ""
  };

  switch (date_format)
  {
    case 0: // DATE_FORMAT_YYYYMMDD
      sprintf(buffer, MSG[msg_id + 0], msg_time->year, msg_time->month, msg_time->day, week_str[day_of_week], msg_time->hour, msg_time->minute, msg_time->second, (msg_time->microsecond / 1000));
      break;
    case 1: // DATE_FORMAT_MMDDYYYY
      sprintf(buffer, MSG[msg_id + 1], msg_time->month, msg_time->day, msg_time->year, week_str[day_of_week], msg_time->hour, msg_time->minute, msg_time->second, (msg_time->microsecond / 1000));
      break;
    case 2: // DATE_FORMAT_DDMMYYYY
      sprintf(buffer, MSG[msg_id + 1], msg_time->day, msg_time->month, msg_time->year, week_str[day_of_week], msg_time->hour, msg_time->minute, msg_time->second, (msg_time->microsecond / 1000));
      break;
  }
}


/*-----------------------------------------------------------------------------
  Save Config Files
-----------------------------------------------------------------------------*/

/* ConfigStoreIo callbacks: config_store.c's save-to-temp, validate, rename
   sequence carried out with sceIo calls. Each returns nonzero on success. */

/* Fails if the file holds more than capacity bytes. */
static int config_io_read(void *context, const char *path, uint8_t *data,
 size_t capacity, size_t *size)
{
  SceUID file;
  size_t total = 0;
  u8 extra;
  s32 result;

  (void)context;
  FILE_OPEN(file, path, READ);
  if(!FILE_CHECK_VALID(file))
    return 0;

  while(total < capacity)
  {
    result = FILE_READ(file, data + total, capacity - total);
    if(result < 0)
    {
      FILE_CLOSE(file);
      return 0;
    }
    if(result == 0)
      break;
    total += (size_t)result;
  }
  result = FILE_READ(file, &extra, 1);
  FILE_CLOSE(file);
  if(result != 0)
    return 0;
  *size = total;
  return 1;
}

static int config_io_write(void *context, const char *path,
 const uint8_t *data, size_t size)
{
  SceUID file;
  size_t total = 0;

  (void)context;
  FILE_OPEN(file, path, WRITE);
  if(!FILE_CHECK_VALID(file))
    return 0;
  while(total < size)
  {
    s32 result = FILE_WRITE(file, data + total, size - total);
    if(result <= 0)
    {
      FILE_CLOSE(file);
      return 0;
    }
    total += (size_t)result;
  }
  FILE_CLOSE(file);
  return 1;
}

/* Sync the device that holds main_path, e.g. "ms0:". */
static int config_io_flush(void *context)
{
  char device[8];
  const char *separator;
  size_t length;

  (void)context;
  separator = strchr(main_path, ':');
  if(separator == NULL)
    return 0;
  length = (size_t)(separator - main_path) + 1;
  if(length >= sizeof(device))
    return 0;
  memcpy(device, main_path, length);
  device[length] = '\0';
  return sceIoSync(device, 0) >= 0;
}

static int config_io_move(void *context, const char *source,
 const char *target)
{
  (void)context;
  return sceIoRename(source, target) >= 0;
}

/* Removing a file that does not exist counts as success. */
static int config_io_remove(void *context, const char *path)
{
  SceIoStat stat;

  (void)context;
  if(sceIoGetstat(path, &stat) < 0)
    return 1;
  return sceIoRemove(path) >= 0;
}

static int config_io_exists(void *context, const char *path)
{
  SceIoStat stat;

  (void)context;
  return sceIoGetstat(path, &stat) >= 0;
}

static const ConfigStoreIo config_store_io =
{
  NULL,
  config_io_read,
  config_io_write,
  config_io_flush,
  config_io_move,
  config_io_remove,
  config_io_exists
};

/* tempgba.cfg plus its .tmp (write target) and .bak (last good) siblings. */
static int config_paths(char *primary, char *temporary, char *recovery)
{
  int result;

  result = snprintf(primary, MAX_PATH, "%s%s", main_path,
   GPSP_CONFIG_FILENAME);
  if(result < 0 || result >= MAX_PATH)
    return 0;
  result = snprintf(temporary, MAX_PATH, "%s.tmp", primary);
  if(result < 0 || result >= MAX_PATH)
    return 0;
  result = snprintf(recovery, MAX_PATH, "%s.bak", primary);
  return result >= 0 && result < MAX_PATH;
}

/* Global settings <-> ConfigModel. The per-game fast forward override is
   never stored globally. */
static void capture_global_config(ConfigModel *model)
{
  u32 i;

  model->screen_scale = option_screen_scale;
  model->screen_magnification = option_screen_mag;
  model->screen_filter = option_screen_filter;
  model->show_fps = psp_fps_debug;
  model->frameskip_type = option_frameskip_type;
  model->frameskip_value = option_frameskip_value;
  model->clock_speed = option_clock_speed;
  model->sound_volume = option_sound_volume;
  model->stack_optimize = option_stack_optimize;
  model->ram_dynarec_policy = option_ram_dynarec_policy;
  model->video_renderer = option_video_renderer;
  model->oam_hijacking = option_oam_hijacking_enabled;
  model->boot_mode = option_boot_mode;
  model->update_backup = option_update_backup;
  model->screen_capture_format = option_screen_capture_format;
  model->enable_analog = option_enable_analog;
  model->analog_sensitivity = option_analog_sensitivity;
  model->language = option_language;
  model->hblank_window_start = option_hblank_irq_window_start;
  model->hblank_window_end = option_hblank_irq_window_end;
  model->psp_vsync = option_psp_vsync;
  model->swap_confirm_buttons = option_swap_confirm_buttons;
  model->theme = option_theme;
  model->fast_forward_profile = option_fast_forward_profile;
  model->fast_forward_game_override = 0;
  model->fast_forward_activation = option_fast_forward_activation;
  model->fast_forward_speed = option_fast_forward_speed;
  model->fast_forward_presentation = option_fast_forward_presentation;
  model->fast_forward_audio = option_fast_forward_audio;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    model->gamepad[i] = gamepad_config_map[i];
}

static void apply_global_config(const ConfigModel *model)
{
  u32 i;
  int has_menu_button = 0;

  option_screen_scale = model->screen_scale;
  option_screen_mag = model->screen_magnification;
  option_screen_filter = model->screen_filter;
  psp_fps_debug = model->show_fps;
  option_frameskip_type = model->frameskip_type;
  option_frameskip_value = model->frameskip_value;
  option_clock_speed = model->clock_speed;
  option_sound_volume = model->sound_volume;
  option_stack_optimize = model->stack_optimize;
  option_ram_dynarec_policy = model->ram_dynarec_policy;
  option_video_renderer = model->video_renderer;
  option_oam_hijacking_enabled = model->oam_hijacking;
  option_boot_mode = model->boot_mode;
  option_update_backup = model->update_backup;
  option_screen_capture_format = model->screen_capture_format;
  option_enable_analog = model->enable_analog;
  option_analog_sensitivity = model->analog_sensitivity;
  option_language = model->language;
  option_hblank_irq_window_start = model->hblank_window_start;
  option_hblank_irq_window_end = model->hblank_window_end;
  option_psp_vsync = model->psp_vsync;
  option_swap_confirm_buttons = model->swap_confirm_buttons;
  option_theme = model->theme;
  option_fast_forward_profile = model->fast_forward_profile;
  option_fast_forward_activation = model->fast_forward_activation;
  option_fast_forward_speed = model->fast_forward_speed;
  option_fast_forward_presentation = model->fast_forward_presentation;
  option_fast_forward_audio = model->fast_forward_audio;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
  {
    gamepad_config_map[i] = model->gamepad[i];
    if(gamepad_config_map[i] == BUTTON_ID_MENU)
      has_menu_button = 1;
  }
  /* Without HOME there must be a way into the menu: put it on Triangle. */
  if(!enable_home_menu && !has_menu_button)
    gamepad_config_map[0] = BUTTON_ID_MENU;
  rebuild_fast_forward_button_mask();
  apply_theme(option_theme);
  main_fast_forward_reset_game_settings();
}

static s32 save_global_config(void)
{
  ConfigModel model;
  char primary[MAX_PATH];
  char temporary[MAX_PATH];
  char recovery[MAX_PATH];

  if(!config_paths(primary, temporary, recovery))
    return -1;
  capture_global_config(&model);
  return config_store_save(&config_store_io, primary, temporary, recovery,
   &model) == CONFIG_STORE_OK ? 0 : -1;
}

static s32 load_global_config(void)
{
  ConfigModel model;
  char primary[MAX_PATH];
  char temporary[MAX_PATH];
  char recovery[MAX_PATH];
  int system_language;

  if(config_paths(primary, temporary, recovery) &&
     config_store_load(&config_store_io, primary, recovery, &model, NULL) ==
      CONFIG_STORE_OK)
  {
    apply_global_config(&model);
    return 0;
  }

  config_model_defaults(&model);
  if(sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,
                                 &system_language) >= 0)
  {
    if(system_language == PSP_SYSTEMPARAM_LANGUAGE_JAPANESE)
      model.language = 0;
    else if(system_language == PSP_SYSTEMPARAM_LANGUAGE_CHINESE_SIMPLIFIED)
      model.language = 2;
    else if(system_language == PSP_SYSTEMPARAM_LANGUAGE_CHINESE_TRADITIONAL)
      model.language = 3;
    else if(system_language == PSP_SYSTEMPARAM_LANGUAGE_ITALIAN)
      model.language = 4;
  }
  apply_global_config(&model);
  return -1;
}

static int config_companion_paths(const char *primary, char *temporary,
 char *recovery)
{
  int result;

  result = snprintf(temporary, MAX_PATH, "%s.tmp", primary);
  if(result < 0 || result >= MAX_PATH)
    return 0;
  result = snprintf(recovery, MAX_PATH, "%s.bak", primary);
  return result >= 0 && result < MAX_PATH;
}

/* Per-game files (<dir_cfg>/<rom>.cfg) use the same format but only carry
   video, frameskip, clock, volume, fast forward and button settings; the
   remaining fields are written as defaults and ignored on load. */
static void capture_game_config(ConfigModel *model)
{
  u32 i;

  config_model_defaults(model);
  model->screen_scale = option_screen_scale;
  model->screen_magnification = option_screen_mag;
  model->screen_filter = option_screen_filter;
  model->frameskip_type = option_frameskip_type;
  model->frameskip_value = option_frameskip_value;
  model->clock_speed = option_clock_speed;
  model->sound_volume = option_sound_volume;
  model->fast_forward_profile = option_game_fast_forward_profile;
  model->fast_forward_game_override = option_fast_forward_game_override;
  model->fast_forward_activation = option_game_fast_forward_activation;
  model->fast_forward_speed = option_game_fast_forward_speed;
  model->fast_forward_presentation = option_game_fast_forward_presentation;
  model->fast_forward_audio = option_game_fast_forward_audio;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
    model->gamepad[i] = gamepad_config_map[i];
}

static void apply_game_config(const ConfigModel *model)
{
  u32 i;
  int has_menu_button = 0;

  option_screen_scale = model->screen_scale;
  option_screen_mag = model->screen_magnification;
  option_screen_filter = model->screen_filter;
  option_frameskip_type = model->frameskip_type;
  option_frameskip_value = model->frameskip_value;
  option_clock_speed = model->clock_speed;
  option_sound_volume = model->sound_volume;
  option_fast_forward_game_override = model->fast_forward_game_override;
  option_game_fast_forward_profile = model->fast_forward_profile;
  option_game_fast_forward_activation = model->fast_forward_activation;
  option_game_fast_forward_speed = model->fast_forward_speed;
  option_game_fast_forward_presentation = model->fast_forward_presentation;
  option_game_fast_forward_audio = model->fast_forward_audio;
  for(i = 0; i < CONFIG_MODEL_GAMEPAD_COUNT; i++)
  {
    gamepad_config_map[i] = model->gamepad[i];
    if(gamepad_config_map[i] == BUTTON_ID_MENU)
      has_menu_button = 1;
  }
  if(!enable_home_menu && !has_menu_button)
    gamepad_config_map[0] = BUTTON_ID_MENU;
  rebuild_fast_forward_button_mask();
  main_fast_forward_apply_settings();
}

static s32 save_game_config_atomic(const char *primary)
{
  ConfigModel model;
  char temporary[MAX_PATH];
  char recovery[MAX_PATH];

  if(!config_companion_paths(primary, temporary, recovery))
    return -1;
  capture_game_config(&model);
  return config_store_save(&config_store_io, primary, temporary, recovery,
   &model) == CONFIG_STORE_OK ? 0 : -1;
}

static s32 load_game_config_atomic(const char *primary)
{
  ConfigModel model;
  char temporary[MAX_PATH];
  char recovery[MAX_PATH];

  if(!config_companion_paths(primary, temporary, recovery) ||
     config_store_load(&config_store_io, primary, recovery, &model, NULL) !=
      CONFIG_STORE_OK)
    return -1;
  apply_game_config(&model);
  return 0;
}

static int build_game_config_path(char *path)
{
  char filename[MAX_FILE];
  int length;

  if(gamepak_filename[0] == '\0')
    return 0;

  change_ext(gamepak_filename, filename, ".cfg");
  length = snprintf(path, MAX_PATH, "%s%s", dir_cfg, filename);
  return length >= 0 && length < MAX_PATH;
}

static s32 save_game_config_file(void)
{
  char game_config_path[MAX_PATH];
  s32 return_value;

  if(!build_game_config_path(game_config_path))
    return -1;

  scePowerLock(0);
  return_value = save_game_config_atomic(game_config_path);
  scePowerUnlock(0);
  return return_value;
}

s32 save_config_file(void)
{
  s32 return_value;

  save_game_config_file();

  scePowerLock(0);
  return_value = save_global_config();
  scePowerUnlock(0);
  return return_value;
}


/*-----------------------------------------------------------------------------
  Load Config Files
-----------------------------------------------------------------------------*/

s32 load_game_config_file(void)
{
  char game_config_path[MAX_PATH];

  if(!build_game_config_path(game_config_path))
    return -1;

  if(load_game_config_atomic(game_config_path) == 0)
    return 0;

  option_frameskip_type = FRAMESKIP_AUTO;
  option_frameskip_value = 9;
  option_clock_speed = PSP_CLOCK_333;
  main_fast_forward_reset_game_settings();
  return -1;
}

s32 load_config_file(void)
{
  return load_global_config();
}


/* Read the "name = path" directory settings. Relative paths are taken from
   main_path; a missing or unopenable directory falls back to main_path with
   an on-screen warning. Returns 0 if the file was read, -1 if it is absent
   (every directory is then main_path). */
s32 load_dir_cfg(char *file_name)
{
  char current_line[256];
  char current_variable[256];
  char current_value[256];

  const char item_roms[]  = "rom_directory";
  const char item_save[]  = "save_directory";
  const char item_state[] = "save_state_directory";
  const char item_cfg[]   = "game_config_directory";
  const char item_snap[]  = "snapshot_directory";
  const char item_cheat[] = "cheat_directory";
  const char item_boxart[] = "boxart_directory";
  const char item_boxart_folders[] = "folder_boxart_directory";

  FILE *dir_config;
  SceUID check_dir = -1;

  char str_buf[256];
  u32 str_line = 7;

  auto void add_launch_directory(void);
  auto void set_directory(char *dir_name, const char *item_name);
  auto void check_directory(char *dir_name, const char *item_name);

  void add_launch_directory(void)
  {
    if (strchr(current_value, ':') == NULL)
    {
      strcpy(str_buf, current_value);
      sprintf(current_value, "%s%s", main_path, str_buf);
    }

    if (current_value[strlen(current_value) - 1] != '/')
    {
      strcat(current_value, "/");
    }
  }

  void set_directory(char *dir_name, const char *item_name)
  {
    if (strcasecmp(current_variable, item_name) == 0)
    {
      if ((check_dir = sceIoDopen(current_value)) >= 0)
      {
        strcpy(dir_name, current_value);
        sceIoDclose(check_dir);
      }
      else
      {
        sprintf(str_buf, MSG[MSG_ERR_SET_DIR_0], current_variable);
	    print_string_gbk(str_buf, 7, str_line, COLOR15_WHITE, COLOR15_BLACK);
        str_line += FONTHEIGHT;

        strcpy(dir_name, main_path);
      }
    }
  }

  void check_directory(char *dir_name, const char *item_name)
  {
    if (dir_name[0] == 0)
    {
      sprintf(str_buf, MSG[MSG_ERR_SET_DIR_1], item_name);
	  print_string_gbk(str_buf, 7, str_line, COLOR15_WHITE, COLOR15_BLACK);
      str_line += FONTHEIGHT;

      strcpy(dir_name, main_path);
    }
  }

  dir_roms[0]   = 0;
  dir_save[0]   = 0;
  dir_state[0]  = 0;
  dir_cfg[0]    = 0;
  dir_snap[0]   = 0;
  dir_cheat[0]  = 0;
  dir_boxart[0] = 0;
  dir_boxart_folders[0] = 0;

  dir_config = fopen(file_name, "r");

  if (dir_config != NULL)
  {
    while (fgets(current_line, 256, dir_config))
    {
      if (parse_config_line(current_line, current_variable, current_value) != -1)
      {
        add_launch_directory();

        set_directory(dir_roms,  item_roms);
        set_directory(dir_save,  item_save);
        set_directory(dir_state, item_state);
        set_directory(dir_cfg,   item_cfg);
        set_directory(dir_snap,  item_snap);
        set_directory(dir_cheat, item_cheat);
        set_directory(dir_boxart, item_boxart);
        set_directory(dir_boxart_folders, item_boxart_folders);
      }
    }

    fclose(dir_config);

    check_directory(dir_roms,  item_roms);
    check_directory(dir_save,  item_save);
    check_directory(dir_state, item_state);
    check_directory(dir_cfg,   item_cfg);
    check_directory(dir_snap,  item_snap);
    check_directory(dir_cheat, item_cheat);
    check_directory(dir_boxart, item_boxart);
    check_directory(dir_boxart_folders, item_boxart_folders);

      if (str_line > 7)
      {
        sprintf(str_buf, MSG[MSG_ERR_SET_DIR_2], main_path);
        sprintf(str_buf, "%s\n\n%s", str_buf, MSG[MSG_ERR_CONT]);

        str_line += FONTHEIGHT;
        error_msg(str_buf, CONFIRMATION_NONE);
      }

    return 0;
  }

  // set launch directory
  strcpy(dir_roms,  main_path);
  strcpy(dir_save,  main_path);
  strcpy(dir_state, main_path);
  strcpy(dir_cfg,   main_path);
  strcpy(dir_snap,  main_path);
  strcpy(dir_cheat, main_path);
  strcpy(dir_boxart, main_path);
  strcpy(dir_boxart_folders, main_path);
  apply_theme(option_theme);
  return -1;
}
