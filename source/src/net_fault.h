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

/*
 * A simulated Wi-Fi network between two consoles: a fixed-capacity queue
 * of datagrams in flight with latency, jitter (which reorders), loss,
 * duplicates, a blackout window that may repeat, and air time per byte.
 *
 * Every random choice comes from one seeded generator, called in a fixed
 * order (loss, duplicate, then the jitter of each copy), so a seed always
 * gives the same run. The caller owns the queue's storage: nothing is
 * allocated, and there are no platform calls.
 */

#ifndef NET_FAULT_H
#define NET_FAULT_H

#include <stddef.h>
#include <stdint.h>

/* Equal to LINK_NET_MAX_DATAGRAM; the tests check it. */
#define NET_FAULT_MAX_DATAGRAM 1024u
#define NET_FAULT_PROFILE_COUNT   6u

typedef struct
{
  const char *name;
  uint32_t latency_us;
  uint32_t jitter_us;          /* extra delay of each copy: [0, jitter) */
  uint32_t loss_percent;
  uint32_t duplicate_percent;
  /* Window, from the epoch, in which every datagram is lost, and the
     period it repeats with (0 for once). */
  uint64_t outage_start_us;
  uint64_t outage_end_us;
  uint64_t outage_period_us;
  uint32_t airtime_ns_per_byte;
} NetFaultProfile;

/* A datagram on its way to console `to`. */
typedef struct
{
  uint8_t data[NET_FAULT_MAX_DATAGRAM];
  size_t size;
  uint64_t arrival;
  int to;
} NetFaultFlight;

typedef struct
{
  const NetFaultProfile *profile;
  NetFaultFlight *flights;
  uint32_t capacity;
  uint32_t count;
  uint32_t rng_state;
  uint64_t epoch_us;     /* outage windows count from here */

  uint32_t sent;         /* datagrams given to net_fault_send */
  uint32_t lost;         /* dropped by loss_percent */
  uint32_t blackout;     /* dropped inside an outage window */
  uint32_t duplicated;   /* sent twice */
  uint32_t overflow;     /* copies refused: queue full or datagram too big */
} NetFault;

/* lan, adhoc, noisy, outage, terrible and dropouts, in this order. Their
   air time is 0: a real PSP's radio adds its own. */
extern const NetFaultProfile net_fault_profiles[NET_FAULT_PROFILE_COUNT];

/* The named profile, or NULL. */
const NetFaultProfile *net_fault_profile_find(const char *name);

void net_fault_init(NetFault *fault, const NetFaultProfile *profile,
 NetFaultFlight *storage, uint32_t capacity, uint32_t seed);
/* Empties the queue and moves the epoch; the generator carries on. */
void net_fault_restart(NetFault *fault, uint64_t epoch_us);
/* The next value of the generator, a linear congruential one. */
uint32_t net_fault_random(NetFault *fault);
void net_fault_send(NetFault *fault, int to, const uint8_t *data,
 size_t size, uint64_t now_us);
/* Calls deliver() for every datagram whose arrival time has come, and
   removes it from the queue. */
void net_fault_deliver(NetFault *fault, uint64_t now_us,
 void (*deliver)(void *context, const NetFaultFlight *flight),
 void *context);

#endif
