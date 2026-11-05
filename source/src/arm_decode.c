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
 * ARM and Thumb decoder: guest instructions to the IR of arm_ir.h.
 *
 * Both instruction sets go through a table that sorts an opcode into its
 * instruction class (bits 27-20 and 7-4 for ARM, bits 15-8 for Thumb),
 * then one function per class emits the ops. The specification is the
 * ARM Architecture Reference Manual for ARMv4T and GBATEK; where the
 * ARM7TDMI does something the manual calls unpredictable and games or
 * test ROMs rely on, the comment says what the chip does.
 */

#include "arm_core.h"

#include <string.h>

#define NO_OP 0xFFFFFFFFu
/* Room one instruction may need: an STM of 16 user registers takes about
   40 ops. */
#define INSN_MAX_OPS 64u

/* Flag liveness bits. N and Z always change together. */
#define LIVE_NZ 1u
#define LIVE_C  2u
#define LIVE_V  4u
#define LIVE_ALL 7u

typedef struct
{
  ArmCore *core;
  ArmIrOp *ops;
  unsigned count;
  unsigned temp;          /* next free temporary */
  uint32_t pc;            /* address of the instruction being decoded */
  int thumb;
  unsigned width;         /* 4 for ARM, 2 for Thumb */
  unsigned insn_op;       /* its ARM_IR_INSN */
  uint32_t skip_op;       /* its ARM_IR_SKIP, or NO_OP */
  unsigned cost;          /* its cycles known now, when it executes */
  int ended;              /* it writes r15 or changes mode */
  int stored;             /* it writes memory */
  int cartridge;          /* the block runs from the GamePak */
  int prefetch;           /* ... with the prefetch buffer enabled */
  unsigned idle;          /* cycles of the instruction the GamePak bus is
                             free, for the prefetch buffer */
  /* Constant propagation: slots whose value is known here. Guest
     registers are only known within the block, temporaries within the
     instruction. */
  uint8_t known[ARM_IR_SLOTS];
  uint32_t value[ARM_IR_SLOTS];
} Builder;

static uint32_t ror32(uint32_t value, unsigned amount)
{
  amount &= 31;
  return amount ? (value >> amount) | (value << (32 - amount)) : value;
}

static int is_alu(unsigned code)
{
  return code >= ARM_IR_MOV && code <= ARM_IR_SLTU;
}

static int is_load(unsigned code)
{
  return code >= ARM_IR_LD8 && code <= ARM_IR_LD32;
}

/* ---- Constant propagation --------------------------------------------- */

static void set_known(Builder *b, unsigned slot, uint32_t value)
{
  /* A guest register written under a condition may or may not hold the
     value afterwards. */
  if(slot < ARM_IR_FIRST_TEMP && b->skip_op != NO_OP)
  {
    b->known[slot] = 0;
    return;
  }
  b->known[slot] = 1;
  b->value[slot] = value;
}

/* Computes an ALU op whose operands are all known. */
static int fold(const Builder *b, const ArmIrOp *op, uint32_t *result)
{
  uint32_t x = b->value[op->a];
  uint32_t y = op->b;

  if(op->flags & ARM_IR_USE_C)
    return 0;
  if(!(op->flags & ARM_IR_IMM))
  {
    if(!b->known[op->b])
      return 0;
    y = b->value[op->b];
  }
  if(op->code != ARM_IR_MOV && op->code != ARM_IR_MVN && !b->known[op->a])
    return 0;
  /* Shifts by a register follow other rules; leave them alone. */
  if(op->code >= ARM_IR_LSL && op->code <= ARM_IR_ROR &&
     !(op->flags & ARM_IR_IMM))
    return 0;

  switch(op->code)
  {
    case ARM_IR_MOV: *result = y; return 1;
    case ARM_IR_MVN: *result = ~y; return 1;
    case ARM_IR_AND: *result = x & y; return 1;
    case ARM_IR_EOR: *result = x ^ y; return 1;
    case ARM_IR_ORR: *result = x | y; return 1;
    case ARM_IR_BIC: *result = x & ~y; return 1;
    case ARM_IR_ADD: *result = x + y; return 1;
    case ARM_IR_SUB: *result = x - y; return 1;
    case ARM_IR_RSB: *result = y - x; return 1;
    case ARM_IR_LSL: *result = x << y; return 1;
    case ARM_IR_LSR: *result = y == 32 ? 0 : x >> y; return 1;
    case ARM_IR_ASR:
      *result = (uint32_t)((int32_t)x >> (y == 32 ? 31 : y));
      return 1;
    case ARM_IR_ROR: *result = ror32(x, y); return 1;
    case ARM_IR_MUL: *result = x * y; return 1;
    default: return 0;
  }
}

static unsigned emit(Builder *b, unsigned code, unsigned flags, unsigned d,
 unsigned a, uint32_t x)
{
  ArmIrOp *op = &b->ops[b->count];
  uint32_t result;

  op->code = (uint8_t)code;
  op->flags = (uint8_t)flags;
  op->d = (uint8_t)d;
  op->a = (uint8_t)a;
  op->b = x;
  if(d != ARM_IR_NONE && (is_alu(code) || is_load(code) ||
     code == ARM_IR_MRS || code == ARM_IR_USER_GET))
  {
    if(is_alu(code) && fold(b, op, &result))
    {
      /* Without flag effects the op becomes a constant move. */
      if(!(flags & (ARM_IR_SET_NZ | ARM_IR_SET_C | ARM_IR_SET_V)))
      {
        op->code = ARM_IR_MOV;
        op->flags = ARM_IR_IMM;
        op->a = 0;
        op->b = result;
      }
      set_known(b, d, result);
    }
    else
      b->known[d] = 0;
  }
  /* In user or system mode this is the register itself. */
  if(code == ARM_IR_USER_SET)
    b->known[x & 15] = 0;
  return b->count++;
}

static unsigned temp(Builder *b)
{
  return b->temp++;
}

static unsigned constant(Builder *b, uint32_t value)
{
  unsigned slot = temp(b);

  emit(b, ARM_IR_MOV, ARM_IR_IMM, slot, 0, value);
  return slot;
}

/* Slot to read guest register r from; r15 reads as the instruction's
   address plus two instructions, as the pipeline makes it. */
static unsigned reg(Builder *b, unsigned r)
{
  if(r != 15)
    return r;
  return constant(b, b->pc + 2 * b->width);
}

/* ---- Timing ------------------------------------------------------------ */

static int cartridge_address(uint32_t address)
{
  return !(address >> 28) && address >= 0x08000000u;
}

/* What happens on the bus around an instruction's opcode fetch. */
enum
{
  FETCH_SEQUENTIAL,
  FETCH_AFTER_STORE,    /* the bus moved to the data written */
  FETCH_WITH_INTERNAL   /* internal cycles and r15 left alone */
};

/* The fetch made while an instruction executes is of the one two places
   further on. It is sequential except after a store, at a 128 KB ROM
   boundary, and with the prefetch buffer disabled for GamePak opcodes
   with internal cycles (GBATEK's prefetch disable bug). With the buffer
   enabled, a GamePak fetch is counted at run time from the buffer. */
