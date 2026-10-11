#include "fake_link_transport.h"

#include <string.h>

static int fake_link_queue_push(FakeLinkNetwork *network, uint8_t endpoint,
 const LinkTransportEvent *event, uint64_t delay)
{
  FakeLinkEventQueue *queue = &network->queues[endpoint];
  uint32_t tail;

  if(queue->count == FAKE_LINK_QUEUE_CAPACITY)
    return 0;

  tail = (queue->head + queue->count) % FAKE_LINK_QUEUE_CAPACITY;
  queue->events[tail].event = *event;
  queue->events[tail].deliver_at = network->now_us + delay;
  queue->count++;
  return 1;
}

static void fake_link_try_connect(FakeLinkNetwork *network)
{
  LinkTransportEvent event;
  int host = -1;
  int joiner = -1;
  uint32_t i;

  if(network->connected)
    return;

  for(i = 0; i < 2; i++)
  {
    if(network->host_requested[i])
      host = (int)i;
    if(network->join_requested[i])
      joiner = (int)i;
  }

  if(host < 0 || joiner < 0 || host == joiner)
    return;
  if(network->queues[host].count == FAKE_LINK_QUEUE_CAPACITY ||
     network->queues[joiner].count == FAKE_LINK_QUEUE_CAPACITY)
    return;

  memset(&event, 0, sizeof(event));
  network->generation++;
  if(network->generation == 0u)
    network->generation = 1u;
  event.type = LINK_TRANSPORT_EVENT_CONNECTED;
  event.generation = network->generation;
  if(!fake_link_queue_push(network, (uint8_t)host, &event, 0) ||
     !fake_link_queue_push(network, (uint8_t)joiner, &event, 0))
    return;
  network->connected = 1;
}

static int fake_link_host(void *context)
{
  FakeLinkEndpoint *endpoint = context;

  memset(&endpoint->network->queues[endpoint->index], 0,
   sizeof(endpoint->network->queues[endpoint->index]));
  endpoint->network->host_requested[endpoint->index] = 1;
  fake_link_try_connect(endpoint->network);
  return 1;
}

static int fake_link_join(void *context)
{
  FakeLinkEndpoint *endpoint = context;

  memset(&endpoint->network->queues[endpoint->index], 0,
   sizeof(endpoint->network->queues[endpoint->index]));
  endpoint->network->join_requested[endpoint->index] = 1;
  fake_link_try_connect(endpoint->network);
  return 1;
}

static int fake_link_send(void *context, const uint8_t *data, size_t size)
{
  FakeLinkEndpoint *endpoint = context;
  FakeLinkNetwork *network = endpoint->network;
  LinkTransportEvent event;
  uint8_t peer = endpoint->index ^ 1u;
  uint8_t copy;

  if(!network->connected || network->fail_sends ||
     size > LINK_PROTOCOL_MAX_PACKET)
    return 0;
  network->sent_packets[endpoint->index]++;
  if(size >= LINK_PROTOCOL_HEADER_SIZE &&
     data[6] <= LINK_MESSAGE_LAST)
  {
    network->sent_types[endpoint->index][data[6]]++;
    network->sent_flags[endpoint->index][data[6]] = data[7];
    network->sent_sequences[endpoint->index][data[6]] =
     ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) |
     ((uint32_t)data[18] << 8) | data[19];
  }
  if(size >= LINK_PROTOCOL_HEADER_SIZE &&
     data[6] <= LINK_MESSAGE_LAST &&
     network->drop_next_type[endpoint->index][data[6]] != 0)
  {
    network->drop_next_type[endpoint->index][data[6]]--;
    return 1;
  }
  if(network->drop_packets)
    return 1;

  memset(&event, 0, sizeof(event));
  event.type = LINK_TRANSPORT_EVENT_PACKET;
  event.generation = network->generation;
  event.size = (uint16_t)size;
  memcpy(event.data, data, size);
  for(copy = 0; copy <= network->duplicate_packets; copy++)
  {
    if(!fake_link_queue_push(network, peer, &event,
       network->latency_us[endpoint->index]))
      return 0;
  }
  return 1;
}

