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

#ifndef LINK_TRANSPORT_SCHEDULER_H
#define LINK_TRANSPORT_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#include "link_spsc_ring.h"
#include "link_transport_lanes.h"

#define LINK_REPLACEABLE_MAILBOX_SLOTS 2u
#define LINK_CRITICAL_RETRY_TRACKERS LINK_SPSC_CAPACITY

typedef enum
{
  LINK_MAILBOX_SLOT_FREE,
  LINK_MAILBOX_SLOT_WRITING,
  LINK_MAILBOX_SLOT_READY,
  LINK_MAILBOX_SLOT_READING
} LinkMailboxSlotState;

typedef struct
{
  volatile uint32_t state;
  LinkSpscItem item;
} LinkReplaceableMailboxSlot;

typedef struct
{
  LinkReplaceableMailboxSlot slots[LINK_REPLACEABLE_MAILBOX_SLOTS];
  volatile uint32_t published;
  volatile uint32_t coalesced;
  volatile uint32_t high_water;
} LinkReplaceableMailbox;

typedef struct
{
  volatile uint32_t epoch;
  volatile uint32_t pending_message;
  volatile uint32_t acknowledged_message;
  uint16_t size;
  uint8_t data[LINK_PROTOCOL_MAX_PACKET];
} LinkCriticalRetryTracker;

typedef struct
{
  LinkSpscRing handoff;
  LinkReplaceableMailbox replaceable[LINK_STATE_COUNT];
  LinkTransportLanes lanes;
  LinkSpscItem critical_pending[LINK_SPSC_CAPACITY];
  LinkCriticalRetryTracker retry[LINK_CRITICAL_RETRY_TRACKERS];
  unsigned int critical_pending_read;
  unsigned int critical_pending_write;
  unsigned int critical_pending_count;
  unsigned int critical_pending_high_water;
  unsigned int high_water;
  volatile uint32_t critical_retry_suppressed;
  volatile uint32_t published_epoch;
  volatile uint32_t next_logical_message;
} LinkTransportScheduler;

void link_transport_scheduler_init(LinkTransportScheduler *scheduler);
void link_transport_scheduler_reset(LinkTransportScheduler *scheduler,
 uint32_t connection_id, uint32_t epoch);
int link_transport_scheduler_set_profile(LinkTransportScheduler *scheduler,
 LinkTransportProfile profile, uint32_t ack_grace_us);
int link_transport_scheduler_queue(LinkTransportScheduler *scheduler,
 LinkTransportLane lane, LinkReplaceableClass state_class,
 uint64_t enqueued_us, const uint8_t *data, size_t size);
int link_transport_scheduler_prepare(LinkTransportScheduler *scheduler,
 uint64_t now_us, LinkLanesPacket *packet);
int link_transport_scheduler_note_sent(LinkTransportScheduler *scheduler,
 const LinkLanesPacket *packet, uint64_t now_us);
int link_transport_scheduler_note_blocked(LinkTransportScheduler *scheduler,
 const LinkLanesPacket *packet, uint64_t now_us);
LinkLanesReceiveResult link_transport_scheduler_receive(
 LinkTransportScheduler *scheduler, const uint8_t *packet, size_t size,
 uint64_t now_us, LinkSpscItem *delivery);
unsigned int link_transport_scheduler_depth(
 const LinkTransportScheduler *scheduler);
unsigned int link_transport_scheduler_replaceable_depth(
 const LinkTransportScheduler *scheduler);
unsigned int link_transport_scheduler_replaceable_class_depth(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class);
unsigned int link_transport_scheduler_replaceable_published(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class);
unsigned int link_transport_scheduler_replaceable_class_coalesced(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class);
unsigned int link_transport_scheduler_replaceable_high_water(
 const LinkTransportScheduler *scheduler, LinkReplaceableClass state_class);
unsigned int link_transport_scheduler_replaceable_coalesced(
 const LinkTransportScheduler *scheduler);
unsigned int link_transport_scheduler_critical_retry_suppressed(
 const LinkTransportScheduler *scheduler);

#endif /* LINK_TRANSPORT_SCHEDULER_H */
