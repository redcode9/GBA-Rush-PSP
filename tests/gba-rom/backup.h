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
 * The cartridge's save chip, picked when the ROM is built (FLAGS_<target>
 * in its Makefile): BACKUP_KIND and BACKUP_SIZE in bytes, plus
 * BACKUP_UNTAGGED for a ROM without the save library ID string that
 * emulators look for. Without flags: 32 KiB of SRAM, tagged.
 *
 * Protocols from GBATEK. The routines run from on-chip work RAM, as
 * cartridges expect of code that reads SRAM or Flash.
 */

#ifndef BACKUP_H
#define BACKUP_H

#include "gba.h"

#define BACKUP_SRAM   1
#define BACKUP_FLASH  2
#define BACKUP_EEPROM 3

#ifndef BACKUP_KIND
#define BACKUP_KIND BACKUP_SRAM
#endif
#ifndef BACKUP_SIZE
#define BACKUP_SIZE 0x8000
#endif

/* Bytes at the start of the chip kept for the result signature
   (result.h); the ROM may use the rest. On Flash it is sector 0. */
#if BACKUP_KIND == BACKUP_SRAM
#define RESULT_SPACE 0x2000
#elif BACKUP_KIND == BACKUP_FLASH
#define RESULT_SPACE 0x1000
#elif BACKUP_SIZE == 0x200
#define RESULT_SPACE 0x100
#else
#define RESULT_SPACE 0x200
#endif

/* Flash sector and EEPROM block sizes. */
#define FLASH_SECTOR 0x1000
#define EEPROM_BLOCK 8

__attribute__((long_call))
void backup_read(u32 offset, void *buffer, u32 size);

/* Makes size bytes from offset hold data, then reads them back: returns
   1 if they match. On Flash, offset and size cover whole sectors, which
   are erased first; on EEPROM, whole 8-byte blocks. */
__attribute__((long_call))
int backup_write(u32 offset, const void *data, u32 size);

/* EEPROM only: writes whole 8-byte blocks without reading the chip
   first, as a game's first save does; returns 1 once the chip reports
   every write done. */
__attribute__((long_call))
int backup_write_unread(u32 offset, const void *data, u32 size);

/* Flash only: the chip's ID, device << 8 | manufacturer. */
__attribute__((long_call))
u32 backup_flash_id(void);

#endif
