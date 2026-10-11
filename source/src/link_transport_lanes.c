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

#include "link_transport_lanes.h"

#include <limits.h>
#include <string.h>

#include "zlib/zlib.h"

#define LINK_LANES_MAGIC 0x47424c32u
#define LINK_LANES_VERSION 2u

static void link_lanes_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)((value >> 8) & 0xffu);
  output[1] = (uint8_t)(value & 0xffu);
}

static void link_lanes_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)((value >> 24) & 0xffu);
  output[1] = (uint8_t)((value >> 16) & 0xffu);
  output[2] = (uint8_t)((value >> 8) & 0xffu);
  output[3] = (uint8_t)(value & 0xffu);
}

static uint16_t link_lanes_read_u16(const uint8_t *input)
{
  return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t link_lanes_read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) |
   ((uint32_t)input[1] << 16) |
   ((uint32_t)input[2] << 8) |
   input[3];
}

static uint64_t link_lanes_add_time(uint64_t value, uint64_t amount)
{
  return UINT64_MAX - value < amount ? UINT64_MAX : value + amount;
}

static uint64_t link_lanes_estimated_rto(const LinkTransportLanes *lanes)
{
  uint64_t variation;
  uint64_t rto;

  variation = lanes->rtt_variation_us > UINT64_MAX / 4u ?
   UINT64_MAX : lanes->rtt_variation_us * 4u;
  rto = UINT64_MAX - lanes->smoothed_rtt_us < variation ?
   UINT64_MAX : lanes->smoothed_rtt_us + variation;
  if(rto < lanes->rto_min_us)
    rto = lanes->rto_min_us;
  if(rto > lanes->rto_max_us)
    rto = lanes->rto_max_us;
  return rto;
}

