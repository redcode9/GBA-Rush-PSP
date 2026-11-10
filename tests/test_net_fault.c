/*
 * Host tests for net_fault.c, the simulated Wi-Fi network of the link
 * tests: loss and duplicate rates, delay and reordering, determinism of
 * the seed, a full queue, blackouts, air time and the named profiles. A
 * pinned vector holds the exact arrivals of the network model the link
 * tests used before this module, so every seeded run stays the same.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_net.h"
#include "net_fault.h"

#define SENDS 100000u
#define CAPACITY 256u

static NetFaultFlight storage[CAPACITY];
static NetFault fault;

typedef struct
{
  uint32_t count;
  uint32_t order[256];
  uint64_t arrival[256];
  uint64_t hash;
} Received;

/* Records each delivery: the sender's index sits in the first two bytes. */
static void receive(void *context, const NetFaultFlight *flight)
{
  Received *received = context;
  uint32_t index = flight->data[0] | (uint32_t)flight->data[1] << 8;

  if(received->count < 256u)
  {
    received->order[received->count] = index;
    received->arrival[received->count] = flight->arrival;
  }
  received->count++;
  received->hash = (received->hash ^ index ^ flight->arrival ^
   (uint64_t)flight->to << 40 ^ (uint64_t)flight->size << 48) *
   0x100000001B3ull;
}

static void send_indexed(int to, uint32_t index, size_t size, uint64_t now)
{
  uint8_t data[NET_FAULT_MAX_DATAGRAM];

  memset(data, 0, size);
  data[0] = (uint8_t)index;
  data[1] = (uint8_t)(index >> 8);
  net_fault_send(&fault, to, data, size, now);
}

static void test_profiles(void)
{
  static const char *const names[NET_FAULT_PROFILE_COUNT] =
   { "lan", "adhoc", "noisy", "outage", "terrible", "dropouts" };
  uint32_t i;

  assert(NET_FAULT_MAX_DATAGRAM == LINK_NET_MAX_DATAGRAM);
  for(i = 0; i < NET_FAULT_PROFILE_COUNT; i++)
  {
    assert(net_fault_profile_find(names[i]) == &net_fault_profiles[i]);
    assert(net_fault_profiles[i].airtime_ns_per_byte == 0u);
  }
  assert(net_fault_profile_find("Lan") == NULL);
  assert(net_fault_profile_find("") == NULL);
  assert(net_fault_profile_find(NULL) == NULL);
  assert(net_fault_profiles[5].outage_end_us == 250000u);
  assert(net_fault_profiles[5].outage_period_us == 1300000u);
}

/* Loss and duplicates match each profile's percentages within one point
   over SENDS datagrams, and every copy that is not lost arrives. */
static void test_rates(void)
{
  uint32_t i;

  for(i = 0; i < NET_FAULT_PROFILE_COUNT; i++)
  {
    NetFaultProfile profile = net_fault_profiles[i];
    Received received;
    uint32_t n;
    uint32_t kept;

    profile.outage_start_us = profile.outage_end_us = 0u;
    memset(&received, 0, sizeof(received));
    net_fault_init(&fault, &profile, storage, CAPACITY, 0xACE1u + i);
    for(n = 0; n < SENDS; n++)
    {
      send_indexed(0, n, 8u, n);
      net_fault_deliver(&fault, n + 1000000u, receive, &received);
    }
    kept = fault.sent - fault.lost;
    assert(fault.sent == SENDS && fault.blackout == 0u &&
     fault.overflow == 0u && fault.count == 0u);
    assert(fault.lost * 100u <= profile.loss_percent * SENDS + SENDS);
    assert(fault.lost * 100u + SENDS >= profile.loss_percent * SENDS);
    assert(fault.duplicated * 100u <= profile.duplicate_percent * kept + kept);
    assert(fault.duplicated * 100u + kept >= profile.duplicate_percent * kept);
    assert(received.count == kept + fault.duplicated);
  }
}

/* Every arrival falls in [latency, latency + jitter) after its send, the
   range is used from end to end, and the jitter reorders datagrams. */
static void test_delay_and_reordering(void)
{
  static const NetFaultProfile profile =
   { "delay", 3000u, 4000u, 0u, 0u, 0u, 0u, 0u, 0u };
  Received received;
  uint64_t now;
  uint64_t shortest = UINT64_MAX;
  uint64_t longest = 0;
  uint32_t inversions = 0;
  uint32_t i;

  memset(&received, 0, sizeof(received));
  net_fault_init(&fault, &profile, storage, CAPACITY, 99u);
  for(now = 100u; now <= 25600u + 8000u; now += 100u)
  {
    net_fault_deliver(&fault, now, receive, &received);
    if(now <= 25600u)
      send_indexed(1, (uint32_t)(now / 100u - 1u), 32u, now);
  }
  assert(received.count == 256u && fault.count == 0u);
  for(i = 0; i < 256u; i++)
  {
    uint64_t delay = received.arrival[i] - (received.order[i] + 1u) * 100u;

    assert(delay >= 3000u && delay < 7000u);
    shortest = delay < shortest ? delay : shortest;
    longest = delay > longest ? delay : longest;
    if(i != 0 && received.order[i] < received.order[i - 1])
      inversions++;
  }
  assert(shortest < 3200u && longest >= 6800u);
  assert(inversions >= 10u);
}

