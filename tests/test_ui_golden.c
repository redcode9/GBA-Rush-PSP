/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * Menu screens drawn on the host for the golden hashes. tests/test_ui_golden.py
 * builds this file together with files it generates from the production
 * sources: ui_golden_defines.h (screen and colour macros), ui_golden_extracted.c
 * (drawing functions of gui.c, main.c and psp_video.c), ui_golden_messages.c
 * (the five message tables in GBK) and ui_golden_gbk.c / ui_golden_sjis.c
 * (ch_print and mh_print with their fonts).
 *
 * The functions below stand in for psp_video.c: they take the same inclusive
 * coordinates and queue into ui_batch, as the PSP backend of UI-8.2 must;
 * tests/ui_raster.c draws the batch. Every screen is drawn in each language
 * and theme, and one line "<screen> <language> <theme> <hash>" is printed
 * for it. Built with UI_GOLDEN_PIXELS=1, each line is followed by the image,
 * one row of RRGGBB hex per line.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ui_batch.h"
#include "ui_raster.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef int32_t s32;

#include "ch_print.h"
#include "mh_print.h"
#include "message.h"
#include "ui_golden_defines.h"

#ifndef UI_GOLDEN_PIXELS
#define UI_GOLDEN_PIXELS 0
#endif

/* SDK, input and main.c calls the extracted code makes. A dialog is
   answered at once; the clock reads Thursday 2026-11-05 14:07; the battery
   is at 69%, on the charger and not charging. */
#define PSP_CTRL_CIRCLE 0x2000
#define PSP_CTRL_CROSS  0x4000

typedef enum
{
  CURSOR_SELECT,
  CURSOR_EXIT,
  CURSOR_NONE
} GUI_ACTION_TYPE;

typedef struct
{
  u16 year, month, day, hour, minute, second;
  u32 microsecond;
} ScePspDateTime;

u32 option_language;
u32 option_swap_confirm_buttons;
int date_format;
/* Sized to its text: in a 512-byte array gcc flags apply_theme's
   sprintf of main_path plus the theme file name (-Wformat-overflow). */
char main_path[] = "ms0:/PSP/GAME/GBARush/";

u32 get_pad_input(u32 mask) { (void)mask; return 0; }
GUI_ACTION_TYPE get_gui_input(void) { return CURSOR_SELECT; }
void main_power_service(void) {}
void main_link_pump(void) {}
int sceKernelDelayThread(u32 delay) { (void)delay; return 0; }
int sceIoRemove(const char *path) { (void)path; return 0; }
int sceRtcGetDayOfWeek(int year, int month, int day)
{
  (void)year; (void)month; (void)day; return 4;
}
int scePowerGetBatteryLifePercent(void) { return 69; }
int scePowerIsPowerOnline(void) { return 1; }
int scePowerGetBatteryLifeTime(void) { return 185; }
int scePowerIsBatteryCharging(void) { return 0; }
int scePowerIsLowBattery(void) { return 0; }

int sceRtcGetCurrentClockLocalTime(ScePspDateTime *time)
{
  time->year = 2026;
  time->month = 11;
  time->day = 5;
  time->hour = 14;
  time->minute = 7;
  time->second = 0;
  time->microsecond = 0;
  return 0;
}

static UiBatch batch;
static UiRaster raster;

static void raster_flush(void *context, const UiCommand *commands,
 uint32_t count, const UiTexture *textures)
{
  ui_raster_execute((UiRaster *)context, commands, count, textures);
}

/* psp_video.c's drawing calls, queued into the batch. */
void clear_screen(u32 color)
{
  /* The 5551 value psp_video.c fills the screen with. */
  u32 r8 = (color >>  0) & 0xFF;
  u32 g8 = (color >>  8) & 0xFF;
  u32 b8 = (color >> 16) & 0xFF;
  u16 color16 = ((r8 >> 3) << 0) | ((g8 >> 3) << 5) | ((b8 >> 3) << 10) | 0x8000;

  ui_batch_fill(&batch, 0, 0, PSP_SCREEN_WIDTH, PSP_SCREEN_HEIGHT, color16);
}

void flip_screen(u32 vsync)
{
  (void)vsync;
  ui_batch_flush(&batch);
}

void draw_box_line(u16 x1, u16 y1, u16 x2, u16 y2, u16 color)
{
  ui_batch_box_line(&batch, x1, y1, x2, y2, color);
}

void draw_box_fill(u16 x1, u16 y1, u16 x2, u16 y2, u16 color)
{
  ui_batch_fill(&batch, x1, y1, x2 + 1, y2 + 1, color);
}

void draw_box_alpha(u16 x1, u16 y1, u16 x2, u16 y2, u32 color)
{
  ui_batch_fill_alpha(&batch, x1, y1, x2 + 1, y2 + 1, color);
}

void draw_hline(u16 sx, u16 ex, u16 y, u16 color)
{
  ui_batch_hline(&batch, sx, ex, y, color);
}

/* Text: the real font code draws into canvas, marking the pixels it sets
   with TEXT_FG and, when the background is filled, the others with
   TEXT_BG. Each mark becomes a T4 page queued as a GLYPH tinted with its
   colour. The batch reads the pages when it flushes, so it flushes before
   they are rewritten. */
#define TEXT_BG 1
#define TEXT_FG 2

typedef void (*FontPrint)(const char *str, u16 x, u16 y, u16 col,
 s16 bg_col, u16 *base_vram, u16 bufferwidth);

static u16 canvas[PSP_SCREEN_HEIGHT][PSP_LINE_SIZE];
static uint8_t pages[2][PSP_SCREEN_WIDTH * PSP_SCREEN_HEIGHT / 2];

