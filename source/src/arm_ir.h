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
 * Intermediate representation of translated ARM7TDMI code.
 *
 * The decoder turns a run of guest instructions (a block) into a short
 * linear list of fixed-size ops. The same list feeds the portable
 * interpreter in arm_interp.c and, later, the MIPS code generator, so the
 * format follows what an Allegrex backend wants to see:
 *
 * Values. Every operand is a one-byte slot. Slots 0 to 14 are the guest
 * registers r0 to r14 of the current mode, which a backend can keep in
 * host registers for the whole block (MIPS has room for all of them).
 * Slots from ARM_IR_FIRST_TEMP up are temporaries local to one guest
 * instruction (numbering starts again with the next), each written once,
 * so a backend can give them the few scratch registers left with a
 * trivial allocator, and a block's length is never limited by them. Slot
 * 15 is never read: reading r15 becomes a constant at translate time, and
 * writing it becomes an exit op.
 *
 * Flags. Ops carry explicit flag effects (ARM_IR_SET_NZ and so on), and
 * after decoding a backward liveness pass clears every flag write that no
 * later op or exit can observe, which removes most C and V computations.
 * N and Z always come from one result value, so a backend sets both by
 * remembering which register holds the last result instead of computing
 * anything. The guest flags live in ArmCore in the shapes MIPS produces
 * cheaply: N is bit 31 of flag_n, Z is set when flag_z is zero, C is 0 or
 * 1 (what sltu yields) and V is bit 31 of flag_v. N and Z are two words
 * rather than one result value because MSR and exception returns can set
 * both at once, which no single value expresses; an ALU result is simply
 * stored to both.
 *
 * Timing. Every guest instruction starts with an ARM_IR_INSN op holding
 * the cycles known at translate time: its code fetches from the region it
 * runs in (with the 128 KB ROM boundary rule), internal cycles, branch
 * refills to known targets and data accesses whose address the decoder
 * could work out by constant propagation (literal pools, I/O registers
 * loaded from them). Only the rest is counted at run time: data accesses
 * to unknown addresses, multiply early termination, refills after
 * indirect jumps, and opcode fetches from the GamePak while its prefetch
 * buffer is enabled. That buffer is the one piece of timing state: it
 * fills while the GamePak bus is idle (ARM_IR_IDLE says for how long) and
 * empties at taken branches and GamePak data accesses. Timing therefore
 * depends on the guest instruction stream alone, never on how the code
 * was split into blocks.
 *
 * Exits. A block ends at the first instruction that writes r15 or changes
 * the processor mode. Exit ops say what kind of jump they are, so a
 * backend can link direct branches, predict returns with a return stack
 * and send other indirect jumps through a dispatch cache.
 */

#ifndef ARM_IR_H
#define ARM_IR_H

#include <stdint.h>

#define ARM_IR_SLOTS 256u
#define ARM_IR_FIRST_TEMP 16u
/* An op's d field when the op has no destination (CMP, stores). */
#define ARM_IR_NONE 255u

typedef enum
{
  ARM_IR_NOP,
  /* Start of a guest instruction: a + 256 * d = cycles known at translate
     time, b = its address. With ARM_IR_CHECK the interpreter first leaves
     the block there if the bus asked it to (after a store to I/O, or to
     code). ARM_IR_FETCH16 or ARM_IR_FETCH32: its opcode fetch comes from
     the GamePak prefetch buffer, counted at run time. */
  ARM_IR_INSN,
  /* Unless condition a holds, skip the next b ops; when it holds, add d
     cycles (the instruction's cost beyond the 1S of a failed condition). */
  ARM_IR_SKIP,
  /* Add b cycles. */
  ARM_IR_CYCLES,

  /* d = a <op> b, where b is a slot or, with ARM_IR_IMM, the value. MOV
     and MVN ignore a. Every operand is read before d is written. */
  ARM_IR_MOV,
  ARM_IR_MVN,
  ARM_IR_AND,
  ARM_IR_EOR,
  ARM_IR_ORR,
  ARM_IR_BIC,
  ARM_IR_ADD,
  ARM_IR_ADC,
  ARM_IR_SUB,
  ARM_IR_SBC,
  ARM_IR_RSB,
  ARM_IR_RSC,
  /* Shifts of a by b. With an immediate, b is 1 to 32 (32 only for LSR
     and ASR) and ARM_IR_SET_C takes the last bit shifted out. With a slot
     the ARM register rules apply: the bottom byte of b counts, 0 leaves
     the value and C alone, and 32 or more shifts everything out. */
  ARM_IR_LSL,
  ARM_IR_LSR,
  ARM_IR_ASR,
  ARM_IR_ROR,
  /* d = C:a >> 1, setting C to bit 0 of a with ARM_IR_SET_C. */
  ARM_IR_RRX,
  /* Low word of a * b; high word of the unsigned and signed products. */
  ARM_IR_MUL,
  ARM_IR_UMULH,
  ARM_IR_SMULH,
  /* d = (a < b) unsigned, for the carries of 64-bit accumulates. */
  ARM_IR_SLTU,
  /* C = b. */
  ARM_IR_SETC,
  /* N and Z of the 64-bit value a:b (a high). */
  ARM_IR_SETNZ64,
  /* Add the multiplier's early termination cycles for operand a: b = 0
     counts leading all-zero or all-one bytes, b = 1 only all-zero bytes
     (UMULL, UMLAL). */
  ARM_IR_MULCYC,

  /* Loads: d = memory at a + b (b always an immediate). LD32 rotates an
     unaligned word as the ARM7TDMI does, LD16 rotates an odd halfword
     into the top byte and LD16S at an odd address loads a signed byte.
     Stores: memory at a + b = d, aligned down to the access size.
     ARM_IR_SEQ marks a sequential access; ARM_IR_FOLDED one whose cycles
     are already in the instruction's ARM_IR_INSN count. */
  ARM_IR_LD8,
  ARM_IR_LD8S,
  ARM_IR_LD16,
  ARM_IR_LD16S,
  ARM_IR_LD32,
  ARM_IR_ST8,
  ARM_IR_ST16,
  ARM_IR_ST32,

  /* The GamePak bus was free for b cycles: the prefetch buffer fills. */
  ARM_IR_IDLE,
  /* A GamePak data read folded into a constant: the buffer empties. */
  ARM_IR_FLUSH,

  /* d = CPSR (a = 0) or SPSR (a = 1). */
  ARM_IR_MRS,
  /* PSR = a under field mask b bits 0-3 (control, extension, status,
     flags), SPSR when b bit 4 is set. */
  ARM_IR_MSR,
  /* User-mode register b, for LDM and STM with the S bit: d = it (GET),
     or it = a (SET). */
  ARM_IR_USER_GET,
  ARM_IR_USER_SET,

  /* Exits; d is an ArmExitKind. EXIT jumps to b. EXIT_REG jumps to slot
     a: with ARM_IR_BX bit 0 picks Thumb or ARM state, with ARM_IR_RESTORE
     the CPSR is first restored from the SPSR (an exception return), and
     the address is then aligned for the state. SWI and UNDEF enter their
     exception with b as the return address. An EXIT with ARM_IR_IDLE_LOOP
     is a branch the embedder named an idle loop (ArmCore.idle_loops). */
  ARM_IR_EXIT,
  ARM_IR_EXIT_REG,
  ARM_IR_SWI,
  ARM_IR_UNDEF,

  ARM_IR_CODES
} ArmIrCode;

