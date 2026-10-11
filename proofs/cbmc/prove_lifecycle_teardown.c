#include <stdint.h>

#include "lifecycle_teardown.h"

extern int nondet_int(void);
extern uint32_t nondet_uint32(void);
extern uint64_t nondet_uint64(void);

static int teardown_valid(const LifecycleTeardown *teardown)
{
  if(teardown->phase > LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED)
    return 0;
  if(teardown->phase == LIFECYCLE_TEARDOWN_IDLE)
    return teardown->cancellation_token == 0;
  return teardown->cancellation_token != 0;
}

int main(void)
{
  LifecycleTeardown teardown;
  LifecycleTeardownPhase before_phase;
  LifecycleTeardownPhase requested;
  LifecycleTeardownPoll poll_result;
  uint64_t before_deadline;
  uint64_t now_us = nondet_uint64();
  uint64_t timeout_us = nondet_uint64();
  int worker_stopped = nondet_int();
  uint32_t raw_phase = nondet_uint32();
  uint32_t raw_requested = nondet_uint32();

  __CPROVER_assume(raw_phase <=
   (uint32_t)LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED);
  __CPROVER_assume(raw_requested >
   (uint32_t)LIFECYCLE_TEARDOWN_IDLE &&
   raw_requested < (uint32_t)LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED);
  teardown.phase = (LifecycleTeardownPhase)raw_phase;
  requested = (LifecycleTeardownPhase)raw_requested;
  __CPROVER_assume(teardown_valid(&teardown));
  __CPROVER_assume(worker_stopped == 0 || worker_stopped == 1);

  before_phase = lifecycle_teardown_phase(&teardown);
  before_deadline = teardown.deadline_us;
  lifecycle_teardown_begin(&teardown, now_us, timeout_us);
  __CPROVER_assert(teardown_valid(&teardown),
   "begin publishes a valid cancellation token");
  if(before_phase != LIFECYCLE_TEARDOWN_IDLE)
    __CPROVER_assert(teardown.deadline_us <= before_deadline,
     "repeated cancellation cannot renew its deadline");

  before_phase = lifecycle_teardown_phase(&teardown);
  lifecycle_teardown_advance(&teardown, requested);
  __CPROVER_assert(lifecycle_teardown_phase(&teardown) >= before_phase,
   "teardown phase never regresses");
  __CPROVER_assert(teardown_valid(&teardown),
   "phase advance preserves lifecycle invariant");

  poll_result = lifecycle_teardown_poll(&teardown, now_us, worker_stopped);
  __CPROVER_assert(poll_result >= LIFECYCLE_TEARDOWN_POLL_PENDING &&
   poll_result <= LIFECYCLE_TEARDOWN_POLL_DEADLINE_EXPIRED,
   "poll returns a declared result");
  if(poll_result == LIFECYCLE_TEARDOWN_POLL_PENDING)
    __CPROVER_assert(now_us < teardown.deadline_us,
     "pending cleanup remains inside its absolute deadline");
  if(poll_result == LIFECYCLE_TEARDOWN_POLL_DEADLINE_EXPIRED)
    __CPROVER_assert(lifecycle_teardown_phase(&teardown) ==
     LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED,
     "deadline expiry records the exact terminal phase");
  return 0;
}
