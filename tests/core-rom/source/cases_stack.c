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
 * Stack cases.
 *
 * ewram_stack: block transfers based on SP with SP in the on-board work
 * RAM (EWRAM), as some games keep it. ARM STMFD/LDMFD and Thumb PUSH/POP
 * (cases_stack_asm.s) must store to and load from EWRAM. The stack top
 * 0203FF40h is chosen so that SP & 7FFCh falls in 03007F00h-03007FFFh: a
 * core whose stack fast path assumes the on-chip RAM (IWRAM) writes there
 * instead, over the IRQ stack. The detail has a bit per check that failed:
 *   0  ARM STMFD did not land in EWRAM
 *   1  ARM LDMFD did not load what is in EWRAM
 *   2  Thumb PUSH did not land in EWRAM
 *   3  Thumb POP did not load what is in EWRAM
 *   4  IWRAM 03007F00h-03007FFFh changed
 * To confirm in mGBA and on hardware.
 */

#include "result.h"

#define STACK_TOP ((vu32 *)0x0203FF40)
/* The 64 bytes under STACK_TOP. */
#define STACK_LOW ((vu32 *)0x0203FF00)
#define IWRAM_TOP ((vu32 *)0x03007F00)

/* io: 8 words to push, then the 8 popped from 64 bytes below top. */
void stack_arm(volatile u32 *top, u32 *io);
/* io: 6 words to push, then the 6 popped from 48 bytes below top. */
void stack_thumb(volatile u32 *top, u32 *io);

static u32 iwram_copy[64];

static int ewram_stack(u32 *detail)
{
  vu32 *low = STACK_LOW;
  u32 io[16];
  u32 failed = 0;
  u32 i;

  for(i = 0; i < 64; i++)
    iwram_copy[i] = IWRAM_TOP[i];

  for(i = 0; i < 8; i++)
  {
    io[i] = 0xA5A50000u | i;
    low[8 + i] = 0;
    low[i] = 0x5A5A0000u | i;
  }
  stack_arm(STACK_TOP, io);
  for(i = 0; i < 8; i++)
  {
    if(low[8 + i] != io[i])
      failed |= 1;
    if(io[8 + i] != (0x5A5A0000u | i))
      failed |= 2;
  }

  for(i = 0; i < 6; i++)
  {
    io[i] = 0xC3C30000u | i;
    low[10 + i] = 0;
    low[4 + i] = 0x3C3C0000u | i;
  }
  stack_thumb(STACK_TOP, io);
  for(i = 0; i < 6; i++)
  {
    if(low[10 + i] != io[i])
      failed |= 4;
    if(io[6 + i] != (0x3C3C0000u | i))
      failed |= 8;
  }

  for(i = 0; i < 64; i++)
    if(IWRAM_TOP[i] != iwram_copy[i])
      failed |= 16;

  *detail = failed;
  return failed ? RESULT_FAIL : RESULT_PASS;
}

const TestCase cases_stack[] =
{
  { "ewram_stack", ewram_stack },
  { NULL, NULL }
};
