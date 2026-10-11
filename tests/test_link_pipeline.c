#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adhoc_datagram.h"
#include "gba_sio.h"
#include "link_protocol.h"
#include "link_session.h"
#include "link_tx_queue.h"

#define PIPELINE_EVENT_CAPACITY 64u
#define PIPELINE_FAULT_CAPACITY 2u
#define PIPELINE_FAULT_BUDGET 3u
#define PIPELINE_STATE_CAPACITY 131071u

typedef enum
{
  PIPELINE_FAULT_NONE,
  PIPELINE_FAULT_DROP,
  PIPELINE_FAULT_DUPLICATE,
  PIPELINE_FAULT_DELAY,
  PIPELINE_FAULT_BACKPRESSURE
} PipelineFaultAction;

typedef struct
{
  unsigned int ordinal;
  PipelineFaultAction action;
} PipelineFault;

static int failures;

#define CHECK(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
       #condition); \
      failures++; \
    } \
  } while(0)

typedef struct PipelineNetwork PipelineNetwork;

typedef struct
{
  PipelineNetwork *network;
  LinkTxQueue tx_queue;
  AdhocDatagram datagram;
  LinkTransportEvent events[PIPELINE_EVENT_CAPACITY];
  unsigned int event_read;
  unsigned int event_count;
  uint8_t index;
} PipelineEndpoint;

struct PipelineNetwork
{
  PipelineEndpoint endpoints[2];
  uint64_t now_us;
  unsigned int drop_data[2];
  unsigned int collisions;
  PipelineFault faults[PIPELINE_FAULT_CAPACITY];
  unsigned int fault_count;
  unsigned int wire_ordinal;
  unsigned int recovery_requests[2];
  uint32_t generation;
  unsigned int sent_message[2][LINK_MESSAGE_LAST + 1u];
  uint8_t capture_next_message[2][LINK_MESSAGE_LAST + 1u];
  uint8_t captured_frame[LINK_PROTOCOL_MAX_PACKET];
  size_t captured_size;
  uint8_t captured_endpoint;
  uint8_t captured_valid;
  uint32_t expected_send[2];
  AdhocDatagramPacket delayed[2];
  uint8_t host_requested[2];
  uint8_t join_requested[2];
  uint8_t connected;
  uint8_t recovery_requested;
  uint8_t collide_all;
  uint8_t delayed_valid[2];
};

typedef struct
{
  uint16_t registers[2][GBA_SIO_REGISTER_BYTES / 2u];
  GbaSio sio[2];
  LinkSession session[2];
  LinkTransport transport[2];
  PipelineNetwork network;
} PipelinePair;

typedef struct
{
  uint32_t expected_send[2];
  uint32_t last_sequence[2];
  uint32_t active_sequence[2];
  uint32_t barrier_cycle[2];
  uint32_t sio_current_cycle[2];
  uint32_t sio_start_cycle[2];
  uint32_t sio_finish_cycle[2];
  uint32_t sio_cycles_remaining[2];
  uint32_t deferred_prepare_sequence[2];
  uint32_t tx_base_sequence[2];
  uint32_t next_tx_sequence[2];
  uint32_t next_rx_sequence[2];
  uint32_t journal_epoch[2];
  uint32_t journal_checkpoint[2];
  uint32_t journal_checkpoint_hash[2];
  uint32_t journal_active_sequence[2];
  uint32_t journal_transfer_hash[2];
  uint32_t journal_start_cycle[2];
  uint32_t journal_finish_cycle[2];
  uint32_t transfer_start_cycle[2];
  uint32_t transfer_finish_cycle[2];
  uint32_t clock_generation[2];
  uint32_t clock_local_cycle[2];
  uint32_t clock_peer_cycle[2];
  uint32_t clock_local_base[2];
  uint32_t clock_peer_base[2];
  uint32_t clock_last_published_cycle[2];
  uint64_t clock_last_published_us[2];
  uint32_t recovery_target_epoch[2];
  uint32_t recovery_round[2];
  uint32_t transport_generation[2];
  uint32_t stale_transport_events[2];
  uint16_t retransmissions[2];
  uint16_t queue_depth[2];
  uint8_t state[2];
  uint8_t transaction_stage[2];
  uint8_t sio_mode[2];
  uint8_t sio_pending_start[2];
  uint8_t sio_waiting[2];
  uint8_t sio_active[2];
  uint8_t sio_decision_locked[2];
  uint8_t barrier[2];
  uint8_t applied[2];
  uint8_t deferred_prepare_valid[2];
  uint8_t deferred_prepare_idle_seen[2];
  uint8_t transport_resume_ready[2];
  uint8_t transport_health[2];
  uint8_t journal_stage[2];
  uint8_t journal_release_shared[2];
  uint8_t journal_applied[2];
  uint8_t journal_irq_delivered[2];
  uint8_t clock_peer_valid[2];
  uint8_t clock_published_valid[2];
  uint8_t clock_throttled[2];
  uint8_t recovery_state[2];
  uint8_t recovery_plan_kind[2];
  uint8_t tx_count[2];
  uint8_t rx_count[2];
  uint8_t ack_pending[2];
  uint8_t attempt_count[2];
  uint8_t delayed[2];
  uint8_t network_recovery_requested;
} PipelineStateSignature;

typedef struct
{
  PipelineStateSignature signature;
  uint8_t used;
} PipelineStateSlot;

static PipelineStateSlot pipeline_states[PIPELINE_STATE_CAPACITY];
static unsigned int pipeline_state_count;
static unsigned int pipeline_schedule_count;
static unsigned int pipeline_fault_schedule_count;
static unsigned int pipeline_recovery_schedule_count;

static int pipeline_state_safe(const PipelinePair *pair);

static int pipeline_event_push(PipelineEndpoint *endpoint,
 const LinkTransportEvent *event)
{
  unsigned int slot;

  if(endpoint->event_count >= PIPELINE_EVENT_CAPACITY)
    return 0;
  slot = (endpoint->event_read + endpoint->event_count) %
   PIPELINE_EVENT_CAPACITY;
  endpoint->events[slot] = *event;
  endpoint->event_count++;
  return 1;
}

static void pipeline_try_connect(PipelineNetwork *network)
{
  LinkTransportEvent event;
  int host = -1;
  int joiner = -1;
  unsigned int index;

  if(network->connected)
    return;
  for(index = 0; index < 2u; index++)
  {
    if(network->host_requested[index])
      host = (int)index;
    if(network->join_requested[index])
      joiner = (int)index;
  }
  if(host < 0 || joiner < 0 || host == joiner)
    return;

  memset(&event, 0, sizeof(event));
  network->generation++;
  if(network->generation == 0u)
    network->generation = 1u;
  event.type = LINK_TRANSPORT_EVENT_CONNECTED;
  event.generation = network->generation;
  CHECK(pipeline_event_push(&network->endpoints[host], &event));
  CHECK(pipeline_event_push(&network->endpoints[joiner], &event));
  network->connected = 1;
}

static int pipeline_host(void *opaque)
{
  PipelineEndpoint *endpoint = (PipelineEndpoint *)opaque;

  endpoint->network->host_requested[endpoint->index] = 1;
  pipeline_try_connect(endpoint->network);
  return 1;
}

static int pipeline_join(void *opaque)
{
  PipelineEndpoint *endpoint = (PipelineEndpoint *)opaque;

  endpoint->network->join_requested[endpoint->index] = 1;
  pipeline_try_connect(endpoint->network);
  return 1;
}

static LinkTxPriority pipeline_priority(uint8_t type)
{
  if(type == LINK_MESSAGE_SIO_STATE)
    return LINK_TX_SIO_STATE;
  if(type == LINK_MESSAGE_HEARTBEAT)
    return LINK_TX_HEARTBEAT;
  if(type == LINK_MESSAGE_CLOCK_SYNC)
    return LINK_TX_CLOCK;
  return LINK_TX_CRITICAL;
}

static int pipeline_send(void *opaque, const uint8_t *data, size_t size)
{
  PipelineEndpoint *endpoint = (PipelineEndpoint *)opaque;
  PipelineNetwork *network = endpoint->network;
  LinkPacket packet;
  int queued;

  if(!network->connected ||
     link_protocol_decode(data, size, &packet) != LINK_PROTOCOL_OK)
    return 0;
  if(packet.type <= LINK_MESSAGE_LAST)
    network->sent_message[endpoint->index][packet.type]++;
  queued = link_tx_queue_push(&endpoint->tx_queue,
   pipeline_priority(packet.type), data, size);
  if(!queued || packet.type > LINK_MESSAGE_LAST)
    return queued;
  if(network->capture_next_message[endpoint->index][packet.type] != 0)
  {
    network->capture_next_message[endpoint->index][packet.type]--;
    memcpy(network->captured_frame, data, size);
    network->captured_size = size;
    network->captured_endpoint = endpoint->index;
    network->captured_valid = 1u;
  }
  return 1;
}

static int pipeline_replay_captured(PipelinePair *pair)
{
  PipelineNetwork *network = &pair->network;
  LinkPacket packet;
  PipelineEndpoint *endpoint;

  if(!network->captured_valid || network->captured_endpoint >= 2u ||
     link_protocol_decode(network->captured_frame, network->captured_size,
      &packet) != LINK_PROTOCOL_OK)
    return 0;
  endpoint = &network->endpoints[network->captured_endpoint];
  if(!link_tx_queue_push(&endpoint->tx_queue,
     pipeline_priority(packet.type), network->captured_frame,
     network->captured_size))
    return 0;
  network->captured_valid = 0u;
  return 1;
}

static int pipeline_poll(void *opaque, LinkTransportEvent *event)
{
  PipelineEndpoint *endpoint = (PipelineEndpoint *)opaque;

  if(endpoint->event_count == 0)
    return 0;
  *event = endpoint->events[endpoint->event_read];
  endpoint->event_read = (endpoint->event_read + 1u) %
   PIPELINE_EVENT_CAPACITY;
  endpoint->event_count--;
  return 1;
}

