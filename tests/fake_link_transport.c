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
  event.type = LINK_TRANSPORT_EVENT_CONNECTED;
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

  if(!network->connected || size > LINK_PROTOCOL_MAX_PACKET)
    return 0;
  if(network->drop_packets)
    return 1;

  memset(&event, 0, sizeof(event));
  event.type = LINK_TRANSPORT_EVENT_PACKET;
  event.size = (uint16_t)size;
  memcpy(event.data, data, size);
  return fake_link_queue_push(network, peer, &event,
   network->latency_us[endpoint->index]);
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

static void fake_link_disconnect(void *context)
{
  FakeLinkEndpoint *endpoint = context;
  FakeLinkNetwork *network = endpoint->network;
  LinkTransportEvent event;
  uint8_t peer = endpoint->index ^ 1u;

  if(network->connected)
  {
    memset(&event, 0, sizeof(event));
    event.type = LINK_TRANSPORT_EVENT_CLOSED;
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
  fake_link_host,
  fake_link_join,
  fake_link_send,
  fake_link_poll,
  fake_link_disconnect,
  fake_link_now
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

int fake_link_network_inject(FakeLinkNetwork *network, uint8_t endpoint,
 const uint8_t *data, size_t size, uint64_t delay)
{
  LinkTransportEvent event;

  if(endpoint >= 2 || size > LINK_PROTOCOL_MAX_PACKET)
    return 0;
  memset(&event, 0, sizeof(event));
  event.type = LINK_TRANSPORT_EVENT_PACKET;
  event.size = (uint16_t)size;
  memcpy(event.data, data, size);
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
  event.error = error;
  fake_link_queue_push(network, 0, &event, 0);
  fake_link_queue_push(network, 1, &event, 0);
  network->connected = 0;
}
