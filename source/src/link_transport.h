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
  LINK_TRANSPORT_EVENT_CLOSED,
  LINK_TRANSPORT_EVENT_ERROR
} LinkTransportEventType;

typedef struct
{
  LinkTransportEventType type;
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
  void (*disconnect)(void *context);
  uint64_t (*now_us)(void *context);
} LinkTransportOps;

typedef struct
{
  const LinkTransportOps *ops;
  void *context;
} LinkTransport;

#endif /* LINK_TRANSPORT_H */
