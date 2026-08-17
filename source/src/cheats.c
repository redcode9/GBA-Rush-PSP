/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2007 takka <takka@tfact.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "common.h"

CHEAT_TYPE ALIGN_DATA cheats[MAX_CHEATS];
u32 num_cheats = 0;
char loaded_cheat_filename[MAX_FILE] = { 0 };

static u16 ALIGN_DATA active_cheat_indexes[MAX_CHEATS];
static u16 ALIGN_DATA active_codebreaker_indexes[MAX_CHEATS];
static u32 active_cheat_count = 0;
static u32 active_codebreaker_count = 0;

static u32 ALIGN_DATA configured_hook_addresses[MAX_CHEAT_HOOKS];
static u32 configured_hook_count = 0;
static u32 active_dispatch_hook = 0;

static void decrypt_gsa_code(int *address_ptr, int *value_ptr,
 CHEAT_VARIANT_ENUM cheat_variant);
static void process_cheat_gs1(CHEAT_TYPE *cheat);
static void process_cheat_gs3(CHEAT_TYPE *cheat);
static void process_cheat_codebreaker(CHEAT_TYPE *cheat);
static u32 codebreaker_hook_address(u32 op1);
static u32 codebreaker_entry_hook(const CHEAT_TYPE *cheat);
static u32 codebreaker_operation_lines(const CHEAT_TYPE *cheat, u32 line);

typedef struct
{
  u32 master;
  u32 seeds[4];
  u32 rng_state;
  u8 table[48];
} CODEBREAKER_STATE;

static u32 codebreaker_rand(CODEBREAKER_STATE *state)
{
  u32 roll = state->rng_state * 0x41C64E6D + 0x3039;
  u32 roll2 = roll * 0x41C64E6D + 0x3039;
  u32 roll3 = roll2 * 0x41C64E6D + 0x3039;
  u32 mix = (roll << 14) & 0xC0000000;

  mix |= (roll2 >> 1) & 0x3FFF8000;
  mix |= (roll3 >> 16) & 0x7FFF;
  state->rng_state = roll3;
  return mix;
}

static u32 codebreaker_swap_index(CODEBREAKER_STATE *state)
{
  u32 roll = codebreaker_rand(state);
  u32 count = sizeof(state->table);
  u32 bit = 1;
  u32 mask;

  if(roll == count)
    roll = 0;

  if(roll < count)
    return roll;

  while((count < 0x10000000) && (count < roll))
  {
    count <<= 4;
    bit <<= 4;
  }

  while((count < 0x80000000) && (count < roll))
  {
    count <<= 1;
    bit <<= 1;
  }

  while(1)
  {
    mask = 0;
    if(roll >= count)
      roll -= count;
    if(roll >= (count >> 1))
    {
      roll -= count >> 1;
      mask |= (bit >> 1) | (bit << 31);
    }
    if(roll >= (count >> 2))
    {
      roll -= count >> 2;
      mask |= (bit >> 2) | (bit << 30);
    }
    if(roll >= (count >> 3))
    {
      roll -= count >> 3;
      mask |= (bit >> 3) | (bit << 29);
    }
    if((roll == 0) || ((bit >> 4) == 0))
      break;
    bit >>= 4;
    count >>= 4;
  }

  mask &= 0xE0000000;
  if((mask == 0) || ((bit & 7) == 0))
    return roll;

  if(mask & ((bit >> 3) | (bit << 29)))
    roll += count >> 3;
  if(mask & ((bit >> 2) | (bit << 30)))
    roll += count >> 2;
  if(mask & ((bit >> 1) | (bit << 31)))
    roll += count >> 1;

  return roll;
}

