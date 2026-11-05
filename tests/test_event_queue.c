/*
 * Host tests for event_queue.c, the deadline-ordered queue of timed
 * hardware events: ordering, ties served by id, reschedule and cancel,
 * deadlines across the wrap of the 32-bit cycle counter, a long random
 * run checked operation by operation against a plain linear model, and
 * update_gba's pass run from the queue against the counters it replaced.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "event_queue.h"

#define FUZZ_OPERATIONS 100000u

static void test_events_come_out_by_deadline(void)
{
  EventQueue queue;
  uint32_t deadline = 0;

  event_queue_clear(&queue);
  assert(event_queue_next(&queue, &deadline) == -1);
  assert(event_queue_pop_due(&queue, 0) == -1);

  event_queue_schedule(&queue, 3, 300, 0);
  event_queue_schedule(&queue, 1, 100, 0);
  event_queue_schedule(&queue, 2, 200, 0);
  assert(event_queue_next(&queue, &deadline) == 1 && deadline == 100);
  assert(event_queue_pop_due(&queue, 99) == -1);
  assert(event_queue_pop_due(&queue, 250) == 1);
  assert(event_queue_pop_due(&queue, 250) == 2);
  assert(event_queue_pop_due(&queue, 250) == -1);
  assert(event_queue_pending(&queue, 3) && !event_queue_pending(&queue, 1));
  assert(event_queue_pop_due(&queue, 300) == 3);
  assert(queue.count == 0 && queue.pending == 0);
}

/* At the same cycle the lower id comes out first, in whatever order the
   events were queued: ids stand for today's service order. */
static void test_ties_come_out_by_id(void)
{
  EventQueue queue;

  memset(&queue, 0, sizeof(queue));
  event_queue_schedule(&queue, 9, 50, 0);
  event_queue_schedule(&queue, 0, 50, 0);
  event_queue_schedule(&queue, 4, 50, 0);
  event_queue_schedule(&queue, 15, 49, 0);
  assert(event_queue_pop_due(&queue, 50) == 15);
  assert(event_queue_pop_due(&queue, 50) == 0);
  assert(event_queue_pop_due(&queue, 50) == 4);
  assert(event_queue_pop_due(&queue, 50) == 9);
}

static void test_reschedule_and_cancel(void)
{
  EventQueue queue;
  uint32_t deadline = 0;

  event_queue_clear(&queue);
  event_queue_schedule(&queue, 2, 10, 0);
  event_queue_schedule(&queue, 5, 20, 0);
  event_queue_schedule(&queue, 2, 30, 0);
  assert(queue.count == 2);
  assert(event_queue_next(&queue, &deadline) == 5 && deadline == 20);
  event_queue_schedule(&queue, 2, 5, 0);
  assert(event_queue_next(&queue, &deadline) == 2 && deadline == 5);

  event_queue_cancel(&queue, 2);
  event_queue_cancel(&queue, 2);
  event_queue_cancel(&queue, 7);
  event_queue_cancel(&queue, EVENT_QUEUE_CAPACITY);
  event_queue_schedule(&queue, EVENT_QUEUE_CAPACITY, 1, 0);
  assert(queue.count == 1 && !event_queue_pending(&queue, 2));
  assert(!event_queue_pending(&queue, EVENT_QUEUE_CAPACITY));
  assert(event_queue_next(&queue, &deadline) == 5 && deadline == 20);
}

/* Deadlines past 0xFFFFFFFF wrap to small values and still come after the
   ones before the wrap; an event already behind the clock is due first. */
static void test_deadlines_across_the_wrap(void)
{
  EventQueue queue;
  uint32_t now = 0xFFFFFF00u;

  event_queue_clear(&queue);
  event_queue_schedule(&queue, 1, 0x40, now);
  event_queue_schedule(&queue, 2, 0xFFFFFFF0u, now);
  event_queue_schedule(&queue, 3, now - 8, now);
  assert(event_queue_pop_due(&queue, now) == 3);
  assert(event_queue_pop_due(&queue, now) == -1);
  assert(event_queue_pop_due(&queue, 0x10) == 2);
  assert(event_queue_pop_due(&queue, 0x10) == -1);
  assert(event_queue_pop_due(&queue, 0x40) == 1);
}

/* The model: one deadline per id and a flag, and the soonest event found
   by scanning every id. */
typedef struct
{
  uint32_t deadline[EVENT_QUEUE_CAPACITY];
  uint8_t pending[EVENT_QUEUE_CAPACITY];
} Model;

static int model_next(const Model *model, uint32_t now)
{
  int best = -1;
  uint32_t id;

  for(id = 0; id < EVENT_QUEUE_CAPACITY; id++)
  {
    if(model->pending[id] && (best < 0 ||
     (int32_t)(model->deadline[id] - now) <
     (int32_t)(model->deadline[best] - now)))
      best = (int)id;
  }
  return best;
}

