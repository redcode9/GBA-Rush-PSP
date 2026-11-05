/*
 * Host tests for gba_timer.h, the timer arithmetic of main.c: TMxCNT_L at
 * any cycle between update_gba passes, overflows caught up however late a
 * pass comes, and TMxCNT_H writes that start, stop or change a running
 * timer at the cycle of the write. The model below drives the header the
 * way main.c does (timer_left, timer_read, timer_control_high and
 * UPDATE_TIMER) and is checked against timers stepped one cycle at a
 * time.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "gba_timer.h"

#define FUZZ_OPERATIONS 100000u

enum { OFF, CYCLES, COUNT_UP };

static const unsigned shifts[4] = { 0, 6, 8, 10 };

/* ---- Reference: two timers stepped one cycle at a time ---------------- */

typedef struct
{
  int on;
  int count_up;
  unsigned shift;
  uint32_t reload;    /* ticks per period */
  uint32_t counter;   /* TMxCNT_L */
  uint32_t phase;     /* cycles since the last tick */
  uint32_t overflows;
} RefTimer;

static RefTimer ref[2];

static void ref_tick(unsigned n)
{
  RefTimer *t = &ref[n];

  if(++t->counter < 0x10000)
    return;
  t->counter = 0x10000 - t->reload;
  t->overflows++;
  if(n == 0 && ref[1].on && ref[1].count_up)
    ref_tick(1);
}

static void ref_cycles(uint32_t cycles)
{
  unsigned n;

  while(cycles--)
    for(n = 0; n < 2; n++)
      if(ref[n].on && !ref[n].count_up &&
         ++ref[n].phase == 1u << ref[n].shift)
      {
        ref[n].phase = 0;
        ref_tick(n);
      }
}

/* A start loads the reload value; a new rate or mode on a running timer
   starts its tick phase again from the write. */
static void ref_control(unsigned n, uint32_t value)
{
  RefTimer *t = &ref[n];
  int count_up = n != 0 && (value & 0x04) != 0;
  unsigned shift = count_up ? 0 : shifts[value & 3];

  if(!(value & 0x80))
  {
    t->on = 0;
    return;
  }
  if(!t->on)
  {
    t->counter = 0x10000 - t->reload;
    t->phase = 0;
  }
  else if(count_up != t->count_up || shift != t->shift)
    t->phase = 0;
  t->on = 1;
  t->count_up = count_up;
  t->shift = shift;
}

/* ---- Model: the header driven as main.c drives it --------------------- */

typedef struct
{
  int status;
  unsigned shift;
  uint32_t reload;
  int32_t count;      /* count-up: overflows of timer 0 left, at the pass */
  uint32_t deadline;  /* counts cycles: the cycle of the next overflow */
  uint32_t held;      /* TMxCNT_L once stopped */
  uint32_t served;    /* overflows the passes served (IRQs) */
} ModelTimer;

static ModelTimer model[2];
static uint32_t ticks;  /* cpu_ticks: the cycle of the last pass */

/* main.c's timer_left. */
static int32_t model_left(unsigned n, uint32_t now)
{
  ModelTimer *t = &model[n];
  int32_t before;
  int32_t left;

  if(t->status == CYCLES)
    return (int32_t)(t->deadline - now);
  left = t->count;
  if(n != 0 && model[0].status != OFF)
  {
    before = model_left(0, now);
    left -= (int32_t)gba_timer_catch_up(&before, model[0].reload,
     model[0].shift);
  }
  return left;
}

/* main.c's timer_read. */
static uint32_t model_read(unsigned n, uint32_t now)
{
  ModelTimer *t = &model[n];

  if(t->status == OFF)
    return t->held;
  return gba_timer_counter(model_left(n, now), t->reload, t->shift);
}

/* main.c's timer_control_high. */
static void model_control(unsigned n, uint32_t value, uint32_t now)
{
  ModelTimer *t = &model[n];
  int running = t->status != OFF;
  int status = CYCLES;
  unsigned shift = shifts[value & 3];
  int32_t left = 0;

  if(running)
    left = model_left(n, now);
  if(!(value & 0x80))
  {
    if(running)
      t->held = gba_timer_counter(left, t->reload, t->shift);
    t->status = OFF;
    return;
  }
  if(n != 0 && (value & 0x04))
  {
    status = COUNT_UP;
    shift = 0;
  }
  if(running && status == t->status && shift == t->shift)
    return;
  left = gba_timer_written(running, left, t->reload, t->shift, shift);
  t->status = status;
  t->shift = shift;
  if(status == CYCLES)
    t->deadline = now + (uint32_t)left;
  else
    t->count = left;
}

