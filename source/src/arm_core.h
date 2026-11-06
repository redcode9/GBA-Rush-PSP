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
 * ARM7TDMI core: decoder (arm_decode.c) into the IR of arm_ir.h, and an
 * interpreter of that IR with a block cache (arm_interp.c).
 *
 * All state, the block cache included, lives in one ArmCore, so two cores
 * can run side by side; the embedder provides the storage (it is a few
 * megabytes) and the bus. Nothing allocates memory or uses floating point.
 *
 * Timing is a pure function of the guest instruction stream. The core
 * counts cycles per instruction from the GBA's waitstates and checks its
 * cycle budget, and a raised interrupt line, only at taken branches and
 * exception entries and returns (and far past the budget, see
 * ARM_RUNAWAY_SHIFT, and in zeros where nothing is mapped, see
 * arm_core_skip). A bus handler can ask it to stop after
 * the current instruction (arm_core_request_exit) when a store changes
 * something the system must act on at once, such as HALTCNT or IME.
 */

#ifndef ARM_CORE_H
#define ARM_CORE_H

#include <stdint.h>

#include "arm_ir.h"

#define ARM_CPSR_N 0x80000000u
#define ARM_CPSR_Z 0x40000000u
#define ARM_CPSR_C 0x20000000u
#define ARM_CPSR_V 0x10000000u
#define ARM_CPSR_I 0x00000080u
#define ARM_CPSR_F 0x00000040u
#define ARM_CPSR_T 0x00000020u
#define ARM_CPSR_MODE 0x0000001Fu

#define ARM_MODE_USER       0x10u
#define ARM_MODE_FIQ        0x11u
#define ARM_MODE_IRQ        0x12u
#define ARM_MODE_SUPERVISOR 0x13u
#define ARM_MODE_ABORT      0x17u
#define ARM_MODE_UNDEFINED  0x1Bu
#define ARM_MODE_SYSTEM     0x1Fu

/* The system bus. Reads and writes may have side effects (I/O); 16- and
   32-bit addresses arrive aligned. code32 reads the aligned word holding
   an instruction or a literal at translate time and must have none.
   unmapped, optional, says whether nothing that holds code is mapped from
   address to the end of its 16 MB region, so that code32 reads 0 all the
   way and always will; the core then moves past those zeros without
   translating them (arm_core_skip). */
typedef struct
{
  void *context;
  uint32_t (*read8)(void *context, uint32_t address);
  uint32_t (*read16)(void *context, uint32_t address);
  uint32_t (*read32)(void *context, uint32_t address);
  void (*write8)(void *context, uint32_t address, uint32_t value);
  void (*write16)(void *context, uint32_t address, uint32_t value);
  void (*write32)(void *context, uint32_t address, uint32_t value);
  uint32_t (*code32)(void *context, uint32_t address);
  int (*unmapped)(void *context, uint32_t address);
} ArmBus;

/* Optional direct access to one of the 16 regions (address bits 24-27),
   such as work RAM: the core reads and writes host memory at
   (address & mask) instead of calling the bus. NULL pointers use the
   bus. Writes to work RAM and VRAM must stay with the core either way,
   which invalidates translated code they overwrite. */
typedef struct
{
  const uint8_t *read;
  uint8_t *write;
  uint32_t mask;
} ArmRegion;

/* Block cache sizes; a block and its ops are only reclaimed when the
   whole cache is flushed. */
#ifndef ARM_CACHE_OPS
#define ARM_CACHE_OPS (1u << 18)
#endif
#ifndef ARM_CACHE_BLOCKS
#define ARM_CACHE_BLOCKS (1u << 14)
#endif
#define ARM_CACHE_HASH (1u << 13)
#define ARM_BLOCK_MAX_OPS 1024u
#define ARM_BLOCK_MAX_INSNS 64u
/* Translated code in RAM is tracked in pages of this size: 1024 for the
   256 KB of work RAM on board, 128 for the 32 KB on chip, then 384 for
   the 96 KB of VRAM. */
