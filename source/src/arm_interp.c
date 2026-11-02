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
 * Interpreter of the IR in arm_ir.h, with the block cache, register
 * banking, exceptions and the run loop. Host memory is accessed little
 * endian, as on the PSP and every host the tests run on.
 */

#include "arm_core.h"

#include <string.h>

#define CPSR_FLAGS (ARM_CPSR_N | ARM_CPSR_Z | ARM_CPSR_C | ARM_CPSR_V)

static uint32_t ror32(uint32_t value, unsigned amount)
{
  amount &= 31;
  return amount ? (value >> amount) | (value << (32 - amount)) : value;
}

/* Address bits 24-27 pick the region; anything above 0FFFFFFFh is
   unmapped, like region 1. */
static unsigned region_of(uint32_t address)
{
  return (address >> 28) ? 1 : address >> 24;
}

unsigned arm_access_cycles(const ArmCore *core, uint32_t address,
 unsigned size, int sequential)
{
  unsigned region = region_of(address);

  /* Every 128 KB block of ROM starts with a non-sequential access. */
  if(sequential && region >= 8 && region <= 13 && !(address & 0x1FFFF))
    sequential = 0;
  return core->wait[region][(size == 4 ? 2 : 0) + (sequential ? 1 : 0)];
}

static void set_waits(ArmCore *core)
{
  static const uint8_t first[4] = { 4, 3, 2, 8 };
  static const uint8_t second[3][2] = { { 2, 1 }, { 4, 1 }, { 8, 1 } };
  unsigned w = core->waitcnt;
  unsigned r;

  for(r = 0; r < 16; r++)
    memset(core->wait[r], 1, 4);
  /* 256 KB work RAM: 16-bit bus, two waitstates. */
  core->wait[2][0] = core->wait[2][1] = 3;
  core->wait[2][2] = core->wait[2][3] = 6;
  /* Palette and VRAM: 16-bit bus. */
  core->wait[5][2] = core->wait[5][3] = 2;
  core->wait[6][2] = core->wait[6][3] = 2;
  for(r = 0; r < 3; r++)
  {
    unsigned n = 1u + first[(w >> (2 + 3 * r)) & 3];
    unsigned s = 1u + second[r][(w >> (4 + 3 * r)) & 1];
    unsigned region = 8 + 2 * r;

    core->wait[region][0] = core->wait[region + 1][0] = (uint8_t)n;
    core->wait[region][1] = core->wait[region + 1][1] = (uint8_t)s;
    /* 32 bits over the 16-bit cartridge bus take a second, sequential
       access. */
    core->wait[region][2] = core->wait[region + 1][2] = (uint8_t)(n + s);
    core->wait[region][3] = core->wait[region + 1][3] = (uint8_t)(2 * s);
  }
  memset(core->wait[14], 1 + first[w & 3], 4);
  memset(core->wait[15], 1 + first[w & 3], 4);
}

/* ---- Registers and modes ------------------------------------------------ */

/* Register bank of a mode: 0 user and system, then FIQ, IRQ, supervisor,
   abort, undefined. */
static unsigned bank_of(unsigned mode)
{
  switch(mode)
  {
    case ARM_MODE_FIQ: return 1;
    case ARM_MODE_IRQ: return 2;
    case ARM_MODE_SUPERVISOR: return 3;
    case ARM_MODE_ABORT: return 4;
    case ARM_MODE_UNDEFINED: return 5;
    default: return 0;
  }
}

static void switch_mode(ArmCore *core, unsigned mode)
{
  unsigned old_bank = bank_of(core->cpsr & ARM_CPSR_MODE);
  unsigned new_bank = bank_of(mode);

  if(old_bank != new_bank)
  {
    if(old_bank == 1 || new_bank == 1)
    {
      memcpy(core->bank_r8_r12[old_bank == 1], &core->v[8], 5 * 4);
      memcpy(&core->v[8], core->bank_r8_r12[new_bank == 1], 5 * 4);
    }
    core->bank_r13_r14[old_bank][0] = core->v[13];
    core->bank_r13_r14[old_bank][1] = core->v[14];
    core->v[13] = core->bank_r13_r14[new_bank][0];
    core->v[14] = core->bank_r13_r14[new_bank][1];
    core->bank_spsr[old_bank] = core->spsr;
    core->spsr = core->bank_spsr[new_bank];
  }
  core->cpsr = (core->cpsr & ~ARM_CPSR_MODE) | mode;
}

