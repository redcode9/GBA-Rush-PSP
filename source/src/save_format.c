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

#include "save_format.h"

#define SRAM_32K   0x8000u
#define SRAM_64K   0x10000u
#define FLASH_64K  0x10000u
#define FLASH_128K 0x20000u
#define EEPROM_512 0x200u
#define EEPROM_8K  0x2000u

SaveLayout save_format_resolve(SaveTag tag, uint32_t file_size)
{
  SaveLayout layout;
  uint32_t native = 0; /* the file size this layout keeps; 0 if unknown */

  layout.type = SAVE_TYPE_NONE;
  layout.sram_size = SRAM_32K;
  layout.flash_size = FLASH_64K;
  layout.eeprom_size = EEPROM_512;

  switch(tag)
  {
    case SAVE_TAG_SRAM:
      layout.type = SAVE_TYPE_SRAM;
      if(file_size > SRAM_32K)
        layout.sram_size = SRAM_64K;
      native = layout.sram_size;
      break;

    case SAVE_TAG_FLASH64:
      layout.type = SAVE_TYPE_FLASH;
      native = FLASH_64K;
      break;

    case SAVE_TAG_FLASH128:
      layout.type = SAVE_TYPE_FLASH;
      layout.flash_size = FLASH_128K;
      native = FLASH_128K;
      break;

    case SAVE_TAG_EEPROM:
      layout.type = SAVE_TYPE_EEPROM;
      if(file_size == EEPROM_8K)
        layout.eeprom_size = EEPROM_8K;
      native = layout.eeprom_size;
      break;

    case SAVE_TAG_NONE:
    default:
      switch(file_size)
      {
        case EEPROM_512:
          layout.type = SAVE_TYPE_EEPROM;
          native = EEPROM_512;
          break;

        case EEPROM_8K:
          layout.type = SAVE_TYPE_EEPROM;
          layout.eeprom_size = EEPROM_8K;
          native = EEPROM_8K;
          break;

        case SRAM_32K:
          layout.type = SAVE_TYPE_SRAM;
          native = SRAM_32K;
          break;

        /* SRAM or Flash: either way the game's choice writes 64 KiB. */
        case SRAM_64K:
          layout.sram_size = SRAM_64K;
          native = SRAM_64K;
          break;

        case FLASH_128K:
          layout.type = SAVE_TYPE_FLASH;
          layout.flash_size = FLASH_128K;
          native = FLASH_128K;
          break;
      }
      break;
  }

  layout.mismatch = file_size != 0 && file_size != native;
  return layout;
}

uint32_t save_format_write_size(uint32_t type, uint32_t sram_size,
 uint32_t flash_size, uint32_t eeprom_size)
{
  switch(type)
  {
    case SAVE_TYPE_SRAM:
      return sram_size == SRAM_32K ? SRAM_32K : SRAM_64K;

    case SAVE_TYPE_FLASH:
      return flash_size == FLASH_64K ? FLASH_64K : FLASH_128K;

    case SAVE_TYPE_EEPROM:
      return eeprom_size == EEPROM_512 ? EEPROM_512 : EEPROM_8K;

    default:
      return 0;
  }
}

/* How many bits each EEPROM phase counts before it moves on; 0 when the
   count is never used (the footers reset it). */
static uint32_t eeprom_counter_limit(uint32_t mode, uint32_t address_length)
{
  switch(mode)
  {
    case SAVE_EEPROM_BASE:
      return 2;

    case SAVE_EEPROM_READ_HEADER:
      return 4;

    case SAVE_EEPROM_READ:
    case SAVE_EEPROM_WRITE:
      return 64;

    case SAVE_EEPROM_ADDRESS:
    case SAVE_EEPROM_WRITE_ADDRESS:
      return address_length;

    default:
      return 0;
  }
}

static int eeprom_address_phase(uint32_t mode)
{
  return mode == SAVE_EEPROM_ADDRESS || mode == SAVE_EEPROM_WRITE_ADDRESS;
}

uint32_t save_format_sanitize(SaveState *state, const SaveState *before)
{
  uint32_t fixed = 0;
  uint32_t limit;

  if(state->type > SAVE_TYPE_NONE)
  {
    state->type = before->type;
    fixed |= SAVE_FIX_TYPE;
  }
  if(state->sram_size != SRAM_32K && state->sram_size != SRAM_64K)
  {
    state->sram_size = before->sram_size;
    fixed |= SAVE_FIX_SRAM_SIZE;
  }
  if(state->flash_size != FLASH_64K && state->flash_size != FLASH_128K)
  {
    state->flash_size = before->flash_size;
    fixed |= SAVE_FIX_FLASH_SIZE;
  }
  if(state->flash_bank > 1)
  {
    state->flash_bank = 0;
    fixed |= SAVE_FIX_FLASH_BANK;
  }
  if(state->eeprom_size != EEPROM_512 && state->eeprom_size != EEPROM_8K)
  {
    state->eeprom_size = before->eeprom_size;
    fixed |= SAVE_FIX_EEPROM_SIZE;
  }
  /* A read or write touches the 8 bytes from the address on. */
  if(state->eeprom_address > SAVE_FORMAT_MAX_SIZE - 8)
  {
    state->eeprom_address = 0;
    fixed |= SAVE_FIX_EEPROM_ADDRESS;
  }
  if(state->eeprom_mode > SAVE_EEPROM_WRITE_FOOTER)
  {
    state->eeprom_mode = SAVE_EEPROM_BASE;
    state->eeprom_counter = 0;
    fixed |= SAVE_FIX_EEPROM_MODE;
  }
  /* Only the address phases read the length (0 until the first command):
     the command phase sets it on its way there. */
  if(eeprom_address_phase(state->eeprom_mode) &&
     state->eeprom_address_length != 6 && state->eeprom_address_length != 14)
  {
    state->eeprom_address_length = state->eeprom_size == EEPROM_8K ? 14 : 6;
    fixed |= SAVE_FIX_EEPROM_ADDRESS_LENGTH;
  }
  limit = eeprom_counter_limit(state->eeprom_mode,
   state->eeprom_address_length);
  if(limit != 0 && state->eeprom_counter >= limit)
  {
    state->eeprom_mode = SAVE_EEPROM_BASE;
    state->eeprom_counter = 0;
    fixed |= SAVE_FIX_EEPROM_COUNTER;
  }
  return fixed;
}
