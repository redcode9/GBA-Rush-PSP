/* Original register-level workload, not a game ROM or a FireRed emulator.
 * Exercise the production PSP lanes/scheduler with independent guest clocks,
 * serial IRQ handlers and the documented nine-word/Timer3 command cadence.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_link_transport.h"
#include "link_session.h"
#include "link_transport_scheduler.h"

#define WORD_COUNT 180u
#define WIRE_CAPACITY 64u
#define TICK_US 100u
#define SERIAL_HANDLER_CYCLES 128u
#define TIMER3_CYCLES (197u * 64u)
#define FRAME_CYCLES 280896u

#define REQUIRE(condition) do { if(!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
  exit(1); } } while(0)

typedef struct
{
  LinkTransportScheduler scheduler;
  LinkTransport event_transport;
  LinkSession session;
  GbaSio sio;
  uint16_t registers[GBA_SIO_REGISTER_BYTES / 2];
  uint32_t cycle;
  uint32_t completed;
  uint32_t handler_cycle;
  uint32_t next_start_cycle;
  uint32_t burst_start_cycle;
  uint8_t handler_pending;
  uint8_t start_pending;
} RuntimeEndpoint;

typedef struct
{
  LinkLanesPacket packet;
  uint64_t delivery_us;
  unsigned int receiver;
  int occupied;
} WirePacket;

typedef struct
{
  FakeLinkNetwork network;
  RuntimeEndpoint endpoints[2];
  WirePacket wire[WIRE_CAPACITY];
  uint16_t expected[WORD_COUNT][2];
  uint64_t latency_us[2];
  uint32_t sent_packets;
  uint32_t dropped_packets;
  uint32_t duplicated_packets;
  int inject_faults;
} RuntimePair;

static int runtime_host(void *context)
{
  RuntimeEndpoint *endpoint = context;
  return endpoint->event_transport.ops->host(endpoint->event_transport.context);
}

static int runtime_join(void *context)
{
  RuntimeEndpoint *endpoint = context;
  return endpoint->event_transport.ops->join(endpoint->event_transport.context);
}

static uint64_t runtime_now(void *context)
{
  RuntimeEndpoint *endpoint = context;
  return endpoint->event_transport.ops->now_us(endpoint->event_transport.context);
}

static int runtime_send(void *context, const uint8_t *data, size_t size)
{
  RuntimeEndpoint *endpoint = context;
  LinkPacket packet;
  LinkReplaceableClass state_class = LINK_STATE_NONE;

  REQUIRE(link_protocol_decode(data, size, &packet) == LINK_PROTOCOL_OK);
  if(packet.type == LINK_MESSAGE_SIO_STATE)
    state_class = LINK_STATE_SIO;
  else if(packet.type == LINK_MESSAGE_HEARTBEAT)
    state_class = LINK_STATE_HEARTBEAT;
  else if(packet.type == LINK_MESSAGE_CLOCK_SYNC)
    state_class = LINK_STATE_CLOCK;
  return link_transport_scheduler_queue(&endpoint->scheduler,
   state_class == LINK_STATE_NONE ? LINK_LANE_CRITICAL : LINK_LANE_REPLACEABLE,
   state_class, runtime_now(endpoint), data, size);
}

static int runtime_poll(void *context, LinkTransportEvent *event)
{
  RuntimeEndpoint *endpoint = context;
  return endpoint->event_transport.ops->poll(endpoint->event_transport.context,
   event);
}

static int runtime_recover(void *context, int reason, LinkTimeoutSource source)
{
  (void)context;
  fprintf(stderr, "unexpected recovery: reason=%d source=%d\n", reason, source);
  return 0;
}

static void runtime_disconnect(void *context)
{
  RuntimeEndpoint *endpoint = context;
  endpoint->event_transport.ops->disconnect(endpoint->event_transport.context);
}

static const LinkTransportOps runtime_ops =
{
  runtime_host, runtime_join, runtime_send, runtime_poll, runtime_recover,
  runtime_disconnect, runtime_now
};

static void queue_wire_packet(RuntimePair *pair, unsigned int sender,
 const LinkLanesPacket *packet, uint64_t extra_delay_us)
{
  unsigned int index;

  for(index = 0; index < WIRE_CAPACITY; index++)
  {
    WirePacket *wire = &pair->wire[index];
    if(wire->occupied)
      continue;
    wire->packet = *packet;
    wire->delivery_us = pair->network.now_us + pair->latency_us[sender] +
     extra_delay_us;
    wire->receiver = sender ^ 1u;
    wire->occupied = 1;
    return;
  }
  REQUIRE(0); /* A full wire fixture must fail, never silently drop work. */
}

