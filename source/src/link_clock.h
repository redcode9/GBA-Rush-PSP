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

#ifndef LINK_CLOCK_H
#define LINK_CLOCK_H

#include <stdint.h>

/* Clock samples pace free-running hot peers and estimate drift. They never
 * authorize crossing an armed SIO barrier; LinkSession owns that horizon. */
#define LINK_CLOCK_SYNC_INTERVAL_CYCLES 0x00004000u
#define LINK_CLOCK_MAX_LEAD_CYCLES      0x00008000u
#define LINK_CLOCK_HOT_WINDOW_US        1000000u
#define LINK_CLOCK_PEER_STALE_US         500000u

typedef struct
{
  uint32_t generation;
  uint32_t local_cycle;
  uint32_t peer_cycle;
  uint32_t local_base;
  uint32_t peer_base;
  uint32_t last_published_cycle;
  uint64_t last_published_us;
  uint64_t last_peer_us;
  uint64_t hot_until_us;
  uint8_t peer_valid;
  uint8_t published_valid;
} LinkClock;

void link_clock_init(LinkClock *clock, uint32_t generation,
 uint32_t local_cycle);
void link_clock_set_local(LinkClock *clock, uint32_t cycle);
void link_clock_note_peer(LinkClock *clock, uint32_t generation,
 uint32_t peer_cycle, uint64_t now_us);
void link_clock_mark_hot(LinkClock *clock, uint64_t now_us);
int link_clock_should_publish(const LinkClock *clock);
void link_clock_note_published(LinkClock *clock);
int link_clock_should_publish_after(const LinkClock *clock, uint64_t now_us,
 uint64_t interval_us);
void link_clock_note_published_at(LinkClock *clock, uint64_t now_us);
int link_clock_should_throttle(const LinkClock *clock, uint64_t now_us);

#endif /* LINK_CLOCK_H */