static void fetch(Builder *b, unsigned kind)
{
  uint32_t address = b->pc + 2 * b->width;
  int sequential = kind != FETCH_AFTER_STORE;

  if(b->prefetch && (address & 0x1FFFF))
  {
    b->ops[b->insn_op].flags |= b->thumb ? ARM_IR_FETCH16 : ARM_IR_FETCH32;
    return;
  }
  if(b->cartridge && !b->prefetch && kind == FETCH_WITH_INTERNAL)
    sequential = 0;
  b->cost += arm_access_cycles(b->core, address, b->width, sequential);
}

static unsigned fetch_s(const Builder *b)
{
  return arm_access_cycles(b->core, b->pc + 2 * b->width, b->width, 1);
}

/* Internal cycles, during which the GamePak bus is free. */
static void internal(Builder *b, unsigned cycles)
{
  b->cost += cycles;
  b->idle += cycles;
}

/* Refilling the pipeline at a branch target: N then S fetches there. */
static unsigned refill(const Builder *b, uint32_t target, int thumb)
{
  unsigned width = thumb ? 2 : 4;

  return arm_access_cycles(b->core, target, width, 0) +
   arm_access_cycles(b->core, target + width, width, 1);
}

/* ---- Instructions ------------------------------------------------------ */

static void begin(Builder *b, unsigned cond)
{
  memset(b->known + ARM_IR_FIRST_TEMP, 0,
   ARM_IR_SLOTS - ARM_IR_FIRST_TEMP);
  b->temp = ARM_IR_FIRST_TEMP;
  b->insn_op = emit(b, ARM_IR_INSN, b->stored ? ARM_IR_CHECK : 0, 0, 0,
   b->pc);
  b->stored = 0;
  b->cost = 0;
  b->idle = 0;
  b->ended = 0;
  b->skip_op = NO_OP;
  if(cond != ARM_COND_AL)
    b->skip_op = emit(b, ARM_IR_SKIP, 0, 0, cond, 0);
}

static void finish(Builder *b)
{
  ArmIrOp *insn;
  unsigned cost = b->cost;

  /* The cycles the prefetch buffer had to fill up, except before a branch,
     which empties it anyway. */
  if(b->prefetch && b->idle && !b->ended)
    emit(b, ARM_IR_IDLE, 0, ARM_IR_NONE, 0, b->idle);
  insn = &b->ops[b->insn_op];
  if(b->skip_op != NO_OP)
  {
    /* A failed condition costs the fetch alone. */
    unsigned failed = (insn->flags & (ARM_IR_FETCH16 | ARM_IR_FETCH32)) ?
     0 : fetch_s(b);
    ArmIrOp *skip = &b->ops[b->skip_op];

    skip->b = (b->count - b->skip_op - 1) |
     ((cost > failed ? cost - failed : 0) << 16);
    cost = failed;
    b->skip_op = NO_OP;
    if(b->ended)
      emit(b, ARM_IR_EXIT, 0, ARM_EXIT_NEXT, 0, b->pc + b->width);
  }
  insn->a = (uint8_t)cost;
  insn->d = (uint8_t)(cost >> 8);
}

/* Ends the block with a jump to the address in slot. A known target
   becomes a direct exit whose refill is counted now. */
static void jump(Builder *b, unsigned slot, unsigned flags, unsigned kind)
{
  b->ended = 1;
  if(b->known[slot] && !(flags & ARM_IR_RESTORE))
  {
    uint32_t target = b->value[slot];
    int thumb = (flags & ARM_IR_BX) ? (int)(target & 1) : b->thumb;

    target &= thumb ? ~1u : ~3u;
    b->cost += refill(b, target, thumb);
    emit(b, ARM_IR_EXIT, flags & ARM_IR_BX, kind, 0,
     target | (uint32_t)thumb);
    return;
  }
  emit(b, ARM_IR_EXIT_REG, flags, kind, slot, 0);
}

static int idle_loop(const ArmCore *core, uint32_t pc)
{
  unsigned i;

  for(i = 0; i < core->idle_loop_count; i++)
    if(core->idle_loops[i] == pc)
      return 1;
  return 0;
}

/* Branch to a target known now. */
static void branch(Builder *b, uint32_t target, unsigned kind)
{
  b->ended = 1;
  b->cost += refill(b, target, b->thumb);
  emit(b, ARM_IR_EXIT, idle_loop(b->core, b->pc) ? ARM_IR_IDLE_LOOP : 0,
   kind, 0, target);
}

static void exception(Builder *b, unsigned code, uint32_t vector)
{
  b->ended = 1;
  fetch(b, FETCH_SEQUENTIAL);
  b->cost += refill(b, vector, 0);
  emit(b, code, 0, ARM_EXIT_SYSTEM, 0, b->pc + b->width);
}

/* Whether a load from this address may be done at translate time: ROM
   (but not its first 256 bytes, which may hold the GPIO port, nor the
   EEPROM region) and the BIOS read by BIOS code. */
static int constant_memory(const Builder *b, uint32_t address)
{
  unsigned region = address >> 24;

  if(address >> 28)
    return 0;
  if(region == 0)
    return address < 0x4000 && b->pc < 0x4000;
  return region >= 8 && region <= 12 && (address & 0x01FFFFFFu) >= 0x100;
}

static uint32_t load_value(unsigned code, uint32_t address, uint32_t word)
{
  uint32_t half = (address & 2) ? word >> 16 : word & 0xFFFF;
  uint32_t byte = (word >> ((address & 3) * 8)) & 0xFF;

  switch(code)
  {
    case ARM_IR_LD8: return byte;
    case ARM_IR_LD8S: return (uint32_t)(int32_t)(int8_t)byte;
    case ARM_IR_LD16: return (address & 1) ? ror32(half, 8) : half;
    case ARM_IR_LD16S:
      if(address & 1)
        return (uint32_t)(int32_t)(int8_t)byte;
      return (uint32_t)(int32_t)(int16_t)half;
    default: return ror32(word, (address & 3) * 8);
  }
}

static unsigned access_size(unsigned code)
{
  switch(code)
  {
    case ARM_IR_LD8: case ARM_IR_LD8S: case ARM_IR_ST8: return 1;
    case ARM_IR_LD16: case ARM_IR_LD16S: case ARM_IR_ST16: return 2;
    default: return 4;
  }
}

/* Emits a load or store of slot d at a + offset. When the address is
   known its cycles are counted now, and a load from ROM becomes the
   value itself. */
static void memory(Builder *b, unsigned code, unsigned flags, unsigned d,
 unsigned a, uint32_t offset)
{
  if(b->cartridge)
    flags |= ARM_IR_CARTRIDGE_CODE;
  if(b->known[a])
  {
    uint32_t address = b->value[a] + offset;
    unsigned size = access_size(code);
    unsigned cycles = arm_access_cycles(b->core, address & ~(size - 1), size,
     flags & ARM_IR_SEQ);

    b->cost += cycles;
    flags |= ARM_IR_FOLDED;
    if(b->cartridge && !cartridge_address(address))
      b->idle += cycles;
    else if(b->cartridge && is_load(code) && !(flags & ARM_IR_SEQ))
      b->cost += arm_cartridge_refetch(b->core, b->pc);
    if(is_load(code) && constant_memory(b, address))
    {
      uint32_t word = b->core->bus.code32(b->core->bus.context,
       address & ~3u);

      emit(b, ARM_IR_MOV, ARM_IR_IMM, d, 0, load_value(code, address, word));
      /* The bus still read the literal, interrupting the prefetch. */
      if(b->prefetch && cartridge_address(address))
        emit(b, ARM_IR_FLUSH, 0, ARM_IR_NONE, 0, 0);
      return;
    }
  }
  if(!is_load(code))
    b->stored = 1;
  emit(b, code, flags, d, a, offset);
}