static void runtime_wire_step(RuntimePair *pair)
{
  unsigned int index;

  for(index = 0; index < WIRE_CAPACITY; index++)
  {
    WirePacket *wire = &pair->wire[index];
    LinkSpscItem delivery;
    LinkLanesReceiveResult result;

    if(!wire->occupied || wire->delivery_us > pair->network.now_us)
      continue;
    result = link_transport_scheduler_receive(
     &pair->endpoints[wire->receiver].scheduler, wire->packet.data,
     wire->packet.size, pair->network.now_us, &delivery);
    REQUIRE(result != LINK_LANES_INVALID && result != LINK_LANES_CONFLICT);
    if(result == LINK_LANES_DELIVERED)
      REQUIRE(fake_link_network_inject(&pair->network, (uint8_t)wire->receiver,
       delivery.data, delivery.size, 0));
    wire->occupied = 0;
  }
  for(index = 0; index < 2u; index++)
  {
    LinkTransportScheduler *scheduler = &pair->endpoints[index].scheduler;
    LinkLanesPacket packet;
    int prepared = link_transport_scheduler_prepare(scheduler,
     pair->network.now_us, &packet);

    REQUIRE(prepared >= 0);
    if(!prepared)
      continue;
    if(pair->inject_faults && pair->network.now_us % 9700u == 0)
    {
      REQUIRE(link_transport_scheduler_note_blocked(scheduler, &packet,
       pair->network.now_us));
      continue;
    }
    REQUIRE(link_transport_scheduler_note_sent(scheduler, &packet,
     pair->network.now_us));
    pair->sent_packets++;
    if(pair->inject_faults && pair->sent_packets % 17u == 0)
    {
      pair->dropped_packets++;
      continue;
    }
    queue_wire_packet(pair, index, &packet, 0);
    if(pair->inject_faults && pair->sent_packets % 11u == 0)
    {
      queue_wire_packet(pair, index, &packet, 1300u);
      pair->duplicated_packets++;
    }
  }
}

static void runtime_check_session(const RuntimeEndpoint *endpoint)
{
  if(endpoint->session.state == LINK_SESSION_ERROR)
    fprintf(stderr, "session error role=%u reason=%u detail=%u sequence=%u\n",
     endpoint->session.role, endpoint->session.reason,
     endpoint->session.reason_detail, endpoint->session.last_sequence);
  REQUIRE(endpoint->session.state != LINK_SESSION_ERROR);
  REQUIRE(endpoint->session.state != LINK_SESSION_DISCONNECTED);
  REQUIRE(link_session_check_invariants(&endpoint->session) == 0);
}