/* The reserved bits 8-27 keep what MSR wrote there, as in mGBA and the
   SingleStepTests reference. */
uint32_t arm_core_cpsr(const ArmCore *core)
{
  return (core->flag_n & ARM_CPSR_N) | (core->flag_z ? 0 : ARM_CPSR_Z) |
   ((core->flag_c & 1) << 29) | ((core->flag_v >> 3) & ARM_CPSR_V) |
   (core->cpsr & ~CPSR_FLAGS);
}

void arm_core_set_cpsr(ArmCore *core, uint32_t cpsr)
{
  switch_mode(core, cpsr & ARM_CPSR_MODE);
  core->cpsr = cpsr & ~CPSR_FLAGS;
  core->flag_n = cpsr & ARM_CPSR_N;
  core->flag_z = (cpsr & ARM_CPSR_Z) ? 0 : 1;
  core->flag_c = (cpsr >> 29) & 1;
  core->flag_v = (cpsr << 3) & ARM_CPSR_N;
}

uint32_t *arm_core_register(ArmCore *core, unsigned mode, unsigned reg)
{
  unsigned current = bank_of(core->cpsr & ARM_CPSR_MODE);
  unsigned wanted = bank_of(mode);

  if(reg == 16)
    return current == wanted ? &core->spsr : &core->bank_spsr[wanted];
  if(reg >= 13 && reg <= 14 && current != wanted)
    return &core->bank_r13_r14[wanted][reg - 13];
  if(reg >= 8 && reg <= 12 && (current == 1) != (wanted == 1))
    return &core->bank_r8_r12[wanted == 1][reg - 8];
  return &core->v[reg & 15];
}

static uint32_t *user_register(ArmCore *core, unsigned reg)
{
  return arm_core_register(core, ARM_MODE_USER, reg);
}

/* User and system mode have no SPSR. */
static int has_spsr(const ArmCore *core)
{
  return bank_of(core->cpsr & ARM_CPSR_MODE) != 0;
}

/* MSR: fields bits 0-3 select the control, extension, status and flags
   bytes, bit 4 the SPSR. User mode may only change the top byte. */
static void write_psr(ArmCore *core, uint32_t value, unsigned fields)
{
  uint32_t mask = 0;
  unsigned i;

  for(i = 0; i < 4; i++)
    if(fields & (1u << i))
      mask |= 0xFFu << (8 * i);
  /* Mode bit 4 is always set: the ARM7TDMI has no 26-bit modes. */
  if(fields & 1)
    value |= 0x10u;
  if(fields & 16)
  {
    if(has_spsr(core))
      core->spsr = (core->spsr & ~mask) | (value & mask);
    return;
  }
  if((core->cpsr & ARM_CPSR_MODE) == ARM_MODE_USER)
    mask &= 0xFF000000u;
  arm_core_set_cpsr(core, (arm_core_cpsr(core) & ~mask) | (value & mask));
}

static void exception(ArmCore *core, unsigned mode, uint32_t vector,
 uint32_t link)
{
  uint32_t old = arm_core_cpsr(core);

  switch_mode(core, mode);
  core->spsr = old;
  core->v[14] = link;
  core->cpsr = (core->cpsr & ~ARM_CPSR_T) | ARM_CPSR_I;
  core->v[15] = vector;
}

/* Cycles to refill the pipeline at the new r15. */
static unsigned refill(const ArmCore *core)
{
  unsigned width = (core->cpsr & ARM_CPSR_T) ? 2 : 4;

  return arm_access_cycles(core, core->v[15], width, 0) +
   arm_access_cycles(core, core->v[15] + width, width, 1);
}

int arm_core_irq(ArmCore *core)
{
  if(core->cpsr & ARM_CPSR_I)
    return 0;
  /* lr points one instruction past the one to return to, so the handler
     returns with subs pc, lr, #4 in either state. */
  exception(core, ARM_MODE_IRQ, 0x18, core->v[15] + 4);
  return (int)refill(core);
}