static void codebreaker_reseed(CODEBREAKER_STATE *state, u32 op1, u16 op2)
{
  u32 i;

  state->rng_state = (op2 & 0xFF) ^ 0x1111;
  for(i = 0; i < sizeof(state->table); i++)
    state->table[i] = i;

  for(i = 0; i < 0x50; i++)
  {
    u32 x = codebreaker_swap_index(state);
    u32 y = codebreaker_swap_index(state);
    u8 swap = state->table[x];
    state->table[x] = state->table[y];
    state->table[y] = swap;
  }

  state->rng_state = 0x4EFAD1C3;
  for(i = 0; i < ((op1 >> 24) & 0xF); i++)
    state->rng_state = codebreaker_rand(state);

  state->seeds[2] = codebreaker_rand(state);
  state->seeds[3] = codebreaker_rand(state);

  state->rng_state = (op2 >> 8) ^ 0xF254;
  for(i = 0; i < (op2 >> 8); i++)
    state->rng_state = codebreaker_rand(state);

  state->seeds[0] = codebreaker_rand(state);
  state->seeds[1] = codebreaker_rand(state);
  state->master = op1;
}

static void codebreaker_load_bytes(u8 *buffer, u32 op1, u16 op2)
{
  buffer[0] = op1 >> 24;
  buffer[1] = op1 >> 16;
  buffer[2] = op1 >> 8;
  buffer[3] = op1;
  buffer[4] = op2 >> 8;
  buffer[5] = op2;
}

static void codebreaker_store_bytes(u8 *buffer, u32 *op1, u16 *op2)
{
  *op1 = ((u32)buffer[0] << 24) | ((u32)buffer[1] << 16) |
   ((u32)buffer[2] << 8) | buffer[3];
  *op2 = ((u16)buffer[4] << 8) | buffer[5];
}

static void codebreaker_decrypt(CODEBREAKER_STATE *state, u32 *op1, u16 *op2)
{
  u8 buffer[6];
  s32 i;
  u32 master;

  codebreaker_load_bytes(buffer, *op1, *op2);
  for(i = sizeof(state->table) - 1; i >= 0; i--)
  {
    u32 offset_x = i >> 3;
    u32 offset_y = state->table[i] >> 3;
    u32 bit_x = i & 7;
    u32 bit_y = state->table[i] & 7;
    u8 x = (buffer[offset_x] >> bit_x) & 1;
    u8 y = (buffer[offset_y] >> bit_y) & 1;

    buffer[offset_x] &= ~(1 << bit_x);
    if(y)
      buffer[offset_x] |= 1 << bit_x;
    buffer[offset_y] &= ~(1 << bit_y);
    if(x)
      buffer[offset_y] |= 1 << bit_y;
  }
  codebreaker_store_bytes(buffer, op1, op2);

  *op1 ^= state->seeds[0];
  *op2 ^= state->seeds[1];

  codebreaker_load_bytes(buffer, *op1, *op2);
  master = state->master;
  for(i = 0; i < 5; i++)
    buffer[i] ^= (master >> 8) ^ buffer[i + 1];
  buffer[5] ^= master >> 8;

  for(i = 5; i > 0; i--)
    buffer[i] ^= master ^ buffer[i - 1];
  buffer[0] ^= master;
  codebreaker_store_bytes(buffer, op1, op2);

  *op1 ^= state->seeds[2];
  *op2 ^= state->seeds[3];
}

static u32 hook_address_present(const u32 *addresses, u32 count, u32 address)
{
  u32 low = 0;
  u32 high = count;

  while(low < high)
  {
    u32 middle = low + ((high - low) >> 1);

    if(addresses[middle] < address)
      low = middle + 1;
    else
      high = middle;
  }

  return (low < count) && (addresses[low] == address);
}

static u32 add_hook_address(u32 *addresses, u32 *count, u32 address)
{
  u32 low = 0;
  u32 high = *count;
  u32 i;

  while(low < high)
  {
    u32 middle = low + ((high - low) >> 1);

    if(addresses[middle] < address)
      low = middle + 1;
    else
      high = middle;
  }

  if((low < *count) && (addresses[low] == address))
    return 1;
  if(*count >= MAX_CHEAT_HOOKS)
    return 0;

  for(i = *count; i > low; i--)
    addresses[i] = addresses[i - 1];

  addresses[low] = address;
  (*count)++;
  return 1;
}

