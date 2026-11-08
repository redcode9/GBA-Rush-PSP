/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * Fixed-capacity queue of timed hardware events, ordered by deadline, for
 * a scheduler that runs the CPU straight to the next event.
 *
 * Each event has a fixed id below EVENT_QUEUE_CAPACITY and is pending at
 * most once, so the queue never fills up and never allocates. Deadlines
 * are absolute values of a 32-bit cycle counter that wraps; every call
 * that orders events takes the counter's current value, `now`, and every
 * pending deadline must stay less than 2^31 cycles away from it, before or
 * after. A deadline already behind `now` is simply due. Two events due on
 * the same cycle come out lowest id first, whatever order they were
 * scheduled in, so ids double as the service order at equal cycles.
 *
 * Kept free of PSP headers so tests/test_event_queue.c checks it on the
 * host.
 */

#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdint.h>

#define EVENT_QUEUE_CAPACITY 16u

typedef struct
{
  uint32_t deadline[EVENT_QUEUE_CAPACITY]; /* by id, valid while pending */
  uint8_t order[EVENT_QUEUE_CAPACITY];     /* pending ids, soonest last */
  uint32_t pending;                        /* bit n set: id n is queued */
  uint32_t count;                          /* entries used in order[] */
} EventQueue;

/* Empties the queue. A zero-filled EventQueue is also empty. */
void event_queue_clear(EventQueue *queue);

/* Queues event id for the cycle `deadline`, or moves it there if it is
   already pending. Ids at or above EVENT_QUEUE_CAPACITY are ignored. */
void event_queue_schedule(EventQueue *queue, uint32_t id, uint32_t deadline,
 uint32_t now);

/* Drops event id if it is pending. */
void event_queue_cancel(EventQueue *queue, uint32_t id);

/* The soonest event without removing it: its id, and its deadline in
   *deadline. Returns -1 and leaves *deadline alone when the queue is
   empty. */
int event_queue_next(const EventQueue *queue, uint32_t *deadline);

/* Removes and returns the soonest event if it is due at `now`, else -1. */
int event_queue_pop_due(EventQueue *queue, uint32_t now);

static inline int event_queue_pending(const EventQueue *queue, uint32_t id)
{
  return id < EVENT_QUEUE_CAPACITY && ((queue->pending >> id) & 1u) != 0;
}

#endif /* EVENT_QUEUE_H */
