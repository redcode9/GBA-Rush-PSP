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
 * MIPS code generator for the IR of arm_ir.h, for the PSP's Allegrex.
 *
 * Blocks come from the interpreter's cache (arm_interp.c), which keeps
 * their IR until the cache is flushed. That makes the interpreter the
 * backend's way out: whatever the generated code does not handle itself
 * (MSR, MRS, SWI, exception returns, user bank transfers, idle loop
 * branches, and the rest of a block after a store asked the core to stop)
 * it hands over by saving the state and naming the op to go on from. The
 * interpreter is also the test oracle, so the two must agree on every
 * cycle.
 *
 * Registers. r0-r7 live in s0-s7 and r13 in fp, which C preserves. r8-r12
 * live in t0-t4 and r14 in t5 for the whole run, rather than being loaded
 * per block: ARM code (the sound mixer, interrupt handlers) uses them all
 * the time, and with every caller-saved register then holding guest state
 * all calls go through one routine that saves them. t6 and t7 hold N and
 * Z, t8 holds C (0 or 1), t9 the cycle count, at the GamePak prefetch
 * level and gp the ArmCore. That leaves a0-a3 for the IR's temporaries
 * (a temporary that finds none free lives in core.v) and v0, v1 and ra as
 * scratch within an op. V stays in core.flag_v: the liveness pass leaves
 * few V computations, and conditions that need it load it.
 *
 * Flags. N and Z follow the IR's convention (sign of flag_n, flag_z zero),
 * but within a block they are only a note of which host register holds the
 * last result: a flag-setting op costs nothing for them, and a condition
 * tests that register with bltz or beq. They are copied into t6 and t7
 * only before that register changes, at conditional sections and at exits.
 * C comes from sltu; ext extracts shifter carries.
 *
 * Timing. Cycles known at translate time are summed in the compiler and
 * added to t9 just before anything can look at them: an exit, a call, the
 * end of a conditional section. Slow paths add the sum on their way in and
 * take it out again on their way back. t9 counts cycles minus cycle_base,
 * so the budget check at a taken branch is a single bgez; the C side sets
 * the base to the current count when an interrupt is waiting, which makes
 * the next taken branch leave as the interpreter's run loop would. The
 * prefetch buffer is one level in cycles (see arm_interp.c). A run of
 * opcode fetches from it has a closed form, four instructions with the
 * Allegrex's min and max however long the run, and fills add up before
 * one cap; after a GamePak data access or a known fill the compiler even
 * knows the level and computes the fetches itself.
 *
 * Exits. Every block starts with its key, then a refill entry that adds the
 * pipeline refill for indirect jumps, checks the budget and empties the
 * prefetch buffer, then the main entry. Direct exits check the budget
 * (taken branches only) and end with a j that the C side patches once the
 * target is compiled. Indirect exits look the target up in a direct-mapped
 * dispatch cache, (pc, Thumb) -> refill entry; returns first try a
 * 16-entry return stack, which calls fill with the address of a per-call
 * slot in the caller (a patchable j to the return point's refill entry).
 * A dropped block gets its main entry overwritten with a jal to a stub
 * that leaves for C, so nothing has to track who links to it.
 *
 * Memory. A load or store whose address the decoder knew becomes a direct
 * access, or a call for I/O. Other addresses are guessed: the compiler
 * runs the block's address arithmetic on the registers as they are when
 * it compiles, and checks the guessed region at run time with three or four
 * instructions (rotr folds the alignment check into the region check);
 * stores also look up a map of the work RAM holding translated code, one
 * byte per 32 bytes. LDM and STM check once for all their words. Whatever
 * fails takes the slow path: first a small routine that handles any work
 * RAM address (shared code such as the BIOS's copies sees buffers in
 * different places), then the interpreter's own access function for I/O,
 * unaligned addresses and stores over code. Exit requests (a store to
 * HALTCNT, code overwritten) only arise in that call, so the hot path
 * never checks for them: the slow path does, and hands the rest of the
 * block to the interpreter, which stops where it would have.
 *
 * Layout. Each block's slow paths, stubs and link records follow its hot
 * code, so the hot paths of a loop stay dense in the 16 KB instruction
 * cache while every branch between the two stays within beq's reach. One
 * shared cold area would need long jumps.
 *
 * Tests run this code in Unicorn, taught the four Allegrex instructions
 * it lacks (scripts/build-unicorn.sh) rather than given substitutes, so
 * they check the words the PSP runs (tests/core/mips_unicorn.c).
 */

#include "arm_mips.h"

#include <stddef.h>
#include <string.h>

/* ---- MIPS encoding ------------------------------------------------------ */

enum
{
  ZERO = 0, AT = 1, V0 = 2, V1 = 3, A0 = 4, A1 = 5, A2 = 6, A3 = 7,
  T0 = 8, T5 = 13, T6 = 14, T7 = 15, T8 = 24, T9 = 25,
  GP = 28, SP = 29, FP = 30, RA = 31
};

#define REG_PREFETCH AT
#define REG_LR T5
#define REG_N T6
#define REG_Z T7
#define REG_C T8
#define REG_CYCLES T9

/* Host register of each guest register r0-r14. */
static const uint8_t guest_register[15] =
{
  16, 17, 18, 19, 20, 21, 22, 23, 8, 9, 10, 11, 12, FP, REG_LR
};

#define R_TYPE(rs, rt, rd, sa, fn) ((uint32_t)(rs) << 21 | \
 (uint32_t)(rt) << 16 | (uint32_t)(rd) << 11 | (uint32_t)(sa) << 6 | (fn))
#define I_TYPE(op, rs, rt, imm) ((uint32_t)(op) << 26 | \
 (uint32_t)(rs) << 21 | (uint32_t)(rt) << 16 | ((uint32_t)(imm) & 0xFFFF))

#define ADDU(d, s, t) R_TYPE(s, t, d, 0, 0x21)
#define SUBU(d, s, t) R_TYPE(s, t, d, 0, 0x23)
#define AND(d, s, t) R_TYPE(s, t, d, 0, 0x24)
#define OR(d, s, t) R_TYPE(s, t, d, 0, 0x25)
#define XOR(d, s, t) R_TYPE(s, t, d, 0, 0x26)
#define NOR(d, s, t) R_TYPE(s, t, d, 0, 0x27)
#define SLTU(d, s, t) R_TYPE(s, t, d, 0, 0x2B)
#define MOVZ(d, s, t) R_TYPE(s, t, d, 0, 0x0A)
#define MOVN(d, s, t) R_TYPE(s, t, d, 0, 0x0B)
#define MOVE(d, s) OR(d, s, ZERO)
#define SLL(d, t, sa) R_TYPE(0, t, d, sa, 0x00)
#define SRL(d, t, sa) R_TYPE(0, t, d, sa, 0x02)
#define SRA(d, t, sa) R_TYPE(0, t, d, sa, 0x03)
#define SLLV(d, t, s) R_TYPE(s, t, d, 0, 0x04)
#define SRLV(d, t, s) R_TYPE(s, t, d, 0, 0x06)
#define SRAV(d, t, s) R_TYPE(s, t, d, 0, 0x07)
#define MULT(s, t) R_TYPE(s, t, 0, 0, 0x18)
#define MULTU(s, t) R_TYPE(s, t, 0, 0, 0x19)
#define MFHI(d) R_TYPE(0, 0, d, 0, 0x10)
#define MFLO(d) R_TYPE(0, 0, d, 0, 0x12)
#define JR(s) R_TYPE(s, 0, 0, 0, 0x08)
#define JALR(s) R_TYPE(s, 0, RA, 0, 0x09)
#define NOP 0u
/* Allegrex: rotr and rotrv as in MIPS32r2; clz, max and min are SPECIAL
   functions 0x16, 0x2C and 0x2D there (MIPS32 has no max or min, and its
   clz is in SPECIAL2). */
#define ROTR(d, t, sa) R_TYPE(1, t, d, sa, 0x02)
#define ROTRV(d, t, s) R_TYPE(s, t, d, 1, 0x06)
#define CLZ(d, s) R_TYPE(s, 0, d, 0, 0x16)
#define MAX(d, s, t) R_TYPE(s, t, d, 0, 0x2C)
#define MIN(d, s, t) R_TYPE(s, t, d, 0, 0x2D)
#define EXT(t, s, pos, size) (0x1Fu << 26 | (uint32_t)(s) << 21 | \
 (uint32_t)(t) << 16 | (uint32_t)((size) - 1) << 11 | (uint32_t)(pos) << 6)
#define INS(t, s, pos, size) (0x1Fu << 26 | (uint32_t)(s) << 21 | \
 (uint32_t)(t) << 16 | (uint32_t)((pos) + (size) - 1) << 11 | \
 (uint32_t)(pos) << 6 | 4u)

#define ADDIU(t, s, imm) I_TYPE(0x09, s, t, imm)
#define SLTIU(t, s, imm) I_TYPE(0x0B, s, t, imm)
#define ANDI(t, s, imm) I_TYPE(0x0C, s, t, imm)
#define ORI(t, s, imm) I_TYPE(0x0D, s, t, imm)
#define XORI(t, s, imm) I_TYPE(0x0E, s, t, imm)
#define LUI(t, imm) I_TYPE(0x0F, 0, t, imm)
#define LB(t, off, s) I_TYPE(0x20, s, t, off)
#define LH(t, off, s) I_TYPE(0x21, s, t, off)
#define LW(t, off, s) I_TYPE(0x23, s, t, off)
#define LBU(t, off, s) I_TYPE(0x24, s, t, off)
#define LHU(t, off, s) I_TYPE(0x25, s, t, off)
#define SB(t, off, s) I_TYPE(0x28, s, t, off)
#define SH(t, off, s) I_TYPE(0x29, s, t, off)
#define SW(t, off, s) I_TYPE(0x2B, s, t, off)
/* Branches, offset left for the fixup. */
#define BEQ(s, t) I_TYPE(0x04, s, t, 0)
#define BNE(s, t) I_TYPE(0x05, s, t, 0)
#define BGTZ(s) I_TYPE(0x07, s, 0, 0)
#define BLTZ(s) I_TYPE(0x01, s, 0, 0)
#define BGEZ(s) I_TYPE(0x01, s, 1, 0)
#define J(address) (2u << 26 | ((address) >> 2 & 0x3FFFFFFu))
#define JAL(address) (3u << 26 | ((address) >> 2 & 0x3FFFFFFu))

/* Offsets from gp. */
#define CORE(field) ((int)offsetof(ArmCore, field))
#define JIT(field) ((int)offsetof(ArmMips, field) - \
 (int)offsetof(ArmMips, core))
#define SLOT(slot) (CORE(v) + 4 * (int)(slot))
#define SPILL(reg) (JIT(spill) + 4 * (int)(reg))

static int fits16(uint32_t value)
{
  return (int32_t)value >= -32768 && (int32_t)value <= 32767;
}

/* ---- Statuses generated code hands back to C ----------------------------- */

/* The low four bits; the exit kind is in bits 4-7, the Thumb state of the
   code that left in bit 8. exit_arg holds the argument. */
enum
{
  STATUS_BUDGET = 1,    /* a taken branch found the budget spent: key */
  STATUS_LINK,          /* a direct exit not linked yet: its record */
  STATUS_MISS,          /* the dispatch cache missed: key, refill not
                           counted yet */
  STATUS_BAIL,          /* the interpreter goes on: op index */
  STATUS_DROPPED,       /* a dropped block was entered: its key */
  STATUS_EXIT           /* code compiled to run once left: key */
};
#define STATUS_THUMB 0x100u
#define STATUS_STEP 0x200u     /* the bail's op is in step_ops */

/* Link record: patch site, key, flags. A return slot is patched to the
   refill entry, and C counts the refill when it links one. */
#define LINK_RETURN 1u

/* Words of a block before its main entry: key, then the refill entry. */
#define BLOCK_HEADER 4u
#define INVALID_KEY 2u

/* ---- The compiler's state ----------------------------------------------- */

#define HOT_WORDS 8192u
#define COLD_WORDS 24576u
#define LABELS 2048u
#define FIXUPS 4096u
#define COLD 0x80000000u
#define UNBOUND 0xFFFFFFFFu

enum { FIX_BRANCH, FIX_JUMP, FIX_HI_LO, FIX_WORD };

enum { MODE_LINKED, MODE_ONCE, MODE_STEP };

typedef struct
{
  uint32_t where;
  uint16_t label;
  uint16_t kind;
} Fixup;

