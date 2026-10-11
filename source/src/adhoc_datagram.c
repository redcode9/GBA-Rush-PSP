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

#include "adhoc_datagram.h"

#include <limits.h>
#include <string.h>

#include "zlib/zlib.h"

#define ADHOC_DATAGRAM_MAGIC       0x47425044u
#define ADHOC_DATAGRAM_VERSION     1u
#define ADHOC_DATAGRAM_FLAG_DATA   0x01u
#define ADHOC_DATAGRAM_FLAG_PROBE  0x02u
#define ADHOC_DATAGRAM_ACK_HOST_DELAY_US          1000u
#define ADHOC_DATAGRAM_ACK_JOIN_DELAY_US          3000u
#define ADHOC_DATAGRAM_ACK_HOST_REPEAT_DELAY_US   4000u
#define ADHOC_DATAGRAM_ACK_JOIN_REPEAT_DELAY_US   5000u
#define ADHOC_DATAGRAM_ACK_REPEATS                    3u
#define ADHOC_DATAGRAM_RTO_INITIAL 200000u
#define ADHOC_DATAGRAM_RTO_MIN     100000u
#define ADHOC_DATAGRAM_RTO_MAX    1000000u
#define ADHOC_DATAGRAM_RTO_JITTER   25000u

static void adhoc_datagram_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8);
  output[1] = (uint8_t)value;
}

static void adhoc_datagram_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static uint16_t adhoc_datagram_read_u16(const uint8_t *input)
{
  return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t adhoc_datagram_read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) |
   ((uint32_t)input[1] << 16) |
   ((uint32_t)input[2] << 8) |
   input[3];
}

static uint64_t adhoc_datagram_add_time(uint64_t value, uint64_t amount)
{
  return UINT64_MAX - value < amount ? UINT64_MAX : value + amount;
}

