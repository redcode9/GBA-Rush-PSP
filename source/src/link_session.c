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

#include "link_session.h"

#include <string.h>

static uint64_t link_session_now(const LinkSession *session)
{
  return session->transport.ops->now_us(session->transport.context);
}

static uint32_t link_session_set_state(LinkSession *session,
 LinkSessionState state)
{
  if(session->state == state)
    return LINK_SESSION_EVENT_NONE;

  session->state = state;
  session->state_started_us = link_session_now(session);
  return LINK_SESSION_EVENT_STATE_CHANGE;
}

static int link_session_state_uses_connection(LinkSessionState state)
{
  return state == LINK_SESSION_NEGOTIATING ||
   state == LINK_SESSION_WAIT_READY ||
   state == LINK_SESSION_WAIT_READY_ACK ||
   state == LINK_SESSION_CONNECTED ||
   state == LINK_SESSION_WAIT_PREPARE ||
   state == LINK_SESSION_WAIT_ACCEPT ||
   state == LINK_SESSION_WAIT_COMMIT;
}

int link_session_is_connected(const LinkSession *session)
{
  return session != NULL &&
   (session->state == LINK_SESSION_CONNECTED ||
    session->state == LINK_SESSION_WAIT_PREPARE ||
    session->state == LINK_SESSION_WAIT_ACCEPT ||
    session->state == LINK_SESSION_WAIT_COMMIT);
}

int link_session_in_barrier(const LinkSession *session)
{
  return session != NULL && session->barrier_active != 0;
}

LinkSessionState link_session_state(const LinkSession *session)
{
  return session->state;
}

static void link_session_reset_runtime(LinkSession *session)
{
  session->peer_minor = 0;
  session->peer_sio_valid = 0;
  session->transport_connected = 0;
  session->barrier_active = 0;
  session->reason = LINK_REASON_NONE;
  session->reason_detail = 0;
  session->negotiated_capabilities = 0;
  session->last_sequence = 0;
  session->active_sequence = 0;
  session->barrier_started_us = 0;
  session->last_rx_us = 0;
  session->last_tx_us = 0;
  memset(&session->peer_hello, 0, sizeof(session->peer_hello));
  memset(&session->peer_sio, 0, sizeof(session->peer_sio));
  memset(&session->transfer, 0, sizeof(session->transfer));
  gba_sio_disconnect(session->sio);
}

void link_session_init(LinkSession *session, GbaSio *sio,
 LinkTransport transport, const LinkHello *hello)
{
  memset(session, 0, sizeof(*session));
  session->sio = sio;
  session->transport = transport;
  if(hello != NULL)
    session->local_hello = *hello;
  session->state = LINK_SESSION_IDLE;
}

static int link_session_transport_valid(const LinkSession *session)
{
  const LinkTransportOps *ops = session->transport.ops;

  return session->sio != NULL && ops != NULL && ops->host != NULL &&
   ops->join != NULL && ops->send != NULL && ops->poll != NULL &&
   ops->disconnect != NULL && ops->now_us != NULL;
}

int link_session_host(LinkSession *session, uint32_t session_id)
{
  if(session == NULL || session_id == 0 ||
     !link_session_transport_valid(session))
    return 0;

  if(session->state != LINK_SESSION_IDLE &&
     session->state != LINK_SESSION_DISCONNECTED &&
     session->state != LINK_SESSION_ERROR)
    link_session_disconnect(session, LINK_REASON_USER);

  link_session_reset_runtime(session);
  session->role = LINK_ROLE_HOST;
  session->session_id = session_id;
  session->state = LINK_SESSION_HOSTING;
  session->state_started_us = link_session_now(session);

  if(!session->transport.ops->host(session->transport.context))
  {
    session->state = LINK_SESSION_ERROR;
    session->reason = LINK_REASON_TRANSPORT;
    return 0;
  }
  return 1;
}

int link_session_join(LinkSession *session)
{
  if(session == NULL || !link_session_transport_valid(session))
    return 0;

  if(session->state != LINK_SESSION_IDLE &&
     session->state != LINK_SESSION_DISCONNECTED &&
     session->state != LINK_SESSION_ERROR)
    link_session_disconnect(session, LINK_REASON_USER);

  link_session_reset_runtime(session);
  session->role = LINK_ROLE_JOINER;
  session->session_id = 0;
  session->state = LINK_SESSION_JOINING;
  session->state_started_us = link_session_now(session);

  if(!session->transport.ops->join(session->transport.context))
  {
    session->state = LINK_SESSION_ERROR;
    session->reason = LINK_REASON_TRANSPORT;
    return 0;
  }
  return 1;
}

