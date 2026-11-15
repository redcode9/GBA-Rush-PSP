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
 * Savestate v2 trailer. A v2 savestate is the 512 KiB payload every
 * savestate has (screenshot at offset 0, BIOS word and SIO snapshot at
 * their fixed offsets near the end) followed by this 48-byte trailer; a
 * file of exactly 512 KiB is a legacy state. On-disk layout, little-endian,
 * part of the format:
 *
 *    0  u32    magic "GRST"
 *    4  u16    format version, 2
 *    6  u16    trailer size, 48
 *    8  u32    payload length, 0x80000
 *   12  u32    CRC32 of the payload
 *   16  u8[4]  game code (ROM header 0xAC-0xAF)
 *   20  u8     software version (ROM header 0xBC), then 3 zero bytes
 *   24  u32    CRC32 of ROM header 0xA0-0xBF
 *   28  u32    ROM file size
 *   32  u32    CRC32 of the active BIOS (diagnostics only, never checked)
 *   36  u32    emulator version, (major << 16) | (minor << 8) | patch
 *   40  u32    reserved, 0
 *   44  u32    CRC32 of bytes 0-43
 */

#ifndef SAVESTATE_TRAILER_H
#define SAVESTATE_TRAILER_H

#include <stddef.h>
#include <stdint.h>

#define SAVESTATE_TRAILER_SIZE 48u
#define SAVESTATE_TRAILER_VERSION 2u
/* Must equal SAVESTATE_SIZE in memory.c. */
#define SAVESTATE_TRAILER_PAYLOAD_SIZE 0x80000u

/* What a savestate records about the ROM it was made with. */
typedef struct
{
  uint8_t game_code[4];
  uint8_t software_version;
  uint32_t header_crc;
  uint32_t rom_size;
} SavestateRomId;

typedef struct
{
  uint32_t version;
  uint32_t payload_size;
  uint32_t payload_crc;
  SavestateRomId rom;
  uint32_t bios_crc;
  uint32_t emulator_version;
} SavestateTrailer;

typedef enum
{
  SAVESTATE_TRAILER_LEGACY,          /* exactly 512 KiB, no trailer */
  SAVESTATE_TRAILER_V2,              /* valid, made with this ROM */
  SAVESTATE_TRAILER_CORRUPT,
  SAVESTATE_TRAILER_NEWER,           /* a later format version */
  SAVESTATE_TRAILER_OTHER_ROM,
  SAVESTATE_TRAILER_ROM_SIZE_DIFFERS /* same game, other ROM size: warn */
} SavestateTrailerResult;

/* Fills `out` from the first 0xC0 bytes of the ROM (its header) and the
   ROM file's size. */
void savestate_trailer_rom_id(const uint8_t *header, uint32_t rom_file_size,
 SavestateRomId *out);

/* Writes the trailer for `t`; magic, size and trailer CRC are its own, and
   the version written is SAVESTATE_TRAILER_VERSION whatever t->version
   holds. */
void savestate_trailer_encode(const SavestateTrailer *t,
 uint8_t out[SAVESTATE_TRAILER_SIZE]);

/* Classifies a savestate file of `file_size` bytes. `tail` holds the
   `tail_size` bytes that follow the payload, as read; no byte past them is
   ever read. `payload_crc` is the CRC32 of the payload as read, `current`
   the running ROM. `out` is zeroed, gets the version for NEWER, and is
   filled once the trailer's own CRC checks out. */
SavestateTrailerResult savestate_trailer_decode(uint32_t file_size,
 const uint8_t *tail, size_t tail_size, uint32_t payload_crc,
 const SavestateRomId *current, SavestateTrailer *out);

#endif
