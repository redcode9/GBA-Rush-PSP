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
 * Timer cases.
 *
 * A timer counts up once per 1, 64, 256 or 1024 system clock cycles, or,
 * in count-up mode, once per overflow of the timer before it. Starting it
 * (TMxCNT_H bit 7 going from 0 to 1) loads the reload value; an overflow
 * loads it again and sets the timer's IF bit if its IRQ is enabled.
 * Reading TMxCNT_L returns the counter, frozen while the timer is stopped
 * (GBATEK, GBA Timers). Cycle counts are GBATEK's for the ARM7TDMI, with
 * work RAM waitstates: IWRAM 1 cycle per access, EWRAM 3 per halfword and
 * 6 per word, I/O 1.
 *
 * timer<n>_p<divider>_<arm|thumb|ewram>: timer n free running from 0 at
 * the divider, read before and after a subs/bne loop of 1024 turns
 * (cases_timer_asm.s). In IWRAM the read costs 3 cycles and the loop
 * 1024 * 1 + 1023 * 3 + 1 = 4094, in ARM and in Thumb alike: 4097. ARM
 * code in EWRAM: the read 8, the loop 1024 * 6 + 1023 * 18 + 6 = 24564,
 * so 24572. The detail is the count read; with a divider it may be
 * either side of cycles / divider, as the prescaler need not start with
 * the loop.
 *
 * The other cases, detail in brackets:
 *   timer_start_delay      started at F/1 and read by the next
 *                          instruction (first read in bits 0-15), then
 *                          read 4 cycles later after a restart (16-31)
 *   timer_stop_hold        stopped 64 cycles after the start, then read
 *                          twice: the same value both times (bits 0-15,
 *                          16-31)
 *   timer_overflow_reload  reloads FFFFh and FFFCh at F/1: every read,
 *                          right after an overflow or not, lies in
 *                          [reload, FFFFh] (the first value outside)
 *   timer_cascade_live     timer 1 counting timer 0's overflows, every
 *                          256 cycles, read live across the loop (count)
 *   timer_many_overflows   the same every 64 cycles: each of the 64
 *                          overflows in the loop counts (count)
 *   timer_irq_on_running   the IRQ bit set on a running timer: the count
 *                          goes on (bits 0-15, 5 = the cycles between the
 *                          reads) and the IRQ comes at the next overflow
 *                          (bit 16)
 *   timer_prescale_running F/1 switched to F/64 while running: the count
 *                          goes on (bits 16-31, about 513) at the new
 *                          rate (bits 0-15, the loop)
 *
 * GBATEK gives no start delay. The two start cases assume the counter
 * holds the reload value at the cycle of the write and steps at each
 * cycle after it, so a load right after the store reads 1 (its opcode
 * fetch passed). Reports of hardware starting a cycle or two late would
 * read less: TIMER_FIRST_READ is what to change once mGBA and a real GBA
 * have run it.
 *
 * To confirm in mGBA and on hardware: every value here (GBATEK only).
 */

#include "result.h"

#define TIMER_L(n) (*(vu16 *)(0x04000100 + (n) * 4))
#define TIMER_H(n) (*(vu16 *)(0x04000102 + (n) * 4))
#define TIMER_START    0x0080
#define TIMER_IRQ      0x0040
#define TIMER_COUNT_UP 0x0004

#define TIMER_FIRST_READ 1
#define LOOP_TURNS 1024
#define LOOP_IWRAM 4097
#define LOOP_EWRAM 24572
/* Spins of a polling loop before giving up, a few frames. */
#define WAIT_LIMIT 100000u

__attribute__((long_call)) u32 timer_loop_arm(vu16 *counter, u32 turns);
__attribute__((long_call)) u32 timer_loop_thumb(vu16 *counter, u32 turns);
__attribute__((long_call)) u32 timer_loop_ewram(vu16 *counter, u32 turns);
__attribute__((long_call)) u32 timer_start_read(vu16 *counter);
__attribute__((long_call)) u32 timer_stop_read(vu16 *counter);
__attribute__((long_call)) u32 timer_irq_on(vu16 *counter);
__attribute__((long_call)) u32 timer_prescale_change(vu16 *counter);

/* Whether ticks of a 1/divider clock fit `cycles` cycles. */
static int ticks_match(u32 ticks, u32 cycles, u32 divider)
{
  return ticks == cycles / divider ||
   (cycles % divider != 0 && ticks == cycles / divider + 1);
}

static int timer_loop(u32 *detail, unsigned timer, unsigned prescaler,
 u32 (*loop)(vu16 *, u32), u32 cycles)
{
  static const u32 dividers[4] = { 1, 64, 256, 1024 };

  TIMER_L(timer) = 0;
  TIMER_H(timer) = TIMER_START | prescaler;
  *detail = loop(&TIMER_L(timer), LOOP_TURNS);
  TIMER_H(timer) = 0;
  return ticks_match(*detail, cycles, dividers[prescaler]) ?
   RESULT_PASS : RESULT_FAIL;
}

/* timer, prescaler, divider, name suffix, loop, its cycles */
#define PRESCALERS(X, t, s, loop, cycles) \
  X(t, 0, 1, s, loop, cycles) X(t, 1, 64, s, loop, cycles) \
  X(t, 2, 256, s, loop, cycles) X(t, 3, 1024, s, loop, cycles)