/* ---- ARM: data processing ---------------------------------------------- */

static const uint8_t alu_code[16] =
{
  ARM_IR_AND, ARM_IR_EOR, ARM_IR_SUB, ARM_IR_RSB,
  ARM_IR_ADD, ARM_IR_ADC, ARM_IR_SBC, ARM_IR_RSC,
  ARM_IR_AND, ARM_IR_EOR, ARM_IR_SUB, ARM_IR_ADD,
  ARM_IR_ORR, ARM_IR_MOV, ARM_IR_BIC, ARM_IR_MVN
};

/* AND, EOR, TST, TEQ, ORR, MOV, BIC and MVN take C from the shifter. */
static int alu_logical(unsigned opcode)
{
  return (0xF303u >> opcode) & 1;
}

static void arm_data(Builder *b, uint32_t opcode)
{
  unsigned alu = (opcode >> 21) & 15;
  int s = (opcode >> 20) & 1;
  unsigned rn = (opcode >> 16) & 15;
  unsigned rd = (opcode >> 12) & 15;
  int test = alu >= 8 && alu <= 11;
  unsigned shifter_c = (s && alu_logical(alu)) ? ARM_IR_SET_C : 0;
  unsigned operand_flags = 0;
  uint32_t operand;
  unsigned flags = 0;
  unsigned first;
  unsigned dest;
  uint32_t pc_value = b->pc + 8;
  int shift_by_register = !(opcode & (1u << 25)) && (opcode & 0x10);

  fetch(b, shift_by_register && rd != 15 ? FETCH_WITH_INTERNAL :
   FETCH_SEQUENTIAL);
  if(opcode & (1u << 25))
  {
    unsigned rotate = (opcode >> 7) & 0x1E;

    operand = ror32(opcode & 0xFF, rotate);
    operand_flags = ARM_IR_IMM;
    if(shifter_c && rotate != 0)
      emit(b, ARM_IR_SETC, 0, ARM_IR_NONE, 0, operand >> 31);
  }
  else
  {
    unsigned rm = opcode & 15;
    unsigned type = (opcode >> 5) & 3;

    if(opcode & 0x10)
    {
      /* Shift by register: one internal cycle, and r15 reads 4 further
         on because the shift amount is read first. */
      unsigned rs = (opcode >> 8) & 15;
      unsigned m;
      unsigned amount;

      pc_value = b->pc + 12;
      m = rm == 15 ? constant(b, pc_value) : rm;
      amount = rs == 15 ? constant(b, pc_value) : rs;
      operand = temp(b);
      emit(b, ARM_IR_LSL + type, shifter_c, operand, m, amount);
      internal(b, 1);
    }
    else
    {
      unsigned amount = (opcode >> 7) & 31;
      unsigned m = reg(b, rm);

      if(type == 0 && amount == 0)
        operand = m;
      else if(type == 3 && amount == 0)
      {
        operand = temp(b);
        emit(b, ARM_IR_RRX, shifter_c | ARM_IR_USE_C, operand, m, 0);
      }
      else
      {
        /* LSR #0 and ASR #0 encode shifts by 32. */
        operand = temp(b);
        emit(b, ARM_IR_LSL + type, shifter_c | ARM_IR_IMM, operand, m,
         amount ? amount : 32);
      }
    }
  }

  first = rn == 15 ? constant(b, pc_value) : rn;
  if(s)
    flags = alu_logical(alu) ? ARM_IR_SET_NZ :
     ARM_IR_SET_NZ | ARM_IR_SET_C | ARM_IR_SET_V;
  if(alu == 5 || alu == 6 || alu == 7)
    flags |= ARM_IR_USE_C;
  if(test)
    dest = ARM_IR_NONE;
  else if(rd == 15)
    dest = temp(b);
  else
    dest = rd;
  emit(b, alu_code[alu], flags | operand_flags, dest, first, operand);

  /* With S, writing r15 also restores the CPSR from the SPSR (in a mode
     that has one; otherwise the flags stay as the ALU set them). The
     ARMv2 TSTP, TEQP, CMPP and CMNP forms, a test with rd 15, restore it
     too and go on with the next instruction. */
  if(rd == 15 && test)
    jump(b, constant(b, b->pc + 4), ARM_IR_RESTORE, ARM_EXIT_SYSTEM);
  else if(rd == 15)
  {
    unsigned kind = ARM_EXIT_JUMP;

    if(alu == 13 && !(opcode & (1u << 25)) && (opcode & 0xFFF) == 14)
      kind = ARM_EXIT_RETURN;
    if(s)
      jump(b, dest, ARM_IR_RESTORE, ARM_EXIT_SYSTEM);
    else
      jump(b, dest, 0, kind);
  }
}

/* ---- ARM: PSR transfers ------------------------------------------------- */

static void arm_mrs(Builder *b, uint32_t opcode)
{
  fetch(b, FETCH_SEQUENTIAL);
  emit(b, ARM_IR_MRS, 0, (opcode >> 12) & 15, (opcode >> 22) & 1, 0);
}

static void arm_msr(Builder *b, uint32_t opcode)
{
  unsigned spsr = (opcode >> 22) & 1;
  unsigned fields = (opcode >> 16) & 15;
  unsigned source;

  fetch(b, FETCH_SEQUENTIAL);
  if(opcode & (1u << 25))
    source = constant(b, ror32(opcode & 0xFF, (opcode >> 7) & 0x1E));
  else
    source = reg(b, opcode & 15);
  emit(b, ARM_IR_MSR, 0, ARM_IR_NONE, source, fields | (spsr << 4));
  /* A new mode or interrupt mask takes effect at a block boundary. */
  if(!spsr && (fields & 1))
  {
    b->ended = 1;
    emit(b, ARM_IR_EXIT, 0, ARM_EXIT_SYSTEM, 0, b->pc + 4);
  }
}

/* ---- ARM: multiplies ---------------------------------------------------- */

static void arm_multiply(Builder *b, uint32_t opcode)
{
  int accumulate = (opcode >> 21) & 1;
  unsigned flags = (opcode & (1u << 20)) ? ARM_IR_SET_NZ : 0;
  unsigned rd = (opcode >> 16) & 15;
  unsigned rn = (opcode >> 12) & 15;
  unsigned rs = reg(b, (opcode >> 8) & 15);
  unsigned rm = reg(b, opcode & 15);

  fetch(b, FETCH_WITH_INTERNAL);
  internal(b, (unsigned)accumulate);
  emit(b, ARM_IR_MULCYC, b->cartridge ? ARM_IR_CARTRIDGE_CODE : 0,
   ARM_IR_NONE, rs, 0);
  if(accumulate)
  {
    unsigned product = temp(b);

    emit(b, ARM_IR_MUL, 0, product, rm, rs);
    emit(b, ARM_IR_ADD, flags, rd, product, reg(b, rn));
  }
  else
    emit(b, ARM_IR_MUL, flags, rd, rm, rs);
}

