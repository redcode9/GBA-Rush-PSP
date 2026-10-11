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

#ifndef ADHOC_DATAGRAM_H
#define ADHOC_DATAGRAM_H

#include <stddef.h>
#include <stdint.h>

#define ADHOC_DATAGRAM_TX_WINDOW 8u
#define ADHOC_DATAGRAM_RX_WINDOW 16u
#define ADHOC_DATAGRAM_HEADER_SIZE 32u
#define ADHOC_DATAGRAM_MAX_PAYLOAD 152u
#define ADHOC_DATAGRAM_MAX_PACKET \
 (ADHOC_DATAGRAM_HEADER_SIZE + ADHOC_DATAGRAM_MAX_PAYLOAD)
#define ADHOC_DATAGRAM_BACKPRESSURE_RETRY_US 10000u

typedef enum
{
  ADHOC_DATAGRAM_INVALID = -1,
  ADHOC_DATAGRAM_FOREIGN = 0,
  ADHOC_DATAGRAM_ACCEPTED = 1,
  ADHOC_DATAGRAM_DUPLICATE = 2,
  ADHOC_DATAGRAM_OUT_OF_WINDOW = 3
} AdhocDatagramReceiveResult;

typedef enum
{
  ADHOC_DATAGRAM_TX_NONE,
  ADHOC_DATAGRAM_TX_DATA,
  ADHOC_DATAGRAM_TX_ACK,
  ADHOC_DATAGRAM_TX_PROBE
} AdhocDatagramTxKind;

typedef struct
{
  uint32_t sequence;
  uint64_t first_sent_us;
  uint64_t last_sent_us;
  uint64_t deadline_us;
  uint16_t size;
  uint8_t attempts;
  uint8_t valid;
  uint8_t payload[ADHOC_DATAGRAM_MAX_PAYLOAD];
} AdhocDatagramTxSlot;

typedef struct
{
  uint32_t sequence;
  uint16_t size;
  uint8_t valid;
  uint8_t payload[ADHOC_DATAGRAM_MAX_PAYLOAD];
} AdhocDatagramRxSlot;

typedef struct
{
  AdhocDatagramTxKind kind;
  uint32_t sequence;
  uint16_t size;
  uint8_t data[ADHOC_DATAGRAM_MAX_PACKET];
} AdhocDatagramPacket;

typedef struct
{
  uint32_t connection_id;
  uint32_t epoch;
  uint32_t tx_base_sequence;
  uint32_t next_tx_sequence;
  uint32_t next_rx_sequence;
  uint32_t last_delivered_sequence;
  uint64_t last_peer_us;
  uint64_t ack_due_us;
  uint64_t retry_due_us;
  uint64_t rto_us;
  uint64_t last_backoff_us;
  uint64_t smoothed_rtt_us;
  uint64_t rtt_variation_us;
  AdhocDatagramTxSlot tx[ADHOC_DATAGRAM_TX_WINDOW];
  AdhocDatagramRxSlot rx[ADHOC_DATAGRAM_RX_WINDOW];
  unsigned int tx_count;
  unsigned int rx_count;
  unsigned int retransmissions;
  unsigned int duplicates;
  unsigned int out_of_window;
  unsigned int invalid_packets;
  unsigned int ack_packets;
  unsigned int received_data_packets;
  unsigned int received_ack_packets;
  unsigned int ack_advances;
  unsigned int probes;
  uint64_t last_ack_progress_us;
  uint8_t delivered_valid;
  uint8_t ack_pending;
  uint8_t ack_repeats;
  uint8_t probe_pending;
  uint8_t host_role;
} AdhocDatagram;

uint32_t adhoc_datagram_next_sequence(uint32_t sequence);
void adhoc_datagram_init(AdhocDatagram *datagram,
 uint32_t connection_id, uint32_t epoch, uint64_t now_us);
void adhoc_datagram_set_role(AdhocDatagram *datagram, int host);
int adhoc_datagram_can_queue(const AdhocDatagram *datagram);
int adhoc_datagram_queue(AdhocDatagram *datagram,
 const uint8_t *payload, size_t size);
void adhoc_datagram_request_probe(AdhocDatagram *datagram);
int adhoc_datagram_prepare(AdhocDatagram *datagram, uint64_t now_us,
 AdhocDatagramPacket *packet);
int adhoc_datagram_note_sent(AdhocDatagram *datagram,
 const AdhocDatagramPacket *packet, uint64_t now_us);
int adhoc_datagram_note_blocked(AdhocDatagram *datagram,
 const AdhocDatagramPacket *packet, uint64_t now_us);
AdhocDatagramReceiveResult adhoc_datagram_receive(
 AdhocDatagram *datagram, const uint8_t *packet, size_t size,
 uint64_t now_us);
int adhoc_datagram_pop(AdhocDatagram *datagram, uint8_t *payload,
 size_t capacity, size_t *size);
unsigned int adhoc_datagram_outstanding(const AdhocDatagram *datagram);
unsigned int adhoc_datagram_buffered(const AdhocDatagram *datagram);
int adhoc_datagram_progress_stalled(const AdhocDatagram *datagram,
 uint64_t now_us, uint64_t timeout_us);

#endif /* ADHOC_DATAGRAM_H */