static void rebuild_configured_hook_index(void)
{
  u32 i;

  configured_hook_count = 0;
  for(i = 0; i < num_cheats; i++)
  {
    u32 line;

    if(cheats[i].cheat_variant != CHEAT_TYPE_CODEBREAKER)
      continue;

    for(line = 0; line < cheats[i].num_cheat_lines; )
    {
      u32 op1 = cheats[i].cheat_codes[line * 2];

      if((op1 >> 28) == 0x1)
        add_hook_address(configured_hook_addresses, &configured_hook_count,
         codebreaker_hook_address(op1));
      line += codebreaker_operation_lines(cheats + i, line);
    }
  }
}

static u32 codebreaker_entry_hook(const CHEAT_TYPE *cheat)
{
  u32 line;

  if(cheat->cheat_variant != CHEAT_TYPE_CODEBREAKER)
    return 0;

  for(line = 0; line < cheat->num_cheat_lines; )
  {
    u32 op1 = cheat->cheat_codes[line * 2];

    if((op1 >> 28) == 0x1)
      return codebreaker_hook_address(op1);
    line += codebreaker_operation_lines(cheat, line);
  }

  return 0;
}

void rebuild_active_cheat_indexes(void)
{
  u32 i;
  u32 previous_hook = active_dispatch_hook;
  u32 first_hook = 0;
  u32 replacement_hook = 0;
  u32 selected_hook;

  /* A CodeBreaker device installs one timed master. If a second master is
   * enabled, treat it as a replacement rather than dispatching every active
   * code at both addresses. */
  for(i = 0; i < num_cheats; i++)
  {
    u32 hook;

    if(cheats[i].cheat_active != 1)
      continue;

    hook = codebreaker_entry_hook(cheats + i);
    if(hook == 0)
      continue;
    if(first_hook == 0)
      first_hook = hook;
    if(hook != previous_hook)
      replacement_hook = hook;
  }

  if(first_hook == 0)
    selected_hook = 0;
  else if((previous_hook != 0) && (replacement_hook != 0))
    selected_hook = replacement_hook;
  else
    selected_hook = first_hook;

  if(selected_hook != 0)
  {
    for(i = 0; i < num_cheats; i++)
    {
      u32 hook = codebreaker_entry_hook(cheats + i);

      if((cheats[i].cheat_active == 1) && (hook != 0) &&
       (hook != selected_hook))
        cheats[i].cheat_active = 0;
    }
  }

  active_cheat_count = 0;
  active_codebreaker_count = 0;
  active_dispatch_hook = selected_hook;

  for(i = 0; i < num_cheats; i++)
  {
    if(cheats[i].cheat_active != 1)
      continue;

    active_cheat_indexes[active_cheat_count++] = (u16)i;
    if(cheats[i].cheat_variant != CHEAT_TYPE_CODEBREAKER)
      continue;

    active_codebreaker_indexes[active_codebreaker_count++] = (u16)i;
  }
}

void clear_cheats(void)
{
  memset(cheats, 0, sizeof(cheats));
  num_cheats = 0;
  loaded_cheat_filename[0] = '\0';
  configured_hook_count = 0;
  active_cheat_count = 0;
  active_codebreaker_count = 0;
  active_dispatch_hook = 0;
}


static void decrypt_gsa_code(int *address_ptr, int *value_ptr,
 CHEAT_VARIANT_ENUM cheat_variant)
{
  u32 i;
  u32 address = *address_ptr;
  u32 value = *value_ptr;
  u32 r = 0xc6ef3720;

  const u32 ALIGN_DATA seeds_v1[4]
   = {0x09f4fbbd, 0x9681884a, 0x352027e9, 0xf3dee5a7};
  const u32 ALIGN_DATA seeds_v3[4]
   = {0x7aa9648f, 0x7fae6994, 0xc0efaad5, 0x42712c57};
  u32 *seeds;

  if(cheat_variant == CHEAT_TYPE_GAMESHARK_V1)
    seeds = (u32 *)seeds_v1;
  else
    seeds = (u32 *)seeds_v3;

  for(i = 0; i < 32; i++)
  {
    value -= ((address << 4) + seeds[2]) ^ (address + r) ^ ((address >> 5)
     + seeds[3]);
    address -= ((value << 4) + seeds[0]) ^ (value + r) ^ ((value >> 5)
     + seeds[1]);
    r -= 0x9e3779b9;
  }

  *address_ptr = address;
  *value_ptr = value;
}

