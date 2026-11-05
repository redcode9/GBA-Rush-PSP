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

/* Save chip drivers; see backup.h. */

#include "backup.h"

/* The save library ID string: emulators scan the ROM for it, at a word
   boundary, to learn the save type. One per ROM, so no other string of
   this kind may appear in it. */
#ifndef BACKUP_UNTAGGED
#if BACKUP_KIND == BACKUP_SRAM
#define BACKUP_TAG "SRAM_V113"
#elif BACKUP_KIND == BACKUP_FLASH && BACKUP_SIZE == 0x20000
#define BACKUP_TAG "FLASH1M_V103"
#elif BACKUP_KIND == BACKUP_FLASH
#define BACKUP_TAG "FLASH512_V131"
#else
#define BACKUP_TAG "EEPROM_V124"
#endif
__attribute__((used, aligned(4)))
const char backup_tag[] = BACKUP_TAG;
#endif

/* Polling a chip gives up after this many reads (far longer than any
   erase or write takes). */
#define BACKUP_TIMEOUT 0x100000

#if BACKUP_KIND == BACKUP_SRAM

#define SRAM ((vu8 *)0x0E000000)

IWRAM_CODE void backup_read(u32 offset, void *buffer, u32 size)
{
  u8 *out = buffer;
  u32 i;

  for(i = 0; i < size; i++)
    out[i] = SRAM[offset + i];
}

IWRAM_CODE int backup_write(u32 offset, const void *data, u32 size)
{
  const u8 *in = data;
  u32 i;

  for(i = 0; i < size; i++)
    if(SRAM[offset + i] != in[i])
      SRAM[offset + i] = in[i];
  for(i = 0; i < size; i++)
    if(SRAM[offset + i] != in[i])
      return 0;
  return 1;
}

IWRAM_CODE u32 backup_flash_id(void)
{
  return 0;
}

#elif BACKUP_KIND == BACKUP_FLASH

#define FLASH ((vu8 *)0x0E000000)

IWRAM_CODE static void flash_command(u32 command)
{
  FLASH[0x5555] = 0xAA;
  FLASH[0x2AAA] = 0x55;
  FLASH[0x5555] = (u8)command;
}

/* The 128 KiB chip shows one 64 KiB bank at a time. Never sent to a
   64 KiB chip, which has no bank command. */
#if BACKUP_SIZE == 0x20000
static u32 flash_bank = ~0u;
#endif

IWRAM_CODE static void flash_select(u32 offset)
{
#if BACKUP_SIZE == 0x20000
  if(flash_bank != offset >> 16)
  {
    flash_bank = offset >> 16;
    flash_command(0xB0);
    FLASH[0] = (u8)flash_bank;
  }
#else
  (void)offset;
#endif
}

IWRAM_CODE static int flash_wait(u32 address, u32 value)
{
  u32 n;

  for(n = 0; n < BACKUP_TIMEOUT; n++)
    if(FLASH[address] == value)
      return 1;
  return 0;
}

IWRAM_CODE void backup_read(u32 offset, void *buffer, u32 size)
{
  u8 *out = buffer;
  u32 i;

  for(i = 0; i < size; i++)
  {
    flash_select(offset + i);
    out[i] = FLASH[(offset + i) & 0xFFFF];
  }
}

IWRAM_CODE int backup_write(u32 offset, const void *data, u32 size)
{
  const u8 *in = data;
  u32 i;

  for(i = 0; i < size; i++)
  {
    u32 address = (offset + i) & 0xFFFF;

    flash_select(offset + i);
    if((address & (FLASH_SECTOR - 1)) == 0)
    {
      flash_command(0x80);
      FLASH[0x5555] = 0xAA;
      FLASH[0x2AAA] = 0x55;
      FLASH[address] = 0x30;
      if(!flash_wait(address, 0xFF))
        return 0;
    }
    if(in[i] != 0xFF)
    {
      flash_command(0xA0);
      FLASH[address] = in[i];
      if(!flash_wait(address, in[i]))
        return 0;
    }
  }
  for(i = 0; i < size; i++)
  {
    flash_select(offset + i);
    if(FLASH[(offset + i) & 0xFFFF] != in[i])
      return 0;
  }
  return 1;
}