static int link_session_send(LinkSession *session, const LinkPacket *packet)
{
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;

  if(link_protocol_encode(packet, frame, sizeof(frame), &frame_size) !=
     LINK_PROTOCOL_OK)
    return 0;
  if(!session->transport.ops->send(session->transport.context, frame,
     frame_size))
    return 0;

  session->last_tx_us = link_session_now(session);
  return 1;
}

static int link_session_send_hello(LinkSession *session)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HELLO,
   session->session_id, 0);
  if(link_protocol_write_hello(&packet, &session->local_hello) !=
     LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static int link_session_send_ready(LinkSession *session,
 const LinkSessionReady *ready)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_SESSION_READY,
   session->session_id, 0);
  if(link_protocol_write_session_ready(&packet, ready) != LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static int link_session_send_reason(LinkSession *session,
 LinkMessageType type, LinkSessionReason reason, uint16_t detail)
{
  LinkPacket packet;
  LinkReason wire_reason;
  uint32_t sequence = session->barrier_active ?
   session->active_sequence : session->last_sequence;

  wire_reason.reason = (uint16_t)reason;
  wire_reason.detail = detail;
  link_protocol_packet_init(&packet, type, session->session_id, sequence);
  if(link_protocol_write_reason(&packet, type, &wire_reason) !=
     LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static uint32_t link_session_end_barrier(LinkSession *session, int abort)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;
  GbaSioEvent sio_event;

  if(session->barrier_active == 0)
    return events;

  if(abort)
  {
    sio_event = gba_sio_abort(session->sio);
    if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
      events |= LINK_SESSION_EVENT_SERIAL_IRQ;
  }

  session->barrier_active = 0;
  session->barrier_started_us = 0;
  events |= LINK_SESSION_EVENT_BARRIER_END;
  events |= abort ? LINK_SESSION_EVENT_TRANSFER_ABORTED :
   LINK_SESSION_EVENT_TRANSFER_COMMITTED;
  return events;
}

static int link_session_reason_valid(uint16_t reason)
{
  return reason <= LINK_REASON_ROM_CHANGE;
}

static uint32_t link_session_fail(LinkSession *session,
 LinkSessionReason reason, uint16_t detail, int notify_peer)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->state == LINK_SESSION_ERROR)
    return events;

  if(notify_peer && session->transport_connected && session->session_id != 0)
  {
    if(session->barrier_active)
      link_session_send_reason(session, LINK_MESSAGE_TRANSFER_ABORT,
       reason, detail);
    link_session_send_reason(session, LINK_MESSAGE_DISCONNECT, reason, detail);
  }

  events |= link_session_end_barrier(session, 1);
  session->reason = reason;
  session->reason_detail = detail;
  session->transport_connected = 0;
  session->transport.ops->disconnect(session->transport.context);
  events |= link_session_set_state(session, LINK_SESSION_ERROR);
  events |= LINK_SESSION_EVENT_SESSION_CLOSED;
  return events;
}

uint32_t link_session_disconnect(LinkSession *session,
 LinkSessionReason reason)
{
  uint32_t events;

  if(session == NULL || !link_session_transport_valid(session))
    return LINK_SESSION_EVENT_NONE;

  if(session->transport_connected && session->session_id != 0)
    link_session_send_reason(session, LINK_MESSAGE_DISCONNECT, reason, 0);

  events = link_session_end_barrier(session, 1);
  session->reason = reason;
  session->reason_detail = 0;
  session->transport_connected = 0;
  session->transport.ops->disconnect(session->transport.context);
  gba_sio_set_connection(session->sio, 0, 0);
  events |= link_session_set_state(session, LINK_SESSION_DISCONNECTED);
  events |= LINK_SESSION_EVENT_SESSION_CLOSED;
  return events;
}

static int link_session_capabilities_valid(LinkSession *session,
 const LinkHello *peer)
{
  uint32_t negotiated = session->local_hello.capabilities &
   peer->capabilities;

  if((negotiated & LINK_SESSION_REQUIRED_CAPABILITIES) !=
     LINK_SESSION_REQUIRED_CAPABILITIES)
    return 0;
  if(session->local_hello.max_players < 2 || peer->max_players < 2)
    return 0;

  session->negotiated_capabilities = negotiated;
  return 1;
}