static void queue_text(FontPrint print, const char *str, s16 x, u16 y,
 u16 fg_color, s16 bg_color)
{
  u32 width, height = FONTHEIGHT, u, v, i;
  const char *c;
  int page;

  /* Centred as psp_video.c does it. */
  if(x < 0) x = (PSP_SCREEN_WIDTH - (strlen(str) * FONTWIDTH)) >> 1;
  /* The fonts take x as u16: a text wider than the screen draws nothing. */
  if((u16)x >= PSP_SCREEN_WIDTH)
    return;
  for(c = str; *c != '\0'; c++)
    if(*c == '\n')
      height += FONTHEIGHT;
  /* The fonts do not stop at the bottom edge. */
  assert(y + height <= PSP_SCREEN_HEIGHT);
  width = PSP_SCREEN_WIDTH - x;

  ui_batch_clear_textures(&batch);
  print(str, x, y, TEXT_FG, bg_color >= 0 ? TEXT_BG : BG_NO_FILL,
   &canvas[0][0], PSP_LINE_SIZE);
  for(page = 0; page < 2; page++)
    memset(pages[page], 0, (width * height + 1) / 2);
  for(v = 0; v < height; v++)
  {
    for(u = 0; u < width; u++)
    {
      u16 *mark = &canvas[y + v][x + u];

      i = v * width + u;
      if(*mark != 0)
        pages[*mark - 1][i >> 1] |= (uint8_t)(15 << ((i & 1) * 4));
      *mark = 0;
    }
  }
  for(page = bg_color >= 0 ? 0 : 1; page < 2; page++)
    ui_batch_glyph(&batch, ui_batch_add_texture(&batch, pages[page],
     UI_TEXTURE_T4, width, height, width), x, y, 0, 0, width, height,
     page ? fg_color : (u16)bg_color);
}

void print_string(const char *str, s16 x, u16 y, u16 fg_color, s16 bg_color)
{
  queue_text(mh_print, str, x, y, fg_color, bg_color);
}

void print_string_gbk(const char *str, s16 x, u16 y, u16 fg_color,
 s16 bg_color)
{
  queue_text(ch_print, str, x, y, fg_color, bg_color);
}

#include "ui_golden_extracted.c"

/* In message.c's table order, which is option_language's. */
static const char *const language_names[5] =
 { "jpn", "eng", "chs", "cht", "ita" };
/* In theme_presets order. */
static const char *const theme_names[9] =
 { "og", "dark", "light", "blue", "green", "red", "purple", "hicon", "retro" };

/* The yes/no dialog over the menu background. */
static void screen_dialog(void)
{
  clear_screen(COLOR15_TO_32(color_bg));
  yesno_dialog(MSG[MSG_AUTO_SAVESTATE_LOAD_PROMPT]);
}

/* The theme preview panel of the Theme menu. */
static void screen_preview(void)
{
  clear_screen(COLOR15_TO_32(color_bg));
  draw_theme_preview(0xFFFF);
  flip_screen(1);
}

/* The same panel while the colour picker edits the battery low (item 9)
   or charging (item 10) colour, the only screens that show those two. */
static void screen_preview_battery(u32 item)
{
  color_picker_item_index = item;
  clear_screen(COLOR15_TO_32(color_bg));
  draw_theme_preview(0);  /* any value but 0xFFFF: a colour is being edited */
  flip_screen(1);
}

static void screen_preview_low(void) { screen_preview_battery(9); }
static void screen_preview_charging(void) { screen_preview_battery(10); }

/* The clock and battery line at the top of the screen. */
static void screen_status(void)
{
  int i;

  /* draw_status_bar() makes its strings on the first of every 30 calls:
     the first 29 here make them in this language, and clear_screen hides
     what they draw. */
  for(i = 0; i < 29; i++)
    draw_status_bar();
  clear_screen(COLOR15_TO_32(color_bg));
  draw_status_bar();
  flip_screen(1);
}

static const struct
{
  const char *name;
  void (*draw)(void);
} screens[] =
{
  { "dialog", screen_dialog },
  { "preview", screen_preview },
  { "preview-low", screen_preview_low },
  { "preview-charging", screen_preview_charging },
  { "status", screen_status }
};

static void put_pixels(void)
{
  static const char digits[] = "0123456789abcdef";
  char row[PSP_SCREEN_WIDTH * 6 + 2];
  u32 x, y, shift;

  for(y = 0; y < PSP_SCREEN_HEIGHT; y++)
  {
    char *out = row;

    for(x = 0; x < PSP_SCREEN_WIDTH; x++)
    {
      for(shift = 0; shift <= 10; shift += 5)
      {
        u32 c5 = (raster.pixels[y][x] >> shift) & 0x1F;
        u32 c8 = (c5 << 3) | (c5 >> 2);

        *out++ = digits[c8 >> 4];
        *out++ = digits[c8 & 15];
      }
    }
    *out++ = '\n';
    *out = '\0';
    fputs(row, stdout);
  }
}

int main(void)
{
  char line[64];
  u32 screen, language, theme;

  ui_batch_init(&batch, raster_flush, &raster);
  for(screen = 0; screen < sizeof(screens) / sizeof(screens[0]); screen++)
  {
    for(language = 0; language < 5; language++)
    {
      for(theme = 0; theme < 9; theme++)
      {
        option_language = language;
        apply_theme(theme);
        screens[screen].draw();
        snprintf(line, sizeof(line), "%s %s %s %08x\n", screens[screen].name,
         language_names[language], theme_names[theme],
         (unsigned)ui_raster_hash(&raster));
        fputs(line, stdout);
        if(UI_GOLDEN_PIXELS)
          put_pixels();
      }
    }
  }
  return 0;
}
