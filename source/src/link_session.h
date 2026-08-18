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

#ifndef LINK_SESSION_H
#define LINK_SESSION_H

#include <stdint.h>

#include "gba_sio.h"
#include "link_protocol.h"
#include "link_transport.h"

#define LINK_SESSION_REQUIRED_CAPABILITIES \
 (LINK_CAPABILITY_NORMAL_8 | LINK_CAPABILITY_NORMAL_32 | \
  LINK_CAPABILITY_MULTI | LINK_CAPABILITY_TWO_PLAYER)

#define LINK_SESSION_HEARTBEAT_US 1000000u
#define LINK_SESSION_TIMEOUT_US   5000000u

typedef enum
{
  LINK_ROLE_NONE,
  LINK_ROLE_HOST,
  LINK_ROLE_JOINER
} LinkRole;

typedef enum
{
  LINK_SESSION_IDLE,
  LINK_SESSION_HOSTING,
  LINK_SESSION_JOINING,
  LINK_SESSION_NEGOTIATING,
  LINK_SESSION_WAIT_READY,
  LINK_SESSION_WAIT_READY_ACK,
  LINK_SESSION_CONNECTED,
  LINK_SESSION_WAIT_PREPARE,
  LINK_SESSION_WAIT_ACCEPT,
  LINK_SESSION_WAIT_COMMIT,
  LINK_SESSION_DISCONNECTED,
  LINK_SESSION_ERROR
} LinkSessionState;

typedef enum
{
  LINK_REASON_NONE,
  LINK_REASON_USER,
  LINK_REASON_TIMEOUT,
  LINK_REASON_TRANSPORT,
  LINK_REASON_PROTOCOL,
  LINK_REASON_INCOMPATIBLE,
  LINK_REASON_MODE_MISMATCH,
  LINK_REASON_QUEUE_FULL,
  LINK_REASON_PEER_CLOSED,
  LINK_REASON_SUSPEND,
  LINK_REASON_ROM_CHANGE
} LinkSessionReason;

typedef enum
{
  LINK_SESSION_EVENT_NONE               = 0,
  LINK_SESSION_EVENT_STATE_CHANGE       = 1u << 0,
  LINK_SESSION_EVENT_BARRIER_BEGIN      = 1u << 1,
  LINK_SESSION_EVENT_BARRIER_END        = 1u << 2,
  LINK_SESSION_EVENT_SERIAL_IRQ         = 1u << 3,
  LINK_SESSION_EVENT_TRANSFER_COMMITTED = 1u << 4,
  LINK_SESSION_EVENT_TRANSFER_ABORTED   = 1u << 5,
  LINK_SESSION_EVENT_SESSION_CLOSED     = 1u << 6
} LinkSessionEvent;

typedef struct
{
  GbaSio *sio;
  LinkTransport transport;
  LinkHello local_hello;
  LinkHello peer_hello;
  LinkSioState peer_sio;
  LinkTransferData transfer;
  LinkRole role;
  LinkSessionState state;
  LinkSessionReason reason;
  uint16_t reason_detail;
  uint32_t session_id;
  uint32_t negotiated_capabilities;
  uint32_t last_sequence;
  uint32_t active_sequence;
  uint64_t state_started_us;
  uint64_t barrier_started_us;
  uint64_t last_rx_us;
  uint64_t last_tx_us;
  uint8_t peer_minor;
  uint8_t peer_sio_valid;
  uint8_t transport_connected;
  uint8_t barrier_active;
} LinkSession;

void link_session_init(LinkSession *session, GbaSio *sio,
 LinkTransport transport, const LinkHello *hello);

int link_session_host(LinkSession *session, uint32_t session_id);
int link_session_join(LinkSession *session);
uint32_t link_session_disconnect(LinkSession *session,
 LinkSessionReason reason);

uint32_t link_session_pump(LinkSession *session);
uint32_t link_session_begin_transfer(LinkSession *session);
uint32_t link_session_publish_sio_state(LinkSession *session,
 const LinkSioState *state);

LinkSessionState link_session_state(const LinkSession *session);
int link_session_is_connected(const LinkSession *session);
int link_session_in_barrier(const LinkSession *session);

#endif /* LINK_SESSION_H */