static void adhoc_datagram_increment(unsigned int *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

static int adhoc_datagram_after(uint32_t value, uint32_t reference)
{
  uint32_t difference = value - reference;

  return difference != 0 && difference < 0x80000000u;
}

static uint32_t adhoc_datagram_distance(uint32_t value,
 uint32_t reference)
{
  uint32_t distance = value - reference;

  if(value < reference && distance != 0)
    distance--;
  return distance;
}

uint32_t adhoc_datagram_next_sequence(uint32_t sequence)
{
  sequence++;
  return sequence == 0 ? 1u : sequence;
}

static uint32_t adhoc_datagram_crc(const uint8_t *packet, size_t size)
{
  static const uint8_t zero_crc[4] = { 0, 0, 0, 0 };
  uLong crc = crc32(0L, Z_NULL, 0);

  crc = crc32(crc, packet, 28);
  crc = crc32(crc, zero_crc, sizeof(zero_crc));
  if(size > ADHOC_DATAGRAM_HEADER_SIZE)
  {
    crc = crc32(crc, packet + ADHOC_DATAGRAM_HEADER_SIZE,
     (uInt)(size - ADHOC_DATAGRAM_HEADER_SIZE));
  }
  return (uint32_t)crc;
}

static uint64_t adhoc_datagram_ack_initial_delay(
 const AdhocDatagram *datagram)
{
  return datagram->host_role ? ADHOC_DATAGRAM_ACK_HOST_DELAY_US :
   ADHOC_DATAGRAM_ACK_JOIN_DELAY_US;
}

static uint64_t adhoc_datagram_ack_repeat_delay(
 const AdhocDatagram *datagram)
{
  return datagram->host_role ? ADHOC_DATAGRAM_ACK_HOST_REPEAT_DELAY_US :
   ADHOC_DATAGRAM_ACK_JOIN_REPEAT_DELAY_US;
}

static void adhoc_datagram_schedule_ack(AdhocDatagram *datagram,
 uint64_t now_us)
{
  datagram->ack_pending = 1;
  datagram->ack_repeats = ADHOC_DATAGRAM_ACK_REPEATS;
  datagram->ack_due_us = adhoc_datagram_add_time(now_us,
   adhoc_datagram_ack_initial_delay(datagram));
}

static uint32_t adhoc_datagram_ack_base(const AdhocDatagram *datagram)
{
  return datagram->delivered_valid ? datagram->last_delivered_sequence : 0;
}

static uint16_t adhoc_datagram_ack_bits(const AdhocDatagram *datagram)
{
  uint32_t sequence = datagram->delivered_valid ?
   adhoc_datagram_next_sequence(datagram->last_delivered_sequence) : 1u;
  uint16_t bits = 0;
  unsigned int offset;
  unsigned int index;

  for(offset = 0; offset < ADHOC_DATAGRAM_RX_WINDOW; offset++)
  {
    for(index = 0; index < ADHOC_DATAGRAM_RX_WINDOW; index++)
    {
      if(datagram->rx[index].valid &&
         datagram->rx[index].sequence == sequence)
      {
        bits |= (uint16_t)(1u << offset);
        break;
      }
    }
    sequence = adhoc_datagram_next_sequence(sequence);
  }
  return bits;
}

static int adhoc_datagram_encode(const AdhocDatagram *datagram,
 uint8_t flags, uint32_t sequence, const uint8_t *payload,
 size_t payload_size, uint8_t *output, size_t capacity,
 size_t *output_size)
{
  size_t packet_size = ADHOC_DATAGRAM_HEADER_SIZE + payload_size;

  if(datagram == NULL || output == NULL || output_size == NULL ||
     datagram->connection_id == 0 || datagram->epoch == 0 ||
     payload_size > ADHOC_DATAGRAM_MAX_PAYLOAD || capacity < packet_size ||
     (flags & ~(ADHOC_DATAGRAM_FLAG_DATA |
                ADHOC_DATAGRAM_FLAG_PROBE)) != 0)
    return 0;
  if((flags & ADHOC_DATAGRAM_FLAG_DATA) != 0)
  {
    if(sequence == 0 || payload == NULL || payload_size == 0 ||
       (flags & ADHOC_DATAGRAM_FLAG_PROBE) != 0)
      return 0;
  }
  else if(sequence != 0 || payload_size != 0)
  {
    return 0;
  }

  memset(output, 0, ADHOC_DATAGRAM_HEADER_SIZE);
  adhoc_datagram_write_u32(output, ADHOC_DATAGRAM_MAGIC);
  output[4] = ADHOC_DATAGRAM_VERSION;
  output[5] = flags;
  adhoc_datagram_write_u16(output + 6, (uint16_t)payload_size);
  adhoc_datagram_write_u32(output + 8, datagram->connection_id);
  adhoc_datagram_write_u32(output + 12, datagram->epoch);
  adhoc_datagram_write_u32(output + 16, sequence);
  adhoc_datagram_write_u32(output + 20,
   adhoc_datagram_ack_base(datagram));
  adhoc_datagram_write_u16(output + 24,
   adhoc_datagram_ack_bits(datagram));
  adhoc_datagram_write_u16(output + 26, ADHOC_DATAGRAM_HEADER_SIZE);
  if(payload_size != 0)
    memcpy(output + ADHOC_DATAGRAM_HEADER_SIZE, payload, payload_size);
  adhoc_datagram_write_u32(output + 28,
   adhoc_datagram_crc(output, packet_size));
  *output_size = packet_size;
  return 1;
}

void adhoc_datagram_init(AdhocDatagram *datagram,
 uint32_t connection_id, uint32_t epoch, uint64_t now_us)
{
  if(datagram == NULL)
    return;
  memset(datagram, 0, sizeof(*datagram));
  datagram->connection_id = connection_id;
  datagram->epoch = epoch;
  datagram->tx_base_sequence = 1;
  datagram->next_tx_sequence = 1;
  datagram->next_rx_sequence = 1;
  datagram->last_peer_us = now_us;
  datagram->rto_us = ADHOC_DATAGRAM_RTO_INITIAL;
  datagram->host_role = 1;
}

void adhoc_datagram_set_role(AdhocDatagram *datagram, int host)
{
  if(datagram == NULL)
    return;
  datagram->host_role = host != 0;
}

int adhoc_datagram_can_queue(const AdhocDatagram *datagram)
{
  return datagram != NULL && datagram->connection_id != 0 &&
   datagram->epoch != 0 && datagram->tx_count < ADHOC_DATAGRAM_TX_WINDOW &&
   adhoc_datagram_distance(datagram->next_tx_sequence,
    datagram->tx_base_sequence) < ADHOC_DATAGRAM_TX_WINDOW;
}

int adhoc_datagram_queue(AdhocDatagram *datagram,
 const uint8_t *payload, size_t size)
{
  unsigned int index;

  if(!adhoc_datagram_can_queue(datagram) || payload == NULL || size == 0 ||
     size > ADHOC_DATAGRAM_MAX_PAYLOAD)
    return 0;
  for(index = 0; index < ADHOC_DATAGRAM_TX_WINDOW; index++)
  {
    AdhocDatagramTxSlot *slot = &datagram->tx[index];

    if(slot->valid)
      continue;
    memset(slot, 0, sizeof(*slot));
    slot->sequence = datagram->next_tx_sequence;
    slot->size = (uint16_t)size;
    slot->valid = 1;
    memcpy(slot->payload, payload, size);
    datagram->next_tx_sequence = adhoc_datagram_next_sequence(
     datagram->next_tx_sequence);
    datagram->tx_count++;
    return 1;
  }
  return 0;
}

void adhoc_datagram_request_probe(AdhocDatagram *datagram)
{
  if(datagram != NULL)
    datagram->probe_pending = 1;
}

static AdhocDatagramTxSlot *adhoc_datagram_due_slot(
 AdhocDatagram *datagram, uint64_t now_us)
{
  uint32_t sequence = datagram->tx_base_sequence;
  unsigned int offset;
  unsigned int index;

  for(offset = 0; offset < ADHOC_DATAGRAM_TX_WINDOW; offset++)
  {
    for(index = 0; index < ADHOC_DATAGRAM_TX_WINDOW; index++)
    {
      AdhocDatagramTxSlot *slot = &datagram->tx[index];

      if(!slot->valid || slot->sequence != sequence)
        continue;
      if(slot->attempts == 0 || now_us >= slot->deadline_us)
        return slot;
      break;
    }
    sequence = adhoc_datagram_next_sequence(sequence);
  }
  return NULL;
}

int adhoc_datagram_prepare(AdhocDatagram *datagram, uint64_t now_us,
 AdhocDatagramPacket *packet)
{
  AdhocDatagramTxSlot *slot;
  size_t packet_size = 0;

  if(datagram == NULL || packet == NULL || datagram->connection_id == 0 ||
     datagram->epoch == 0)
    return 0;
  memset(packet, 0, sizeof(*packet));
  if(datagram->retry_due_us != 0 && now_us < datagram->retry_due_us)
    return 0;
  slot = adhoc_datagram_due_slot(datagram, now_us);
  if(datagram->ack_pending && now_us >= datagram->ack_due_us &&
     slot != NULL && slot->attempts != 0)
  {
    if(!adhoc_datagram_encode(datagram, 0, 0, NULL, 0, packet->data,
       sizeof(packet->data), &packet_size))
      return 0;
    packet->kind = ADHOC_DATAGRAM_TX_ACK;
  }
  else if(slot != NULL)
  {
    if(!adhoc_datagram_encode(datagram, ADHOC_DATAGRAM_FLAG_DATA,
       slot->sequence, slot->payload, slot->size, packet->data,
       sizeof(packet->data), &packet_size))
      return 0;
    packet->kind = ADHOC_DATAGRAM_TX_DATA;
    packet->sequence = slot->sequence;
  }
  else if(datagram->probe_pending)
  {
    if(!adhoc_datagram_encode(datagram, ADHOC_DATAGRAM_FLAG_PROBE, 0,
       NULL, 0, packet->data, sizeof(packet->data), &packet_size))
      return 0;
    packet->kind = ADHOC_DATAGRAM_TX_PROBE;
  }
  else if(datagram->ack_pending && now_us >= datagram->ack_due_us)
  {
    if(!adhoc_datagram_encode(datagram, 0, 0, NULL, 0, packet->data,
       sizeof(packet->data), &packet_size))
      return 0;
    packet->kind = ADHOC_DATAGRAM_TX_ACK;
  }
  else
  {
    return 0;
  }
  packet->size = (uint16_t)packet_size;
  return 1;
}

static uint32_t adhoc_datagram_retry_hash(const AdhocDatagram *datagram,
 uint32_t sequence, uint8_t attempts)
{
  uint32_t value = sequence ^ (uint32_t)attempts * 0x9E3779B9u;

  value ^= datagram->host_role ? 0xA341316Cu : 0xC8013EA4u;
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  return value ^ (value >> 16);
}

static uint64_t adhoc_datagram_backoff(const AdhocDatagram *datagram,
 uint32_t sequence, uint8_t attempts)
{
  uint64_t timeout = datagram->rto_us;
  uint64_t jitter;

  jitter = adhoc_datagram_retry_hash(datagram, sequence, attempts) %
   (ADHOC_DATAGRAM_RTO_JITTER + 1u);
  return timeout + jitter;
}

static void adhoc_datagram_backoff_rto(AdhocDatagram *datagram,
 uint64_t now_us)
{
  uint64_t next;

  if(datagram->last_backoff_us != 0 &&
     now_us - datagram->last_backoff_us < datagram->rto_us)
    return;
  next = datagram->rto_us << 1;
  if(next > ADHOC_DATAGRAM_RTO_MAX || next < datagram->rto_us)
    next = ADHOC_DATAGRAM_RTO_MAX;
  datagram->rto_us = next;
  datagram->last_backoff_us = now_us;
}

static void adhoc_datagram_note_ack_sent(AdhocDatagram *datagram,
 uint64_t now_us)
{
  if(datagram->ack_repeats != 0)
    datagram->ack_repeats--;
  datagram->ack_pending = datagram->ack_repeats != 0;
  datagram->ack_due_us = datagram->ack_pending ?
   adhoc_datagram_add_time(now_us,
    adhoc_datagram_ack_repeat_delay(datagram)) : 0;
}

int adhoc_datagram_note_sent(AdhocDatagram *datagram,
 const AdhocDatagramPacket *packet, uint64_t now_us)
{
  unsigned int index;

  if(datagram == NULL || packet == NULL ||
     packet->kind == ADHOC_DATAGRAM_TX_NONE)
    return 0;
  if(packet->kind == ADHOC_DATAGRAM_TX_DATA)
  {
    for(index = 0; index < ADHOC_DATAGRAM_TX_WINDOW; index++)
    {
      AdhocDatagramTxSlot *slot = &datagram->tx[index];

      if(!slot->valid || slot->sequence != packet->sequence)
        continue;
      if(slot->attempts == 0)
        slot->first_sent_us = now_us;
      else
      {
        adhoc_datagram_increment(&datagram->retransmissions);
        adhoc_datagram_backoff_rto(datagram, now_us);
      }
      if(slot->attempts != UINT8_MAX)
        slot->attempts++;
      slot->last_sent_us = now_us;
      slot->deadline_us = adhoc_datagram_add_time(now_us,
       adhoc_datagram_backoff(datagram, slot->sequence,
        (uint8_t)(slot->attempts - 1u)));
      datagram->retry_due_us = 0;
      adhoc_datagram_note_ack_sent(datagram, now_us);
      return 1;
    }
    return 0;
  }
  if(packet->kind == ADHOC_DATAGRAM_TX_PROBE)
  {
    datagram->probe_pending = 0;
    adhoc_datagram_increment(&datagram->probes);
  }
  else if(packet->kind != ADHOC_DATAGRAM_TX_ACK)
  {
    return 0;
  }
  datagram->retry_due_us = 0;
  adhoc_datagram_note_ack_sent(datagram, now_us);
  adhoc_datagram_increment(&datagram->ack_packets);
  return 1;
}

int adhoc_datagram_note_blocked(AdhocDatagram *datagram,
 const AdhocDatagramPacket *packet, uint64_t now_us)
{
  if(datagram == NULL || packet == NULL ||
     packet->kind == ADHOC_DATAGRAM_TX_NONE)
    return 0;
  datagram->retry_due_us = adhoc_datagram_add_time(now_us,
   ADHOC_DATAGRAM_BACKPRESSURE_RETRY_US);
  return 1;
}

static int adhoc_datagram_sequence_acked(uint32_t sequence,
 uint32_t ack_base, uint16_t ack_bits)
{
  uint32_t candidate;
  unsigned int offset;

  if(ack_base != 0 &&
     (sequence == ack_base || adhoc_datagram_after(ack_base, sequence)))
    return 1;
  candidate = ack_base == 0 ? 1u :
   adhoc_datagram_next_sequence(ack_base);
  for(offset = 0; offset < ADHOC_DATAGRAM_RX_WINDOW; offset++)
  {
    if(sequence == candidate)
      return (ack_bits & (uint16_t)(1u << offset)) != 0;
    candidate = adhoc_datagram_next_sequence(candidate);
  }
  return 0;
}

static void adhoc_datagram_update_rto(AdhocDatagram *datagram,
 uint64_t sample_us)
{
  uint64_t difference;
  uint64_t rto;

  if(sample_us == 0)
    sample_us = 1;
  if(datagram->smoothed_rtt_us == 0)
  {
    datagram->smoothed_rtt_us = sample_us;
    datagram->rtt_variation_us = sample_us / 2u;
  }
  else
  {
    difference = datagram->smoothed_rtt_us > sample_us ?
     datagram->smoothed_rtt_us - sample_us :
     sample_us - datagram->smoothed_rtt_us;
    datagram->rtt_variation_us =
     (3u * datagram->rtt_variation_us + difference) / 4u;
    datagram->smoothed_rtt_us =
     (7u * datagram->smoothed_rtt_us + sample_us) / 8u;
  }
  rto = datagram->smoothed_rtt_us + 4u * datagram->rtt_variation_us;
  if(rto < ADHOC_DATAGRAM_RTO_MIN)
    rto = ADHOC_DATAGRAM_RTO_MIN;
  if(rto > ADHOC_DATAGRAM_RTO_MAX)
    rto = ADHOC_DATAGRAM_RTO_MAX;
  datagram->rto_us = rto;
  datagram->last_backoff_us = 0;
}

static void adhoc_datagram_process_ack(AdhocDatagram *datagram,
 uint32_t ack_base, uint16_t ack_bits, uint64_t now_us)
{
  uint32_t previous_base = datagram->tx_base_sequence;
  uint32_t next_ack;
  uint32_t sent_span;
  uint32_t ack_span;
  unsigned int index;
  int ack_valid = 0;
  int progressed = 0;

  if(ack_base == 0)
  {
    ack_valid = datagram->tx_base_sequence == 1;
  }
  else
  {
    next_ack = adhoc_datagram_next_sequence(ack_base);
    sent_span = adhoc_datagram_distance(datagram->next_tx_sequence,
     datagram->tx_base_sequence);
    ack_span = adhoc_datagram_distance(next_ack,
     datagram->tx_base_sequence);
    if(ack_span <= sent_span)
    {
      datagram->tx_base_sequence = next_ack;
      ack_valid = 1;
    }
  }
  if(!ack_valid)
    return;

  for(index = 0; index < ADHOC_DATAGRAM_TX_WINDOW; index++)
  {
    AdhocDatagramTxSlot *slot = &datagram->tx[index];

    if(!slot->valid || !adhoc_datagram_sequence_acked(slot->sequence,
       ack_base, ack_bits))
      continue;
    if(slot->attempts == 1 && now_us >= slot->first_sent_us)
      adhoc_datagram_update_rto(datagram, now_us - slot->first_sent_us);
    memset(slot, 0, sizeof(*slot));
    progressed = 1;
    if(datagram->tx_count != 0)
      datagram->tx_count--;
  }
  if(datagram->tx_base_sequence != previous_base || progressed)
  {
    adhoc_datagram_increment(&datagram->ack_advances);
    datagram->last_ack_progress_us = now_us;
  }
}

static int adhoc_datagram_receive_offset(const AdhocDatagram *datagram,
 uint32_t sequence, unsigned int *offset)
{
  uint32_t candidate = datagram->next_rx_sequence;
  unsigned int index;

  for(index = 0; index < ADHOC_DATAGRAM_RX_WINDOW; index++)
  {
    if(candidate == sequence)
    {
      *offset = index;
      return 1;
    }
    candidate = adhoc_datagram_next_sequence(candidate);
  }
  return 0;
}

AdhocDatagramReceiveResult adhoc_datagram_receive(
 AdhocDatagram *datagram, const uint8_t *packet, size_t size,
 uint64_t now_us)
{
  uint32_t connection_id;
  uint32_t epoch;
  uint32_t sequence;
  uint32_t ack_base;
  uint32_t received_crc;
  uint16_t ack_bits;
  uint16_t payload_size;
  uint8_t flags;
  unsigned int offset;
  unsigned int index;

  if(datagram == NULL || packet == NULL ||
     size < ADHOC_DATAGRAM_HEADER_SIZE ||
     size > ADHOC_DATAGRAM_MAX_PACKET ||
     adhoc_datagram_read_u32(packet) != ADHOC_DATAGRAM_MAGIC ||
     packet[4] != ADHOC_DATAGRAM_VERSION ||
     adhoc_datagram_read_u16(packet + 26) !=
      ADHOC_DATAGRAM_HEADER_SIZE)
    goto invalid;
  flags = packet[5];
  payload_size = adhoc_datagram_read_u16(packet + 6);
  sequence = adhoc_datagram_read_u32(packet + 16);
  if((flags & ~(ADHOC_DATAGRAM_FLAG_DATA |
                ADHOC_DATAGRAM_FLAG_PROBE)) != 0 ||
     size != ADHOC_DATAGRAM_HEADER_SIZE + payload_size ||
     payload_size > ADHOC_DATAGRAM_MAX_PAYLOAD ||
     ((flags & ADHOC_DATAGRAM_FLAG_DATA) != 0 &&
      (sequence == 0 || payload_size == 0 ||
       (flags & ADHOC_DATAGRAM_FLAG_PROBE) != 0)) ||
     ((flags & ADHOC_DATAGRAM_FLAG_DATA) == 0 &&
      (sequence != 0 || payload_size != 0)))
    goto invalid;
  received_crc = adhoc_datagram_read_u32(packet + 28);
  if(received_crc != adhoc_datagram_crc(packet, size))
    goto invalid;

  connection_id = adhoc_datagram_read_u32(packet + 8);
  epoch = adhoc_datagram_read_u32(packet + 12);
  if(connection_id != datagram->connection_id || epoch != datagram->epoch)
    return ADHOC_DATAGRAM_FOREIGN;

  ack_base = adhoc_datagram_read_u32(packet + 20);
  ack_bits = adhoc_datagram_read_u16(packet + 24);
  adhoc_datagram_process_ack(datagram, ack_base, ack_bits, now_us);
  datagram->last_peer_us = now_us;
  if((flags & ADHOC_DATAGRAM_FLAG_PROBE) != 0)
  {
    adhoc_datagram_schedule_ack(datagram, now_us);
    return ADHOC_DATAGRAM_ACCEPTED;
  }
  if((flags & ADHOC_DATAGRAM_FLAG_DATA) == 0)
  {
    adhoc_datagram_increment(&datagram->received_ack_packets);
    return ADHOC_DATAGRAM_ACCEPTED;
  }

  adhoc_datagram_increment(&datagram->received_data_packets);

  adhoc_datagram_schedule_ack(datagram, now_us);
  if(!adhoc_datagram_receive_offset(datagram, sequence, &offset))
  {
    if(!adhoc_datagram_after(sequence, datagram->next_rx_sequence))
    {
      adhoc_datagram_increment(&datagram->duplicates);
      adhoc_datagram_schedule_ack(datagram, now_us);
      return ADHOC_DATAGRAM_DUPLICATE;
    }
    adhoc_datagram_increment(&datagram->out_of_window);
    adhoc_datagram_schedule_ack(datagram, now_us);
    return ADHOC_DATAGRAM_OUT_OF_WINDOW;
  }

  for(index = 0; index < ADHOC_DATAGRAM_RX_WINDOW; index++)
  {
    AdhocDatagramRxSlot *slot = &datagram->rx[index];

    if(slot->valid && slot->sequence == sequence)
    {
      adhoc_datagram_increment(&datagram->duplicates);
      adhoc_datagram_schedule_ack(datagram, now_us);
      return ADHOC_DATAGRAM_DUPLICATE;
    }
  }
  for(index = 0; index < ADHOC_DATAGRAM_RX_WINDOW; index++)
  {
    AdhocDatagramRxSlot *slot = &datagram->rx[index];

    if(slot->valid)
      continue;
    slot->sequence = sequence;
    slot->size = payload_size;
    slot->valid = 1;
    memcpy(slot->payload, packet + ADHOC_DATAGRAM_HEADER_SIZE,
     payload_size);
    datagram->rx_count++;
    (void)offset;
    return ADHOC_DATAGRAM_ACCEPTED;
  }

invalid:
  if(datagram != NULL)
    adhoc_datagram_increment(&datagram->invalid_packets);
  return ADHOC_DATAGRAM_INVALID;
}

int adhoc_datagram_pop(AdhocDatagram *datagram, uint8_t *payload,
 size_t capacity, size_t *size)
{
  unsigned int index;

  if(size != NULL)
    *size = 0;
  if(datagram == NULL || payload == NULL || size == NULL)
    return 0;
  for(index = 0; index < ADHOC_DATAGRAM_RX_WINDOW; index++)
  {
    AdhocDatagramRxSlot *slot = &datagram->rx[index];

    if(!slot->valid || slot->sequence != datagram->next_rx_sequence)
      continue;
    if(slot->size > capacity)
      return 0;
    memcpy(payload, slot->payload, slot->size);
    *size = slot->size;
    datagram->last_delivered_sequence = slot->sequence;
    datagram->delivered_valid = 1;
    datagram->next_rx_sequence = adhoc_datagram_next_sequence(
     datagram->next_rx_sequence);
    memset(slot, 0, sizeof(*slot));
    if(datagram->rx_count != 0)
      datagram->rx_count--;
    return 1;
  }
  return 0;
}

unsigned int adhoc_datagram_outstanding(const AdhocDatagram *datagram)
{
  return datagram != NULL ? datagram->tx_count : 0;
}

unsigned int adhoc_datagram_buffered(const AdhocDatagram *datagram)
{
  return datagram != NULL ? datagram->rx_count : 0;
}

int adhoc_datagram_progress_stalled(const AdhocDatagram *datagram,
 uint64_t now_us, uint64_t timeout_us)
{
  const AdhocDatagramTxSlot *oldest = NULL;
  uint32_t sequence;
  uint64_t progress_us;
  unsigned int offset;
  unsigned int index;

  if(datagram == NULL || datagram->tx_count == 0u || timeout_us == 0u)
    return 0;

  sequence = datagram->tx_base_sequence;
  for(offset = 0; offset < ADHOC_DATAGRAM_TX_WINDOW; offset++)
  {
    for(index = 0; index < ADHOC_DATAGRAM_TX_WINDOW; index++)
    {
      if(datagram->tx[index].valid &&
         datagram->tx[index].sequence == sequence)
      {
        oldest = &datagram->tx[index];
        break;
      }
    }
    if(oldest != NULL)
      break;
    sequence = adhoc_datagram_next_sequence(sequence);
  }
  if(oldest == NULL || oldest->attempts == 0u ||
     now_us < oldest->first_sent_us)
    return 0;

  progress_us = oldest->first_sent_us;
  if(datagram->last_ack_progress_us > progress_us &&
     datagram->last_ack_progress_us <= now_us)
    progress_us = datagram->last_ack_progress_us;
  return now_us - progress_us >= timeout_us;
}