static void runtime_init(RuntimePair *pair, int faults)
{
  LinkHello hello;
  unsigned int index;
  uint16_t checksum = 0;

  memset(pair, 0, sizeof(*pair));
  memset(&hello, 0, sizeof(hello));
  hello.capabilities = LINK_SESSION_SUPPORTED_CAPABILITIES;
  hello.max_players = 2;
  hello.rom_size = 1;
  hello.rom_crc32 = 1;
  pair->latency_us[0] = 300u;
  pair->latency_us[1] = 700u;
  pair->inject_faults = faults;
  fake_link_network_init(&pair->network,
   &pair->endpoints[0].event_transport, &pair->endpoints[1].event_transport);
  for(index = 0; index < 2u; index++)
  {
    RuntimeEndpoint *endpoint = &pair->endpoints[index];
    LinkTransport transport = { &runtime_ops, endpoint };

    gba_sio_init(&endpoint->sio, endpoint->registers);
    link_transport_scheduler_init(&endpoint->scheduler);
    link_transport_scheduler_reset(&endpoint->scheduler, 0x12345678u, 1u);
    REQUIRE(link_transport_scheduler_set_profile(&endpoint->scheduler,
     LINK_TRANSPORT_PROFILE_HOT, 500u));
    link_session_init(&endpoint->session, &endpoint->sio, transport, &hello);
  }
  REQUIRE(link_session_host(&pair->endpoints[0].session, 0x12345678u));
  REQUIRE(link_session_join(&pair->endpoints[1].session));
  for(index = 0; index < 20000u; index++)
  {
    runtime_wire_step(pair);
    link_session_pump(&pair->endpoints[0].session);
    link_session_pump(&pair->endpoints[1].session);
    if(pair->endpoints[0].session.state == LINK_SESSION_CONNECTED &&
       pair->endpoints[1].session.state == LINK_SESSION_CONNECTED)
      break;
    fake_link_network_advance(&pair->network, TICK_US);
  }
  REQUIRE(index != 20000u);
  for(index = 0; index < WORD_COUNT; index++)
  {
    if(index % 9u == 0)
    {
      pair->expected[index][0] = checksum;
      pair->expected[index][1] = checksum;
      checksum = 0;
    }
    else
    {
      pair->expected[index][0] = (uint16_t)(0x1234u + index * 57u);
      pair->expected[index][1] = (uint16_t)(0xABCDu ^ index * 31u);
      checksum = (uint16_t)(checksum + pair->expected[index][0] +
       pair->expected[index][1]);
    }
  }
}

static void runtime_serial_irq(RuntimePair *pair, unsigned int player)
{
  RuntimeEndpoint *endpoint = &pair->endpoints[player];
  unsigned int index;

  REQUIRE(!endpoint->handler_pending);
  REQUIRE(endpoint->completed < WORD_COUNT);
  for(index = 0; index < 2u; index++)
    REQUIRE(endpoint->registers[(GBA_SIO_REG_MULTI0 >> 1) + index] ==
     pair->expected[endpoint->completed][index]);
  REQUIRE(endpoint->registers[GBA_SIO_REG_MULTI2 >> 1] == 0xFFFFu);
  REQUIRE(endpoint->registers[GBA_SIO_REG_MULTI3 >> 1] == 0xFFFFu);
  endpoint->completed++;
  endpoint->handler_pending = 1;
  endpoint->handler_cycle = endpoint->cycle + SERIAL_HANDLER_CYCLES;
}

static void runtime_limit_cycles(uint32_t *cycles, uint32_t deadline,
 uint32_t current)
{
  uint32_t remaining = deadline - current;
  if(remaining < *cycles)
    *cycles = remaining;
}

