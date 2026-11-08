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
 * Program counter cases.
 *
 * pc_high: a BX to 10000000h plus the address of ROM code, in ARM and in
 * Thumb state (cases_pc_asm.s). GBATEK's memory map says the upper 4 bits
 * of the address bus are not used, which reads as the jump running the
 * ROM code there; other sources treat 10000000h and up as open bus, where
 * the CPU would run garbage and the case would hang. The case passes with
 * the first reading: detail bit 0 is set when the ARM code ran up there,
 * bit 1 for Thumb. Which reading is right is open until a real GBA runs
 * it (docs/DESIGN-DECISIONS.md).
 */

#include "result.h"

u32 pc_high_arm(void);
u32 pc_high_thumb(void);

static int pc_high(u32 *detail)
{
  *detail = pc_high_arm();
  *detail |= pc_high_thumb();
  return *detail == 3 ? RESULT_PASS : RESULT_FAIL;
}

const TestCase cases_pc[] =
{
  { "pc_high", pc_high },
  { NULL, NULL }
};
