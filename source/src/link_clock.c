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

#include "link_clock.h"

#include <limits.h>
#include <string.h>

static uint64_t link_clock_add_time(uint64_t value, uint64_t amount)
{
  return UINT64_MAX - value < amount ? UINT64_MAX : value + amount;
}

void link_clock_init(LinkClock *clock, uint32_t generation,
 uint32_t local_cycle)
{
  if(clock == NULL)
    return;
  memset(clock, 0, sizeof(*clock));
  clock->generation = generation == 0 ? 1u : generation;
  clock->local_cycle = local_cycle;
  clock->local_base = local_cycle;
}

void link_clock_set_local(LinkClock *clock, uint32_t cycle)
{
  if(clock != NULL)
    clock->local_cycle = cycle;
}

void link_clock_note_peer(LinkClock *clock, uint32_t generation,
 uint32_t peer_cycle, uint64_t now_us)
{
  if(clock == NULL || generation == 0)
    return;
  if(!clock->peer_valid || generation != clock->generation)
  {
    clock->generation = generation;
    clock->local_base = clock->local_cycle;
    clock->peer_base = peer_cycle;
    clock->peer_valid = 1;
  }
  clock->peer_cycle = peer_cycle;
  clock->last_peer_us = now_us;
}

void link_clock_mark_hot(LinkClock *clock, uint64_t now_us)
{
  if(clock != NULL)
    clock->hot_until_us = link_clock_add_time(now_us,
     LINK_CLOCK_HOT_WINDOW_US);
}

int link_clock_should_publish(const LinkClock *clock)
{
  if(clock == NULL)
    return 0;
  return !clock->published_valid ||
   clock->local_cycle - clock->last_published_cycle >=
    LINK_CLOCK_SYNC_INTERVAL_CYCLES;
}

void link_clock_note_published(LinkClock *clock)
{
  if(clock == NULL)
    return;
  clock->last_published_cycle = clock->local_cycle;
  clock->published_valid = 1;
}

int link_clock_should_publish_after(const LinkClock *clock, uint64_t now_us,
 uint64_t interval_us)
{
  if(clock == NULL)
    return 0;
  return !clock->published_valid || now_us < clock->last_published_us ||
   now_us - clock->last_published_us >= interval_us;
}

void link_clock_note_published_at(LinkClock *clock, uint64_t now_us)
{
  if(clock == NULL)
    return;
  link_clock_note_published(clock);
  clock->last_published_us = now_us;
}

int link_clock_should_throttle(const LinkClock *clock, uint64_t now_us)
{
  uint32_t local_elapsed;
  uint32_t peer_elapsed;
  uint32_t lead;

  if(clock == NULL || !clock->peer_valid || now_us > clock->hot_until_us ||
     now_us < clock->last_peer_us ||
     now_us - clock->last_peer_us > LINK_CLOCK_PEER_STALE_US)
    return 0;
  local_elapsed = clock->local_cycle - clock->local_base;
  peer_elapsed = clock->peer_cycle - clock->peer_base;
  lead = local_elapsed - peer_elapsed;
  return lead < 0x80000000u && lead > LINK_CLOCK_MAX_LEAD_CYCLES;
}