static void runtime_guest_step(RuntimePair *pair, unsigned int player,
 uint32_t cycle_budget)
{
  RuntimeEndpoint *endpoint = &pair->endpoints[player];
  LinkSession *session = &endpoint->session;
  LinkSioState presence;
  uint32_t events;
  uint32_t cycles = cycle_budget;

  if(endpoint->handler_pending && endpoint->cycle == endpoint->handler_cycle)
  {
    endpoint->handler_pending = 0;
    if(endpoint->completed < WORD_COUNT)
    {
      gba_sio_write16(&endpoint->sio, GBA_SIO_REG_SEND,
       pair->expected[endpoint->completed][player]);
      if(player == 0)
      {
        endpoint->next_start_cycle = endpoint->completed % 9u == 0 ?
         endpoint->burst_start_cycle + FRAME_CYCLES :
         endpoint->cycle + TIMER3_CYCLES;
        endpoint->start_pending = 1;
      }
    }
  }
  if(player == 0 && endpoint->start_pending &&
     endpoint->cycle == endpoint->next_start_cycle)
  {
    if(endpoint->completed % 9u == 0)
      endpoint->burst_start_cycle = endpoint->cycle;
    gba_sio_write16(&endpoint->sio, GBA_SIO_REG_CONTROL, 0x6083u);
    endpoint->start_pending = 0;
  }
  memset(&presence, 0, sizeof(presence));
  presence.mode = GBA_SIO_MODE_MULTIPLAYER;
  presence.player_count = 2;
  presence.player_id = (uint8_t)player;
  presence.flags = LINK_SIO_FLAG_GAMEPLAY_ACTIVE;
  presence.control = endpoint->registers[GBA_SIO_REG_CONTROL >> 1];
  presence.rcnt = endpoint->registers[GBA_SIO_REG_RCNT >> 1];
  link_session_set_emulated_cycle(session, endpoint->cycle);
  link_session_publish_sio_state(session, &presence);
  events = link_session_pump(session);
  link_session_publish_clock(session, endpoint->cycle);
  events |= link_session_begin_transfer(session);
  if(events & LINK_SESSION_EVENT_SERIAL_IRQ)
    runtime_serial_irq(pair, player);
  runtime_check_session(endpoint);
  if(link_session_pause_reason(session) != LINK_SESSION_PAUSE_NONE)
    return;

  if(endpoint->handler_pending)
    runtime_limit_cycles(&cycles, endpoint->handler_cycle, endpoint->cycle);
  if(endpoint->start_pending)
    runtime_limit_cycles(&cycles, endpoint->next_start_cycle, endpoint->cycle);
  if(gba_sio_next_event(&endpoint->sio) < cycles)
    cycles = gba_sio_next_event(&endpoint->sio);
  if(link_session_cycles_until_pause(session) < cycles)
    cycles = link_session_cycles_until_pause(session);
  endpoint->cycle += cycles;
  if(gba_sio_advance(&endpoint->sio, cycles) & GBA_SIO_EVENT_SERIAL_IRQ)
    runtime_serial_irq(pair, player);
}

static void test_independent_guest_clocks(int faults, int slow_host)
{
  RuntimePair pair;
  unsigned int player;
  uint32_t tick;

  runtime_init(&pair, faults);
  for(player = 0; player < 2u; player++)
  {
    gba_sio_write16(&pair.endpoints[player].sio, GBA_SIO_REG_RCNT, 0);
    gba_sio_write16(&pair.endpoints[player].sio, GBA_SIO_REG_CONTROL, 0x6003u);
    gba_sio_write16(&pair.endpoints[player].sio, GBA_SIO_REG_SEND,
     pair.expected[0][player]);
  }
  pair.endpoints[0].start_pending = 1;
  for(tick = 0; tick < 500000u; tick++)
  {
    runtime_wire_step(&pair);
    runtime_guest_step(&pair, 0, slow_host ? 300u : 700u);
    runtime_guest_step(&pair, 1, slow_host ? 700u : 300u);
    if(pair.endpoints[0].completed == WORD_COUNT &&
       pair.endpoints[1].completed == WORD_COUNT)
      break;
    fake_link_network_advance(&pair.network, TICK_US);
  }
  REQUIRE(tick != 500000u);
  for(player = 0; player < 2u; player++)
  {
    REQUIRE(link_stream_apply_count(&pair.endpoints[player].session.stream) ==
     WORD_COUNT);
    REQUIRE(pair.endpoints[player].session.legacy_transfer_tx_count == 0);
    REQUIRE(pair.endpoints[player].scheduler.lanes.rejected == 0);
    REQUIRE(pair.network.recovery_requests[player] == 0);
  }
  if(faults)
  {
    REQUIRE(pair.dropped_packets != 0);
    REQUIRE(pair.duplicated_packets != 0);
  }
}

int main(void)
{
  test_independent_guest_clocks(0, 0);
  test_independent_guest_clocks(0, 1);
  test_independent_guest_clocks(1, 0);
  test_independent_guest_clocks(1, 1);
  puts("Link cable runtime tests passed (production scheduler, 720 words)");
  return 0;
}
