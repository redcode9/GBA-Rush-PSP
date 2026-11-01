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

#include "link_net.h"

#include <string.h>

/* Unacknowledged data is repeated after this long without any send. */
#define LINK_NET_MIN_RESEND_US   15000u
#define LINK_NET_DEFAULT_HEARTBEAT_US 250000u

static void put32(uint8_t *out, uint32_t value)
{
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
  out[2] = (uint8_t)(value >> 16);
  out[3] = (uint8_t)(value >> 24);
}

static uint32_t get32(const uint8_t *in)
{
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
   ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

/* Datagram layout, little-endian u32 fields:
 *    0 'G' 'R', version, message count
 *    4 sender id         8 receiver id (0 until the peer is known)
 *   12 sequence number of the first message (the oldest unacknowledged)
 *   16 ack: messages received in order
 *   20 messages the emulator has consumed (used for pacing)
 *   24 guest clock      28 send time (us)   32 echo of the peer's send time
 *   36 reserved (0)
 * then `count` messages, each [type, size, data]. */

/* Sequence numbers wrap, so compare them as signed distances. */
static int32_t seq_diff(uint32_t a, uint32_t b)
{
  return (int32_t)(a - b);
}

/* local_id identifies this console in every header; 0 is reserved. */
void link_net_init(LinkNet *net, uint32_t local_id, uint64_t now_us)
{
  memset(net, 0, sizeof(*net));
  net->local_id = local_id != 0 ? local_id : 1u;
  net->heartbeat_us = LINK_NET_DEFAULT_HEARTBEAT_US;
  net->ack_delay_us = LINK_NET_ACK_DELAY_US;
  net->last_receive_us = now_us;
  net->send_now = 1;
}

int link_net_queue(LinkNet *net, uint8_t type, const void *data,
 uint8_t size)
{
  LinkNetMessage *message;

  if(type == 0 || size > LINK_NET_MAX_MESSAGE ||
     net->out_next - net->out_acked >= LINK_NET_QUEUE)
    return 0;

  message = &net->out[net->out_next % LINK_NET_QUEUE];
  message->type = type;
  message->size = size;
  if(size != 0)
    memcpy(message->data, data, size);
  net->out_next++;
  net->send_now = 1;
  return 1;
}

const LinkNetMessage *link_net_peek(const LinkNet *net)
{
  if(net->in_read == net->in_write)
    return NULL;
  return &net->in[net->in_read % LINK_NET_QUEUE];
}

void link_net_pop(LinkNet *net)
{
  if(net->in_read == net->in_write)
    return;
  net->in_read++;
  /* The peer may be pacing itself on how much we have consumed. */
  net->ack_pending = 1;
}

/* echo is our own send time as the peer returned it, already advanced by
   how long the peer held it, so now - echo is the network round trip. */
static void link_net_sample_rtt(LinkNet *net, uint32_t echo,
 uint64_t now_us)
{
  uint32_t sample = (uint32_t)now_us - echo;

  /* Ignore samples that cannot be real, such as a stale echo. */
  if(echo == 0 || sample > 5000000u)
    return;
  /* Smoothed like TCP's SRTT: each sample moves the estimate by 1/8. */
  net->rtt_us = net->rtt_us == 0 ? sample :
   (net->rtt_us * 7u + sample) / 8u;
  if(net->rtt_min_us == 0 || sample < net->rtt_min_us)
    net->rtt_min_us = sample;
  if(sample > net->rtt_max_us)
    net->rtt_max_us = sample;
}

int link_net_receive(LinkNet *net, const uint8_t *datagram, size_t size,
 uint64_t now_us)
{
  uint32_t src;
  uint32_t dst;
  uint32_t seq;
  uint32_t ack;
  uint32_t consumed;
  size_t offset;
  unsigned int count;
  unsigned int i;

  if(datagram == NULL || size < LINK_NET_HEADER_SIZE ||
     size > LINK_NET_MAX_DATAGRAM || datagram[0] != 'G' ||
     datagram[1] != 'R' || datagram[2] != LINK_NET_VERSION)
    goto reject;

  count = datagram[3];
  src = get32(datagram + 4);
  dst = get32(datagram + 8);
  seq = get32(datagram + 12);
  ack = get32(datagram + 16);
  consumed = get32(datagram + 20);

  /* Packets from an earlier connection or another console carry a
     different identity and are ignored. */
  if(src == 0 || src == net->local_id ||
     (dst != 0 && dst != net->local_id) ||
     (net->peer_id != 0 && src != net->peer_id))
    goto reject;

  /* Validate every message before applying anything. */
  offset = LINK_NET_HEADER_SIZE;
  for(i = 0; i < count; i++)
  {
    if(size - offset < 2u || datagram[offset] == 0 ||
       datagram[offset + 1] > LINK_NET_MAX_MESSAGE ||
       size - offset - 2u < datagram[offset + 1])
      goto reject;
    offset += 2u + datagram[offset + 1];
  }
  if(offset != size)
    goto reject;

  /* An acknowledgement can only cover messages that were sent. */
  if(seq_diff(ack, net->out_next) > 0 ||
     seq_diff(consumed, net->out_next) > 0)
    goto reject;

  net->peer_id = src;
  net->peer_seen = 1;
  net->last_receive_us = now_us;
  net->datagrams_received++;

  if(seq_diff(ack, net->out_acked) > 0)
    net->out_acked = ack;
  if(seq_diff(consumed, net->peer_consumed) > 0)
    net->peer_consumed = consumed;
  net->peer_clock = get32(datagram + 24);
  net->peer_stamp = get32(datagram + 28);
  net->peer_stamp_received_us = now_us;
  link_net_sample_rtt(net, get32(datagram + 32), now_us);

  offset = LINK_NET_HEADER_SIZE;
  for(i = 0; i < count; i++)
  {
    uint32_t message_seq = seq + i;
    uint8_t type = datagram[offset];
    uint8_t message_size = datagram[offset + 1];
    int32_t distance = seq_diff(message_seq, net->in_write);

    if(distance < 0)
    {
      /* Already delivered: the peer missed our acknowledgement. */
      net->ack_pending = 1;
    }
    else if(distance == 0 && net->in_write - net->in_read < LINK_NET_QUEUE)
    {
      LinkNetMessage *message = &net->in[net->in_write % LINK_NET_QUEUE];

      message->type = type;
      message->size = message_size;
      memcpy(message->data, datagram + offset + 2, message_size);
      net->in_write++;
      net->ack_pending = 1;
    }
    else
    {
      /* A gap or a full inbox: the peer repeats these later. */
      break;
    }
    offset += 2u + message_size;
  }

  return 1;

reject:
  net->datagrams_rejected++;
  return 0;
}

size_t link_net_build(LinkNet *net, uint8_t *datagram, size_t capacity,
 uint32_t clock, uint64_t now_us)
{
  uint64_t since_send = now_us - net->last_send_us;
  uint32_t resend_us = net->rtt_us * 3u;
  uint32_t echo = 0;
  uint32_t seq;
  size_t offset;
  unsigned int count = 0;
  int resend = 0;

  if(capacity > LINK_NET_MAX_DATAGRAM)
    capacity = LINK_NET_MAX_DATAGRAM;
  if(datagram == NULL || capacity < LINK_NET_HEADER_SIZE)
    return 0;

  if(resend_us < LINK_NET_MIN_RESEND_US)
    resend_us = LINK_NET_MIN_RESEND_US;

  /* Send at once when a message was queued; otherwise only for a delayed
     ack, a resend after about three round trips, or the heartbeat. */
  if(!net->send_now)
  {
    int ack_due = net->ack_pending &&
     now_us - net->peer_stamp_received_us >= net->ack_delay_us;

    resend = net->out_next != net->out_acked && since_send >= resend_us;
    if(!ack_due && !resend && since_send < net->heartbeat_us)
      return 0;
  }

  /* The echo adds how long we held the peer's stamp, so the peer's
     round-trip sample leaves out our own delay. */
  if(net->peer_seen)
    echo = net->peer_stamp + (uint32_t)(now_us - net->peer_stamp_received_us);

  datagram[0] = 'G';
  datagram[1] = 'R';
  datagram[2] = LINK_NET_VERSION;
  put32(datagram + 4, net->local_id);
  put32(datagram + 8, net->peer_id);
  put32(datagram + 12, net->out_acked);
  put32(datagram + 16, net->in_write);
  put32(datagram + 20, net->in_read);
  put32(datagram + 24, clock);
  put32(datagram + 28, (uint32_t)now_us);
  put32(datagram + 32, echo);
  put32(datagram + 36, 0);

  offset = LINK_NET_HEADER_SIZE;
  for(seq = net->out_acked; seq != net->out_next && count < 255u; seq++)
  {
    const LinkNetMessage *message = &net->out[seq % LINK_NET_QUEUE];

    if(capacity - offset < 2u + message->size)
      break;
    datagram[offset] = message->type;
    datagram[offset + 1] = message->size;
    memcpy(datagram + offset + 2, message->data, message->size);
    offset += 2u + message->size;
    count++;
  }
  datagram[3] = (uint8_t)count;

  if(resend)
    net->resends++;
  net->send_now = 0;
  net->ack_pending = 0;
  net->last_send_us = now_us;
  net->datagrams_sent++;
  return offset;
}

void link_net_set_heartbeat(LinkNet *net, uint32_t interval_us)
{
  net->heartbeat_us = interval_us;
}

void link_net_set_ack_delay(LinkNet *net, uint32_t delay_us)
{
  net->ack_delay_us = delay_us;
}

uint32_t link_net_backlog(const LinkNet *net)
{
  return net->out_next - net->peer_consumed;
}

uint32_t link_net_unacked(const LinkNet *net)
{
  return net->out_next - net->out_acked;
}

int link_net_peer_silent(const LinkNet *net, uint64_t now_us,
 uint32_t timeout_us)
{
  return now_us - net->last_receive_us >= timeout_us;
}
