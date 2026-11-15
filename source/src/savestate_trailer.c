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
 * Savestate v2 trailer encoder and decoder (layout in savestate_trailer.h).
 * Fields are packed byte by byte: the trailer sits at an odd place in a
 * buffer the caller owns, and the PSP faults on unaligned word loads.
 */

#include <string.h>

#include "savestate_trailer.h"
#include "zlib/zlib.h"

#define TRAILER_CRC_OFFSET 44u

static const uint8_t trailer_magic[4] = { 'G', 'R', 'S', 'T' };

static uint32_t get_u16(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t get_u32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
   ((uint32_t)p[3] << 24);
}

static void put_u16(uint8_t *p, uint32_t value)
{
  p[0] = (uint8_t)(value & 0xFFu);
  p[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_u32(uint8_t *p, uint32_t value)
{
  put_u16(p, value & 0xFFFFu);
  put_u16(p + 2, value >> 16);
}

static uint32_t trailer_crc32(const uint8_t *data, uint32_t size)
{
  uLong crc = crc32(0L, Z_NULL, 0);

  return (uint32_t)crc32(crc, data, size);
}

void savestate_trailer_rom_id(const uint8_t *header, uint32_t rom_file_size,
 SavestateRomId *out)
{
  memcpy(out->game_code, header + 0xAC, sizeof(out->game_code));
  out->software_version = header[0xBC];
  out->header_crc = trailer_crc32(header + 0xA0, 0x20u);
  out->rom_size = rom_file_size;
}

void savestate_trailer_encode(const SavestateTrailer *t,
 uint8_t out[SAVESTATE_TRAILER_SIZE])
{
  memset(out, 0, SAVESTATE_TRAILER_SIZE);
  memcpy(out, trailer_magic, sizeof(trailer_magic));
  put_u16(out + 4, SAVESTATE_TRAILER_VERSION);
  put_u16(out + 6, SAVESTATE_TRAILER_SIZE);
  put_u32(out + 8, t->payload_size);
  put_u32(out + 12, t->payload_crc);
  memcpy(out + 16, t->rom.game_code, sizeof(t->rom.game_code));
  out[20] = t->rom.software_version;
  put_u32(out + 24, t->rom.header_crc);
  put_u32(out + 28, t->rom.rom_size);
  put_u32(out + 32, t->bios_crc);
  put_u32(out + 36, t->emulator_version);
  put_u32(out + TRAILER_CRC_OFFSET, trailer_crc32(out, TRAILER_CRC_OFFSET));
}

SavestateTrailerResult savestate_trailer_decode(uint32_t file_size,
 const uint8_t *tail, size_t tail_size, uint32_t payload_crc,
 const SavestateRomId *current, SavestateTrailer *out)
{
  uint32_t version;

  memset(out, 0, sizeof(*out));

  /* Only the exact size marks a legacy state. */
  if(file_size == SAVESTATE_TRAILER_PAYLOAD_SIZE)
    return SAVESTATE_TRAILER_LEGACY;
  if(file_size < SAVESTATE_TRAILER_PAYLOAD_SIZE ||
   tail_size != file_size - SAVESTATE_TRAILER_PAYLOAD_SIZE || tail_size < 8u ||
   memcmp(tail, trailer_magic, sizeof(trailer_magic)) != 0)
    return SAVESTATE_TRAILER_CORRUPT;

  /* Magic and version alone decide this: a later layout may move or resize
     everything after them, the CRC included. */
  version = get_u16(tail + 4);
  if(version > SAVESTATE_TRAILER_VERSION)
  {
    out->version = version;
    return SAVESTATE_TRAILER_NEWER;
  }
  if(version != SAVESTATE_TRAILER_VERSION ||
   get_u16(tail + 6) != SAVESTATE_TRAILER_SIZE ||
   tail_size != SAVESTATE_TRAILER_SIZE ||
   trailer_crc32(tail, TRAILER_CRC_OFFSET) !=
    get_u32(tail + TRAILER_CRC_OFFSET))
    return SAVESTATE_TRAILER_CORRUPT;

  out->version = version;
  out->payload_size = get_u32(tail + 8);
  out->payload_crc = get_u32(tail + 12);
  memcpy(out->rom.game_code, tail + 16, sizeof(out->rom.game_code));
  out->rom.software_version = tail[20];
  out->rom.header_crc = get_u32(tail + 24);
  out->rom.rom_size = get_u32(tail + 28);
  out->bios_crc = get_u32(tail + 32);
  out->emulator_version = get_u32(tail + 36);

  if(out->payload_size != SAVESTATE_TRAILER_PAYLOAD_SIZE ||
   out->payload_crc != payload_crc)
    return SAVESTATE_TRAILER_CORRUPT;
  if(memcmp(out->rom.game_code, current->game_code,
   sizeof(current->game_code)) != 0 ||
   out->rom.software_version != current->software_version ||
   out->rom.header_crc != current->header_crc)
    return SAVESTATE_TRAILER_OTHER_ROM;
  if(out->rom.rom_size != current->rom_size)
    return SAVESTATE_TRAILER_ROM_SIZE_DIFFERS;
  return SAVESTATE_TRAILER_V2;
}