void arm_core_request_exit(ArmCore *core)
{
  core->exit_request |= ARM_EXIT_LEAVE_BLOCK | ARM_EXIT_STOP;
}

/* Sets r15 (and with bx the state, from bit 0) and aligns it. */
static void set_pc(ArmCore *core, uint32_t target, int bx)
{
  if(bx)
    core->cpsr = (core->cpsr & ~ARM_CPSR_T) | ((target & 1) << 5);
  core->v[15] = target & ((core->cpsr & ARM_CPSR_T) ? ~1u : ~3u);
}

static int condition(const ArmCore *core, unsigned cond)
{
  unsigned n = core->flag_n >> 31;
  unsigned z = core->flag_z == 0;
  unsigned c = core->flag_c & 1;
  unsigned v = core->flag_v >> 31;

  switch(cond)
  {
    case ARM_COND_EQ: return z;
    case ARM_COND_NE: return !z;
    case ARM_COND_CS: return c;
    case ARM_COND_CC: return !c;
    case ARM_COND_MI: return n;
    case ARM_COND_PL: return !n;
    case ARM_COND_VS: return v;
    case ARM_COND_VC: return !v;
    case ARM_COND_HI: return c && !z;
    case ARM_COND_LS: return !c || z;
    case ARM_COND_GE: return n == v;
    case ARM_COND_LT: return n != v;
    case ARM_COND_GT: return !z && n == v;
    case ARM_COND_LE: return z || n != v;
    case ARM_COND_AL: return 1;
    default: return 0;
  }
}

/* ---- Block cache -------------------------------------------------------- */

static unsigned hash_of(uint32_t key)
{
  return ((key >> 1) ^ (key >> 13)) & (ARM_CACHE_HASH - 1);
}

/* Index of the work RAM page holding address, or -1. */
static int code_page(uint32_t address)
{
  switch(region_of(address))
  {
    case 2: return (int)((address & 0x3FFFFu) / ARM_CODE_PAGE_SIZE);
    case 3: return 1024 + (int)((address & 0x7FFFu) / ARM_CODE_PAGE_SIZE);
    default: return -1;
  }
}

void arm_core_flush(ArmCore *core)
{
  core->block_count = 0;
  core->op_count = 0;
  core->flushes++;
  memset(core->hash, 0xFF, sizeof(core->hash));
  memset(core->page_blocks, 0xFF, sizeof(core->page_blocks));
  core->exit_request |= ARM_EXIT_LEAVE_BLOCK;
}

static void unlink_block(ArmCore *core, unsigned index)
{
  uint16_t *link = &core->hash[hash_of(core->blocks[index].key)];

  while(*link != ARM_NO_BLOCK)
  {
    if(*link == index)
    {
      *link = core->blocks[index].next_hash;
      return;
    }
    link = &core->blocks[*link].next_hash;
  }
}

/* Forgets every block translated from a page. The block running may be
   one of them, so it is left at the next instruction boundary. */
static void invalidate_page(ArmCore *core, unsigned page)
{
  unsigned index = core->page_blocks[page];

  while(index != ARM_NO_BLOCK)
  {
    unlink_block(core, index);
    index = core->blocks[index].next_page;
  }
  core->page_blocks[page] = ARM_NO_BLOCK;
  core->exit_request |= ARM_EXIT_LEAVE_BLOCK;
}

void arm_core_invalidate(ArmCore *core, uint32_t address, uint32_t size)
{
  uint32_t end = address + size;

  address &= ~(ARM_CODE_PAGE_SIZE - 1);
  for(; address < end; address += ARM_CODE_PAGE_SIZE)
  {
    int page = code_page(address);

    if(page >= 0 && core->page_blocks[page] != ARM_NO_BLOCK)
      invalidate_page(core, (unsigned)page);
  }
}

static ArmBlock *lookup(ArmCore *core, uint32_t key)
{
  unsigned index = core->hash[hash_of(key)];

  while(index != ARM_NO_BLOCK)
  {
    if(core->blocks[index].key == key)
      return &core->blocks[index];
    index = core->blocks[index].next_hash;
  }
  return NULL;
}

