/*
 * Host tests for power_lifecycle.c. The PSP power callback thread records
 * suspend (with or without an autosave request) and resume (with the
 * audio restart result); the main loop later takes each pending save and
 * resume exactly once, however the callbacks repeat or interleave.
 * Callback bits become entries and resumes through power_lifecycle_classify,
 * a switch-only entry ends by itself after 2 s, and each callback's raw
 * bits are kept for the runtime log.
 */

#include <assert.h>
#include <stdio.h>

#include "power_lifecycle.h"

#define SWITCH POWER_LIFECYCLE_ENTRY_SWITCH
#define SUSPENDING POWER_LIFECYCLE_ENTRY_SUSPENDING
#define RESUME POWER_LIFECYCLE_ACTION_RESUME
#define BIT_SWITCH POWER_LIFECYCLE_BIT_POWER_SWITCH
#define BIT_SUSPENDING POWER_LIFECYCLE_BIT_SUSPENDING
#define BIT_RESUMING POWER_LIFECYCLE_BIT_RESUMING
#define BIT_RESUME_COMPLETE POWER_LIFECYCLE_BIT_RESUME_COMPLETE
/* Battery present and AC power: status bits that ride along. */
#define BIT_STATUS 0x00001080u

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
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
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
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
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
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
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
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
  power_lifecycle_resume(&lifecycle, 1);
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
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
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 0);
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

static void test_power_switch_alone_enters(void)
{
  PowerLifecycle lifecycle;

  assert(power_lifecycle_classify(BIT_SWITCH) == SWITCH);
  assert(power_lifecycle_classify(BIT_SWITCH | BIT_STATUS) == SWITCH);
  assert(power_lifecycle_classify(BIT_STATUS) == 0);

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, SWITCH, 1);
  assert(lifecycle.suspended == SWITCH);
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_SUSPENDED);
  assert(power_lifecycle_take_save(&lifecycle));
}

static void test_suspending_alone_enters(void)
{
  PowerLifecycle lifecycle;

  assert(power_lifecycle_classify(BIT_SUSPENDING) == SUSPENDING);
  assert(power_lifecycle_classify(BIT_SUSPENDING | BIT_STATUS) ==
   SUSPENDING);

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
  assert(lifecycle.suspended == SUSPENDING);
  assert(power_lifecycle_take_save(&lifecycle));
}

/* POWER_SWITCH and SUSPENDING in one callback or in two: one save. */
static void test_switch_and_suspending_save_once(void)
{
  PowerLifecycle lifecycle;

  assert(power_lifecycle_classify(BIT_SWITCH | BIT_SUSPENDING) ==
   SUSPENDING);

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, SWITCH, 1);
  assert(power_lifecycle_take_save(&lifecycle));
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
  assert(lifecycle.suspended == SUSPENDING);
  assert(!power_lifecycle_take_save(&lifecycle));
  assert(lifecycle.save_sequence == 1);
}

/* A RESUME_COMPLETE after a switch-only entry is a resume. */
static void test_resume_after_switch_only_entry(void)
{
  PowerLifecycle lifecycle;
  int result = 0;

  assert(power_lifecycle_classify(BIT_RESUME_COMPLETE) == RESUME);

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, SWITCH, 1);
  power_lifecycle_resume(&lifecycle, 6);
  assert(power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == 6);
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_RUNNING);
}

/* The switch bit that comes with a resume does not start a new suspend. */
static void test_switch_during_resume_does_not_enter(void)
{
  assert(power_lifecycle_classify(BIT_SWITCH | BIT_RESUMING) == 0);
  assert(power_lifecycle_classify(BIT_SWITCH | BIT_RESUME_COMPLETE) ==
   RESUME);
  assert(power_lifecycle_classify(BIT_SWITCH | BIT_RESUMING |
   BIT_RESUME_COMPLETE | BIT_STATUS) == RESUME);
  assert(power_lifecycle_classify(BIT_RESUMING) == 0);
}

/* An entry and RESUME_COMPLETE in one callback: suspend, then resume. */
static void test_entry_with_resume_complete_does_both(void)
{
  assert(power_lifecycle_classify(BIT_SUSPENDING | BIT_RESUME_COMPLETE) ==
   (SUSPENDING | RESUME));
  assert(power_lifecycle_classify(BIT_SWITCH | BIT_SUSPENDING |
   BIT_RESUME_COMPLETE) == (SUSPENDING | RESUME));
}

/* Without SUSPENDING or RESUME_COMPLETE, a switch-only entry ends 2 s after
   the emulator thread parked, as a resume that must reserve audio. */
static void test_switch_entry_times_out(void)
{
  PowerLifecycle lifecycle;
  const uint64_t entered = 5000000u;
  int result = 0;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, SWITCH, 1);
  assert(power_lifecycle_take_save(&lifecycle));

  assert(!power_lifecycle_expire_switch(&lifecycle, entered, entered - 1u));
  assert(!power_lifecycle_expire_switch(&lifecycle, entered,
   entered + POWER_LIFECYCLE_SWITCH_TIMEOUT_US - 1u));
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_SUSPENDED);

  assert(power_lifecycle_expire_switch(&lifecycle, entered,
   entered + POWER_LIFECYCLE_SWITCH_TIMEOUT_US));
  assert(power_lifecycle_state(&lifecycle) ==
   POWER_LIFECYCLE_RESUME_PENDING);
  assert(!power_lifecycle_expire_switch(&lifecycle, entered,
   entered + 2u * POWER_LIFECYCLE_SWITCH_TIMEOUT_US));

  /* A late RESUME_COMPLETE finds nothing to resume. */
  power_lifecycle_resume(&lifecycle, 9);
  assert(power_lifecycle_take_resume(&lifecycle, &result));
  assert(result == POWER_LIFECYCLE_AUDIO_NOT_RESERVED);
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_RUNNING);

  /* A SUSPENDING after the timeout is a new suspend with its own save. */
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_SUSPENDED);
  assert(power_lifecycle_take_save(&lifecycle));
}

