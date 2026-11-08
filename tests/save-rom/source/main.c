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
 * The save type test ROM. The Makefile builds one target per save chip
 * (backup.h). Each boot runs the cases below, then writes a new
 * generation every second, or when A is pressed, and shows it on line 3;
 * so the save can be cut by power-offs at any moment and checked at the
 * next boot. L+R+START jumps to 01000000h, where nothing is mapped, to
 * see how the emulator survives a crashed game.
 *
 * A generation fills the chip after the signature's block (RESULT_SPACE):
 * "SGEN" and its number, then a byte pattern, then the number again in
 * the last 4 bytes, written last. The pattern changes at every offset
 * from one generation to the next, and differs between the two 64 KiB
 * banks of a 128 KiB Flash and the two halves of a 64 KiB SRAM, so a
 * partial or mirrored image shows up as the first offset that does not
 * match.
 *
 * save-eeprom8k_wfirst (BACKUP_WRITE_FIRST) first runs write_first,
 * before anything reads the chip: it writes one block, the last of the
 * signature's space (3Fh), with an 81-unit DMA as a game's first save
 * does, then reads it back. With no .sav the emulator has to size the
 * EEPROM from that write: one that takes it as 512 bytes reads the
 * address as 6 bits, 000000b, writes block 0 and reads back a blank
 * block. The case is PASS with 3Fh, or FAIL with the first 4 bytes read
 * back (FFFFFFFFh: blank), or DEAD0000h when the chip never reports the
 * write done. The ROM cannot tell a blank chip without reading it, so
 * every boot writes the same bytes to that block rather than a whole
 * generation, which would destroy what read_back checks; after the first
 * boot the target runs as save-eeprom8k. Its signature, written later,
 * takes blocks 0-12.
 */

#include "result.h"
#include "backup.h"

#define DATA_START RESULT_SPACE
#define CHUNK 0x1000u

static u8 chunk[CHUNK] EWRAM_BSS __attribute__((aligned(4)));
static u32 generation;

static u8 data_byte(u32 gen, u32 offset)
{
  if(offset < DATA_START + 4)
    return (u8)"SGEN"[offset - DATA_START];
  if(offset < DATA_START + 8)
    return (u8)(gen >> ((offset - DATA_START - 4) * 8));
  if(offset >= BACKUP_SIZE - 4)
    return (u8)(gen >> ((offset - (BACKUP_SIZE - 4)) * 8));
  return (u8)(offset + (offset >> 8) + (offset >> 16) * 0x55 + gen * 0x3B);
}

static u32 chunk_size(u32 offset)
{
  return BACKUP_SIZE - offset < CHUNK ? BACKUP_SIZE - offset : CHUNK;
}

/* The first offset that does not hold generation gen, or 0 if all do. */
static u32 check_generation(u32 gen)
{
  u32 offset;
  u32 i;

  for(offset = DATA_START; offset < BACKUP_SIZE; offset += CHUNK)
  {
    backup_read(offset, chunk, chunk_size(offset));
    for(i = 0; i < chunk_size(offset); i++)
      if(chunk[i] != data_byte(gen, offset + i))
        return offset + i;
  }
  return 0;
}

/* Writes generation gen; returns the first offset that does not read it
   back, or 0. */
static u32 write_generation(u32 gen)
{
  u32 offset;
  u32 i;

  for(offset = DATA_START; offset < BACKUP_SIZE; offset += CHUNK)
  {
    for(i = 0; i < chunk_size(offset); i++)
      chunk[i] = data_byte(gen, offset + i);
    backup_write(offset, chunk, chunk_size(offset));
  }
  return check_generation(gen);
}

#if BACKUP_KIND == BACKUP_FLASH
/* The chip's ID, device << 8 | manufacturer, must be one GBATEK lists
   for its size. The detail is the ID. */
static int flash_id(u32 *detail)
{
#if BACKUP_SIZE == 0x20000
  static const u16 known[] = { 0x1362, 0x09C2 };
#else
  static const u16 known[] = { 0x1B32, 0xD4BF, 0x1CC2, 0x3D1F };
#endif
  unsigned i;

  *detail = backup_flash_id();
  for(i = 0; i < sizeof(known) / sizeof(known[0]); i++)
    if(*detail == known[i])
      return RESULT_PASS;
  return RESULT_FAIL;
}
#endif

/* The generation the last session left: PASS with its number as the
   detail, SKIP on a blank chip, FAIL with the first offset that does not
   match. */