static int pipeline_request_recovery(void *opaque, int reason,
 LinkTimeoutSource timeout_source)
{
  PipelineEndpoint *endpoint = (PipelineEndpoint *)opaque;
  PipelineNetwork *network = endpoint->network;
  LinkTransportEvent event;

  (void)reason;
  (void)timeout_source;
  if(!network->connected)
    return 0;
  network->recovery_requests[endpoint->index]++;
  if(network->recovery_requested)
    return 1;
  memset(&event, 0, sizeof(event));
  event.type = LINK_TRANSPORT_EVENT_RECOVERING;
  event.generation = network->generation;
  if(!pipeline_event_push(&network->endpoints[0], &event) ||
     !pipeline_event_push(&network->endpoints[1], &event))
    return 0;
  network->recovery_requested = 1;
  return 1;
}

static void pipeline_disconnect(void *opaque)
{
  PipelineEndpoint *endpoint = (PipelineEndpoint *)opaque;

  endpoint->network->connected = 0;
}

static uint64_t pipeline_now(void *opaque)
{
  PipelineEndpoint *endpoint = (PipelineEndpoint *)opaque;

  return endpoint->network->now_us;
}

static const LinkTransportOps pipeline_ops =
{
  .host = pipeline_host,
  .join = pipeline_join,
  .send = pipeline_send,
  .poll = pipeline_poll,
  .request_recovery = pipeline_request_recovery,
  .disconnect = pipeline_disconnect,
  .now_us = pipeline_now
};

static LinkHello pipeline_hello(void)
{
  LinkHello hello;

  memset(&hello, 0, sizeof(hello));
  hello.capabilities = LINK_SESSION_SUPPORTED_CAPABILITIES;
  hello.rom_crc32 = 0x10203040u;
  hello.rom_size = 0x01000000u;
  memcpy(hello.title, "LINK TEST   ", 12u);
  memcpy(hello.game_code, "TST0", 4u);
  hello.max_players = 2;
  return hello;
}

static void pipeline_init(PipelinePair *pair)
{
  LinkHello hello = pipeline_hello();
  unsigned int index;

  memset(pair, 0, sizeof(*pair));
  for(index = 0; index < 2u; index++)
  {
    PipelineEndpoint *endpoint = &pair->network.endpoints[index];

    endpoint->network = &pair->network;
    endpoint->index = (uint8_t)index;
    link_tx_queue_init(&endpoint->tx_queue);
    adhoc_datagram_init(&endpoint->datagram, 0x55667788u, 1u, 0u);
    adhoc_datagram_set_role(&endpoint->datagram, index == 0u);
    pair->transport[index].ops = &pipeline_ops;
    pair->transport[index].context = endpoint;
    gba_sio_init(&pair->sio[index], pair->registers[index]);
    link_session_init(&pair->session[index], &pair->sio[index],
     pair->transport[index], &hello);
  }
}

static void pipeline_fill(PipelineEndpoint *endpoint)
{
  LinkTxItem item;
  unsigned int filled = 0;

  while(filled < 4u && adhoc_datagram_can_queue(&endpoint->datagram) &&
        link_tx_queue_pop_ready(&endpoint->tx_queue,
         adhoc_datagram_outstanding(&endpoint->datagram), &item))
  {
    CHECK(adhoc_datagram_queue(&endpoint->datagram, item.data, item.size));
    filled++;
  }
}

static void pipeline_deliver(PipelineEndpoint *endpoint,
 const AdhocDatagramPacket *packet)
{
  LinkTransportEvent event;
  uint8_t payload[ADHOC_DATAGRAM_MAX_PAYLOAD];
  size_t payload_size;
  AdhocDatagramReceiveResult result;

  result = adhoc_datagram_receive(&endpoint->datagram, packet->data,
   packet->size, endpoint->network->now_us);
  if(result == ADHOC_DATAGRAM_FOREIGN ||
     result == ADHOC_DATAGRAM_OUT_OF_WINDOW)
    return;
  CHECK(result == ADHOC_DATAGRAM_ACCEPTED ||
   result == ADHOC_DATAGRAM_DUPLICATE);
  if(result != ADHOC_DATAGRAM_ACCEPTED &&
     result != ADHOC_DATAGRAM_DUPLICATE)
    return;
  while(adhoc_datagram_pop(&endpoint->datagram, payload,
         sizeof(payload), &payload_size))
  {
    memset(&event, 0, sizeof(event));
    event.type = LINK_TRANSPORT_EVENT_PACKET;
    event.generation = endpoint->network->generation;
    event.size = (uint16_t)payload_size;
    memcpy(event.data, payload, payload_size);
    CHECK(pipeline_event_push(endpoint, &event));
  }
}

static PipelineFaultAction pipeline_fault_action(PipelineNetwork *network)
{
  unsigned int ordinal = network->wire_ordinal++;
  unsigned int index;

  for(index = 0; index < network->fault_count; index++)
  {
    if(network->faults[index].ordinal == ordinal)
      return network->faults[index].action;
  }
  return PIPELINE_FAULT_NONE;
}

static void pipeline_wire_step(PipelineNetwork *network)
{
  AdhocDatagramPacket packet[2];
  AdhocDatagramPacket delayed[2];
  PipelineFaultAction action[2] =
   { PIPELINE_FAULT_NONE, PIPELINE_FAULT_NONE };
  int prepared[2] = { 0, 0 };
  int dropped[2] = { 0, 0 };
  uint8_t delayed_valid[2];
  unsigned int index;

  memcpy(delayed, network->delayed, sizeof(delayed));
  memcpy(delayed_valid, network->delayed_valid, sizeof(delayed_valid));
  memset(network->delayed_valid, 0, sizeof(network->delayed_valid));

  for(index = 0; index < 2u; index++)
  {
    PipelineEndpoint *endpoint = &network->endpoints[index];

    pipeline_fill(endpoint);
    prepared[index] = adhoc_datagram_prepare(&endpoint->datagram,
     network->now_us, &packet[index]);
    if(prepared[index])
    {
      action[index] = pipeline_fault_action(network);
      if(action[index] == PIPELINE_FAULT_BACKPRESSURE)
      {
        CHECK(adhoc_datagram_note_blocked(&endpoint->datagram,
         &packet[index], network->now_us));
        prepared[index] = 0;
      }
      else
      {
        CHECK(adhoc_datagram_note_sent(&endpoint->datagram, &packet[index],
         network->now_us));
      }
    }
  }

  if(network->collide_all && prepared[0] && prepared[1])
  {
    dropped[0] = 1;
    dropped[1] = 1;
    network->collisions++;
  }
  for(index = 0; index < 2u; index++)
  {
    if(prepared[index] && packet[index].kind == ADHOC_DATAGRAM_TX_DATA &&
       network->drop_data[index] != 0)
    {
      network->drop_data[index]--;
      dropped[index] = 1;
    }
    if(!prepared[index] || dropped[index] ||
       action[index] == PIPELINE_FAULT_DROP)
      continue;
    if(action[index] == PIPELINE_FAULT_DELAY)
    {
      network->delayed[index] = packet[index];
      network->delayed_valid[index] = 1;
      continue;
    }
    pipeline_deliver(&network->endpoints[index ^ 1u], &packet[index]);
    if(action[index] == PIPELINE_FAULT_DUPLICATE)
      pipeline_deliver(&network->endpoints[index ^ 1u], &packet[index]);
  }
  for(index = 0; index < 2u; index++)
  {
    if(delayed_valid[index])
      pipeline_deliver(&network->endpoints[index ^ 1u], &delayed[index]);
  }
  network->now_us += 1000u;
}

static void pipeline_main_step(PipelinePair *pair)
{
  link_session_pump(&pair->session[0]);
  link_session_pump(&pair->session[1]);
  link_session_publish_clock(&pair->session[0],
   pair->session[0].clock.local_cycle);
  link_session_publish_clock(&pair->session[1],
   pair->session[1].clock.local_cycle);
}

static void pipeline_recovery_endpoint(PipelinePair *pair,
 unsigned int index, int recovering)
{
  LinkTransportEvent event;
  PipelineEndpoint *endpoint;

  CHECK(index < 2u);
  if(index >= 2u)
    return;
  memset(&event, 0, sizeof(event));
  event.type = recovering ? LINK_TRANSPORT_EVENT_RECOVERING :
   LINK_TRANSPORT_EVENT_RECOVERED;
  event.generation = pair->network.generation;
  endpoint = &pair->network.endpoints[index];
  if(!recovering)
  {
    link_tx_queue_init(&endpoint->tx_queue);
    adhoc_datagram_init(&endpoint->datagram, 0x55667788u,
     pair->network.generation, pair->network.now_us);
    adhoc_datagram_set_role(&endpoint->datagram, index == 0u);
  }
  CHECK(pipeline_event_push(endpoint, &event));
}

static void pipeline_recovery(PipelinePair *pair, int recovering)
{
  unsigned int index;

  if(!recovering)
  {
    pair->network.recovery_requested = 0;
    pair->network.generation++;
    if(pair->network.generation == 0u)
      pair->network.generation = 1u;
  }
  for(index = 0; index < 2u; index++)
    pipeline_recovery_endpoint(pair, index, recovering);
  pipeline_main_step(pair);
}

static int pipeline_wait_connected(PipelinePair *pair,
 uint64_t timeout_us)
{
  uint64_t deadline = pair->network.now_us + timeout_us;

  while(pair->network.now_us < deadline)
  {
    if(pair->network.now_us % 20000u == 0)
      pipeline_main_step(pair);
    pipeline_wire_step(&pair->network);
    if(link_session_is_connected(&pair->session[0]) &&
       link_session_is_connected(&pair->session[1]))
      return 1;
  }
  pipeline_main_step(pair);
  return link_session_is_connected(&pair->session[0]) &&
   link_session_is_connected(&pair->session[1]);
}

static void pipeline_open(PipelinePair *pair)
{
  CHECK(link_session_host(&pair->session[0], 0x12345678u));
  CHECK(link_session_join(&pair->session[1]));
}

static void test_async_handshake_after_transport_ack(void)
{
  PipelinePair pair;

  pipeline_init(&pair);
  pipeline_open(&pair);
  CHECK(pipeline_wait_connected(&pair, 6000000u));
  CHECK(pair.network.endpoints[1].datagram.ack_packets != 0u);
  CHECK(pair.session[0].peer_hello.rom_crc32 == 0x10203040u);
  CHECK(pair.session[1].peer_hello.rom_crc32 == 0x10203040u);
}

