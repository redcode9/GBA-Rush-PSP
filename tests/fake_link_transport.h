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
  uint32_t generation;
  uint64_t now_us;
  uint64_t latency_us[2];
  uint8_t host_requested[2];
  uint8_t join_requested[2];
  uint8_t connected;
  uint8_t drop_packets;
  uint8_t duplicate_packets;
  uint8_t fail_sends;
  uint8_t reject_recovery_requests;
  uint8_t defer_recovery_events;
  uint8_t recovery_requested;
  uint8_t drop_next_type[2][LINK_MESSAGE_LAST + 1u];
  uint32_t disconnect_calls[2];
  uint32_t recovery_requests[2];
  int recovery_reasons[2];
  LinkTimeoutSource recovery_timeout_sources[2];
  uint32_t sent_packets[2];
  uint32_t sent_types[2][LINK_MESSAGE_LAST + 1u];
  uint8_t sent_flags[2][LINK_MESSAGE_LAST + 1u];
  uint32_t sent_sequences[2][LINK_MESSAGE_LAST + 1u];
};

void fake_link_network_init(FakeLinkNetwork *network,
 LinkTransport *first, LinkTransport *second);
void fake_link_network_advance(FakeLinkNetwork *network, uint64_t microseconds);
void fake_link_network_set_latency(FakeLinkNetwork *network,
 uint8_t endpoint, uint64_t microseconds);
void fake_link_network_set_drop(FakeLinkNetwork *network, int drop);
void fake_link_network_set_duplicates(FakeLinkNetwork *network,
 uint8_t duplicates);
void fake_link_network_set_send_failure(FakeLinkNetwork *network, int fail);
void fake_link_network_set_recovery_failure(FakeLinkNetwork *network,
 int fail);
void fake_link_network_set_recovery_deferred(FakeLinkNetwork *network,
 int deferred);
void fake_link_network_drop_next(FakeLinkNetwork *network,
 uint8_t endpoint, LinkMessageType type);
int fake_link_network_inject(FakeLinkNetwork *network, uint8_t endpoint,
 const uint8_t *data, size_t size, uint64_t delay);
int fake_link_network_activity(FakeLinkNetwork *network, uint8_t endpoint,
 uint64_t delay);
int fake_link_network_health(FakeLinkNetwork *network, uint8_t endpoint,
 int degraded, uint64_t delay);
int fake_link_network_health_generation(FakeLinkNetwork *network,
 uint8_t endpoint, int degraded, uint32_t generation, uint64_t delay);
int fake_link_network_recovery(FakeLinkNetwork *network, uint8_t endpoint,
 int recovering, uint64_t delay);
void fake_link_network_close(FakeLinkNetwork *network, int error);

#endif /* FAKE_LINK_TRANSPORT_H */