static ArmBlock *translate(ArmCore *core, uint32_t key)
{
  ArmBlock *block;
  ArmIrBlockInfo info;
  unsigned index;
  unsigned bucket = hash_of(key);
  int page;

  if(core->block_count >= ARM_CACHE_BLOCKS ||
     core->op_count + ARM_BLOCK_MAX_OPS > ARM_CACHE_OPS)
    arm_core_flush(core);
  index = core->block_count++;
  block = &core->blocks[index];
  block->key = key;
  block->first = core->op_count;
  block->count = (uint16_t)arm_translate(core, key & ~1u, (int)(key & 1),
   &core->ops[core->op_count], ARM_BLOCK_MAX_OPS, ARM_BLOCK_MAX_INSNS,
   &info);
  block->insns = (uint16_t)info.insns;
  core->op_count += block->count;
  block->next_hash = core->hash[bucket];
  core->hash[bucket] = (uint16_t)index;
  block->next_page = ARM_NO_BLOCK;
  page = code_page(key);
  if(page >= 0)
  {
    block->next_page = core->page_blocks[page];
    core->page_blocks[page] = (uint16_t)index;
  }
  return block;
}

/* ---- Memory ------------------------------------------------------------ */

static void count_access(ArmCore *core, uint32_t address, unsigned size,
 unsigned flags)
{
  if(!(flags & ARM_IR_FOLDED))
    core->cycles += (int32_t)arm_access_cycles(core, address, size,
     flags & ARM_IR_SEQ);
}

static uint32_t read32(ArmCore *core, uint32_t address)
{
  const ArmRegion *map = &core->map[region_of(address)];
  uint32_t value;

  if(map->read)
  {
    memcpy(&value, map->read + (address & map->mask), 4);
    return value;
  }
  return core->bus.read32(core->bus.context, address);
}

static uint32_t read16(ArmCore *core, uint32_t address)
{
  const ArmRegion *map = &core->map[region_of(address)];
  uint16_t value;

  if(map->read)
  {
    memcpy(&value, map->read + (address & map->mask), 2);
    return value;
  }
  return core->bus.read16(core->bus.context, address);
}

static uint32_t read8(ArmCore *core, uint32_t address)
{
  const ArmRegion *map = &core->map[region_of(address)];

  if(map->read)
    return map->read[address & map->mask];
  return core->bus.read8(core->bus.context, address);
}

/* A write to work RAM holding translated code drops those translations. */
static const ArmRegion *write_region(ArmCore *core, uint32_t address)
{
  int page = code_page(address);

  if(page >= 0 && core->page_blocks[page] != ARM_NO_BLOCK)
    invalidate_page(core, (unsigned)page);
  return &core->map[region_of(address)];
}

static void write32(ArmCore *core, uint32_t address, uint32_t value)
{
  const ArmRegion *map = write_region(core, address);

  if(map->write)
    memcpy(map->write + (address & map->mask), &value, 4);
  else
    core->bus.write32(core->bus.context, address, value);
}

static void write16(ArmCore *core, uint32_t address, uint32_t value)
{
  const ArmRegion *map = write_region(core, address);
  uint16_t half = (uint16_t)value;

  if(map->write)
    memcpy(map->write + (address & map->mask), &half, 2);
  else
    core->bus.write16(core->bus.context, address, half);
}

static void write8(ArmCore *core, uint32_t address, uint32_t value)
{
  const ArmRegion *map = write_region(core, address);

  if(map->write)
    map->write[address & map->mask] = (uint8_t)value;
  else
    core->bus.write8(core->bus.context, address, value & 0xFF);
}

/* ---- Execution --------------------------------------------------------- */

static uint32_t shift_left(uint32_t a, unsigned amount,
 uint32_t *carry)
{
  if(amount == 0)
    return a;
  if(amount < 32)
  {
    *carry = (a >> (32 - amount)) & 1;
    return a << amount;
  }
  *carry = amount == 32 ? a & 1 : 0;
  return 0;
}