static void test_suspending_entry_never_times_out(void)
{
  PowerLifecycle lifecycle;
  int result = 0;

  power_lifecycle_init(&lifecycle);
  power_lifecycle_suspend(&lifecycle, SWITCH, 1);
  power_lifecycle_suspend(&lifecycle, SUSPENDING, 1);
  assert(!power_lifecycle_expire_switch(&lifecycle, 0u,
   10u * POWER_LIFECYCLE_SWITCH_TIMEOUT_US));
  assert(power_lifecycle_state(&lifecycle) == POWER_LIFECYCLE_SUSPENDED);
  assert(!power_lifecycle_take_resume(&lifecycle, &result));

  power_lifecycle_init(&lifecycle);
  assert(!power_lifecycle_expire_switch(&lifecycle, 0u,
   10u * POWER_LIFECYCLE_SWITCH_TIMEOUT_US));
  assert(!power_lifecycle_take_resume(&lifecycle, &result));
}

/* Each callback's bits come back on their own, in order, once. */
static void test_raw_bits_are_kept_per_callback(void)
{
  static const uint32_t wake[] =
  {
    BIT_SWITCH, BIT_SUSPENDING | BIT_SWITCH, BIT_RESUMING,
    BIT_RESUME_COMPLETE | BIT_SWITCH | BIT_STATUS
  };
  PowerLifecycle lifecycle;
  uint32_t bits = 99u;
  uint32_t number = 99u;
  uint32_t i;

  power_lifecycle_init(&lifecycle);
  assert(!power_lifecycle_take_bits(&lifecycle, &bits, &number));
  assert(bits == 99u && number == 99u);

  for(i = 0; i < 4u; i++)
    power_lifecycle_note_bits(&lifecycle, wake[i]);
  for(i = 0; i < 4u; i++)
  {
    assert(power_lifecycle_take_bits(&lifecycle, &bits, &number));
    assert(bits == wake[i]);
    assert(number == i + 1u);
  }
  assert(!power_lifecycle_take_bits(&lifecycle, &bits, &number));

  /* Taking resumes after the last callback taken. */
  power_lifecycle_note_bits(&lifecycle, BIT_STATUS);
  assert(power_lifecycle_take_bits(&lifecycle, NULL, &number));
  assert(number == 5u);
  assert(!power_lifecycle_take_bits(&lifecycle, NULL, NULL));
}

/* Callbacks the ring overwrote are skipped; the numbers show the gap. */
static void test_raw_bits_overrun_leaves_a_gap(void)
{
  const uint32_t total = POWER_LIFECYCLE_RAW_SLOTS + 3u;
  const uint32_t first = total - (POWER_LIFECYCLE_RAW_SLOTS - 1u) + 1u;
  PowerLifecycle lifecycle;
  uint32_t bits;
  uint32_t number;
  uint32_t i;

  power_lifecycle_init(&lifecycle);
  for(i = 1; i <= total; i++)
    power_lifecycle_note_bits(&lifecycle, i << 8);

  for(i = first; i <= total; i++)
  {
    assert(power_lifecycle_take_bits(&lifecycle, &bits, &number));
    assert(number == i);
    assert(bits == i << 8);
  }
  assert(!power_lifecycle_take_bits(&lifecycle, &bits, &number));

  /* A full ring but for the slot being written loses nothing... */
  for(i = 1; i < POWER_LIFECYCLE_RAW_SLOTS; i++)
    power_lifecycle_note_bits(&lifecycle, i);
  assert(power_lifecycle_take_bits(&lifecycle, &bits, &number));
  assert(number == total + 1u && bits == 1u);
  while(power_lifecycle_take_bits(&lifecycle, &bits, &number))
    ;
  assert(number == total + POWER_LIFECYCLE_RAW_SLOTS - 1u);

  /* ...and one more callback costs the oldest. */
  for(i = 1; i <= POWER_LIFECYCLE_RAW_SLOTS; i++)
    power_lifecycle_note_bits(&lifecycle, i);
  assert(power_lifecycle_take_bits(&lifecycle, &bits, &number));
  assert(number == total + POWER_LIFECYCLE_RAW_SLOTS + 1u && bits == 2u);
}

/* Runs every callback case; a failing assert aborts the run. */
int main(void)
{
  test_initial_state();
  test_suspend_and_resume();
  test_callbacks_before_foreground();
  test_duplicate_callbacks();
  test_repeated_cycles_coalesce_safely();
  test_suspend_without_game();
  test_resume_without_suspend_is_ignored();
  test_power_switch_alone_enters();
  test_suspending_alone_enters();
  test_switch_and_suspending_save_once();
  test_resume_after_switch_only_entry();
  test_switch_during_resume_does_not_enter();
  test_entry_with_resume_complete_does_both();
  test_switch_entry_times_out();
  test_suspending_entry_never_times_out();
  test_raw_bits_are_kept_per_callback();
  test_raw_bits_overrun_leaves_a_gap();
  puts("Power lifecycle tests passed");
  return 0;
}