static void arm_multiply_long(Builder *b, uint32_t opcode)
{
  int is_signed = (opcode >> 22) & 1;
  int accumulate = (opcode >> 21) & 1;
  int s = (opcode >> 20) & 1;
  unsigned hi = (opcode >> 16) & 15;
  unsigned lo = (opcode >> 12) & 15;
  unsigned rs = reg(b, (opcode >> 8) & 15);
  unsigned rm = reg(b, opcode & 15);
  unsigned low = temp(b);
  unsigned high = temp(b);

  fetch(b, FETCH_WITH_INTERNAL);
  internal(b, 1 + (unsigned)accumulate);
  emit(b, ARM_IR_MULCYC, b->cartridge ? ARM_IR_CARTRIDGE_CODE : 0,
   ARM_IR_NONE, rs, is_signed ? 0 : 1);
  emit(b, ARM_IR_MUL, 0, low, rm, rs);
  emit(b, is_signed ? ARM_IR_SMULH : ARM_IR_UMULH, 0, high, rm, rs);
  if(accumulate)
  {
    unsigned sum_low = temp(b);
    unsigned carry = temp(b);
    unsigned sum_high = temp(b);
    unsigned total_high = temp(b);

    emit(b, ARM_IR_ADD, 0, sum_low, low, reg(b, lo));
    emit(b, ARM_IR_SLTU, 0, carry, sum_low, low);
    emit(b, ARM_IR_ADD, 0, sum_high, high, reg(b, hi));
    emit(b, ARM_IR_ADD, 0, total_high, sum_high, carry);
    low = sum_low;
    high = total_high;
  }
  emit(b, ARM_IR_MOV, 0, lo, 0, low);
  emit(b, ARM_IR_MOV, 0, hi, 0, high);
  if(s)
    emit(b, ARM_IR_SETNZ64, 0, ARM_IR_NONE, high, low);
}

/* ---- Loads and stores --------------------------------------------------- */

/* One load or store with ARM addressing: base slot, offset (an immediate,
   or a slot with offset_slot set), pre or post indexing, writeback. Shared
   by ARM single and halfword transfers and every Thumb form. */
static void transfer(Builder *b, unsigned code, unsigned rd, unsigned rn,
 uint32_t offset, int offset_slot, int pre, int up, int writeback)
{
  int load = is_load(code);
  unsigned base = reg(b, rn);
  unsigned address = base;
  uint32_t displacement = 0;
  unsigned updated = ARM_IR_NONE;
  unsigned dest;

  if(!pre)
    writeback = 1;
  /* The written back base, worked out before the load can overwrite the
     offset register. */
  if(writeback || (offset_slot && pre))
  {
    updated = temp(b);
    emit(b, up ? ARM_IR_ADD : ARM_IR_SUB, offset_slot ? 0 : ARM_IR_IMM,
     updated, base, offset);
    if(pre)
      address = updated;
  }
  else if(pre)
    displacement = up ? offset : (uint32_t)-offset;

  if(load)
  {
    fetch(b, rd == 15 ? FETCH_SEQUENTIAL : FETCH_WITH_INTERNAL);
    internal(b, 1);
    dest = rd == 15 ? temp(b) : rd;
    memory(b, code, 0, dest, address, displacement);
    /* The loaded value wins over the written back base. */
    if(writeback && rd != rn)
      emit(b, ARM_IR_MOV, 0, rn, 0, updated);
    if(rd == 15)
      jump(b, dest, 0, rn == 13 ? ARM_EXIT_RETURN : ARM_EXIT_JUMP);
  }
  else
  {
    unsigned value = rd == 15 ? constant(b, b->pc + 3 * b->width) : rd;

    fetch(b, FETCH_AFTER_STORE);
    memory(b, code, 0, value, address, displacement);
    if(writeback)
      emit(b, ARM_IR_MOV, 0, rn, 0, updated);
  }
}

static void arm_single(Builder *b, uint32_t opcode)
{
  int load = (opcode >> 20) & 1;
  unsigned code;
  uint32_t offset;
  int offset_slot = 0;

  if(load)
    code = (opcode & (1u << 22)) ? ARM_IR_LD8 : ARM_IR_LD32;
  else
    code = (opcode & (1u << 22)) ? ARM_IR_ST8 : ARM_IR_ST32;
  if(opcode & (1u << 25))
  {
    unsigned rm = reg(b, opcode & 15);
    unsigned type = (opcode >> 5) & 3;
    unsigned amount = (opcode >> 7) & 31;

    offset_slot = 1;
    if(type == 0 && amount == 0)
      offset = rm;
    else if(type == 3 && amount == 0)
    {
      offset = temp(b);
      emit(b, ARM_IR_RRX, ARM_IR_USE_C, offset, rm, 0);
    }
    else
    {
      offset = temp(b);
      emit(b, ARM_IR_LSL + type, ARM_IR_IMM, offset, rm,
       amount ? amount : 32);
    }
  }
  else
    offset = opcode & 0xFFF;
  transfer(b, code, (opcode >> 12) & 15, (opcode >> 16) & 15, offset,
   offset_slot, (opcode >> 24) & 1, (opcode >> 23) & 1,
   (opcode >> 21) & 1);
}

static void undefined(Builder *b)
{
  exception(b, ARM_IR_UNDEF, 0x04);
}

static void arm_halfword(Builder *b, uint32_t opcode)
{
  int load = (opcode >> 20) & 1;
  unsigned kind = (opcode >> 5) & 3;
  static const uint8_t loads[4] =
   { ARM_IR_NOP, ARM_IR_LD16, ARM_IR_LD8S, ARM_IR_LD16S };
  uint32_t offset;
  int offset_slot = 0;

  /* Stores of signed values are ARMv5 doubleword transfers. */
  if(!load && kind != 1)
  {
    undefined(b);
    return;
  }
  if(opcode & (1u << 22))
    offset = ((opcode >> 4) & 0xF0) | (opcode & 15);
  else
  {
    offset = reg(b, opcode & 15);
    offset_slot = 1;
  }
  transfer(b, load ? loads[kind] : ARM_IR_ST16, (opcode >> 12) & 15,
   (opcode >> 16) & 15, offset, offset_slot, (opcode >> 24) & 1,
   (opcode >> 23) & 1, (opcode >> 21) & 1);
}

static void arm_swap(Builder *b, uint32_t opcode)
{
  int byte = (opcode >> 22) & 1;
  unsigned rn = reg(b, (opcode >> 16) & 15);
  unsigned rd = (opcode >> 12) & 15;
  unsigned rm = (opcode & 15) == 15 ? constant(b, b->pc + 12) : opcode & 15;
  unsigned loaded = temp(b);

  fetch(b, rd == 15 ? FETCH_SEQUENTIAL : FETCH_WITH_INTERNAL);
  internal(b, 1);
  memory(b, byte ? ARM_IR_LD8 : ARM_IR_LD32, 0, loaded, rn, 0);
  memory(b, byte ? ARM_IR_ST8 : ARM_IR_ST32, 0, rm, rn, 0);
  if(rd == 15)
    jump(b, loaded, 0, ARM_EXIT_JUMP);
  else
    emit(b, ARM_IR_MOV, 0, rd, 0, loaded);
}

static unsigned popcount16(unsigned list)
{
  unsigned n = 0;

  for(; list; list &= list - 1)
    n++;
  return n;
}

/* LDM and STM, and the Thumb forms (PUSH, POP, LDMIA, STMIA). The
   ARM7TDMI transfers r15 alone and moves the base by 64 bytes for an
   empty list; with the base in the list, an LDM's loaded value wins and
   an STM stores the old base only when it is the first register. */