static uint32_t shift_right(uint32_t a, unsigned amount, uint32_t *carry)
{
  if(amount == 0)
    return a;
  if(amount < 32)
  {
    *carry = (a >> (amount - 1)) & 1;
    return a >> amount;
  }
  *carry = amount == 32 ? a >> 31 : 0;
  return 0;
}

static uint32_t shift_arithmetic(uint32_t a, unsigned amount,
 uint32_t *carry)
{
  if(amount == 0)
    return a;
  if(amount < 32)
  {
    *carry = (a >> (amount - 1)) & 1;
    return (uint32_t)((int32_t)a >> amount);
  }
  *carry = a >> 31;
  return (uint32_t)((int32_t)a >> 31);
}

static uint32_t rotate_right(uint32_t a, unsigned amount, uint32_t *carry)
{
  if(amount == 0)
    return a;
  a = ror32(a, amount);
  *carry = a >> 31;
  return a;
}

/* Runs a block's ops until an exit, leaving the next address in r15. */
static ArmExitKind execute(ArmCore *core, const ArmIrOp *op)
{
  uint32_t *v = core->v;

  for(;; op++)
  {
    unsigned flags = op->flags;
    uint32_t a = v[op->a];
    uint32_t b = (flags & ARM_IR_IMM) ? op->b : v[op->b & 0xFF];
    uint32_t result;
    uint32_t carry = core->flag_c;
    uint32_t address;

    switch(op->code)
    {
      case ARM_IR_NOP:
        continue;
      case ARM_IR_INSN:
        if((flags & ARM_IR_CHECK) && core->exit_request)
        {
          v[15] = op->b;
          return ARM_EXIT_NEXT;
        }
        core->cycles += op->a | (op->d << 8);
        core->insn_pc = op->b;
        core->instructions++;
        continue;
      case ARM_IR_SKIP:
        if(condition(core, op->a))
          core->cycles += (int32_t)(op->b >> 16);
        else
          op += op->b & 0xFFFF;
        continue;
      case ARM_IR_CYCLES:
        core->cycles += (int32_t)op->b;
        continue;

      case ARM_IR_MOV: result = b; break;
      case ARM_IR_MVN: result = ~b; break;
      case ARM_IR_AND: result = a & b; break;
      case ARM_IR_EOR: result = a ^ b; break;
      case ARM_IR_ORR: result = a | b; break;
      case ARM_IR_BIC: result = a & ~b; break;
      case ARM_IR_ADD:
        result = a + b;
        if(flags & ARM_IR_SET_C)
          core->flag_c = result < a;
        if(flags & ARM_IR_SET_V)
          core->flag_v = (a ^ result) & (b ^ result);
        break;
      case ARM_IR_ADC:
        result = a + b + carry;
        if(flags & ARM_IR_SET_C)
          core->flag_c = carry ? result <= a : result < a;
        if(flags & ARM_IR_SET_V)
          core->flag_v = (a ^ result) & (b ^ result);
        break;
      case ARM_IR_SUB:
        result = a - b;
        if(flags & ARM_IR_SET_C)
          core->flag_c = a >= b;
        if(flags & ARM_IR_SET_V)
          core->flag_v = (a ^ b) & (a ^ result);
        break;
      case ARM_IR_SBC:
        result = a - b - (carry ^ 1);
        if(flags & ARM_IR_SET_C)
          core->flag_c = carry ? a >= b : a > b;
        if(flags & ARM_IR_SET_V)
          core->flag_v = (a ^ b) & (a ^ result);
        break;
      case ARM_IR_RSB:
        result = b - a;
        if(flags & ARM_IR_SET_C)
          core->flag_c = b >= a;
        if(flags & ARM_IR_SET_V)
          core->flag_v = (b ^ a) & (b ^ result);
        break;
      case ARM_IR_RSC:
        result = b - a - (carry ^ 1);
        if(flags & ARM_IR_SET_C)
          core->flag_c = carry ? b >= a : b > a;
        if(flags & ARM_IR_SET_V)
          core->flag_v = (b ^ a) & (b ^ result);
        break;
      case ARM_IR_LSL:
        if(flags & ARM_IR_IMM)
        {
          carry = (a >> (32 - b)) & 1;
          result = a << b;
        }
        else
          result = shift_left(a, b & 0xFF, &carry);
        if(flags & ARM_IR_SET_C)
          core->flag_c = carry;
        break;
      case ARM_IR_LSR:
        result = shift_right(a, (flags & ARM_IR_IMM) ? b : b & 0xFF, &carry);
        if(flags & ARM_IR_SET_C)
          core->flag_c = carry;
        break;
      case ARM_IR_ASR:
        result = shift_arithmetic(a, (flags & ARM_IR_IMM) ? b : b & 0xFF,
         &carry);
        if(flags & ARM_IR_SET_C)
          core->flag_c = carry;
        break;
      case ARM_IR_ROR:
        if((flags & ARM_IR_IMM) || (b & 0xFF) == 0)
          result = rotate_right(a, b & 0xFF, &carry);
        else
          result = rotate_right(a, ((b - 1) & 31) + 1, &carry);
        if(flags & ARM_IR_SET_C)
          core->flag_c = carry;
        break;
      case ARM_IR_RRX:
        result = (carry << 31) | (a >> 1);
        if(flags & ARM_IR_SET_C)
          core->flag_c = a & 1;
        break;
      case ARM_IR_MUL:
        result = a * b;
        break;
      case ARM_IR_UMULH:
        result = (uint32_t)(((uint64_t)a * b) >> 32);
        break;
      case ARM_IR_SMULH:
        result = (uint32_t)((uint64_t)((int64_t)(int32_t)a *
         (int32_t)b) >> 32);
        break;
      case ARM_IR_SLTU:
        result = a < b;
        break;
      case ARM_IR_SETC:
        core->flag_c = op->b;
        continue;
      case ARM_IR_SETNZ64:
        core->flag_n = a;
        core->flag_z = a | v[op->b];
        continue;
      case ARM_IR_MULCYC:
        /* Early termination: one cycle per significant byte of the
           multiplier, leading zero bytes (or, signed, one bytes) free. */
        if(op->b == 0 && (a >> 31))
          a = ~a;
        core->cycles += (a >> 8) == 0 ? 1 : (a >> 16) == 0 ? 2 :
         (a >> 24) == 0 ? 3 : 4;
        continue;

      case ARM_IR_LD8:
        address = a + op->b;
        count_access(core, address, 1, flags);
        v[op->d] = read8(core, address);
        continue;
      case ARM_IR_LD8S:
        address = a + op->b;
        count_access(core, address, 1, flags);
        v[op->d] = (uint32_t)(int32_t)(int8_t)read8(core, address);
        continue;
      case ARM_IR_LD16:
        address = a + op->b;
        count_access(core, address & ~1u, 2, flags);
        v[op->d] = ror32(read16(core, address & ~1u), (address & 1) * 8);
        continue;
      case ARM_IR_LD16S:
        address = a + op->b;
        count_access(core, address & ~1u, 2, flags);
        result = read16(core, address & ~1u);
        if(address & 1)
          v[op->d] = (uint32_t)(int32_t)(int8_t)(result >> 8);
        else
          v[op->d] = (uint32_t)(int32_t)(int16_t)result;
        continue;
      case ARM_IR_LD32:
        address = a + op->b;
        count_access(core, address & ~3u, 4, flags);
        v[op->d] = ror32(read32(core, address & ~3u), (address & 3) * 8);
        continue;
      case ARM_IR_ST8:
        address = a + op->b;
        count_access(core, address, 1, flags);
        write8(core, address, v[op->d]);
        continue;
      case ARM_IR_ST16:
        address = (a + op->b) & ~1u;
        count_access(core, address, 2, flags);
        write16(core, address, v[op->d]);
        continue;
      case ARM_IR_ST32:
        address = (a + op->b) & ~3u;
        count_access(core, address, 4, flags);
        write32(core, address, v[op->d]);
        continue;

      case ARM_IR_MRS:
        /* Without an SPSR the CPSR reads instead. */
        v[op->d] = (op->a && has_spsr(core)) ? core->spsr :
         arm_core_cpsr(core);
        continue;
      case ARM_IR_MSR:
        write_psr(core, a, op->b);
        continue;
      case ARM_IR_USER_GET:
        v[op->d] = *user_register(core, op->b);
        continue;
      case ARM_IR_USER_SET:
        *user_register(core, op->b) = a;
        continue;

      case ARM_IR_EXIT:
        set_pc(core, op->b, flags & ARM_IR_BX);
        return (ArmExitKind)op->d;
      case ARM_IR_EXIT_REG:
        if((flags & ARM_IR_RESTORE) && has_spsr(core))
          arm_core_set_cpsr(core, core->spsr);
        set_pc(core, a, flags & ARM_IR_BX);
        core->cycles += (int32_t)refill(core);
        return (ArmExitKind)op->d;
      case ARM_IR_SWI:
        exception(core, ARM_MODE_SUPERVISOR, 0x08, op->b);
        return ARM_EXIT_SYSTEM;
      default:
        exception(core, ARM_MODE_UNDEFINED, 0x04, op->b);
        return ARM_EXIT_SYSTEM;
    }
    /* ALU ops end here. */
    if(flags & ARM_IR_SET_NZ)
      core->flag_n = core->flag_z = result;
    v[op->d] = result;
  }
}

