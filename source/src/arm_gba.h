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

/* Copies the core's registers to reg[], reg_mode[] and spsr[], where
   savestates read them (cpu_write_mem_savestate). */
void arm_gba_save_registers(void);

/* Work RAM written behind the core's back (DMA, cheats): drops the code
   translated from it. Does nothing while the Classic core runs. */
void arm_gba_written(u32 address, u32 size);

/* The address where CodeBreaker cheats run (0: once per frame instead);
   the code there runs process_cheats_at_hook() first. */
void arm_gba_set_cheat_hook(u32 address);

#endif /* ARM_GBA_H */