static void test_loss_and_bidirectional_half_duplex_collision_recover(void)
{
  PipelinePair pair;
  LinkSioState host_state;
  LinkSioState join_state;
  uint64_t deadline;

  pipeline_init(&pair);
  pair.network.drop_data[0] = 1u;
  pipeline_open(&pair);
  CHECK(pipeline_wait_connected(&pair, 6000000u));
  CHECK(pair.network.endpoints[0].datagram.retransmissions != 0u);

  memset(&host_state, 0, sizeof(host_state));
  host_state.mode = GBA_SIO_MODE_MULTIPLAYER;
  host_state.player_id = 0;
  host_state.player_count = 2;
  host_state.flags = LINK_SIO_FLAG_GAMEPLAY_ACTIVE;
  host_state.control = 0x6003u;
  join_state = host_state;
  join_state.player_id = 1;
  join_state.control = 0x6002u;

  pair.network.collide_all = 1;
  CHECK(link_session_publish_sio_state(&pair.session[0], &host_state) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(link_session_publish_sio_state(&pair.session[1], &join_state) ==
   LINK_SESSION_EVENT_NONE);
  deadline = pair.network.now_us + 2000000u;
  while(pair.network.now_us < deadline &&
        (!pair.session[0].peer_sio_valid ||
         !pair.session[1].peer_sio_valid ||
         !pair.session[0].clock.peer_valid ||
         !pair.session[1].clock.peer_valid))
  {
    if(pair.network.now_us % 20000u == 0)
      pipeline_main_step(&pair);
    pipeline_wire_step(&pair.network);
  }
  pipeline_main_step(&pair);

  CHECK(pair.network.collisions != 0u);
  CHECK(pair.network.endpoints[1].datagram.retransmissions != 0u);
  CHECK(pair.session[0].peer_sio_valid);
  CHECK(pair.session[1].peer_sio_valid);
  CHECK(pair.session[0].clock.peer_valid);
  CHECK(pair.session[1].clock.peer_valid);
  CHECK(pair.session[0].peer_sio.control == join_state.control);
  CHECK(pair.session[1].peer_sio.control == host_state.control);
  CHECK(link_session_is_connected(&pair.session[0]));
  CHECK(link_session_is_connected(&pair.session[1]));
}

static LinkSioState pipeline_sio_state(const PipelinePair *pair,
 unsigned int endpoint)
{
  const GbaSio *sio = &pair->sio[endpoint];
  LinkSioState state;

  memset(&state, 0, sizeof(state));
  state.mode = (uint8_t)sio->mode;
  state.player_id = sio->player_id;
  state.player_count = sio->player_count;
  state.flags = LINK_SIO_FLAG_GAMEPLAY_ACTIVE;
  if(sio->pending_start)
    state.flags |= LINK_SIO_FLAG_PENDING_START;
  if(sio->waiting)
    state.flags |= LINK_SIO_FLAG_WAITING;
  if(sio->active)
    state.flags |= LINK_SIO_FLAG_ACTIVE;
  state.control = pair->registers[endpoint][GBA_SIO_REG_CONTROL >> 1];
  state.rcnt = pair->registers[endpoint][GBA_SIO_REG_RCNT >> 1];
  return state;
}

static void test_transactional_transfer_survives_datagram_loss(void)
{
  PipelinePair pair;
  LinkSioState state;
  uint64_t deadline;
  uint64_t started;
  uint32_t cycles;

  pipeline_init(&pair);
  pipeline_open(&pair);
  CHECK(pipeline_wait_connected(&pair, 6000000u));
  CHECK((pair.session[0].negotiated_capabilities &
   LINK_CAPABILITY_TRANSACTIONAL) != 0);
  CHECK((pair.session[0].negotiated_capabilities &
   LINK_CAPABILITY_FAST_TRANSFER) == 0);

  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x2000u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x2000u);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 0x1234u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_SEND, 0xABCDu);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6083u);
  state = pipeline_sio_state(&pair, 0u);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  state = pipeline_sio_state(&pair, 1u);
  CHECK(link_session_publish_sio_state(&pair.session[1], &state) ==
   LINK_SESSION_EVENT_NONE);

  deadline = pair.network.now_us + 1000000u;
  while(pair.network.now_us < deadline &&
        (!pair.session[0].peer_sio_valid ||
         !pair.session[1].peer_sio_valid ||
         !pair.session[0].clock.peer_valid ||
         !pair.session[1].clock.peer_valid))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pair.session[0].peer_sio_valid);
  CHECK(pair.session[1].peer_sio_valid);
  CHECK(pair.session[0].clock.peer_valid);
  CHECK(pair.session[1].clock.peer_valid);

  pair.network.drop_data[0] = 1u;
  started = pair.network.now_us;
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  deadline = started + 2000000u;
  while(pair.network.now_us < deadline &&
        (link_session_state(&pair.session[0]) != LINK_SESSION_CONNECTED ||
         link_session_state(&pair.session[1]) != LINK_SESSION_CONNECTED))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
    CHECK(link_session_check_invariants(&pair.session[0]) == 0);
    CHECK(link_session_check_invariants(&pair.session[1]) == 0);
  }
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.network.now_us - started < 2000000u);
  CHECK(pair.network.endpoints[0].datagram.retransmissions != 0u);
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_START] == 1u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_REPLY] == 1u);
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_PREPARE] == 0u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_ACCEPT] == 0u);
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_COMMIT] == 0u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_COMMIT_ACK] ==
   0u);
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_RELEASE] == 0u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_RELEASE_ACK] ==
   0u);

  cycles = gba_sio_next_event(&pair.sio[0]);
  CHECK(cycles != GBA_SIO_NO_EVENT);
  CHECK(gba_sio_next_event(&pair.sio[1]) == cycles);
  CHECK(gba_sio_advance(&pair.sio[0], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_advance(&pair.sio[1], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(pair.registers[0][GBA_SIO_REG_MULTI1 >> 1] == 0xABCDu);
  CHECK(pair.registers[1][GBA_SIO_REG_MULTI0 >> 1] == 0x1234u);
}

static int pipeline_exchange_multiplayer_word(PipelinePair *pair,
 uint16_t host_word, uint16_t joiner_word)
{
  LinkSioState state;
  uint32_t sequence = pair->session[0].last_sequence + 1u;
  uint32_t cycles;
  uint64_t deadline;

  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_SEND, host_word);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_SEND, joiner_word);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  if(gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL, 0x6083u) !=
     GBA_SIO_EVENT_START)
    return 0;

  state = pipeline_sio_state(pair, 0u);
  if(link_session_publish_sio_state(&pair->session[0], &state) !=
     LINK_SESSION_EVENT_NONE)
    return 0;
  state = pipeline_sio_state(pair, 1u);
  if(link_session_publish_sio_state(&pair->session[1], &state) !=
     LINK_SESSION_EVENT_NONE)
    return 0;
  if((link_session_begin_transfer(&pair->session[0]) &
      LINK_SESSION_EVENT_BARRIER_BEGIN) == 0)
    return 0;

  deadline = pair->network.now_us + 2000000u;
  while(pair->network.now_us < deadline &&
        (pair->session[0].last_sequence != sequence ||
         pair->session[1].last_sequence != sequence ||
         pair->session[0].state != LINK_SESSION_CONNECTED ||
         pair->session[1].state != LINK_SESSION_CONNECTED))
  {
    pipeline_wire_step(&pair->network);
    pipeline_main_step(pair);
  }
  if(pair->session[0].last_sequence != sequence ||
     pair->session[1].last_sequence != sequence ||
     pair->session[0].state != LINK_SESSION_CONNECTED ||
     pair->session[1].state != LINK_SESSION_CONNECTED)
    return 0;

  cycles = gba_sio_next_event(&pair->sio[0]);
  if(cycles == GBA_SIO_NO_EVENT ||
     gba_sio_next_event(&pair->sio[1]) != cycles ||
     gba_sio_advance(&pair->sio[0], cycles) !=
      GBA_SIO_EVENT_SERIAL_IRQ ||
     gba_sio_advance(&pair->sio[1], cycles) !=
      GBA_SIO_EVENT_SERIAL_IRQ)
    return 0;
  return pair->registers[0][GBA_SIO_REG_MULTI0 >> 1] == host_word &&
   pair->registers[0][GBA_SIO_REG_MULTI1 >> 1] == joiner_word &&
   pair->registers[1][GBA_SIO_REG_MULTI0 >> 1] == host_word &&
   pair->registers[1][GBA_SIO_REG_MULTI1 >> 1] == joiner_word;
}

/* Transport vectors only; guest IRQ cadence is covered by test_link_cable_runtime. */
static void test_multiplayer_nine_word_stream(void)
{
  PipelinePair pair;
  LinkSioState state;
  uint64_t deadline;
  static const uint16_t host_words[9] =
  {
    0x0000u, 0xB9A0u, 0x8FFFu, 0x7210u, 0x3401u,
    0xAA55u, 0x55AAu, 0x0080u, 0xFFFFu
  };
  static const uint16_t joiner_words[9] =
  {
    0x0000u, 0xB9A0u, 0xB9A0u, 0x1270u, 0x4301u,
    0x55AAu, 0xAA55u, 0x8000u, 0x0000u
  };
  unsigned int word;

  pipeline_init(&pair);
  pipeline_open(&pair);
  CHECK(pipeline_wait_connected(&pair, 6000000u));
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  state = pipeline_sio_state(&pair, 0u);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  state = pipeline_sio_state(&pair, 1u);
  CHECK(link_session_publish_sio_state(&pair.session[1], &state) ==
   LINK_SESSION_EVENT_NONE);
  deadline = pair.network.now_us + 1000000u;
  while(pair.network.now_us < deadline &&
        (!pair.session[0].peer_sio_valid ||
         !pair.session[1].peer_sio_valid ||
         !pair.session[0].clock.peer_valid ||
         !pair.session[1].clock.peer_valid))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }

  for(word = 0; word < 9u; word++)
    CHECK(pipeline_exchange_multiplayer_word(&pair, host_words[word],
     joiner_words[word]));
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_START] == 9u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_REPLY] == 9u);
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_PREPARE] == 0u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_ACCEPT] == 0u);
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_COMMIT] == 0u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_COMMIT_ACK] ==
   0u);
  CHECK(pair.network.sent_message[0][LINK_MESSAGE_TRANSFER_RELEASE] == 0u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_RELEASE_ACK] ==
   0u);
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 9u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 9u);
  CHECK(link_stream_latch_count(&pair.session[1].stream) == 9u);
  CHECK(link_stream_reply_cache_depth(&pair.session[1].stream) <= 1u);
}