static uint32_t fuzz_state = 0x2545F491u;

static uint32_t fuzz_random(void)
{
  fuzz_state ^= fuzz_state << 13;
  fuzz_state ^= fuzz_state >> 17;
  fuzz_state ^= fuzz_state << 5;
  return fuzz_state;
}

/* A deadline the scheduler could ask for: often one of a few nearby cycles
   so ties are common, sometimes already late, sometimes far ahead. */
static uint32_t fuzz_deadline(uint32_t now)
{
  switch(fuzz_random() % 4)
  {
    case 0:
      return now + (fuzz_random() % 4) * 16;
    case 1:
      return now - (fuzz_random() % 1024);
    case 2:
      return now + (fuzz_random() % 1232);
    default:
      return now + (fuzz_random() & 0x03FFFFFFu);
  }
}

/* The whole queue, not only its head, must match the model: popping a copy
   empty gives every pending event in the order the model serves them. */
static void fuzz_compare(const EventQueue *queue, const Model *model,
 uint32_t now)
{
  EventQueue copy = *queue;
  Model left = *model;
  uint32_t deadline = 0;
  uint32_t count = 0;
  uint32_t id;
  int expected;

  for(id = 0; id < EVENT_QUEUE_CAPACITY; id++)
  {
    assert(event_queue_pending(queue, id) == left.pending[id]);
    count += left.pending[id];
  }
  assert(queue->count == count);

  expected = model_next(&left, now);
  assert(event_queue_next(queue, &deadline) == expected);
  if(expected >= 0)
    assert(deadline == model->deadline[expected]);

  while((expected = model_next(&left, now)) >= 0)
  {
    /* Pop at the head's own deadline: the copy must hand out exactly it. */
    uint32_t at = left.deadline[expected];

    if((int32_t)(at - now) < 0)
      at = now;
    assert(event_queue_pop_due(&copy, at) == expected);
    left.pending[expected] = 0;
  }
  assert(copy.count == 0 && copy.pending == 0);
}

/* 100,000 random schedules, reschedules, cancels, peeks and clock steps,
   with each due event served the way the scheduler will: pop, then maybe
   queue it again from its own deadline, as a periodic event does. The
   clock starts just before the wrap and wraps many times. */
static void test_fuzz_against_linear_model(void)
{
  EventQueue queue;
  Model model;
  uint32_t now = 0xFFFF0000u;
  uint32_t operation;
  uint32_t full_checks = 0;

  event_queue_clear(&queue);
  memset(&model, 0, sizeof(model));

  for(operation = 0; operation < FUZZ_OPERATIONS; operation++)
  {
    uint32_t id = fuzz_random() % EVENT_QUEUE_CAPACITY;
    uint32_t choice = fuzz_random() % 10;
    uint32_t deadline = 0;
    int expected;

    if(choice < 4)
    {
      deadline = fuzz_deadline(now);
      event_queue_schedule(&queue, id, deadline, now);
      model.deadline[id] = deadline;
      model.pending[id] = 1;
    }
    else if(choice < 6)
    {
      event_queue_cancel(&queue, id);
      model.pending[id] = 0;
    }
    else if(choice < 7)
    {
      expected = model_next(&model, now);
      if(expected >= 0 && (int32_t)(model.deadline[expected] - now) > 0)
        expected = -1;
      assert(event_queue_pop_due(&queue, now) == expected);
      if(expected >= 0)
        model.pending[expected] = 0;
    }
    else if(choice < 9)
    {
      now += fuzz_random() % (1u << (fuzz_random() % 25));
      while((expected = model_next(&model, now)) >= 0 &&
       (int32_t)(model.deadline[expected] - now) <= 0)
      {
        assert(event_queue_pop_due(&queue, now) == expected);
        model.pending[expected] = 0;
        if(fuzz_random() % 2)
        {
          deadline = model.deadline[expected] + 1 + fuzz_random() % 2048;
          event_queue_schedule(&queue, (uint32_t)expected, deadline, now);
          model.deadline[expected] = deadline;
          model.pending[expected] = 1;
        }
      }
      assert(event_queue_pop_due(&queue, now) == -1);
    }
    else
    {
      fuzz_compare(&queue, &model, now);
      full_checks++;
    }

    expected = model_next(&model, now);
    assert(event_queue_next(&queue, &deadline) == expected);
    if(expected >= 0)
      assert(deadline == model.deadline[expected]);
  }
  assert(full_checks > FUZZ_OPERATIONS / 20);
}

/* The wait handed to the CPU: the nearest event ahead, never one already
   behind the clock, which waits for the next pop. */
