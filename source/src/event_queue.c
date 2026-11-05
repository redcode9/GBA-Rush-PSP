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

#include "event_queue.h"

#include <string.h>

/* The queue is a sorted array with a linear insert and cancel: with at
   most 16 events that is cheaper than a heap. A heap pays off only with
   many more events. */

/* Whether event a, due at a_deadline, comes out before event b. */
static int event_queue_before(uint32_t a, uint32_t a_deadline, uint32_t b,
 uint32_t b_deadline, uint32_t now)
{
  int32_t a_offset = (int32_t)(a_deadline - now);
  int32_t b_offset = (int32_t)(b_deadline - now);

  if(a_offset != b_offset)
    return a_offset < b_offset;
  return a < b;
}

void event_queue_clear(EventQueue *queue)
{
  queue->pending = 0;
  queue->count = 0;
}

void event_queue_schedule(EventQueue *queue, uint32_t id, uint32_t deadline,
 uint32_t now)
{
  uint32_t position = 0;

  if(id >= EVENT_QUEUE_CAPACITY)
    return;

  event_queue_cancel(queue, id);

  /* order[] holds the events that come out after this one first. */
  while(position < queue->count &&
   event_queue_before(id, deadline, queue->order[position],
   queue->deadline[queue->order[position]], now))
    position++;

  memmove(queue->order + position + 1, queue->order + position,
   queue->count - position);
  queue->order[position] = (uint8_t)id;
  queue->deadline[id] = deadline;
  queue->pending |= 1u << id;
  queue->count++;
}

void event_queue_cancel(EventQueue *queue, uint32_t id)
{
  uint32_t position = 0;

  if(!event_queue_pending(queue, id))
    return;

  while(queue->order[position] != id)
    position++;

  memmove(queue->order + position, queue->order + position + 1,
   queue->count - position - 1);
  queue->pending &= ~(1u << id);
  queue->count--;
}

int event_queue_next(const EventQueue *queue, uint32_t *deadline)
{
  uint32_t id;

  if(queue->count == 0)
    return -1;

  id = queue->order[queue->count - 1];
  *deadline = queue->deadline[id];
  return (int)id;
}

int event_queue_pop_due(EventQueue *queue, uint32_t now)
{
  uint32_t id;

  if(queue->count == 0)
    return -1;

  id = queue->order[queue->count - 1];
  if((int32_t)(queue->deadline[id] - now) > 0)
    return -1;

  queue->pending &= ~(1u << id);
  queue->count--;
  return (int)id;
}

uint32_t event_queue_cycles_until(const EventQueue *queue, uint32_t now)
{
  uint32_t position = queue->count;
  uint32_t cycles = UINT32_MAX;

  /* Soonest first: the events behind now, then the first one ahead, which
     is nearer than every later one. */
  while(position-- > 0)
  {
    uint32_t offset = queue->deadline[queue->order[position]] - now;

    if(offset < cycles)
      cycles = offset;
    if((int32_t)offset >= 0)
      break;
  }
  return cycles;
}
