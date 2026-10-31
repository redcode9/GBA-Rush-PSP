/*
 * Host tests for power_lifecycle.c. The PSP power callback thread records
 * suspend (with or without an autosave request) and resume (with the
 * audio restart result); the main loop later takes each pending save and
 * resume exactly once, however the callbacks repeat or interleave.
 */

#include <assert.h>
#include <stdio.h>

#include "power_lifecycle.h"

static void test_initial_state(void)
{
  PowerLifecycle lifecycle;
  int result = 123;

  power_lifecycle_init(&lifecycle);
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_RUNNING);
  assert(!power_lifecycle_take_save(&lifecycle));
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == 123);

  power_lifecycle_init(&lifecycle);
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_RUNNING);
}

static void test_suspend_and_resume(void)
{
  PowerLifecycle lifecycle;
  int result = 0;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, 1);
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_SUSPENDED);
  assert(power_lifecycle_take_save(&lifecycle));
  assert(!power_lifecycle_take_save(&lifecycle));

  power_lifecycle_resume(&lifecycle, 17);
  assert(power_lifecycle_state(&lifecycle) ==
   POWER_LIFECYCLE_RESUME_PENDING);
  assert(power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == 17);
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_RUNNING);
}

/* Suspend and resume both arrive before the main loop runs again. */
static void test_callbacks_before_foreground(void)
{
  PowerLifecycle lifecycle;
  int result = 0;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, 1);
  power_lifecycle_resume(&lifecycle, -8);

  assert(power_lifecycle_take_save(&lifecycle));
  assert(power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == -8);
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_RUNNING);
}

/* A repeated callback without the opposite one in between is ignored. */
static void test_duplicate_callbacks(void)
{
  PowerLifecycle lifecycle;
  int result = 0;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, 1);
  power_lifecycle_suspend(&lifecycle, 1);
  assert(power_lifecycle_take_save(&lifecycle));
  assert(!power_lifecycle_take_save(&lifecycle));

  power_lifecycle_resume(&lifecycle, 4);
  power_lifecycle_resume(&lifecycle, 5);
  assert(power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == 4);
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
}

/* Two full cycles before the main loop looks: one save, latest result. */
static void test_repeated_cycles_coalesce_safely(void)
{
  PowerLifecycle lifecycle;
  int result = 0;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, 1);
  power_lifecycle_resume(&lifecycle, 1);
  power_lifecycle_suspend(&lifecycle, 1);
  power_lifecycle_resume(&lifecycle, 2);

  assert(power_lifecycle_take_save(&lifecycle));
  assert(!power_lifecycle_take_save(&lifecycle));
  assert(power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == 2);
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
}

static void test_suspend_without_game(void)
{
  PowerLifecycle lifecycle;
  int result = 0;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, 0);
  power_lifecycle_resume(&lifecycle, 0);

  assert(!power_lifecycle_take_save(&lifecycle));
  assert(power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == 0);
}

static void test_resume_without_suspend_is_ignored(void)
{
  PowerLifecycle lifecycle;
  int result = 91;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_resume(&lifecycle, -3);

  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_RUNNING);
  assert(!power_lifecycle_take_save(&lifecycle));
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == 91);
}

int main(void)
{
  test_initial_state();
  test_suspend_and_resume();
  test_callbacks_before_foreground();
  test_duplicate_callbacks();
  test_repeated_cycles_coalesce_safely();
  test_suspend_without_game();
  test_resume_without_suspend_is_ignored();
  puts("Power lifecycle tests passed");
  return 0;
}
