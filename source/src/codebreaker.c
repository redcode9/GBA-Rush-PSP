/* unofficial gameplaySP kai
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 * Copyright (C) 2007 takka@fb2 <takkees@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <stddef.h>
#include <string.h>

#include "codebreaker.h"

/* Three steps of the 0x41C64E6D / 0x3039 LCG folded into one 32-bit
 * value. */
static uint32_t codebreaker_rand(CODEBREAKER_STATE *state)
{
  uint32_t roll = state->rng_state * 0x41C64E6D + 0x3039;
  uint32_t roll2 = roll * 0x41C64E6D + 0x3039;
  uint32_t roll3 = roll2 * 0x41C64E6D + 0x3039;
  uint32_t mix = (roll << 14) & 0xC0000000;

  mix |= (roll2 >> 1) & 0x3FFF8000;
  mix |= (roll3 >> 16) & 0x7FFF;
  state->rng_state = roll3;
  return mix;
}

/* Next random value modulo the 48-entry table, computed with a
 * shift-and-subtract remainder. */
static uint32_t codebreaker_swap_index(CODEBREAKER_STATE *state)
{
  uint32_t roll = codebreaker_rand(state);
  uint32_t count = sizeof(state->table);
  uint32_t bit = 1;
  uint32_t mask;

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

void codebreaker_state_reset(CODEBREAKER_STATE *state)
{
  memset(state, 0, sizeof(*state));
}

/* Builds the decryption state from a 9xxxxxxx code: shuffles the bit table,
 * then derives the four XOR keys from op1 bits 24-27 and op2. */
void codebreaker_reseed(CODEBREAKER_STATE *state, uint32_t op1,
 uint16_t op2)
{
  uint32_t i;

  state->rng_state = (op2 & 0xFF) ^ 0x1111;
  for(i = 0; i < sizeof(state->table); i++)
    state->table[i] = i;

  for(i = 0; i < 0x50; i++)
  {
    uint32_t x = codebreaker_swap_index(state);
    uint32_t y = codebreaker_swap_index(state);
    uint8_t swap = state->table[x];
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

/* A code line as 6 big-endian bytes: 4 from op1, then 2 from op2. */
void codebreaker_unpack(uint8_t *buffer, uint32_t op1, uint16_t op2)
{
  buffer[0] = op1 >> 24;
  buffer[1] = op1 >> 16;
  buffer[2] = op1 >> 8;
  buffer[3] = op1;
  buffer[4] = op2 >> 8;
  buffer[5] = op2;
}

/* Data lines that follow a code: one for a 4xxxxxxx slide, and op2 * 2
 * bytes at 6 per line for a 5xxxxxxx byte string. */
uint32_t codebreaker_payload_lines(uint32_t op1, uint16_t op2)
{
  switch(op1 >> 28)
  {
    case 0x4:
      return 1;

    case 0x5:
      return (op2 + 2) / 3;

    default:
      return 0;
  }
}

static uint32_t codebreaker_operation_lines(const uint32_t *codes,
 uint32_t line_count, uint32_t line)
{
  uint32_t lines;

  if(line >= line_count)
    return 0;

  lines = 1 + codebreaker_payload_lines(codes[line * 2],
   (uint16_t)codes[(line * 2) + 1]);
  if(lines > (line_count - line))
    lines = line_count - line;
  return lines;
}

/* Runs decrypted (op1, op2) lines. A false condition code skips the next
 * code together with its data lines. */
void codebreaker_execute(const uint32_t *codes, uint32_t line_count)
{
  uint32_t i;

  for(i = 0; i < line_count; i++)
  {
    uint32_t op1 = codes[i * 2];
    uint16_t op2 = (uint16_t)codes[(i * 2) + 1];
    uint32_t opcode = op1 >> 28;
    uint32_t address = op1 & 0x0FFFFFFF;
    uint32_t condition = 1;
    uint32_t is_condition = 0;

    switch(opcode)
    {
      case 0x0: /* Game ID */
      case 0x1: /* Timed hook */
      case 0x9: /* Encryption seed */
        break;

      case 0x2: /* OR halfword */
        address &= ~1u;
        codebreaker_memory_write16(address,
         codebreaker_memory_read16(address) | op2);
        break;

      case 0x3: /* Write byte */
        codebreaker_memory_write8(address, (uint8_t)op2);
        break;

      case 0x4: /* Slide: halfword writes stepping address and value */
        if((i + 1) < line_count)
        {
          uint32_t parameters = codes[(i + 1) * 2];
          uint16_t address_offset = (uint16_t)codes[((i + 1) * 2) + 1];
          uint32_t repeat = parameters & 0xFFFF;
          uint16_t operand = op2;
          uint16_t operand_offset = parameters >> 16;
          uint32_t j;

          address &= ~1u;
          for(j = 0; j < repeat; j++)
          {
            codebreaker_memory_write16(address, operand);
            address += address_offset;
            operand += operand_offset;
          }
          i++;
        }
        break;

      case 0x5: /* Write the bytes carried by the following lines */
      {
        uint32_t remaining = (uint32_t)op2 * 2;
        uint32_t next_line = i + 1;

        while(remaining && (next_line < line_count))
        {
          uint32_t values = codes[next_line * 2];
          uint16_t value3 = (uint16_t)codes[(next_line * 2) + 1];
          uint8_t bytes[6];
          uint32_t byte;

          codebreaker_unpack(bytes, values, value3);
          for(byte = 0; (byte < sizeof(bytes)) && remaining; byte++)
          {
            codebreaker_memory_write8(address++, bytes[byte]);
            remaining--;
          }
          next_line++;
        }
        i = next_line - 1;
        break;
      }

      case 0x6: /* AND halfword */
        address &= ~1u;
        codebreaker_memory_write16(address,
         codebreaker_memory_read16(address) & op2);
        break;

      case 0x7: /* If equal */
        address &= ~1u;
        condition = codebreaker_memory_read16(address) == op2;
        is_condition = 1;
        break;

      case 0x8: /* Write halfword */
        address &= ~1u;
        codebreaker_memory_write16(address, op2);
        break;

      case 0xA: /* If not equal */
        address &= ~1u;
        condition = codebreaker_memory_read16(address) != op2;
        is_condition = 1;
        break;

      case 0xB: /* If greater */
        address &= ~1u;
        condition = codebreaker_memory_read16(address) > op2;
        is_condition = 1;
        break;

      case 0xC: /* If less */
        address &= ~1u;
        condition = codebreaker_memory_read16(address) < op2;
        is_condition = 1;
        break;

      case 0xD: /* If keys pressed (KEYINPUT at 0x4000130 is active low) */
        condition = (address == 0x20) &&
         ((codebreaker_memory_read16(0x04000130) & op2) == 0);
        is_condition = 1;
        break;

      case 0xE: /* Add; an odd address adds signed op2 to a word */
        if(address & 1)
        {
          address &= ~1u;
          codebreaker_memory_write32(address,
           codebreaker_memory_read32(address) + (int32_t)(int16_t)op2);
        }
        else
        {
          codebreaker_memory_write16(address,
           codebreaker_memory_read16(address) + op2);
        }
        break;

      case 0xF: /* If the halfword has any bit of op2 set */
        address &= ~1u;
        condition = (codebreaker_memory_read16(address) & op2) != 0;
        is_condition = 1;
        break;
    }

    if(is_condition && !condition && ((i + 1) < line_count))
      i += codebreaker_operation_lines(codes, line_count, i + 1);
  }
}

static void codebreaker_pack(const uint8_t *buffer, uint32_t *op1,
 uint16_t *op2)
{
  *op1 = ((uint32_t)buffer[0] << 24) |
   ((uint32_t)buffer[1] << 16) | ((uint32_t)buffer[2] << 8) | buffer[3];
  *op2 = ((uint16_t)buffer[4] << 8) | buffer[5];
}

/* Undoes the bit permutation, XORs with the first key pair, unchains the
 * bytes using the master code, then XORs with the second key pair. */
void codebreaker_decrypt(CODEBREAKER_STATE *state, uint32_t *op1,
 uint16_t *op2)
{
  uint8_t buffer[6];
  int32_t i;
  uint32_t master;

  codebreaker_unpack(buffer, *op1, *op2);
  for(i = sizeof(state->table) - 1; i >= 0; i--)
  {
    uint32_t offset_x = i >> 3;
    uint32_t offset_y = state->table[i] >> 3;
    uint32_t bit_x = i & 7;
    uint32_t bit_y = state->table[i] & 7;
    uint8_t x = (buffer[offset_x] >> bit_x) & 1;
    uint8_t y = (buffer[offset_y] >> bit_y) & 1;

    buffer[offset_x] &= ~(1 << bit_x);
    if(y)
      buffer[offset_x] |= 1 << bit_x;
    buffer[offset_y] &= ~(1 << bit_y);
    if(x)
      buffer[offset_y] |= 1 << bit_y;
  }
  codebreaker_pack(buffer, op1, op2);

  *op1 ^= state->seeds[0];
  *op2 ^= state->seeds[1];

  codebreaker_unpack(buffer, *op1, *op2);
  master = state->master;
  for(i = 0; i < 5; i++)
    buffer[i] ^= (master >> 8) ^ buffer[i + 1];
  buffer[5] ^= master >> 8;

  for(i = 5; i > 0; i--)
    buffer[i] ^= master ^ buffer[i - 1];
  buffer[0] ^= master;
  codebreaker_pack(buffer, op1, op2);

  *op1 ^= state->seeds[2];
  *op2 ^= state->seeds[3];
}
