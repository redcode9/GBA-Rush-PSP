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

#include "link_spsc_ring.h"

#include <limits.h>
#include <string.h>

static uint32_t link_spsc_load_relaxed(const volatile uint32_t *value)
{
  return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static uint32_t link_spsc_load_acquire(const volatile uint32_t *value)
{
  return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void link_spsc_store_release(volatile uint32_t *target,
 uint32_t value)
{
  __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

static void link_spsc_increment(unsigned int *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

void link_spsc_ring_init(LinkSpscRing *ring)
{
  if(ring != NULL)
    memset(ring, 0, sizeof(*ring));
}

int link_spsc_ring_push(LinkSpscRing *ring, LinkTransportLane lane,
 LinkReplaceableClass state_class, LinkLogicalId logical_id,
 uint64_t enqueued_us, const uint8_t *data, size_t size)
{
  LinkSpscItem *slot;
  uint32_t read_index;
  uint32_t write_index;
  uint32_t depth;

  if(ring == NULL || lane < LINK_LANE_IMMEDIATE ||
     lane > LINK_LANE_REPLACEABLE || data == NULL || size == 0 ||
     size > LINK_PROTOCOL_MAX_PACKET ||
     (lane == LINK_LANE_REPLACEABLE &&
      (state_class <= LINK_STATE_NONE || state_class >= LINK_STATE_COUNT)) ||
     (lane != LINK_LANE_REPLACEABLE && state_class != LINK_STATE_NONE))
    return 0;

  write_index = link_spsc_load_relaxed(&ring->write_index);
  read_index = link_spsc_load_acquire(&ring->read_index);
  depth = write_index - read_index;
  if(depth >= LINK_SPSC_CAPACITY)
  {
    link_spsc_increment(&ring->rejected);
    return 0;
  }

  slot = &ring->slots[write_index % LINK_SPSC_CAPACITY];
  slot->lane = lane;
  slot->state_class = state_class;
  slot->logical_id = logical_id;
  slot->enqueued_us = enqueued_us;
  slot->size = (uint16_t)size;
  memcpy(slot->data, data, size);
  link_spsc_store_release(&ring->write_index, write_index + 1u);

  depth++;
  if(depth > ring->high_water)
    ring->high_water = depth;
  return 1;
}

int link_spsc_ring_pop(LinkSpscRing *ring, LinkSpscItem *item)
{
  uint32_t read_index;
  uint32_t write_index;

  if(ring == NULL || item == NULL)
    return 0;
  read_index = link_spsc_load_relaxed(&ring->read_index);
  write_index = link_spsc_load_acquire(&ring->write_index);
  if(read_index == write_index)
    return 0;

  *item = ring->slots[read_index % LINK_SPSC_CAPACITY];
  link_spsc_store_release(&ring->read_index, read_index + 1u);
  return 1;
}

unsigned int link_spsc_ring_depth(const LinkSpscRing *ring)
{
  uint32_t read_index;
  uint32_t write_index;
  uint32_t depth;

  if(ring == NULL)
    return 0;
  read_index = link_spsc_load_acquire(&ring->read_index);
  write_index = link_spsc_load_acquire(&ring->write_index);
  depth = write_index - read_index;
  return depth <= LINK_SPSC_CAPACITY ? (unsigned int)depth :
   LINK_SPSC_CAPACITY;
}

void link_spsc_ring_discard(LinkSpscRing *ring)
{
  uint32_t write_index;

  if(ring == NULL)
    return;
  write_index = link_spsc_load_acquire(&ring->write_index);
  link_spsc_store_release(&ring->read_index, write_index);
}
