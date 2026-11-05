/*
 * Host tests for power_lifecycle.c. The PSP power callback thread records
 * suspend (with or without an autosave request) and resume (with the
 * audio restart result); the main loop later takes each pending save and
 * resume exactly once, however the callbacks repeat or interleave.
 * Callback bits become entries and resumes through power_lifecycle_classify,
 * a switch-only entry ends by itself after 2 s, and each callback's raw
 * bits are kept for the runtime log. A seeded property test then runs
 * random sequences of all of these against a reference model.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---- Seeded property test ------------------------------------------------
 * Random sequences of power callbacks, take_save, take_resume, take_bits and
 * clock steps that call expire_switch. After every step the lifecycle must
 * agree with a small reference model of the SAV-7.6 rules. */

#define PROPERTY_SEQUENCES 10000u
#define PROPERTY_MAX_STEPS 48u

typedef struct
{
  uint32_t entry; /* 0, SWITCH or SUSPENDING, like lifecycle.suspended */
  uint64_t entered_us; /* clock when that entry began */
  int save_owed;
  int resume_owed;
  int audio_result; /* what the owed resume hands over */
  uint32_t noted; /* callbacks so far, with the bits of each */
  uint32_t history[PROPERTY_MAX_STEPS + 1u];
  uint32_t taken; /* callbacks take_bits returned or skipped */
} PowerModel;

/* Cases every seed must reach, so the SAV-7.6 sequences are in the mix. */
enum
{
  COVER_SWITCH_ENTRY,
  COVER_SUSPENDING_ENTRY,
  COVER_SWITCH_AND_SUSPENDING_ENTRY,
  COVER_SWITCH_THEN_SUSPENDING,
  COVER_RESUME_AFTER_SWITCH_ENTRY,
  COVER_RESUME_WITHOUT_ENTRY,
  COVER_SWITCH_WITH_RESUME_BITS,
  COVER_SWITCH_BEFORE_TIMEOUT,
  COVER_SWITCH_TIMEOUT,
  COVER_BITS_OVERRUN,
  COVER_COUNT
};

static unsigned long property_seed;
static uint32_t property_sequence;
static uint32_t property_step;
static uint32_t property_random_state;
static uint32_t property_cover[COVER_COUNT];

static void property_check(int ok, const char *what, int line)
{
  if(ok)
    return;

  fprintf(stderr, "Power property violated: seed %lu, sequence %u, step %u: "
   "%s (line %d)\n", property_seed, (unsigned)property_sequence,
   (unsigned)property_step, what, line);
  assert(ok);
}

#define PROPERTY_CHECK(e) property_check((e) != 0, #e, __LINE__)

/* xorshift32, so every host replays the same sequences for a seed. */
static uint32_t property_random(uint32_t range)
{
  uint32_t x = property_random_state;

  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  property_random_state = x;
  return x % range;
}

/* Half the callbacks carry one of the SAV-7.6 bit combinations, the other
   half any mix of the four bits; status bits ride along on half. */
static uint32_t property_bits(void)
{
  static const uint32_t named[] =
  {
    BIT_SWITCH, BIT_SUSPENDING, BIT_SWITCH | BIT_SUSPENDING,
    BIT_RESUME_COMPLETE, BIT_RESUMING, BIT_SWITCH | BIT_RESUMING,
    BIT_SWITCH | BIT_RESUME_COMPLETE, BIT_RESUMING | BIT_RESUME_COMPLETE
  };
  static const uint32_t single[] =
  {
    BIT_SWITCH, BIT_SUSPENDING, BIT_RESUMING, BIT_RESUME_COMPLETE
  };
  uint32_t bits = 0;
  uint32_t mask;
  uint32_t i;

  if(property_random(2))
    bits = named[property_random(8)];
  else
  {
    mask = property_random(16);
    for(i = 0; i < 4; i++)
    {
      if(mask & (1u << i))
        bits |= single[i];
    }
  }
  if(property_random(2))
    bits |= BIT_STATUS;
  return bits;
}

/* The lifecycle runs power_callback's steps from main.c (without the sound
   calls); the model applies the rules as SAV-7.6 states them. */
