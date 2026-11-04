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

#include "net_fault.h"

#include <string.h>

/* Latency, jitter (us), loss and duplicate (%), outage window and period
   (us), air time (ns per byte). */
const NetFaultProfile net_fault_profiles[NET_FAULT_PROFILE_COUNT] =
{
  { "lan",       1500u,  1000u,  0u,  0u, 0u, 0u, 0u, 0u },
  { "adhoc",     3000u,  4000u,  3u,  1u, 0u, 0u, 0u, 0u },
  { "noisy",     8000u, 15000u, 15u,  5u, 0u, 0u, 0u, 0u },
  { "outage",    3000u,  4000u,  3u,  1u, 3000000u, 3400000u, 0u, 0u },
  { "terrible", 20000u, 40000u, 30u, 10u, 0u, 0u, 0u, 0u },
  /* Radio dropouts of a quarter second, after which a burst of frames
     arrives at once. */
  { "dropouts",  3000u,  4000u,  3u,  1u, 0u, 250000u, 1300000u, 0u }
};

const NetFaultProfile *net_fault_profile_find(const char *name)
{
  uint32_t i;

  if(name == NULL)
    return NULL;
  for(i = 0; i < NET_FAULT_PROFILE_COUNT; i++)
    if(strcmp(net_fault_profiles[i].name, name) == 0)
      return &net_fault_profiles[i];
  return NULL;
}

void net_fault_init(NetFault *fault, const NetFaultProfile *profile,
 NetFaultFlight *storage, uint32_t capacity, uint32_t seed)
{
  memset(fault, 0, sizeof(*fault));
  fault->profile = profile;
  fault->flights = storage;
  fault->capacity = capacity;
  fault->rng_state = seed;
}

void net_fault_restart(NetFault *fault, uint64_t epoch_us)
{
  fault->count = 0;
  fault->epoch_us = epoch_us;
}

/* The low bits of this generator are weak, so drop them. */
uint32_t net_fault_random(NetFault *fault)
{
  fault->rng_state = fault->rng_state * 1664525u + 1013904223u;
  return fault->rng_state >> 8;
}

void net_fault_send(NetFault *fault, int to, const uint8_t *data,
 size_t size, uint64_t now_us)
{
  const NetFaultProfile *profile = fault->profile;
  uint64_t since = now_us - fault->epoch_us;
  unsigned int copies = 1;

  fault->sent++;
  if(size > NET_FAULT_MAX_DATAGRAM)
  {
    fault->overflow++;
    return;
  }
  if(profile->outage_period_us != 0)
    since %= profile->outage_period_us;
  if(since >= profile->outage_start_us && since < profile->outage_end_us)
  {
    fault->blackout++;
    return;
  }
  /* Every seeded run depends on this order of the generator's calls. */
  if(net_fault_random(fault) % 100u < profile->loss_percent)
  {
    fault->lost++;
    return;
  }
  if(net_fault_random(fault) % 100u < profile->duplicate_percent)
  {
    fault->duplicated++;
    copies = 2;
  }
  for(; copies != 0; copies--)
  {
    NetFaultFlight *flight;

    if(fault->count == fault->capacity)
    {
      fault->overflow++;
      continue;
    }
    flight = &fault->flights[fault->count++];
    memcpy(flight->data, data, size);
    flight->size = size;
    flight->to = to;
    /* Random jitter on each copy also reorders datagrams. */
    flight->arrival = now_us + profile->latency_us +
     (uint64_t)size * profile->airtime_ns_per_byte / 1000u +
     (profile->jitter_us != 0 ?
      net_fault_random(fault) % profile->jitter_us : 0u);
  }
}

/* The swap with the last flight sets the delivery order of datagrams due
   at the same time, so seeded runs depend on it too. */
void net_fault_deliver(NetFault *fault, uint64_t now_us,
 void (*deliver)(void *context, const NetFaultFlight *flight),
 void *context)
{
  uint32_t i = 0;

  while(i < fault->count)
  {
    if(fault->flights[i].arrival <= now_us)
    {
      deliver(context, &fault->flights[i]);
      fault->flights[i] = fault->flights[--fault->count];
    }
    else
      i++;
  }
}