static int link_session_sequence_is_current(const LinkSession *session,
 uint32_t sequence)
{
  if(sequence == session->last_sequence)
    return 1;
  return session->barrier_active && sequence == session->active_sequence;
}

static int link_session_sequence_is_next(const LinkSession *session,
 uint32_t sequence)
{
  return sequence == session->last_sequence + 1u;
}

static void link_session_transfer_init(LinkTransferData *transfer,
 const GbaSioTransfer *sio_transfer)
{
  uint32_t i;

  memset(transfer, 0, sizeof(*transfer));
  transfer->mode = (uint8_t)sio_transfer->mode;
  transfer->setting = sio_transfer->setting;
  transfer->clock_owner = sio_transfer->player_id;
  transfer->player_count = 2;
  transfer->participant_mask = (uint8_t)(1u << sio_transfer->player_id);
  for(i = 0; i < 4; i++)
    transfer->data[i] = 0xFFFFFFFFu;
  transfer->data[sio_transfer->player_id] = sio_transfer->outgoing;
}

static int link_session_transfer_shape_valid(const LinkTransferData *transfer)
{
  if(transfer->mode > GBA_SIO_MODE_MULTIPLAYER ||
     transfer->player_count != 2 || transfer->clock_owner > 1)
    return 0;
  if(transfer->mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    if(transfer->setting > 3 || transfer->clock_owner != 0)
      return 0;
  }
  else if(transfer->setting > 1)
  {
    return 0;
  }
  return 1;
}

static int link_session_transfer_matches(const LinkTransferData *left,
 const LinkTransferData *right)
{
  return left->mode == right->mode &&
   left->setting == right->setting &&
   left->clock_owner == right->clock_owner &&
   left->player_count == right->player_count;
}

static int link_session_send_transfer(LinkSession *session,
 LinkMessageType type, const LinkTransferData *transfer, uint32_t sequence)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, type, session->session_id, sequence);
  if(link_protocol_write_transfer(&packet, type, transfer) != LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static int link_session_commit_sio(LinkSession *session,
 const LinkTransferData *transfer)
{
  GbaSioResult result;
  uint32_t i;
  uint8_t local_player = session->role == LINK_ROLE_HOST ? 0 : 1;

  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  if(transfer->mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    for(i = 0; i < 4; i++)
      result.data[i] = transfer->data[i] & 0xFFFFu;
  }
  else
  {
    result.data[0] = transfer->data[local_player ^ 1u];
  }
  return gba_sio_commit(session->sio, &result);
}

uint32_t link_session_begin_transfer(LinkSession *session)
{
  GbaSioTransfer sio_transfer;
  LinkMessageType type;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session == NULL || session->state != LINK_SESSION_CONNECTED)
    return events;
  if(!gba_sio_take_start(session->sio, &sio_transfer))
    return events;

  if(sio_transfer.player_id > 1 || sio_transfer.player_count != 2)
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);
  if(sio_transfer.mode == GBA_SIO_MODE_MULTIPLAYER &&
     session->role != LINK_ROLE_HOST)
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  session->active_sequence = session->last_sequence + 1u;
  link_session_transfer_init(&session->transfer, &sio_transfer);
  session->barrier_active = 1;
  session->barrier_started_us = link_session_now(session);
  events |= LINK_SESSION_EVENT_BARRIER_BEGIN;

  if(session->role == LINK_ROLE_HOST)
  {
    type = LINK_MESSAGE_TRANSFER_PREPARE;
    events |= link_session_set_state(session, LINK_SESSION_WAIT_ACCEPT);
  }
  else
  {
    type = LINK_MESSAGE_TRANSFER_REQUEST;
    events |= link_session_set_state(session, LINK_SESSION_WAIT_PREPARE);
  }

  if(!link_session_send_transfer(session, type, &session->transfer,
     session->active_sequence))
  {
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  }
  return events;
}