static void block_transfer(Builder *b, unsigned rn, unsigned list, int load,
 int pre, int up, int writeback, int s_bit)
{
  unsigned count = popcount16(list);
  uint32_t size = count * 4;
  int user;
  unsigned base = reg(b, rn);
  unsigned aligned = temp(b);
  unsigned updated = ARM_IR_NONE;
  uint32_t offset;
  unsigned first_reg = 16;
  unsigned flags = 0;
  unsigned pc_slot = ARM_IR_NONE;
  unsigned r;

  if(list == 0)
  {
    list = 1u << 15;
    size = 0x40;
  }
  for(r = 0; r < 16; r++)
    if(list & (1u << r))
    {
      first_reg = r;
      break;
    }
  /* With S, LDM with r15 returns from an exception; any other LDM or STM
     transfers the user mode registers. */
  user = s_bit && !(load && (list & 0x8000u));

  if(up)
    offset = pre ? 4 : 0;
  else
    offset = pre ? (uint32_t)-size : (uint32_t)(4 - size);
  emit(b, ARM_IR_AND, ARM_IR_IMM, aligned, base, ~3u);
  if(writeback)
  {
    updated = temp(b);
    emit(b, up ? ARM_IR_ADD : ARM_IR_SUB, ARM_IR_IMM, updated, base, size);
  }

  if(load)
  {
    fetch(b, (list & 0x8000u) ? FETCH_SEQUENTIAL : FETCH_WITH_INTERNAL);
    internal(b, 1);
  }
  else
    fetch(b, FETCH_AFTER_STORE);
  for(r = 0; r < 16; r++)
  {
    unsigned slot;

    if(!(list & (1u << r)))
      continue;
    if(load)
    {
      if(r == 15)
        slot = pc_slot = temp(b);
      else if(user && r >= 8)
        slot = temp(b);
      else
        slot = r;
      memory(b, ARM_IR_LD32, flags, slot, aligned, offset);
      if(user && r >= 8 && r != 15)
        emit(b, ARM_IR_USER_SET, 0, ARM_IR_NONE, slot, r);
    }
    else
    {
      if(r == 15)
        slot = constant(b, b->pc + 3 * b->width);
      else if(r == rn && writeback && r != first_reg)
        slot = updated;
      else if(user && r >= 8)
      {
        slot = temp(b);
        emit(b, ARM_IR_USER_GET, 0, slot, 0, r);
      }
      else
        slot = r;
      memory(b, ARM_IR_ST32, flags, slot, aligned, offset);
    }
    /* The first access is non-sequential, the rest follow on. */
    flags = ARM_IR_SEQ;
    offset += 4;
  }
  if(writeback && !(load && (list & (1u << rn))))
    emit(b, ARM_IR_MOV, 0, rn, 0, updated);
  if(pc_slot != ARM_IR_NONE)
  {
    if(s_bit)
      jump(b, pc_slot, ARM_IR_RESTORE, ARM_EXIT_SYSTEM);
    else
      jump(b, pc_slot, 0, rn == 13 ? ARM_EXIT_RETURN : ARM_EXIT_JUMP);
  }
}

static void arm_block(Builder *b, uint32_t opcode)
{
  block_transfer(b, (opcode >> 16) & 15, opcode & 0xFFFF,
   (opcode >> 20) & 1, (opcode >> 24) & 1, (opcode >> 23) & 1,
   (opcode >> 21) & 1, (opcode >> 22) & 1);
}

/* ---- ARM: branches ------------------------------------------------------ */

static void arm_branch(Builder *b, uint32_t opcode)
{
  uint32_t target = b->pc + 8 + ((uint32_t)((int32_t)(opcode << 8) >> 6));

  fetch(b, FETCH_SEQUENTIAL);
  if(opcode & (1u << 24))
  {
    emit(b, ARM_IR_MOV, ARM_IR_IMM, 14, 0, b->pc + 4);
    branch(b, target, ARM_EXIT_CALL);
  }
  else
    branch(b, target, ARM_EXIT_JUMP);
}

/* BX Rm. Returns through lr; a jump made right after lr was pointed at
   the next instruction (mov lr, pc; bx rX) is a call. */
static void exchange(Builder *b, unsigned rm)
{
  unsigned kind = ARM_EXIT_JUMP;
  uint32_t next = b->pc + b->width;

  fetch(b, FETCH_SEQUENTIAL);
  if(rm == 14)
    kind = ARM_EXIT_RETURN;
  else if(b->known[14] && (b->value[14] & ~1u) == next)
    kind = ARM_EXIT_CALL;
  jump(b, reg(b, rm), ARM_IR_BX, kind);
}

/* ---- ARM decode table --------------------------------------------------- */

enum
{
  ARM_CLASS_DATA,
  ARM_CLASS_MRS,
  ARM_CLASS_MSR,
  ARM_CLASS_MUL,
  ARM_CLASS_MULL,
  ARM_CLASS_SWP,
  ARM_CLASS_BX,
  ARM_CLASS_HALF,
  ARM_CLASS_SINGLE,
  ARM_CLASS_BLOCK,
  ARM_CLASS_BRANCH,
  ARM_CLASS_SWI,
  ARM_CLASS_UNDEF
};

/* Indexed by opcode bits 27-20 and 7-4. */
static uint8_t arm_table[4096];

static unsigned arm_classify(unsigned high, unsigned low)
{
  switch(high >> 5)
  {
    case 0:
      if(low == 9)
      {
        if((high & 0xFC) == 0x00)
          return ARM_CLASS_MUL;
        if((high & 0xF8) == 0x08)
          return ARM_CLASS_MULL;
        if((high & 0xFB) == 0x10)
          return ARM_CLASS_SWP;
        return ARM_CLASS_UNDEF;
      }
      if((low & 9) == 9)
        return ARM_CLASS_HALF;
      /* TST, TEQ, CMP and CMN without S encode the miscellaneous group. */
      if((high & 0x19) == 0x10)
      {
        if(high == 0x12 && low == 1)
          return ARM_CLASS_BX;
        if(low == 0)
          return (high & 2) ? ARM_CLASS_MSR : ARM_CLASS_MRS;
        return ARM_CLASS_UNDEF;
      }
      return ARM_CLASS_DATA;
    case 1:
      if((high & 0x1B) == 0x12)
        return ARM_CLASS_MSR;
      if((high & 0x1B) == 0x10)
        return ARM_CLASS_UNDEF;
      return ARM_CLASS_DATA;
    case 2:
      return ARM_CLASS_SINGLE;
    case 3:
      return (low & 1) ? ARM_CLASS_UNDEF : ARM_CLASS_SINGLE;
    case 4:
      return ARM_CLASS_BLOCK;
    case 5:
      return ARM_CLASS_BRANCH;
    case 6:
      return ARM_CLASS_UNDEF;
    default:
      return (high & 0x10) ? ARM_CLASS_SWI : ARM_CLASS_UNDEF;
  }
}

