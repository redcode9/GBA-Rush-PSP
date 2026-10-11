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

#ifndef LINK_TRANSPORT_H
#define LINK_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "link_protocol.h"

typedef enum
{
  LINK_TRANSPORT_EVENT_CONNECTED,
  LINK_TRANSPORT_EVENT_PACKET,
  LINK_TRANSPORT_EVENT_ACTIVITY,
  LINK_TRANSPORT_EVENT_RECOVERING,
  LINK_TRANSPORT_EVENT_RECOVERED,
  LINK_TRANSPORT_EVENT_CLOSED,
  LINK_TRANSPORT_EVENT_ERROR,
  LINK_TRANSPORT_EVENT_DEGRADED,
  LINK_TRANSPORT_EVENT_HEALTHY
} LinkTransportEventType;

typedef enum
{
  LINK_TRANSPORT_HEALTH_HEALTHY,
  LINK_TRANSPORT_HEALTH_PRESSURED,
  LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED,
  LINK_TRANSPORT_HEALTH_RECOVERING,
  LINK_TRANSPORT_HEALTH_FAILED
} LinkTransportHealth;

typedef enum
{
  LINK_TIMEOUT_NONE,
  LINK_TIMEOUT_RECEIVE_SILENCE,
  LINK_TIMEOUT_ACTIVE_TRANSFER,
  LINK_TIMEOUT_AHEAD_PACING,
  LINK_TIMEOUT_PRESSURE,
  LINK_TIMEOUT_RECOVERY_RENDEZVOUS,
  LINK_TIMEOUT_SHUTDOWN,
  LINK_TIMEOUT_NEGOTIATION,
  LINK_TIMEOUT_ABORT_ACK,
  LINK_TIMEOUT_CONTROL,
  LINK_TIMEOUT_PEER_DISCOVERY,
  LINK_TIMEOUT_CONNECT_HANDSHAKE
} LinkTimeoutSource;

typedef struct
{
  LinkTransportEventType type;
  uint32_t generation;
  int error;
  uint16_t size;
  uint8_t data[LINK_PROTOCOL_MAX_PACKET];
} LinkTransportEvent;

typedef struct
{
  int (*host)(void *context);
  int (*join)(void *context);
  int (*send)(void *context, const uint8_t *data, size_t size);
  int (*poll)(void *context, LinkTransportEvent *event);
  int (*request_recovery)(void *context, int reason,
   LinkTimeoutSource timeout_source);
  void (*disconnect)(void *context);
  uint64_t (*now_us)(void *context);
} LinkTransportOps;

typedef struct
{
  const LinkTransportOps *ops;
  void *context;
} LinkTransport;

#endif /* LINK_TRANSPORT_H */