static void property_callback(PowerLifecycle *lifecycle, PowerModel *model,
 uint32_t bits, int game, uint64_t now)
{
  int audio_result = (int)property_random(64) - 32;
  uint32_t action = power_lifecycle_classify(bits);
  uint32_t entry = 0;

  power_lifecycle_note_bits(lifecycle, bits);
  if((action & POWER_LIFECYCLE_ENTRY_MASK) != 0)
    power_lifecycle_suspend(lifecycle, action & POWER_LIFECYCLE_ENTRY_MASK,
     game);
  if((action & RESUME) != 0 &&
     power_lifecycle_state(lifecycle) == POWER_LIFECYCLE_SUSPENDED)
    power_lifecycle_resume(lifecycle, audio_result);

  model->history[model->noted++] = bits;
  /* SUSPENDING enters; POWER_SWITCH enters unless it comes with RESUMING
     or RESUME_COMPLETE. */
  if(bits & BIT_SUSPENDING)
    entry = SUSPENDING;
  else if(!(bits & BIT_SWITCH))
    entry = 0;
  else if(!(bits & (BIT_RESUMING | BIT_RESUME_COMPLETE)))
    entry = SWITCH;
  else if(model->entry == 0)
    property_cover[COVER_SWITCH_WITH_RESUME_BITS]++;

  /* An entry owes one save, if a game is loaded; SUSPENDING after a switch
     entry joins it without a second save. */
  if(entry != 0 && model->entry == 0)
  {
    model->entry = entry;
    model->entered_us = now;
    model->save_owed |= game;
    if(entry == SWITCH)
      property_cover[COVER_SWITCH_ENTRY]++;
    else if(bits & BIT_SWITCH)
      property_cover[COVER_SWITCH_AND_SUSPENDING_ENTRY]++;
    else
      property_cover[COVER_SUSPENDING_ENTRY]++;
  }
  else if(entry == SUSPENDING && model->entry == SWITCH)
  {
    model->entry = SUSPENDING;
    property_cover[COVER_SWITCH_THEN_SUSPENDING]++;
  }

  /* RESUME_COMPLETE ends any entry and owes a resume; without an entry it
     is ignored. */
  if(bits & BIT_RESUME_COMPLETE)
  {
    if(model->entry == 0)
      property_cover[COVER_RESUME_WITHOUT_ENTRY]++;
    else
    {
      if(model->entry == SWITCH)
        property_cover[COVER_RESUME_AFTER_SWITCH_ENTRY]++;
      model->entry = 0;
      model->resume_owed = 1;
      model->audio_result = audio_result;
    }
  }
}

/* At most one save per entry: only an entry that asked for one owes it. */
static void property_take_save(PowerLifecycle *lifecycle, PowerModel *model)
{
  PROPERTY_CHECK(power_lifecycle_take_save(lifecycle) == model->save_owed);
  model->save_owed = 0;
}

/* No lost resume and none taken twice, with the latest audio result. */
static void property_take_resume(PowerLifecycle *lifecycle,
 PowerModel *model)
{
  int audio_result = 0x5a5a;
  int taken = power_lifecycle_take_resume(lifecycle, &audio_result);

  PROPERTY_CHECK(taken == model->resume_owed);
  PROPERTY_CHECK(audio_result == (taken ? model->audio_result : 0x5a5a));
  model->resume_owed = 0;
}

/* Each callback's bits come back once, in order, unless more than
   POWER_LIFECYCLE_RAW_SLOTS - 1 wait: then the oldest are skipped. */
static void property_take_bits(PowerLifecycle *lifecycle, PowerModel *model)
{
  uint32_t bits = 0x5a5au;
  uint32_t number = 0x5a5au;
  uint32_t next = model->taken;
  int taken = power_lifecycle_take_bits(lifecycle, &bits, &number);

  if(model->noted - next >= POWER_LIFECYCLE_RAW_SLOTS)
  {
    next = model->noted - (POWER_LIFECYCLE_RAW_SLOTS - 1u);
    property_cover[COVER_BITS_OVERRUN]++;
  }
  PROPERTY_CHECK(taken == (next != model->noted));
  if(!taken)
  {
    PROPERTY_CHECK(bits == 0x5a5au && number == 0x5a5au);
    return;
  }
  PROPERTY_CHECK(bits == model->history[next] && number == next + 1u);
  model->taken = next + 1u;
}

/* Clock steps of 0-3 s, half of them on the edges of the 2 s timeout. Only
   a switch-only entry expires, and only 2 s or more after it entered. */