static int fake_link_poll(void *context, LinkTransportEvent *event)
{
  FakeLinkEndpoint *endpoint = context;
  FakeLinkEventQueue *queue = &endpoint->network->queues[endpoint->index];

  if(queue->count == 0 ||
     queue->events[queue->head].deliver_at > endpoint->network->now_us)
    return 0;

  *event = queue->events[queue->head].event;
  queue->head = (queue->head + 1) % FAKE_LINK_QUEUE_CAPACITY;
  queue->count--;
  return 1;
}

static int fake_link_request_recovery(void *context, int reason,
 LinkTimeoutSource timeout_source)
{
  FakeLinkEndpoint *endpoint = context;
  FakeLinkNetwork *network = endpoint->network;
  LinkTransportEvent event;

  if(!network->connected)
    return 0;
  network->recovery_requests[endpoint->index]++;
  network->recovery_reasons[endpoint->index] = reason;
  network->recovery_timeout_sources[endpoint->index] = timeout_source;
  if(network->reject_recovery_requests)
    return 0;
  if(network->recovery_requested)
    return 1;
  if(network->defer_recovery_events)
  {
    network->recovery_requested = 1;
    return 1;
  }
  if(network->queues[0].count == FAKE_LINK_QUEUE_CAPACITY ||
     network->queues[1].count == FAKE_LINK_QUEUE_CAPACITY)
    return 0;

  memset(&event, 0, sizeof(event));
  event.type = LINK_TRANSPORT_EVENT_RECOVERING;
  event.generation = network->generation;
  if(!fake_link_queue_push(network, 0, &event, 0) ||
     !fake_link_queue_push(network, 1, &event, 0))
    return 0;
  network->recovery_requested = 1;
  return 1;
}

static void fake_link_disconnect(void *context)
{
  FakeLinkEndpoint *endpoint = context;
  FakeLinkNetwork *network = endpoint->network;
  LinkTransportEvent event;
  uint8_t peer = endpoint->index ^ 1u;

  network->disconnect_calls[endpoint->index]++;
  if(network->connected)
  {
    memset(&event, 0, sizeof(event));
    event.type = LINK_TRANSPORT_EVENT_CLOSED;
    event.generation = network->generation;
    fake_link_queue_push(network, peer, &event, 0);
  }
  network->connected = 0;
  memset(network->host_requested, 0, sizeof(network->host_requested));
  memset(network->join_requested, 0, sizeof(network->join_requested));
}

static uint64_t fake_link_now(void *context)
{
  FakeLinkEndpoint *endpoint = context;
  return endpoint->network->now_us;
}

static const LinkTransportOps fake_link_ops =
{
  .host = fake_link_host,
  .join = fake_link_join,
  .send = fake_link_send,
  .poll = fake_link_poll,
  .request_recovery = fake_link_request_recovery,
  .disconnect = fake_link_disconnect,
  .now_us = fake_link_now
};

void fake_link_network_init(FakeLinkNetwork *network,
 LinkTransport *first, LinkTransport *second)
{
  memset(network, 0, sizeof(*network));
  network->endpoints[0].network = network;
  network->endpoints[0].index = 0;
  network->endpoints[1].network = network;
  network->endpoints[1].index = 1;

  first->ops = &fake_link_ops;
  first->context = &network->endpoints[0];
  second->ops = &fake_link_ops;
  second->context = &network->endpoints[1];
}

void fake_link_network_advance(FakeLinkNetwork *network, uint64_t microseconds)
{
  network->now_us += microseconds;
}

void fake_link_network_set_latency(FakeLinkNetwork *network,
 uint8_t endpoint, uint64_t microseconds)
{
  if(endpoint < 2)
    network->latency_us[endpoint] = microseconds;
}

