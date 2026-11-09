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
 * Loads the ROM in the first entry of a .zip (format: PKWARE APPNOTE.TXT)
 * through caller-supplied read and progress callbacks, so it has no PSP
 * headers and tests/test_zip_rom.c can run it on the host. Every way an
 * archive can be broken ends in one of the outcomes below; the output's
 * CRC32 is computed while it is written and checked against the archive's.
 */

#ifndef ZIP_ROM_H
#define ZIP_ROM_H

#include <stdint.h>

enum
{
  ZIP_ROM_OK = 0,
  ZIP_ROM_UNSUPPORTED, /* encrypted, not stored/deflate, or not a ROM name */
  ZIP_ROM_TOO_BIG,     /* the ROM does not fit in out_cap */
  ZIP_ROM_TRUNCATED,   /* the archive ends before the entry does */
  ZIP_ROM_CORRUPT,     /* bad header, bad stream, or size/CRC mismatch */
  ZIP_ROM_IO_ERROR     /* read returned an error */
};

/* Most ROM bytes inflated between two progress calls, so a run of padding
   that packs into a few archive bytes still moves the bar. */
#define ZIP_ROM_INFLATE_STEP (256u * 1024u)

typedef struct
{
  void *ctx;
  /* Reads up to len bytes into buf: the count read, 0 at end of file, or a
     negative value on error. */
  int32_t (*read)(void *ctx, uint8_t *buf, uint32_t len);
  /* Optional (may be NULL): archive bytes used so far, against total, after
     each read of a stored ROM and each inflate step; bytes read ahead into
     in_buf count once they are used. Each call's done is larger than the
     one before. */
  void (*progress)(void *ctx, uint32_t done, uint32_t total);
  uint8_t *in_buf;   /* caller-owned input buffer of in_cap bytes */
  uint32_t in_cap;
  uint32_t total;    /* archive length for progress, 0 when unknown */
} ZipRomIo;

/* Decompresses the first entry, a .gba, .agb or .bin file, into out.
   Returns a ZIP_ROM_* outcome; on ZIP_ROM_OK *size is the ROM size. */
int zip_rom_load(const ZipRomIo *io, uint8_t *out, uint32_t out_cap,
 uint32_t *size);

#endif /* ZIP_ROM_H */
