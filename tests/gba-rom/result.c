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

/* The result protocol and the text screen; see result.h. */

#include "result.h"
#include "backup.h"

#ifndef ROM_ID
#error "ROM_ID names the ROM in its signature; tests/gba-rom.mk sets it"
#endif

#define SIGNATURE_VERSION 1
#define HEADER_SIZE 48
#define ROM_NAME_SIZE 28
#define NO_CASE 0xFFFF
#define MAX_CASES 1024
#define FIRST_LIST_LINE 4
#define LINES 20

static const TestCase *cases[MAX_CASES];
static u32 case_count;
static u32 detail_offset;
static u32 names_offset;
static u32 signature_size;
static u32 crc_table[256];
static unsigned list_line;
static int write_failed;
static u8 image[RESULT_SPACE] EWRAM_BSS __attribute__((aligned(4)));

/* ---- Text ---------------------------------------------------------------- */

/* 5x7 glyphs, one byte per row; bit 4 is the leftmost pixel. */
static const u8 digit_font[10][7] =
{
  { 14, 17, 19, 21, 25, 17, 14 },
  { 4, 12, 4, 4, 4, 4, 14 },
  { 14, 17, 1, 2, 4, 8, 31 },
  { 30, 1, 1, 14, 1, 1, 30 },
  { 2, 6, 10, 18, 31, 2, 2 },
  { 31, 16, 16, 30, 1, 1, 30 },
  { 14, 16, 16, 30, 17, 17, 14 },
  { 31, 1, 2, 4, 8, 8, 8 },
  { 14, 17, 17, 14, 17, 17, 14 },
  { 14, 17, 17, 15, 1, 1, 14 }
};

static const u8 letter_font[26][7] =
{
  { 14, 17, 17, 31, 17, 17, 17 },
  { 30, 17, 17, 30, 17, 17, 30 },
  { 14, 17, 16, 16, 16, 17, 14 },
  { 30, 17, 17, 17, 17, 17, 30 },
  { 31, 16, 16, 30, 16, 16, 31 },
  { 31, 16, 16, 30, 16, 16, 16 },
  { 14, 17, 16, 23, 17, 17, 15 },
  { 17, 17, 17, 31, 17, 17, 17 },
  { 14, 4, 4, 4, 4, 4, 14 },
  { 7, 2, 2, 2, 18, 18, 12 },
  { 17, 18, 20, 24, 20, 18, 17 },
  { 16, 16, 16, 16, 16, 16, 31 },
  { 17, 27, 21, 21, 17, 17, 17 },
  { 17, 25, 21, 19, 17, 17, 17 },
  { 14, 17, 17, 17, 17, 17, 14 },
  { 30, 17, 17, 30, 16, 16, 16 },
  { 14, 17, 17, 17, 21, 18, 13 },
  { 30, 17, 17, 30, 20, 18, 17 },
  { 15, 16, 16, 14, 1, 1, 30 },
  { 31, 4, 4, 4, 4, 4, 4 },
  { 17, 17, 17, 17, 17, 17, 14 },
  { 17, 17, 17, 17, 17, 10, 4 },
  { 17, 17, 17, 21, 21, 21, 10 },
  { 17, 17, 10, 4, 10, 17, 17 },
  { 17, 17, 10, 4, 4, 4, 4 },
  { 31, 1, 2, 4, 8, 16, 31 }
};

static u32 glyph_row(char c, unsigned row)
{
  if(c >= '0' && c <= '9')
    return digit_font[c - '0'][row];
  if(c >= 'a' && c <= 'z')
    c = (char)(c - 'a' + 'A');
  if(c >= 'A' && c <= 'Z')
    return letter_font[c - 'A'][row];
  switch(c)
  {
    case '-': return row == 3 ? 31 : 0;
    case ':': return row == 2 || row == 5 ? 4 : 0;
    case '/': return row < 5 ? 1u << row : 0;
    case '.': return row == 6 ? 4 : 0;
    case '_': return row == 6 ? 31 : 0;
    default: return 0;
  }
}

void text_clear(unsigned line)
{
  vu16 *pixel = VRAM + line * 8 * SCREEN_WIDTH;
  unsigned i;

  for(i = 0; i < 8 * SCREEN_WIDTH; i++)
    pixel[i] = COLOR_BACKGROUND;
}