uint32_t link_session_publish_sio_state(LinkSession *session,
 const LinkSioState *state)
{
  LinkPacket packet;

  if(session == NULL || state == NULL ||
     session->state != LINK_SESSION_CONNECTED)
    return LINK_SESSION_EVENT_NONE;

  link_protocol_packet_init(&packet, LINK_MESSAGE_SIO_STATE,
   session->session_id, session->last_sequence);
  if(link_protocol_write_sio_state(&packet, state) != LINK_PROTOCOL_OK ||
     !link_session_send(session, &packet))
  {
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  }
  return LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_handle_hello(LinkSession *session,
 const LinkPacket *packet)
{
  LinkHello hello;
  LinkSessionReady ready;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(packet->sequence != 0 ||
     link_protocol_read_hello(packet, &hello) != LINK_PROTOCOL_OK)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(session->role == LINK_ROLE_JOINER && session->session_id == 0 &&
     session->state == LINK_SESSION_NEGOTIATING)
  {
    if(packet->session_id == 0)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
    session->session_id = packet->session_id;
  }

  if(packet->session_id != session->session_id ||
     session->state != LINK_SESSION_NEGOTIATING)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_session_capabilities_valid(session, &hello))
    return link_session_fail(session, LINK_REASON_INCOMPATIBLE, 0, 1);

  session->peer_hello = hello;
  session->peer_minor = packet->minor;

  if(session->role == LINK_ROLE_HOST)
  {
    ready.capabilities = session->negotiated_capabilities;
    ready.player_id = 1;
    ready.player_count = 2;
    if(!link_session_send_ready(session, &ready))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    events |= link_session_set_state(session, LINK_SESSION_WAIT_READY_ACK);
  }
  else
  {
    if(!link_session_send_hello(session))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    events |= link_session_set_state(session, LINK_SESSION_WAIT_READY);
  }
  return events;
}

static int link_session_ready_valid(const LinkSession *session,
 const LinkSessionReady *ready)
{
  return ready->capabilities == session->negotiated_capabilities &&
   ready->player_id == 1 && ready->player_count == 2;
}

static uint32_t link_session_handle_ready(LinkSession *session,
 const LinkPacket *packet)
{
  LinkSessionReady ready;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(packet->session_id != session->session_id || packet->sequence != 0 ||
     link_protocol_read_session_ready(packet, &ready) != LINK_PROTOCOL_OK ||
     !link_session_ready_valid(session, &ready))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(session->role == LINK_ROLE_JOINER &&
     session->state == LINK_SESSION_WAIT_READY)
  {
    gba_sio_set_connection(session->sio, 1, 2);
    if(!link_session_send_ready(session, &ready))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  }
  else if(session->role == LINK_ROLE_HOST &&
          session->state == LINK_SESSION_WAIT_READY_ACK)
  {
    gba_sio_set_connection(session->sio, 0, 2);
    events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  }
  else
  {
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }
  return events;
}

static uint32_t link_session_handle_sio_state(LinkSession *session,
 const LinkPacket *packet)
{
  LinkSioState state;

  if(!link_session_is_connected(session) ||
     !link_session_sequence_is_current(session, packet->sequence) ||
     link_protocol_read_sio_state(packet, &state) != LINK_PROTOCOL_OK ||
     state.player_count != 2 || state.player_id > 1)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  session->peer_sio = state;
  session->peer_sio_valid = 1;
  return LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_handle_request(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  GbaSioTransfer local;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_HOST ||
     session->state != LINK_SESSION_CONNECTED ||
     !link_session_sequence_is_next(session, packet->sequence) ||
     link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK ||
     !link_session_transfer_shape_valid(&transfer) ||
     transfer.mode == GBA_SIO_MODE_MULTIPLAYER ||
     transfer.clock_owner != 1 || transfer.participant_mask != 0x02)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(!gba_sio_latch_remote_start(session->sio,
     (GbaSioMode)transfer.mode, transfer.setting, &local))
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  transfer.data[0] = local.outgoing;
  transfer.participant_mask = 0x03;
  session->transfer = transfer;
  session->active_sequence = packet->sequence;
  session->barrier_active = 1;
  session->barrier_started_us = link_session_now(session);
  events |= LINK_SESSION_EVENT_BARRIER_BEGIN;
  events |= link_session_set_state(session, LINK_SESSION_WAIT_ACCEPT);

  if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_PREPARE,
     &session->transfer, session->active_sequence))
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return events;
}

