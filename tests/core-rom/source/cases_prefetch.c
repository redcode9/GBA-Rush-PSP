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
 * Prefetch window cases.
 *
 * The ARM7TDMI's pipeline has fetched the two instructions after the one
 * it executes, so a store into either of them does not change what runs
 * this time; a store into the third one does (ARM7TDMI data sheet, the
 * pipeline; GBATEK, CPU Overview). The routines are in IWRAM
 * (cases_prefetch_asm.s); the detail is what they return.
 *
 * prefetch_<arm|thumb>_<n>_<cold|warm>: a STR (ARM) or STRH (Thumb) into
 * the instruction n after it, which returns 1 in its old version and 2 in
 * the new one: 1 for n = 1 and 2, 2 for n = 3. "cold" is the routine's
 * first run; "warm" runs another copy twice and checks the second run,
 * which meets code that ran, was rewritten and put back before.
 *
 * prefetch_stm, prefetch_push, prefetch_swp: an ARM STM of two words, a
 * Thumb PUSH of one word (two instructions) and an ARM SWP into the
 * instructions right after them: the old versions run, 3, 3 and 1.
 *
 * mGBA 0.10.3 passes every case. To confirm on hardware.
 */

#include "result.h"

#define PREFETCH_CASES(X) \
  X(arm_1, 1) X(arm_2, 1) X(arm_3, 2) \
  X(thumb_1, 1) X(thumb_2, 1) X(thumb_3, 2)

#define PREFETCH_FUNCTIONS(name, expected) \
  __attribute__((long_call)) u32 prefetch_##name##_cold(void); \
  __attribute__((long_call)) u32 prefetch_##name##_warm(void); \
  static int run_##name##_cold(u32 *detail) \
  { \
    *detail = prefetch_##name##_cold(); \
    return *detail == expected ? RESULT_PASS : RESULT_FAIL; \
  } \
  static int run_##name##_warm(u32 *detail) \
  { \
    prefetch_##name##_warm(); \
    *detail = prefetch_##name##_warm(); \
    return *detail == expected ? RESULT_PASS : RESULT_FAIL; \
  }
#define PREFETCH_ENTRIES(name, expected) \
  { "prefetch_" #name "_cold", run_##name##_cold }, \
  { "prefetch_" #name "_warm", run_##name##_warm },

PREFETCH_CASES(PREFETCH_FUNCTIONS)

__attribute__((long_call)) u32 prefetch_stm(void);
__attribute__((long_call)) u32 prefetch_push(void);
__attribute__((long_call)) u32 prefetch_swp(void);

static int run_stm(u32 *detail)
{
  *detail = prefetch_stm();
  return *detail == 3 ? RESULT_PASS : RESULT_FAIL;
}

static int run_push(u32 *detail)
{
  *detail = prefetch_push();
  return *detail == 3 ? RESULT_PASS : RESULT_FAIL;
}

static int run_swp(u32 *detail)
{
  *detail = prefetch_swp();
  return *detail == 1 ? RESULT_PASS : RESULT_FAIL;
}

const TestCase cases_prefetch[] =
{
  PREFETCH_CASES(PREFETCH_ENTRIES)
  { "prefetch_stm", run_stm },
  { "prefetch_push", run_push },
  { "prefetch_swp", run_swp },
  { NULL, NULL }
};