static CHEAT_VARIANT_ENUM parse_cheat_variant(const char *variant)
{
  if(!strcasecmp(variant, "gameshark_v1") ||
   !strcasecmp(variant, "gameshark_v2") ||
   !strcasecmp(variant, "PAR_v1") ||
   !strcasecmp(variant, "PAR_v2"))
    return CHEAT_TYPE_GAMESHARK_V1;

  if(!strcasecmp(variant, "gameshark_v3") ||
   !strcasecmp(variant, "PAR_v3"))
    return CHEAT_TYPE_GAMESHARK_V3;

  if(!strcasecmp(variant, "direct_v1") ||
   !strcasecmp(variant, "direct_v2"))
    return CHEAT_TYPE_DIRECT_V1;

  if(!strcasecmp(variant, "direct_v3") || !strcasecmp(variant, "#"))
    return CHEAT_TYPE_DIRECT_V3;

  if(!strcasecmp(variant, "codebreaker") ||
   !strcasecmp(variant, "codebreaker_v1") ||
   !strcasecmp(variant, "codebreaker_v2") ||
   !strcasecmp(variant, "CB"))
    return CHEAT_TYPE_CODEBREAKER;

  return CHEAT_TYPE_INVALID;
}

static void trim_line_end(char *line)
{
  u32 length = strlen(line);

  while(length && ((line[length - 1] == '\n') ||
   (line[length - 1] == '\r') || (line[length - 1] == ' ') ||
   (line[length - 1] == '\t')))
    line[--length] = '\0';
}

static u32 line_is_truncated(const char *line, FILE *file)
{
  return (strchr(line, '\n') == NULL) && !feof(file);
}

