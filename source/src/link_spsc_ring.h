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

#ifndef LINK_SPSC_RING_H
#define LINK_SPSC_RING_H

#include <stddef.h>
#include <stdint.h>

#include "link_protocol.h"

#define LINK_SPSC_CAPACITY 8u

typedef enum
{
  LINK_LANE_IMMEDIATE,
  LINK_LANE_CRITICAL,
  LINK_LANE_REPLACEABLE
} LinkTransportLane;

typedef enum
{
  LINK_STATE_NONE,
  LINK_STATE_SIO,
  LINK_STATE_HEARTBEAT,
  LINK_STATE_CLOCK,
  LINK_STATE_COUNT
} LinkReplaceableClass;

typedef struct
{
  uint32_t epoch;
  uint32_t message;
} LinkLogicalId;

typedef struct
{
  LinkTransportLane lane;
  LinkReplaceableClass state_class;
  LinkLogicalId logical_id;
  uint64_t enqueued_us;
  uint16_t size;
  uint8_t data[LINK_PROTOCOL_MAX_PACKET];
} LinkSpscItem;

typedef struct
{
  LinkSpscItem slots[LINK_SPSC_CAPACITY];
  volatile uint32_t read_index;
  volatile uint32_t write_index;
  unsigned int rejected;
  unsigned int high_water;
} LinkSpscRing;

void link_spsc_ring_init(LinkSpscRing *ring);
int link_spsc_ring_push(LinkSpscRing *ring, LinkTransportLane lane,
 LinkReplaceableClass state_class, LinkLogicalId logical_id,
 uint64_t enqueued_us, const uint8_t *data, size_t size);
int link_spsc_ring_pop(LinkSpscRing *ring, LinkSpscItem *item);
unsigned int link_spsc_ring_depth(const LinkSpscRing *ring);
void link_spsc_ring_discard(LinkSpscRing *ring);

#endif /* LINK_SPSC_RING_H */
