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
 * Save chip bus cases, in the save-sram32 and save-flash128 targets (the
 * table is empty in the others).
 *
 * SRAM and Flash sit on an 8-bit bus: a 16- or 32-bit load reads the byte
 * at its address and repeats it in every byte lane (GBATEK, GBA Cart
 * Backup SRAM/FRAM). The CPU then treats the value as from any other
 * memory: an LDRH from an odd address rotates it right by 8, an LDRSH
 * from an odd address sign-extends the byte as LDRSB does, and an LDR
 * rotates by 8 times the address's low two bits (ARM7TDMI data sheet).
 * The detail is the value loaded.
 *
 * sram_<load>_<even|odd>, sram_ldr_<0-3>: loads from four bytes A5h 5Ah
 * 3Ch C3h written 256 bytes after the signature's space and put back
 * afterwards (a power cut in between leaves them in the save data, where
 * the next boot's read_back finds them). The odd and misaligned loads
 * take the byte at their own address; an emulator that takes it from the
 * aligned address reads A5h for all of them.
 *
 * flash_ldrh_maker, flash_ldrh_device: in ID mode, an LDRH from 0E000000h
 * reads the maker byte twice, one from 0E000001h the device byte twice,
 * rotated: compared with the bytes LDRB reads there.
 *
 * To confirm in mGBA and on hardware (the byte taken at an odd address
 * above all).
 */

#include "result.h"
#include "backup.h"

#if BACKUP_KIND == BACKUP_SRAM && BACKUP_SIZE == 0x8000 && \
 !defined(BACKUP_UNTAGGED)
#define BUS_SRAM
#elif BACKUP_KIND == BACKUP_FLASH && BACKUP_SIZE == 0x20000
#define BUS_FLASH
#endif

#if defined(BUS_SRAM) || defined(BUS_FLASH)

#define BACKUP_BUS 0x0E000000u

enum { LOAD_B, LOAD_H, LOAD_SH, LOAD_W };

/* One load of the given kind, from code in IWRAM as backup.c's. */
IWRAM_CODE static u32 bus_load(u32 address, u32 kind)
{
  u32 zero = 0;
  u32 value;

  switch(kind)
  {
    case LOAD_B:
      __asm__ volatile("ldrb %0, [%1]" : "=l"(value) : "l"(address));
      break;
    case LOAD_H:
      __asm__ volatile("ldrh %0, [%1]" : "=l"(value) : "l"(address));
      break;
    case LOAD_SH:
      __asm__ volatile("ldrsh %0, [%1, %2]" : "=l"(value) :
       "l"(address), "l"(zero));
      break;
    default:
      __asm__ volatile("ldr %0, [%1]" : "=l"(value) : "l"(address));
      break;
  }
  return value;
}

#endif

#ifdef BUS_SRAM

#define PATTERN_AT (RESULT_SPACE + 0x100)

static const u8 pattern[4] = { 0xA5, 0x5A, 0x3C, 0xC3 };

static int sram_load(u32 *detail, u32 kind, u32 offset, u32 expected)
{
  u8 saved[4];

  backup_read(PATTERN_AT, saved, sizeof(saved));
  backup_write(PATTERN_AT, pattern, sizeof(pattern));
  *detail = bus_load(BACKUP_BUS + PATTERN_AT + offset, kind);
  backup_write(PATTERN_AT, saved, sizeof(saved));
  return *detail == expected ? RESULT_PASS : RESULT_FAIL;
}

/* name, load, offset, expected */
#define SRAM_CASES(X) \
  X(sram_ldrb_even, LOAD_B, 0, 0xA5) \
  X(sram_ldrb_odd, LOAD_B, 1, 0x5A) \
  X(sram_ldrh_even, LOAD_H, 0, 0xA5A5) \
  X(sram_ldrh_odd, LOAD_H, 1, 0x5A00005Au) \
  X(sram_ldrsh_even, LOAD_SH, 0, 0xFFFFA5A5u) \
  X(sram_ldrsh_odd, LOAD_SH, 1, 0x5A) \
  X(sram_ldr_0, LOAD_W, 0, 0xA5A5A5A5u) \
  X(sram_ldr_1, LOAD_W, 1, 0x5A5A5A5Au) \
  X(sram_ldr_2, LOAD_W, 2, 0x3C3C3C3Cu) \
  X(sram_ldr_3, LOAD_W, 3, 0xC3C3C3C3u)

#define SRAM_FUNCTION(name, kind, offset, expected) \
  static int name(u32 *detail) \
  { \
    return sram_load(detail, kind, offset, expected); \
  }
#define SRAM_ENTRY(name, kind, offset, expected) { #name, name },

SRAM_CASES(SRAM_FUNCTION)

#endif

#ifdef BUS_FLASH

#define FLASH ((vu8 *)BACKUP_BUS)

IWRAM_CODE static void flash_command(u32 command)
{
  FLASH[0x5555] = 0xAA;
  FLASH[0x2AAA] = 0x55;
  FLASH[0x5555] = (u8)command;
}

/* Byte at offset in ID mode (bits 0-7), and the LDRH from there. */
IWRAM_CODE static u32 flash_id_load(u32 offset, u32 *halfword)
{
  u32 byte;

  flash_command(0x90);
  byte = FLASH[offset];
  *halfword = bus_load(BACKUP_BUS + offset, LOAD_H);
  flash_command(0xF0);
  return byte;
}

static u32 rotate_right(u32 value, u32 bits)
{
  return bits ? value >> bits | value << (32 - bits) : value;
}

static int flash_ldrh(u32 *detail, u32 offset)
{
  u32 byte = flash_id_load(offset, detail);

  return *detail == rotate_right(byte * 0x0101, offset * 8) ?
   RESULT_PASS : RESULT_FAIL;
}

static int flash_ldrh_maker(u32 *detail)
{
  return flash_ldrh(detail, 0);
}

static int flash_ldrh_device(u32 *detail)
{
  return flash_ldrh(detail, 1);
}

#endif

const TestCase cases_bus[] =
{
#ifdef BUS_SRAM
  SRAM_CASES(SRAM_ENTRY)
#endif
#ifdef BUS_FLASH
  { "flash_ldrh_maker", flash_ldrh_maker },
  { "flash_ldrh_device", flash_ldrh_device },
#endif
  { NULL, NULL }
};