static void test_cycles_until(void)
{
  EventQueue queue;

  event_queue_clear(&queue);
  assert(event_queue_cycles_until(&queue, 0) == UINT32_MAX);
  event_queue_schedule(&queue, 5, 1000, 0);
  event_queue_schedule(&queue, 1, 300, 0);
  assert(event_queue_cycles_until(&queue, 100) == 200);
  assert(event_queue_cycles_until(&queue, 300) == 0);
  event_queue_schedule(&queue, 2, 290, 300);
  assert(event_queue_cycles_until(&queue, 300) == 0);
  assert(event_queue_pop_due(&queue, 300) == 2);
  assert(event_queue_pop_due(&queue, 300) == 1);
  /* A popped event keeps its deadline, from which the next one counts. */
  assert(queue.deadline[1] == 300 && queue.deadline[2] == 290);
  event_queue_schedule(&queue, 2, 295, 300);
  assert(event_queue_cycles_until(&queue, 300) == 700);
  event_queue_clear(&queue);
  event_queue_schedule(&queue, 3, 0xFFFFFFF0u, 0xFFFFFFF8u);
  assert(event_queue_cycles_until(&queue, 0xFFFFFFF8u) == 0xFFFFFFF8u);
}

/* update_gba's pass two ways: with a counter per event as it had before the
   queue (cycles left, less each slice, due at 0 or below, the next slice
   the smallest counter taken as unsigned), and from the queue, collecting
   the due events in a mask, serving them lowest id first and moving each
   from its old deadline. Slices end early at random, as an alert ends
   them, or run past the budget, as the New core runs; timer 1 restarts
   at the start of a slice with periods shorter than a slice, so it falls
   behind and comes due on every pass. Each pass must serve the same
   events in the same order, keep counters equal to deadline - now and
   hand out the same slice. */
static void test_pass_matches_counters(void)
{
  /* main.c's ids: the line comes after the timers. */
  enum { TIMER0 = 1, TIMER1 = 2, LINE = 5, EVENTS = 3, PASSES = 200000 };
  static const uint32_t ids[EVENTS] = { TIMER0, TIMER1, LINE };
  uint32_t period[EVENTS] = { 1000, 16, 272 };
  uint32_t line_period[2] = { 272, 960 };
  int32_t count[EVENTS];
  EventQueue queue;
  uint32_t now = 0xFFFF0000u;
  uint32_t slice = UINT32_MAX;
  uint32_t pass;
  uint32_t k;
  uint32_t late = 0;

  event_queue_clear(&queue);
  for(k = 0; k < EVENTS; k++)
  {
    count[k] = (int32_t)period[k];
    event_queue_schedule(&queue, ids[k], now + period[k], now);
    if((uint32_t)count[k] < slice)
      slice = (uint32_t)count[k];
  }

  for(pass = 0; pass < PASSES; pass++)
  {
    uint32_t elapsed = slice;
    uint32_t served_counters[EVENTS];
    uint32_t served_queue[EVENTS];
    uint32_t served = 0;
    uint32_t served_from_queue = 0;
    uint32_t due = 0;
    int event;

    assert(event_queue_cycles_until(&queue, now) == slice);
    if(fuzz_random() % 50 == 0)
    {
      period[1] = 1 + fuzz_random() % 64;
      count[1] = (int32_t)period[1];
      event_queue_schedule(&queue, TIMER1, now + period[1], now);
    }
    switch(fuzz_random() % 4)
    {
      case 0:
        elapsed = fuzz_random() % (slice + 1);
        break;
      case 1:
        elapsed = slice + fuzz_random() % 8;
        break;
    }
    now += elapsed;

    for(k = 0; k < EVENTS; k++)
    {
      count[k] -= (int32_t)elapsed;
      if(count[k] <= 0)
      {
        served_counters[served++] = ids[k];
        count[k] += (int32_t)period[k];
      }
    }

    while((event = event_queue_pop_due(&queue, now)) >= 0)
      due |= 1u << event;
    for(k = 0; k < EVENTS; k++)
    {
      if((due & (1u << ids[k])) != 0)
      {
        served_queue[served_from_queue++] = ids[k];
        event_queue_schedule(&queue, ids[k],
         queue.deadline[ids[k]] + period[k], now);
      }
    }

    assert(served_from_queue == served);
    assert(memcmp(served_counters, served_queue,
     served * sizeof(served_queue[0])) == 0);
    if((due & (1u << LINE)) != 0)
      period[2] = line_period[period[2] == 272];

    slice = UINT32_MAX;
    for(k = 0; k < EVENTS; k++)
    {
      assert(count[k] == (int32_t)(queue.deadline[ids[k]] - now));
      if((uint32_t)count[k] < slice)
        slice = (uint32_t)count[k];
    }
    late += count[1] <= 0;
  }
  /* The test covers a timer behind the clock. */
  assert(late > PASSES / 100);
}

int main(void)
{
  test_events_come_out_by_deadline();
  test_ties_come_out_by_id();
  test_reschedule_and_cancel();
  test_deadlines_across_the_wrap();
  test_fuzz_against_linear_model();
  test_cycles_until();
  test_pass_matches_counters();
  puts("Event queue tests passed");
  return 0;
}