static uint32_t link_session_handle_prepare(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  GbaSioTransfer local;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_JOINER ||
     link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK ||
     !link_session_transfer_shape_valid(&transfer))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(session->state == LINK_SESSION_CONNECTED)
  {
    if(!link_session_sequence_is_next(session, packet->sequence) ||
       transfer.clock_owner != 0 || transfer.participant_mask != 0x01)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

    if(!gba_sio_latch_remote_start(session->sio,
       (GbaSioMode)transfer.mode, transfer.setting, &local))
      return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

    transfer.data[1] = local.outgoing;
    transfer.participant_mask = 0x03;
    session->active_sequence = packet->sequence;
    session->barrier_active = 1;
    session->barrier_started_us = link_session_now(session);
    events |= LINK_SESSION_EVENT_BARRIER_BEGIN;
  }
  else if(session->state == LINK_SESSION_WAIT_PREPARE)
  {
    if(packet->sequence != session->active_sequence ||
       transfer.clock_owner != 1 || transfer.participant_mask != 0x03 ||
       !link_session_transfer_matches(&transfer, &session->transfer) ||
       transfer.data[1] != session->transfer.data[1])
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }
  else
  {
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }

  session->transfer = transfer;
  events |= link_session_set_state(session, LINK_SESSION_WAIT_COMMIT);
  if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_ACCEPT,
     &session->transfer, session->active_sequence))
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return events;
}

static uint32_t link_session_handle_accept(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_HOST ||
     session->state != LINK_SESSION_WAIT_ACCEPT ||
     packet->sequence != session->active_sequence ||
     link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK ||
     !link_session_transfer_shape_valid(&transfer) ||
     transfer.participant_mask != 0x03 ||
     !link_session_transfer_matches(&transfer, &session->transfer) ||
     transfer.data[0] != session->transfer.data[0] ||
     ((session->transfer.participant_mask & 0x02) != 0 &&
      transfer.data[1] != session->transfer.data[1]))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  session->transfer = transfer;
  if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_COMMIT,
     &session->transfer, session->active_sequence))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  if(!link_session_commit_sio(session, &session->transfer))
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  session->last_sequence = session->active_sequence;
  events |= link_session_end_barrier(session, 0);
  events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  return events;
}

static uint32_t link_session_handle_commit(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_JOINER ||
     session->state != LINK_SESSION_WAIT_COMMIT ||
     packet->sequence != session->active_sequence ||
     link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK ||
     transfer.participant_mask != 0x03 ||
     !link_session_transfer_matches(&transfer, &session->transfer) ||
     memcmp(transfer.data, session->transfer.data,
      sizeof(transfer.data)) != 0)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(!link_session_commit_sio(session, &transfer))
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  session->last_sequence = session->active_sequence;
  events |= link_session_end_barrier(session, 0);
  events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  return events;
}

static uint32_t link_session_handle_abort(LinkSession *session,
 const LinkPacket *packet)
{
  LinkReason reason;

  if(!session->barrier_active ||
     packet->sequence != session->active_sequence ||
     link_protocol_read_reason(packet, &reason) != LINK_PROTOCOL_OK ||
     !link_session_reason_valid(reason.reason))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  return link_session_fail(session, (LinkSessionReason)reason.reason,
   reason.detail, 0);
}

static uint32_t link_session_handle_disconnect(LinkSession *session,
 const LinkPacket *packet)
{
  LinkReason reason;
  uint32_t events;

  if(!link_session_sequence_is_current(session, packet->sequence) ||
     link_protocol_read_reason(packet, &reason) != LINK_PROTOCOL_OK ||
     !link_session_reason_valid(reason.reason))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  events = link_session_end_barrier(session, 1);
  session->reason = (LinkSessionReason)reason.reason;
  session->reason_detail = reason.detail;
  session->transport_connected = 0;
  session->transport.ops->disconnect(session->transport.context);
  gba_sio_set_connection(session->sio, 0, 0);
  events |= link_session_set_state(session, LINK_SESSION_DISCONNECTED);
  events |= LINK_SESSION_EVENT_SESSION_CLOSED;
  return events;
}