static void test_resume_survives_five_second_datagram_outage(void)
{
  PipelinePair pair;
  uint64_t deadline;

  pipeline_init(&pair);
  pipeline_open(&pair);
  CHECK(pipeline_wait_connected(&pair, 6000000u));

  pipeline_recovery(&pair, 1);
  CHECK(pair.session[0].state == LINK_SESSION_RECOVERING);
  CHECK(pair.session[1].state == LINK_SESSION_RECOVERING);

  pair.network.drop_data[0] = UINT32_MAX;
  pair.network.drop_data[1] = UINT32_MAX;
  pipeline_recovery(&pair, 0);
  deadline = pair.network.now_us + 5000000u;
  while(pair.network.now_us < deadline)
  {
    if(pair.network.now_us % 20000u == 0)
      pipeline_main_step(&pair);
    pipeline_wire_step(&pair.network);
  }
  pipeline_main_step(&pair);
  CHECK(pair.session[0].state == LINK_SESSION_WAIT_RESUME_ACK);
  CHECK(pair.session[1].state == LINK_SESSION_RECOVERING);

  pair.network.drop_data[0] = 0;
  pair.network.drop_data[1] = 0;
  deadline = pair.network.now_us + 8000000u;
  while(pair.network.now_us < deadline &&
        (pair.session[0].state != LINK_SESSION_CONNECTED ||
         pair.session[1].state != LINK_SESSION_CONNECTED))
  {
    if(pair.network.now_us % 20000u == 0)
      pipeline_main_step(&pair);
    pipeline_wire_step(&pair.network);
  }
  pipeline_main_step(&pair);

  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(pair.network.endpoints[0].datagram.retransmissions != 0u);
}

static void test_prepared_joiner_recovers_physical_failure_race(void)
{
  PipelinePair pair;
  LinkSioState state;
  LinkTransportEvent health;
  uint32_t events;
  uint32_t cycles;
  uint64_t deadline;

  pipeline_init(&pair);
  pipeline_open(&pair);
  CHECK(pipeline_wait_connected(&pair, 6000000u));
  pair.session[0].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  pair.session[1].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  state = pipeline_sio_state(&pair, 0u);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  state = pipeline_sio_state(&pair, 1u);
  CHECK(link_session_publish_sio_state(&pair.session[1], &state) ==
   LINK_SESSION_EVENT_NONE);
  deadline = pair.network.now_us + 1000000u;
  while(pair.network.now_us < deadline &&
        (!pair.session[0].peer_sio_valid ||
         !pair.session[1].peer_sio_valid ||
         !pair.session[0].clock.peer_valid ||
         !pair.session[1].clock.peer_valid))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pipeline_exchange_multiplayer_word(&pair, 0x1111u, 0x2222u));

  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 0x3333u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_SEND, 0x4444u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  CHECK(gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6083u) ==
   GBA_SIO_EVENT_START);
  state = pipeline_sio_state(&pair, 0u);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  state = pipeline_sio_state(&pair, 1u);
  CHECK(link_session_publish_sio_state(&pair.session[1], &state) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN);

  deadline = pair.network.now_us + 2000000u;
  while(pair.network.now_us < deadline &&
        (pair.session[0].journal.stage != LINK_JOURNAL_DECIDED ||
         pair.session[1].journal.stage != LINK_JOURNAL_ARMED))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_DECIDED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);

  pair.network.drop_data[0] = UINT32_MAX;
  memset(&health, 0, sizeof(health));
  health.type = LINK_TRANSPORT_EVENT_DEGRADED;
  health.generation = pair.network.generation;
  CHECK(pipeline_event_push(&pair.network.endpoints[0], &health));
  CHECK(pipeline_event_push(&pair.network.endpoints[1], &health));
  pipeline_main_step(&pair);
  CHECK(pair.session[0].transport_health == LINK_TRANSPORT_HEALTH_PRESSURED);
  CHECK(pair.session[1].transport_health == LINK_TRANSPORT_HEALTH_PRESSURED);

  pair.network.now_us += LINK_SESSION_TRANSFER_TIMEOUT_US + 1u;
  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0u);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);
  CHECK(pair.session[1].transport_health ==
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED);
  CHECK(pair.network.recovery_requested);

  link_session_pump(&pair.session[0]);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[0].state == LINK_SESSION_RECOVERING);
  CHECK(pair.session[1].state == LINK_SESSION_RECOVERING);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_DECIDED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);

  pair.network.drop_data[0] = 0;
  pipeline_recovery(&pair, 0);
  deadline = pair.network.now_us + 20000000u;
  while(pair.network.now_us < deadline &&
        (pair.session[0].state != LINK_SESSION_CONNECTED ||
         pair.session[1].state != LINK_SESSION_CONNECTED ||
         pair.session[0].transport_health ==
          LINK_TRANSPORT_HEALTH_RECOVERING ||
         pair.session[1].transport_health ==
          LINK_TRANSPORT_HEALTH_RECOVERING))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].transport_health !=
   LINK_TRANSPORT_HEALTH_RECOVERING);
  CHECK(pair.session[1].transport_health !=
   LINK_TRANSPORT_HEALTH_RECOVERING);
  CHECK(pair.session[0].last_sequence == 2u);
  CHECK(pair.session[1].last_sequence == 2u);
  CHECK(pair.session[0].applied_sequence == 2u);
  CHECK(pair.session[1].applied_sequence == 2u);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);

  cycles = gba_sio_next_event(&pair.sio[0]);
  CHECK(cycles != GBA_SIO_NO_EVENT);
  CHECK(gba_sio_next_event(&pair.sio[1]) == cycles);
  CHECK(gba_sio_advance(&pair.sio[0], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_advance(&pair.sio[1], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(pair.registers[0][GBA_SIO_REG_MULTI1 >> 1] == 0x4444u);
  CHECK(pair.registers[1][GBA_SIO_REG_MULTI0 >> 1] == 0x3333u);
}

static void test_host_armed_joiner_empty_uses_symmetric_recovery(void)
{
  PipelinePair pair;
  LinkSioState state;
  uint64_t deadline;
  unsigned int step;

  pipeline_init(&pair);
  pipeline_open(&pair);
  CHECK(pipeline_wait_connected(&pair, 6000000u));
  pair.session[0].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  pair.session[1].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  state = pipeline_sio_state(&pair, 0u);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  state = pipeline_sio_state(&pair, 1u);
  CHECK(link_session_publish_sio_state(&pair.session[1], &state) ==
   LINK_SESSION_EVENT_NONE);
  deadline = pair.network.now_us + 1000000u;
  while(pair.network.now_us < deadline &&
        (!pair.session[0].peer_sio_valid ||
         !pair.session[1].peer_sio_valid ||
         !pair.session[0].clock.peer_valid ||
         !pair.session[1].clock.peer_valid))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }

  pair.network.drop_data[0] = UINT32_MAX;
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 0x5555u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_SEND, 0xAAAAu);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  CHECK(gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6083u) ==
   GBA_SIO_EVENT_START);
  state = pipeline_sio_state(&pair, 0u);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  state = pipeline_sio_state(&pair, 1u);
  CHECK(link_session_publish_sio_state(&pair.session[1], &state) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_ARMED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);

  for(step = 0; step < 100u; step++)
  {
    pipeline_wire_step(&pair.network);
    if(step % 20u == 0)
      pipeline_main_step(&pair);
  }
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_ARMED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[0].last_rx_us > pair.session[1].last_rx_us);

  deadline = pair.session[1].last_rx_us + LINK_SESSION_TIMEOUT_US;
  pair.network.now_us = deadline - 1u;
  pipeline_main_step(&pair);
  CHECK(pair.network.recovery_requests[0] == 0u);
  CHECK(pair.network.recovery_requests[1] == 0u);
  CHECK(pair.session[1].transport_health !=
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED);

  pair.network.now_us = deadline;
  pipeline_main_step(&pair);
  CHECK(pair.network.recovery_requests[0] == 0u);
  CHECK(pair.network.recovery_requests[1] == 1u);
  CHECK(pair.session[1].transport_health ==
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_ARMED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);

  pipeline_main_step(&pair);
  CHECK(pair.session[0].state == LINK_SESSION_RECOVERING);
  CHECK(pair.session[1].state == LINK_SESSION_RECOVERING);
  pair.network.drop_data[0] = 0;
  pipeline_recovery(&pair, 0);
  deadline = pair.network.now_us + 20000000u;
  while(pair.network.now_us < deadline &&
        (pair.session[0].state != LINK_SESSION_CONNECTED ||
         pair.session[1].state != LINK_SESSION_CONNECTED ||
         pair.session[0].transport_health ==
          LINK_TRANSPORT_HEALTH_RECOVERING ||
         pair.session[1].transport_health ==
          LINK_TRANSPORT_HEALTH_RECOVERING))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].transport_health !=
   LINK_TRANSPORT_HEALTH_RECOVERING);
  CHECK(pair.session[1].transport_health !=
   LINK_TRANSPORT_HEALTH_RECOVERING);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[0].transaction_applied == 0);
  CHECK(pair.session[1].transaction_applied == 0);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0u);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0u);
}

static uint32_t pipeline_state_hash(const PipelineStateSignature *signature)
{
  const uint8_t *bytes = (const uint8_t *)signature;
  uint32_t hash = 2166136261u;
  size_t index;

  for(index = 0; index < sizeof(*signature); index++)
    hash = (hash ^ bytes[index]) * 16777619u;
  return hash;
}