#define LOOP_CASES(X) \
  PRESCALERS(X, 0, arm, timer_loop_arm, LOOP_IWRAM) \
  PRESCALERS(X, 1, arm, timer_loop_arm, LOOP_IWRAM) \
  PRESCALERS(X, 2, arm, timer_loop_arm, LOOP_IWRAM) \
  PRESCALERS(X, 3, arm, timer_loop_arm, LOOP_IWRAM) \
  PRESCALERS(X, 0, thumb, timer_loop_thumb, LOOP_IWRAM) \
  PRESCALERS(X, 1, thumb, timer_loop_thumb, LOOP_IWRAM) \
  PRESCALERS(X, 2, thumb, timer_loop_thumb, LOOP_IWRAM) \
  PRESCALERS(X, 3, thumb, timer_loop_thumb, LOOP_IWRAM) \
  PRESCALERS(X, 0, ewram, timer_loop_ewram, LOOP_EWRAM)

#define LOOP_FUNCTION(t, p, d, s, loop, cycles) \
  static int timer##t##_p##d##_##s(u32 *detail) \
  { \
    return timer_loop(detail, t, p, loop, cycles); \
  }
#define LOOP_ENTRY(t, p, d, s, loop, cycles) \
  { "timer" #t "_p" #d "_" #s, timer##t##_p##d##_##s },

LOOP_CASES(LOOP_FUNCTION)

static int timer_start_delay(u32 *detail)
{
  u32 first;

  TIMER_L(0) = 0;
  *detail = timer_start_read(&TIMER_L(0));
  TIMER_H(0) = 0;
  first = *detail & 0xFFFF;
  return first == TIMER_FIRST_READ && *detail >> 16 == first + 4 ?
   RESULT_PASS : RESULT_FAIL;
}

static int timer_stop_hold(u32 *detail)
{
  u32 held;

  TIMER_L(0) = 0;
  *detail = timer_stop_read(&TIMER_L(0));
  held = *detail & 0xFFFF;
  return held == 63 + TIMER_FIRST_READ && *detail >> 16 == held ?
   RESULT_PASS : RESULT_FAIL;
}

static int timer_overflow_reload(u32 *detail)
{
  static const u16 reloads[2] = { 0xFFFF, 0xFFFC };
  unsigned r;
  unsigned i;

  for(r = 0; r < 2; r++)
  {
    TIMER_L(0) = reloads[r];
    TIMER_H(0) = TIMER_START;
    for(i = 0; i < 32 && *detail == 0; i++)
    {
      u32 value = TIMER_L(0);

      if(value < reloads[r])
        *detail = value | 0x10000;
    }
    TIMER_H(0) = 0;
  }
  return *detail == 0 ? RESULT_PASS : RESULT_FAIL;
}

/* Timer 1 counts the overflows of timer 0, which runs at F/1 from
   reload; returns whether the count across the loop is right. */
static int count_up(u32 *detail, u16 reload)
{
  TIMER_L(1) = 0;
  TIMER_H(1) = TIMER_START | TIMER_COUNT_UP;
  TIMER_L(0) = reload;
  TIMER_H(0) = TIMER_START;
  *detail = timer_loop_arm(&TIMER_L(1), LOOP_TURNS);
  TIMER_H(0) = 0;
  TIMER_H(1) = 0;
  return ticks_match(*detail, LOOP_IWRAM, 0x10000 - reload) ?
   RESULT_PASS : RESULT_FAIL;
}

static int timer_cascade_live(u32 *detail)
{
  return count_up(detail, 0xFF00);
}

static int timer_many_overflows(u32 *detail)
{
  return count_up(detail, 0xFFC0);
}

static int timer_irq_on_running(u32 *detail)
{
  u32 n;

  TIMER_L(0) = 0;
  TIMER_H(0) = TIMER_START;
  *detail = timer_irq_on(&TIMER_L(0));
  for(n = 0; n < WAIT_LIMIT && !(REG_IF & IRQ_TIMER0); n++)
    ;
  TIMER_H(0) = 0;
  if(REG_IF & IRQ_TIMER0)
    *detail |= 0x10000;
  return *detail == (5 | 0x10000) ? RESULT_PASS : RESULT_FAIL;
}

static int timer_prescale_running(u32 *detail)
{
  u32 first;

  TIMER_L(0) = 0;
  *detail = timer_prescale_change(&TIMER_L(0));
  TIMER_H(0) = 0;
  first = *detail >> 16;
  return ticks_match(*detail & 0xFFFF, LOOP_IWRAM, 64) &&
   first >= 513 - 8 && first <= 513 + 8 ? RESULT_PASS : RESULT_FAIL;
}

const TestCase cases_timer[] =
{
  LOOP_CASES(LOOP_ENTRY)
  { "timer_start_delay", timer_start_delay },
  { "timer_stop_hold", timer_stop_hold },
  { "timer_overflow_reload", timer_overflow_reload },
  { "timer_cascade_live", timer_cascade_live },
  { "timer_many_overflows", timer_many_overflows },
  { "timer_irq_on_running", timer_irq_on_running },
  { "timer_prescale_running", timer_prescale_running },
  { NULL, NULL }
};