static void property_clock(PowerLifecycle *lifecycle, PowerModel *model,
 uint64_t *now)
{
  static const uint32_t edges[] =
  {
    0u, 1u, 100000u, 1000000u, 1999999u, 2000000u, 2000001u, 3000000u
  };
  int due;
  int expired;

  *now += property_random(2) ? edges[property_random(8)] :
   property_random(3000001u);
  due = *now >= model->entered_us + POWER_LIFECYCLE_SWITCH_TIMEOUT_US;
  expired = power_lifecycle_expire_switch(lifecycle, model->entered_us,
   *now);

  PROPERTY_CHECK(expired == (model->entry == SWITCH && due));
  if(model->entry != SWITCH)
    return;

  if(!due)
  {
    property_cover[COVER_SWITCH_BEFORE_TIMEOUT]++;
    return;
  }
  property_cover[COVER_SWITCH_TIMEOUT]++;
  model->entry = 0;
  model->resume_owed = 1;
  model->audio_result = POWER_LIFECYCLE_AUDIO_NOT_RESERVED;
}

/* Only RUNNING, SUSPENDED and RESUME_PENDING, as the model has it. */
static void property_check_state(const PowerLifecycle *lifecycle,
 const PowerModel *model)
{
  PowerLifecycleState state = power_lifecycle_state(lifecycle);

  PROPERTY_CHECK(state == POWER_LIFECYCLE_RUNNING ||
   state == POWER_LIFECYCLE_SUSPENDED ||
   state == POWER_LIFECYCLE_RESUME_PENDING);
  PROPERTY_CHECK(state == (model->entry != 0 ? POWER_LIFECYCLE_SUSPENDED :
   model->resume_owed ? POWER_LIFECYCLE_RESUME_PENDING :
   POWER_LIFECYCLE_RUNNING));
  PROPERTY_CHECK(lifecycle->suspended == model->entry);
}

static void property_run_sequence(void)
{
  PowerLifecycle lifecycle;
  PowerModel model;
  uint64_t now = property_random(0xffffffffu);
  int game = property_random(8) != 0;
  uint32_t steps = 1u + property_random(PROPERTY_MAX_STEPS);
  uint32_t op;
  uint32_t i;

  power_lifecycle_init(&lifecycle);
  memset(&model, 0, sizeof(model));
  model.entered_us = now;
  for(property_step = 0; property_step < steps; property_step++)
  {
    op = property_random(20);
    if(op < 8)
      property_callback(&lifecycle, &model, property_bits(), game, now);
    else if(op < 11)
      property_take_save(&lifecycle, &model);
    else if(op < 14)
      property_take_resume(&lifecycle, &model);
    else if(op < 16)
      property_take_bits(&lifecycle, &model);
    else
      property_clock(&lifecycle, &model, &now);
    property_check_state(&lifecycle, &model);
  }

  /* Quiescent: a RESUME_COMPLETE ends any entry, everything owed is taken
     once, and then nothing more. Up to POWER_LIFECYCLE_RAW_SLOTS - 1
     callbacks wait, so the last take_bits finds none. */
  property_callback(&lifecycle, &model, BIT_RESUME_COMPLETE, game, now);
  property_take_save(&lifecycle, &model);
  property_take_resume(&lifecycle, &model);
  for(i = 0; i < POWER_LIFECYCLE_RAW_SLOTS; i++)
    property_take_bits(&lifecycle, &model);
  property_check_state(&lifecycle, &model);
  PROPERTY_CHECK(power_lifecycle_state(&lifecycle) ==
   POWER_LIFECYCLE_RUNNING);
  PROPERTY_CHECK(!power_lifecycle_take_save(&lifecycle));
  PROPERTY_CHECK(!power_lifecycle_take_resume(&lifecycle, NULL));
  PROPERTY_CHECK(!power_lifecycle_take_bits(&lifecycle, NULL, NULL));
}

static void test_property_sequences(unsigned long seed)
{
  property_seed = seed;
  property_random_state = (uint32_t)seed != 0 ? (uint32_t)seed : 0x9e3779b9u;
  memset(property_cover, 0, sizeof(property_cover));
  for(property_sequence = 0; property_sequence < PROPERTY_SEQUENCES;
   property_sequence++)
    property_run_sequence();

  /* A case the seed never reached reports its COVER_ index as the step. */
  for(property_step = 0; property_step < COVER_COUNT; property_step++)
    PROPERTY_CHECK(property_cover[property_step] != 0);
}

/* Runs every callback case, then the property test on the fixed seeds, or
   on the one seed given as the first argument; a failing assert aborts. */
int main(int argc, char **argv)
{
  static const unsigned long seeds[] =
  {
    1u, 20261104u, 0x9e3779b9u, 0xc0ffee11u
  };
  size_t i;

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
  if(argc > 1)
    test_property_sequences(strtoul(argv[1], NULL, 10));
  else
  {
    for(i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++)
      test_property_sequences(seeds[i]);
  }
  puts("Power lifecycle tests passed");
  return 0;
}