static CHEAT_LOAD_RESULT parse_cheats_file(FILE *cheats_file, u32 store,
 u32 collect_hooks, u32 *parsed_count, u32 *error_line)
{
  char current_line[256];
  u32 cheat_count = 0;
  u32 line_number = 0;
  CODEBREAKER_STATE codebreaker_state;

  memset(&codebreaker_state, 0, sizeof(codebreaker_state));

  while(fgets(current_line, sizeof(current_line), cheats_file))
  {
    char *variant_ptr;
    char *name_ptr;
    char *separator;
    CHEAT_VARIANT_ENUM current_cheat_variant;
    u32 num_cheat_lines = 0;
    u32 pending_codebreaker_data = 0;

    line_number++;
    if(line_is_truncated(current_line, cheats_file))
    {
      if(error_line)
        *error_line = line_number;
      return CHEAT_LOAD_ERROR_FORMAT;
    }
    trim_line_end(current_line);
    variant_ptr = current_line;
    while((*variant_ptr == ' ') || (*variant_ptr == '\t'))
      variant_ptr++;
    if(*variant_ptr == '\0')
      continue;

    separator = variant_ptr;
    while(*separator && (*separator != ' ') && (*separator != '\t'))
      separator++;
    if(*separator == '\0')
    {
      if(error_line)
        *error_line = line_number;
      return CHEAT_LOAD_ERROR_FORMAT;
    }

    *separator++ = '\0';
    while((*separator == ' ') || (*separator == '\t'))
      separator++;
    name_ptr = separator;
    current_cheat_variant = parse_cheat_variant(variant_ptr);
    if((current_cheat_variant == CHEAT_TYPE_INVALID) || (*name_ptr == '\0'))
    {
      if(error_line)
        *error_line = line_number;
      return CHEAT_LOAD_ERROR_FORMAT;
    }
    if(cheat_count >= MAX_CHEATS)
    {
      if(error_line)
        *error_line = line_number;
      return CHEAT_LOAD_ERROR_CAPACITY;
    }

    if(store)
    {
      memset(cheats + cheat_count, 0, sizeof(CHEAT_TYPE));
      strncpy(cheats[cheat_count].cheat_name, name_ptr,
       CHEAT_NAME_LENGTH - 1);
      cheats[cheat_count].cheat_name[CHEAT_NAME_LENGTH - 1] = '\0';
      cheats[cheat_count].cheat_variant = current_cheat_variant;
    }

    while(fgets(current_line, sizeof(current_line), cheats_file))
    {
      char *code_ptr;
      char extra;
      unsigned int raw_address;
      unsigned int raw_value;
      int address;
      int value;

      line_number++;
      if(line_is_truncated(current_line, cheats_file))
      {
        if(error_line)
          *error_line = line_number;
        return CHEAT_LOAD_ERROR_FORMAT;
      }
      trim_line_end(current_line);
      code_ptr = current_line;
      while((*code_ptr == ' ') || (*code_ptr == '\t'))
        code_ptr++;
      if(*code_ptr == '\0')
        break;

      if(sscanf(code_ptr, "%x %x %c", &raw_address, &raw_value, &extra) != 2)
      {
        if(error_line)
          *error_line = line_number;
        return CHEAT_LOAD_ERROR_FORMAT;
      }
      if(num_cheat_lines >=
       (sizeof(cheats[0].cheat_codes) / (sizeof(u32) * 2)))
      {
        if(error_line)
          *error_line = line_number;
        return CHEAT_LOAD_ERROR_CAPACITY;
      }

      address = (int)raw_address;
      value = (int)raw_value;
      if(current_cheat_variant == CHEAT_TYPE_CODEBREAKER)
      {
        u32 cb_address = (u32)address;
        u16 cb_value = (u16)value;

        if(raw_value > 0xFFFF)
        {
          if(error_line)
            *error_line = line_number;
          return CHEAT_LOAD_ERROR_FORMAT;
        }

        if(codebreaker_state.master)
          codebreaker_decrypt(&codebreaker_state, &cb_address, &cb_value);

        address = (int)cb_address;
        value = (int)cb_value;
        if(pending_codebreaker_data != 0)
        {
          pending_codebreaker_data--;
        }
        else
        {
          u32 opcode = cb_address >> 28;

          if(opcode == 0x4)
            pending_codebreaker_data = 1;
          else if(opcode == 0x5)
            pending_codebreaker_data = (cb_value + 2) / 3;
          else if((opcode == 0xD) &&
           ((cb_address & 0x0FFFFFFF) != 0x20))
          {
            if(error_line)
              *error_line = line_number;
            return CHEAT_LOAD_ERROR_FORMAT;
          }

          if(opcode == 0x9)
            codebreaker_reseed(&codebreaker_state, cb_address, cb_value);
          if(collect_hooks && (opcode == 0x1) &&
           !add_hook_address(configured_hook_addresses,
            &configured_hook_count, codebreaker_hook_address(cb_address)))
          {
            if(error_line)
              *error_line = line_number;
            return CHEAT_LOAD_ERROR_CAPACITY;
          }
        }
      }
      else if((current_cheat_variant != CHEAT_TYPE_DIRECT_V1) &&
       (current_cheat_variant != CHEAT_TYPE_DIRECT_V3))
        decrypt_gsa_code(&address, &value, current_cheat_variant);

      if(store)
      {
        cheats[cheat_count].cheat_codes[num_cheat_lines * 2] = (u32)address;
        cheats[cheat_count].cheat_codes[(num_cheat_lines * 2) + 1] =
         (u32)value;
      }
      num_cheat_lines++;
    }

    if((num_cheat_lines == 0) || (pending_codebreaker_data != 0))
    {
      if(error_line)
        *error_line = line_number;
      return CHEAT_LOAD_ERROR_FORMAT;
    }

    if(store)
      cheats[cheat_count].num_cheat_lines = num_cheat_lines;
    cheat_count++;
  }

  if(parsed_count)
    *parsed_count = cheat_count;
  return cheat_count ? CHEAT_LOAD_OK : CHEAT_LOAD_ERROR_EMPTY;
}