/* ---- Running ----------------------------------------------------------- */

void arm_core_init(ArmCore *core, const ArmBus *bus)
{
  memset(core, 0, sizeof(*core));
  core->bus = *bus;
  set_waits(core);
  arm_core_flush(core);
  arm_core_reset(core);
}

void arm_core_reset(ArmCore *core)
{
  memset(core->v, 0, sizeof(core->v));
  memset(core->bank_r13_r14, 0, sizeof(core->bank_r13_r14));
  memset(core->bank_r8_r12, 0, sizeof(core->bank_r8_r12));
  memset(core->bank_spsr, 0, sizeof(core->bank_spsr));
  core->spsr = 0;
  core->cpsr = ARM_MODE_SUPERVISOR | ARM_CPSR_I | ARM_CPSR_F;
  core->flag_n = 0;
  core->flag_z = 1;
  core->flag_c = 0;
  core->flag_v = 0;
  core->exit_request = 0;
  core->irq_line = 0;
  core->waitcnt = 0;
  set_waits(core);
  arm_core_flush(core);
  core->exit_request = 0;
}

void arm_core_set_waitcnt(ArmCore *core, uint16_t waitcnt)
{
  if(waitcnt == core->waitcnt)
    return;
  core->waitcnt = waitcnt;
  set_waits(core);
  arm_core_flush(core);
}