typedef struct
{
  ArmMips *jit;
  ArmCore *core;
  const ArmIrOp *ops;
  unsigned count;
  uint32_t op_base;        /* index of ops[0] for bails */
  unsigned mode;
  uint32_t key;
  int thumb;
  unsigned width;
  unsigned access;         /* prefetch: the code's sequential halfword */
  unsigned full;           /* a full prefetch buffer */
  int32_t pending;         /* cycles not yet added to t9 */
  unsigned fetches;        /* prefetch buffer fetches not yet emitted */
  int32_t fill;            /* and fill, constant part */
  int filled;              /* a fill waits for its cap */
  int level_known;         /* the prefetch level is level, whatever the
                              register holds */
  unsigned level;
  unsigned insns;          /* INSN ops so far */
  uint32_t pc;
  int dead;                /* nothing reaches the ops until the section
                              ends */
  int failed;
  unsigned n_reg;          /* where N and Z are now */
  unsigned z_reg;
  unsigned section_end;    /* op index ending the open section, or 0 */
  unsigned section_label;
  int32_t section_pending;
  int section_settled;     /* N and Z were settled when it opened */
  unsigned section_n;      /* and where they were */
  unsigned section_z;
  uint8_t temp_reg[ARM_IR_SLOTS];   /* 0: kept in core.v */
  uint16_t last_use[ARM_IR_SLOTS];
  unsigned temps_free;     /* free registers among a0-a3 */
  unsigned scratch_used;
  /* Values as the decoder knows them, and guesses from the registers. */
  uint8_t known[ARM_IR_SLOTS];
  uint32_t value[ARM_IR_SLOTS];
  uint8_t guessed[ARM_IR_SLOTS];
  uint32_t guess[ARM_IR_SLOTS];
  int cold;
  unsigned hot_count;
  unsigned cold_count;
  unsigned label_count;
  unsigned fixup_count;
  uint32_t hot[HOT_WORDS];
  uint32_t cold_code[COLD_WORDS];
  uint32_t labels[LABELS];
  Fixup fixups[FIXUPS];
} Builder;

/* ponytail: one builder for every ArmMips, as compiles never overlap. */
static Builder builder;

static ArmMips *jit_of(ArmCore *core)
{
  return (ArmMips *)(void *)((char *)core - offsetof(ArmMips, core));
}

static uint32_t target_of(const ArmMips *jit, const void *host)
{
  if(jit->host.target)
    return jit->host.target(jit->host.context, host);
  return (uint32_t)(uintptr_t)host;
}

static uint32_t code_address(const ArmMips *jit, uint32_t word)
{
  return jit->buffer_target + 4 * word;
}

static uint32_t *code_host(const ArmMips *jit, uint32_t address)
{
  return jit->buffer + (address - jit->buffer_target) / 4;
}

static void sync_code(ArmMips *jit, uint32_t *code, uint32_t words)
{
  jit->host.sync(jit->host.context, code, 4 * words);
}

/* ---- Emitting ----------------------------------------------------------- */

static uint32_t position(const Builder *b)
{
  return b->cold ? COLD | b->cold_count : b->hot_count;
}

static void emit(Builder *b, uint32_t word)
{
  if(b->cold)
  {
    if(b->cold_count < COLD_WORDS)
      b->cold_code[b->cold_count++] = word;
    else
      b->failed = 1;
  }
  else if(b->hot_count < HOT_WORDS)
    b->hot[b->hot_count++] = word;
  else
    b->failed = 1;
}

static unsigned label_new(Builder *b)
{
  if(b->label_count >= LABELS)
  {
    b->failed = 1;
    return 0;
  }
  b->labels[b->label_count] = UNBOUND;
  return b->label_count++;
}

static void label_bind(Builder *b, unsigned label)
{
  b->labels[label] = position(b);
}

/* Emits word, to be completed with the address of label. */
static void emit_fixup(Builder *b, uint32_t word, unsigned label,
 unsigned kind)
{
  if(b->fixup_count >= FIXUPS)
    b->failed = 1;
  else
  {
    Fixup *f = &b->fixups[b->fixup_count++];

    f->where = position(b);
    f->label = (uint16_t)label;
    f->kind = (uint16_t)kind;
  }
  emit(b, word);
}

static void branch(Builder *b, uint32_t word, unsigned label)
{
  emit_fixup(b, word, label, FIX_BRANCH);
}

static void load_constant(Builder *b, unsigned reg, uint32_t value)
{
  if(fits16(value))
    emit(b, ADDIU(reg, ZERO, value));
  else if(value <= 0xFFFF)
    emit(b, ORI(reg, ZERO, value));
  else
  {
    emit(b, LUI(reg, value >> 16));
    if(value & 0xFFFF)
      emit(b, ORI(reg, reg, value & 0xFFFF));
  }
}

/* lui and ori loading the address of label. */
static void load_label(Builder *b, unsigned reg, unsigned label)
{
  emit_fixup(b, LUI(reg, 0), label, FIX_HI_LO);
  emit(b, ORI(reg, reg, 0));
}

static void add_cycles(Builder *b, int32_t cycles)
{
  if(!cycles)
    return;
  if(fits16((uint32_t)cycles))
    emit(b, ADDIU(REG_CYCLES, REG_CYCLES, cycles));
  else
  {
    load_constant(b, V1, (uint32_t)cycles);
    emit(b, ADDU(REG_CYCLES, REG_CYCLES, V1));
  }
}

static void flush_pending(Builder *b)
{
  add_cycles(b, b->pending);
  b->pending = 0;
}

/* ---- Registers ---------------------------------------------------------- */

/* A scratch register for the op being compiled: v0, v1, ra, then free
   temporary registers. */
static unsigned scratch(Builder *b)
{
  static const uint8_t order[] = { V0, V1, RA, A0, A1, A2, A3 };
  unsigned i;

  for(i = 0; i < sizeof(order); i++)
  {
    unsigned r = order[i];

    if(b->scratch_used & (1u << r))
      continue;
    if(r >= A0 && r <= A3 && !(b->temps_free & (1u << r)))
      continue;
    b->scratch_used |= 1u << r;
    return r;
  }
  b->failed = 1;
  return V0;
}

static void scratch_release(Builder *b, unsigned r)
{
  b->scratch_used &= ~(1u << r);
}

/* Copies N and Z into their registers. */
static void settle_copy(Builder *b)
{
  if(b->n_reg != REG_N)
    emit(b, MOVE(REG_N, b->n_reg));
  if(b->z_reg != REG_Z)
    emit(b, MOVE(REG_Z, b->z_reg));
}

static void settle(Builder *b)
{
  settle_copy(b);
  b->n_reg = REG_N;
  b->z_reg = REG_Z;
}

/* The host register holding slot. */
static unsigned source(Builder *b, unsigned slot)
{
  unsigned r;

  if(slot < 15)
    return guest_register[slot];
  if(b->temp_reg[slot])
    return b->temp_reg[slot];
  r = scratch(b);
  emit(b, LW(r, SLOT(slot), GP));
  return r;
}

/* The host register an op writes slot to; with sets_nz the op also
   becomes where N and Z are. commit() completes the write. */
static unsigned destination(Builder *b, unsigned slot, int sets_nz)
{
  unsigned r;

  if(slot < 15)
  {
    r = guest_register[slot];
    if(!sets_nz && (r == b->n_reg || r == b->z_reg))
      settle(b);
    return r;
  }
  for(r = A0; r <= A3; r++)
    if((b->temps_free & (1u << r)) && !(b->scratch_used & (1u << r)))
    {
      b->temps_free &= ~(1u << r);
      b->temp_reg[slot] = (uint8_t)r;
      return r;
    }
  b->temp_reg[slot] = 0;
  return scratch(b);
}

static void commit(Builder *b, unsigned slot, unsigned r)
{
  if(slot >= ARM_IR_FIRST_TEMP && slot != ARM_IR_NONE && !b->temp_reg[slot])
    emit(b, SW(r, SLOT(slot), GP));
}

static void temp_free(Builder *b, unsigned slot)
{
  if(slot >= ARM_IR_FIRST_TEMP && slot != ARM_IR_NONE && b->temp_reg[slot])
  {
    b->temps_free |= 1u << b->temp_reg[slot];
    b->temp_reg[slot] = 0;
  }
}

/* ---- Op properties ------------------------------------------------------ */

static int is_alu(unsigned code)
{
  return code >= ARM_IR_MOV && code <= ARM_IR_SLTU;
}

static int is_load(unsigned code)
{
  return code >= ARM_IR_LD8 && code <= ARM_IR_LD32;
}