static int read_back(u32 *detail)
{
  u8 header[8];
  u32 bad;
  unsigned i;

  backup_read(DATA_START, header, sizeof(header));
  if(header[0] != 'S' || header[1] != 'G' || header[2] != 'E' ||
     header[3] != 'N')
  {
    for(i = 1; i < sizeof(header); i++)
      if(header[i] != header[0])
      {
        *detail = DATA_START;
        return RESULT_FAIL;
      }
    return RESULT_SKIP;
  }
  generation = header[4] | (u32)header[5] << 8 | (u32)header[6] << 16 |
   (u32)header[7] << 24;
  bad = check_generation(generation);
  *detail = bad ? bad : generation;
  return bad ? RESULT_FAIL : RESULT_PASS;
}

/* The next generation, read back across the whole chip: PASS with its
   number, or FAIL with the first offset that does not match. */
static int write_verify(u32 *detail)
{
  u32 bad = write_generation(generation + 1);

  generation++;
  *detail = bad ? bad : generation;
  return bad ? RESULT_FAIL : RESULT_PASS;
}

#ifdef BACKUP_WRITE_FIRST
/* -std=c99 has no _Static_assert. */
#if BACKUP_KIND != BACKUP_EEPROM || BACKUP_SIZE != 0x2000
#error "BACKUP_WRITE_FIRST is for the 8 KiB EEPROM"
#endif

#define WFIRST_OFFSET (RESULT_SPACE - EEPROM_BLOCK)
#define WFIRST_NOT_DONE 0xDEAD0000u

static int wfirst_status;
static u32 wfirst_detail;

/* Runs before result_run(), whose first step reads the chip. */
static void write_first(void)
{
  static const u8 probe[EEPROM_BLOCK] =
   { 'W', 'F', 'I', 'R', 'S', 'T', 0x5A, 0xA5 };
  u8 back[EEPROM_BLOCK];
  unsigned i;

  wfirst_status = RESULT_FAIL;
  if(!backup_write_unread(WFIRST_OFFSET, probe, sizeof(probe)))
  {
    wfirst_detail = WFIRST_NOT_DONE;
    return;
  }
  backup_read(WFIRST_OFFSET, back, sizeof(back));
  wfirst_detail = back[0] | (u32)back[1] << 8 | (u32)back[2] << 16 |
   (u32)back[3] << 24;
  for(i = 0; i < sizeof(back); i++)
    if(back[i] != probe[i])
      return;
  wfirst_status = RESULT_PASS;
  wfirst_detail = WFIRST_OFFSET / EEPROM_BLOCK;
}

/* What write_first found. */
static int write_first_case(u32 *detail)
{
  *detail = wfirst_detail;
  return wfirst_status;
}
#endif

static const TestCase cases_save[] =
{
#ifdef BACKUP_WRITE_FIRST
  { "write_first", write_first_case },
#endif
#if BACKUP_KIND == BACKUP_FLASH
  { "flash_id", flash_id },
#endif
  { "read_back", read_back },
  { "write_verify", write_verify },
  { NULL, NULL }
};

/* cases_bus.c, empty except in the targets it is for. */
extern const TestCase cases_bus[];

static const TestCase *const areas[] = { cases_save, cases_bus, NULL };

static void show_generation(u32 bad)
{
  text_clear(3);
  text_draw(0, 3, "GEN", COLOR_WHITE);
  text_hex(4, 3, generation, 8, COLOR_WHITE);
  if(bad == 0)
    text_draw(13, 3, "OK", COLOR_GREEN);
  else
  {
    text_draw(13, 3, "BAD AT", COLOR_RED);
    text_hex(20, 3, bad, 8, COLOR_RED);
  }
}

int main(void)
{
  const u32 crash_keys = KEY_L | KEY_R | KEY_START;
  u32 held = 0;
  u32 frames = 0;

#ifdef BACKUP_WRITE_FIRST
  write_first();
#endif
  result_run(areas);
  show_generation(0);
  for(;;)
  {
    u32 keys;

    wait_vblank();
    keys = ~REG_KEYINPUT & 0x03FF;
    if((keys & crash_keys) == crash_keys)
      ((void (*)(void))0x01000000)();
    if(++frames >= 60 || (keys & ~held & KEY_A))
    {
      frames = 0;
      generation++;
      show_generation(write_generation(generation));
    }
    held = keys;
  }
}