/* The same seed gives the same deliveries; another seed does not. */
static uint64_t run_seed(uint32_t seed)
{
  Received received;
  uint64_t now;
  uint32_t index = 0;

  memset(&received, 0, sizeof(received));
  net_fault_init(&fault, &net_fault_profiles[2], storage, CAPACITY, seed);
  for(now = 0; now < 2000000u; now += 100u)
  {
    net_fault_deliver(&fault, now, receive, &received);
    if(now % 300u == 0)
    {
      send_indexed((int)(index & 1u), index, 16u + index % 900u, now);
      index++;
    }
  }
  assert(received.count > 5000u);
  return received.hash ^ fault.lost ^ (uint64_t)fault.duplicated << 32;
}

static void test_seed_determinism(void)
{
  assert(run_seed(4) == run_seed(4));
  assert(run_seed(4) != run_seed(9));
  assert(run_seed(21) != run_seed(9));
}

/* The generator of the former model, stepped calls times from state. */
static uint32_t old_model_state(uint32_t state, uint32_t calls)
{
  for(; calls != 0; calls--)
    state = state * 1664525u + 1013904223u;
  return state;
}

/* A full queue refuses copies and counts them, writing nothing past its
   capacity; an oversized datagram is refused the same way. As in the
   former model, a refused copy draws no jitter: each send draws loss and
   duplicate, then one jitter per queued copy. */
static void test_queue_full(void)
{
  static const NetFaultProfile twice =
   { "twice", 100u, 1000u, 0u, 100u, 0u, 0u, 0u, 0u };
  static NetFaultFlight guarded[5];
  uint8_t data[NET_FAULT_MAX_DATAGRAM + 1u];
  uint32_t i;

  memset(guarded, 0xA5, sizeof(guarded));
  memset(data, 0x11, sizeof(data));
  net_fault_init(&fault, &twice, guarded, 4u, 1u);
  for(i = 0; i < 3u; i++)
    net_fault_send(&fault, 0, data, NET_FAULT_MAX_DATAGRAM, i);
  assert(fault.count == 4u && fault.duplicated == 3u && fault.overflow == 2u);
  assert(fault.rng_state == old_model_state(1u, 3u * 2u + 4u));
  net_fault_send(&fault, 0, data, NET_FAULT_MAX_DATAGRAM + 1u, 3u);
  assert(fault.sent == 4u && fault.overflow == 3u && fault.count == 4u);
  assert(fault.rng_state == old_model_state(1u, 3u * 2u + 4u));
  for(i = 0; i < sizeof(guarded[4]); i++)
    assert(((const uint8_t *)&guarded[4])[i] == 0xA5u);
  /* The second copy of the send at 1 us drew the eighth number. */
  assert(guarded[3].size == NET_FAULT_MAX_DATAGRAM && guarded[3].arrival ==
   1u + 100u + (old_model_state(1u, 8u) >> 8) % 1000u);
}

/* Nothing sent inside a blackout arrives, and the blackout uses no
   random numbers, or every later choice would shift. */
static void test_blackouts(void)
{
  static const NetFaultProfile window =
   { "window", 1000u, 0u, 0u, 0u, 2000u, 5000u, 0u, 0u };
  static const NetFaultProfile periodic =
   { "periodic", 1000u, 0u, 0u, 0u, 0u, 2500u, 10000u, 0u };
  Received received;
  uint64_t t;
  uint32_t dropped = 0;

  memset(&received, 0, sizeof(received));
  net_fault_init(&fault, &window, storage, CAPACITY, 7u);
  net_fault_restart(&fault, 50000u);
  for(t = 0; t < 10000u; t += 500u)
  {
    uint32_t before = fault.rng_state;
    uint32_t count = received.count;

    send_indexed(0, (uint32_t)t, 4u, 50000u + t);
    net_fault_deliver(&fault, 50000u + t + 1000u, receive, &received);
    if(t >= 2000u && t < 5000u)
    {
      assert(fault.rng_state == before && received.count == count);
      dropped++;
    }
    else
      assert(received.count == count + 1u);
  }
  assert(dropped == 6u && fault.blackout == dropped);

  dropped = 0;
  net_fault_init(&fault, &periodic, storage, CAPACITY, 7u);
  net_fault_restart(&fault, 1000u);
  for(t = 0; t < 35000u; t += 500u)
  {
    uint32_t count = received.count;

    send_indexed(0, (uint32_t)t, 4u, 1000u + t);
    net_fault_deliver(&fault, 1000u + t + 1000u, receive, &received);
    if(t % 10000u < 2500u)
    {
      assert(received.count == count);
      dropped++;
    }
    else
      assert(received.count == count + 1u);
  }
  assert(dropped == 20u && fault.blackout == dropped);
}