CHEAT_LOAD_RESULT load_cheats(const char *cheats_path, u32 *error_line)
{
  FILE *cheats_file;
  CHEAT_LOAD_RESULT result;
  u32 parsed_count = 0;
  u32 previous_count = num_cheats;
  const char *basename;

  if(error_line)
    *error_line = 0;
  cheats_file = fopen(cheats_path, "rb");
  if(cheats_file == NULL)
    return CHEAT_LOAD_ERROR_OPEN;

  configured_hook_count = 0;
  result = parse_cheats_file(cheats_file, 0, 1, &parsed_count, error_line);
  if(result != CHEAT_LOAD_OK)
  {
    fclose(cheats_file);
    rebuild_configured_hook_index();
    return result;
  }

  rewind(cheats_file);
  result = parse_cheats_file(cheats_file, 1, 0, &parsed_count, error_line);
  fclose(cheats_file);
  if(result != CHEAT_LOAD_OK)
  {
    clear_cheats();
    return result;
  }

  if(previous_count > parsed_count)
    memset(cheats + parsed_count, 0,
     (previous_count - parsed_count) * sizeof(CHEAT_TYPE));
  num_cheats = parsed_count;

  basename = strrchr(cheats_path, '/');
  if(basename)
    basename++;
  else
    basename = cheats_path;
  strncpy(loaded_cheat_filename, basename, MAX_FILE - 1);
  loaded_cheat_filename[MAX_FILE - 1] = '\0';

  rebuild_configured_hook_index();
  rebuild_active_cheat_indexes();
  flush_translation_cache(TRANSLATION_REGION_READONLY,
   FLUSH_REASON_INITIALIZING);
  flush_translation_cache(TRANSLATION_REGION_WRITABLE,
   FLUSH_REASON_INITIALIZING);
  return CHEAT_LOAD_OK;
}

static void process_cheat_gs1(CHEAT_TYPE *cheat)
{
  u32 cheat_opcode;
  u32 *code_ptr = cheat->cheat_codes;
  u32 address, value;
  u32 i;

  for(i = 0; i < cheat->num_cheat_lines; i++)
  {
    address = code_ptr[0];
    value = code_ptr[1];

    code_ptr += 2;

    cheat_opcode = address >> 28;
    address &= 0xFFFFFFF;

    switch(cheat_opcode)
    {
      case 0x0:
        write_memory8(address, value);
        break;

      case 0x1:
        write_memory16(address, value);
        break;

      case 0x2:
        write_memory32(address, value);
        break;

      case 0x3:
      {
        u32 num_addresses = address & 0xFFFF;
        u32 address1, address2;
        u32 i2;

        for(i2 = 0; i2 < num_addresses; i2++)
        {
          address1 = code_ptr[0];
          address2 = code_ptr[1];
          code_ptr += 2;
          i++;

          write_memory32(address1, value);
          if(address2 != 0)
            write_memory32(address2, value);
        }
        break;
      }

      // ROM patch not supported yet
      case 0x6:
        if(gamepak_file_large == -1)  // オンメモリのROMの場合だけ
        {
          ADDRESS16(gamepak_rom, (address * 2) - 0x08000000)
           = (value & 0xFFFF);  // データの書込み
        }
        break;

      // GS button down not supported yet
      case 0x8:
        break;

      // Reencryption (DEADFACE) not supported yet
      case 0xD:
        if(read_memory16(address) != (value & 0xFFFF))
        {
          code_ptr += 2;
          i++;
        }
        break;

      case 0xE:
        if(read_memory16(value & 0xFFFFFFF) != (address & 0xFFFF))
        {
          u32 skip = ((address >> 16) & 0x03);
          code_ptr += skip * 2;
          i += skip;
        }
        break;

      // Hook routine not supported yet (not important??)
      case 0x0F:
        break;
    }
  }
}

// These are especially incomplete.