static ArmBlock *current_block(ArmCore *core)
{
  uint32_t key = core->v[15] | ((core->cpsr & ARM_CPSR_T) ? 1u : 0u);
  ArmBlock *block = lookup(core, key);

  return block ? block : translate(core, key);
}

ArmExitKind arm_core_run_block(ArmCore *core, unsigned *insns)
{
  ArmBlock *block = current_block(core);

  if(insns)
    *insns = block->insns;
  return execute(core, &core->ops[block->first]);
}

int32_t arm_core_run(ArmCore *core, int32_t cycles)
{
  ArmExitKind kind = ARM_EXIT_JUMP;

  core->cycles = 0;
  core->cycle_limit = cycles;
  for(;;)
  {
    /* Interrupts and the budget only at taken branches, so both depend on
       the instruction stream and not on where blocks happen to start. */
    if(kind != ARM_EXIT_NEXT)
    {
      if(core->cycles >= core->cycle_limit)
        break;
      if(core->irq_line && !(core->cpsr & ARM_CPSR_I))
        core->cycles += arm_core_irq(core);
    }
    kind = arm_core_run_block(core, NULL);
    if(core->exit_request)
    {
      unsigned request = core->exit_request;

      core->exit_request = 0;
      if(request & ARM_EXIT_STOP)
        break;
    }
  }
  return core->cycles;
}

int arm_core_step(ArmCore *core)
{
  ArmIrOp ops[96];
  int32_t before = core->cycles;

  arm_translate(core, core->v[15], (core->cpsr & ARM_CPSR_T) != 0, ops, 96,
   1, NULL);
  execute(core, ops);
  core->exit_request = 0;
  return core->cycles - before;
}
