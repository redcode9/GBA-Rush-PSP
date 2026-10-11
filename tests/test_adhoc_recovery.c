#include <stdio.h>

#include "adhoc_recovery.h"

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

static const AdhocRecoveryConfig test_config =
{
  100u,
  40u,
  60u,
  100u,
  120u,
  25u
};

static void test_idle_probe_recovers_on_peer_activity(void)
{
  AdhocRecovery recovery;

  adhoc_recovery_init(&recovery, 0, &test_config);
  CHECK(adhoc_recovery_poll(&recovery, 99u) ==
   ADHOC_RECOVERY_ACTION_NONE);
  CHECK(adhoc_recovery_poll(&recovery, 100u) ==
   ADHOC_RECOVERY_ACTION_PROBE);
  CHECK(recovery.state == ADHOC_RECOVERY_PROBING);
  adhoc_recovery_probe_sent(&recovery, 100u);
  adhoc_recovery_note_peer(&recovery, 102u);
  CHECK(recovery.state == ADHOC_RECOVERY_HEALTHY);
  CHECK(recovery.total_transport_resyncs == 0);
}

static void test_disabled_idle_probe_waits_for_real_error(void)
{
  AdhocRecovery recovery;
  AdhocRecoveryConfig config = test_config;

  config.idle_timeout_us = 0;
  adhoc_recovery_init(&recovery, 0, &config);
  CHECK(adhoc_recovery_poll(&recovery, UINT64_MAX) ==
   ADHOC_RECOVERY_ACTION_NONE);
  CHECK(recovery.state == ADHOC_RECOVERY_HEALTHY);
  adhoc_recovery_request(&recovery);
  CHECK(adhoc_recovery_poll(&recovery, UINT64_MAX) ==
   ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT);
}

static void test_explicit_probe_precedes_transport_resync(void)
{
  AdhocRecovery recovery;
  AdhocRecoveryConfig config = test_config;

  config.idle_timeout_us = 0;
  adhoc_recovery_init(&recovery, 0, &config);
  adhoc_recovery_probe(&recovery, 50u);
  CHECK(recovery.state == ADHOC_RECOVERY_PROBING);
  CHECK(adhoc_recovery_poll(&recovery, 50u) ==
   ADHOC_RECOVERY_ACTION_PROBE);
  CHECK(adhoc_recovery_poll(&recovery, 89u) ==
   ADHOC_RECOVERY_ACTION_PROBE);
  CHECK(adhoc_recovery_poll(&recovery, 90u) ==
   ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT);
}

static void test_explicit_probe_is_cancelled_by_peer_activity(void)
{
  AdhocRecovery recovery;
  AdhocRecoveryConfig config = test_config;

  config.idle_timeout_us = 0;
  adhoc_recovery_init(&recovery, 0, &config);
  adhoc_recovery_probe(&recovery, 50u);
  adhoc_recovery_probe(&recovery, 60u);
  CHECK(recovery.state_started_us == 50u);
  adhoc_recovery_note_peer(&recovery, 70u);
  CHECK(recovery.state == ADHOC_RECOVERY_HEALTHY);
  CHECK(adhoc_recovery_poll(&recovery, 1000u) ==
   ADHOC_RECOVERY_ACTION_NONE);
}

static void test_probe_is_rate_limited(void)
{
  AdhocRecovery recovery;

  adhoc_recovery_init(&recovery, 0, &test_config);
  CHECK(adhoc_recovery_poll(&recovery, 100u) ==
   ADHOC_RECOVERY_ACTION_PROBE);
  adhoc_recovery_probe_sent(&recovery, 100u);
  CHECK(adhoc_recovery_poll(&recovery, 124u) ==
   ADHOC_RECOVERY_ACTION_NONE);
  CHECK(adhoc_recovery_poll(&recovery, 125u) ==
   ADHOC_RECOVERY_ACTION_PROBE);
  CHECK(adhoc_recovery_poll(&recovery, 140u) ==
   ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT);
}

static void test_retry_and_hierarchical_escalation(void)
{
  AdhocRecovery recovery;

  adhoc_recovery_init(&recovery, 0, &test_config);
  adhoc_recovery_request(&recovery);
  CHECK(adhoc_recovery_poll(&recovery, 1u) ==
   ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT);
  adhoc_recovery_platform_result(&recovery, 1u, 0);
  CHECK(recovery.platform_attempts == 1);
  CHECK(adhoc_recovery_poll(&recovery, 25u) ==
   ADHOC_RECOVERY_ACTION_NONE);
  CHECK(adhoc_recovery_poll(&recovery, 26u) ==
   ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT);

  CHECK(adhoc_recovery_poll(&recovery, 61u) ==
   ADHOC_RECOVERY_ACTION_REBUILD_GROUP);
  adhoc_recovery_platform_result(&recovery, 61u, 0);
  CHECK(adhoc_recovery_poll(&recovery, 85u) ==
   ADHOC_RECOVERY_ACTION_NONE);
  CHECK(adhoc_recovery_poll(&recovery, 86u) ==
   ADHOC_RECOVERY_ACTION_REBUILD_GROUP);

  CHECK(adhoc_recovery_poll(&recovery, 161u) ==
   ADHOC_RECOVERY_ACTION_RESTART_STACK);
  CHECK(adhoc_recovery_poll(&recovery, 280u) ==
   ADHOC_RECOVERY_ACTION_RESTART_STACK);
  CHECK(adhoc_recovery_poll(&recovery, 281u) ==
   ADHOC_RECOVERY_ACTION_FAIL);
  CHECK(recovery.state == ADHOC_RECOVERY_FAILED);
}

static void test_success_completes_current_tier(void)
{
  AdhocRecovery recovery;

  adhoc_recovery_init(&recovery, 0, &test_config);
  adhoc_recovery_request(&recovery);
  CHECK(adhoc_recovery_poll(&recovery, 1u) ==
   ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT);
  adhoc_recovery_platform_result(&recovery, 2u, 1);
  CHECK(recovery.state == ADHOC_RECOVERY_HEALTHY);
  CHECK(recovery.total_transport_resyncs == 1);

  adhoc_recovery_request(&recovery);
  adhoc_recovery_poll(&recovery, 3u);
  CHECK(adhoc_recovery_poll(&recovery, 63u) ==
   ADHOC_RECOVERY_ACTION_REBUILD_GROUP);
  adhoc_recovery_platform_result(&recovery, 64u, 1);
  CHECK(recovery.state == ADHOC_RECOVERY_HEALTHY);
  CHECK(recovery.total_group_rebuilds == 1);
}

int main(void)
{
  test_idle_probe_recovers_on_peer_activity();
  test_disabled_idle_probe_waits_for_real_error();
  test_explicit_probe_precedes_transport_resync();
  test_explicit_probe_is_cancelled_by_peer_activity();
  test_probe_is_rate_limited();
  test_retry_and_hierarchical_escalation();
  test_success_completes_current_tier();

  if(failures != 0)
  {
    fprintf(stderr, "%d ad-hoc recovery test(s) failed\n", failures);
    return 1;
  }
  puts("ad-hoc recovery tests passed");
  return 0;
}