static int pipeline_state_record(const PipelinePair *pair)
{
  PipelineStateSignature signature;
  uint32_t hash;
  unsigned int probe;
  unsigned int slot;
  unsigned int index;

  memset(&signature, 0, sizeof(signature));
  memcpy(signature.expected_send, pair->network.expected_send,
   sizeof(signature.expected_send));
  for(index = 0; index < 2u; index++)
  {
    const PipelineEndpoint *endpoint = &pair->network.endpoints[index];
    unsigned int tx_slot;

    signature.last_sequence[index] = pair->session[index].last_sequence;
    signature.active_sequence[index] = pair->session[index].active_sequence;
    signature.barrier_cycle[index] = pair->session[index].barrier_cycle;
    signature.sio_current_cycle[index] = pair->sio[index].current_cycle;
    signature.sio_start_cycle[index] =
     pair->sio[index].transfer_start_cycle;
    signature.sio_finish_cycle[index] =
     pair->sio[index].transfer_finish_cycle;
    signature.sio_cycles_remaining[index] =
     pair->sio[index].cycles_remaining;
    signature.deferred_prepare_sequence[index] =
     pair->session[index].deferred_prepare_sequence;
    signature.tx_base_sequence[index] =
     endpoint->datagram.tx_base_sequence;
    signature.next_tx_sequence[index] =
     endpoint->datagram.next_tx_sequence;
    signature.next_rx_sequence[index] =
     endpoint->datagram.next_rx_sequence;
    signature.journal_epoch[index] = pair->session[index].journal.epoch;
    signature.journal_checkpoint[index] =
     pair->session[index].journal.checkpoint;
    signature.journal_checkpoint_hash[index] =
     pair->session[index].journal.checkpoint_hash;
    signature.journal_active_sequence[index] =
     pair->session[index].journal.active_sequence;
    signature.journal_transfer_hash[index] =
     pair->session[index].journal.transfer_hash;
    signature.journal_start_cycle[index] =
     pair->session[index].journal.start_cycle;
    signature.journal_finish_cycle[index] =
     pair->session[index].journal.finish_cycle;
    signature.transfer_start_cycle[index] =
     pair->session[index].transfer.start_cycle;
    signature.transfer_finish_cycle[index] =
     pair->session[index].transfer.finish_cycle;
    signature.clock_generation[index] =
     pair->session[index].clock.generation;
    signature.clock_local_cycle[index] =
     pair->session[index].clock.local_cycle;
    signature.clock_peer_cycle[index] =
     pair->session[index].clock.peer_cycle;
    signature.clock_local_base[index] =
     pair->session[index].clock.local_base;
    signature.clock_peer_base[index] =
     pair->session[index].clock.peer_base;
    signature.clock_last_published_cycle[index] =
     pair->session[index].clock.last_published_cycle;
    signature.clock_last_published_us[index] =
     pair->session[index].clock.last_published_us;
    signature.recovery_target_epoch[index] =
     pair->session[index].recovery.target_epoch;
    signature.recovery_round[index] = pair->session[index].recovery.round;
    signature.transport_generation[index] =
     pair->session[index].transport_generation;
    signature.stale_transport_events[index] =
     pair->session[index].stale_transport_events;
    signature.retransmissions[index] =
     (uint16_t)endpoint->datagram.retransmissions;
    signature.queue_depth[index] = (uint16_t)link_tx_queue_depth(
     &endpoint->tx_queue);
    signature.state[index] = (uint8_t)pair->session[index].state;
    signature.transaction_stage[index] =
     (uint8_t)pair->session[index].transaction_stage;
    signature.sio_mode[index] = (uint8_t)pair->sio[index].mode;
    signature.sio_pending_start[index] = pair->sio[index].pending_start;
    signature.sio_waiting[index] = pair->sio[index].waiting;
    signature.sio_active[index] = pair->sio[index].active;
    signature.sio_decision_locked[index] =
     pair->sio[index].decision_locked;
    signature.barrier[index] = pair->session[index].barrier_active;
    signature.applied[index] = pair->session[index].transaction_applied;
    signature.deferred_prepare_valid[index] =
     pair->session[index].deferred_prepare_valid;
    signature.deferred_prepare_idle_seen[index] =
     pair->session[index].deferred_prepare_idle_seen;
    signature.transport_resume_ready[index] =
     pair->session[index].transport_resume_ready;
    signature.transport_health[index] =
     (uint8_t)pair->session[index].transport_health;
    signature.journal_stage[index] =
     (uint8_t)pair->session[index].journal.stage;
    signature.journal_release_shared[index] =
     pair->session[index].journal.release_shared;
    signature.journal_applied[index] =
     pair->session[index].journal.applied;
    signature.journal_irq_delivered[index] =
     pair->session[index].journal.irq_delivered;
    signature.clock_peer_valid[index] =
     pair->session[index].clock.peer_valid;
    signature.clock_published_valid[index] =
     pair->session[index].clock.published_valid;
    signature.clock_throttled[index] = (uint8_t)link_clock_should_throttle(
     &pair->session[index].clock, pair->network.now_us);
    signature.recovery_state[index] =
     (uint8_t)pair->session[index].recovery.state;
    signature.recovery_plan_kind[index] =
     (uint8_t)pair->session[index].recovery_plan.kind;
    signature.tx_count[index] = (uint8_t)endpoint->datagram.tx_count;
    signature.rx_count[index] = (uint8_t)endpoint->datagram.rx_count;
    signature.ack_pending[index] = endpoint->datagram.ack_pending;
    for(tx_slot = 0; tx_slot < ADHOC_DATAGRAM_TX_WINDOW; tx_slot++)
      signature.attempt_count[index] +=
       endpoint->datagram.tx[tx_slot].attempts;
    signature.delayed[index] = pair->network.delayed_valid[index];
  }
  signature.network_recovery_requested = pair->network.recovery_requested;

  hash = pipeline_state_hash(&signature);
  for(probe = 0; probe < PIPELINE_STATE_CAPACITY; probe++)
  {
    slot = (hash + probe) % PIPELINE_STATE_CAPACITY;
    if(!pipeline_states[slot].used)
    {
      pipeline_states[slot].signature = signature;
      pipeline_states[slot].used = 1;
      pipeline_state_count++;
      return 1;
    }
    if(memcmp(&pipeline_states[slot].signature, &signature,
       sizeof(signature)) == 0)
      return 1;
  }
  return 0;
}

static int pipeline_state_safe(const PipelinePair *pair)
{
  const LinkSession *host = &pair->session[0];
  const LinkSession *joiner = &pair->session[1];
  unsigned int index;

  if(link_session_check_invariants(host) != 0 ||
     link_session_check_invariants(joiner) != 0)
    return 0;
  for(index = 0; index < 2u; index++)
  {
    const GbaSio *sio = &pair->sio[index];

    if((unsigned int)sio->pending_start + sio->waiting + sio->active > 1u ||
       (sio->decision_locked && !sio->waiting && !sio->active) ||
       (sio->active &&
        (sio->cycles_remaining == 0 ||
         sio->transfer_finish_cycle - sio->current_cycle !=
          sio->cycles_remaining)))
      return 0;
  }
  if(host->transaction_applied && joiner->transaction_applied &&
     (host->applied_sequence != joiner->applied_sequence ||
      memcmp(&host->applied_transfer, &joiner->applied_transfer,
       sizeof(host->applied_transfer)) != 0))
    return 0;
  if(host->transaction_sequence == joiner->transaction_sequence &&
     ((host->transaction_applied &&
       joiner->transaction_stage == LINK_TRANSACTION_ABORTED) ||
      (joiner->transaction_applied &&
       host->transaction_stage == LINK_TRANSACTION_ABORTED)))
    return 0;
  return pipeline_state_record(pair);
}

static void pipeline_rebind(PipelinePair *pair)
{
  unsigned int index;

  for(index = 0; index < 2u; index++)
  {
    pair->network.endpoints[index].network = &pair->network;
    pair->transport[index].context = &pair->network.endpoints[index];
    pair->session[index].transport = pair->transport[index];
    pair->session[index].sio = &pair->sio[index];
    pair->sio[index].registers = pair->registers[index];
  }
}

static int pipeline_idle(const PipelinePair *pair)
{
  unsigned int index;

  for(index = 0; index < 2u; index++)
  {
    const PipelineEndpoint *endpoint = &pair->network.endpoints[index];

    if(link_tx_queue_depth(&endpoint->tx_queue) != 0 ||
       adhoc_datagram_outstanding(&endpoint->datagram) != 0 ||
       adhoc_datagram_buffered(&endpoint->datagram) != 0 ||
       endpoint->event_count != 0 || endpoint->datagram.ack_pending)
      return 0;
  }
  return pair->network.delayed_valid[0] == 0 &&
   pair->network.delayed_valid[1] == 0;
}

static int pipeline_arm_profile(PipelinePair *pair, GbaSioMode mode,
 uint8_t clock_owner, uint8_t setting, uint32_t generation)
{
  LinkSioState state;
  uint32_t first = 0x11223344u ^ generation;
  uint32_t second = 0xAABBCCDDu ^ (generation << 8);
  uint16_t mode_bits = mode == GBA_SIO_MODE_NORMAL_32 ? 0x1000u :
   mode == GBA_SIO_MODE_MULTIPLAYER ? 0x2000u : 0u;
  uint16_t control;

  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL, mode_bits);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_CONTROL, mode_bits);
  if(mode == GBA_SIO_MODE_NORMAL_32)
  {
    gba_sio_write32(&pair->sio[0], GBA_SIO_REG_DATA32_L, first);
    gba_sio_write32(&pair->sio[1], GBA_SIO_REG_DATA32_L, second);
  }
  else
  {
    first = 0x1234u ^ generation;
    second = 0xABCDu ^ (generation << 4);
    gba_sio_write16(&pair->sio[0], GBA_SIO_REG_SEND, (uint16_t)first);
    gba_sio_write16(&pair->sio[1], GBA_SIO_REG_SEND, (uint16_t)second);
  }
  pair->network.expected_send[0] = first;
  pair->network.expected_send[1] = second;

  if(mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    control = (uint16_t)(0x6080u | setting);
    gba_sio_write16(&pair->sio[1], GBA_SIO_REG_CONTROL,
     (uint16_t)(control & ~0x0080u));
    gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL, control);
  }
  else
  {
    control = (uint16_t)(mode_bits | 0x4080u |
     (setting != 0 ? 2u : 0u));
    gba_sio_write16(&pair->sio[clock_owner ^ 1u], GBA_SIO_REG_CONTROL,
     control);
    gba_sio_write16(&pair->sio[clock_owner], GBA_SIO_REG_CONTROL,
     (uint16_t)(control | 1u));
  }

  state = pipeline_sio_state(pair, 0u);
  if(link_session_publish_sio_state(&pair->session[0], &state) !=
     LINK_SESSION_EVENT_NONE)
    return 0;
  state = pipeline_sio_state(pair, 1u);
  if(link_session_publish_sio_state(&pair->session[1], &state) !=
     LINK_SESSION_EVENT_NONE)
    return 0;
  if(link_session_publish_clock(&pair->session[0],
      pair->session[0].clock.local_cycle) != LINK_SESSION_EVENT_NONE ||
     link_session_publish_clock(&pair->session[1],
      pair->session[1].clock.local_cycle) != LINK_SESSION_EVENT_NONE)
    return 0;
  return 1;
}