static void arm_instruction(Builder *b, uint32_t opcode)
{
  unsigned cond = opcode >> 28;

  begin(b, cond);
  /* ARMv4 reserves the NV condition; the ARM7TDMI never executes it. */
  if(cond == ARM_COND_NV)
  {
    b->ops[b->skip_op].a = ARM_COND_NV;
    finish(b);
    return;
  }
  switch(arm_table[((opcode >> 16) & 0xFF0) | ((opcode >> 4) & 15)])
  {
    case ARM_CLASS_DATA: arm_data(b, opcode); break;
    case ARM_CLASS_MRS: arm_mrs(b, opcode); break;
    case ARM_CLASS_MSR: arm_msr(b, opcode); break;
    case ARM_CLASS_MUL: arm_multiply(b, opcode); break;
    case ARM_CLASS_MULL: arm_multiply_long(b, opcode); break;
    case ARM_CLASS_SWP: arm_swap(b, opcode); break;
    case ARM_CLASS_BX: exchange(b, opcode & 15); break;
    case ARM_CLASS_HALF: arm_halfword(b, opcode); break;
    case ARM_CLASS_SINGLE: arm_single(b, opcode); break;
    case ARM_CLASS_BLOCK: arm_block(b, opcode); break;
    case ARM_CLASS_BRANCH: arm_branch(b, opcode); break;
    case ARM_CLASS_SWI: exception(b, ARM_IR_SWI, 0x08); break;
    default: undefined(b); break;
  }
  finish(b);
}

/* ---- Thumb ------------------------------------------------------------- */

#define NZCV (ARM_IR_SET_NZ | ARM_IR_SET_C | ARM_IR_SET_V)

static void thumb_shift(Builder *b, unsigned opcode)
{
  unsigned type = (opcode >> 11) & 3;
  unsigned amount = (opcode >> 6) & 31;
  unsigned rs = (opcode >> 3) & 7;
  unsigned rd = opcode & 7;

  fetch(b, FETCH_SEQUENTIAL);
  if(type == 0 && amount == 0)
    emit(b, ARM_IR_MOV, ARM_IR_SET_NZ, rd, 0, rs);
  else
    emit(b, ARM_IR_LSL + type, ARM_IR_IMM | ARM_IR_SET_NZ | ARM_IR_SET_C,
     rd, rs, amount ? amount : 32);
}

static void thumb_add_sub(Builder *b, unsigned opcode)
{
  unsigned code = (opcode & 0x200) ? ARM_IR_SUB : ARM_IR_ADD;
  unsigned operand = (opcode >> 6) & 7;

  fetch(b, FETCH_SEQUENTIAL);
  emit(b, code, NZCV | ((opcode & 0x400) ? ARM_IR_IMM : 0), opcode & 7,
   (opcode >> 3) & 7, operand);
}

static void thumb_immediate(Builder *b, unsigned opcode)
{
  unsigned rd = (opcode >> 8) & 7;
  uint32_t value = opcode & 0xFF;

  fetch(b, FETCH_SEQUENTIAL);
  switch((opcode >> 11) & 3)
  {
    case 0:
      emit(b, ARM_IR_MOV, ARM_IR_IMM | ARM_IR_SET_NZ, rd, 0, value);
      break;
    case 1:
      emit(b, ARM_IR_SUB, ARM_IR_IMM | NZCV, ARM_IR_NONE, rd, value);
      break;
    case 2:
      emit(b, ARM_IR_ADD, ARM_IR_IMM | NZCV, rd, rd, value);
      break;
    default:
      emit(b, ARM_IR_SUB, ARM_IR_IMM | NZCV, rd, rd, value);
      break;
  }
}

static void thumb_alu(Builder *b, unsigned opcode)
{
  unsigned rs = (opcode >> 3) & 7;
  unsigned rd = opcode & 7;
  unsigned alu = (opcode >> 6) & 15;
  const unsigned nz = ARM_IR_SET_NZ;

  /* Shifts by register and MUL have internal cycles. */
  fetch(b, ((0x209Cu >> alu) & 1) ? FETCH_WITH_INTERNAL : FETCH_SEQUENTIAL);
  switch(alu)
  {
    case 0: emit(b, ARM_IR_AND, nz, rd, rd, rs); break;
    case 1: emit(b, ARM_IR_EOR, nz, rd, rd, rs); break;
    case 2:
      emit(b, ARM_IR_LSL, nz | ARM_IR_SET_C, rd, rd, rs);
      internal(b, 1);
      break;
    case 3:
      emit(b, ARM_IR_LSR, nz | ARM_IR_SET_C, rd, rd, rs);
      internal(b, 1);
      break;
    case 4:
      emit(b, ARM_IR_ASR, nz | ARM_IR_SET_C, rd, rd, rs);
      internal(b, 1);
      break;
    case 5: emit(b, ARM_IR_ADC, NZCV | ARM_IR_USE_C, rd, rd, rs); break;
    case 6: emit(b, ARM_IR_SBC, NZCV | ARM_IR_USE_C, rd, rd, rs); break;
    case 7:
      emit(b, ARM_IR_ROR, nz | ARM_IR_SET_C, rd, rd, rs);
      internal(b, 1);
      break;
    case 8: emit(b, ARM_IR_AND, nz, ARM_IR_NONE, rd, rs); break;
    case 9: emit(b, ARM_IR_RSB, NZCV | ARM_IR_IMM, rd, rs, 0); break;
    case 10: emit(b, ARM_IR_SUB, NZCV, ARM_IR_NONE, rd, rs); break;
    case 11: emit(b, ARM_IR_ADD, NZCV, ARM_IR_NONE, rd, rs); break;
    case 12: emit(b, ARM_IR_ORR, nz, rd, rd, rs); break;
    case 13:
      /* MUL rd, rs is MULS rd, rs, rd: rd is the multiplier. */
      emit(b, ARM_IR_MULCYC, b->cartridge ? ARM_IR_CARTRIDGE_CODE : 0,
       ARM_IR_NONE, rd, 0);
      emit(b, ARM_IR_MUL, nz, rd, rs, rd);
      break;
    case 14: emit(b, ARM_IR_BIC, nz, rd, rd, rs); break;
    default: emit(b, ARM_IR_MVN, nz, rd, 0, rs); break;
  }
}

static void thumb_high(Builder *b, unsigned opcode)
{
  unsigned rd = (opcode & 7) | ((opcode >> 4) & 8);
  unsigned rs = (opcode >> 3) & 15;
  unsigned source;
  unsigned dest;

  switch((opcode >> 8) & 3)
  {
    case 0:
      fetch(b, FETCH_SEQUENTIAL);
      source = reg(b, rs);
      dest = rd == 15 ? temp(b) : rd;
      emit(b, ARM_IR_ADD, 0, dest, reg(b, rd), source);
      if(rd == 15)
        jump(b, dest, 0, ARM_EXIT_JUMP);
      break;
    case 1:
      fetch(b, FETCH_SEQUENTIAL);
      source = reg(b, rs);
      emit(b, ARM_IR_SUB, NZCV, ARM_IR_NONE, reg(b, rd), source);
      break;
    case 2:
      fetch(b, FETCH_SEQUENTIAL);
      source = reg(b, rs);
      if(rd == 15)
        jump(b, source, 0, rs == 14 ? ARM_EXIT_RETURN : ARM_EXIT_JUMP);
      else
        emit(b, ARM_IR_MOV, 0, rd, 0, source);
      break;
    default:
      exchange(b, rs);
      break;
  }
}