/* main.c's UPDATE_TIMER for both timers, at a pass on cycle now. */
static void model_pass(uint32_t now)
{
  unsigned n;

  ticks = now;
  for(n = 0; n < 2; n++)
  {
    ModelTimer *t = &model[n];
    int32_t left;
    uint32_t overflows;

    if(t->status == CYCLES)
      t->count = (int32_t)(t->deadline - ticks);
    if(t->status == OFF)
      continue;
    left = t->count;
    overflows = gba_timer_catch_up(&left, t->reload, t->shift);
    if(!overflows)
      continue;
    t->served += overflows;
    if(n == 0 && model[1].status == COUNT_UP)
      model[1].count -= (int32_t)overflows;
    t->count = left;
    if(t->status == CYCLES)
      t->deadline = ticks + (uint32_t)left;
  }
}

static void reset(uint32_t now)
{
  unsigned n;

  for(n = 0; n < 2; n++)
  {
    RefTimer r = { 0, 0, 0, 0x10000, 0, 0, 0 };
    ModelTimer m = { OFF, 0, 0x10000, 0, 0, 0, 0 };

    ref[n] = r;
    model[n] = m;
  }
  ticks = now;
}

static void set_reload(unsigned n, uint32_t value)
{
  ref[n].reload = 0x10000 - value;
  model[n].reload = 0x10000 - value;
}

static uint32_t fuzz_state = 0x2545F491u;

static uint32_t fuzz_random(void)
{
  fuzz_state ^= fuzz_state << 13;
  fuzz_state ^= fuzz_state >> 17;
  fuzz_state ^= fuzz_state << 5;
  return fuzz_state;
}

/* Whether a running timer has an overflow due that no pass has served. */
static int model_overdue(uint32_t now)
{
  unsigned n;

  for(n = 0; n < 2; n++)
    if(model[n].status != OFF && model_left(n, now) <= 0)
      return 1;
  return 0;
}

/* ---- Cases ------------------------------------------------------------ */

static void test_catch_up(void)
{
  int32_t left;

  left = 5;
  assert(gba_timer_catch_up(&left, 4, 0) == 0 && left == 5);
  left = 0;
  assert(gba_timer_catch_up(&left, 4, 0) == 1 && left == 4);
  left = -4;
  assert(gba_timer_catch_up(&left, 4, 0) == 2 && left == 4);
  left = -5;
  assert(gba_timer_catch_up(&left, 4, 0) == 2 && left == 3);
  /* F/64, 2 ticks per period: 200 cycles late is 1 + 200 / 128 overflows,
     the next one 128 - 72 cycles away. */
  left = -200;
  assert(gba_timer_catch_up(&left, 2, 6) == 2 && left == 56);
  /* A period of one cycle, 960 cycles late. */
  left = -959;
  assert(gba_timer_catch_up(&left, 1, 0) == 960 && left == 1);
}

/* The live read: a timer started from FFF0h at F/64 reads its reload value
   for the first 64 cycles, then one more every 64 cycles, and FFFFh, not
   0, in the last cycles before an overflow. */
static void test_live_read(void)
{
  uint32_t reload = 0x10;
  uint32_t period = reload << 6;
  uint32_t c;
  unsigned shift;

  for(c = 0; c < period; c++)
    assert(gba_timer_counter((int32_t)(period - c), reload, 6) ==
     0xFFF0 + c / 64);
  assert(gba_timer_counter(1, reload, 6) == 0xFFFF);
  assert(gba_timer_counter(0, reload, 6) == 0xFFF0);
  assert(gba_timer_counter(-64, reload, 6) == 0xFFF1);
  for(shift = 0; shift <= 10; shift += 2)
  {
    int32_t left;

    for(left = -3 * (int32_t)(3 << shift); left <= (int32_t)(3 << shift);
        left++)
    {
      uint32_t value = gba_timer_counter(left, 3, shift);

      assert(value >= 0x10000 - 3 && value <= 0xFFFF);
    }
  }
}

