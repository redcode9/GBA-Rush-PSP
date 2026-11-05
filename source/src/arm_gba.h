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
 * The new ARM7TDMI core (arm_core.h, compiled to MIPS by arm_mips.h) as
 * the GBA's CPU, the "New" choice of the CPU core option. arm_gba.c says
 * how it plugs into the rest of the emulator.
 */

#ifndef ARM_GBA_H
#define ARM_GBA_H

/* Runs the game from the CPU state in reg[], reg_mode[] and spsr[], as
   the Classic core's execute_arm_translate does; never returns. */
void arm_gba_run(void);

/* Cycles the core has run since update_gba last advanced cpu_ticks: 0
   outside its runs, which is always while the Classic core runs. */
u32 arm_gba_cycles_run(void);

/* Copies the core's registers to reg[], reg_mode[] and spsr[], where
   savestates read them (cpu_write_mem_savestate). */
void arm_gba_save_registers(void);

/* Work RAM written behind the core's back (DMA, cheats): drops the code
   translated from it. Does nothing while the Classic core runs. */
void arm_gba_written(u32 address, u32 size);

/* Blocks the new core compiled and times it threw all its code away (a
   full code buffer, a WAITCNT change, a reset or a loaded state) since it
   started; both 0 while the Classic core runs. */
void arm_gba_counters(u32 *blocks, u32 *flushes);

/* The address where CodeBreaker cheats run (0: once per frame instead);
   the code there runs process_cheats_at_hook() first. */
void arm_gba_set_cheat_hook(u32 address);

#ifdef ENABLE_DIAGNOSTIC_CORE_PROFILE
#include "core_profile.h"

/* Diagnostic builds: a sampling profile of the new core over a benchmark
   capture (core_profile.h). start clears it and starts sampling, stop
   ends it, and format writes its report: -1 if it does not fit or the new
   core was not running. pause(1) skips the samples until pause(0), so the
   time the menu is open does not count. */
void arm_gba_profile_start(void);
void arm_gba_profile_stop(void);
void arm_gba_profile_pause(int pause);
int arm_gba_profile_format(char *out, u32 size);
#endif

#endif /* ARM_GBA_H */