static int pipeline_prepare_profile(PipelinePair *pair, GbaSioMode mode,
 uint8_t clock_owner, uint8_t setting)
{
  uint64_t deadline;

  pipeline_init(pair);
  pipeline_open(pair);
  if(!pipeline_wait_connected(pair, 6000000u))
    return 0;
  /* Preserve the exhaustive legacy state-space baseline independently from
     the focused v3 schedules above. */
  pair->session[0].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  pair->session[1].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  if(!pipeline_arm_profile(pair, mode, clock_owner, setting, 0u))
    return 0;

  deadline = pair->network.now_us + 2000000u;
  while(pair->network.now_us < deadline &&
        (!pair->session[0].peer_sio_valid ||
         !pair->session[1].peer_sio_valid ||
         !pair->session[0].clock.peer_valid ||
         !pair->session[1].clock.peer_valid))
  {
    pipeline_wire_step(&pair->network);
    pipeline_main_step(pair);
  }
  if(!pair->session[0].peer_sio_valid || !pair->session[1].peer_sio_valid ||
     !pair->session[0].clock.peer_valid ||
     !pair->session[1].clock.peer_valid)
    return 0;

  deadline = pair->network.now_us + 2000000u;
  while(pair->network.now_us < deadline && !pipeline_idle(pair))
  {
    pipeline_wire_step(&pair->network);
    pipeline_main_step(pair);
  }
  pair->network.wire_ordinal = 0;
  return pipeline_idle(pair) && pipeline_state_safe(pair);
}

static void test_deferred_prepare_survives_datagram_loss(void)
{
  PipelinePair pair;
  uint64_t deadline;
  uint32_t cycles;

  CHECK(pipeline_prepare_profile(&pair, GBA_SIO_MODE_MULTIPLAYER,
   0u, 3u));
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  deadline = pair.network.now_us + 2000000u;
  while(pair.network.now_us < deadline &&
        (pair.session[0].last_sequence != 1u ||
         pair.session[1].last_sequence != 1u ||
         pair.session[0].state != LINK_SESSION_CONNECTED ||
         pair.session[1].state != LINK_SESSION_CONNECTED))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);

  cycles = gba_sio_next_event(&pair.sio[0]);
  CHECK(cycles != GBA_SIO_NO_EVENT);
  CHECK(gba_sio_advance(&pair.sio[0], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_next_event(&pair.sio[1]) == cycles);

  pair.network.expected_send[0] = 0x3333u;
  pair.network.expected_send[1] = 0x4444u;
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 0x3333u);
  CHECK(gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL,
   0x6083u) == GBA_SIO_EVENT_START);
  pair.network.drop_data[0] = 1u;
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);

  deadline = pair.network.now_us + 2000000u;
  while(pair.network.now_us < deadline &&
        !pair.session[1].deferred_prepare_valid)
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pair.session[1].deferred_prepare_valid != 0);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(gba_sio_next_event(&pair.sio[1]) == cycles);

  CHECK(gba_sio_advance(&pair.sio[1], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_SEND, 0x4444u);
  pipeline_main_step(&pair);
  pipeline_main_step(&pair);

  deadline = pair.network.now_us + 2000000u;
  while(pair.network.now_us < deadline &&
        (pair.session[0].last_sequence != 2u ||
         pair.session[1].last_sequence != 2u ||
         pair.session[0].state != LINK_SESSION_CONNECTED ||
         pair.session[1].state != LINK_SESSION_CONNECTED))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
    CHECK(pipeline_state_safe(&pair));
  }
  CHECK(pair.session[0].last_sequence == 2u);
  CHECK(pair.session[1].last_sequence == 2u);
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(gba_sio_next_event(&pair.sio[0]) == cycles);
  CHECK(gba_sio_next_event(&pair.sio[1]) == cycles);
  CHECK(gba_sio_advance(&pair.sio[0], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_advance(&pair.sio[1], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(pair.registers[0][GBA_SIO_REG_MULTI0 >> 1] == 0x3333u);
  CHECK(pair.registers[0][GBA_SIO_REG_MULTI1 >> 1] == 0x4444u);
  CHECK(pair.registers[1][GBA_SIO_REG_MULTI0 >> 1] == 0x3333u);
  CHECK(pair.registers[1][GBA_SIO_REG_MULTI1 >> 1] == 0x4444u);
}

static int pipeline_verify_profile(PipelinePair *pair, GbaSioMode mode)
{
  uint32_t cycles = gba_sio_next_event(&pair->sio[0]);

  if(cycles == GBA_SIO_NO_EVENT ||
     gba_sio_next_event(&pair->sio[1]) != cycles ||
     gba_sio_advance(&pair->sio[0], cycles) != GBA_SIO_EVENT_SERIAL_IRQ ||
     gba_sio_advance(&pair->sio[1], cycles) != GBA_SIO_EVENT_SERIAL_IRQ)
    return 0;

  if(mode == GBA_SIO_MODE_MULTIPLAYER)
    return pair->registers[0][GBA_SIO_REG_MULTI0 >> 1] ==
      (uint16_t)pair->network.expected_send[0] &&
     pair->registers[0][GBA_SIO_REG_MULTI1 >> 1] ==
      (uint16_t)pair->network.expected_send[1] &&
     pair->registers[1][GBA_SIO_REG_MULTI0 >> 1] ==
      (uint16_t)pair->network.expected_send[0] &&
     pair->registers[1][GBA_SIO_REG_MULTI1 >> 1] ==
      (uint16_t)pair->network.expected_send[1];
  if(mode == GBA_SIO_MODE_NORMAL_32)
    return pair->registers[0][GBA_SIO_REG_DATA32_L >> 1] ==
      (uint16_t)pair->network.expected_send[1] &&
     pair->registers[0][GBA_SIO_REG_DATA32_H >> 1] ==
      (uint16_t)(pair->network.expected_send[1] >> 16) &&
     pair->registers[1][GBA_SIO_REG_DATA32_L >> 1] ==
      (uint16_t)pair->network.expected_send[0] &&
     pair->registers[1][GBA_SIO_REG_DATA32_H >> 1] ==
      (uint16_t)(pair->network.expected_send[0] >> 16);
  return pair->registers[0][GBA_SIO_REG_SEND >> 1] ==
    (uint16_t)(pair->network.expected_send[1] & 0xFFu) &&
   pair->registers[1][GBA_SIO_REG_SEND >> 1] ==
    (uint16_t)(pair->network.expected_send[0] & 0xFFu);
}

static int pipeline_prepare_stream_profile(PipelinePair *pair,
 uint32_t generation)
{
  uint64_t deadline;

  pipeline_init(pair);
  pipeline_open(pair);
  if(!pipeline_wait_connected(pair, 6000000u) ||
     !pipeline_arm_profile(pair, GBA_SIO_MODE_MULTIPLAYER, 0u, 3u,
      generation))
    return 0;
  deadline = pair->network.now_us + 2000000u;
  while(pair->network.now_us < deadline &&
        (!pair->session[0].peer_sio_valid ||
         !pair->session[1].peer_sio_valid ||
         !pair->session[0].clock.peer_valid ||
         !pair->session[1].clock.peer_valid || !pipeline_idle(pair)))
  {
    pipeline_wire_step(&pair->network);
    pipeline_main_step(pair);
  }
  return pair->session[0].peer_sio_valid &&
   pair->session[1].peer_sio_valid && pair->session[0].clock.peer_valid &&
   pair->session[1].clock.peer_valid && pipeline_idle(pair);
}

static int pipeline_rearm_stream_profile(PipelinePair *pair,
 uint32_t generation)
{
  uint64_t deadline;

  if(!pipeline_arm_profile(pair, GBA_SIO_MODE_MULTIPLAYER, 0u, 3u,
      generation))
    return 0;
  deadline = pair->network.now_us + 2000000u;
  while(pair->network.now_us < deadline && !pipeline_idle(pair))
  {
    pipeline_wire_step(&pair->network);
    pipeline_main_step(pair);
  }
  return pipeline_idle(pair);
}

static int pipeline_wait_stream_result(PipelinePair *pair,
 uint32_t sequence)
{
  uint64_t deadline = pair->network.now_us + 2000000u;

  while(pair->network.now_us < deadline &&
        (pair->session[0].last_sequence != sequence ||
         pair->session[1].last_sequence != sequence ||
         pair->session[0].state != LINK_SESSION_CONNECTED ||
         pair->session[1].state != LINK_SESSION_CONNECTED))
  {
    pipeline_wire_step(&pair->network);
    pipeline_main_step(pair);
  }
  return pair->session[0].last_sequence == sequence &&
   pair->session[1].last_sequence == sequence &&
   pair->session[0].state == LINK_SESSION_CONNECTED &&
   pair->session[1].state == LINK_SESSION_CONNECTED &&
   pipeline_verify_profile(pair, GBA_SIO_MODE_MULTIPLAYER);
}

static void test_stream_loss_duplicate_delay_and_backpressure(void)
{
  PipelinePair pair;
  uint64_t deadline;

  CHECK(pipeline_prepare_stream_profile(&pair, 0u));
  pair.network.drop_data[0] = 1u;
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pipeline_wait_stream_result(&pair, 1u));
  CHECK(pair.network.endpoints[0].datagram.retransmissions != 0u);
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 1u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 1u);

  CHECK(pipeline_prepare_stream_profile(&pair, 0u));
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pipeline_wire_step(&pair.network);
  pair.network.drop_data[1] = 1u;
  pipeline_main_step(&pair);
  CHECK(pipeline_wait_stream_result(&pair, 1u));
  CHECK(pair.network.endpoints[1].datagram.retransmissions != 0u);
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 1u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 1u);

  CHECK(pipeline_prepare_stream_profile(&pair, 0u));
  pair.network.capture_next_message[0][LINK_MESSAGE_TRANSFER_START] = 1u;
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pipeline_wait_stream_result(&pair, 1u));
  CHECK(pair.network.captured_valid != 0u);
  CHECK(pipeline_replay_captured(&pair));
  deadline = pair.network.now_us + 1000000u;
  while(pair.network.now_us < deadline &&
        link_stream_replay_count(&pair.session[1].stream) == 0u)
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(link_stream_latch_count(&pair.session[1].stream) == 1u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 1u);
  CHECK(link_stream_replay_count(&pair.session[1].stream) != 0u);
  CHECK(pair.network.sent_message[1][LINK_MESSAGE_TRANSFER_REPLY] == 2u);
  CHECK(link_stream_reply_cache_depth(&pair.session[1].stream) <= 1u);

  CHECK(pipeline_prepare_stream_profile(&pair, 0u));
  pair.network.capture_next_message[1][LINK_MESSAGE_TRANSFER_REPLY] = 1u;
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pipeline_wait_stream_result(&pair, 1u));
  CHECK(pair.network.captured_valid != 0u);
  CHECK(pipeline_replay_captured(&pair));
  deadline = pair.network.now_us + 1000000u;
  while(pair.network.now_us < deadline &&
        pair.session[0].stream.duplicate_count == 0u)
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 1u);
  CHECK(pair.session[0].stream.duplicate_count != 0u);

  CHECK(pipeline_prepare_stream_profile(&pair, 0u));
  pair.network.capture_next_message[1][LINK_MESSAGE_TRANSFER_REPLY] = 1u;
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pipeline_wait_stream_result(&pair, 1u));
  CHECK(pair.network.captured_valid != 0u);
  CHECK(pipeline_rearm_stream_profile(&pair, 1u));
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pipeline_wait_stream_result(&pair, 2u));
  CHECK(pipeline_replay_captured(&pair));
  deadline = pair.network.now_us + 1000000u;
  while(pair.network.now_us < deadline && !pipeline_idle(&pair))
  {
    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
  }
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 2u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 2u);

  CHECK(pipeline_prepare_stream_profile(&pair, 0u));
  pair.network.faults[0].ordinal = pair.network.wire_ordinal;
  pair.network.faults[0].action = PIPELINE_FAULT_BACKPRESSURE;
  pair.network.fault_count = 1u;
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pipeline_wait_stream_result(&pair, 1u));
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 1u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 1u);
}