static uint32_t link_session_handle_packet(LinkSession *session,
 const uint8_t *data, size_t size)
{
  LinkPacket packet;
  LinkProtocolError error;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  int adopt_session = session->role == LINK_ROLE_JOINER &&
   session->session_id == 0 && session->state == LINK_SESSION_NEGOTIATING;

  error = link_protocol_decode(data, size, &packet);
  if(error != LINK_PROTOCOL_OK)
    return link_session_fail(session, LINK_REASON_PROTOCOL,
     (uint16_t)error, 0);

  if(!adopt_session && packet.session_id != session->session_id)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_protocol_message_known(packet.type))
  {
    if(!link_session_is_connected(session) ||
       !link_session_sequence_is_current(session, packet.sequence))
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
    session->last_rx_us = link_session_now(session);
    return events;
  }

  switch(packet.type)
  {
    case LINK_MESSAGE_HELLO:
      events = link_session_handle_hello(session, &packet);
      break;

    case LINK_MESSAGE_SESSION_READY:
      events = link_session_handle_ready(session, &packet);
      break;

    case LINK_MESSAGE_SIO_STATE:
      events = link_session_handle_sio_state(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_REQUEST:
      events = link_session_handle_request(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_PREPARE:
      events = link_session_handle_prepare(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_ACCEPT:
      events = link_session_handle_accept(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_COMMIT:
      events = link_session_handle_commit(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_ABORT:
      events = link_session_handle_abort(session, &packet);
      break;

    case LINK_MESSAGE_HEARTBEAT:
      if(!link_session_is_connected(session) ||
         !link_session_sequence_is_current(session, packet.sequence))
        events = link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
      break;

    case LINK_MESSAGE_DISCONNECT:
      events = link_session_handle_disconnect(session, &packet);
      break;
  }

  if(session->state != LINK_SESSION_ERROR)
    session->last_rx_us = link_session_now(session);
  return events;
}

static uint32_t link_session_handle_connected(LinkSession *session)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint64_t now = link_session_now(session);

  if(session->state != LINK_SESSION_HOSTING &&
     session->state != LINK_SESSION_JOINING)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  session->transport_connected = 1;
  session->last_rx_us = now;
  session->last_tx_us = now;
  events |= link_session_set_state(session, LINK_SESSION_NEGOTIATING);

  if(session->role == LINK_ROLE_HOST && !link_session_send_hello(session))
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return events;
}

static uint32_t link_session_send_heartbeat(LinkSession *session)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   session->session_id, session->last_sequence);
  if(!link_session_send(session, &packet))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return LINK_SESSION_EVENT_NONE;
}

uint32_t link_session_pump(LinkSession *session)
{
  LinkTransportEvent transport_event;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint64_t now;
  int poll_result;
  uint32_t processed = 0;

  if(session == NULL || !link_session_transport_valid(session))
    return events;

  do
  {
    poll_result = session->transport.ops->poll(session->transport.context,
     &transport_event);
    if(poll_result < 0)
    {
      events |= link_session_fail(session, LINK_REASON_TRANSPORT, 0, 0);
      break;
    }
    if(poll_result == 0)
      break;

    processed++;
    switch(transport_event.type)
    {
      case LINK_TRANSPORT_EVENT_CONNECTED:
        events |= link_session_handle_connected(session);
        break;

      case LINK_TRANSPORT_EVENT_PACKET:
        events |= link_session_handle_packet(session, transport_event.data,
         transport_event.size);
        break;

      case LINK_TRANSPORT_EVENT_CLOSED:
        events |= link_session_fail(session, LINK_REASON_PEER_CLOSED, 0, 0);
        break;

      case LINK_TRANSPORT_EVENT_ERROR:
        events |= link_session_fail(session, LINK_REASON_TRANSPORT,
         (uint16_t)transport_event.error, 0);
        break;
    }
  }
  while(processed < 64 && session->state != LINK_SESSION_ERROR &&
        session->state != LINK_SESSION_DISCONNECTED);

  if(!link_session_state_uses_connection(session->state))
    return events;

  now = link_session_now(session);
  if(session->state == LINK_SESSION_NEGOTIATING ||
     session->state == LINK_SESSION_WAIT_READY ||
     session->state == LINK_SESSION_WAIT_READY_ACK)
  {
    if(now - session->state_started_us >= LINK_SESSION_TIMEOUT_US)
      events |= link_session_fail(session, LINK_REASON_TIMEOUT, 0, 1);
    return events;
  }

  if(session->barrier_active &&
     now - session->barrier_started_us >= LINK_SESSION_TIMEOUT_US)
  {
    events |= link_session_fail(session, LINK_REASON_TIMEOUT, 0, 1);
    return events;
  }

  if(now - session->last_rx_us >= LINK_SESSION_TIMEOUT_US)
  {
    events |= link_session_fail(session, LINK_REASON_TIMEOUT, 0, 1);
    return events;
  }

  if(now - session->last_tx_us >= LINK_SESSION_HEARTBEAT_US)
    events |= link_session_send_heartbeat(session);
  return events;
}
