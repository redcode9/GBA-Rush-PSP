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
 * The result protocol of the test ROMs. A ROM is a list of cases, one
 * per behaviour, kept in tables of one file per area (cases_<area>.c).
 * result_run() runs them in order and reports every outcome twice: as
 * text on the screen, and in a signature at the start of the save chip
 * that tools/decode_test_rom.py reads from the emulator's .sav file. A
 * cartridge has a single save chip, so Flash and EEPROM targets keep the
 * signature in a reserved block of their own chip (RESULT_SPACE, in
 * backup.h).
 *
 * Signature, little endian:
 *    0  "GRTS"
 *    4  version, 1
 *    5  0
 *    6  u16 number of cases N
 *    8  u16 the case running, FFFFh when none: a case that hangs or
 *       crashes the emulator stays marked here
 *   10  u16 boots into this run
 *   12  u32 CRC-32 (zlib's) of the signature, read with this field 0
 *   16  u32 size of the signature in bytes
 *   20  ROM name, 28 bytes padded with NULs
 *   48  N status bytes (RESULT_*), then zeros to a multiple of 4
 *    .  N u32 details: what each case measured, its own meaning
 *    .  N case names, each followed by a NUL
 *
 * A boot that finds an unfinished run of the same ROM (same name, same
 * cases) marks the case that was running RESULT_HUNG and goes on with the
 * next one, so a power cycle gets past a case that locks up. A boot after
 * a finished run, or with anything else in the chip, starts over.
 */

#ifndef RESULT_H
#define RESULT_H

#include "gba.h"

enum
{
  RESULT_NOT_RUN,
  RESULT_PASS,
  RESULT_FAIL,
  RESULT_SKIP,
  RESULT_HUNG
};

typedef struct
{
  /* Lower case letters, digits and underscores, at most 24 characters. */
  const char *name;
  /* Starts with IME, IE and DISPSTAT at 0, IF clear, the default IRQ
     handler, system mode with the CPSR's I bit clear and the screen in
     mode 3; result_run() puts those back afterwards, anything else the
     case changes it restores itself. Returns RESULT_PASS, RESULT_FAIL or
     RESULT_SKIP and may set *detail, which starts at 0. */
  int (*run)(u32 *detail);
} TestCase;

/* Runs every case of areas, a NULL-terminated list of tables that each
   end with an entry whose name is NULL, then returns. */
void result_run(const TestCase *const *areas);

/* zlib's CRC-32 of size bytes at data, carried on from crc: 0 starts a
   new one, and a CRC passed back in continues it over more data. */
u32 result_crc32(u32 crc, const void *data, u32 size);

/* Text on a grid of 40 columns and 20 lines of 6x8 pixels, in mode 3.
   result_run() uses lines 0-2 and from 4 on; line 3 is the ROM's. Only
   capitals, digits and - : / . _ have glyphs; lower case letters draw as
   capitals. */
#define COLOR_BACKGROUND 0x0C00
#define COLOR_WHITE      0x7FFF
#define COLOR_GREEN      0x03E0
#define COLOR_RED        0x001F
#define COLOR_YELLOW     0x03FF

void text_clear(unsigned line);
void text_draw(unsigned column, unsigned line, const char *text,
 u16 color);
void text_hex(unsigned column, unsigned line, u32 value, unsigned digits,
 u16 color);

#endif