static int pipeline_transfer_complete(const PipelinePair *pair,
 uint32_t sequence)
{
  if(pair->session[0].state != LINK_SESSION_CONNECTED ||
     pair->session[1].state != LINK_SESSION_CONNECTED ||
     pair->session[0].last_sequence != sequence ||
     pair->session[1].last_sequence != sequence)
    return 0;
  return pair->session[0].transaction_stage ==
   LINK_TRANSACTION_ACKNOWLEDGED &&
   pair->session[1].transaction_stage ==
   LINK_TRANSACTION_ACKNOWLEDGED &&
   pair->session[0].transaction_applied &&
   pair->session[1].transaction_applied;
}

static int pipeline_transfer_succeeded(PipelinePair *pair,
 GbaSioMode mode, uint32_t sequence)
{
  return pipeline_transfer_complete(pair, sequence) &&
   pipeline_verify_profile(pair, mode);
}

static int pipeline_explore_fault_closure(const PipelinePair *input,
 GbaSioMode mode, uint8_t clock_owner, uint8_t setting,
 unsigned int faults_remaining, uint32_t goal_sequence,
 int wait_between, uint64_t deadline)
{
  PipelinePair pair = *input;

  pipeline_rebind(&pair);
  while(pair.network.now_us < deadline)
  {
    PipelinePair probe;
    unsigned int prepared;
    unsigned int ordinal;
    unsigned int action0;
    unsigned int action1;

    if(!pipeline_state_safe(&pair) ||
       pair.session[0].state == LINK_SESSION_ERROR ||
       pair.session[1].state == LINK_SESSION_ERROR)
    {
      fprintf(stderr, "fault closure failed mode=%u owner=%u setting=%u remaining=%u H=%u/%u/%u J=%u/%u/%u\n",
       (unsigned int)mode, clock_owner, setting, faults_remaining,
       pair.session[0].state, pair.session[0].last_sequence,
       pair.session[0].transaction_stage,
       pair.session[1].state, pair.session[1].last_sequence,
       pair.session[1].transaction_stage);
      return 0;
    }
    if(pipeline_transfer_complete(&pair, goal_sequence))
    {
      uint32_t events;

      if(goal_sequence == 1u)
      {
        if(wait_between && !pipeline_idle(&pair))
        {
          /* Keep exploring faults in the trailing acknowledgement train. */
        }
        else
        {
          if(!pipeline_transfer_succeeded(&pair, mode, 1u) ||
             !pipeline_arm_profile(&pair, mode, clock_owner, setting, 1u))
            return 0;
          events = link_session_begin_transfer(
           &pair.session[clock_owner]);
          if((events & LINK_SESSION_EVENT_BARRIER_BEGIN) == 0 ||
             !pipeline_state_safe(&pair))
            return 0;
          goal_sequence = 2u;
          continue;
        }
      }
      else if(pipeline_idle(&pair))
      {
        if(!pipeline_transfer_succeeded(&pair, mode, 2u))
          return 0;
        pipeline_schedule_count++;
        pipeline_fault_schedule_count++;
        return 1;
      }
    }

    probe = pair;
    pipeline_rebind(&probe);
    probe.network.fault_count = 0;
    ordinal = probe.network.wire_ordinal;
    pipeline_wire_step(&probe.network);
    prepared = probe.network.wire_ordinal - ordinal;
    if(prepared == 0u)
    {
      pair = probe;
      pipeline_rebind(&pair);
      pipeline_main_step(&pair);
      continue;
    }
    if(prepared > 2u)
      return 0;

    for(action0 = PIPELINE_FAULT_NONE;
        action0 <= PIPELINE_FAULT_BACKPRESSURE; action0++)
    {
      unsigned int last_action = prepared == 2u ?
       PIPELINE_FAULT_BACKPRESSURE : PIPELINE_FAULT_NONE;

      for(action1 = PIPELINE_FAULT_NONE; action1 <= last_action; action1++)
      {
        PipelinePair branch;
        unsigned int used = (action0 != PIPELINE_FAULT_NONE) +
         (action1 != PIPELINE_FAULT_NONE);

        if(used > faults_remaining)
          continue;
        branch = pair;
        pipeline_rebind(&branch);
        memset(branch.network.faults, 0, sizeof(branch.network.faults));
        branch.network.fault_count = 0;
        if(action0 != PIPELINE_FAULT_NONE)
        {
          PipelineFault *fault = &branch.network.faults[
           branch.network.fault_count++];

          fault->ordinal = ordinal;
          fault->action = (PipelineFaultAction)action0;
        }
        if(action1 != PIPELINE_FAULT_NONE)
        {
          PipelineFault *fault = &branch.network.faults[
           branch.network.fault_count++];

          fault->ordinal = ordinal + 1u;
          fault->action = (PipelineFaultAction)action1;
        }
        pipeline_wire_step(&branch.network);
        branch.network.fault_count = 0;
        pipeline_main_step(&branch);
        if(!pipeline_explore_fault_closure(&branch, mode, clock_owner,
           setting, faults_remaining - used, goal_sequence,
           wait_between, deadline))
          return 0;
      }
    }
    return 1;
  }

  fprintf(stderr, "fault closure timeout mode=%u owner=%u setting=%u remaining=%u\n",
   (unsigned int)mode, clock_owner, setting, faults_remaining);
  return 0;
}

static int pipeline_run_fault_closure(const PipelinePair *base,
 GbaSioMode mode, uint8_t clock_owner, uint8_t setting,
 int wait_between)
{
  PipelinePair pair = *base;
  uint32_t events;

  pipeline_rebind(&pair);
  memset(pair.network.faults, 0, sizeof(pair.network.faults));
  pair.network.fault_count = 0;
  pair.network.wire_ordinal = 0;
  pair.network.collide_all = 1;
  memset(pair.network.delayed_valid, 0,
   sizeof(pair.network.delayed_valid));

  events = link_session_begin_transfer(&pair.session[clock_owner]);
  if((events & LINK_SESSION_EVENT_BARRIER_BEGIN) == 0 ||
     !pipeline_state_safe(&pair))
    return 0;
  return pipeline_explore_fault_closure(&pair, mode, clock_owner, setting,
   PIPELINE_FAULT_BUDGET, 1u, wait_between,
   pair.network.now_us + 8000000u);
}

