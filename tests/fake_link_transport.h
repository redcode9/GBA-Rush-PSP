#ifndef FAKE_LINK_TRANSPORT_H
#define FAKE_LINK_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "link_transport.h"

#define FAKE_LINK_QUEUE_CAPACITY 64u

typedef struct FakeLinkNetwork FakeLinkNetwork;

typedef struct
{
  FakeLinkNetwork *network;
  uint8_t index;
} FakeLinkEndpoint;

typedef struct
{
  LinkTransportEvent event;
  uint64_t deliver_at;
} FakeLinkQueuedEvent;

typedef struct
{
  FakeLinkQueuedEvent events[FAKE_LINK_QUEUE_CAPACITY];
  uint32_t head;
  uint32_t count;
} FakeLinkEventQueue;

struct FakeLinkNetwork
{
  FakeLinkEndpoint endpoints[2];
  FakeLinkEventQueue queues[2];
  uint64_t now_us;
  uint64_t latency_us[2];
  uint8_t host_requested[2];
  uint8_t join_requested[2];
  uint8_t connected;
  uint8_t drop_packets;
};

void fake_link_network_init(FakeLinkNetwork *network,
 LinkTransport *first, LinkTransport *second);
void fake_link_network_advance(FakeLinkNetwork *network, uint64_t microseconds);
void fake_link_network_set_latency(FakeLinkNetwork *network,
 uint8_t endpoint, uint64_t microseconds);
void fake_link_network_set_drop(FakeLinkNetwork *network, int drop);
int fake_link_network_inject(FakeLinkNetwork *network, uint8_t endpoint,
 const uint8_t *data, size_t size, uint64_t delay);
void fake_link_network_close(FakeLinkNetwork *network, int error);

#endif /* FAKE_LINK_TRANSPORT_H */
