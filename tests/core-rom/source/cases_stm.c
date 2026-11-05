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
 * Multiple store cases.
 *
 * Every word of a block store (ARM STM, Thumb STMIA and PUSH) is an
 * ordinary write: one that changes code makes the new code run the next
 * time it is entered, and one that lands on I/O acts at once, whichever
 * word of the store it is. Routines that have run are rewritten so that
 * the code changes in a word other than the last one stored
 * (cases_stm_asm.s). The detail is the routine's result before (1) and
 * after (2, in bits 8-15) the store, unless said otherwise.
 *
 *   stm_iwram_below_sp  ARM STM of three words, the last on data, over
 *                       a routine in IWRAM below SP
 *   stm_iwram_above_sp  the same with SP moved below the routine, the
 *                       base register another one
 *   stm_ewram           the same in EWRAM
 *   stm_vram            the same in VRAM (06015000h)
 *   stmia_thumb_ewram   Thumb STMIA of two words, the last on data
 *   stmia_thumb_iwram   the same in IWRAM
 *   stm_two_routines    one STM over the two words of one routine and the
 *                       first word of the next: 1 and 3 before (bytes 0
 *                       and 1), 2 and 4 after (bytes 2 and 3)
 *   stm_cpu_fast_set    CpuFastSet (SWI 0Ch) over a routine in IWRAM; the
 *                       official BIOS copies with LDMIA/STMIA of eight
 *                       words, the built-in one word by word
 *   push_lr_code        Thumb PUSH {r4, lr} with SP moved into a routine,
 *                       rewriting its second word (LR's), not the one it
 *                       starts at; in GBA Rush, with the stack
 *                       optimization off
 *   stm_timer_start     timer 0 started by the first word of an STM and
 *                       read by the next instruction (bits 0-15), then by
 *                       a single STR (bits 16-31): the STM's second word
 *                       takes one more cycle, so the first read is one
 *                       more than the second
 *   stm_irq_enable      IME set by the first word of an STM with a
 *                       VBlank IRQ pending, then by a single STR: the
 *                       IRQ comes within the eight instructions after
 *                       either store, and at most one instruction apart
 *                       (instructions run before it: STM in bits 0-7,
 *                       STR in 8-15; FFh if it never came)
 *
 * To confirm in mGBA and on hardware.
 */

#include "result.h"

#define VRAM_CODE ((u32 *)0x06015000)
#define TM0CNT ((vu32 *)0x04000100)
#define IME32 ((vu32 *)0x04000208)
#define TIMER_START 0x0080
/* Spins of a polling loop before giving up, a few frames. */
#define WAIT_LIMIT 100000u

extern u32 stm_code_low[], stm_code_high[], stm_code_ewram[];
extern u32 stm_fast_code[8];
extern const u32 stm_fast_new[8], stm_arm_old[3];
extern u16 stm_thumb_iwram[], stm_thumb_ewram[];
volatile u32 irq_mark;

u32 stm_rewrite(u32 *code, u32 lower_sp);
u32 stm_pair_rewrite(void);
u32 stm_thumb_rewrite(u16 *code);
u32 stm_push_rewrite(void);
void stm_irq_handler(void);
__attribute__((long_call)) u32 stm_timer_start(vu32 *tm0cnt);
__attribute__((long_call)) void stm_irq_enable_stm(vu32 *ime);
__attribute__((long_call)) void stm_irq_enable_str(vu32 *ime);

static int rewritten(u32 *detail, u32 result)
{
  *detail = result;
  return result == 0x0201 ? RESULT_PASS : RESULT_FAIL;
}

static int stm_iwram_below_sp(u32 *detail)
{
  return rewritten(detail, stm_rewrite(stm_code_low, 0));
}

static int stm_iwram_above_sp(u32 *detail)
{
  return rewritten(detail, stm_rewrite(stm_code_high, 1));
}

static int stm_ewram(u32 *detail)
{
  return rewritten(detail, stm_rewrite(stm_code_ewram, 0));
}

static int stm_vram(u32 *detail)
{
  unsigned i;

  for(i = 0; i < 3; i++)
    VRAM_CODE[i] = stm_arm_old[i];
  return rewritten(detail, stm_rewrite(VRAM_CODE, 0));
}

static int stmia_thumb_ewram(u32 *detail)
{
  return rewritten(detail, stm_thumb_rewrite(stm_thumb_ewram));
}

static int stmia_thumb_iwram(u32 *detail)
{
  return rewritten(detail, stm_thumb_rewrite(stm_thumb_iwram));
}

static int stm_two_routines(u32 *detail)
{
  *detail = stm_pair_rewrite();
  return *detail == 0x04020301 ? RESULT_PASS : RESULT_FAIL;
}

/* CpuFastSet (SWI 0Ch) copying words, a multiple of 8. */
static void cpu_fast_set(const void *source, void *destination, u32 words)
{
  register u32 r0 __asm__("r0") = (u32)source;
  register u32 r1 __asm__("r1") = (u32)destination;
  register u32 r2 __asm__("r2") = words;

  __asm__ volatile("swi 0x0C" : "+r"(r0), "+r"(r1), "+r"(r2) : :
   "r3", "r12", "memory");
}

static int stm_cpu_fast_set(u32 *detail)
{
  u32 (*run)(void) = (u32 (*)(void))(u32)stm_fast_code;

  *detail = run();
  cpu_fast_set(stm_fast_new, stm_fast_code, 8);
  *detail |= run() << 8;
  return *detail == 0x0201 ? RESULT_PASS : RESULT_FAIL;
}

static int push_lr_code(u32 *detail)
{
  return rewritten(detail, stm_push_rewrite());
}

static int stm_timer_start_case(u32 *detail)
{
  *detail = stm_timer_start(TM0CNT);
  *TM0CNT = 0;
  return (*detail & 0xFFFF) == (*detail >> 16) + 1 ?
   RESULT_PASS : RESULT_FAIL;
}

static int stm_irq_enable(u32 *detail)
{
  u32 marks[2];
  unsigned i;
  u32 n;

  IRQ_VECTOR = stm_irq_handler;
  REG_DISPSTAT = DISPSTAT_VBLANK_IRQ;
  REG_IE = IRQ_VBLANK;
  for(i = 0; i < 2; i++)
  {
    irq_mark = 0xFF;
    REG_IF = IRQ_VBLANK;
    for(n = 0; n < WAIT_LIMIT && !(REG_IF & IRQ_VBLANK); n++)
      ;
    (i == 0 ? stm_irq_enable_stm : stm_irq_enable_str)(IME32);
    marks[i] = irq_mark;
  }
  *detail = marks[0] | marks[1] << 8;
  return marks[0] <= 8 && marks[1] <= 8 && marks[0] <= marks[1] + 1 &&
   marks[1] <= marks[0] + 1 ? RESULT_PASS : RESULT_FAIL;
}

const TestCase cases_stm[] =
{
  { "stm_iwram_below_sp", stm_iwram_below_sp },
  { "stm_iwram_above_sp", stm_iwram_above_sp },
  { "stm_ewram", stm_ewram },
  { "stm_vram", stm_vram },
  { "stmia_thumb_ewram", stmia_thumb_ewram },
  { "stmia_thumb_iwram", stmia_thumb_iwram },
  { "stm_two_routines", stm_two_routines },
  { "stm_cpu_fast_set", stm_cpu_fast_set },
  { "push_lr_code", push_lr_code },
  { "stm_timer_start", stm_timer_start_case },
  { "stm_irq_enable", stm_irq_enable },
  { NULL, NULL }
};