static void link_lanes_increment(unsigned int *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

uint32_t link_transport_next_number(uint32_t value)
{
  value++;
  return value == 0 ? 1u : value;
}

int link_transport_serial_after(uint32_t value, uint32_t reference)
{
  uint32_t difference = value - reference;

  return difference != 0 && difference < 0x80000000u;
}

static uint32_t link_lanes_payload_hash(const uint8_t *data, size_t size)
{
  uLong crc = crc32(0L, Z_NULL, 0);

  return (uint32_t)crc32(crc, data, (uInt)size);
}

static uint32_t link_lanes_packet_crc(const uint8_t *packet, size_t size)
{
  static const uint8_t zero_crc[4] = { 0, 0, 0, 0 };
  uLong crc = crc32(0L, Z_NULL, 0);

  crc = crc32(crc, packet, 32u);
  crc = crc32(crc, zero_crc, sizeof(zero_crc));
  if(size > LINK_LANES_HEADER_SIZE)
  {
    crc = crc32(crc, packet + LINK_LANES_HEADER_SIZE,
     (uInt)(size - LINK_LANES_HEADER_SIZE));
  }
  return (uint32_t)crc;
}

static int link_lanes_logical_valid(const LinkTransportLanes *lanes,
 LinkLogicalId logical_id)
{
  return logical_id.epoch == lanes->epoch && logical_id.message != 0;
}

void link_transport_lanes_init(LinkTransportLanes *lanes,
 uint32_t connection_id, uint32_t epoch)
{
  if(lanes == NULL)
    return;
  memset(lanes, 0, sizeof(*lanes));
  lanes->connection_id = connection_id;
  lanes->epoch = epoch;
  lanes->next_wire_packet = 1u;
  lanes->profile = LINK_TRANSPORT_PROFILE_COLD;
  lanes->rto_min_us = LINK_LANES_RTO_MIN_US;
  lanes->rto_max_us = LINK_LANES_RTO_MAX_US;
  lanes->rto_us = LINK_LANES_RTO_INITIAL_US;
  lanes->state_next = LINK_STATE_SIO;
}

int link_transport_lanes_set_profile(LinkTransportLanes *lanes,
 LinkTransportProfile profile, uint32_t ack_grace_us)
{
  uint64_t timeout;

  if(lanes == NULL || (profile != LINK_TRANSPORT_PROFILE_COLD &&
     profile != LINK_TRANSPORT_PROFILE_HOT))
    return 0;
  lanes->profile = profile;
  if(profile == LINK_TRANSPORT_PROFILE_HOT)
  {
    if(ack_grace_us == 0)
      ack_grace_us = LINK_LANES_HOT_ACK_GRACE_DEFAULT_US;
    if(ack_grace_us < LINK_LANES_HOT_ACK_GRACE_MIN_US)
      ack_grace_us = LINK_LANES_HOT_ACK_GRACE_MIN_US;
    if(ack_grace_us > LINK_LANES_HOT_ACK_GRACE_MAX_US)
      ack_grace_us = LINK_LANES_HOT_ACK_GRACE_MAX_US;
    lanes->ack_grace_us = ack_grace_us;
    lanes->rto_min_us = LINK_LANES_HOT_RTO_MIN_US;
    lanes->rto_max_us = LINK_LANES_HOT_RTO_MAX_US;
    lanes->rto_us = lanes->smoothed_rtt_us == 0 ?
     LINK_LANES_HOT_RTO_INITIAL_US : link_lanes_estimated_rto(lanes);
  }
  else
  {
    lanes->ack_grace_us = 0;
    lanes->rto_min_us = LINK_LANES_RTO_MIN_US;
    lanes->rto_max_us = LINK_LANES_RTO_MAX_US;
    lanes->rto_us = lanes->smoothed_rtt_us == 0 ?
     LINK_LANES_RTO_INITIAL_US : link_lanes_estimated_rto(lanes);
  }
  if(lanes->ack_pending)
    lanes->ack_due_us = link_lanes_add_time(lanes->ack_pending_since_us,
     lanes->ack_grace_us);
  if(lanes->critical.valid && lanes->critical.attempts != 0)
  {
    timeout = lanes->rto_us;
    if(lanes->critical.attempts > 1u)
    {
      unsigned int shift = lanes->critical.attempts - 1u;

      if(shift > LINK_LANES_MAX_PROBE_SHIFT)
        shift = LINK_LANES_MAX_PROBE_SHIFT;
      timeout = timeout > (UINT64_MAX >> shift) ? UINT64_MAX :
       timeout << shift;
      if(timeout > lanes->rto_max_us)
        timeout = lanes->rto_max_us;
    }
    lanes->critical.deadline_us = link_lanes_add_time(
     lanes->critical.last_sent_us, timeout);
  }
  return 1;
}

int link_transport_lanes_queue_critical(LinkTransportLanes *lanes,
 LinkLogicalId logical_id, uint64_t enqueued_us, const uint8_t *data,
 size_t size)
{
  uint32_t hash;

  if(lanes == NULL || data == NULL || size == 0 ||
     size > LINK_LANES_MAX_PAYLOAD ||
     !link_lanes_logical_valid(lanes, logical_id))
    return 0;
  hash = link_lanes_payload_hash(data, size);
  if(lanes->critical.valid)
  {
    if(lanes->critical.logical_id.message == logical_id.message &&
       lanes->critical.logical_id.epoch == logical_id.epoch &&
       lanes->critical.size == size && lanes->critical.payload_hash == hash &&
       memcmp(lanes->critical.data, data, size) == 0)
    {
      link_lanes_increment(&lanes->coalesced);
      return 1;
    }
    link_lanes_increment(&lanes->rejected);
    return 0;
  }

  memset(&lanes->critical, 0, sizeof(lanes->critical));
  lanes->critical.valid = 1;
  lanes->critical.logical_id = logical_id;
  lanes->critical.enqueued_us = enqueued_us;
  lanes->critical.size = (uint16_t)size;
  lanes->critical.payload_hash = hash;
  memcpy(lanes->critical.data, data, size);
  return 1;
}

int link_transport_lanes_queue_state(LinkTransportLanes *lanes,
 LinkReplaceableClass state_class, LinkLogicalId logical_id,
 uint64_t enqueued_us, const uint8_t *data, size_t size)
{
  LinkLanesState *state;

  if(lanes == NULL || state_class <= LINK_STATE_NONE ||
     state_class >= LINK_STATE_COUNT || data == NULL || size == 0 ||
     size > LINK_LANES_MAX_PAYLOAD ||
     !link_lanes_logical_valid(lanes, logical_id))
    return 0;
  state = &lanes->state[state_class];
  if(state->valid &&
     !link_transport_serial_after(logical_id.message,
      state->logical_id.message))
  {
    if(logical_id.message == state->logical_id.message &&
       state->size == size && memcmp(state->data, data, size) == 0)
      link_lanes_increment(&lanes->coalesced);
    else
      link_lanes_increment(&lanes->rejected);
    return logical_id.message == state->logical_id.message &&
     state->size == size && memcmp(state->data, data, size) == 0;
  }
  if(state->valid)
    link_lanes_increment(&lanes->coalesced);
  memset(state, 0, sizeof(*state));
  state->valid = 1;
  state->logical_id = logical_id;
  state->enqueued_us = enqueued_us;
  state->size = (uint16_t)size;
  memcpy(state->data, data, size);
  return 1;
}

void link_transport_lanes_request_probe(LinkTransportLanes *lanes)
{
  if(lanes != NULL)
    lanes->probe_pending = 1;
}

static uint32_t link_lanes_take_wire_packet(LinkTransportLanes *lanes)
{
  uint32_t value = lanes->next_wire_packet;

  lanes->next_wire_packet = link_transport_next_number(value);
  return value;
}

static int link_lanes_encode(LinkTransportLanes *lanes,
 LinkLanesTxKind kind, LinkReplaceableClass state_class,
 LinkLogicalId logical_id, uint32_t acknowledged_message,
 const uint8_t *payload, size_t payload_size, LinkLanesPacket *packet)
{
  size_t packet_size = LINK_LANES_HEADER_SIZE + payload_size;
  uint32_t wire_packet;

  if(lanes == NULL || packet == NULL || lanes->connection_id == 0 ||
     lanes->epoch == 0 || payload_size > LINK_LANES_MAX_PAYLOAD ||
     (payload_size != 0 && payload == NULL))
    return 0;
  wire_packet = link_lanes_take_wire_packet(lanes);
  memset(packet, 0, sizeof(*packet));
  packet->kind = kind;
  packet->state_class = state_class;
  packet->logical_id = logical_id;
  packet->wire_packet = wire_packet;
  packet->acknowledged_message = acknowledged_message;
  packet->size = (uint16_t)packet_size;
  link_lanes_write_u32(packet->data, LINK_LANES_MAGIC);
  packet->data[4] = LINK_LANES_VERSION;
  packet->data[5] = (uint8_t)kind;
  packet->data[6] = (uint8_t)state_class;
  packet->data[7] = LINK_LANES_HEADER_SIZE;
  link_lanes_write_u16(packet->data + 8, (uint16_t)payload_size);
  link_lanes_write_u16(packet->data + 10, 0u);
  link_lanes_write_u32(packet->data + 12, lanes->connection_id);
  link_lanes_write_u32(packet->data + 16, lanes->epoch);
  link_lanes_write_u32(packet->data + 20, wire_packet);
  link_lanes_write_u32(packet->data + 24, logical_id.message);
  link_lanes_write_u32(packet->data + 28, acknowledged_message);
  if(payload_size != 0)
    memcpy(packet->data + LINK_LANES_HEADER_SIZE, payload, payload_size);
  link_lanes_write_u32(packet->data + 32,
   link_lanes_packet_crc(packet->data, packet_size));
  return 1;
}

static int link_lanes_prepare_state(LinkTransportLanes *lanes,
 uint64_t now_us, LinkLanesPacket *packet)
{
  unsigned int count;
  unsigned int state_class;

  for(count = 0; count + 1u < LINK_STATE_COUNT; count++)
  {
    state_class = lanes->state_next + count;
    if(state_class >= LINK_STATE_COUNT)
      state_class = LINK_STATE_SIO +
       (state_class - LINK_STATE_COUNT);
    if(!lanes->state[state_class].valid)
      continue;
    if(state_class == LINK_STATE_CLOCK &&
       lanes->clock_not_before_us != 0 &&
       now_us < lanes->clock_not_before_us)
    {
      link_lanes_increment(&lanes->clock_deferred);
      continue;
    }
    lanes->state_next = state_class + 1u;
    if(lanes->state_next >= LINK_STATE_COUNT)
      lanes->state_next = LINK_STATE_SIO;
    return link_lanes_encode(lanes, LINK_LANES_TX_STATE,
     (LinkReplaceableClass)state_class,
     lanes->state[state_class].logical_id, lanes->ack_message,
     lanes->state[state_class].data, lanes->state[state_class].size,
     packet);
  }
  return 0;
}

int link_transport_lanes_prepare(LinkTransportLanes *lanes,
 uint64_t now_us, LinkLanesPacket *packet)
{
  LinkLogicalId no_logical = { 0u, 0u };
  int critical_ready;

  if(lanes == NULL || packet == NULL || lanes->connection_id == 0 ||
     lanes->epoch == 0)
    return 0;
  critical_ready = (lanes->retry_due_us == 0 ||
    now_us >= lanes->retry_due_us) && lanes->critical.valid &&
     (lanes->critical.attempts == 0 ||
      now_us >= lanes->critical.deadline_us);
  if(critical_ready)
  {
    return link_lanes_encode(lanes, LINK_LANES_TX_CRITICAL,
     LINK_STATE_NONE, lanes->critical.logical_id, lanes->ack_message,
     lanes->critical.data, lanes->critical.size, packet);
  }
  if(lanes->ack_pending)
  {
    if(now_us < lanes->ack_due_us)
      return 0;
    return link_lanes_encode(lanes, LINK_LANES_TX_ACK, LINK_STATE_NONE,
     no_logical, lanes->ack_message, NULL, 0, packet);
  }
  if(lanes->retry_due_us != 0 && now_us < lanes->retry_due_us)
    return 0;
  if(link_lanes_prepare_state(lanes, now_us, packet))
    return 1;
  if(lanes->probe_pending)
  {
    return link_lanes_encode(lanes, LINK_LANES_TX_PROBE, LINK_STATE_NONE,
     no_logical, lanes->ack_message, NULL, 0, packet);
  }
  return 0;
}

static uint64_t link_lanes_probe_timeout(const LinkTransportLanes *lanes,
 uint8_t attempts)
{
  unsigned int shift = attempts > 0 ? attempts - 1u : 0u;
  uint64_t timeout = lanes->rto_us;

  if(shift > LINK_LANES_MAX_PROBE_SHIFT)
    shift = LINK_LANES_MAX_PROBE_SHIFT;
  if(timeout > (UINT64_MAX >> shift))
    return UINT64_MAX;
  timeout <<= shift;
  return timeout > lanes->rto_max_us ? lanes->rto_max_us : timeout;
}

static void link_lanes_note_ack_sent(LinkTransportLanes *lanes,
 const LinkLanesPacket *packet)
{
  if(lanes->ack_pending &&
     packet->acknowledged_message == lanes->ack_message)
  {
    lanes->ack_pending = 0;
    lanes->ack_pending_since_us = 0;
    lanes->ack_due_us = 0;
  }
}

static void link_lanes_note_sojourn(LinkTransportLanes *lanes,
 uint64_t enqueued_us, uint64_t now_us)
{
  uint64_t sojourn;

  if(now_us < enqueued_us)
    return;
  sojourn = now_us - enqueued_us;
  if(sojourn > lanes->sojourn_high_water_us)
    lanes->sojourn_high_water_us = sojourn;
}

static void link_lanes_end_backpressure(LinkTransportLanes *lanes,
 uint64_t now_us)
{
  uint64_t age;

  if(!lanes->backpressure_active)
    return;
  age = now_us >= lanes->backpressure_started_us ?
   now_us - lanes->backpressure_started_us : 0u;
  if(age > lanes->backpressure_max_age_us)
    lanes->backpressure_max_age_us = age;
  lanes->backpressure_active = 0;
  lanes->backpressure_started_us = 0;
  lanes->backpressure_streak = 0;
}

static void link_lanes_note_clock_success(LinkTransportLanes *lanes,
 uint64_t now_us)
{
  if(lanes->clock_backoff_us == 0)
  {
    lanes->clock_success_streak = 0;
    lanes->clock_not_before_us = 0;
    return;
  }
  if(lanes->clock_success_streak < LINK_LANES_CLOCK_SUCCESS_THRESHOLD)
    lanes->clock_success_streak++;
  if(lanes->clock_success_streak >= LINK_LANES_CLOCK_SUCCESS_THRESHOLD)
  {
    if(lanes->clock_backoff_us <= LINK_LANES_CLOCK_BACKOFF_MIN_US)
      lanes->clock_backoff_us = 0;
    else
      lanes->clock_backoff_us /= 2u;
    lanes->clock_success_streak = 0;
  }
  lanes->clock_not_before_us = lanes->clock_backoff_us == 0 ? 0 :
   link_lanes_add_time(now_us, lanes->clock_backoff_us);
}

int link_transport_lanes_note_sent(LinkTransportLanes *lanes,
 const LinkLanesPacket *packet, uint64_t now_us)
{
  LinkLanesState *state;

  if(lanes == NULL || packet == NULL || packet->kind == LINK_LANES_TX_NONE)
    return 0;
  lanes->retry_due_us = 0;
  link_lanes_note_ack_sent(lanes, packet);
  if(packet->kind == LINK_LANES_TX_ACK)
  {
    link_lanes_end_backpressure(lanes, now_us);
    return 1;
  }
  if(packet->kind == LINK_LANES_TX_PROBE)
  {
    link_lanes_end_backpressure(lanes, now_us);
    lanes->probe_pending = 0;
    link_lanes_increment(&lanes->probes);
    return 1;
  }
  if(packet->kind == LINK_LANES_TX_CRITICAL)
  {
    if(!lanes->critical.valid ||
       packet->logical_id.epoch != lanes->critical.logical_id.epoch ||
       packet->logical_id.message != lanes->critical.logical_id.message)
      return 0;
    link_lanes_end_backpressure(lanes, now_us);
    link_lanes_note_sojourn(lanes, lanes->critical.enqueued_us, now_us);
    if(lanes->critical.attempts == 0)
      lanes->critical.first_sent_us = now_us;
    else
    {
      link_lanes_increment(&lanes->retransmissions);
      link_lanes_increment(&lanes->probes);
    }
    if(lanes->critical.attempts != UINT8_MAX)
      lanes->critical.attempts++;
    lanes->critical.last_sent_us = now_us;
    lanes->critical.deadline_us = link_lanes_add_time(now_us,
     link_lanes_probe_timeout(lanes, lanes->critical.attempts));
    return 1;
  }
  if(packet->kind != LINK_LANES_TX_STATE ||
     packet->state_class <= LINK_STATE_NONE ||
     packet->state_class >= LINK_STATE_COUNT)
    return 0;
  link_lanes_end_backpressure(lanes, now_us);
  if(packet->state_class == LINK_STATE_CLOCK)
    link_lanes_note_clock_success(lanes, now_us);
  state = &lanes->state[packet->state_class];
  if(!state->valid ||
     state->logical_id.epoch != packet->logical_id.epoch ||
     state->logical_id.message != packet->logical_id.message)
    return 1;
  link_lanes_note_sojourn(lanes, state->enqueued_us, now_us);
  memset(state, 0, sizeof(*state));
  return 1;
}

int link_transport_lanes_note_blocked(LinkTransportLanes *lanes,
 const LinkLanesPacket *packet, uint64_t now_us)
{
  unsigned int next_backoff;

  if(lanes == NULL || packet == NULL || packet->kind == LINK_LANES_TX_NONE)
    return 0;
  link_lanes_increment(&lanes->no_space);
  if(!lanes->backpressure_active)
  {
    lanes->backpressure_active = 1;
    lanes->backpressure_started_us = now_us;
    lanes->backpressure_streak = 0;
    link_lanes_increment(&lanes->backpressure_episodes);
  }
  link_lanes_increment(&lanes->backpressure_streak);
  if(lanes->backpressure_streak > lanes->backpressure_streak_high_water)
    lanes->backpressure_streak_high_water = lanes->backpressure_streak;
  lanes->last_blocked_kind = packet->kind;
  lanes->last_blocked_state_class = packet->state_class;

  next_backoff = lanes->clock_backoff_us;
  if(next_backoff == 0)
    next_backoff = LINK_LANES_CLOCK_BACKOFF_MIN_US;
  else if(next_backoff < LINK_LANES_CLOCK_BACKOFF_MAX_US)
  {
    if(next_backoff > LINK_LANES_CLOCK_BACKOFF_MAX_US / 2u)
      next_backoff = LINK_LANES_CLOCK_BACKOFF_MAX_US;
    else
      next_backoff *= 2u;
  }
  if(next_backoff != lanes->clock_backoff_us)
  {
    lanes->clock_backoff_us = next_backoff;
    link_lanes_increment(&lanes->clock_backoff_raises);
  }
  lanes->clock_success_streak = 0;
  lanes->clock_not_before_us = link_lanes_add_time(now_us,
   lanes->clock_backoff_us);
  lanes->retry_due_us = link_lanes_add_time(now_us,
   LINK_LANES_BACKPRESSURE_RETRY_US);
  if(packet->kind == LINK_LANES_TX_CRITICAL && lanes->critical.valid)
    link_lanes_note_sojourn(lanes, lanes->critical.enqueued_us, now_us);
  else if(packet->kind == LINK_LANES_TX_STATE &&
          packet->state_class > LINK_STATE_NONE &&
          packet->state_class < LINK_STATE_COUNT &&
          lanes->state[packet->state_class].valid)
    link_lanes_note_sojourn(lanes,
     lanes->state[packet->state_class].enqueued_us, now_us);
  return 1;
}

static void link_lanes_update_rto(LinkTransportLanes *lanes,
 uint64_t sample_us)
{
  uint64_t difference;

  if(sample_us == 0)
    sample_us = 1;
  if(sample_us > lanes->rto_max_us)
    sample_us = lanes->rto_max_us;
  if(lanes->smoothed_rtt_us == 0)
  {
    lanes->smoothed_rtt_us = sample_us;
    lanes->rtt_variation_us = sample_us / 2u;
  }
  else
  {
    difference = lanes->smoothed_rtt_us > sample_us ?
     lanes->smoothed_rtt_us - sample_us :
     sample_us - lanes->smoothed_rtt_us;
    lanes->rtt_variation_us = lanes->rtt_variation_us -
     lanes->rtt_variation_us / 4u + difference / 4u;
    lanes->smoothed_rtt_us = lanes->smoothed_rtt_us -
     lanes->smoothed_rtt_us / 8u + sample_us / 8u;
  }
  lanes->rto_us = link_lanes_estimated_rto(lanes);
}

static LinkLanesReceiveResult link_lanes_receive_ack(
 LinkTransportLanes *lanes, uint32_t acknowledged_message,
 uint64_t now_us)
{
  uint64_t sample_us;

  if(!lanes->critical.valid || acknowledged_message == 0 ||
     acknowledged_message != lanes->critical.logical_id.message)
    return LINK_LANES_STALE;
  if(lanes->critical.attempts == 1 &&
     now_us >= lanes->critical.first_sent_us)
  {
    sample_us = now_us - lanes->critical.first_sent_us;
    link_lanes_update_rto(lanes, sample_us);
  }
  memset(&lanes->critical, 0, sizeof(lanes->critical));
  lanes->last_ack_progress_us = now_us;
  link_lanes_increment(&lanes->ack_advances);
  return LINK_LANES_ACKED;
}

static void link_lanes_fill_delivery(LinkSpscItem *delivery,
 LinkTransportLane lane, LinkReplaceableClass state_class,
 uint32_t epoch, uint32_t logical_message, const uint8_t *payload,
 uint16_t payload_size, uint64_t now_us)
{
  memset(delivery, 0, sizeof(*delivery));
  delivery->lane = lane;
  delivery->state_class = state_class;
  delivery->logical_id.epoch = epoch;
  delivery->logical_id.message = logical_message;
  delivery->enqueued_us = now_us;
  delivery->size = payload_size;
  memcpy(delivery->data, payload, payload_size);
}

static void link_lanes_schedule_ack(LinkTransportLanes *lanes,
 uint32_t message, uint64_t now_us, int immediate)
{
  lanes->ack_message = message;
  lanes->ack_pending = 1;
  lanes->ack_pending_since_us = now_us;
  lanes->ack_due_us = immediate ? now_us :
   link_lanes_add_time(now_us, lanes->ack_grace_us);
}

LinkLanesReceiveResult link_transport_lanes_receive(
 LinkTransportLanes *lanes, const uint8_t *packet, size_t size,
 uint64_t now_us, LinkSpscItem *delivery)
{
  LinkLanesTxKind kind;
  LinkReplaceableClass state_class;
  uint32_t connection_id;
  uint32_t epoch;
  uint32_t wire_packet;
  uint32_t logical_message;
  uint32_t acknowledged_message;
  uint32_t received_crc;
  uint32_t payload_hash;
  uint16_t payload_size;
  const uint8_t *payload;
  LinkLanesReceiveResult ack_result = LINK_LANES_STALE;

  if(delivery != NULL)
    memset(delivery, 0, sizeof(*delivery));
  if(lanes == NULL || packet == NULL || size < LINK_LANES_HEADER_SIZE ||
     size > LINK_LANES_MAX_PACKET ||
     link_lanes_read_u32(packet) != LINK_LANES_MAGIC ||
     packet[4] != LINK_LANES_VERSION || packet[7] != LINK_LANES_HEADER_SIZE ||
     link_lanes_read_u16(packet + 10) != 0)
    goto invalid;
  kind = (LinkLanesTxKind)packet[5];
  state_class = (LinkReplaceableClass)packet[6];
  payload_size = link_lanes_read_u16(packet + 8);
  connection_id = link_lanes_read_u32(packet + 12);
  epoch = link_lanes_read_u32(packet + 16);
  wire_packet = link_lanes_read_u32(packet + 20);
  logical_message = link_lanes_read_u32(packet + 24);
  acknowledged_message = link_lanes_read_u32(packet + 28);
  received_crc = link_lanes_read_u32(packet + 32);
  if(kind <= LINK_LANES_TX_NONE || kind > LINK_LANES_TX_PROBE ||
     wire_packet == 0 || size != LINK_LANES_HEADER_SIZE + payload_size ||
     payload_size > LINK_LANES_MAX_PAYLOAD ||
     received_crc != link_lanes_packet_crc(packet, size) ||
     ((kind == LINK_LANES_TX_CRITICAL || kind == LINK_LANES_TX_STATE) !=
      (payload_size != 0)) ||
     (kind == LINK_LANES_TX_STATE &&
      (state_class <= LINK_STATE_NONE || state_class >= LINK_STATE_COUNT)) ||
     (kind != LINK_LANES_TX_STATE && state_class != LINK_STATE_NONE) ||
     ((kind == LINK_LANES_TX_CRITICAL || kind == LINK_LANES_TX_STATE) &&
      logical_message == 0) ||
     ((kind == LINK_LANES_TX_ACK || kind == LINK_LANES_TX_PROBE) &&
      logical_message != 0))
    goto invalid;
  if(connection_id != lanes->connection_id || epoch != lanes->epoch)
  {
    link_lanes_increment(&lanes->foreign_packets);
    return LINK_LANES_FOREIGN;
  }

  if(acknowledged_message != 0 && lanes->critical.valid &&
     acknowledged_message == lanes->critical.logical_id.message)
  {
    ack_result = link_lanes_receive_ack(lanes, acknowledged_message,
     now_us);
  }
  if(kind == LINK_LANES_TX_ACK)
  {
    link_lanes_increment(&lanes->received_acks);
    return ack_result;
  }
  if(kind == LINK_LANES_TX_PROBE)
  {
    link_lanes_schedule_ack(lanes, lanes->last_rx_critical, now_us, 1);
    return LINK_LANES_CONTROL;
  }

  payload = packet + LINK_LANES_HEADER_SIZE;
  if(kind == LINK_LANES_TX_CRITICAL)
  {
    payload_hash = link_lanes_payload_hash(payload, payload_size);
    if(logical_message == lanes->last_rx_critical)
    {
      link_lanes_schedule_ack(lanes, logical_message, now_us, 1);
      if(payload_hash != lanes->last_rx_critical_hash)
      {
        link_lanes_increment(&lanes->conflicts);
        return LINK_LANES_CONFLICT;
      }
      link_lanes_increment(&lanes->duplicates);
      return LINK_LANES_DUPLICATE;
    }
    if(lanes->last_rx_critical != 0 &&
       !link_transport_serial_after(logical_message,
        lanes->last_rx_critical))
    {
      link_lanes_schedule_ack(lanes, lanes->last_rx_critical, now_us, 1);
      link_lanes_increment(&lanes->stale);
      return LINK_LANES_STALE;
    }
    if(delivery == NULL)
      return LINK_LANES_INVALID;
    lanes->last_rx_critical = logical_message;
    lanes->last_rx_critical_hash = payload_hash;
    link_lanes_schedule_ack(lanes, logical_message, now_us, 0);
    link_lanes_fill_delivery(delivery, LINK_LANE_CRITICAL,
     LINK_STATE_NONE, epoch, logical_message, payload, payload_size, now_us);
    return LINK_LANES_DELIVERED;
  }

  if(lanes->last_rx_state[state_class] != 0 &&
     !link_transport_serial_after(logical_message,
      lanes->last_rx_state[state_class]))
  {
    if(logical_message == lanes->last_rx_state[state_class])
    {
      link_lanes_increment(&lanes->duplicates);
      return LINK_LANES_DUPLICATE;
    }
    link_lanes_increment(&lanes->stale);
    return LINK_LANES_STALE;
  }
  if(delivery == NULL)
    return LINK_LANES_INVALID;
  lanes->last_rx_state[state_class] = logical_message;
  link_lanes_fill_delivery(delivery, LINK_LANE_REPLACEABLE, state_class,
   epoch, logical_message, payload, payload_size, now_us);
  return LINK_LANES_DELIVERED;

invalid:
  if(lanes != NULL)
    link_lanes_increment(&lanes->invalid_packets);
  return LINK_LANES_INVALID;
}

int link_transport_lanes_critical_outstanding(
 const LinkTransportLanes *lanes)
{
  return lanes != NULL && lanes->critical.valid;
}

int link_transport_lanes_progress_stalled(const LinkTransportLanes *lanes,
 uint64_t now_us, uint64_t timeout_us)
{
  uint64_t progress_us;

  if(lanes == NULL || !lanes->critical.valid ||
     lanes->critical.attempts == 0 || timeout_us == 0 ||
     now_us < lanes->critical.first_sent_us)
    return 0;
  progress_us = lanes->critical.first_sent_us;
  if(lanes->last_ack_progress_us > progress_us &&
     lanes->last_ack_progress_us <= now_us)
    progress_us = lanes->last_ack_progress_us;
  return now_us - progress_us >= timeout_us;
}

uint64_t link_transport_lanes_backpressure_age(
 const LinkTransportLanes *lanes, uint64_t now_us)
{
  if(lanes == NULL || !lanes->backpressure_active ||
     now_us < lanes->backpressure_started_us)
    return 0;
  return now_us - lanes->backpressure_started_us;
}

int link_transport_lanes_backpressure_stalled(
 const LinkTransportLanes *lanes, uint64_t now_us, uint64_t timeout_us)
{
  return lanes != NULL && lanes->backpressure_active && timeout_us != 0 &&
   now_us >= lanes->backpressure_started_us &&
   now_us - lanes->backpressure_started_us >= timeout_us;
}

unsigned int link_transport_lanes_depth(const LinkTransportLanes *lanes)
{
  unsigned int depth;
  unsigned int state_class;

  if(lanes == NULL)
    return 0;
  depth = lanes->critical.valid != 0;
  for(state_class = LINK_STATE_SIO; state_class < LINK_STATE_COUNT;
      state_class++)
    depth += lanes->state[state_class].valid != 0;
  return depth;
}
