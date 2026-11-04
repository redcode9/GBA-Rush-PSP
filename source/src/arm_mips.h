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
 * MIPS (PSP Allegrex) code generator for the ARM7TDMI core: it compiles
 * the blocks of the interpreter's cache (arm_core.h) into host code and
 * runs them with the interpreter's timing, cycle for cycle. arm_mips.c
 * describes the design.
 *
 * The generated code is plain 32-bit MIPS plus the Allegrex instructions
 * ext, ins, rotr, rotrv, clz, min and max, so it runs on the PSP or, for
 * tests, in a MIPS emulator; ArmMipsHost says how to reach it.
 */

#ifndef ARM_MIPS_H
#define ARM_MIPS_H

#include "arm_core.h"

/* Direct-mapped cache of (pc, Thumb) -> code for indirect jumps. */
#define ARM_MIPS_DISPATCH 4096u
#define ARM_MIPS_RETURNS 16u
/* Bytes of work RAM (256 KB, then the 32 KB inside) per entry of the map
   of translated code that stores check. */
#define ARM_MIPS_CODE_GRANULE 32u
#define ARM_MIPS_CODE_MAP ((0x40000u + 0x8000u) / ARM_MIPS_CODE_GRANULE)
/* Words at the end of the code buffer for code compiled and run once
   (arm_mips_step, arm_mips_run_block). */
#define ARM_MIPS_SCRATCH_WORDS 32768u

/* What the embedder provides. NULL members take the PSP's way. */
typedef struct
{
  void *context;
  /* The address generated code sees for host memory (the same one on the
     PSP; a test maps host buffers into an emulator elsewhere). */
  uint32_t (*target)(void *context, const void *host);
  /* Makes code written at [code, code + bytes) visible to instruction
     fetch: on the PSP, write back the data cache and invalidate the
     instruction cache over it. Required. */
  void (*sync)(void *context, void *code, uint32_t bytes);
  /* Calls the generated routine at entry with a0 = core and a1 = code and
     returns its v0 (on the PSP, a plain call). */
  uint32_t (*enter)(void *context, uint32_t entry, uint32_t core,
   uint32_t code);
  /* The address generated code calls for arm_mips_access (0: its own). */
  uint32_t access;
  /* A free-running clock in any unit (NULL: none), read around each
     translation for the decode and compile times in ArmMipsStats. */
  uint32_t (*clock)(void *context);
} ArmMipsHost;

/* What the thread running the core is doing, in ArmMips.phase, for a
   sampling profiler. Outside arm_mips_run it is EVENTS: the rest of the
   system (timers, video, sound) between two runs of the core. */
enum
{
  ARM_MIPS_PHASE_CODE,       /* generated code and the routines it calls */
  ARM_MIPS_PHASE_DISPATCH,   /* C between blocks: lookups, links, IRQs */
  ARM_MIPS_PHASE_MEMORY,     /* the memory accesses generated code calls */
  ARM_MIPS_PHASE_TRANSLATE,  /* decoding and compiling new blocks */
  ARM_MIPS_PHASE_INTERPRET,  /* the interpreter, for a block or its rest */
  ARM_MIPS_PHASE_EVENTS,
  ARM_MIPS_PHASES
};

typedef struct
{
  uint32_t blocks;          /* compiled */
  uint32_t guest_insns;     /* in them */
  uint32_t hot_words;       /* their main paths */
  uint32_t cold_words;      /* their slow paths, stubs and records */
  uint32_t links;           /* direct exits linked after the fact */
  uint32_t misses;          /* indirect exits the dispatch cache missed */
  uint32_t bails;           /* blocks finished by the interpreter */
  uint32_t dropped;         /* compiled blocks whose code was overwritten */
  uint32_t interpreted;     /* blocks too large to compile */
  uint32_t flushes;
  uint32_t decode_ticks;    /* clock ticks spent decoding new blocks */
  uint32_t compile_ticks;   /* and compiling them */
} ArmMipsStats;

typedef struct ArmMips
{
  /* Generated code reaches these below gp, which points at core: keep
     them within 32 KB of it. */
  uint32_t profile[4];      /* return stack hits and misses, dispatch hits
                               and misses (with profiling set) */
  uint32_t returns[ARM_MIPS_RETURNS][2];  /* key, code to return to */
  uint32_t return_top;      /* byte offset of the top entry */
  uint32_t access;          /* arm_mips_access, as the code calls it */
  uint32_t spill[32];       /* host registers kept around calls */
  int32_t cycle_base;       /* the cycle register holds cycles minus this */
  uint32_t call_pc;         /* insn_pc | Thumb of the access in progress */
  uint32_t exit_arg;
  uint32_t entered;         /* with sampling set, where generated code last
                               entered a block (see arm_mips_sampled_key) */
  uint32_t fast[2][4];      /* work RAM and internal RAM for the slow paths:
                               target, mask (0: use C), code map bias */
  /* Nonzero where work RAM holds translated code (since the last flush):
     a store there takes the slow path, which drops that code. Finer than
     the interpreter's 256-byte pages, so data next to code stays fast. */
  uint8_t code_map[ARM_MIPS_CODE_MAP];
  ArmCore core;

  /* C only. */
  uint32_t dispatch[ARM_MIPS_DISPATCH][2];  /* key, refill entry */
  uint32_t code[ARM_CACHE_BLOCKS];   /* each block's entry, 0 when none,
                                        1 when it is interpreted */
  ArmIrOp step_ops[96];
  ArmMipsHost host;
  uint32_t *buffer;
  uint32_t words;           /* buffer size */
  uint32_t used;            /* words taken by the runtime and blocks */
  uint32_t blocks_start;
  uint32_t buffer_target;
  uint32_t core_target;
  uint32_t routine_enter;
  uint32_t routine_exit;
  uint32_t routine_call;
  uint32_t routine_dropped;
  uint32_t routine_fast[8]; /* work RAM accesses by IR code, LD8 first */
  uint32_t generation;      /* counts flushes, to spot one mid-link */
  int count_instructions;   /* keep core.instructions (tests, stats) */
  int profiling;            /* count return stack and dispatch hits */
  int sampling;             /* blocks note their entry (flush to apply) */
  volatile uint32_t phase;  /* ARM_MIPS_PHASE_* */
  ArmMipsStats stats;
} ArmMips;

/* Sets up the core (arm_core_init) and the code buffer, whose start
   holds the runtime routines. bytes must leave room for blocks beyond
   ARM_MIPS_SCRATCH_WORDS; the buffer must sit within one 256 MB segment
   of the target address space. */
void arm_mips_init(ArmMips *jit, const ArmBus *bus, const ArmMipsHost *host,
 uint32_t *buffer, uint32_t bytes);

/* As arm_core_run, arm_core_run_block and arm_core_step, with generated
   code. The last two compile without linking and throw the code away. */
int32_t arm_mips_run(ArmMips *jit, int32_t cycles);
ArmExitKind arm_mips_run_block(ArmMips *jit);
int arm_mips_step(ArmMips *jit);

/* For a profiler sampling from another thread: the key (address | Thumb)
   of the block generated code entered last, 0 if unknown. Needs sampling
   set, and may be stale for a moment after a flush. */
uint32_t arm_mips_sampled_key(const ArmMips *jit);

/* The memory access generated code calls for what it cannot do itself:
   info is the op's code | flags << 8. */
uint32_t arm_mips_access(ArmCore *core, uint32_t address, uint32_t value,
 uint32_t info);

#endif