/* A start and a stop halfway through a slice: the count runs from the
   write's cycle, not from the pass before it, and holds the stop's. */
static void test_start_stop_at_write(void)
{
  reset(1000);
  set_reload(0, 0xFF00);
  model_pass(1000);
  model_control(0, 0x81, 1050);           /* F/64 at cycle 1050 */
  assert(model_read(0, 1050) == 0xFF00);
  assert(model_read(0, 1050 + 64 * 3 - 20) == 0xFF02);
  assert(model_read(0, 1050 + 64 * 3) == 0xFF03);
  model_pass(1300);
  model_control(0, 0x00, 1050 + 64 * 5 + 10);
  assert(model_read(0, 1050 + 64 * 5 + 10) == 0xFF05);
  assert(model_read(0, 5000) == 0xFF05);
  model_pass(6000);
  assert(model_read(0, 7000) == 0xFF05 && model[0].served == 0);
}

/* A write to a running timer: its count goes on, at the new rate from the
   write; one that changes only the IRQ bit leaves the ticks where they
   were. */
static void test_running_write(void)
{
  reset(0);
  set_reload(0, 0);
  model_control(0, 0x80, 0);              /* F/1 from 0 */
  model_control(0, 0x81, 513);            /* F/64 at 513 */
  assert(model_read(0, 516) == 513);
  assert(model_read(0, 513 + 4097) == 513 + 64);
  reset(0);
  set_reload(0, 0);
  model_control(0, 0x81, 0);              /* F/64 from 0 */
  model_control(0, 0xC1, 100);            /* IRQ on at 100 */
  assert(model_read(0, 127) == 1 && model_read(0, 128) == 2);
  model_control(1, 0x80, 0);
  model_control(1, 0x84, 300);            /* count-up from 300, count 300 */
  assert(model_read(1, 300) == 300 && model[1].count == 0x10000 - 300);
}

/* 100,000 random steps across the wrap of the cycle counter, each a pass,
   a TMxCNT_H or TMxCNT_L write or nothing, then reads of both timers,
   against the cycle-stepped timers: every read gives the stepped counter
   and every pass serves the overflows they had. A write comes after a
   pass when an overflow is due and not yet served, as the New core's
   update_gba comes at each overflow (main.c drops the unserved overflows
   of a timer it stops or changes). */
static void test_against_ticks(void)
{
  static const uint32_t reloads[] = { 0xFFFF, 0xFFFE, 0xFFFD, 0xFFC0,
   0xFF00, 0xF000, 0x0000 };
  uint32_t now = 0xFFF00000u;
  uint32_t i;
  unsigned n;

  reset(now);
  for(i = 0; i < FUZZ_OPERATIONS; i++)
  {
    uint32_t step = fuzz_random() % 300;
    uint32_t choice = fuzz_random() % 10;

    /* Now and then a slice longer than a period at F/1. */
    if(fuzz_random() % 16 == 0)
      step = fuzz_random() % 70000;
    n = fuzz_random() % 2;
    ref_cycles(step);
    now += step;
    if(choice >= 6 && model_overdue(now))
      choice = 0;
    if(choice < 3)
    {
      model_pass(now);
      assert(model[0].served == ref[0].overflows);
      assert(model[1].served == ref[1].overflows);
    }
    else if(choice >= 6 && choice < 8)
    {
      uint32_t value = fuzz_random() & 0x47;

      if(fuzz_random() % 4)
        value |= 0x80;
      ref_control(n, value);
      model_control(n, value, now);
    }
    else if(choice >= 8)
    {
      uint32_t value = fuzz_random();

      if(value % 2)
        value = reloads[fuzz_random() % 7];
      set_reload(n, value & 0xFFFF);
    }
    for(n = 0; n < 2; n++)
      assert(model_read(n, now) == ref[n].counter);
  }
}

int main(void)
{
  test_catch_up();
  test_live_read();
  test_start_stop_at_write();
  test_running_write();
  test_against_ticks();
  puts("GBA timer tests passed");
  return 0;
}