#define ARM_CODE_PAGE_SIZE 256u
#define ARM_CODE_PAGES (1024u + 128u + 384u)
#define ARM_NO_BLOCK 0xFFFFu
#define ARM_IDLE_LOOPS 16u
#define ARM_NO_IDLE_PASS 0xFFFFFFFFu
/* A run still going 2^ARM_RUNAWAY_SHIFT cycles past its budget ends at the
   next block that fell through at its length or page limit (an EXIT with
   ARM_IR_LIMIT). Only code sliding through memory without branches gets
   that far, such as zeroed work RAM (zeros where nothing is mapped are
   skipped instead, see arm_core_skip), and such a run would otherwise
   never return to the system: no frame, no menu, no save. Real code takes
   a branch long before, so this never changes its timing. */
#define ARM_RUNAWAY_SHIFT 18

typedef struct
{
  uint32_t key;         /* start address, bit 0 set for Thumb */
  uint32_t first;       /* index of the first op in ArmCore.ops */
  uint16_t count;       /* ops */
  uint16_t insns;       /* guest instructions */
  uint16_t next_hash;   /* next block in the same hash bucket */
  uint16_t next_page;   /* next block in the same RAM page */
} ArmBlock;

/* Bits of ArmCore.exit_request. */
#define ARM_EXIT_LEAVE_BLOCK 1u  /* leave the block, keep running */
#define ARM_EXIT_STOP        2u  /* return from arm_core_run */

typedef struct ArmCore
{
  /* Guest registers r0-r15 of the current mode, then the IR temporaries.
     r15 holds the address of the next instruction to run. */
  uint32_t v[ARM_IR_SLOTS];
  uint32_t flag_n;          /* N is bit 31 */
  uint32_t flag_z;          /* Z is set when this is 0 */
  uint32_t flag_c;          /* 0 or 1 */
  uint32_t flag_v;          /* V is bit 31 */
  uint32_t cpsr;            /* mode, T, F and I; the flags live above */
  uint32_t spsr;            /* SPSR of the current mode */
  /* Banked registers of the modes not running: r13 and r14 per bank
     (user and system, FIQ, IRQ, supervisor, abort, undefined), r8-r12 for
     FIQ and for every other mode, and the SPSRs. */
  uint32_t bank_r13_r14[6][2];
  uint32_t bank_r8_r12[2][5];
  uint32_t bank_spsr[6];

  int32_t cycles;           /* run so far in this arm_core_run */
  int32_t cycle_limit;
  uint32_t insn_pc;         /* address of the instruction running */
  uint32_t exit_request;    /* ARM_EXIT_* bits */
  int irq_line;             /* the system's IRQ request, level triggered */
  uint32_t instructions;    /* guest instructions run, wraps */

  ArmBus bus;
  ArmRegion map[16];
  /* Cycles per access to each region: 16-bit N and S, 32-bit N and S
     (8-bit accesses cost what 16-bit ones do). */
  uint8_t wait[16][4];
  uint16_t waitcnt;
  /* GamePak prefetch buffer, as the cycles it has spent filling: the
     halfwords ready times their access time, plus the cycles spent on the
     next one. One number, so the MIPS backend keeps it in a register. */
  uint16_t prefetch;

  /* Told when a block is dropped (its index) or the whole cache is
     (ARM_NO_BLOCK), so a code generator can drop its code; NULL if none. */
  void (*dropped)(struct ArmCore *core, unsigned block);
  /* Code at address hook (0: none) calls hooked whenever it is about to
     run, for cheats that run at a game address: it always starts a block,
     which a backend leaves to the interpreter. Flush after changing it. */
  uint32_t hook;
  void (*hooked)(struct ArmCore *core);
  /* Idle loops, a game spinning until an interrupt: the addresses of their
     B instructions (or ARM BL), conditional or not. Taken, such a branch
     skips the passes that would only spend the budget, so arm_core_run
     ends where spinning would have and the system moves straight to its
     next event. Flush after changing them. */
  uint32_t idle_loops[ARM_IDLE_LOOPS];
  unsigned idle_loop_count;
  /* The target and cycle of the last idle loop branch taken in this run,
     to measure one pass; ARM_NO_IDLE_PASS when there is none. */
  uint32_t idle_target;
  int32_t idle_cycles;
  /* Set once code in VRAM is translated, until the next flush: until then
     a store to VRAM needs no look at its pages. */
  uint32_t vram_code;

  uint32_t block_count;
  uint32_t op_count;
  uint32_t flushes;
  uint16_t hash[ARM_CACHE_HASH];
  uint16_t page_blocks[ARM_CODE_PAGES];
  ArmBlock blocks[ARM_CACHE_BLOCKS];
  ArmIrOp ops[ARM_CACHE_OPS];
} ArmCore;

