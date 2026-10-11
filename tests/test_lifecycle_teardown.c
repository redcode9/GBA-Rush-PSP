#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "lifecycle_teardown.h"

static const LifecycleTeardownPhase ordered_phases[] =
{
  LIFECYCLE_TEARDOWN_WORKER_SIGNALED,
  LIFECYCLE_TEARDOWN_WAITING,
  LIFECYCLE_TEARDOWN_WORKER_QUIESCED,
  LIFECYCLE_TEARDOWN_RESOURCES_CLOSED,
  LIFECYCLE_TEARDOWN_MODULES_UNLOADED,
  LIFECYCLE_TEARDOWN_WORKER_STOPPED,
  LIFECYCLE_TEARDOWN_COMPLETE
};

static void test_complete_teardown(void)
{
  LifecycleTeardown teardown;
  size_t index;

  lifecycle_teardown_init(&teardown);
  assert(lifecycle_teardown_begin(&teardown, 1000u, 500u) == 1u);
  assert(lifecycle_teardown_cancelled(&teardown));
  assert(lifecycle_teardown_phase(&teardown) ==
   LIFECYCLE_TEARDOWN_ADMISSION_STOPPED);
  assert(lifecycle_teardown_remaining_us(&teardown, 1000u) == 500u);
  for(index = 0; index < sizeof(ordered_phases) /
      sizeof(ordered_phases[0]); index++)
    assert(lifecycle_teardown_advance(&teardown, ordered_phases[index]));
  assert(lifecycle_teardown_phase(&teardown) ==
   LIFECYCLE_TEARDOWN_COMPLETE);
  assert(lifecycle_teardown_poll(&teardown, 2000u, 0) ==
   LIFECYCLE_TEARDOWN_POLL_WORKER_STOPPED);
  lifecycle_teardown_expire(&teardown);
  assert(lifecycle_teardown_phase(&teardown) ==
   LIFECYCLE_TEARDOWN_COMPLETE);
}

static void test_deadline_is_absolute_and_not_renewed(void)
{
  LifecycleTeardown teardown;

  lifecycle_teardown_init(&teardown);
  assert(lifecycle_teardown_begin(&teardown, UINT64_MAX - 5u, 20u) == 1u);
  assert(teardown.deadline_us == UINT64_MAX);

  lifecycle_teardown_init(&teardown);
  assert(lifecycle_teardown_begin(&teardown, 2000u, 400u) == 1u);
  assert(lifecycle_teardown_begin(&teardown, 2200u, 4000u) == 1u);
  assert(teardown.deadline_us == 2400u);
  assert(lifecycle_teardown_begin(&teardown, 2200u, 100u) == 1u);
  assert(teardown.deadline_us == 2300u);
  assert(lifecycle_teardown_remaining_us(&teardown, 2299u) == 1u);
  assert(lifecycle_teardown_remaining_us(&teardown, 2300u) == 0u);
}

static void test_fault_at_each_externally_visible_phase(void)
{
  unsigned int exit_path;
  size_t fault;

  for(exit_path = 0; exit_path < 2u; exit_path++)
  {
    uint64_t timeout = exit_path ? 250u : 1000u;

    for(fault = 0; fault < sizeof(ordered_phases) /
        sizeof(ordered_phases[0]); fault++)
    {
      LifecycleTeardown teardown;
      size_t phase;

      lifecycle_teardown_init(&teardown);
      assert(lifecycle_teardown_begin(&teardown, 10000u, timeout) == 1u);
      for(phase = 0; phase < fault; phase++)
        assert(lifecycle_teardown_advance(&teardown,
         ordered_phases[phase]));

      assert(lifecycle_teardown_poll(&teardown,
       10000u + timeout - 1u, 0) == LIFECYCLE_TEARDOWN_POLL_PENDING);
      assert(lifecycle_teardown_poll(&teardown, 10000u + timeout, 0) ==
       LIFECYCLE_TEARDOWN_POLL_DEADLINE_EXPIRED);
      assert(lifecycle_teardown_phase(&teardown) ==
       LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED);
      assert(!lifecycle_teardown_advance(&teardown,
       ordered_phases[fault]));
    }
  }
}

static void test_nonblocking_poll_observes_worker_stop(void)
{
  LifecycleTeardown teardown;

  lifecycle_teardown_init(&teardown);
  lifecycle_teardown_begin(&teardown, 100u, 50u);
  assert(lifecycle_teardown_poll(&teardown, 100u, 1) ==
   LIFECYCLE_TEARDOWN_POLL_WORKER_STOPPED);
  assert(lifecycle_teardown_phase(&teardown) ==
   LIFECYCLE_TEARDOWN_ADMISSION_STOPPED);
}

static void test_invalid_inputs(void)
{
  LifecycleTeardown teardown;

  lifecycle_teardown_init(&teardown);
  assert(lifecycle_teardown_begin(NULL, 0, 1) == 0);
  assert(!lifecycle_teardown_cancelled(NULL));
  assert(!lifecycle_teardown_advance(NULL,
   LIFECYCLE_TEARDOWN_WORKER_SIGNALED));
  assert(lifecycle_teardown_remaining_us(NULL, 0) == 0);
  assert(lifecycle_teardown_phase(NULL) == LIFECYCLE_TEARDOWN_IDLE);
  assert(!lifecycle_teardown_advance(&teardown,
   LIFECYCLE_TEARDOWN_IDLE));
}

int main(void)
{
  test_complete_teardown();
  test_deadline_is_absolute_and_not_renewed();
  test_fault_at_each_externally_visible_phase();
  test_nonblocking_poll_observes_worker_stop();
  test_invalid_inputs();
  puts("Lifecycle teardown tests passed");
  return 0;
}