static int pipeline_run_recovery_schedule(const PipelinePair *base,
 GbaSioMode mode, uint8_t clock_owner, uint8_t setting,
 unsigned int recovery_step, uint64_t outage_us,
 int recovery_initiator, uint64_t recovery_skew_us,
 const PipelineFault *faults, unsigned int fault_count,
 unsigned int *wire_count)
{
  PipelinePair pair = *base;
  uint64_t deadline;
  uint32_t events;
  uint32_t target_sequence = 1u;
  unsigned int step = 0;
  int recovered = 0;
  int retried = 0;

  pipeline_rebind(&pair);
  if(fault_count > PIPELINE_FAULT_CAPACITY)
    return 0;
  memset(pair.network.faults, 0, sizeof(pair.network.faults));
  if(fault_count != 0)
    memcpy(pair.network.faults, faults, fault_count * sizeof(*faults));
  pair.network.fault_count = fault_count;
  pair.network.wire_ordinal = 0;
  pair.network.collide_all = 1;
  memset(pair.network.delayed_valid, 0,
   sizeof(pair.network.delayed_valid));

  events = link_session_begin_transfer(&pair.session[clock_owner]);
  if((events & LINK_SESSION_EVENT_BARRIER_BEGIN) == 0 ||
     !pipeline_state_safe(&pair))
    return 0;

  deadline = pair.network.now_us + outage_us + 6000000u;
  while(pair.network.now_us < deadline)
  {
    if(!recovered && step == recovery_step)
    {
      if(recovery_initiator >= 0)
      {
        unsigned int initiator = (unsigned int)recovery_initiator;

        if(initiator >= 2u)
          return 0;
        pipeline_recovery_endpoint(&pair, initiator, 1);
        pipeline_main_step(&pair);
        if(!pipeline_state_safe(&pair))
          return 0;
        pair.network.now_us += recovery_skew_us;
        pipeline_recovery_endpoint(&pair, initiator ^ 1u, 1);
        pipeline_main_step(&pair);
      }
      else
      {
        pipeline_recovery(&pair, 1);
      }
      if(!pipeline_state_safe(&pair))
        return 0;
      pair.network.now_us += outage_us;
      pipeline_recovery(&pair, 0);
      if(!pipeline_state_safe(&pair))
        return 0;
      recovered = 1;
    }

    pipeline_wire_step(&pair.network);
    pipeline_main_step(&pair);
    if(!pipeline_state_safe(&pair) ||
       pair.session[0].state == LINK_SESSION_ERROR ||
       pair.session[1].state == LINK_SESSION_ERROR)
    {
      fprintf(stderr, "recovery exploration failed mode=%u owner=%u setting=%u step=%u outage=%llu H=%u/%u/%u J=%u/%u/%u reason=%u/%u detail=%u/%u terminal=%u/%u\n",
       (unsigned int)mode, clock_owner, setting, recovery_step,
       (unsigned long long)outage_us,
       pair.session[0].state, pair.session[0].last_sequence,
       pair.session[0].transaction_stage,
       pair.session[1].state, pair.session[1].last_sequence,
       pair.session[1].transaction_stage,
       pair.session[0].reason, pair.session[1].reason,
       pair.session[0].reason_detail, pair.session[1].reason_detail,
       pair.session[0].terminal_message_type,
       pair.session[1].terminal_message_type);
      return 0;
    }
    if(recovered && pair.session[0].state == LINK_SESSION_CONNECTED &&
       pair.session[1].state == LINK_SESSION_CONNECTED &&
       pair.session[0].last_sequence == target_sequence &&
       pair.session[1].last_sequence == target_sequence)
    {
      if(pair.session[0].transaction_stage == LINK_TRANSACTION_ABORTED &&
         pair.session[1].transaction_stage == LINK_TRANSACTION_ABORTED &&
         !pair.session[0].transaction_applied &&
         !pair.session[1].transaction_applied && !retried)
      {
        if(!pipeline_arm_profile(&pair, mode, clock_owner, setting, 1u))
          return 0;
        events = link_session_begin_transfer(&pair.session[clock_owner]);
        if((events & LINK_SESSION_EVENT_BARRIER_BEGIN) == 0 ||
           !pipeline_state_safe(&pair))
          return 0;
        target_sequence++;
        retried = 1;
        deadline = pair.network.now_us + 4000000u;
        continue;
      }
      if(!pipeline_idle(&pair))
      {
        step++;
        continue;
      }
      if(pipeline_transfer_succeeded(&pair, mode, target_sequence))
      {
        if(wire_count != NULL)
          *wire_count = pair.network.wire_ordinal;
        return 1;
      }
      fprintf(stderr, "recovery exploration inconsistent completion mode=%u owner=%u setting=%u step=%u outage=%llu H=%u/%u/%u J=%u/%u/%u cycles=%u/%u data=%04x/%04x\n",
       (unsigned int)mode, clock_owner, setting, recovery_step,
       (unsigned long long)outage_us,
       pair.session[0].state, pair.session[0].transaction_stage,
       pair.session[0].transaction_applied,
       pair.session[1].state, pair.session[1].transaction_stage,
       pair.session[1].transaction_applied,
       gba_sio_next_event(&pair.sio[0]),
       gba_sio_next_event(&pair.sio[1]),
       pair.registers[0][GBA_SIO_REG_SEND >> 1],
       pair.registers[1][GBA_SIO_REG_SEND >> 1]);
      return 0;
    }
    step++;
  }

  fprintf(stderr, "recovery exploration timeout mode=%u owner=%u setting=%u step=%u outage=%llu\n",
   (unsigned int)mode, clock_owner, setting, recovery_step,
   (unsigned long long)outage_us);
  return 0;
}

static int pipeline_explore_profile(GbaSioMode mode, uint8_t clock_owner,
 uint8_t setting)
{
  PipelinePair base;
  PipelineFault fault;
  static const uint64_t outages[] = { 0u, 5000000u, 14000000u };
  static const uint64_t recovery_skews[] = { 50000u, 500000u };
  unsigned int action;
  unsigned int initiator;
  unsigned int ordinal;
  unsigned int recovery_step;
  unsigned int skew;
  unsigned int outage;

  if(!pipeline_prepare_profile(&base, mode, clock_owner, setting))
    return 0;
  if(!pipeline_run_fault_closure(&base, mode, clock_owner, setting, 0) ||
     !pipeline_run_fault_closure(&base, mode, clock_owner, setting, 1))
    return 0;

  for(recovery_step = 0; recovery_step < 9u; recovery_step++)
  {
    for(outage = 0; outage < sizeof(outages) / sizeof(outages[0]); outage++)
    {
      unsigned int wire_count = 0;

      if(!pipeline_run_recovery_schedule(&base, mode, clock_owner, setting,
         recovery_step, outages[outage], -1, 0u, NULL, 0u, &wire_count))
        return 0;
      pipeline_schedule_count++;
      pipeline_recovery_schedule_count++;
      for(ordinal = 0; ordinal < wire_count; ordinal++)
      {
        for(action = PIPELINE_FAULT_DROP;
            action <= PIPELINE_FAULT_BACKPRESSURE; action++)
        {
          fault.ordinal = ordinal;
          fault.action = (PipelineFaultAction)action;
          if(!pipeline_run_recovery_schedule(&base, mode, clock_owner,
             setting, recovery_step, outages[outage], -1, 0u,
             &fault, 1u, NULL))
            return 0;
          pipeline_schedule_count++;
          pipeline_recovery_schedule_count++;
        }
      }
    }
    for(initiator = 0; initiator < 2u; initiator++)
    {
      for(skew = 0; skew < sizeof(recovery_skews) /
          sizeof(recovery_skews[0]); skew++)
      {
        for(outage = 0; outage < sizeof(outages) /
            sizeof(outages[0]); outage++)
        {
          if(!pipeline_run_recovery_schedule(&base, mode, clock_owner,
             setting, recovery_step, outages[outage], (int)initiator,
             recovery_skews[skew], NULL, 0u, NULL))
            return 0;
          pipeline_schedule_count++;
          pipeline_recovery_schedule_count++;
        }
      }
    }
  }
  return 1;
}

static void test_explicit_link_state_space(void)
{
  unsigned int setting;
  unsigned int owner;

  memset(pipeline_states, 0, sizeof(pipeline_states));
  pipeline_state_count = 0;
  pipeline_schedule_count = 0;
  pipeline_fault_schedule_count = 0;
  pipeline_recovery_schedule_count = 0;

  for(setting = 0; setting < 4u; setting++)
    CHECK(pipeline_explore_profile(GBA_SIO_MODE_MULTIPLAYER, 0u,
     (uint8_t)setting));
  for(owner = 0; owner < 2u; owner++)
  {
    for(setting = 0; setting < 2u; setting++)
    {
      CHECK(pipeline_explore_profile(GBA_SIO_MODE_NORMAL_8,
       (uint8_t)owner, (uint8_t)setting));
      CHECK(pipeline_explore_profile(GBA_SIO_MODE_NORMAL_32,
       (uint8_t)owner, (uint8_t)setting));
    }
  }
  CHECK(pipeline_fault_schedule_count == 5134792u);
  /* Presence refresh after 5/14-second outages adds 752 packet opportunities,
     each explored with four faults. Disabling only that refresh restores the
     former 21,780 recovery schedules and 67,753 state signatures. */
  CHECK(pipeline_recovery_schedule_count == 24788u);
  CHECK(pipeline_schedule_count == 5159580u);
  CHECK(pipeline_schedule_count == pipeline_fault_schedule_count +
   pipeline_recovery_schedule_count);
  CHECK(pipeline_state_count == 120593u);
  printf("Explored %u link schedules (%u fault, %u recovery) across %u states\n",
   pipeline_schedule_count, pipeline_fault_schedule_count,
   pipeline_recovery_schedule_count, pipeline_state_count);
}

int main(int argc, char **argv)
{
  if(argc == 2 && strcmp(argv[1], "--stream-only") == 0)
  {
    test_transactional_transfer_survives_datagram_loss();
    test_multiplayer_nine_word_stream();
    test_stream_loss_duplicate_delay_and_backpressure();
    if(failures != 0)
    {
      fprintf(stderr, "%d stream pipeline test(s) failed\n", failures);
      return 1;
    }
    puts("Link stream pipeline tests passed");
    return 0;
  }

  test_async_handshake_after_transport_ack();
  test_loss_and_bidirectional_half_duplex_collision_recover();
  test_transactional_transfer_survives_datagram_loss();
  test_multiplayer_nine_word_stream();
  test_stream_loss_duplicate_delay_and_backpressure();
  test_deferred_prepare_survives_datagram_loss();
  test_resume_survives_five_second_datagram_outage();
  test_prepared_joiner_recovers_physical_failure_race();
  test_host_armed_joiner_empty_uses_symmetric_recovery();
  test_explicit_link_state_space();

  if(failures != 0)
  {
    fprintf(stderr, "%d link pipeline test(s) failed\n", failures);
    return 1;
  }
  puts("Link pipeline tests passed");
  return 0;
}