static void process_cheat_gs3(CHEAT_TYPE *cheat)
{
  u32 cheat_opcode;
  u32 *code_ptr = cheat->cheat_codes;
  u32 address, value;
  u32 i;

  for(i = 0; i < cheat->num_cheat_lines; i++)
  {
    address = code_ptr[0];
    value = code_ptr[1];

    code_ptr += 2;

    cheat_opcode = address >> 28;
    address &= 0xFFFFFFF;

    switch(cheat_opcode)
    {
      case 0x0:
        cheat_opcode = address >> 24;
        address = (address & 0xFFFFF) + ((address << 4) & 0xF000000);

        switch(cheat_opcode)
        {
          case 0x0:
          {
            u32 iterations = value >> 8;
            u32 i2;

            value &= 0xFF;

            for(i2 = 0; i2 <= iterations; i2++, address++)
            {
              write_memory8(address, value);
            }
            break;
          }

          case 0x2:
          {
            u32 iterations = value >> 16;
            u32 i2;

            value &= 0xFFFF;

            for(i2 = 0; i2 <= iterations; i2++, address += 2)
            {
              write_memory16(address, value);
            }
            break;
          }

          case 0x4:
            write_memory32(address, value);
            break;
        }
        break;

      case 0x4:
        cheat_opcode = address >> 24;
        address = (address & 0xFFFFF) + ((address << 4) & 0xF000000);

        switch(cheat_opcode)
        {
          case 0x0:
            address = read_memory32(address) + (value >> 24);
            write_memory8(address, value & 0xFF);
            break;

          case 0x2:
            address = read_memory32(address) + ((value >> 16) * 2);
            write_memory16(address, value & 0xFFFF);
            break;

          case 0x4:
            address = read_memory32(address);
            write_memory32(address, value);
            break;

        }
        break;

      case 0x8:
        cheat_opcode = address >> 24;
        address = (address & 0xFFFFF) + ((address << 4) & 0xF000000);

        switch(cheat_opcode)
        {
          case 0x0:
            value = (value & 0xFF) + read_memory8(address);
            write_memory8(address, value);
            break;

          case 0x2:
            value = (value & 0xFFFF) + read_memory16(address);
            write_memory16(address, value);
            break;

          case 0x4:
            value = value + read_memory32(address);
            write_memory32(address, value);
            break;
        }
        break;

      case 0xC:
        cheat_opcode = address >> 24;
        address = (address & 0xFFFFFF) + 0x4000000;

        switch(cheat_opcode)
        {
          case 0x6:
            write_memory16(address, value);
            break;

          case 0x7:
            write_memory32(address, value);
            break;
        }
        break;
    }
  }
}

static u32 codebreaker_operation_lines(const CHEAT_TYPE *cheat, u32 line)
{
  u32 opcode;
  u32 lines = 1;

  if(line >= cheat->num_cheat_lines)
    return 0;

  opcode = cheat->cheat_codes[line * 2] >> 28;
  if(opcode == 0x4)
    lines = 2;
  else if(opcode == 0x5)
    lines += (cheat->cheat_codes[(line * 2) + 1] + 2) / 3;

  if(lines > (cheat->num_cheat_lines - line))
    lines = cheat->num_cheat_lines - line;
  return lines;
}

