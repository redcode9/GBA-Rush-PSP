#include <stdio.h>

#include "link_clock.h"

static int failures;

#define CHECK(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
       #condition); \
      failures++; \
    } \
  } while(0)

static void test_publish_interval(void)
{
  LinkClock clock;

  link_clock_init(&clock, 7u, 100u);
  CHECK(link_clock_should_publish(&clock));
  link_clock_note_published(&clock);
  link_clock_set_local(&clock, 100u + LINK_CLOCK_SYNC_INTERVAL_CYCLES - 1u);
  CHECK(!link_clock_should_publish(&clock));
  link_clock_set_local(&clock, 100u + LINK_CLOCK_SYNC_INTERVAL_CYCLES);
  CHECK(link_clock_should_publish(&clock));
}

static void test_wall_clock_publish_interval(void)
{
  LinkClock clock;

  link_clock_init(&clock, 7u, 100u);
  CHECK(link_clock_should_publish_after(&clock, 500u, 1000000u));
  link_clock_note_published_at(&clock, 500u);
  CHECK(!link_clock_should_publish_after(&clock, 1000499u, 1000000u));
  CHECK(link_clock_should_publish_after(&clock, 1000500u, 1000000u));
  CHECK(link_clock_should_publish_after(&clock, 499u, 1000000u));
}

static void test_hot_policy_is_cycle_scale_not_frame_scale(void)
{
  CHECK(LINK_CLOCK_SYNC_INTERVAL_CYCLES == 0x00004000u);
  CHECK(LINK_CLOCK_MAX_LEAD_CYCLES == 0x00008000u);
  CHECK(LINK_CLOCK_MAX_LEAD_CYCLES < 280896u);
}

static void test_hot_clock_bounds_lead(void)
{
  LinkClock clock;

  link_clock_init(&clock, 1u, 1000u);
  link_clock_note_peer(&clock, 1u, 2000u, 10u);
  link_clock_mark_hot(&clock, 10u);
  link_clock_set_local(&clock, 1000u + LINK_CLOCK_MAX_LEAD_CYCLES);
  CHECK(!link_clock_should_throttle(&clock, 20u));
  link_clock_set_local(&clock, 1001u + LINK_CLOCK_MAX_LEAD_CYCLES);
  CHECK(link_clock_should_throttle(&clock, 20u));
  CHECK(!link_clock_should_throttle(&clock,
   10u + LINK_CLOCK_HOT_WINDOW_US + 1u));
}

static void test_stale_peer_never_freezes_emulation(void)
{
  LinkClock clock;

  link_clock_init(&clock, 1u, 0u);
  link_clock_note_peer(&clock, 1u, 0u, 100u);
  link_clock_mark_hot(&clock, 100u);
  link_clock_set_local(&clock, LINK_CLOCK_MAX_LEAD_CYCLES + 1u);
  CHECK(link_clock_should_throttle(&clock, 200u));
  CHECK(!link_clock_should_throttle(&clock,
   100u + LINK_CLOCK_PEER_STALE_US + 1u));
}

static void test_generation_rebases_wrap_safe_clock(void)
{
  LinkClock clock;

  link_clock_init(&clock, 1u, UINT32_MAX - 8u);
  link_clock_note_peer(&clock, 1u, UINT32_MAX - 4u, 1u);
  link_clock_set_local(&clock, 16u);
  link_clock_note_peer(&clock, 1u, 20u, 2u);
  CHECK(!link_clock_should_throttle(&clock, 2u));

  link_clock_set_local(&clock, 100u);
  link_clock_note_peer(&clock, 2u, 500u, 3u);
  CHECK(clock.local_base == 100u);
  CHECK(clock.peer_base == 500u);
}

int main(void)
{
  test_publish_interval();
  test_wall_clock_publish_interval();
  test_hot_policy_is_cycle_scale_not_frame_scale();
  test_hot_clock_bounds_lead();
  test_stale_peer_never_freezes_emulation();
  test_generation_rebases_wrap_safe_clock();

  if(failures != 0)
  {
    fprintf(stderr, "%d link clock test(s) failed\n", failures);
    return 1;
  }
  puts("link clock tests passed");
  return 0;
}
