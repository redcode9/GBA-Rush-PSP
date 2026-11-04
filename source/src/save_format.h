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
 * Cartridge save layout, decided without any I/O so the host tests can
 * cover every case: the save library tag in the ROM picks the type, the
 * .sav file's size only picks the size within that type, and the bytes
 * written back are always a size the chip really has. Also brings back in
 * range the save fields a savestate restores.
 */

#ifndef SAVE_FORMAT_H
#define SAVE_FORMAT_H

#include <stdint.h>

/* The ID string the ROM's save library leaves in it (EEPROM_V, SRAM_V or
   SRAM_F_V, FLASH_V or FLASH512_V, FLASH1M_V). */
typedef enum
{
  SAVE_TAG_NONE,
  SAVE_TAG_EEPROM,
  SAVE_TAG_SRAM,
  SAVE_TAG_FLASH64,
  SAVE_TAG_FLASH128
} SaveTag;

/* Same values as memory.c's backup types, which savestates store. */
typedef enum
{
  SAVE_TYPE_SRAM,
  SAVE_TYPE_FLASH,
  SAVE_TYPE_EEPROM,
  SAVE_TYPE_NONE
} SaveType;

/* Same values as memory.c's EEPROM protocol states, which savestates
   store. */
typedef enum
{
  SAVE_EEPROM_BASE,
  SAVE_EEPROM_READ,
  SAVE_EEPROM_READ_HEADER,
  SAVE_EEPROM_ADDRESS,
  SAVE_EEPROM_WRITE,
  SAVE_EEPROM_WRITE_ADDRESS,
  SAVE_EEPROM_ADDRESS_FOOTER,
  SAVE_EEPROM_WRITE_FOOTER
} SaveEepromMode;

#define SAVE_FORMAT_MAX_SIZE 0x20000u

typedef struct
{
  SaveType type;        /* SAVE_TYPE_NONE: the game's first access decides */
  uint32_t sram_size;   /* 0x8000 or 0x10000 */
  uint32_t flash_size;  /* 0x10000 or 0x20000 */
  uint32_t eeprom_size; /* 0x200 or 0x2000 */
  int mismatch;         /* the file is not the size this layout writes */
} SaveLayout;

/* The layout for a ROM's tag and its .sav's size, 0 for no file. A tag
   decides the type, and the Flash tags the size too. Without a tag the
   file size decides as before: 512 B and 8 KiB EEPROM, 32 KiB SRAM,
   128 KiB Flash, while 64 KiB leaves the type to the game with SRAM and
   Flash both at 64 KiB. EEPROM files other than 512 B and 8 KiB start at
   512 B, and the game's first 17-unit DMA makes the chip 8 KiB. */
SaveLayout save_format_resolve(SaveTag tag, uint32_t file_size);

/* Bytes to write for the type and sizes in use: 0 while the type is
   unknown, else the chip's size, from 512 B to SAVE_FORMAT_MAX_SIZE. A
   size the type cannot have counts as that type's larger size. */
uint32_t save_format_write_size(uint32_t type, uint32_t sram_size,
 uint32_t flash_size, uint32_t eeprom_size);

/* The save fields a savestate restores. */
typedef struct
{
  uint32_t type;
  uint32_t sram_size;
  uint32_t flash_size;
  uint32_t flash_bank;
  uint32_t eeprom_size;
  uint32_t eeprom_mode;
  uint32_t eeprom_address_length;
  uint32_t eeprom_address;
  uint32_t eeprom_counter;
} SaveState;

/* save_format_sanitize() bits: which fields it changed. */
#define SAVE_FIX_TYPE                  0x001u
#define SAVE_FIX_SRAM_SIZE             0x002u
#define SAVE_FIX_FLASH_SIZE            0x004u
#define SAVE_FIX_FLASH_BANK            0x008u
#define SAVE_FIX_EEPROM_SIZE           0x010u
#define SAVE_FIX_EEPROM_MODE           0x020u
#define SAVE_FIX_EEPROM_ADDRESS_LENGTH 0x040u
#define SAVE_FIX_EEPROM_ADDRESS        0x080u
#define SAVE_FIX_EEPROM_COUNTER        0x100u

/* Makes the fields a savestate just restored safe to index the 128 KiB
   save buffer with: an impossible type or size goes back to its value in
   before (the state in use until the load), a bad Flash bank or EEPROM
   address becomes 0, an EEPROM address phase with a bad address width
   takes the chip's, and an EEPROM bit counter past its protocol phase
   restarts the protocol. Valid fields are left alone. Returns the
   SAVE_FIX_* bits of the fields changed. */
uint32_t save_format_sanitize(SaveState *state, const SaveState *before);

#endif /* SAVE_FORMAT_H */
