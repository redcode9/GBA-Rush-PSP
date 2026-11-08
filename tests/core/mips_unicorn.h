/*
 * Runs the code arm_mips.c generates in Unicorn (unicorn-engine.org), a
 * MIPS32 emulator, so the backend can be tested on any host. Stock Unicorn
 * lacks the Allegrex's clz, clo, max and min (SPECIAL functions 0x16, 0x17,
 * 0x2C and 0x2D), so a hook runs them unless scripts/build-unicorn.sh
 * patched them in. scripts/check-core.sh says how to build the tests.
 */

#ifndef MIPS_UNICORN_H
#define MIPS_UNICORN_H

#include <stddef.h>

#include "arm_mips.h"

void mips_unicorn_init(void);
/* Zeroed host memory that can be mapped: page aligned, whole pages. */
void *mips_unicorn_alloc(size_t bytes);
/* Maps host memory at a target address (both page aligned). */
void mips_unicorn_map(void *host, size_t bytes, uint32_t target);
/* Callbacks for arm_mips_init; jit is the one Unicorn runs. */
ArmMipsHost mips_unicorn_host(ArmMips *jit);
/* Counts the MIPS instructions run in mips_unicorn_executed, and calls
   block (when set) with the address and size of each run of them. */
void mips_unicorn_count(void (*block)(uint32_t address, uint32_t size));
extern unsigned long long mips_unicorn_executed;
/* Calls of arm_mips_access, the slow memory path, and of them by address
   bits 24-27 (and loads and stores by bit 4 of info, as the IR codes). */
extern unsigned long long mips_unicorn_calls;
extern unsigned long long mips_unicorn_region_calls[16][2];

#endif
