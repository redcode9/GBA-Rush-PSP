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

/*
 * CodeBreaker codes: decryption, payload line counts and execution, used by
 * cheats.c. Free of emulator headers so tests/test_codebreaker.c runs it on
 * the host; memory goes through the codebreaker_memory_* functions, which
 * cheats.c defines.
 */

#ifndef CODEBREAKER_H
#define CODEBREAKER_H

#include <stdint.h>

/* Decryption state set up by a 9xxxxxxx seed code; master is that code
 * (0 = lines are not encrypted). table permutes the 48 bits of a line. */
typedef struct
{
  uint32_t master;
  uint32_t seeds[4];
  uint32_t rng_state;
  uint8_t table[48];
} CODEBREAKER_STATE;

void codebreaker_state_reset(CODEBREAKER_STATE *state);
void codebreaker_reseed(CODEBREAKER_STATE *state, uint32_t op1,
 uint16_t op2);
void codebreaker_decrypt(CODEBREAKER_STATE *state, uint32_t *op1,
 uint16_t *op2);
void codebreaker_unpack(uint8_t *buffer, uint32_t op1, uint16_t op2);
uint32_t codebreaker_payload_lines(uint32_t op1, uint16_t op2);
void codebreaker_execute(const uint32_t *codes, uint32_t line_count);

uint16_t codebreaker_memory_read16(uint32_t address);
uint32_t codebreaker_memory_read32(uint32_t address);
void codebreaker_memory_write8(uint32_t address, uint8_t value);
void codebreaker_memory_write16(uint32_t address, uint16_t value);
void codebreaker_memory_write32(uint32_t address, uint32_t value);

#endif