static void process_cheat_codebreaker(CHEAT_TYPE *cheat)
{
  u32 i;

  for(i = 0; i < cheat->num_cheat_lines; i++)
  {
    u32 op1 = cheat->cheat_codes[i * 2];
    u16 op2 = cheat->cheat_codes[(i * 2) + 1];
    u32 opcode = op1 >> 28;
    u32 address = op1 & 0x0FFFFFFF;
    u32 condition = 1;
    u32 is_condition = 0;

    switch(opcode)
    {
      case 0x0: /* Game ID */
      case 0x1: /* Timed hook, handled by the dynarec callback */
      case 0x9: /* Encryption seed, decoded while loading the file */
        break;

      case 0x2:
        address &= ~1u;
        write_memory16(address, read_memory16(address) | op2);
        break;

      case 0x3:
        write_memory8(address, op2);
        break;

      case 0x4:
        if((i + 1) < cheat->num_cheat_lines)
        {
          u32 parameters = cheat->cheat_codes[(i + 1) * 2];
          u16 address_offset = cheat->cheat_codes[((i + 1) * 2) + 1];
          u32 repeat = parameters & 0xFFFF;
          u16 operand = op2;
          u16 operand_offset = parameters >> 16;
          u32 j;

          address &= ~1u;
          for(j = 0; j < repeat; j++)
          {
            write_memory16(address, operand);
            address += address_offset;
            operand += operand_offset;
          }
          i++;
        }
        break;

      case 0x5:
      {
        u32 remaining = (u32)op2 * 2;
        u32 next_line = i + 1;

        while(remaining && (next_line < cheat->num_cheat_lines))
        {
          u32 values = cheat->cheat_codes[next_line * 2];
          u16 value3 = cheat->cheat_codes[(next_line * 2) + 1];
          u8 bytes[6];
          u32 byte;

          codebreaker_load_bytes(bytes, values, value3);
          for(byte = 0; (byte < sizeof(bytes)) && remaining; byte++)
          {
            write_memory8(address++, bytes[byte]);
            remaining--;
          }
          next_line++;
        }
        i = next_line - 1;
        break;
      }

      case 0x6:
        address &= ~1u;
        write_memory16(address, read_memory16(address) & op2);
        break;

      case 0x7:
        address &= ~1u;
        condition = read_memory16(address) == op2;
        is_condition = 1;
        break;

      case 0x8:
        address &= ~1u;
        write_memory16(address, op2);
        break;

      case 0xA:
        address &= ~1u;
        condition = read_memory16(address) != op2;
        is_condition = 1;
        break;

      case 0xB:
        address &= ~1u;
        condition = read_memory16(address) > op2;
        is_condition = 1;
        break;

      case 0xC:
        address &= ~1u;
        condition = read_memory16(address) < op2;
        is_condition = 1;
        break;

      case 0xD:
        condition = (address == 0x20) &&
         ((read_memory16(0x04000130) & op2) == 0);
        is_condition = 1;
        break;

      case 0xE:
        if(address & 1)
        {
          address &= ~1u;
          write_memory32(address, read_memory32(address) + (s32)(s16)op2);
        }
        else
        {
          write_memory16(address, read_memory16(address) + op2);
        }
        break;

      case 0xF:
        address &= ~1u;
        condition = (read_memory16(address) & op2) != 0;
        is_condition = 1;
        break;
    }

    if(is_condition && !condition && ((i + 1) < cheat->num_cheat_lines))
      i += codebreaker_operation_lines(cheat, i + 1);
  }
}

static u32 codebreaker_hook_address(u32 op1)
{
  return 0x08000000 | (op1 & 0x01FFFFFF);
}

u32 cheat_hook_configured(u32 pc)
{
  return hook_address_present(configured_hook_addresses,
   configured_hook_count, pc & ~1u);
}

void process_cheats_at_hook(u32 pc)
{
  u32 i;

  if((active_dispatch_hook == 0) ||
   (active_dispatch_hook != (pc & ~1u)))
    return;

  for(i = 0; i < active_codebreaker_count; i++)
    process_cheat_codebreaker(cheats + active_codebreaker_indexes[i]);
}


void process_cheats(void)
{
  u32 i;

  if(active_cheat_count == 0)
    return;

  for(i = 0; i < active_cheat_count; i++)
  {
    CHEAT_TYPE *cheat = cheats + active_cheat_indexes[i];

    switch(cheat->cheat_variant)
    {
      case CHEAT_TYPE_GAMESHARK_V1:
      case CHEAT_TYPE_DIRECT_V1:
        process_cheat_gs1(cheat);
        break;

      case CHEAT_TYPE_GAMESHARK_V3:
      case CHEAT_TYPE_DIRECT_V3:
        process_cheat_gs3(cheat);
        break;

      case CHEAT_TYPE_CODEBREAKER:
        if(active_dispatch_hook == 0)
          process_cheat_codebreaker(cheat);
        break;

      default:
        break;
    }
  }
}
