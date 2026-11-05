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
 * Branch timing cases.
 *
 * branch_<state>_<kind>_<region>: one branch to the next instruction,
 * timed by timer 0 at F/1 between two reads of TM0CNT_L, with the code in
 * IWRAM or in EWRAM (cases_branch_asm.s). The detail is the cycles
 * counted: the first read plus the branch.
 *
 * Cycles from GBATEK's ARM7TDMI tables, where every S and N of a branch
 * is an opcode fetch from the code's region: F is 1 in IWRAM, 6 for an
 * ARM opcode and 3 for a Thumb one in EWRAM. The first read (LDRH from
 * I/O) costs 1S + 1N + 1I = F + 2. Then:
 *   B, BL, taken Bcc, BX, MOV pc   2S + 1N               3F
 *   Bcc not taken, skipped ARM
 *   instruction (MOVNE)            1S                    F
 *   LDR pc                         2S + 2N + 1I          3F + D + 1
 *   LDM {r12, pc}                  3S + 2N + 1I          3F + 2D + 1
 *   Thumb BL (two halves)          1S, then 2S + 1N      4F
 *   Thumb POP {pc}                 2S + 2N + 1I          3F + 2
 * D is a data word from the code's region, 1 cycle in IWRAM and 6 in
 * EWRAM; the stack POP reads is in IWRAM.
 *
 * To confirm in mGBA and on hardware: every value here (GBATEK only).
 */

#include "result.h"

#define TM0CNT_L ((vu16 *)0x04000100)
#define TM0CNT_H (*(vu16 *)0x04000102)
#define TIMER_START 0x0080

#define READ(f) ((f) + 2)

#define BRANCH_CASES(X) \
  X(arm_b_iwram, READ(1) + 3) \
  X(arm_bl_iwram, READ(1) + 3) \
  X(arm_beq_iwram, READ(1) + 3) \
  X(arm_bne_iwram, READ(1) + 1) \
  X(arm_bx_iwram, READ(1) + 3) \
  X(arm_movpc_iwram, READ(1) + 3) \
  X(arm_ldrpc_iwram, READ(1) + 3 + 1 + 1) \
  X(arm_ldmpc_iwram, READ(1) + 3 + 2 + 1) \
  X(arm_skip_iwram, READ(1) + 1) \
  X(arm_b_ewram, READ(6) + 18) \
  X(arm_bl_ewram, READ(6) + 18) \
  X(arm_beq_ewram, READ(6) + 18) \
  X(arm_bne_ewram, READ(6) + 6) \
  X(arm_bx_ewram, READ(6) + 18) \
  X(arm_movpc_ewram, READ(6) + 18) \
  X(arm_ldrpc_ewram, READ(6) + 18 + 6 + 1) \
  X(arm_ldmpc_ewram, READ(6) + 18 + 12 + 1) \
  X(arm_skip_ewram, READ(6) + 6) \
  X(thumb_b_iwram, READ(1) + 3) \
  X(thumb_beq_iwram, READ(1) + 3) \
  X(thumb_bne_iwram, READ(1) + 1) \
  X(thumb_bl_iwram, READ(1) + 4) \
  X(thumb_bx_iwram, READ(1) + 3) \
  X(thumb_poppc_iwram, READ(1) + 3 + 2) \
  X(thumb_b_ewram, READ(3) + 9) \
  X(thumb_beq_ewram, READ(3) + 9) \
  X(thumb_bne_ewram, READ(3) + 3) \
  X(thumb_bl_ewram, READ(3) + 12) \
  X(thumb_bx_ewram, READ(3) + 9) \
  X(thumb_poppc_ewram, READ(3) + 9 + 2)

static int timed(u32 *detail, u32 (*branch)(vu16 *), u32 cycles)
{
  *TM0CNT_L = 0;
  TM0CNT_H = TIMER_START;
  *detail = branch(TM0CNT_L);
  TM0CNT_H = 0;
  return *detail == cycles ? RESULT_PASS : RESULT_FAIL;
}

#define BRANCH_FUNCTION(name, cycles) \
  __attribute__((long_call)) u32 branch_##name(vu16 *counter); \
  static int run_##name(u32 *detail) \
  { \
    return timed(detail, branch_##name, cycles); \
  }
#define BRANCH_ENTRY(name, cycles) { "branch_" #name, run_##name },

BRANCH_CASES(BRANCH_FUNCTION)

const TestCase cases_branch[] =
{
  BRANCH_CASES(BRANCH_ENTRY)
  { NULL, NULL }
};