void fake_link_network_set_drop(FakeLinkNetwork *network, int drop)
{
  network->drop_packets = drop != 0;
}

void fake_link_network_set_duplicates(FakeLinkNetwork *network,
 uint8_t duplicates)
{
  network->duplicate_packets = duplicates;
}

void fake_link_network_set_send_failure(FakeLinkNetwork *network, int fail)
{
  network->fail_sends = fail != 0;
}

void fake_link_network_set_recovery_failure(FakeLinkNetwork *network,
 int fail)
{
  network->reject_recovery_requests = fail != 0;
}

void fake_link_network_set_recovery_deferred(FakeLinkNetwork *network,
 int deferred)
{
  network->defer_recovery_events = deferred != 0;
}

void fake_link_network_drop_next(FakeLinkNetwork *network,
 uint8_t endpoint, LinkMessageType type)
{
  if(endpoint < 2 && type <= LINK_MESSAGE_LAST)
    network->drop_next_type[endpoint][type]++;
}

int fake_link_network_inject(FakeLinkNetwork *network, uint8_t endpoint,
 const uint8_t *data, size_t size, uint64_t delay)
{
  LinkTransportEvent event;

  if(endpoint >= 2 || size > LINK_PROTOCOL_MAX_PACKET)
    return 0;
  memset(&event, 0, sizeof(event));
  event.type = LINK_TRANSPORT_EVENT_PACKET;
  event.generation = network->generation;
  event.size = (uint16_t)size;
  memcpy(event.data, data, size);
  return fake_link_queue_push(network, endpoint, &event, delay);
}

int fake_link_network_activity(FakeLinkNetwork *network, uint8_t endpoint,
 uint64_t delay)
{
  LinkTransportEvent event;

  if(endpoint >= 2)
    return 0;
  memset(&event, 0, sizeof(event));
  event.type = LINK_TRANSPORT_EVENT_ACTIVITY;
  event.generation = network->generation;
  return fake_link_queue_push(network, endpoint, &event, delay);
}

int fake_link_network_health(FakeLinkNetwork *network, uint8_t endpoint,
 int degraded, uint64_t delay)
{
  return fake_link_network_health_generation(network, endpoint, degraded,
   network->generation, delay);
}

int fake_link_network_health_generation(FakeLinkNetwork *network,
 uint8_t endpoint, int degraded, uint32_t generation, uint64_t delay)
{
  LinkTransportEvent event;

  if(endpoint >= 2)
    return 0;
  memset(&event, 0, sizeof(event));
  event.type = degraded ? LINK_TRANSPORT_EVENT_DEGRADED :
   LINK_TRANSPORT_EVENT_HEALTHY;
  event.generation = generation;
  return fake_link_queue_push(network, endpoint, &event, delay);
}

int fake_link_network_recovery(FakeLinkNetwork *network, uint8_t endpoint,
 int recovering, uint64_t delay)
{
  LinkTransportEvent event;

  if(endpoint >= 2)
    return 0;
  memset(&event, 0, sizeof(event));
  event.type = recovering ? LINK_TRANSPORT_EVENT_RECOVERING :
   LINK_TRANSPORT_EVENT_RECOVERED;
  if(!recovering && network->recovery_requested)
  {
    network->generation++;
    if(network->generation == 0u)
      network->generation = 1u;
    network->recovery_requested = 0;
  }
  event.generation = network->generation;
  return fake_link_queue_push(network, endpoint, &event, delay);
}

void fake_link_network_close(FakeLinkNetwork *network, int error)
{
  LinkTransportEvent event;

  if(!network->connected)
    return;
  memset(&event, 0, sizeof(event));
  event.type = error == 0 ? LINK_TRANSPORT_EVENT_CLOSED :
   LINK_TRANSPORT_EVENT_ERROR;
  event.generation = network->generation;
  event.error = error;
  fake_link_queue_push(network, 0, &event, 0);
  fake_link_queue_push(network, 1, &event, 0);
  network->connected = 0;
}
