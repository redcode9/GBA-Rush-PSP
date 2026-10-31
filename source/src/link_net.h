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
 * Reliable, ordered message channel between two consoles over a datagram
 * socket that may drop, duplicate or reorder packets.
 *
 * Every datagram repeats all messages the peer has not acknowledged yet,
 * the way GGPO resends unconfirmed inputs. A lost packet is therefore
 * repaired by the next one instead of waiting for a retransmission timer,
 * which matters on a radio link where losses come in short bursts.
 *
 * The header also carries the sender's guest clock, how many messages its
 * emulator has consumed and a timestamp echo for round-trip measurement.
 * The link code uses these to pace the faster console.
 *
 * The channel is plain C with no platform calls, so the same code runs on
 * the PSP and in the host tests.
 */

#ifndef LINK_NET_H
#define LINK_NET_H

#include <stddef.h>
#include <stdint.h>

#define LINK_NET_VERSION          1u
#define LINK_NET_MAX_DATAGRAM  1024u
#define LINK_NET_HEADER_SIZE     40u
#define LINK_NET_MAX_MESSAGE     40u
#define LINK_NET_QUEUE          128u

typedef struct
{
  uint8_t type;
  uint8_t size;
  uint8_t data[LINK_NET_MAX_MESSAGE];
} LinkNetMessage;

typedef struct
{
  uint32_t local_id;
  uint32_t peer_id;

  /* Messages from out_acked up to out_next are kept until acknowledged. */
  LinkNetMessage out[LINK_NET_QUEUE];
  uint32_t out_acked;
  uint32_t out_next;

  /* Messages delivered in order and waiting for the emulator. */
  LinkNetMessage in[LINK_NET_QUEUE];
  uint32_t in_read;
  uint32_t in_write;

  /* What the peer reported in its latest header. */
  uint32_t peer_consumed;
  uint32_t peer_clock;
  uint32_t peer_stamp;
  uint64_t peer_stamp_received_us;
  uint8_t peer_seen;

  uint64_t last_send_us;
  uint64_t last_receive_us;
  uint64_t heartbeat_us;
  uint8_t send_now;
  uint8_t ack_pending;

  uint32_t rtt_us;
  uint32_t rtt_max_us;
  uint32_t datagrams_sent;
  uint32_t datagrams_received;
  uint32_t datagrams_rejected;
  uint32_t resends;
} LinkNet;

void link_net_init(LinkNet *net, uint32_t local_id, uint64_t now_us);

/* Queues a message for reliable delivery. Returns 0 if the window is full. */
int link_net_queue(LinkNet *net, uint8_t type, const void *data,
 uint8_t size);

/* Next delivered message, or NULL. Pop it once it has been handled. */
const LinkNetMessage *link_net_peek(const LinkNet *net);
void link_net_pop(LinkNet *net);

/* Parses a datagram from the peer. Returns 1 if it was accepted. */
int link_net_receive(LinkNet *net, const uint8_t *datagram, size_t size,
 uint64_t now_us);

/* Builds the next datagram if one is due and returns its size, else 0.
   clock is the local guest cycle counter, reported to the peer. */
size_t link_net_build(LinkNet *net, uint8_t *datagram, size_t capacity,
 uint32_t clock, uint64_t now_us);

/* How often an otherwise idle channel sends a header. */
void link_net_set_heartbeat(LinkNet *net, uint32_t interval_us);

/* Messages sent that the peer's emulator has not consumed yet. */
uint32_t link_net_backlog(const LinkNet *net);
uint32_t link_net_unacked(const LinkNet *net);
int link_net_peer_silent(const LinkNet *net, uint64_t now_us,
 uint32_t timeout_us);

#endif /* LINK_NET_H */
