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

#include <string.h>

#include "adhoc_peer.h"

int adhoc_peer_select(const uint8_t *local_mac, const uint8_t *preferred_mac,
 const uint8_t *peer_macs, size_t peer_count, uint8_t *selected_mac)
{
  const uint8_t *candidate;
  size_t index;

  if(local_mac == NULL || peer_macs == NULL || selected_mac == NULL)
    return 0;

  if(preferred_mac != NULL)
  {
    for(index = 0; index < peer_count; index++)
    {
      candidate = peer_macs + index * ADHOC_PEER_MAC_SIZE;
      if(memcmp(candidate, local_mac, ADHOC_PEER_MAC_SIZE) != 0 &&
         memcmp(candidate, preferred_mac, ADHOC_PEER_MAC_SIZE) == 0)
      {
        memcpy(selected_mac, candidate, ADHOC_PEER_MAC_SIZE);
        return 1;
      }
    }
  }

  for(index = 0; index < peer_count; index++)
  {
    candidate = peer_macs + index * ADHOC_PEER_MAC_SIZE;
    if(memcmp(candidate, local_mac, ADHOC_PEER_MAC_SIZE) != 0)
    {
      memcpy(selected_mac, candidate, ADHOC_PEER_MAC_SIZE);
      return 1;
    }
  }

  return 0;
}
