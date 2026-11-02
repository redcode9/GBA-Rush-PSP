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

#ifndef ADHOC_PEER_H
#define ADHOC_PEER_H

#include <stddef.h>
#include <stdint.h>

#define ADHOC_PEER_MAC_SIZE 6u

/* Picks the peer to talk to from the group's member list (peer_count MACs
   packed in peer_macs): preferred_mac if present, else the first member
   that is not this PSP. Returns 1 and fills selected_mac on success. Kept
   apart from psp_adhoc.c so it can be tested on the host. */
int adhoc_peer_select(const uint8_t *local_mac, const uint8_t *preferred_mac,
 const uint8_t *peer_macs, size_t peer_count, uint8_t *selected_mac);

#endif /* ADHOC_PEER_H */