void text_draw(unsigned column, unsigned line, const char *text, u16 color)
{
  for(; *text != 0 && column < 40; text++, column++)
  {
    unsigned row;

    for(row = 0; row < 7; row++)
    {
      u32 bits = glyph_row(*text, row);
      vu16 *pixel = VRAM + (line * 8 + row) * SCREEN_WIDTH + column * 6;
      unsigned x;

      for(x = 0; x < 6; x++)
        pixel[x] = (bits & (16u >> x)) ? color : COLOR_BACKGROUND;
    }
  }
}

void text_hex(unsigned column, unsigned line, u32 value, unsigned digits,
 u16 color)
{
  char text[9];
  unsigned i;

  for(i = 0; i < digits && i < 8; i++)
    text[i] = "0123456789ABCDEF"[(value >> ((digits - 1 - i) * 4)) & 15];
  text[i] = 0;
  text_draw(column, line, text, color);
}

static void text_decimal(unsigned column, unsigned line, u32 value,
 u16 color)
{
  char text[11];
  unsigned i = sizeof(text) - 1;

  text[i] = 0;
  do
  {
    text[--i] = (char)('0' + value % 10);
    value /= 10;
  } while(value != 0 && i > 0);
  text_draw(column, line, text + i, color);
}

/* ---- Signature ------------------------------------------------------------ */

static void put16(u32 offset, u32 value)
{
  image[offset] = (u8)value;
  image[offset + 1] = (u8)(value >> 8);
}

static void put32(u32 offset, u32 value)
{
  put16(offset, value);
  put16(offset + 2, value >> 16);
}

static u32 get16(u32 offset)
{
  return image[offset] | (u32)image[offset + 1] << 8;
}

static u32 get32(u32 offset)
{
  return get16(offset) | get16(offset + 2) << 16;
}

