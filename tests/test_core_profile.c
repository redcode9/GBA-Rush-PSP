/*
 * Host tests for core_profile.c, the new core's sampling profile: the
 * phases sum to the running samples, blocks are counted one by one until
 * the table is full, and the report lists the hottest first and fits its
 * capacity at the largest values.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core_profile.h"

static CoreProfile profile;

/* The number after "name=" in the report, or -1. */
static long field(const char *report, const char *name)
{
  char key[64];
  const char *at;

  snprintf(key, sizeof(key), "\n%s=", name);
  at = strstr(report, key);
  return at ? strtol(at + strlen(key), NULL, 10) : -1;
}

static void test_phases_sum_to_running_samples(void)
{
  ArmMipsStats stats;
  char report[CORE_PROFILE_CAPACITY];
  long permille = 0;
  unsigned i;

  memset(&stats, 0, sizeof(stats));
  core_profile_clear(&profile);
  for(i = 0; i < 700; i++)
    core_profile_add(&profile, ARM_MIPS_PHASE_CODE, 0x08000101u);
  for(i = 0; i < 100; i++)
    core_profile_add(&profile, ARM_MIPS_PHASE_DISPATCH, 0);
  for(i = 0; i < 100; i++)
    core_profile_add(&profile, ARM_MIPS_PHASE_MEMORY, 0);
  for(i = 0; i < 50; i++)
    core_profile_add(&profile, ARM_MIPS_PHASE_TRANSLATE, 0);
  for(i = 0; i < 50; i++)
    core_profile_add(&profile, ARM_MIPS_PHASE_EVENTS, 0);
  for(i = 0; i < 300; i++)
    core_profile_add_waiting(&profile);
  /* A phase out of range is ignored. */
  core_profile_add(&profile, ARM_MIPS_PHASES, 0);

  assert(core_profile_format(report, sizeof(report), &profile, 997,
   &stats) > 0);
  assert(field(report, "samples") == 1000);
  assert(field(report, "waiting") == 300);
  assert(field(report, "sample-us") == 997);
  assert(field(report, "phase-code-permille") == 700);
  assert(field(report, "phase-interpret") == 0);
  permille += field(report, "phase-code-permille");
  permille += field(report, "phase-dispatch-permille");
  permille += field(report, "phase-memory-permille");
  permille += field(report, "phase-translate-permille");
  permille += field(report, "phase-interpret-permille");
  permille += field(report, "phase-events-permille");
  assert(permille == 1000);
  assert(field(report, "block-08000101") == 700);
  assert(field(report, "code-other") == 0);
}

static void test_blocks_hottest_first_and_the_rest_as_other(void)
{
  ArmMipsStats stats;
  char report[CORE_PROFILE_CAPACITY];
  const char *first;
  const char *second;
  unsigned i;
  unsigned j;

  memset(&stats, 0, sizeof(stats));
  core_profile_clear(&profile);
  /* Block i gets i + 1 samples; more blocks than the report lists. */
  for(i = 0; i < CORE_PROFILE_TOP + 8; i++)
    for(j = 0; j <= i; j++)
      core_profile_add(&profile, ARM_MIPS_PHASE_CODE, 0x08000000u + 4 * i);
  core_profile_add(&profile, ARM_MIPS_PHASE_CODE, 0);
  assert(core_profile_format(report, sizeof(report), &profile, 997,
   &stats) > 0);
  first = strstr(report, "block-0800009C=40");
  second = strstr(report, "block-08000098=39");
  assert(first && second && first < second);
  assert(!strstr(report, "block-08000000="));
  /* The 8 coldest blocks (1 to 8 samples) and the unknown one. */
  assert(field(report, "code-other") == 36 + 1);
}

static void test_full_table_counts_other(void)
{
  ArmMipsStats stats;
  char report[CORE_PROFILE_CAPACITY];
  uint32_t i;

  memset(&stats, 0, sizeof(stats));
  core_profile_clear(&profile);
  for(i = 0; i < 4 * CORE_PROFILE_BLOCKS; i++)
    core_profile_add(&profile, ARM_MIPS_PHASE_CODE, 0x02000000u + 2 * i + 1);
  assert(core_profile_format(report, sizeof(report), &profile, 997,
   &stats) > 0);
  assert(field(report, "phase-code") == 4 * CORE_PROFILE_BLOCKS);
  assert(field(report, "code-other") ==
   4 * CORE_PROFILE_BLOCKS - CORE_PROFILE_TOP);
}

static void test_translation_times(void)
{
  ArmMipsStats stats;
  char report[CORE_PROFILE_CAPACITY];

  memset(&stats, 0, sizeof(stats));
  stats.decode_ticks = 300;
  stats.compile_ticks = 700;
  stats.guest_insns = 2000;
  stats.blocks = 150;
  core_profile_clear(&profile);
  assert(core_profile_format(report, sizeof(report), &profile, 997,
   &stats) > 0);
  assert(field(report, "translate-decode-us") == 300);
  assert(field(report, "translate-compile-us") == 700);
  assert(field(report, "translate-ns-per-instruction") == 500);
  assert(field(report, "blocks-compiled") == 150);
  /* No samples: no division by zero. */
  assert(field(report, "phase-code-permille") == 0);
}

static void test_largest_report_fits(void)
{
  ArmMipsStats stats;
  char report[CORE_PROFILE_CAPACITY];
  unsigned i;
  int length;

  memset(&stats, 0xFF, sizeof(stats));
  core_profile_clear(&profile);
  for(i = 0; i < ARM_MIPS_PHASES; i++)
    profile.phase[i] = 0xFFFFFFFFu;
  profile.waiting = profile.other = 0xFFFFFFFFu;
  for(i = 0; i < CORE_PROFILE_BLOCKS; i++)
  {
    profile.key[i] = 0xF0000000u + i;
    profile.count[i] = 0xFFFFFFF0u + (i & 15);
  }
  length = core_profile_format(report, sizeof(report), &profile, 0xFFFFFFFFu,
   &stats);
  assert(length > 0 && (size_t)length < sizeof(report));
  assert(strlen(report) == (size_t)length);
  /* Too small a buffer is refused, not overrun. */
  assert(core_profile_format(report, 100, &profile, 997, &stats) == -1);
  assert(core_profile_format(report, 0, &profile, 997, &stats) == -1);
}

int main(void)
{
  test_phases_sum_to_running_samples();
  test_blocks_hottest_first_and_the_rest_as_other();
  test_full_table_counts_other();
  test_translation_times();
  test_largest_report_fits();
  printf("core_profile tests passed\n");
  return 0;
}
