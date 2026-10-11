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

#include "link_tx_queue.h"

#include <limits.h>
#include <string.h>

static void link_tx_increment(unsigned int *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

static int link_tx_item_set(LinkTxItem *item, const uint8_t *data,
 size_t size)
{
  if(item == NULL || data == NULL || size == 0 ||
     size > sizeof(item->data))
    return 0;
  item->size = (uint16_t)size;
  memcpy(item->data, data, size);
  return 1;
}

static int link_tx_critical_contains(const LinkTxQueue *queue,
 const uint8_t *data, size_t size)
{
  unsigned int offset;
  unsigned int slot;

  for(offset = 0; offset < queue->critical_count; offset++)
  {
    slot = (queue->critical_read + offset) % LINK_TX_CRITICAL_CAPACITY;
    if(queue->critical[slot].size == size &&
       memcmp(queue->critical[slot].data, data, size) == 0)
      return 1;
  }
  return 0;
}

void link_tx_queue_init(LinkTxQueue *queue)
{
  if(queue != NULL)
    memset(queue, 0, sizeof(*queue));
}

int link_tx_queue_push(LinkTxQueue *queue, LinkTxPriority priority,
 const uint8_t *data, size_t size)
{
  unsigned int slot;

  if(queue == NULL || priority < LINK_TX_CRITICAL ||
     priority > LINK_TX_CLOCK || data == NULL || size == 0 ||
     size > LINK_PROTOCOL_MAX_PACKET)
    return 0;

  if(priority == LINK_TX_CRITICAL)
  {
    if(link_tx_critical_contains(queue, data, size))
    {
      link_tx_increment(&queue->coalesced);
      return 1;
    }
    if(queue->critical_count >= LINK_TX_CRITICAL_CAPACITY)
    {
      link_tx_increment(&queue->rejected);
      return 0;
    }
    if(!link_tx_item_set(&queue->critical[queue->critical_write], data,
       size))
      return 0;
    queue->critical_write = (queue->critical_write + 1u) %
     LINK_TX_CRITICAL_CAPACITY;
    queue->critical_count++;
    return 1;
  }

  slot = (unsigned int)priority - 1u;
  if(queue->replaceable_valid[slot])
    link_tx_increment(&queue->coalesced);
  if(!link_tx_item_set(&queue->replaceable[slot], data, size))
    return 0;
  queue->replaceable_valid[slot] = 1;
  return 1;
}

int link_tx_queue_pop(LinkTxQueue *queue, LinkTxItem *item)
{
  unsigned int index;
  unsigned int slot;

  if(queue == NULL || item == NULL)
    return 0;
  if(queue->critical_count != 0)
  {
    *item = queue->critical[queue->critical_read];
    queue->critical_read = (queue->critical_read + 1u) %
     LINK_TX_CRITICAL_CAPACITY;
    queue->critical_count--;
    return 1;
  }

  for(index = 0; index < LINK_TX_REPLACEABLE_COUNT; index++)
  {
    slot = (queue->replaceable_next + index) %
     LINK_TX_REPLACEABLE_COUNT;
    if(!queue->replaceable_valid[slot])
      continue;
    *item = queue->replaceable[slot];
    queue->replaceable_valid[slot] = 0;
    queue->replaceable_next = (slot + 1u) % LINK_TX_REPLACEABLE_COUNT;
    return 1;
  }
  return 0;
}

int link_tx_queue_pop_ready(LinkTxQueue *queue,
 unsigned int reliable_outstanding, LinkTxItem *item)
{
  if(queue == NULL || item == NULL)
    return 0;
  if(reliable_outstanding != 0)
    return 0;
  return link_tx_queue_pop(queue, item);
}

unsigned int link_tx_queue_depth(const LinkTxQueue *queue)
{
  unsigned int depth;
  unsigned int index;

  if(queue == NULL)
    return 0;
  depth = queue->critical_count;
  for(index = 0; index < LINK_TX_REPLACEABLE_COUNT; index++)
    depth += queue->replaceable_valid[index] != 0;
  return depth;
}