static int is_store(unsigned code)
{
  return code >= ARM_IR_ST8 && code <= ARM_IR_ST32;
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

/* The slots an op reads, into reads[]; returns how many. */
static unsigned op_reads(const ArmIrOp *op, unsigned *reads)
{
  unsigned n = 0;

  if(is_alu(op->code))
  {
    if(op->code != ARM_IR_MOV && op->code != ARM_IR_MVN)
      reads[n++] = op->a;
    if(!(op->flags & ARM_IR_IMM))
      reads[n++] = op->b & 0xFF;
    return n;
  }
  switch(op->code)
  {
    case ARM_IR_SETNZ64:
      reads[n++] = op->a;
      reads[n++] = op->b & 0xFF;
      break;
    case ARM_IR_ST8: case ARM_IR_ST16: case ARM_IR_ST32:
      reads[n++] = op->d;
      /* fall through */
    case ARM_IR_LD8: case ARM_IR_LD8S: case ARM_IR_LD16: case ARM_IR_LD16S:
    case ARM_IR_LD32: case ARM_IR_MULCYC: case ARM_IR_MSR:
    case ARM_IR_USER_SET: case ARM_IR_EXIT_REG:
      reads[n++] = op->a;
      break;
    default:
      break;
  }
  return n;
}

/* Last reads of the temporaries of the instruction starting at op i. */
static void instruction_uses(Builder *b, unsigned i)
{
  unsigned j;

  for(j = i + 1; j < b->count && b->ops[j].code != ARM_IR_INSN; j++)
  {
    unsigned reads[3];
    unsigned n = op_reads(&b->ops[j], reads);

    while(n--)
      if(reads[n] >= ARM_IR_FIRST_TEMP)
        b->last_use[reads[n]] = (uint16_t)j;
    if(b->ops[j].d >= ARM_IR_FIRST_TEMP && b->ops[j].d != ARM_IR_NONE &&
       b->last_use[b->ops[j].d] < j)
      b->last_use[b->ops[j].d] = (uint16_t)j;
  }
}

/* Frees the temporaries op i read for the last time, or wrote unused. */
static void release(Builder *b, unsigned i)
{
  const ArmIrOp *op = &b->ops[i];
  unsigned reads[3];
  unsigned n = op_reads(op, reads);

  while(n--)
    if(reads[n] >= ARM_IR_FIRST_TEMP && b->last_use[reads[n]] <= i)
      temp_free(b, reads[n]);
  if(op->d >= ARM_IR_FIRST_TEMP && op->d != ARM_IR_NONE &&
     b->last_use[op->d] <= i && !is_store(op->code))
    temp_free(b, op->d);
}

/* ---- Known values and guesses ------------------------------------------- */

static int evaluate(unsigned code, uint32_t x, uint32_t y, uint32_t *out)
{
  switch(code)
  {
    case ARM_IR_MOV: *out = y; return 1;
    case ARM_IR_MVN: *out = ~y; return 1;
    case ARM_IR_AND: *out = x & y; return 1;
    case ARM_IR_EOR: *out = x ^ y; return 1;
    case ARM_IR_ORR: *out = x | y; return 1;
    case ARM_IR_BIC: *out = x & ~y; return 1;
    case ARM_IR_ADD: *out = x + y; return 1;
    case ARM_IR_SUB: *out = x - y; return 1;
    case ARM_IR_RSB: *out = y - x; return 1;
    default: return 0;
  }
}

/* Follows op through the known values (dropped for guest registers
   written under a condition, as the decoder does) and the guesses. */
static void follow(Builder *b, const ArmIrOp *op)
{
  unsigned d = op->d;
  uint32_t y = op->b;
  uint32_t out;
  int imm = (op->flags & ARM_IR_IMM) != 0;

  if(d == ARM_IR_NONE || op->code == ARM_IR_INSN || op->code == ARM_IR_SKIP ||
     is_store(op->code) || op->code >= ARM_IR_EXIT)
    return;
  if(is_alu(op->code) && !(op->flags & ARM_IR_USE_C))
  {
    int known = imm || b->known[y & 0xFF];
    int guessed = imm || b->guessed[y & 0xFF];
    uint32_t ky = imm ? y : b->value[y & 0xFF];
    uint32_t gy = imm ? y : b->guess[y & 0xFF];

    if(op->code != ARM_IR_MOV && op->code != ARM_IR_MVN)
    {
      known = known && b->known[op->a];
      guessed = guessed && b->guessed[op->a];
    }
    b->known[d] = (uint8_t)(known && evaluate(op->code, b->value[op->a], ky,
     &out) && !(d < 15 && b->section_end));
    if(b->known[d])
      b->value[d] = out;
    b->guessed[d] = (uint8_t)(guessed && evaluate(op->code, b->guess[op->a],
     gy, &out));
    if(b->guessed[d])
      b->guess[d] = out;
    return;
  }
  b->known[d] = 0;
  b->guessed[d] = 0;
}

/* ---- Conditions --------------------------------------------------------- */

/* Branches to label unless cond holds. */
static void branch_unless(Builder *b, unsigned cond, unsigned label)
{
  unsigned n = b->n_reg;
  unsigned z = b->z_reg;

  if(cond >= ARM_COND_VS && cond <= ARM_COND_LE && cond != ARM_COND_HI &&
     cond != ARM_COND_LS)
    emit(b, LW(V1, CORE(flag_v), GP));
  switch(cond)
  {
    case ARM_COND_EQ: branch(b, BNE(z, ZERO), label); break;
    case ARM_COND_NE: branch(b, BEQ(z, ZERO), label); break;
    case ARM_COND_CS: branch(b, BEQ(REG_C, ZERO), label); break;
    case ARM_COND_CC: branch(b, BNE(REG_C, ZERO), label); break;
    case ARM_COND_MI: branch(b, BGEZ(n), label); break;
    case ARM_COND_PL: branch(b, BLTZ(n), label); break;
    case ARM_COND_VS: branch(b, BGEZ(V1), label); break;
    case ARM_COND_VC: branch(b, BLTZ(V1), label); break;
    case ARM_COND_HI:
      branch(b, BEQ(REG_C, ZERO), label);
      emit(b, NOP);
      branch(b, BEQ(z, ZERO), label);
      break;
    case ARM_COND_LS:
      /* Skip when C is set and Z clear. */
      emit(b, SLTU(V0, ZERO, z));
      emit(b, AND(V0, V0, REG_C));
      branch(b, BNE(V0, ZERO), label);
      break;
    case ARM_COND_GE:
      emit(b, XOR(V0, n, V1));
      branch(b, BLTZ(V0), label);
      break;
    case ARM_COND_LT:
      emit(b, XOR(V0, n, V1));
      branch(b, BGEZ(V0), label);
      break;
    case ARM_COND_GT:
      emit(b, XOR(V0, n, V1));
      branch(b, BEQ(z, ZERO), label);
      emit(b, NOP);
      branch(b, BLTZ(V0), label);
      break;
    case ARM_COND_LE:
      /* Skip when Z is clear and N equals V. */
      emit(b, XOR(V0, n, V1));
      emit(b, SRL(V0, V0, 31));
      emit(b, SLTU(V1, ZERO, z));
      emit(b, SUBU(V0, V1, V0));
      branch(b, BGTZ(V0), label);
      break;
    case ARM_COND_AL:
      return;
    default:
      branch(b, BEQ(ZERO, ZERO), label);
      break;
  }
  emit(b, NOP);
}

/* ---- Leaving ------------------------------------------------------------ */

static void count_instructions(Builder *b)
{
  if(!b->jit->count_instructions)
    return;
  emit(b, LW(V1, CORE(instructions), GP));
  emit(b, ADDIU(V1, V1, b->insns));
  emit(b, SW(V1, CORE(instructions), GP));
}

static uint32_t status(const Builder *b, unsigned code, unsigned kind)
{
  return code | kind << 4 | (b->thumb ? STATUS_THUMB : 0) |
   (b->mode == MODE_STEP ? STATUS_STEP : 0);
}

/* Returns to C: a1 already holds the argument. */
static void leave(Builder *b, uint32_t code)
{
  emit(b, J(b->jit->routine_exit));
  emit(b, ORI(A0, ZERO, code));
}

/* Hands the block to the interpreter at op index, from the state as it is
   here: N and Z copied, live temporaries stored to core.v (from the spill
   area when a slow path saved them there), insn_pc set. With exact the
   cycle register already holds every cycle. */
static void bail(Builder *b, unsigned index, int spilled, int exact,
 int index_in_a1)
{
  unsigned slot;

  settle_copy(b);
  for(slot = ARM_IR_FIRST_TEMP; slot < ARM_IR_SLOTS; slot++)
  {
    unsigned r = b->temp_reg[slot];

    if(!r || b->last_use[slot] < index)
      continue;
    if(spilled)
    {
      emit(b, LW(V0, SPILL(r), GP));
      r = V0;
    }
    emit(b, SW(r, SLOT(slot), GP));
  }
  load_constant(b, V1, b->pc);
  emit(b, SW(V1, CORE(insn_pc), GP));
  count_instructions(b);
  if(!exact)
    add_cycles(b, b->pending);
  if(!spilled && b->level_known)
    load_constant(b, REG_PREFETCH, b->level);
  if(index_in_a1)
  {
    load_constant(b, V0, b->op_base);
    emit(b, ADDU(A1, A1, V0));
  }
  else
    load_constant(b, A1, b->op_base + index);
  leave(b, status(b, STATUS_BAIL, 0));
}

/* Bails out at op index for good: the ops after it are dead. */
static void bail_here(Builder *b, unsigned index)
{
  bail(b, index, 0, 0, 0);
  b->dead = 1;
}

/* The code that runs key, if it is compiled: its refill entry. */
static uint32_t compiled(const ArmMips *jit, uint32_t key)
{
  const uint32_t *entry = jit->dispatch[(key >> 1) & (ARM_MIPS_DISPATCH - 1)];

  return entry[0] == key ? entry[1] : 0;
}

/* A patchable j to the block at key: straight there if it is compiled,
   else to a stub and record that ask C to link it. */
static void link_jump(Builder *b, uint32_t key, unsigned flags)
{
  uint32_t refill = b->mode == MODE_LINKED ? compiled(b->jit, key) : 0;
  unsigned site;
  unsigned stub;
  unsigned record;
  int was_cold = b->cold;

  if(refill)
  {
    emit(b, J((flags & LINK_RETURN) ? refill : refill + 12));
    emit(b, NOP);
    return;
  }
  site = label_new(b);
  stub = label_new(b);
  record = label_new(b);
  label_bind(b, site);
  emit_fixup(b, J(0), stub, FIX_JUMP);
  emit(b, NOP);
  b->cold = 1;
  label_bind(b, stub);
  load_label(b, A1, record);
  leave(b, STATUS_LINK);
  label_bind(b, record);
  emit_fixup(b, 0, site, FIX_WORD);
  emit(b, key);
  emit(b, flags);
  b->cold = was_cold;
}

/* Pushes the return point of a call onto the return stack. */
static void return_push(Builder *b)
{
  uint32_t key = (b->pc + b->width) | (uint32_t)b->thumb;
  unsigned slot = label_new(b);

  emit(b, LW(V1, JIT(return_top), GP));
  emit(b, ADDIU(V1, V1, 8));
  emit(b, ANDI(V1, V1, 8 * ARM_MIPS_RETURNS - 8));
  emit(b, SW(V1, JIT(return_top), GP));
  emit(b, ADDU(V1, V1, GP));
  load_constant(b, A0, key);
  emit(b, SW(A0, JIT(returns), V1));
  load_label(b, A0, slot);
  emit(b, SW(A0, JIT(returns) + 4, V1));
  b->cold = 1;
  label_bind(b, slot);
  link_jump(b, key, LINK_RETURN);
  b->cold = 0;
}

static void profile(Builder *b, unsigned counter)
{
  if(!b->jit->profiling)
    return;
  emit(b, LW(V1, JIT(profile) + 4 * (int)counter, GP));
  emit(b, ADDIU(V1, V1, 1));
  emit(b, SW(V1, JIT(profile) + 4 * (int)counter, GP));
}

/* The key of a direct exit's target. */
static uint32_t exit_key(const Builder *b, const ArmIrOp *op)
{
  uint32_t target = op->b;
  int thumb = b->thumb;

  if(op->flags & ARM_IR_BX)
    thumb = (int)(target & 1);
  return (target & (thumb ? ~1u : ~3u)) | (uint32_t)thumb;
}

static void exit_direct(Builder *b, const ArmIrOp *op, unsigned index)
{
  unsigned kind = op->d;
  uint32_t key = exit_key(b, op);

  /* An idle loop's branch leaves the run anyway: the interpreter takes it
     and spends the budget. */
  if(kind == ARM_EXIT_SYSTEM || (op->flags & ARM_IR_IDLE_LOOP))
  {
    bail_here(b, index);
    return;
  }
  settle(b);
  flush_pending(b);
  count_instructions(b);
  if(kind != ARM_EXIT_NEXT)
  {
    if(b->mode == MODE_LINKED)
    {
      unsigned budget = label_new(b);

      branch(b, BGEZ(REG_CYCLES), budget);
      emit(b, MOVE(REG_PREFETCH, ZERO));
      b->cold = 1;
      label_bind(b, budget);
      load_constant(b, A1, key);
      leave(b, status(b, STATUS_BUDGET, kind));
      b->cold = 0;
      if(kind == ARM_EXIT_CALL)
        return_push(b);
    }
    else
      emit(b, MOVE(REG_PREFETCH, ZERO));
  }
  if(b->mode == MODE_LINKED)
    link_jump(b, key, 0);
  else
  {
    load_constant(b, A1, key);
    leave(b, status(b, STATUS_EXIT, kind));
  }
  b->dead = 1;
}

static void exit_indirect(Builder *b, const ArmIrOp *op, unsigned index)
{
  unsigned kind = op->d;
  unsigned target;
  unsigned miss;
  unsigned probe;
  uint32_t table;
  int32_t low;

  if(kind == ARM_EXIT_SYSTEM || (op->flags & ARM_IR_RESTORE))
  {
    bail_here(b, index);
    return;
  }
  target = source(b, op->a);
  /* The key: bit 0 picks the state for bx, and the address aligns. */
  if(op->flags & ARM_IR_BX)
  {
    emit(b, SRL(V1, target, 1));
    emit(b, AND(V1, V1, target));
    emit(b, MOVE(V0, target));
    emit(b, INS(V0, V1, 1, 1));
  }
  else if(b->thumb)
    emit(b, ORI(V0, target, 1));
  else
  {
    emit(b, MOVE(V0, target));
    emit(b, INS(V0, ZERO, 0, 2));
  }
  settle(b);
  flush_pending(b);
  count_instructions(b);
  miss = label_new(b);
  if(b->mode != MODE_LINKED)
  {
    b->dead = 1;
    emit(b, MOVE(A1, V0));
    leave(b, status(b, STATUS_MISS, kind));
    return;
  }
  probe = label_new(b);
  if(kind == ARM_EXIT_CALL)
    return_push(b);
  else if(kind == ARM_EXIT_RETURN)
  {
    unsigned ras_miss = label_new(b);

    emit(b, LW(V1, JIT(return_top), GP));
    emit(b, ADDU(A0, V1, GP));
    emit(b, LW(A1, JIT(returns), A0));
    emit(b, ADDIU(V1, V1, -8));
    emit(b, ANDI(V1, V1, 8 * ARM_MIPS_RETURNS - 8));
    branch(b, BNE(A1, V0), ras_miss);
    emit(b, SW(V1, JIT(return_top), GP));
    profile(b, 0);
    emit(b, LW(A1, JIT(returns) + 4, A0));
    emit(b, JR(A1));
    emit(b, NOP);
    label_bind(b, ras_miss);
    profile(b, 1);
  }
  label_bind(b, probe);
  table = target_of(b->jit, b->jit->dispatch);
  low = (int32_t)(int16_t)(table & 0xFFFF);
  emit(b, SLL(V1, V0, 2));
  emit(b, ANDI(V1, V1, (ARM_MIPS_DISPATCH - 1) << 3));
  emit(b, LUI(A0, (table - (uint32_t)low) >> 16));
  emit(b, ADDU(V1, V1, A0));
  if(low > 32763)
  {
    emit(b, ADDIU(V1, V1, low));
    low = 0;
  }
  emit(b, LW(A0, low, V1));
  branch(b, BNE(A0, V0), miss);
  emit(b, LW(A1, low + 4, V1));
  profile(b, 2);
  emit(b, JR(A1));
  emit(b, NOP);
  b->cold = 1;
  label_bind(b, miss);
  profile(b, 3);
  emit(b, MOVE(A1, V0));
  leave(b, status(b, STATUS_MISS, kind));
  b->cold = 0;
  b->dead = 1;
}

/* ---- ALU ---------------------------------------------------------------- */

/* The register holding operand b of op, materialising an immediate. */
static unsigned operand(Builder *b, const ArmIrOp *op, unsigned rb)
{
  unsigned r;

  if(!(op->flags & ARM_IR_IMM))
    return rb;
  if(op->b == 0)
    return ZERO;
  r = scratch(b);
  load_constant(b, r, op->b);
  return r;
}

/* V for x + y = r, or x - y = r with subtract. */
static void overflow(Builder *b, unsigned x, unsigned y, unsigned r,
 int subtract)
{
  unsigned s1 = scratch(b);
  unsigned s2 = scratch(b);

  emit(b, XOR(s1, x, subtract ? y : r));
  emit(b, XOR(s2, subtract ? x : y, r));
  emit(b, AND(s1, s1, s2));
  emit(b, SW(s1, CORE(flag_v), GP));
  scratch_release(b, s1);
  scratch_release(b, s2);
}

/* C = carry ? x >= y : x > y, for SBC and RSC (x - y - !carry). */
static void borrow(Builder *b, unsigned x, unsigned y)
{
  unsigned s1 = scratch(b);
  unsigned s2 = scratch(b);

  emit(b, SLTU(s1, x, y));
  emit(b, XORI(s1, s1, 1));
  emit(b, SLTU(s2, y, x));
  emit(b, MOVZ(s1, s2, REG_C));
  emit(b, MOVE(REG_C, s1));
  scratch_release(b, s1);
  scratch_release(b, s2);
}

static void shift_immediate(Builder *b, unsigned code, int set_c,
 unsigned out, unsigned ra, unsigned amount)
{
  switch(code)
  {
    case ARM_IR_LSL:
      if(set_c)
        emit(b, EXT(REG_C, ra, 32 - amount, 1));
      emit(b, SLL(out, ra, amount));
      break;
    case ARM_IR_LSR:
    case ARM_IR_ASR:
      if(amount == 32)
      {
        if(set_c)
          emit(b, SRL(REG_C, ra, 31));
        emit(b, code == ARM_IR_LSR ? MOVE(out, ZERO) : SRA(out, ra, 31));
        break;
      }
      if(set_c)
        emit(b, EXT(REG_C, ra, amount - 1, 1));
      emit(b, code == ARM_IR_LSR ? SRL(out, ra, amount) :
       SRA(out, ra, amount));
      break;
    default:
      if(set_c)
        emit(b, EXT(REG_C, ra, amount - 1, 1));
      emit(b, ROTR(out, ra, amount));
      break;
  }
}

/* Shifts by the bottom byte of a register, with the ARM rules: 0 leaves
   the value and C, 32 and more shift everything out. */
static void shift_register(Builder *b, unsigned code, int set_c,
 unsigned out, unsigned ra, unsigned rb)
{
  unsigned n = scratch(b);
  unsigned s1 = scratch(b);
  unsigned s2 = scratch(b);

  emit(b, ANDI(n, rb, 0xFF));
  switch(code)
  {
    case ARM_IR_LSL:
    case ARM_IR_LSR:
      if(set_c)
      {
        /* The last bit out: bit 32 - n of a for LSL, n - 1 for LSR, none
           past 32. */
        emit(b, ADDIU(s1, n, -1));
        if(code == ARM_IR_LSL)
        {
          emit(b, SLLV(s1, ra, s1));
          emit(b, SRL(s1, s1, 31));
        }
        else
        {
          emit(b, SRLV(s1, ra, s1));
          emit(b, ANDI(s1, s1, 1));
        }
        emit(b, SLTIU(s2, n, 33));
        emit(b, MOVZ(s1, ZERO, s2));
        emit(b, MOVN(REG_C, s1, n));
      }
      emit(b, code == ARM_IR_LSL ? SLLV(out, ra, n) : SRLV(out, ra, n));
      emit(b, SLTIU(s1, n, 32));
      emit(b, MOVZ(out, ZERO, s1));
      break;
    case ARM_IR_ASR:
      if(set_c)
      {
        emit(b, ORI(s1, ZERO, 32));
        emit(b, MIN(s1, n, s1));
        emit(b, ADDIU(s1, s1, -1));
        emit(b, SRLV(s1, ra, s1));
        emit(b, ANDI(s1, s1, 1));
        emit(b, MOVN(REG_C, s1, n));
      }
      emit(b, ORI(s1, ZERO, 31));
      emit(b, MIN(s1, n, s1));
      emit(b, SRAV(out, ra, s1));
      break;
    default:
      emit(b, ROTRV(out, ra, n));
      if(set_c)
      {
        emit(b, SRL(s1, out, 31));
        emit(b, MOVN(REG_C, s1, n));
      }
      break;
  }
}

static void alu(Builder *b, unsigned i)
{
  const ArmIrOp *op = &b->ops[i];
  unsigned code = op->code;
  unsigned flags = op->flags;
  int imm = (flags & ARM_IR_IMM) != 0;
  int set_nz = (flags & ARM_IR_SET_NZ) != 0;
  int set_c = (flags & ARM_IR_SET_C) != 0;
  int set_v = (flags & ARM_IR_SET_V) != 0;
  int shift = code >= ARM_IR_LSL && code <= ARM_IR_ROR;
  uint32_t k = op->b;
  unsigned ra = 0;
  unsigned rb = 0;
  unsigned dest;
  unsigned out;
  unsigned y;
  unsigned s;

  if(code != ARM_IR_MOV && code != ARM_IR_MVN)
    ra = source(b, op->a);
  if(!imm)
    rb = source(b, k & 0xFF);
  if(op->d == ARM_IR_NONE)
    dest = set_nz ? REG_Z : scratch(b);
  else
    dest = destination(b, op->d, set_nz);
  /* Flags need the operands after the result is written. */
  out = dest;
  if(!shift && (set_c || set_v) && (dest == ra || dest == rb))
    out = scratch(b);

  switch(code)
  {
    case ARM_IR_MOV:
      if(imm)
        load_constant(b, out, k);
      else if(out != rb)
        emit(b, MOVE(out, rb));
      break;
    case ARM_IR_MVN:
      if(imm)
        load_constant(b, out, ~k);
      else
        emit(b, NOR(out, rb, ZERO));
      break;
    case ARM_IR_AND:
      if(imm && k <= 0xFFFF)
        emit(b, ANDI(out, ra, k));
      else
        emit(b, AND(out, ra, operand(b, op, rb)));
      break;
    case ARM_IR_EOR:
      if(imm && k <= 0xFFFF)
        emit(b, XORI(out, ra, k));
      else
        emit(b, XOR(out, ra, operand(b, op, rb)));
      break;
    case ARM_IR_ORR:
      if(imm && k <= 0xFFFF)
        emit(b, ORI(out, ra, k));
      else
        emit(b, OR(out, ra, operand(b, op, rb)));
      break;
    case ARM_IR_BIC:
      if(imm && ~k <= 0xFFFF)
        emit(b, ANDI(out, ra, ~k));
      else if(imm)
      {
        s = scratch(b);
        load_constant(b, s, ~k);
        emit(b, AND(out, ra, s));
      }
      else
      {
        s = scratch(b);
        emit(b, NOR(s, rb, ZERO));
        emit(b, AND(out, ra, s));
      }
      break;
    case ARM_IR_ADD:
      if(imm && fits16(k) && !set_v)
        emit(b, ADDIU(out, ra, k));
      else
      {
        y = operand(b, op, rb);
        emit(b, ADDU(out, ra, y));
        if(set_v)
          overflow(b, ra, y, out, 0);
      }
      if(set_c)
        emit(b, SLTU(REG_C, out, ra));
      break;
    case ARM_IR_ADC:
      y = operand(b, op, rb);
      if(!set_c)
      {
        emit(b, ADDU(out, ra, y));
        emit(b, ADDU(out, out, REG_C));
      }
      else
      {
        unsigned sum = scratch(b);
        unsigned carry = scratch(b);

        emit(b, ADDU(sum, ra, y));
        emit(b, SLTU(carry, sum, ra));
        emit(b, ADDU(out, sum, REG_C));
        emit(b, SLTU(sum, out, REG_C));
        emit(b, OR(REG_C, carry, sum));
        scratch_release(b, sum);
        scratch_release(b, carry);
      }
      if(set_v)
        overflow(b, ra, y, out, 0);
      break;
    case ARM_IR_SUB:
      if(imm && fits16(0u - k) && fits16(k) && !set_v)
      {
        emit(b, ADDIU(out, ra, 0u - k));
        if(set_c)
          emit(b, SLTIU(REG_C, ra, k));
      }
      else
      {
        y = operand(b, op, rb);
        emit(b, SUBU(out, ra, y));
        if(set_v)
          overflow(b, ra, y, out, 1);
        if(set_c)
          emit(b, SLTU(REG_C, ra, y));
      }
      if(set_c)
        emit(b, XORI(REG_C, REG_C, 1));
      break;
    case ARM_IR_RSB:
      y = operand(b, op, rb);
      emit(b, SUBU(out, y, ra));
      if(set_v)
        overflow(b, y, ra, out, 1);
      if(set_c)
      {
        emit(b, SLTU(REG_C, y, ra));
        emit(b, XORI(REG_C, REG_C, 1));
      }
      break;
    case ARM_IR_SBC:
    case ARM_IR_RSC:
    {
      unsigned x;

      y = operand(b, op, rb);
      x = ra;
      if(code == ARM_IR_RSC)
      {
        x = y;
        y = ra;
      }
      s = scratch(b);
      emit(b, SUBU(s, x, y));
      emit(b, ADDU(s, s, REG_C));
      emit(b, ADDIU(out, s, -1));
      scratch_release(b, s);
      if(set_v)
        overflow(b, x, y, out, 1);
      if(set_c)
        borrow(b, x, y);
      break;
    }
    case ARM_IR_LSL:
    case ARM_IR_LSR:
    case ARM_IR_ASR:
    case ARM_IR_ROR:
      if(imm)
        shift_immediate(b, code, set_c, out, ra, k);
      else
        shift_register(b, code, set_c, out, ra, rb);
      break;
    case ARM_IR_RRX:
      s = scratch(b);
      emit(b, SRL(s, ra, 1));
      emit(b, INS(s, REG_C, 31, 1));
      if(set_c)
        emit(b, ANDI(REG_C, ra, 1));
      emit(b, MOVE(out, s));
      break;
    case ARM_IR_MUL:
      y = operand(b, op, rb);
      /* With the high word of the same product next (MULL), one
         multiply serves both. */
      if(i + 1 < b->count && (b->ops[i + 1].code == ARM_IR_UMULH ||
         b->ops[i + 1].code == ARM_IR_SMULH) && b->ops[i + 1].a == op->a &&
         b->ops[i + 1].b == op->b && !(flags & ARM_IR_IMM) &&
         op->d != op->a && op->d != (op->b & 0xFF))
      {
        emit(b, b->ops[i + 1].code == ARM_IR_UMULH ? MULTU(ra, y) :
         MULT(ra, y));
        emit(b, MFLO(out));
        break;
      }
      emit(b, MULT(ra, y));
      emit(b, MFLO(out));
      break;
    case ARM_IR_UMULH:
    case ARM_IR_SMULH:
      y = operand(b, op, rb);
      if(!(i > 0 && b->ops[i - 1].code == ARM_IR_MUL &&
         b->ops[i - 1].a == op->a && b->ops[i - 1].b == op->b &&
         b->ops[i - 1].d != op->a && b->ops[i - 1].d != (op->b & 0xFF)))
        emit(b, code == ARM_IR_UMULH ? MULTU(ra, y) : MULT(ra, y));
      emit(b, MFHI(out));
      break;
    default:
      emit(b, SLTU(out, ra, operand(b, op, rb)));
      break;
  }

  if(out != dest)
    emit(b, MOVE(dest, out));
  if(set_nz)
  {
    if(op->d >= ARM_IR_FIRST_TEMP && op->d != ARM_IR_NONE)
    {
      emit(b, MOVE(REG_Z, dest));
      dest = REG_Z;
    }
    b->n_reg = b->z_reg = dest;
  }
  if(op->d != ARM_IR_NONE)
    commit(b, op->d, dest);
}

/* ---- Timing ops --------------------------------------------------------- */

/* The opcode fetches through the prefetch buffer since the last op that
   used it. A buffered halfword costs a cycle (its fetch filling the buffer
   by one, so the level drops by access - 1); otherwise the CPU waits for
   the rest of it and the buffer is empty. k fetches in a row therefore
   cost max(k, k * access - level) and leave max(0, level - k * (access -
   1)): with t = level - k * (access - 1), k cycles plus -min(t, 0), and the
   level max(t, 0). One sequence covers a run of instructions that touch
   neither memory nor the buffer. */
static void emit_fetches(Builder *b)
{
  emit(b, ADDIU(V0, REG_PREFETCH, -(int)(b->fetches * (b->access - 1))));
  emit(b, MIN(V1, V0, ZERO));
  emit(b, MAX(REG_PREFETCH, V0, ZERO));
  emit(b, SUBU(REG_CYCLES, REG_CYCLES, V1));
  b->fetches = 0;
}

/* Fills add up the same way, capped once at the end, since
   min(min(level + x, full) + y, full) = min(level + x + y, full). A fill
   has added to the register already (the part known only at run time) or
   waits in fill; capping completes them. At most one kind waits. */
static void emit_fill(Builder *b)
{
  if(b->fill)
    emit(b, ADDIU(REG_PREFETCH, REG_PREFETCH, b->fill));
  emit(b, ORI(V1, ZERO, b->full));
  emit(b, MIN(REG_PREFETCH, REG_PREFETCH, V1));
  b->fill = 0;
  b->filled = 0;
}

/* Brings the prefetch register up to date, for an op that uses it. */
static void prefetch_settle(Builder *b)
{
  if(b->level_known)
    load_constant(b, REG_PREFETCH, b->level);
  b->level_known = 0;
  if(b->fetches)
    emit_fetches(b);
  if(b->filled)
    emit_fill(b);
}

/* The level is value from here on (an empty buffer, after a GamePak data
   access or a taken branch). Waiting fetches still cost their cycles. */
static void prefetch_known(Builder *b, unsigned value)
{
  if(b->fetches)
    emit_fetches(b);
  b->fill = 0;
  b->filled = 0;
  b->level_known = 1;
  b->level = value;
}

/* Fetches of an instruction: with the level known, their cost and the new
   level are known too. */
static void prefetch_fetches(Builder *b, unsigned halfwords)
{
  if(b->level_known)
  {
    int32_t wait = (int32_t)(halfwords * b->access) - (int32_t)b->level;
    int32_t left = (int32_t)b->level - (int32_t)(halfwords *
     (b->access - 1));

    b->pending += wait > (int32_t)halfwords ? wait : (int32_t)halfwords;
    b->level = left > 0 ? (unsigned)left : 0;
    return;
  }
  if(b->filled)
    emit_fill(b);
  b->fetches += halfwords;
  b->pending += (int32_t)halfwords;
}

/* The prefetch buffer fills by cycles (a register, added now, and a
   constant, added later). */
static void prefetch_fill(Builder *b, unsigned reg, uint32_t cycles)
{
  if(b->level_known && !reg)
  {
    b->level = b->level + cycles < b->full ? b->level + cycles : b->full;
    return;
  }
  if(b->level_known)
    prefetch_settle(b);
  if(b->fetches)
    emit_fetches(b);
  if(reg)
    emit(b, ADDU(REG_PREFETCH, REG_PREFETCH, reg));
  b->fill += (int32_t)cycles;
  b->filled = 1;
}

/* Multiplier early termination: 4 - min(leading zero (or, signed, sign)
   bytes, 3) cycles. */
static void multiply_cycles(Builder *b, const ArmIrOp *op)
{
  unsigned ra = source(b, op->a);
  unsigned s = scratch(b);
  unsigned t = scratch(b);

  if(op->b == 0)
  {
    emit(b, SRA(s, ra, 31));
    emit(b, XOR(s, s, ra));
    emit(b, CLZ(s, s));
  }
  else
    emit(b, CLZ(s, ra));
  emit(b, SRL(s, s, 3));
  emit(b, ORI(t, ZERO, 3));
  emit(b, MIN(s, s, t));
  emit(b, SUBU(REG_CYCLES, REG_CYCLES, s));
  b->pending += 4;
  if(op->flags & ARM_IR_CARTRIDGE_CODE)
  {
    /* The buffer fills by the 4 - m cycles. */
    prefetch_settle(b);
    emit(b, SUBU(REG_PREFETCH, REG_PREFETCH, s));
    b->fill += 4;
    b->filled = 1;
  }
}

/* ---- Memory ------------------------------------------------------------- */

/* log2(mask + 1), or 0 when mask is not one less than a power of two. */
static unsigned mask_bits(uint32_t mask)
{
  unsigned bits = 0;

  while(bits < 32 && (mask >> bits) & 1)
    bits++;
  return bits < 32 && (mask >> bits) == 0 ? bits : 0;
}

/* The region an address in it is accessed directly, or 16 if none. */
static unsigned direct_region(const Builder *b, uint32_t address, int store)
{
  unsigned region = address >> 24;
  const ArmRegion *map;

  if(region < 2 || region > 13)
    return 16;
  map = &b->core->map[region];
  if(!(store ? map->write != NULL : map->read != NULL) ||
     mask_bits(map->mask) < 10)
    return 16;
  return region;
}

/* Cycles, prefetch buffer and refetch of ops [i, i + n) at region, as
   count_access does them. */
static void access_effects(Builder *b, unsigned i, unsigned n,
 unsigned region, int exact)
{
  unsigned fill = 0;
  int flush = 0;
  unsigned j;

  for(j = i; j < i + n; j++)
  {
    const ArmIrOp *op = &b->ops[j];
    unsigned size = access_size(op->code);

    if(!(op->flags & ARM_IR_FOLDED))
    {
      unsigned cycles = b->core->wait[region][(size == 4 ? 2 : 0) +
       ((op->flags & ARM_IR_SEQ) ? 1 : 0)];

      b->pending += (int32_t)cycles;
      if((op->flags & ARM_IR_CARTRIDGE_CODE) && region < 8)
        fill += cycles;
    }
    if((op->flags & ARM_IR_CARTRIDGE_CODE) && region >= 8)
    {
      flush = 1;
      if(!(op->flags & (ARM_IR_FOLDED | ARM_IR_SEQ)))
        b->pending += (int32_t)arm_cartridge_refetch(b->core, b->pc);
    }
  }
  if(fill)
    prefetch_fill(b, 0, fill);
  /* With the address known the slow path empties the buffer as well; a
     guessed one may be elsewhere. */
  if(flush && exact)
    prefetch_known(b, 0);
  else if(flush)
    emit(b, MOVE(REG_PREFETCH, ZERO));
}

/* The target address of a direct region's memory. */
static uint32_t region_target(const Builder *b, unsigned region)
{
  const ArmRegion *map = &b->core->map[region];

  return target_of(b->jit, map->write ? map->write : map->read);
}

/* Host address of the guest address in reg in a direct region, into p,
   using s; with lui_done p already holds the top half of an aligned
   buffer's address. */
static void host_address(Builder *b, unsigned p, unsigned reg,
 unsigned region, int lui_done, unsigned s)
{
  uint32_t mask = b->core->map[region].mask;
  uint32_t base = region_target(b, region);
  unsigned bits = mask_bits(mask);

  if(!(base & mask))
  {
    /* An aligned buffer: the offset goes straight into the low bits. */
    if(!lui_done)
      emit(b, LUI(p, base >> 16));
    if(base & 0xFFFF)
      emit(b, ORI(p, p, base & 0xFFFF));
    emit(b, INS(p, reg, 0, bits));
    return;
  }
  emit(b, EXT(p, reg, 0, bits));
  load_constant(b, s, base);
  emit(b, ADDU(p, p, s));
}

/* Branches to slow if the 32 bytes of work RAM holding the address in reg
   (or the constant address, with reg ZERO) may hold translated code; s is
   a scratch register. */
static void code_check(Builder *b, unsigned reg, uint32_t address,
 unsigned region, unsigned slow, unsigned s)
{
  unsigned bits = region == 2 ? 18 : 15;
  int base = JIT(code_map) + (region == 2 ? 0 : 0x40000 /
   (int)ARM_MIPS_CODE_GRANULE);

  if(reg == ZERO)
    emit(b, LB(s, base + (int)((address & ((1u << bits) - 1)) /
     ARM_MIPS_CODE_GRANULE), GP));
  else
  {
    emit(b, EXT(s, reg, 5, bits - 5));
    emit(b, ADDU(s, s, GP));
    emit(b, LB(s, base, s));
  }
  branch(b, BNE(s, ZERO), slow);
  emit(b, NOP);
}

/* The same for every 32 bytes that [address, address + span] touches:
   points 32 bytes apart from its start, and its end. */
static void code_checks(Builder *b, unsigned reg, uint32_t address,
 uint32_t span, unsigned region, unsigned slow, unsigned s)
{
  uint32_t offset;

  if(reg == ZERO)
  {
    uint32_t granule;

    for(granule = address / ARM_MIPS_CODE_GRANULE;
        granule <= (address + span) / ARM_MIPS_CODE_GRANULE; granule++)
      code_check(b, ZERO, granule * ARM_MIPS_CODE_GRANULE, region, slow, s);
    return;
  }
  code_check(b, reg, 0, region, slow, s);
  for(offset = ARM_MIPS_CODE_GRANULE; offset < span;
      offset += ARM_MIPS_CODE_GRANULE)
  {
    emit(b, ADDIU(s, reg, offset));
    code_check(b, s, 0, region, slow, s);
  }
  if(span)
  {
    emit(b, ADDIU(s, reg, span));
    code_check(b, s, 0, region, slow, s);
  }
}

static uint32_t access_word(unsigned code, unsigned rt, int offset, unsigned p)
{
  switch(code)
  {
    case ARM_IR_LD8: return LBU(rt, offset, p);
    case ARM_IR_LD8S: return LB(rt, offset, p);
    case ARM_IR_LD16: return LHU(rt, offset, p);
    case ARM_IR_LD16S: return LH(rt, offset, p);
    case ARM_IR_LD32: return LW(rt, offset, p);
    case ARM_IR_ST8: return SB(rt, offset, p);
    case ARM_IR_ST16: return SH(rt, offset, p);
    default: return SW(rt, offset, p);
  }
}

/* The slow path of ops [i, i + n): each through arm_mips_access, then
   back to done. A pending exit request hands the rest of the block to the
   interpreter. */
static void slow_path(Builder *b, unsigned i, unsigned n, unsigned slow,
 unsigned done, int32_t pending_before, int32_t pending_after,
 const unsigned *dest, unsigned in_memory, int level)
{
  unsigned base_slot = b->ops[i].a;
  uint32_t address = b->value[base_slot] + b->ops[i].b;
  int io = b->known[base_slot] && (address >> 24 < 2 || address >> 24 > 3);
  unsigned saved = 0;
  unsigned tail = label_new(b);
  unsigned r;
  unsigned j;
  int was_cold = b->cold;

  b->cold = 1;
  label_bind(b, slow);
  for(r = A0; r <= A3; r++)
    if(!(b->temps_free & (1u << r)))
    {
      saved |= 1u << r;
      emit(b, SW(r, SPILL(r), GP));
    }
  add_cycles(b, pending_before);
  /* A level known to the compiler (level >= 0) goes into its register. */
  if(level >= 0)
    load_constant(b, REG_PREFETCH, (uint32_t)level);
  for(j = i; j < i + n; j++)
  {
    const ArmIrOp *op = &b->ops[j];
    unsigned base = base_slot < 15 ? guest_register[base_slot] :
     b->temp_reg[base_slot];

    if(base && (saved & (1u << base)))
      emit(b, LW(A1, SPILL(base), GP));
    else if(base)
      emit(b, MOVE(A1, base));
    else
      emit(b, LW(A1, SLOT(base_slot), GP));
    if(fits16(op->b))
      emit(b, ADDIU(A1, A1, op->b));
    else
    {
      load_constant(b, V0, op->b);
      emit(b, ADDU(A1, A1, V0));
    }
    if(is_store(op->code))
    {
      unsigned value = op->d < 15 ? guest_register[op->d] :
       b->temp_reg[op->d];

      if(value && (saved & (1u << value)))
        emit(b, LW(A2, SPILL(value), GP));
      else if(value)
        emit(b, MOVE(A2, value));
      else
        emit(b, LW(A2, SLOT(op->d), GP));
    }
    emit(b, ORI(A3, ZERO, op->code | (unsigned)op->flags << 8));
    load_constant(b, V1, b->pc | (uint32_t)b->thumb);
    /* A known address outside work RAM (I/O) goes straight to C. */
    emit(b, JAL(io ? b->jit->routine_call :
     b->jit->routine_fast[op->code - ARM_IR_LD8]));
    emit(b, NOP);
    if(is_load(op->code))
    {
      if(in_memory & (1u << (j - i)))
        emit(b, SW(V0, SLOT(op->d), GP));
      else if(saved & (1u << dest[j - i]))
        emit(b, SW(V0, SPILL(dest[j - i]), GP));
      else
        emit(b, MOVE(dest[j - i], V0));
    }
    branch(b, BNE(V1, ZERO), tail);
    emit(b, ORI(A1, ZERO, j + 1));
  }
  for(r = A0; r <= A3; r++)
    if(saved & (1u << r))
      emit(b, LW(r, SPILL(r), GP));
  add_cycles(b, -pending_after);
  /* The fast path's fill still waits for its cap: take it out here too. */
  if(b->fill)
    emit(b, ADDIU(REG_PREFETCH, REG_PREFETCH, -b->fill));
  branch(b, BEQ(ZERO, ZERO), done);
  emit(b, NOP);
  label_bind(b, tail);
  bail(b, i + 1, 1, 1, 1);
  b->cold = was_cold;
}

/* A load or store, or the words of an LDM or STM (a run of word accesses
   from one base at consecutive offsets): returns the ops it took. */
static unsigned memory(Builder *b, unsigned i)
{
  const ArmIrOp *first = &b->ops[i];
  unsigned code = first->code;
  int store = is_store(code);
  unsigned size = access_size(code);
  unsigned base_slot = first->a;
  unsigned n = 1;
  unsigned dest[16];
  unsigned in_memory = 0;
  unsigned region;
  uint32_t span;
  unsigned slow = label_new(b);
  unsigned done = label_new(b);
  unsigned s;
  unsigned p = V0;
  int known = b->known[base_slot];
  int exact;
  int level_known;
  unsigned level;
  uint32_t guess;
  int32_t pending_before;
  unsigned j;

  if(code == ARM_IR_LD32 || code == ARM_IR_ST32)
    while(i + n < b->count && n < 16 && b->ops[i + n].code == code &&
          b->ops[i + n].a == base_slot &&
          b->ops[i + n].b == first->b + 4 * n &&
          (b->ops[i + n].flags & ARM_IR_SEQ) &&
          !(is_load(code) && b->ops[i + n - 1].d == base_slot))
      n++;
  span = 4 * (n - 1);

  /* Where the loads go; a load into the register holding N or Z copies
     them first, on both paths. */
  for(j = 0; j < n; j++)
  {
    const ArmIrOp *op = &b->ops[i + j];

    dest[j] = ZERO;
    if(is_load(code))
    {
      dest[j] = destination(b, op->d, 0);
      if(op->d >= ARM_IR_FIRST_TEMP && !b->temp_reg[op->d])
        in_memory |= 1u << j;
    }
  }

  guess = known ? b->value[base_slot] + first->b :
   b->guessed[base_slot] ? b->guess[base_slot] + first->b :
   (base_slot == 13 ? 0x03000000u : 0x02000000u);
  region = direct_region(b, guess, store);
  /* A constant address needs no check but the code map, unless it is
     unaligned or runs off the region (left to the slow path). */
  if(known && region < 16 && ((guess & (size - 1)) ||
     (guess & b->core->map[region].mask) + span >
     b->core->map[region].mask ||
     (region >= 8 && ((guess & 0x1FFFF) + span) >> 17)))
    region = 16;
  exact = known && region < 16;
  /* The slow path calls C, which needs the prefetch level in its register;
     a known level stays known through an access to a known address. */
  level_known = exact && b->level_known;
  level = b->level;
  if(!level_known)
    prefetch_settle(b);
  pending_before = b->pending;
  s = scratch(b);
  if(exact)
  {
    if(store && region <= 3)
      code_checks(b, ZERO, guess, span, region, slow, s);
    p = scratch(b);
    load_constant(b, p, region_target(b, region) +
     (guess & b->core->map[region].mask));
  }
  else if(region < 16)
  {
    const ArmRegion *map = &b->core->map[region];
    uint32_t target = region_target(b, region);
    unsigned bits = mask_bits(map->mask);
    unsigned base = source(b, base_slot);
    unsigned address = base;

    if(first->b)
    {
      address = scratch(b);
      if(fits16(first->b))
        emit(b, ADDIU(address, base, first->b));
      else
      {
        load_constant(b, address, first->b);
        emit(b, ADDU(address, base, address));
      }
    }
    p = scratch(b);
    /* The region (address bits 24-31) and, for halfwords and words, the
       alignment in one test: rotate the region byte down next to the low
       bits. */
    if(size == 1)
      emit(b, SRL(s, address, 24));
    else
      emit(b, ROTR(s, address, 24));
    emit(b, XORI(s, s, region));
    if(size > 1)
      emit(b, ANDI(s, s, size == 2 ? 0x1FF : 0x3FF));
    branch(b, BNE(s, ZERO), slow);
    if(!(target & map->mask))
      emit(b, LUI(p, target >> 16));
    else
      emit(b, NOP);
    if(span)
    {
      /* The words stay in the region, and a ROM run within 128 KB (where
         every one is sequential). */
      unsigned limit = region >= 8 ? 17 : bits;

      emit(b, EXT(s, address, 0, limit));
      emit(b, ADDIU(s, s, span));
      emit(b, SRL(s, s, limit));
      branch(b, BNE(s, ZERO), slow);
      emit(b, NOP);
    }
    if(store && region <= 3)
      code_checks(b, address, 0, span, region, slow, s);
    host_address(b, p, address, region, !(target & map->mask), s);
  }

  if(region < 16)
  {
    for(j = 0; j < n; j++)
    {
      const ArmIrOp *op = &b->ops[i + j];
      unsigned rt = store ? source(b, op->d) : dest[j];

      emit(b, access_word(op->code, rt, (int)(4 * j), p));
      if(in_memory & (1u << j))
        emit(b, SW(rt, SLOT(op->d), GP));
    }
    b->scratch_used = 0;
    access_effects(b, i, n, region, exact);
  }
  else
  {
    branch(b, BEQ(ZERO, ZERO), slow);
    emit(b, NOP);
  }
  label_bind(b, done);
  slow_path(b, i, n, slow, done, pending_before, b->pending, dest,
   in_memory, level_known ? (int)level : -1);
  return n;
}

/* ---- Blocks ------------------------------------------------------------- */

/* Whether the conditional section opened by the SKIP at op i may change
   N and Z, or the registers they are kept in. */
static int section_sets_nz(const Builder *b, unsigned i)
{
  unsigned end = i + 1 + (b->ops[i].b & 0xFFFF);
  unsigned j;

  for(j = i + 1; j < end && j < b->count; j++)
  {
    const ArmIrOp *op = &b->ops[j];

    if(op->code == ARM_IR_SETNZ64 || (is_alu(op->code) &&
       (op->flags & ARM_IR_SET_NZ)))
      return 1;
    if(op->d < 15 && (is_alu(op->code) || is_load(op->code)) &&
       (guest_register[op->d] == b->n_reg ||
       guest_register[op->d] == b->z_reg))
      return 1;
  }
  return 0;
}

/* Ends a conditional section: both ways in leave N and Z where the
   section found them, settled if the section could change them. */
static void section_close(Builder *b)
{
  if(!b->dead)
  {
    if(b->section_settled)
      settle(b);
    prefetch_settle(b);
    add_cycles(b, b->pending - b->section_pending);
  }
  /* The prefetch register was up to date when the section opened. */
  b->level_known = 0;
  b->fetches = 0;
  b->fill = 0;
  b->filled = 0;
  b->n_reg = b->section_n;
  b->z_reg = b->section_z;
  b->pending = b->section_pending;
  label_bind(b, b->section_label);
  b->section_end = 0;
  b->dead = 0;
}

static void instruction(Builder *b, unsigned i)
{
  const ArmIrOp *op = &b->ops[i];
  unsigned slot;

  for(slot = ARM_IR_FIRST_TEMP; slot < ARM_IR_SLOTS; slot++)
  {
    b->temp_reg[slot] = 0;
    b->last_use[slot] = 0;
    b->known[slot] = 0;
    b->guessed[slot] = 0;
  }
  b->temps_free = 0xF0;
  instruction_uses(b, i);
  b->pending += op->a | op->d << 8;
  b->pc = op->b;
  b->insns++;
  /* A fetch costs at least its cycle; prefetch_settle adds any wait. */
  if(op->flags & (ARM_IR_FETCH16 | ARM_IR_FETCH32))
    prefetch_fetches(b, (op->flags & ARM_IR_FETCH32) ? 2 : 1);
}

/* Whether op leaves the prefetch buffer alone, so fetches pending before
   it can wait. */
static int prefetch_untouched(const ArmIrOp *op)
{
  switch(op->code)
  {
    case ARM_IR_NOP: case ARM_IR_INSN: case ARM_IR_CYCLES: case ARM_IR_SETC:
    case ARM_IR_SETNZ64:
      return 1;
    case ARM_IR_MULCYC:
      return !(op->flags & ARM_IR_CARTRIDGE_CODE);
    /* These settle what they need themselves. */
    case ARM_IR_IDLE: case ARM_IR_FLUSH:
      return 1;
    default:
      return is_alu(op->code) || is_load(op->code) || is_store(op->code);
  }
}

static void compile_ops(Builder *b)
{
  unsigned i;

  for(i = 0; i < b->count && !b->failed; i++)
  {
    const ArmIrOp *op = &b->ops[i];
    unsigned n = 1;
    unsigned j;

    if(b->section_end && i == b->section_end)
      section_close(b);
    if(b->dead)
    {
      if(!b->section_end)
        break;
      continue;
    }
    b->scratch_used = 0;
    if((op->code == ARM_IR_EXIT || op->code == ARM_IR_EXIT_REG) &&
       op->d != ARM_EXIT_NEXT)
      prefetch_known(b, 0);
    else if(!prefetch_untouched(op))
      prefetch_settle(b);
    switch(op->code)
    {
      case ARM_IR_NOP:
        break;
      case ARM_IR_INSN:
        instruction(b, i);
        break;
      case ARM_IR_SKIP:
        /* N and Z go to their registers first if the section may change
           them, so both ways agree where it ends. */
        b->section_settled = section_sets_nz(b, i);
        if(b->section_settled)
          settle(b);
        b->section_n = b->n_reg;
        b->section_z = b->z_reg;
        b->section_label = label_new(b);
        b->section_end = i + 1 + (op->b & 0xFFFF);
        b->section_pending = b->pending;
        branch_unless(b, op->a, b->section_label);
        b->pending += (int32_t)(op->b >> 16);
        break;
      case ARM_IR_CYCLES:
        b->pending += (int32_t)op->b;
        break;
      case ARM_IR_SETC:
        emit(b, op->b ? ORI(REG_C, ZERO, 1) : MOVE(REG_C, ZERO));
        break;
      case ARM_IR_SETNZ64:
      {
        unsigned high = source(b, op->a);
        unsigned low = source(b, op->b & 0xFF);

        emit(b, MOVE(REG_N, high));
        emit(b, OR(REG_Z, high, low));
        b->n_reg = REG_N;
        b->z_reg = REG_Z;
        break;
      }
      case ARM_IR_MULCYC:
        multiply_cycles(b, op);
        break;
      case ARM_IR_IDLE:
        prefetch_fill(b, 0, op->b);
        break;
      case ARM_IR_FLUSH:
        prefetch_known(b, 0);
        break;
      case ARM_IR_LD8: case ARM_IR_LD8S: case ARM_IR_LD16:
      case ARM_IR_LD16S: case ARM_IR_LD32: case ARM_IR_ST8:
      case ARM_IR_ST16: case ARM_IR_ST32:
        n = memory(b, i);
        break;
      case ARM_IR_EXIT:
        exit_direct(b, op, i);
        break;
      case ARM_IR_EXIT_REG:
        exit_indirect(b, op, i);
        break;
      case ARM_IR_MRS: case ARM_IR_MSR: case ARM_IR_USER_GET:
      case ARM_IR_USER_SET: case ARM_IR_SWI: case ARM_IR_UNDEF:
        bail_here(b, i);
        break;
      default:
        alu(b, i);
        break;
    }
    for(j = i; j < i + n; j++)
    {
      release(b, j);
      follow(b, &b->ops[j]);
    }
    i += n - 1;
  }
  if(!b->dead && !b->failed)
    b->failed = 1;
}

/* Lays the code out at word `at` of the buffer: hot code, then cold. */
static void place(ArmMips *jit, Builder *b, uint32_t at)
{
  uint32_t hot = code_address(jit, at);
  uint32_t cold = hot + 4 * b->hot_count;
  unsigned i;

  for(i = 0; i < b->fixup_count; i++)
  {
    const Fixup *f = &b->fixups[i];
    uint32_t label = b->labels[f->label];
    uint32_t address = (label & COLD) ? cold + 4 * (label & ~COLD) :
     hot + 4 * label;
    uint32_t *word = (f->where & COLD) ? &b->cold_code[f->where & ~COLD] :
     &b->hot[f->where];
    uint32_t where = (f->where & COLD) ? cold + 4 * (f->where & ~COLD) :
     hot + 4 * f->where;

    switch(f->kind)
    {
      case FIX_BRANCH:
        *word |= ((address - where - 4) >> 2) & 0xFFFF;
        break;
      case FIX_JUMP:
        *word |= (address >> 2) & 0x3FFFFFF;
        break;
      case FIX_HI_LO:
        word[0] |= address >> 16;
        word[1] |= address & 0xFFFF;
        break;
      default:
        *word = address;
        break;
    }
  }
  memcpy(jit->buffer + at, b->hot, 4 * b->hot_count);
  memcpy(jit->buffer + at + b->hot_count, b->cold_code, 4 * b->cold_count);
  sync_code(jit, jit->buffer + at, b->hot_count + b->cold_count);
}

/* Compiles ops into the buffer at word `at`, up to word `end`; returns
   the address of the code to enter (the main entry), or 0 if the block
   is too large or does not fit. */
static uint32_t compile(ArmMips *jit, const ArmIrOp *ops, unsigned count,
 uint32_t key, uint32_t op_base, unsigned mode, uint32_t at, uint32_t end)
{
  ArmCore *core = &jit->core;
  Builder *b = &builder;
  uint32_t pc = key & ~1u;
  unsigned code_region = (pc >> 28) ? 1 : pc >> 24;
  unsigned r;

  b->jit = jit;
  b->core = core;
  b->ops = ops;
  b->count = count;
  b->op_base = op_base;
  b->mode = mode;
  b->key = key;
  b->thumb = (int)(key & 1);
  b->width = b->thumb ? 2 : 4;
  b->access = core->wait[code_region][1];
  b->full = 8 * b->access;
  b->pending = 0;
  b->insns = 0;
  b->fetches = 0;
  b->fill = 0;
  b->filled = 0;
  b->level_known = 0;
  b->pc = pc;
  b->dead = 0;
  b->failed = 0;
  b->n_reg = REG_N;
  b->z_reg = REG_Z;
  b->section_end = 0;
  b->cold = 0;
  b->hot_count = 0;
  b->cold_count = 0;
  b->label_count = 0;
  b->fixup_count = 0;
  b->temps_free = 0xF0;
  for(r = 0; r < 15; r++)
  {
    b->known[r] = 0;
    b->guessed[r] = 1;
    b->guess[r] = core->v[r];
  }
  b->known[15] = b->guessed[15] = 0;

  if(mode == MODE_LINKED)
  {
    unsigned budget = label_new(b);
    uint32_t refill = arm_access_cycles(core, pc, b->width, 0) +
     arm_access_cycles(core, pc + b->width, b->width, 1);

    emit(b, key);
    emit(b, ADDIU(REG_CYCLES, REG_CYCLES, refill));
    branch(b, BGEZ(REG_CYCLES), budget);
    emit(b, MOVE(REG_PREFETCH, ZERO));
    b->cold = 1;
    label_bind(b, budget);
    load_constant(b, A1, key);
    leave(b, status(b, STATUS_BUDGET, ARM_EXIT_JUMP));
    b->cold = 0;
  }
  compile_ops(b);
  if(b->failed || at + b->hot_count + b->cold_count > end)
    return 0;
  place(jit, b, at);
  if(mode == MODE_LINKED)
  {
    jit->stats.blocks++;
    jit->stats.guest_insns += b->insns;
    jit->stats.hot_words += b->hot_count;
    jit->stats.cold_words += b->cold_count;
    jit->used = at + b->hot_count + b->cold_count;
    return code_address(jit, at + BLOCK_HEADER);
  }
  return code_address(jit, at);
}

/* ---- Runtime routines --------------------------------------------------- */

/* Work RAM accesses the slow paths can still do without C, when a guess
   was wrong (a routine shared by callers with buffers in different places,
   such as the BIOS's copies): one routine per access code, called as the
   C path is (a1 address, a2 value, a3 info, v1 pc | Thumb) and returning
   the loaded value with no exit request, or going on into that C path.
   They count waitstates and fill the prefetch buffer as count_access does
   (work RAM has no 128 KB rule and no buffer flush). */
static void write_fast_access(ArmMips *jit, Builder *b)
{
  unsigned call = label_new(b);
  unsigned cycles = label_new(b);
  unsigned done = label_new(b);
  int table = JIT(fast);
  unsigned code;

  for(code = ARM_IR_LD8; code <= ARM_IR_ST32; code++)
  {
    unsigned size = access_size(code);
    int store = is_store(code);

    jit->routine_fast[code - ARM_IR_LD8] = code_address(jit, b->hot_count);
    emit(b, SRL(V0, A1, 24));
    emit(b, ADDIU(V0, V0, -2));
    emit(b, SLTIU(A0, V0, 2));
    branch(b, BEQ(A0, ZERO), call);
    emit(b, ANDI(A0, A1, size - 1));
    branch(b, BNE(A0, ZERO), call);
    emit(b, SLL(V0, V0, 4));
    emit(b, ADDU(V0, V0, GP));
    emit(b, LW(A0, table + 4, V0));
    branch(b, BEQ(A0, ZERO), call);
    emit(b, AND(A0, A0, A1));
    if(store)
    {
      /* Not over translated code: a2 makes room for the code map index. */
      emit(b, SW(A2, 0, SP));
      emit(b, LW(A2, table + 8, V0));
      emit(b, ADDU(A2, A2, A0));
      emit(b, SRL(A2, A2, 5));
      emit(b, ADDU(A2, A2, GP));
      emit(b, LB(A2, JIT(code_map), A2));
      branch(b, BNE(A2, ZERO), call);
      emit(b, LW(A2, 0, SP));
    }
    emit(b, LW(V0, table, V0));
    emit(b, ADDU(V0, V0, A0));
    emit(b, access_word(code, store ? A2 : V0, 0, V0));
    branch(b, BEQ(ZERO, ZERO), cycles);
    emit(b, ORI(A0, ZERO, size == 4 ? 2 : 0));
  }
  /* Waitstates: wait[region][a0 + sequential], none when folded; with code
     in the GamePak they fill the buffer, capped at eight halfwords of the
     code's region. */
  label_bind(b, cycles);
  emit(b, ANDI(A2, A3, ARM_IR_SEQ << 8));
  emit(b, SRL(A2, A2, 13));
  emit(b, ADDU(A0, A0, A2));
  emit(b, SRL(A2, A1, 24));
  emit(b, SLL(A2, A2, 2));
  emit(b, ADDU(A2, A2, A0));
  emit(b, ADDU(A2, A2, GP));
  emit(b, LBU(A0, CORE(wait), A2));
  emit(b, ANDI(A2, A3, ARM_IR_FOLDED << 8));
  emit(b, MOVN(A0, ZERO, A2));
  emit(b, ADDU(REG_CYCLES, REG_CYCLES, A0));
  emit(b, ANDI(A2, A3, ARM_IR_CARTRIDGE_CODE << 8));
  branch(b, BEQ(A2, ZERO), done);
  emit(b, NOP);
  emit(b, ADDU(REG_PREFETCH, REG_PREFETCH, A0));
  emit(b, SRL(A2, V1, 24));
  emit(b, SLL(A2, A2, 2));
  emit(b, ADDU(A2, A2, GP));
  emit(b, LBU(A2, CORE(wait) + 1, A2));
  emit(b, SLL(A2, A2, 3));
  emit(b, MIN(REG_PREFETCH, REG_PREFETCH, A2));
  label_bind(b, done);
  emit(b, JR(RA));
  emit(b, MOVE(V1, ZERO));
  label_bind(b, call);
  emit(b, J(jit->routine_call));
  emit(b, NOP);
}

/* The work RAM regions the fast accesses may use: mapped both ways, with
   the masks the code map assumes. */
static void fast_regions(ArmMips *jit)
{
  unsigned i;

  for(i = 0; i < 2; i++)
  {
    const ArmRegion *map = &jit->core.map[2 + i];

    jit->fast[i][0] = jit->fast[i][1] = 0;
    jit->fast[i][2] = i ? 0x40000 : 0;
    if(map->read && map->write && map->read == map->write &&
       map->mask == (i ? 0x7FFFu : 0x3FFFFu))
    {
      jit->fast[i][0] = target_of(jit, map->write);
      jit->fast[i][1] = map->mask;
    }
  }
}

/* Writes the routines at the start of the buffer: entering generated code
   from C, leaving it, calling arm_mips_access from it, and the stub a
   dropped block's entry jumps to. */
static void write_runtime(ArmMips *jit)
{
  Builder *b = &builder;
  unsigned r;
  static const uint8_t saved[] = { 8, 9, 10, 11, 12, 13, 14, 15, 24 };

  memset(b, 0, offsetof(Builder, hot));
  b->jit = jit;
  b->core = &jit->core;

  /* enter(core, code) */
  jit->routine_enter = code_address(jit, b->hot_count);
  emit(b, ADDIU(SP, SP, -64));
  emit(b, SW(RA, 60, SP));
  emit(b, SW(FP, 56, SP));
  emit(b, SW(GP, 52, SP));
  for(r = 0; r < 8; r++)
    emit(b, SW(16 + r, 20 + 4 * (int)r, SP));
  emit(b, MOVE(GP, A0));
  for(r = 0; r < 15; r++)
    emit(b, LW(guest_register[r], SLOT(r), GP));
  emit(b, LW(REG_N, CORE(flag_n), GP));
  emit(b, LW(REG_Z, CORE(flag_z), GP));
  emit(b, LW(REG_C, CORE(flag_c), GP));
  emit(b, LW(REG_CYCLES, CORE(cycles), GP));
  emit(b, LW(V0, JIT(cycle_base), GP));
  emit(b, SUBU(REG_CYCLES, REG_CYCLES, V0));
  emit(b, LHU(REG_PREFETCH, CORE(prefetch), GP));
  emit(b, JR(A1));
  emit(b, NOP);

  /* exit: a0 = status, a1 = argument */
  jit->routine_exit = code_address(jit, b->hot_count);
  for(r = 0; r < 15; r++)
    emit(b, SW(guest_register[r], SLOT(r), GP));
  emit(b, SW(REG_N, CORE(flag_n), GP));
  emit(b, SW(REG_Z, CORE(flag_z), GP));
  emit(b, SW(REG_C, CORE(flag_c), GP));
  emit(b, LW(V0, JIT(cycle_base), GP));
  emit(b, ADDU(V0, REG_CYCLES, V0));
  emit(b, SW(V0, CORE(cycles), GP));
  emit(b, SH(REG_PREFETCH, CORE(prefetch), GP));
  emit(b, SW(A1, JIT(exit_arg), GP));
  emit(b, MOVE(V0, A0));
  for(r = 0; r < 8; r++)
    emit(b, LW(16 + r, 20 + 4 * (int)r, SP));
  emit(b, LW(GP, 52, SP));
  emit(b, LW(FP, 56, SP));
  emit(b, LW(RA, 60, SP));
  emit(b, JR(RA));
  emit(b, ADDIU(SP, SP, 64));

  /* call: a1-a3 = arguments, v1 = pc | Thumb; returns v0 = the loaded
     value and v1 = exit_request. C keeps s0-s8, but gp is saved as
     well, in case. */
  jit->routine_call = code_address(jit, b->hot_count);
  emit(b, SW(RA, SPILL(RA), GP));
  for(r = 0; r < sizeof(saved); r++)
    emit(b, SW(saved[r], SPILL(saved[r]), GP));
  emit(b, LW(V0, JIT(cycle_base), GP));
  emit(b, ADDU(V0, REG_CYCLES, V0));
  emit(b, SW(V0, CORE(cycles), GP));
  emit(b, SH(REG_PREFETCH, CORE(prefetch), GP));
  emit(b, SW(V1, JIT(call_pc), GP));
  emit(b, SW(GP, 16, SP));
  emit(b, MOVE(A0, GP));
  emit(b, LW(V0, JIT(access), GP));
  emit(b, JALR(V0));
  emit(b, NOP);
  emit(b, LW(GP, 16, SP));
  for(r = 0; r < sizeof(saved); r++)
    emit(b, LW(saved[r], SPILL(saved[r]), GP));
  emit(b, LW(REG_CYCLES, CORE(cycles), GP));
  emit(b, LW(V1, JIT(cycle_base), GP));
  emit(b, SUBU(REG_CYCLES, REG_CYCLES, V1));
  emit(b, LHU(REG_PREFETCH, CORE(prefetch), GP));
  emit(b, LW(V1, CORE(exit_request), GP));
  emit(b, LW(RA, SPILL(RA), GP));
  emit(b, JR(RA));
  emit(b, NOP);

  /* dropped: a jal from a dropped block's main entry, whose key sits four
     words before it. */
  jit->routine_dropped = code_address(jit, b->hot_count);
  emit(b, LW(A1, -4 * (int)BLOCK_HEADER - 8, RA));
  emit(b, J(jit->routine_exit));
  emit(b, ORI(A0, ZERO, STATUS_DROPPED));

  write_fast_access(jit, b);
  place(jit, b, 0);
  jit->blocks_start = b->hot_count;
}

/* ---- C side ------------------------------------------------------------- */

/* Sets (or clears) the code map over the work RAM a block was translated
   from. Blocks never cross a 256-byte page, so no mirror boundary either.
   Returns the interpreter's page of the block, or -1 outside work RAM. */
static int protect(ArmMips *jit, const ArmBlock *block, uint8_t mark)
{
  uint32_t pc = block->key & ~1u;
  uint32_t end = pc + block->insns * ((block->key & 1) ? 2u : 4u);
  uint32_t mask;
  unsigned base;
  int page;

  switch((pc >> 28) ? 1 : pc >> 24)
  {
    case 2:
      mask = 0x3FFFF;
      base = 0;
      page = (int)((pc & mask) / ARM_CODE_PAGE_SIZE);
      break;
    case 3:
      mask = 0x7FFF;
      base = 0x40000 / ARM_MIPS_CODE_GRANULE;
      page = 1024 + (int)((pc & mask) / ARM_CODE_PAGE_SIZE);
      break;
    default:
      return -1;
  }
  for(pc &= ~(ARM_MIPS_CODE_GRANULE - 1); pc < end;
      pc += ARM_MIPS_CODE_GRANULE)
    jit->code_map[base + (pc & mask) / ARM_MIPS_CODE_GRANULE] = mark;
  return page;
}

static void reset_code(ArmMips *jit)
{
  unsigned i;

  jit->used = jit->blocks_start;
  memset(jit->code, 0, sizeof(jit->code));
  memset(jit->code_map, 0, sizeof(jit->code_map));
  for(i = 0; i < ARM_MIPS_DISPATCH; i++)
    jit->dispatch[i][0] = INVALID_KEY;
  for(i = 0; i < ARM_MIPS_RETURNS; i++)
    jit->returns[i][0] = INVALID_KEY;
  jit->generation++;
  jit->stats.flushes++;
}

/* The interpreter dropped a block (or all of them): its entry now leaves
   for C, and the code stays until the next flush. Its work RAM leaves the
   code map, but for the blocks still in its page (the interpreter's list
   still holds this one, which unmarks first). */
static void dropped(ArmCore *core, unsigned block)
{
  ArmMips *jit = jit_of(core);
  uint32_t entry;
  uint32_t *word;
  int page;

  if(block == ARM_NO_BLOCK)
  {
    reset_code(jit);
    return;
  }
  page = protect(jit, &core->blocks[block], 0);
  if(page >= 0)
  {
    unsigned other;

    for(other = core->page_blocks[page]; other != ARM_NO_BLOCK;
        other = core->blocks[other].next_page)
      if(other != block)
        protect(jit, &core->blocks[other], 1);
  }
  entry = jit->code[block];
  jit->code[block] = 0;
  if(entry <= 1)
    return;
  jit->stats.dropped++;
  word = code_host(jit, entry);
  word[0] = JAL(jit->routine_dropped);
  word[1] = NOP;
  sync_code(jit, word, 2);
}

/* The cycle register counts towards cycle_base: the budget, or now if an
   interrupt waits, so the next taken branch leaves. */
static void set_base(ArmMips *jit)
{
  ArmCore *core = &jit->core;

  jit->cycle_base = core->cycle_limit;
  if(core->irq_line && !(core->cpsr & ARM_CPSR_I) &&
     core->cycles < core->cycle_limit)
    jit->cycle_base = core->cycles;
}

uint32_t arm_mips_access(ArmCore *core, uint32_t address, uint32_t value,
 uint32_t info)
{
  ArmMips *jit = jit_of(core);
  uint32_t result;

  core->insn_pc = jit->call_pc & ~1u;
  core->cpsr = (core->cpsr & ~ARM_CPSR_T) | (jit->call_pc & 1) << 5;
  result = arm_core_access(core, info & 0xFF, info >> 8, address, value);
  set_base(jit);
  return result;
}

static uint32_t run_code(ArmMips *jit, uint32_t code)
{
  if(jit->host.enter)
    return jit->host.enter(jit->host.context, jit->routine_enter,
     jit->core_target, code);
#if defined(__mips__)
  return ((uint32_t (*)(uint32_t, uint32_t))(uintptr_t)jit->routine_enter)(
   jit->core_target, code);
#else
  return 0;
#endif
}

static void go_to(ArmCore *core, uint32_t key)
{
  core->cpsr = (core->cpsr & ~ARM_CPSR_T) | (key & 1) << 5;
  core->v[15] = key & ~1u;
}

/* An indirect jump's pipeline refill and prefetch flush, which the code
   leaves to its target's refill entry. */
static void refill(ArmCore *core)
{
  unsigned width = (core->cpsr & ARM_CPSR_T) ? 2 : 4;

  core->cycles += (int32_t)(arm_access_cycles(core, core->v[15], width, 0) +
   arm_access_cycles(core, core->v[15] + width, width, 1));
  core->prefetch = 0;
}

static void dispatch_insert(ArmMips *jit, uint32_t key, uint32_t entry)
{
  uint32_t *slot = jit->dispatch[(key >> 1) & (ARM_MIPS_DISPATCH - 1)];

  slot[0] = key;
  slot[1] = entry - 4 * (BLOCK_HEADER - 1);
}

/* The main entry of the block at r15, compiled now if need be; 0 if the
   interpreter has to run it. */
static uint32_t block_entry(ArmMips *jit)
{
  ArmCore *core = &jit->core;
  const ArmBlock *block = arm_core_block(core);
  unsigned index = (unsigned)(block - core->blocks);
  uint32_t end = jit->words - ARM_MIPS_SCRATCH_WORDS;
  uint32_t entry = jit->code[index];
  int tries;

  /* Every block C runs goes (back) into the dispatch cache, so a jump the
     cache missed finds it next time. */
  if(entry > 1)
    dispatch_insert(jit, block->key, entry);
  if(entry)
    return entry > 1 ? entry : 0;
  /* The hook's block runs in the interpreter, which calls the hook: never
     compiled, nothing links to it or caches it. */
  if(core->hook && (block->key & ~1u) == (core->hook & ~1u))
  {
    jit->code[index] = 1;
    return 0;
  }
  for(tries = 0; tries < 2; tries++)
  {
    protect(jit, block, 1);
    entry = compile(jit, &core->ops[block->first], block->count, block->key,
     block->first, MODE_LINKED, jit->used, end);
    if(entry || builder.failed)
      break;
    /* The buffer is full: start again with an empty cache. */
    arm_core_flush(core);
    core->exit_request = 0;
    block = arm_core_block(core);
    index = (unsigned)(block - core->blocks);
  }
  if(!entry)
  {
    jit->code[index] = 1;
    jit->stats.interpreted++;
    return 0;
  }
  jit->code[index] = entry;
  dispatch_insert(jit, block->key, entry);
  return entry;
}

/* Links a direct exit to its target, now compiled; returns whether the
   target still needs its refill counted (a return slot). */
static int link(ArmMips *jit, uint32_t record)
{
  const uint32_t *r = code_host(jit, record);
  uint32_t site = r[0];
  uint32_t key = r[1];
  uint32_t flags = r[2];
  uint32_t generation = jit->generation;
  uint32_t entry;

  go_to(&jit->core, key);
  entry = block_entry(jit);
  if(entry && generation == jit->generation)
  {
    uint32_t *word = code_host(jit, site);

    *word = J((flags & LINK_RETURN) ? entry - 4 * (BLOCK_HEADER - 1) :
     entry);
    sync_code(jit, word, 1);
    jit->stats.links++;
  }
  return (flags & LINK_RETURN) != 0;
}

static const ArmIrOp *bail_ops(ArmMips *jit, uint32_t code)
{
  return (code & STATUS_STEP) ? jit->step_ops : jit->core.ops;
}

int32_t arm_mips_run(ArmMips *jit, int32_t cycles)
{
  ArmCore *core = &jit->core;
  int check = 1;

  fast_regions(jit);
  core->cycles = 0;
  core->cycle_limit = cycles;
  for(;;)
  {
    uint32_t entry;
    uint32_t code;
    uint32_t arg;

    if(check)
    {
      if(core->cycles >= core->cycle_limit)
        break;
      if(core->irq_line && !(core->cpsr & ARM_CPSR_I))
        core->cycles += arm_core_irq(core);
    }
    entry = block_entry(jit);
    core->exit_request = 0;
    if(!entry)
      check = arm_core_run_block(core) != ARM_EXIT_NEXT;
    else
    {
      set_base(jit);
      code = run_code(jit, entry);
      arg = jit->exit_arg;
      check = 1;
      switch(code & 15)
      {
        case STATUS_BUDGET:
          go_to(core, arg);
          break;
        case STATUS_LINK:
          if(link(jit, arg))
            refill(core);
          else
            check = 0;
          break;
        case STATUS_MISS:
          go_to(core, arg);
          refill(core);
          jit->stats.misses++;
          break;
        case STATUS_BAIL:
          core->cpsr = (core->cpsr & ~ARM_CPSR_T) |
           ((code & STATUS_THUMB) ? ARM_CPSR_T : 0);
          check = arm_core_execute(core, bail_ops(jit, code) + arg) !=
           ARM_EXIT_NEXT;
          jit->stats.bails++;
          break;
        default:
          go_to(core, arg);
          check = 0;
          break;
      }
    }
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

/* Runs ops compiled to run once; returns the exit kind. */
static ArmExitKind run_once(ArmMips *jit, const ArmIrOp *ops, unsigned count,
 uint32_t op_base, unsigned mode)
{
  ArmCore *core = &jit->core;
  uint32_t key = core->v[15] | ((core->cpsr & ARM_CPSR_T) ? 1u : 0u);
  uint32_t at = jit->words - ARM_MIPS_SCRATCH_WORDS;
  uint32_t entry = compile(jit, ops, count, key, op_base, mode, at,
   jit->words);
  uint32_t code;
  uint32_t arg;
  ArmExitKind kind;

  core->exit_request = 0;
  if(!entry)
    return arm_core_execute(core, ops);
  fast_regions(jit);
  set_base(jit);
  code = run_code(jit, entry);
  arg = jit->exit_arg;
  kind = (ArmExitKind)((code >> 4) & 15);
  switch(code & 15)
  {
    case STATUS_MISS:
      go_to(core, arg);
      refill(core);
      return kind;
    case STATUS_BAIL:
      core->cpsr = (core->cpsr & ~ARM_CPSR_T) |
       ((code & STATUS_THUMB) ? ARM_CPSR_T : 0);
      return arm_core_execute(core, bail_ops(jit, code) + arg);
    default:
      go_to(core, arg);
      return kind;
  }
}

ArmExitKind arm_mips_run_block(ArmMips *jit)
{
  ArmCore *core = &jit->core;
  const ArmBlock *block = arm_core_block(core);

  protect(jit, block, 1);
  return run_once(jit, &core->ops[block->first], block->count, block->first,
   MODE_ONCE);
}

int arm_mips_step(ArmMips *jit)
{
  ArmCore *core = &jit->core;
  int32_t before = core->cycles;
  unsigned count = arm_translate(core, core->v[15],
   (core->cpsr & ARM_CPSR_T) != 0, jit->step_ops, 96, 1, NULL);

  run_once(jit, jit->step_ops, count, 0, MODE_STEP);
  return core->cycles - before;
}

void arm_mips_init(ArmMips *jit, const ArmBus *bus, const ArmMipsHost *host,
 uint32_t *buffer, uint32_t bytes)
{
  arm_core_init(&jit->core, bus);
  jit->host = *host;
  jit->buffer = buffer;
  jit->words = bytes / 4;
  jit->buffer_target = target_of(jit, buffer);
  jit->core_target = target_of(jit, &jit->core);
  jit->access = host->access ? host->access :
   (uint32_t)(uintptr_t)arm_mips_access;
  jit->return_top = 0;
  jit->generation = 0;
  jit->count_instructions = 0;
  jit->profiling = 0;
  memset(&jit->stats, 0, sizeof(jit->stats));
  memset(jit->profile, 0, sizeof(jit->profile));
  write_runtime(jit);
  reset_code(jit);
  jit->stats.flushes = 0;
  jit->core.dropped = dropped;
}