static u32 crc32(const u8 *data, u32 size)
{
  u32 crc = ~0u;
  u32 i;

  if(crc_table[1] == 0)
    for(i = 0; i < 256; i++)
    {
      u32 c = i;
      unsigned k;

      for(k = 0; k < 8; k++)
        c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      crc_table[i] = c;
    }
  for(i = 0; i < size; i++)
    crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

static u32 signature_crc(void)
{
  u32 stored = get32(12);
  u32 crc;

  put32(12, 0);
  crc = crc32(image, signature_size);
  put32(12, stored);
  return crc;
}

static void flush(void)
{
  u32 bytes = BACKUP_KIND == BACKUP_FLASH ? RESULT_SPACE :
   (signature_size + EEPROM_BLOCK - 1) & ~(u32)(EEPROM_BLOCK - 1);

  put32(12, signature_crc());
  if(!backup_write(0, image, bytes))
    write_failed = 1;
}

static u32 status_of(u32 index)
{
  return image[HEADER_SIZE + index];
}

static void set_result(u32 index, u32 status, u32 detail)
{
  image[HEADER_SIZE + index] = (u8)status;
  put32(detail_offset + index * 4, detail);
}

/* Lays out a new run in image. */
static void build(void)
{
  const char *rom = ROM_ID;
  u32 offset = names_offset;
  u32 i;

  for(i = 0; i < RESULT_SPACE; i++)
    image[i] = 0;
  image[0] = 'G';
  image[1] = 'R';
  image[2] = 'T';
  image[3] = 'S';
  image[4] = SIGNATURE_VERSION;
  put16(6, case_count);
  put16(8, NO_CASE);
  put16(10, 1);
  put32(16, signature_size);
  for(i = 0; rom[i] != 0 && i < ROM_NAME_SIZE - 1; i++)
    image[20 + i] = (u8)rom[i];
  for(i = 0; i < case_count; i++)
  {
    const char *name = cases[i]->name;

    do
      image[offset++] = (u8)*name;
    while(*name++ != 0);
  }
}

/* Whether image, just read from the chip, is an unfinished run of these
   very cases. */
static int resumable(void)
{
  const char *rom = ROM_ID;
  u32 offset = names_offset;
  u32 i;
  int unfinished = get16(8) != NO_CASE;

  if(image[0] != 'G' || image[1] != 'R' || image[2] != 'T' ||
     image[3] != 'S' || image[4] != SIGNATURE_VERSION ||
     get16(6) != case_count || get32(16) != signature_size ||
     get32(12) != signature_crc())
    return 0;
  for(i = 0; i < ROM_NAME_SIZE; i++)
  {
    if(image[20 + i] != (u8)rom[i])
      return 0;
    if(rom[i] == 0)
      break;
  }
  for(i = 0; i < case_count; i++)
  {
    const char *name = cases[i]->name;

    do
      if(image[offset++] != (u8)*name)
        return 0;
    while(*name++ != 0);
    unfinished |= status_of(i) == RESULT_NOT_RUN;
  }
  return unfinished;
}

/* ---- Screen --------------------------------------------------------------- */

static const char *const status_names[] =
{
  "NOT RUN", "PASS", "FAIL", "SKIP", "HUNG"
};

static void draw_summary(void)
{
  u32 totals[5] = { 0, 0, 0, 0, 0 };
  u32 i;

  for(i = 0; i < case_count; i++)
    totals[status_of(i) < 5 ? status_of(i) : RESULT_FAIL]++;
  text_clear(1);
  text_draw(0, 1, "PASS", COLOR_GREEN);
  text_decimal(5, 1, totals[RESULT_PASS], COLOR_WHITE);
  text_draw(10, 1, "FAIL", COLOR_RED);
  text_decimal(15, 1, totals[RESULT_FAIL] + totals[RESULT_HUNG],
   COLOR_WHITE);
  text_draw(20, 1, "SKIP", COLOR_YELLOW);
  text_decimal(25, 1, totals[RESULT_SKIP], COLOR_WHITE);
  text_draw(30, 1, "LEFT", COLOR_WHITE);
  text_decimal(35, 1, totals[RESULT_NOT_RUN], COLOR_WHITE);
}

/* Failed and hung cases get a line each, from line 4, while they fit. */
static void draw_case(u32 index)
{
  u32 status = status_of(index);

  if((status != RESULT_FAIL && status != RESULT_HUNG) || list_line >= LINES)
    return;
  text_draw(0, list_line, cases[index]->name, COLOR_WHITE);
  text_draw(25, list_line, status_names[status], COLOR_RED);
  text_hex(30, list_line, get32(detail_offset + index * 4), 8, COLOR_WHITE);
  list_line++;
}

static void draw_status(const char *text, u16 color)
{
  text_clear(2);
  text_draw(0, 2, text, color);
}

/* ---- Running -------------------------------------------------------------- */

static void reset_machine(void)
{
  REG_IME = 0;
  REG_IE = 0;
  REG_DISPSTAT = 0;
  REG_IF = 0xFFFF;
  IRQ_VECTOR = irq_default;
  REG_DISPCNT = DISPCNT_MODE3 | DISPCNT_BG2;
}

void result_run(const TestCase *const *areas)
{
  u32 i;

  case_count = 0;
  signature_size = 0;
  for(; *areas != NULL; areas++)
  {
    const TestCase *c;

    for(c = *areas; c->name != NULL && case_count < MAX_CASES; c++)
    {
      const char *name = c->name;

      cases[case_count++] = c;
      while(*name++ != 0)
        signature_size++;
      signature_size++;
    }
  }
  detail_offset = HEADER_SIZE + ((case_count + 3) & ~3u);
  names_offset = detail_offset + case_count * 4;
  signature_size += names_offset;

  reset_machine();
  for(i = 0; i < 20; i++)
    text_clear(i);
  text_draw(0, 0, ROM_ID, COLOR_WHITE);
  if(signature_size > RESULT_SPACE)
  {
    draw_status("TOO MANY CASES FOR THE SAVE CHIP", COLOR_RED);
    for(;;)
      ;
  }

  backup_read(0, image, (signature_size + EEPROM_BLOCK - 1) &
   ~(u32)(EEPROM_BLOCK - 1));
  if(resumable())
  {
    if(get16(8) < case_count)
      set_result(get16(8), RESULT_HUNG, 0);
    put16(8, NO_CASE);
    put16(10, get16(10) + 1);
  }
  else
    build();
  flush();

  text_draw(28, 0, "BOOT", COLOR_WHITE);
  text_decimal(33, 0, get16(10), COLOR_WHITE);
  draw_summary();
  list_line = FIRST_LIST_LINE;
  for(i = 0; i < case_count; i++)
    draw_case(i);

  for(i = 0; i < case_count; i++)
  {
    u32 detail = 0;
    int status;

    if(status_of(i) != RESULT_NOT_RUN)
      continue;
    draw_status("RUN", COLOR_YELLOW);
    text_draw(4, 2, cases[i]->name, COLOR_YELLOW);
    put16(8, i);
    flush();

    reset_machine();
    status = cases[i]->run(&detail);
    reset_machine();

    put16(8, NO_CASE);
    set_result(i, (u32)status, detail);
    flush();
    draw_summary();
    draw_case(i);
  }

  if(write_failed)
    draw_status("SAVE CHIP WRITE FAILED", COLOR_RED);
  else
    draw_status("DONE", COLOR_GREEN);
}
