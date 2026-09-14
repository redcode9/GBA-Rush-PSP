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

#ifndef LINK_TX_QUEUE_H
#define LINK_TX_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "link_protocol.h"

#define LINK_TX_CRITICAL_CAPACITY 32u
#define LINK_TX_REPLACEABLE_COUNT 3u

typedef enum
{
  LINK_TX_CRITICAL,
  LINK_TX_SIO_STATE,
  LINK_TX_HEARTBEAT,
  LINK_TX_CLOCK
} LinkTxPriority;

typedef struct
{
  uint16_t size;
  uint8_t data[LINK_PROTOCOL_MAX_PACKET];
} LinkTxItem;

typedef struct
{
  LinkTxItem critical[LINK_TX_CRITICAL_CAPACITY];
  LinkTxItem replaceable[LINK_TX_REPLACEABLE_COUNT];
  uint8_t replaceable_valid[LINK_TX_REPLACEABLE_COUNT];
  unsigned int critical_read;
  unsigned int critical_write;
  unsigned int critical_count;
  unsigned int replaceable_next;
  unsigned int coalesced;
  unsigned int rejected;
} LinkTxQueue;

void link_tx_queue_init(LinkTxQueue *queue);
int link_tx_queue_push(LinkTxQueue *queue, LinkTxPriority priority,
 const uint8_t *data, size_t size);
int link_tx_queue_pop(LinkTxQueue *queue, LinkTxItem *item);
int link_tx_queue_pop_ready(LinkTxQueue *queue,
 unsigned int reliable_outstanding, LinkTxItem *item);
unsigned int link_tx_queue_depth(const LinkTxQueue *queue);

#endif /* LINK_TX_QUEUE_H */