static void thumb_load_store(Builder *b, unsigned opcode)
{
  static const uint8_t register_codes[8] =
  {
    ARM_IR_ST32, ARM_IR_ST16, ARM_IR_ST8, ARM_IR_LD8S,
    ARM_IR_LD32, ARM_IR_LD16, ARM_IR_LD8, ARM_IR_LD16S
  };
  unsigned rd = opcode & 7;
  unsigned rb = (opcode >> 3) & 7;
  unsigned top = opcode >> 11;

  switch(top)
  {
    case 9:
      /* LDR rd, [pc, #imm]: the pc reads word aligned. */
      transfer(b, ARM_IR_LD32, (opcode >> 8) & 7, 15,
       (opcode & 0xFF) * 4 - ((b->pc + 4) & 2), 0, 1, 1, 0);
      break;
    case 10:
    case 11:
      transfer(b, register_codes[(opcode >> 9) & 7], rd, rb,
       (opcode >> 6) & 7, 1, 1, 1, 0);
      break;
    case 12:
    case 13:
      transfer(b, (opcode & 0x800) ? ARM_IR_LD32 : ARM_IR_ST32, rd, rb,
       ((opcode >> 6) & 31) * 4, 0, 1, 1, 0);
      break;
    case 14:
    case 15:
      transfer(b, (opcode & 0x800) ? ARM_IR_LD8 : ARM_IR_ST8, rd, rb,
       (opcode >> 6) & 31, 0, 1, 1, 0);
      break;
    case 16:
    case 17:
      transfer(b, (opcode & 0x800) ? ARM_IR_LD16 : ARM_IR_ST16, rd, rb,
       ((opcode >> 6) & 31) * 2, 0, 1, 1, 0);
      break;
    default:
      transfer(b, (opcode & 0x800) ? ARM_IR_LD32 : ARM_IR_ST32,
       (opcode >> 8) & 7, 13, (opcode & 0xFF) * 4, 0, 1, 1, 0);
      break;
  }
}

static void thumb_address(Builder *b, unsigned opcode)
{
  unsigned rd = (opcode >> 8) & 7;
  uint32_t offset = (opcode & 0xFF) * 4;

  fetch(b, FETCH_SEQUENTIAL);
  if(opcode & 0x800)
    emit(b, ARM_IR_ADD, ARM_IR_IMM, rd, 13, offset);
  else
    emit(b, ARM_IR_MOV, ARM_IR_IMM, rd, 0, ((b->pc + 4) & ~3u) + offset);
}

static void thumb_misc(Builder *b, unsigned opcode)
{
  switch((opcode >> 8) & 15)
  {
    case 0:
      fetch(b, FETCH_SEQUENTIAL);
      emit(b, (opcode & 0x80) ? ARM_IR_SUB : ARM_IR_ADD, ARM_IR_IMM, 13, 13,
       (opcode & 0x7F) * 4);
      break;
    case 4:
    case 5:
      block_transfer(b, 13, (opcode & 0xFF) | ((opcode & 0x100) << 6), 0, 1,
       0, 1, 0);
      break;
    case 12:
    case 13:
      block_transfer(b, 13, (opcode & 0xFF) | ((opcode & 0x100) << 7), 1, 0,
       1, 1, 0);
      break;
    default:
      undefined(b);
      break;
  }
}

static void thumb_multiple(Builder *b, unsigned opcode)
{
  block_transfer(b, (opcode >> 8) & 7, opcode & 0xFF, (opcode >> 11) & 1, 0,
   1, 1, 0);
}

static void thumb_conditional(Builder *b, unsigned opcode)
{
  unsigned cond = (opcode >> 8) & 15;

  if(cond == ARM_COND_NV)
  {
    exception(b, ARM_IR_SWI, 0x08);
    return;
  }
  /* A Thumb branch is the only conditional Thumb instruction; reopen the
     instruction under its condition. The manual leaves condition 1110
     undefined, but the ARM7TDMI branches always. */
  if(cond != ARM_COND_AL)
    b->skip_op = emit(b, ARM_IR_SKIP, 0, 0, cond, 0);
  fetch(b, FETCH_SEQUENTIAL);
  branch(b, b->pc + 4 + (uint32_t)((int32_t)(int8_t)(opcode & 0xFF) * 2),
   ARM_EXIT_JUMP);
}

static void thumb_branch(Builder *b, unsigned opcode)
{
  int32_t offset = (int32_t)((uint32_t)opcode << 21) >> 20;

  fetch(b, FETCH_SEQUENTIAL);
  switch(opcode >> 11)
  {
    case 28:
      branch(b, b->pc + 4 + (uint32_t)offset, ARM_EXIT_JUMP);
      break;
    case 30:
      /* First half of BL: lr = pc + (offset << 12). */
      emit(b, ARM_IR_MOV, ARM_IR_IMM, 14, 0,
       b->pc + 4 + ((uint32_t)offset << 11));
      break;
    default:
    {
      /* Second half: jump to lr + offset, lr = return address | 1. */
      unsigned target = temp(b);

      emit(b, ARM_IR_ADD, ARM_IR_IMM, target, 14, (opcode & 0x7FF) * 2);
      emit(b, ARM_IR_MOV, ARM_IR_IMM, 14, 0, (b->pc + 2) | 1);
      jump(b, target, 0, ARM_EXIT_CALL);
      break;
    }
  }
}

enum
{
  THUMB_CLASS_SHIFT,
  THUMB_CLASS_ADD_SUB,
  THUMB_CLASS_IMMEDIATE,
  THUMB_CLASS_ALU,
  THUMB_CLASS_HIGH,
  THUMB_CLASS_LOAD_STORE,
  THUMB_CLASS_ADDRESS,
  THUMB_CLASS_MISC,
  THUMB_CLASS_MULTIPLE,
  THUMB_CLASS_CONDITIONAL,
  THUMB_CLASS_BRANCH,
  THUMB_CLASS_UNDEF
};

/* Indexed by opcode bits 15-8. */
static uint8_t thumb_table[256];

static unsigned thumb_classify(unsigned high)
{
  if(high < 0x18)
    return THUMB_CLASS_SHIFT;
  if(high < 0x20)
    return THUMB_CLASS_ADD_SUB;
  if(high < 0x40)
    return THUMB_CLASS_IMMEDIATE;
  if(high < 0x44)
    return THUMB_CLASS_ALU;
  if(high < 0x48)
    return THUMB_CLASS_HIGH;
  if(high < 0xA0)
    return THUMB_CLASS_LOAD_STORE;
  if(high < 0xB0)
    return THUMB_CLASS_ADDRESS;
  if(high < 0xC0)
    return THUMB_CLASS_MISC;
  if(high < 0xD0)
    return THUMB_CLASS_MULTIPLE;
  if(high < 0xE0)
    return THUMB_CLASS_CONDITIONAL;
  if(high >= 0xE8 && high < 0xF0)
    return THUMB_CLASS_UNDEF;
  return THUMB_CLASS_BRANCH;
}

static void thumb_instruction(Builder *b, unsigned opcode)
{
  begin(b, ARM_COND_AL);
  switch(thumb_table[opcode >> 8])
  {
    case THUMB_CLASS_SHIFT: thumb_shift(b, opcode); break;
    case THUMB_CLASS_ADD_SUB: thumb_add_sub(b, opcode); break;
    case THUMB_CLASS_IMMEDIATE: thumb_immediate(b, opcode); break;
    case THUMB_CLASS_ALU: thumb_alu(b, opcode); break;
    case THUMB_CLASS_HIGH: thumb_high(b, opcode); break;
    case THUMB_CLASS_LOAD_STORE: thumb_load_store(b, opcode); break;
    case THUMB_CLASS_ADDRESS: thumb_address(b, opcode); break;
    case THUMB_CLASS_MISC: thumb_misc(b, opcode); break;
    case THUMB_CLASS_MULTIPLE: thumb_multiple(b, opcode); break;
    case THUMB_CLASS_CONDITIONAL: thumb_conditional(b, opcode); break;
    case THUMB_CLASS_BRANCH: thumb_branch(b, opcode); break;
    default: undefined(b); break;
  }
  finish(b);
}