/* Op flags. Their meaning depends on the code. */
#define ARM_IR_IMM     0x01u  /* ALU: b is an immediate */
#define ARM_IR_SET_NZ  0x02u  /* ALU: write N and Z from the result */
#define ARM_IR_SET_C   0x04u  /* ALU: write C */
#define ARM_IR_SET_V   0x08u  /* ALU: write V */
#define ARM_IR_USE_C   0x10u  /* ALU: the result depends on C */
#define ARM_IR_SEQ     0x20u  /* memory: sequential access */
#define ARM_IR_FOLDED  0x40u  /* memory: cycles already counted */
#define ARM_IR_CHECK   0x80u  /* INSN: honour exit requests here */
#define ARM_IR_FETCH16 0x01u  /* INSN: Thumb fetch from the prefetch buffer */
#define ARM_IR_FETCH32 0x02u  /* INSN: ARM fetch from the prefetch buffer */
/* Memory and MULCYC: the code runs from the GamePak, so GamePak data
   accesses empty the prefetch buffer and other cycles fill it. */
#define ARM_IR_CARTRIDGE_CODE 0x01u
#define ARM_IR_BX      0x20u  /* EXIT_REG: bit 0 selects the state */
#define ARM_IR_RESTORE 0x40u  /* EXIT_REG: CPSR = SPSR first */
#define ARM_IR_IDLE_LOOP 0x80u  /* EXIT: the branch of an idle loop */
#define ARM_IR_LIMIT   0x40u  /* EXIT: the fall-through of a block that
                                 reached its length or page limit */

/* What an exit is, for the dispatcher and a backend's predictors. Only
   NEXT is not a taken branch: the guest just runs on into the next block,
   so the dispatcher does not stop there for events or interrupts. */
typedef enum
{
  ARM_EXIT_NEXT,    /* fall through into the following instruction */
  ARM_EXIT_JUMP,    /* any other branch */
  ARM_EXIT_CALL,    /* sets lr to its return address */
  ARM_EXIT_RETURN,  /* bx lr, mov pc lr, pop {pc}, ldr pc from the stack */
  ARM_EXIT_SYSTEM   /* exception entry or return, mode change */
} ArmExitKind;

/* Condition codes, as encoded in ARM instructions. */
enum
{
  ARM_COND_EQ, ARM_COND_NE, ARM_COND_CS, ARM_COND_CC,
  ARM_COND_MI, ARM_COND_PL, ARM_COND_VS, ARM_COND_VC,
  ARM_COND_HI, ARM_COND_LS, ARM_COND_GE, ARM_COND_LT,
  ARM_COND_GT, ARM_COND_LE, ARM_COND_AL, ARM_COND_NV
};

/* Eight bytes, so a block's ops sit in a few cache lines. */
typedef struct
{
  uint8_t code;
  uint8_t flags;
  uint8_t d;
  uint8_t a;
  uint32_t b;
} ArmIrOp;

/* What the decoder reports about a block it translated. */
typedef struct
{
  uint32_t end;       /* address after the last instruction */
  unsigned insns;     /* guest instructions */
  unsigned ops;
} ArmIrBlockInfo;

struct ArmCore;

/* Translates the block at pc (Thumb when thumb is set) into at most
   max_ops ops and max_insns instructions, then runs the flag liveness
   pass. Returns the number of ops. Blocks in work RAM stop at the end of
   their ARM_CODE_PAGE_SIZE page so a store can invalidate them by page. */
unsigned arm_translate(struct ArmCore *core, uint32_t pc, int thumb,
 ArmIrOp *ops, unsigned max_ops, unsigned max_insns, ArmIrBlockInfo *info);

/* Name of an op code, for dumps and tests. */
const char *arm_ir_name(unsigned code);

#endif