IWRAM_CODE u32 backup_flash_id(void)
{
  u32 id;

  flash_command(0x90);
  id = FLASH[0] | (u32)FLASH[1] << 8;
  flash_command(0xF0);
  return id;
}

#else

/* EEPROM: one bit per halfword, moved by DMA3 at 0D000000h (the ROM is
   under 16 MiB). A request is 2 command bits (11 read, 10 write), the
   block number, for a write its 64 bits, then a 0; bits go most
   significant first. A read answers with 4 bits to ignore, then 64. */
#define EEPROM ((vu16 *)0x0D000000)
#define EEPROM_ADDRESS_BITS (BACKUP_SIZE == 0x200 ? 6 : 14)

static u16 eeprom_bits[2 + 14 + 64 + 1];

IWRAM_CODE static void dma3(const volatile void *source,
 volatile void *destination, u32 count)
{
  REG_DMA3SAD = (u32)source;
  REG_DMA3DAD = (u32)destination;
  REG_DMA3CNT = DMA_ENABLE | count;
  while(REG_DMA3CNT & DMA_ENABLE)
    ;
}

/* Fills eeprom_bits from position 2 with the block number, then returns
   the position after it. */
IWRAM_CODE static u32 eeprom_address(u32 block)
{
  u32 n = 2;
  int bit;

  for(bit = EEPROM_ADDRESS_BITS - 1; bit >= 0; bit--)
    eeprom_bits[n++] = (u16)((block >> bit) & 1);
  return n;
}

IWRAM_CODE static void eeprom_read_block(u32 block, u8 *out)
{
  u32 n;
  u32 i;

  eeprom_bits[0] = 1;
  eeprom_bits[1] = 1;
  n = eeprom_address(block);
  eeprom_bits[n++] = 0;
  dma3(eeprom_bits, EEPROM, n);
  dma3(EEPROM, eeprom_bits, 68);
  for(i = 0; i < 8; i++)
  {
    u32 byte = 0;
    u32 bit;

    for(bit = 0; bit < 8; bit++)
      byte = (byte << 1) | (eeprom_bits[4 + i * 8 + bit] & 1);
    out[i] = (u8)byte;
  }
}

IWRAM_CODE static int eeprom_write_block(u32 block, const u8 *in)
{
  u32 n;
  u32 i;

  eeprom_bits[0] = 1;
  eeprom_bits[1] = 0;
  n = eeprom_address(block);
  for(i = 0; i < 64; i++)
    eeprom_bits[n++] = (u16)((in[i / 8] >> (7 - i % 8)) & 1);
  eeprom_bits[n++] = 0;
  dma3(eeprom_bits, EEPROM, n);
  for(i = 0; i < BACKUP_TIMEOUT; i++)
    if(*EEPROM & 1)
      return 1;
  return 0;
}

IWRAM_CODE void backup_read(u32 offset, void *buffer, u32 size)
{
  u8 *out = buffer;
  u32 i;

  for(i = 0; i < size; i += EEPROM_BLOCK)
    eeprom_read_block((offset + i) / EEPROM_BLOCK, out + i);
}

IWRAM_CODE int backup_write(u32 offset, const void *data, u32 size)
{
  const u8 *in = data;
  u8 block[EEPROM_BLOCK];
  u32 i;
  u32 j;

  for(i = 0; i < size; i += EEPROM_BLOCK)
  {
    u32 number = (offset + i) / EEPROM_BLOCK;
    int same = 1;

    eeprom_read_block(number, block);
    for(j = 0; j < EEPROM_BLOCK; j++)
      same &= block[j] == in[i + j];
    if(same)
      continue;
    if(!eeprom_write_block(number, in + i))
      return 0;
    eeprom_read_block(number, block);
    for(j = 0; j < EEPROM_BLOCK; j++)
      if(block[j] != in[i + j])
        return 0;
  }
  return 1;
}

IWRAM_CODE u32 backup_flash_id(void)
{
  return 0;
}

#endif