/* Each byte adds airtime_ns_per_byte to the arrival. */
static void test_airtime(void)
{
  static const NetFaultProfile radio =
   { "radio", 100u, 0u, 0u, 0u, 0u, 0u, 0u, 1900u };

  net_fault_init(&fault, &radio, storage, CAPACITY, 3u);
  send_indexed(0, 0, 16u, 0u);
  send_indexed(0, 1, 1024u, 0u);
  send_indexed(0, 2, 1u, 500u);
  assert(fault.flights[0].arrival == 100u + 30u);
  assert(fault.flights[1].arrival == 100u + 1945u);
  assert(fault.flights[2].arrival == 500u + 100u + 1u);
}

/* Literal arrivals and delivery order of the former model of the link
   tests with the terrible profile and 1.9 us of air time per byte. */
static void test_pinned_vector(void)
{
  static const uint32_t queued[9][2] =
  {
    { 1, 39946 }, { 2, 41453 }, { 3, 50351 }, { 4, 42716 }, { 5, 50010 },
    { 6, 37292 }, { 9, 66672 }, { 10, 41043 }, { 10, 68441 }
  };
  static const uint32_t delivered[9][2] =
  {
    { 1, 39946 }, { 6, 37292 }, { 10, 68441 }, { 9, 66672 }, { 10, 41043 },
    { 5, 50010 }, { 4, 42716 }, { 3, 50351 }, { 2, 41453 }
  };
  NetFaultProfile profile = net_fault_profiles[4];
  Received received;
  uint32_t i;

  profile.airtime_ns_per_byte = 1900u;
  net_fault_init(&fault, &profile, storage, CAPACITY, 0x12345678u);
  for(i = 0; i < 12u; i++)
    send_indexed((int)(i & 1u), i, 16u + 8u * i, 1000u * i);
  assert(fault.lost == 4u && fault.duplicated == 1u && fault.count == 9u);
  for(i = 0; i < 9u; i++)
  {
    assert(fault.flights[i].data[0] == queued[i][0]);
    assert(fault.flights[i].arrival == queued[i][1]);
  }
  memset(&received, 0, sizeof(received));
  net_fault_deliver(&fault, 40000u, receive, &received);
  assert(received.count == 2u);
  net_fault_deliver(&fault, 100000u, receive, &received);
  assert(received.count == 9u && fault.count == 0u);
  for(i = 0; i < 9u; i++)
    assert(received.order[i] == delivered[i][0] &&
     received.arrival[i] == delivered[i][1]);
  assert(fault.rng_state == 0x1E7FEAB3u);
}

/* The earliest arrival of the queue, wherever it sits, and nothing on an
   empty one; the queue and the generator stay as they were. */
static void test_next_arrival(void)
{
  static const NetFaultProfile late =
   { "late", 100u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
  static NetFaultFlight before[4];
  uint64_t arrival = 77u;
  uint32_t state;

  Received received;

  memset(&received, 0, sizeof(received));
  net_fault_init(&fault, &late, storage, CAPACITY, 5u);
  assert(net_fault_next_arrival(&fault, &arrival) == 0 && arrival == 77u);
  /* Arrivals 1000, 300, 700 and 400, in this order in the queue. */
  send_indexed(0, 0, 4u, 900u);
  send_indexed(0, 1, 4u, 200u);
  send_indexed(0, 2, 4u, 600u);
  send_indexed(0, 3, 4u, 300u);
  state = fault.rng_state;
  memcpy(before, storage, sizeof(before));
  assert(net_fault_next_arrival(&fault, &arrival) == 1 && arrival == 300u);
  assert(fault.count == 4u && fault.rng_state == state && fault.sent == 4u);
  assert(memcmp(before, storage, sizeof(before)) == 0);

  /* Each delivery moves the last flight into the freed slot, so the
     earliest ends up in the middle, then in the last slot. */
  net_fault_deliver(&fault, 300u, receive, &received);
  assert(net_fault_next_arrival(&fault, &arrival) == 1 && arrival == 400u);
  net_fault_deliver(&fault, 400u, receive, &received);
  assert(fault.count == 2u && storage[1].arrival == 700u);
  assert(net_fault_next_arrival(&fault, &arrival) == 1 && arrival == 700u);
  net_fault_restart(&fault, 0u);
  assert(net_fault_next_arrival(&fault, &arrival) == 0 && arrival == 700u);
  assert(received.count == 2u && fault.rng_state == state);
}

int main(void)
{
  test_profiles();
  test_rates();
  test_delay_and_reordering();
  test_seed_determinism();
  test_queue_full();
  test_blackouts();
  test_airtime();
  test_pinned_vector();
  test_next_arrival();
  puts("Net fault tests passed");
  return 0;
}