/* Sets up the core in its reset state: supervisor mode, ARM state,
   interrupts disabled, pc 0, WAITCNT 0. */
void arm_core_init(ArmCore *core, const ArmBus *bus);
void arm_core_reset(ArmCore *core);

uint32_t arm_core_cpsr(const ArmCore *core);
/* Writes the whole CPSR, switching register banks when the mode changes. */
void arm_core_set_cpsr(ArmCore *core, uint32_t cpsr);
/* Where register 8-14 (or the SPSR, reg 16) of a mode is kept right now:
   in v[] for the current mode, in a bank otherwise. */
uint32_t *arm_core_register(ArmCore *core, unsigned mode, unsigned reg);

/* New WAITCNT value; flushes the translations, whose timing it changes. */
void arm_core_set_waitcnt(ArmCore *core, uint16_t waitcnt);
void arm_core_flush(ArmCore *core);
/* Drops translations of RAM (work RAM, VRAM) written behind the core's
   back (DMA). */
void arm_core_invalidate(ArmCore *core, uint32_t address, uint32_t size);

/* Enters the IRQ exception if the CPSR allows it; returns its cycles. */
int arm_core_irq(ArmCore *core);
/* Asks the core to stop after the instruction running (from bus code). */
void arm_core_request_exit(ArmCore *core);

/* Runs until at least `cycles` have passed, checking only at taken
   branches (and ARM_RUNAWAY_SHIFT, arm_core_skip), or until an exit
   request; returns the cycles run. */
int32_t arm_core_run(ArmCore *core, int32_t cycles);
/* The block at r15, translated now if the cache does not hold it. */
const ArmBlock *arm_core_block(ArmCore *core);
/* Whether r15 is in zeros where nothing is mapped (ArmBus.unmapped) that
   arm_core_skip moves past, so nothing there needs translating. */
int arm_core_zeros(const ArmCore *core);
/* For a run loop: when arm_core_zeros holds and no block at r15 is
   cached, moves past the zeros there as running them would, up to the
   first instruction at or past the budget, and then asks the run to stop
   (ARM_EXIT_STOP), or up to the last two before the end of their 16 MB
   region. Returns whether it did. */
int arm_core_skip(ArmCore *core);
/* The block at r15 if the cache holds it, else NULL. */
const ArmBlock *arm_core_cached(ArmCore *core);
/* Runs the block at r15 and returns its exit kind; no interrupt or budget
   check. */
ArmExitKind arm_core_run_block(ArmCore *core);
/* Translates and runs exactly one instruction outside the cache; returns
   the cycles it took. */
int arm_core_step(ArmCore *core);
/* Interprets ops from op on, to the block's exit; for a backend that
   leaves the rest of a block to the interpreter. */
ArmExitKind arm_core_execute(ArmCore *core, const ArmIrOp *op);
/* One load or store op (code and flags as in the IR) at address, as the
   interpreter does it: cycles, prefetch buffer, bus and code invalidation.
   Returns the value loaded; value is the one stored. */
uint32_t arm_core_access(ArmCore *core, unsigned code, unsigned flags,
 uint32_t address, uint32_t value);

/* Cycles of one access of size 1, 2 or 4 bytes at address. */
unsigned arm_access_cycles(const ArmCore *core, uint32_t address,
 unsigned size, int sequential);
/* Extra cycles for the next opcode fetch of code at pc when a load used
   the GamePak bus: that fetch becomes non-sequential. */
unsigned arm_cartridge_refetch(const ArmCore *core, uint32_t pc);

#endif
