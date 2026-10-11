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

#ifndef LINK_TRANSPORT_LANES_H
#define LINK_TRANSPORT_LANES_H

#include <stddef.h>
#include <stdint.h>

#include "link_spsc_ring.h"

#define LINK_LANES_HEADER_SIZE 36u
#define LINK_LANES_MAX_PAYLOAD LINK_PROTOCOL_MAX_PACKET
#define LINK_LANES_MAX_PACKET \
 (LINK_LANES_HEADER_SIZE + LINK_LANES_MAX_PAYLOAD)
#define LINK_LANES_BACKPRESSURE_RETRY_US 10000u
#define LINK_LANES_CLOCK_BACKOFF_MIN_US  10000u
#define LINK_LANES_CLOCK_BACKOFF_MAX_US  80000u
#define LINK_LANES_CLOCK_SUCCESS_THRESHOLD 8u
#define LINK_LANES_RTO_INITIAL_US 200000u
#define LINK_LANES_RTO_MIN_US 50000u
#define LINK_LANES_RTO_MAX_US 1000000u
#define LINK_LANES_HOT_RTO_INITIAL_US 8000u
#define LINK_LANES_HOT_RTO_MIN_US 2000u
#define LINK_LANES_HOT_RTO_MAX_US 20000u
#define LINK_LANES_HOT_ACK_GRACE_DEFAULT_US 500u
#define LINK_LANES_HOT_ACK_GRACE_MIN_US 100u
#define LINK_LANES_HOT_ACK_GRACE_MAX_US 2000u
#define LINK_LANES_MAX_PROBE_SHIFT 4u

typedef enum
{
  LINK_TRANSPORT_PROFILE_COLD,
  LINK_TRANSPORT_PROFILE_HOT
} LinkTransportProfile;

typedef enum
{
  LINK_LANES_TX_NONE,
  LINK_LANES_TX_ACK,
  LINK_LANES_TX_CRITICAL,
  LINK_LANES_TX_STATE,
  LINK_LANES_TX_PROBE
} LinkLanesTxKind;

typedef enum
{
  LINK_LANES_INVALID = -2,
  LINK_LANES_CONFLICT = -1,
  LINK_LANES_FOREIGN = 0,
  LINK_LANES_CONTROL = 1,
  LINK_LANES_DELIVERED = 2,
  LINK_LANES_DUPLICATE = 3,
  LINK_LANES_STALE = 4,
  LINK_LANES_ACKED = 5
} LinkLanesReceiveResult;

typedef struct
{
  uint8_t valid;
  uint8_t attempts;
  LinkLogicalId logical_id;
  uint64_t enqueued_us;
  uint64_t first_sent_us;
  uint64_t last_sent_us;
  uint64_t deadline_us;
  uint16_t size;
  uint32_t payload_hash;
  uint8_t data[LINK_LANES_MAX_PAYLOAD];
} LinkLanesCritical;

typedef struct
{
  uint8_t valid;
  LinkLogicalId logical_id;
  uint64_t enqueued_us;
  uint16_t size;
  uint8_t data[LINK_LANES_MAX_PAYLOAD];
} LinkLanesState;

typedef struct
{
  LinkLanesTxKind kind;
  LinkReplaceableClass state_class;
  LinkLogicalId logical_id;
  uint32_t wire_packet;
  uint32_t acknowledged_message;
  uint16_t size;
  uint8_t data[LINK_LANES_MAX_PACKET];
} LinkLanesPacket;

typedef struct
{
  uint32_t connection_id;
  uint32_t epoch;
  uint32_t next_wire_packet;
  uint32_t last_rx_critical;
  uint32_t last_rx_critical_hash;
  uint32_t last_rx_state[LINK_STATE_COUNT];
  uint32_t ack_message;
  uint64_t retry_due_us;
  uint64_t smoothed_rtt_us;
  uint64_t rtt_variation_us;
  uint64_t rto_us;
  uint64_t last_ack_progress_us;
  uint64_t ack_pending_since_us;
  uint64_t ack_due_us;
  uint64_t sojourn_high_water_us;
  uint64_t clock_not_before_us;
  uint64_t backpressure_started_us;
  uint64_t backpressure_max_age_us;
  LinkLanesCritical critical;
  LinkLanesState state[LINK_STATE_COUNT];
  LinkLanesTxKind last_blocked_kind;
  LinkReplaceableClass last_blocked_state_class;
  unsigned int state_next;
  unsigned int retransmissions;
  unsigned int probes;
  unsigned int coalesced;
  unsigned int rejected;
  unsigned int no_space;
  unsigned int duplicates;
  unsigned int stale;
  unsigned int conflicts;
  unsigned int invalid_packets;
  unsigned int foreign_packets;
  unsigned int received_acks;
  unsigned int ack_advances;
  unsigned int clock_backoff_us;
  unsigned int clock_success_streak;
  unsigned int clock_backoff_raises;
  unsigned int clock_deferred;
  unsigned int backpressure_streak;
  unsigned int backpressure_streak_high_water;
  unsigned int backpressure_episodes;
  unsigned int ack_grace_us;
  unsigned int rto_min_us;
  unsigned int rto_max_us;
  LinkTransportProfile profile;
  uint8_t ack_pending;
  uint8_t probe_pending;
  uint8_t backpressure_active;
} LinkTransportLanes;

uint32_t link_transport_next_number(uint32_t value);
int link_transport_serial_after(uint32_t value, uint32_t reference);
void link_transport_lanes_init(LinkTransportLanes *lanes,
 uint32_t connection_id, uint32_t epoch);
int link_transport_lanes_set_profile(LinkTransportLanes *lanes,
 LinkTransportProfile profile, uint32_t ack_grace_us);
int link_transport_lanes_queue_critical(LinkTransportLanes *lanes,
 LinkLogicalId logical_id, uint64_t enqueued_us, const uint8_t *data,
 size_t size);
int link_transport_lanes_queue_state(LinkTransportLanes *lanes,
 LinkReplaceableClass state_class, LinkLogicalId logical_id,
 uint64_t enqueued_us, const uint8_t *data, size_t size);
void link_transport_lanes_request_probe(LinkTransportLanes *lanes);
int link_transport_lanes_prepare(LinkTransportLanes *lanes,
 uint64_t now_us, LinkLanesPacket *packet);
int link_transport_lanes_note_sent(LinkTransportLanes *lanes,
 const LinkLanesPacket *packet, uint64_t now_us);
int link_transport_lanes_note_blocked(LinkTransportLanes *lanes,
 const LinkLanesPacket *packet, uint64_t now_us);
LinkLanesReceiveResult link_transport_lanes_receive(
 LinkTransportLanes *lanes, const uint8_t *packet, size_t size,
 uint64_t now_us, LinkSpscItem *delivery);
int link_transport_lanes_critical_outstanding(
 const LinkTransportLanes *lanes);
int link_transport_lanes_progress_stalled(const LinkTransportLanes *lanes,
 uint64_t now_us, uint64_t timeout_us);
uint64_t link_transport_lanes_backpressure_age(
 const LinkTransportLanes *lanes, uint64_t now_us);
int link_transport_lanes_backpressure_stalled(
 const LinkTransportLanes *lanes, uint64_t now_us, uint64_t timeout_us);
unsigned int link_transport_lanes_depth(const LinkTransportLanes *lanes);

#endif /* LINK_TRANSPORT_LANES_H */
