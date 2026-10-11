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

#include "link_transport_scheduler.h"

#include <limits.h>
#include <string.h>

static uint32_t link_scheduler_load_acquire(
 const volatile uint32_t *value)
{
  return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void link_scheduler_store_release(volatile uint32_t *target,
 uint32_t value)
{
  __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static void link_scheduler_increment(volatile uint32_t *value)
{
  uint32_t current;
  uint32_t next;

  do
  {
    current = __atomic_load_n(value, __ATOMIC_RELAXED);
    if(current == UINT_MAX)
      return;
    next = current + 1u;
  }
  while(!__atomic_compare_exchange_n(value, &current, next, 0,
         __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

static uint32_t link_scheduler_take_logical(
 LinkTransportScheduler *scheduler)
{
  uint32_t current;
  uint32_t expected;
  uint32_t next;

  for(;;)
  {
    current = __atomic_load_n(&scheduler->next_logical_message,
     __ATOMIC_RELAXED);
    if(current == 0)
      current = 1u;
    expected = current;
    next = link_transport_next_number(current);
    if(__atomic_compare_exchange_n(&scheduler->next_logical_message,
       &expected, next, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
      return current;
  }
}

void link_transport_scheduler_init(LinkTransportScheduler *scheduler)
{
  if(scheduler == NULL)
    return;
  memset(scheduler, 0, sizeof(*scheduler));
  link_spsc_ring_init(&scheduler->handoff);
}

void link_transport_scheduler_reset(LinkTransportScheduler *scheduler,
 uint32_t connection_id, uint32_t epoch)
{
  unsigned int state_class;
  unsigned int slot_index;

  if(scheduler == NULL)
    return;
  __atomic_store_n(&scheduler->published_epoch, 0u, __ATOMIC_RELEASE);
  link_spsc_ring_discard(&scheduler->handoff);
  for(state_class = LINK_STATE_SIO; state_class < LINK_STATE_COUNT;
      state_class++)
  {
    LinkReplaceableMailbox *mailbox =
     &scheduler->replaceable[state_class];

    for(slot_index = 0; slot_index < LINK_REPLACEABLE_MAILBOX_SLOTS;
        slot_index++)
    {
      uint32_t expected = LINK_MAILBOX_SLOT_READY;

      if(__atomic_compare_exchange_n(&mailbox->slots[slot_index].state,
         &expected, LINK_MAILBOX_SLOT_READING, 0, __ATOMIC_ACQ_REL,
         __ATOMIC_ACQUIRE))
      {
        link_scheduler_store_release(&mailbox->slots[slot_index].state,
         LINK_MAILBOX_SLOT_FREE);
      }
    }
    __atomic_store_n(&mailbox->published, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&mailbox->coalesced, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&mailbox->high_water, 0u, __ATOMIC_RELAXED);
  }
  link_transport_lanes_init(&scheduler->lanes, connection_id, epoch);
  scheduler->critical_pending_read = 0;
  scheduler->critical_pending_write = 0;
  scheduler->critical_pending_count = 0;
  scheduler->critical_pending_high_water = 0;
  scheduler->high_water = 0;
  __atomic_store_n(&scheduler->critical_retry_suppressed, 0u,
   __ATOMIC_RELAXED);
  __atomic_store_n(&scheduler->next_logical_message, 1u,
   __ATOMIC_RELEASE);
  __atomic_store_n(&scheduler->published_epoch, epoch, __ATOMIC_RELEASE);
}

int link_transport_scheduler_set_profile(LinkTransportScheduler *scheduler,
 LinkTransportProfile profile, uint32_t ack_grace_us)
{
  return scheduler != NULL && link_transport_lanes_set_profile(
   &scheduler->lanes, profile, ack_grace_us);
}

static int link_scheduler_publish_replaceable(
 LinkTransportScheduler *scheduler, LinkReplaceableClass state_class,
 LinkLogicalId logical_id, uint64_t enqueued_us, const uint8_t *data,
 size_t size)
{
  LinkReplaceableMailbox *mailbox;
  LinkReplaceableMailboxSlot *slot = NULL;
  uint32_t expected;
  unsigned int slot_index;
  int replaced = 0;

  if(state_class <= LINK_STATE_NONE || state_class >= LINK_STATE_COUNT ||
     data == NULL || size == 0 || size > LINK_PROTOCOL_MAX_PACKET)
    return 0;
  mailbox = &scheduler->replaceable[state_class];

  for(slot_index = 0; slot_index < LINK_REPLACEABLE_MAILBOX_SLOTS;
      slot_index++)
  {
    expected = LINK_MAILBOX_SLOT_READY;
    if(__atomic_compare_exchange_n(&mailbox->slots[slot_index].state,
       &expected, LINK_MAILBOX_SLOT_WRITING, 0, __ATOMIC_ACQ_REL,
       __ATOMIC_ACQUIRE))
    {
      slot = &mailbox->slots[slot_index];
      replaced = 1;
      break;
    }
  }
  if(slot == NULL)
  {
    for(slot_index = 0; slot_index < LINK_REPLACEABLE_MAILBOX_SLOTS;
        slot_index++)
    {
      expected = LINK_MAILBOX_SLOT_FREE;
      if(__atomic_compare_exchange_n(&mailbox->slots[slot_index].state,
         &expected, LINK_MAILBOX_SLOT_WRITING, 0, __ATOMIC_ACQ_REL,
         __ATOMIC_ACQUIRE))
      {
        slot = &mailbox->slots[slot_index];
        break;
      }
    }
  }
  if(slot == NULL)
    return 0;

  slot->item.lane = LINK_LANE_REPLACEABLE;
  slot->item.state_class = state_class;
  slot->item.logical_id = logical_id;
  slot->item.enqueued_us = enqueued_us;
  slot->item.size = (uint16_t)size;
  memcpy(slot->item.data, data, size);
  link_scheduler_store_release(&slot->state, LINK_MAILBOX_SLOT_READY);
  link_scheduler_increment(&mailbox->published);
  if(replaced)
    link_scheduler_increment(&mailbox->coalesced);
  if(link_scheduler_load_acquire(&mailbox->high_water) == 0)
    link_scheduler_store_release(&mailbox->high_water, 1u);
  return 1;
}

static int link_scheduler_take_replaceable(
 LinkReplaceableMailbox *mailbox, LinkSpscItem *item)
{
  unsigned int slot_index;

  for(slot_index = 0; slot_index < LINK_REPLACEABLE_MAILBOX_SLOTS;
      slot_index++)
  {
    LinkReplaceableMailboxSlot *slot = &mailbox->slots[slot_index];
    uint32_t expected = LINK_MAILBOX_SLOT_READY;

    if(!__atomic_compare_exchange_n(&slot->state, &expected,
       LINK_MAILBOX_SLOT_READING, 0, __ATOMIC_ACQ_REL,
       __ATOMIC_ACQUIRE))
      continue;
    *item = slot->item;
    link_scheduler_store_release(&slot->state, LINK_MAILBOX_SLOT_FREE);
    return 1;
  }
  return 0;
}

static int link_scheduler_retryable_critical(const uint8_t *data,
 size_t size)
{
  uint8_t type;

  if(data == NULL || size < LINK_PROTOCOL_HEADER_SIZE ||
     size > LINK_PROTOCOL_MAX_PACKET)
    return 0;
  type = data[6];
  return type == LINK_MESSAGE_RESUME || type == LINK_MESSAGE_RESUME_ACK ||
   type == LINK_MESSAGE_RESUME_DONE;
}

static int link_scheduler_retry_pending(LinkTransportScheduler *scheduler,
 uint32_t epoch, const uint8_t *data, size_t size)
{
  unsigned int index;

  for(index = 0; index < LINK_CRITICAL_RETRY_TRACKERS; index++)
  {
    LinkCriticalRetryTracker *tracker = &scheduler->retry[index];
    uint32_t pending = link_scheduler_load_acquire(
     &tracker->pending_message);
    uint32_t acknowledged = link_scheduler_load_acquire(
     &tracker->acknowledged_message);

    if(pending == 0 || pending == acknowledged ||
       link_scheduler_load_acquire(&tracker->epoch) != epoch ||
       tracker->size != size || memcmp(tracker->data, data, size) != 0)
      continue;
    link_scheduler_increment(&scheduler->critical_retry_suppressed);
    return 1;
  }
  return 0;
}

static int link_scheduler_track_retry(LinkTransportScheduler *scheduler,
 LinkLogicalId logical_id, const uint8_t *data, size_t size)
{
  unsigned int index;

  for(index = 0; index < LINK_CRITICAL_RETRY_TRACKERS; index++)
  {
    LinkCriticalRetryTracker *tracker = &scheduler->retry[index];
    uint32_t pending = link_scheduler_load_acquire(
     &tracker->pending_message);
    uint32_t acknowledged = link_scheduler_load_acquire(
     &tracker->acknowledged_message);
    uint32_t epoch = link_scheduler_load_acquire(&tracker->epoch);

    if(pending != 0 && pending != acknowledged && epoch == logical_id.epoch)
      continue;
    tracker->size = (uint16_t)size;
    memcpy(tracker->data, data, size);
    __atomic_store_n(&tracker->acknowledged_message, 0u,
     __ATOMIC_RELAXED);
    __atomic_store_n(&tracker->epoch, logical_id.epoch, __ATOMIC_RELAXED);
    link_scheduler_store_release(&tracker->pending_message,
     logical_id.message);
    return (int)index + 1;
  }
  return 0;
}

static void link_scheduler_cancel_retry(LinkTransportScheduler *scheduler,
 int tracker_index, uint32_t message)
{
  LinkCriticalRetryTracker *tracker;
  uint32_t expected;

  if(tracker_index <= 0 ||
     tracker_index > (int)LINK_CRITICAL_RETRY_TRACKERS)
    return;
  tracker = &scheduler->retry[(unsigned int)tracker_index - 1u];
  expected = message;
  __atomic_compare_exchange_n(&tracker->pending_message, &expected, 0u, 0,
   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void link_scheduler_acknowledge_retry(
 LinkTransportScheduler *scheduler, LinkLogicalId logical_id)
{
  unsigned int index;

  for(index = 0; index < LINK_CRITICAL_RETRY_TRACKERS; index++)
  {
    LinkCriticalRetryTracker *tracker = &scheduler->retry[index];
    uint32_t pending = link_scheduler_load_acquire(
     &tracker->pending_message);

    if(pending != logical_id.message ||
       link_scheduler_load_acquire(&tracker->epoch) != logical_id.epoch)
      continue;
    link_scheduler_store_release(&tracker->acknowledged_message, pending);
    return;
  }
}

int link_transport_scheduler_queue(LinkTransportScheduler *scheduler,
 LinkTransportLane lane, LinkReplaceableClass state_class,
 uint64_t enqueued_us, const uint8_t *data, size_t size)
{
  LinkLogicalId logical_id;
  int retry_tracker = 0;
  int retryable;
  int queued;

  if(scheduler == NULL || lane < LINK_LANE_IMMEDIATE ||
     lane > LINK_LANE_REPLACEABLE || data == NULL || size == 0 ||
     size > LINK_PROTOCOL_MAX_PACKET ||
     (lane == LINK_LANE_REPLACEABLE &&
      (state_class <= LINK_STATE_NONE || state_class >= LINK_STATE_COUNT)) ||
     (lane != LINK_LANE_REPLACEABLE && state_class != LINK_STATE_NONE))
    return 0;
  logical_id.epoch = __atomic_load_n(&scheduler->published_epoch,
   __ATOMIC_ACQUIRE);
  if(logical_id.epoch == 0)
    return 0;
  retryable = lane == LINK_LANE_CRITICAL &&
   link_scheduler_retryable_critical(data, size);
  if(retryable && link_scheduler_retry_pending(scheduler,
     logical_id.epoch, data, size))
    return 1;
  logical_id.message = link_scheduler_take_logical(scheduler);
  if(lane == LINK_LANE_REPLACEABLE)
  {
    return link_scheduler_publish_replaceable(scheduler, state_class,
     logical_id, enqueued_us, data, size);
  }
  if(retryable)
    retry_tracker = link_scheduler_track_retry(scheduler, logical_id, data,
     size);
  queued = link_spsc_ring_push(&scheduler->handoff, lane, state_class,
   logical_id, enqueued_us, data, size);
  if(!queued)
    link_scheduler_cancel_retry(scheduler, retry_tracker,
     logical_id.message);
  return queued;
}

static int link_scheduler_defer_critical(LinkTransportScheduler *scheduler,
 const LinkSpscItem *item)
{
  if(scheduler->critical_pending_count >= LINK_SPSC_CAPACITY)
    return 0;
  scheduler->critical_pending[scheduler->critical_pending_write] = *item;
  scheduler->critical_pending_write =
   (scheduler->critical_pending_write + 1u) % LINK_SPSC_CAPACITY;
  scheduler->critical_pending_count++;
  if(scheduler->critical_pending_count >
     scheduler->critical_pending_high_water)
  {
    scheduler->critical_pending_high_water =
     scheduler->critical_pending_count;
  }
  return 1;
}

static int link_scheduler_promote_critical(LinkTransportScheduler *scheduler)
{
  LinkSpscItem item;

  if(link_transport_lanes_critical_outstanding(&scheduler->lanes) ||
     scheduler->critical_pending_count == 0)
    return 1;
  item = scheduler->critical_pending[scheduler->critical_pending_read];
  scheduler->critical_pending_read =
   (scheduler->critical_pending_read + 1u) % LINK_SPSC_CAPACITY;
  scheduler->critical_pending_count--;
  return link_transport_lanes_queue_critical(&scheduler->lanes,
   item.logical_id, item.enqueued_us, item.data, item.size);
}

static int link_scheduler_drain(LinkTransportScheduler *scheduler)
{
  LinkSpscItem item;
  unsigned int depth;
  unsigned int state_class;

  while(link_spsc_ring_pop(&scheduler->handoff, &item))
  {
    if(item.logical_id.epoch != scheduler->lanes.epoch)
      continue;
    if(item.lane == LINK_LANE_CRITICAL)
    {
      if(!link_transport_lanes_critical_outstanding(&scheduler->lanes) &&
         scheduler->critical_pending_count == 0)
      {
        if(!link_transport_lanes_queue_critical(&scheduler->lanes,
           item.logical_id, item.enqueued_us, item.data, item.size))
          return 0;
      }
      else if(!link_scheduler_defer_critical(scheduler, &item))
      {
        return 0;
      }
    }
    else
    {
      return 0;
    }
  }
  for(state_class = LINK_STATE_SIO; state_class < LINK_STATE_COUNT;
      state_class++)
  {
    if(!link_scheduler_take_replaceable(
       &scheduler->replaceable[state_class], &item))
      continue;
    if(item.logical_id.epoch != scheduler->lanes.epoch)
      continue;
    if(!link_transport_lanes_queue_state(&scheduler->lanes,
       item.state_class, item.logical_id, item.enqueued_us, item.data,
       item.size))
      return 0;
  }
  if(!link_scheduler_promote_critical(scheduler))
    return 0;
  depth = link_transport_scheduler_depth(scheduler);
  if(depth > scheduler->high_water)
    scheduler->high_water = depth;
  return 1;
}

int link_transport_scheduler_prepare(LinkTransportScheduler *scheduler,
 uint64_t now_us, LinkLanesPacket *packet)
{
  if(scheduler == NULL || packet == NULL || !link_scheduler_drain(scheduler))
    return -1;
  return link_transport_lanes_prepare(&scheduler->lanes, now_us, packet);
}

int link_transport_scheduler_note_sent(LinkTransportScheduler *scheduler,
 const LinkLanesPacket *packet, uint64_t now_us)
{
  return scheduler != NULL && link_transport_lanes_note_sent(
   &scheduler->lanes, packet, now_us);
}

int link_transport_scheduler_note_blocked(LinkTransportScheduler *scheduler,
 const LinkLanesPacket *packet, uint64_t now_us)
{
  return scheduler != NULL && link_transport_lanes_note_blocked(
   &scheduler->lanes, packet, now_us);
}

LinkLanesReceiveResult link_transport_scheduler_receive(
 LinkTransportScheduler *scheduler, const uint8_t *packet, size_t size,
 uint64_t now_us, LinkSpscItem *delivery)
{
  LinkLogicalId outstanding = { 0u, 0u };
  LinkLanesReceiveResult result;

  if(scheduler == NULL)
    return LINK_LANES_INVALID;
  if(link_transport_lanes_critical_outstanding(&scheduler->lanes))
    outstanding = scheduler->lanes.critical.logical_id;
  result = link_transport_lanes_receive(&scheduler->lanes, packet, size,
   now_us, delivery);
  if(outstanding.message != 0 &&
     !link_transport_lanes_critical_outstanding(&scheduler->lanes))
    link_scheduler_acknowledge_retry(scheduler, outstanding);
  return result;
}

unsigned int link_transport_scheduler_replaceable_depth(
 const LinkTransportScheduler *scheduler)
{
  unsigned int depth = 0;
  unsigned int state_class;

  if(scheduler == NULL)
    return 0;
  for(state_class = LINK_STATE_SIO; state_class < LINK_STATE_COUNT;
      state_class++)
    depth += link_transport_scheduler_replaceable_class_depth(scheduler,
     (LinkReplaceableClass)state_class);
  return depth;
}

unsigned int link_transport_scheduler_replaceable_class_depth(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class)
{
  unsigned int depth = 0;
  unsigned int slot_index;

  if(scheduler == NULL || state_class <= LINK_STATE_NONE ||
     state_class >= LINK_STATE_COUNT)
    return 0;
  for(slot_index = 0; slot_index < LINK_REPLACEABLE_MAILBOX_SLOTS;
      slot_index++)
  {
    depth += link_scheduler_load_acquire(
     &scheduler->replaceable[state_class].slots[slot_index].state) ==
     LINK_MAILBOX_SLOT_READY;
  }
  return depth;
}

unsigned int link_transport_scheduler_replaceable_published(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class)
{
  if(scheduler == NULL || state_class <= LINK_STATE_NONE ||
     state_class >= LINK_STATE_COUNT)
    return 0;
  return link_scheduler_load_acquire(
   &scheduler->replaceable[state_class].published);
}

unsigned int link_transport_scheduler_replaceable_class_coalesced(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class)
{
  if(scheduler == NULL || state_class <= LINK_STATE_NONE ||
     state_class >= LINK_STATE_COUNT)
    return 0;
  return link_scheduler_load_acquire(
   &scheduler->replaceable[state_class].coalesced);
}

unsigned int link_transport_scheduler_replaceable_high_water(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class)
{
  if(scheduler == NULL || state_class <= LINK_STATE_NONE ||
     state_class >= LINK_STATE_COUNT)
    return 0;
  return link_scheduler_load_acquire(
   &scheduler->replaceable[state_class].high_water);
}

unsigned int link_transport_scheduler_replaceable_coalesced(
 const LinkTransportScheduler *scheduler)
{
  unsigned int total = 0;
  unsigned int state_class;

  if(scheduler == NULL)
    return 0;
  for(state_class = LINK_STATE_SIO; state_class < LINK_STATE_COUNT;
      state_class++)
  {
    total += link_scheduler_load_acquire(
     &scheduler->replaceable[state_class].coalesced);
  }
  return total;
}

unsigned int link_transport_scheduler_critical_retry_suppressed(
 const LinkTransportScheduler *scheduler)
{
  if(scheduler == NULL)
    return 0;
  return link_scheduler_load_acquire(&scheduler->critical_retry_suppressed);
}

unsigned int link_transport_scheduler_depth(
 const LinkTransportScheduler *scheduler)
{
  if(scheduler == NULL)
    return 0;
  return link_spsc_ring_depth(&scheduler->handoff) +
   link_transport_lanes_depth(&scheduler->lanes) +
   scheduler->critical_pending_count +
   link_transport_scheduler_replaceable_depth(scheduler);
}