/* ---- Flag liveness ------------------------------------------------------ */

static unsigned condition_reads(unsigned cond)
{
  static const uint8_t reads[16] =
  {
    LIVE_NZ, LIVE_NZ, LIVE_C, LIVE_C, LIVE_NZ, LIVE_NZ, LIVE_V, LIVE_V,
    LIVE_NZ | LIVE_C, LIVE_NZ | LIVE_C, LIVE_NZ | LIVE_V, LIVE_NZ | LIVE_V,
    LIVE_NZ | LIVE_V, LIVE_NZ | LIVE_V, 0, 0
  };

  return reads[cond & 15];
}

/* Walks the block backwards and drops flag writes nothing observes. Every
   exit, and every point where a store may make the block leave early,
   observes all four flags. Ops under a condition never kill a flag, since
   the old value survives when they are skipped. */
static void liveness(ArmIrOp *ops, unsigned count)
{
  uint8_t conditional[ARM_BLOCK_MAX_OPS];
  unsigned live = LIVE_ALL;
  unsigned i;

  memset(conditional, 0, count);
  for(i = 0; i < count; i++)
    if(ops[i].code == ARM_IR_SKIP)
    {
      unsigned end = i + 1 + (ops[i].b & 0xFFFF);
      unsigned j;

      for(j = i + 1; j < end && j < count; j++)
        conditional[j] = 1;
    }

  for(i = count; i-- > 0;)
  {
    ArmIrOp *op = &ops[i];
    unsigned writes = 0;
    unsigned kills;

    switch(op->code)
    {
      case ARM_IR_INSN:
        if(op->flags & ARM_IR_CHECK)
          live = LIVE_ALL;
        continue;
      case ARM_IR_SKIP:
        live |= condition_reads(op->a);
        continue;
      case ARM_IR_EXIT:
      case ARM_IR_EXIT_REG:
      case ARM_IR_SWI:
      case ARM_IR_UNDEF:
      case ARM_IR_MRS:
        live = LIVE_ALL;
        continue;
      case ARM_IR_MSR:
        if((op->b & 0x18) == 8 && !conditional[i])
          live = 0;
        continue;
      case ARM_IR_SETC:
        if(!(live & LIVE_C))
          op->code = ARM_IR_NOP;
        else if(!conditional[i])
          live &= ~LIVE_C;
        continue;
      case ARM_IR_SETNZ64:
        if(!(live & LIVE_NZ))
          op->code = ARM_IR_NOP;
        else if(!conditional[i])
          live &= ~LIVE_NZ;
        continue;
      default:
        break;
    }
    if(!is_alu(op->code))
      continue;

    if(op->flags & ARM_IR_SET_NZ)
      writes |= LIVE_NZ;
    if(op->flags & ARM_IR_SET_C)
      writes |= LIVE_C;
    if(op->flags & ARM_IR_SET_V)
      writes |= LIVE_V;
    if(!(live & LIVE_NZ))
      op->flags &= ~ARM_IR_SET_NZ;
    if(!(live & LIVE_C))
      op->flags &= ~ARM_IR_SET_C;
    if(!(live & LIVE_V))
      op->flags &= ~ARM_IR_SET_V;
    if(op->d == ARM_IR_NONE &&
       !(op->flags & (ARM_IR_SET_NZ | ARM_IR_SET_C | ARM_IR_SET_V)))
    {
      op->code = ARM_IR_NOP;
      continue;
    }

    kills = writes;
    /* A shift by a register leaves C alone for an amount of zero. */
    if(op->code >= ARM_IR_LSL && op->code <= ARM_IR_ROR &&
       !(op->flags & ARM_IR_IMM))
      kills &= ~LIVE_C;
    if(!conditional[i])
      live &= ~kills;
    if(op->flags & ARM_IR_USE_C)
      live |= LIVE_C;
  }
}

/* ---- Blocks ------------------------------------------------------------ */

static void tables_init(void)
{
  static int ready;
  unsigned i;

  if(ready)
    return;
  for(i = 0; i < 4096; i++)
    arm_table[i] = (uint8_t)arm_classify(i >> 4, i & 15);
  for(i = 0; i < 256; i++)
    thumb_table[i] = (uint8_t)thumb_classify(i);
  ready = 1;
}

unsigned arm_translate(ArmCore *core, uint32_t pc, int thumb,
 ArmIrOp *ops, unsigned max_ops, unsigned max_insns, ArmIrBlockInfo *info)
{
  Builder b;
  uint32_t page_end = 0;
  unsigned insns = 0;
  unsigned region = pc >> 24;

  tables_init();
  if(max_ops > ARM_BLOCK_MAX_OPS)
    max_ops = ARM_BLOCK_MAX_OPS;
  memset(&b, 0, sizeof(b));
  b.core = core;
  b.ops = ops;
  b.pc = pc;
  b.thumb = thumb;
  b.width = thumb ? 2 : 4;
  b.skip_op = NO_OP;
  b.cartridge = region >= 8 && region <= 13;
  b.prefetch = b.cartridge && (core->waitcnt & 0x4000);
  if(region == 2 || region == 3)
    page_end = (pc | (ARM_CODE_PAGE_SIZE - 1)) + 1;

  while(insns < max_insns && b.count + INSN_MAX_OPS <= max_ops &&
   (insns == 0 || ((page_end == 0 || b.pc != page_end) &&
    (core->hook == 0 || b.pc != (core->hook & ~1u)))))
  {
    uint32_t word = core->bus.code32(core->bus.context, b.pc & ~3u);

    if(thumb)
      thumb_instruction(&b, (b.pc & 2) ? word >> 16 : word & 0xFFFF);
    else
      arm_instruction(&b, word);
    insns++;
    b.pc += b.width;
    if(b.ended)
      break;
  }
  if(!b.ended)
    emit(&b, ARM_IR_EXIT, ARM_IR_LIMIT, ARM_EXIT_NEXT, 0, b.pc);
  liveness(ops, b.count);
  if(info)
  {
    info->end = b.pc;
    info->insns = insns;
    info->ops = b.count;
  }
  return b.count;
}

const char *arm_ir_name(unsigned code)
{
  static const char *const names[ARM_IR_CODES] =
  {
    "nop", "insn", "skip", "cycles",
    "mov", "mvn", "and", "eor", "orr", "bic",
    "add", "adc", "sub", "sbc", "rsb", "rsc",
    "lsl", "lsr", "asr", "ror", "rrx",
    "mul", "umulh", "smulh", "sltu", "setc", "setnz64", "mulcyc",
    "ld8", "ld8s", "ld16", "ld16s", "ld32", "st8", "st16", "st32",
    "idle", "flush", "mrs", "msr", "user_get", "user_set",
    "exit", "exit_reg", "swi", "undef"
  };

  return code < ARM_IR_CODES ? names[code] : "?";
}
