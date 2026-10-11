#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_link_transport.h"
#include "gba_sio.h"
#include "link_protocol.h"
#include "link_session.h"

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

static int session_health_is(const LinkSession *session,
 LinkTransportHealth health)
{
  return session->transport_health == health;
}

typedef struct
{
  uint16_t registers[2][GBA_SIO_REGISTER_BYTES / 2];
  GbaSio sio[2];
  FakeLinkNetwork network;
  LinkTransport transport[2];
  LinkSession session[2];
} LinkPair;

static void check_recovered_transfer(LinkPair *pair);

static LinkHello make_hello(uint32_t crc, const char *title,
 const char *code)
{
  LinkHello hello;

  memset(&hello, 0, sizeof(hello));
  hello.capabilities = LINK_SESSION_REQUIRED_CAPABILITIES;
  hello.rom_crc32 = crc;
  hello.rom_size = 0x01000000;
  memcpy(hello.title, title, strlen(title) < sizeof(hello.title) ?
   strlen(title) : sizeof(hello.title));
  memcpy(hello.game_code, code, sizeof(hello.game_code));
  hello.max_players = 2;
  return hello;
}

static uint32_t pump_pair(LinkPair *pair, uint32_t rounds)
{
  uint32_t events = 0;
  uint32_t i;

  for(i = 0; i < rounds; i++)
  {
    events |= link_session_pump(&pair->session[0]);
    events |= link_session_pump(&pair->session[1]);
  }
  return events;
}

static LinkSioState pair_sio_state(const LinkPair *pair, uint8_t endpoint,
 int gameplay_active)
{
  const GbaSio *sio = &pair->sio[endpoint];
  LinkSioState state;

  memset(&state, 0, sizeof(state));
  state.mode = (uint8_t)sio->mode;
  state.player_id = sio->player_id;
  state.player_count = sio->player_count;
  state.flags = 0;
  if(sio->pending_start)
    state.flags |= LINK_SIO_FLAG_PENDING_START;
  if(sio->waiting)
    state.flags |= LINK_SIO_FLAG_WAITING;
  if(sio->active)
    state.flags |= LINK_SIO_FLAG_ACTIVE;
  if(gameplay_active)
    state.flags |= LINK_SIO_FLAG_GAMEPLAY_ACTIVE;
  state.control = pair->registers[endpoint][GBA_SIO_REG_CONTROL >> 1];
  state.rcnt = pair->registers[endpoint][GBA_SIO_REG_RCNT >> 1];
  return state;
}

static void publish_sio_state(LinkPair *pair, uint8_t endpoint,
 int gameplay_active)
{
  LinkSioState state = pair_sio_state(pair, endpoint, gameplay_active);

  CHECK(link_session_publish_sio_state(&pair->session[endpoint], &state) ==
   LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair->session[endpoint ^ 1u]);
  CHECK(link_session_publish_clock(&pair->session[endpoint],
   pair->session[endpoint].clock.local_cycle) == LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair->session[endpoint ^ 1u]);
  link_session_pump(&pair->session[endpoint]);
}

static void publish_pair_sio(LinkPair *pair, int first_gameplay,
 int second_gameplay)
{
  publish_sio_state(pair, 0, first_gameplay);
  publish_sio_state(pair, 1, second_gameplay);
}

static int inject_packet(LinkPair *pair, uint8_t endpoint,
 const LinkPacket *packet, uint64_t delay)
{
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;

  if(link_protocol_encode(packet, frame, sizeof(frame), &frame_size) !=
     LINK_PROTOCOL_OK)
    return 0;
  return fake_link_network_inject(&pair->network, endpoint, frame,
   frame_size, delay);
}

static void init_pair(LinkPair *pair, int different_roms)
{
  LinkHello first = make_hello(0x11111111, "FIRST GAME", "AAAA");
  LinkHello second = make_hello(different_roms ? 0x22222222 : 0x11111111,
   different_roms ? "SECOND GAME" : "FIRST GAME",
   different_roms ? "BBBB" : "AAAA");

  memset(pair, 0, sizeof(*pair));
  gba_sio_init(&pair->sio[0], pair->registers[0]);
  gba_sio_init(&pair->sio[1], pair->registers[1]);
  fake_link_network_init(&pair->network, &pair->transport[0],
   &pair->transport[1]);
  link_session_init(&pair->session[0], &pair->sio[0], pair->transport[0],
   &first);
  link_session_init(&pair->session[1], &pair->sio[1], pair->transport[1],
   &second);
}

static void init_no_rom_pair(LinkPair *pair)
{
  LinkHello hello = make_hello(0x44494147u, "LINK BENCH", "DIAG");
  uint32_t endpoint;

  hello.rom_size = 1u;
  hello.capabilities = LINK_SESSION_SUPPORTED_CAPABILITIES;
  memset(pair, 0, sizeof(*pair));
  fake_link_network_init(&pair->network, &pair->transport[0],
   &pair->transport[1]);
  for(endpoint = 0; endpoint < 2; endpoint++)
  {
    /* Deliberately bypass init_pair: a cold menu has never reset a ROM. */
    CHECK(pair->sio[endpoint].registers == NULL);
    CHECK(gba_sio_prepare(&pair->sio[endpoint], pair->registers[endpoint]));
    link_session_init(&pair->session[endpoint], &pair->sio[endpoint],
     pair->transport[endpoint], &hello);
  }
}

static void open_pair(LinkPair *pair, int different_roms)
{
  init_pair(pair, different_roms);

  CHECK(link_session_host(&pair->session[0], 0x12345678) == 1);
  CHECK(link_session_join(&pair->session[1]) == 1);
  pump_pair(pair, 8);
  CHECK(link_session_state(&pair->session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair->session[1]) == LINK_SESSION_CONNECTED);
}

static void open_transactional_pair(LinkPair *pair)
{
  open_pair(pair, 0);
  CHECK((pair->session[0].negotiated_capabilities &
   LINK_CAPABILITY_TRANSACTIONAL) != 0);
  CHECK((pair->session[1].negotiated_capabilities &
   LINK_CAPABILITY_TRANSACTIONAL) != 0);
  CHECK((pair->session[0].negotiated_capabilities &
   LINK_CAPABILITY_FAST_TRANSFER) == 0);
  CHECK((pair->session[1].negotiated_capabilities &
   LINK_CAPABILITY_FAST_TRANSFER) == 0);
  /* Retain direct coverage of the isolated legacy implementation. Production
     peers negotiate STREAM_TRANSFER and never enter this test-only branch. */
  pair->session[0].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  pair->session[1].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
}

static void select_mode(GbaSio *sio, GbaSioMode mode)
{
  uint16_t control = 0;

  gba_sio_write16(sio, GBA_SIO_REG_RCNT, 0);
  if(mode == GBA_SIO_MODE_NORMAL_32)
    control = 0x1000;
  else if(mode == GBA_SIO_MODE_MULTIPLAYER)
    control = 0x2000;
  gba_sio_write16(sio, GBA_SIO_REG_CONTROL, control);
}

static void configure_multiplayer(LinkPair *pair, uint16_t first,
 uint16_t second, uint8_t baud)
{
  uint16_t control = (uint16_t)(0x6000u | baud);

  select_mode(&pair->sio[0], GBA_SIO_MODE_MULTIPLAYER);
  select_mode(&pair->sio[1], GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_SEND, first);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_SEND, second);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_CONTROL, control);
  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL,
   (uint16_t)(control | 0x0080u));
  publish_pair_sio(pair, 1, 1);
}

static uint16_t pair_reg(const LinkPair *pair, uint8_t endpoint,
 uint32_t address)
{
  return pair->registers[endpoint][address >> 1];
}

static void complete_pair_transfer(LinkPair *pair)
{
  uint32_t first_cycles = gba_sio_next_event(&pair->sio[0]);
  uint32_t second_cycles = gba_sio_next_event(&pair->sio[1]);

  CHECK(first_cycles != GBA_SIO_NO_EVENT);
  CHECK(second_cycles == first_cycles);
  CHECK(gba_sio_advance(&pair->sio[0], first_cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_advance(&pair->sio[1], second_cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
}

static void test_handshake_and_different_roms(void)
{
  LinkPair pair;

  open_pair(&pair, 1);
  CHECK(pair.session[0].peer_hello.rom_crc32 == 0x22222222);
  CHECK(pair.session[1].peer_hello.rom_crc32 == 0x11111111);
  CHECK(pair.session[0].negotiated_capabilities ==
   LINK_SESSION_REQUIRED_CAPABILITIES);
  CHECK(pair.session[1].negotiated_capabilities ==
   LINK_SESSION_REQUIRED_CAPABILITIES);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0030) == 0);
  CHECK(pair.sio[0].player_id == 0);
  CHECK(pair.sio[1].player_id == 1);
}

static void test_delayed_handshake(void)
{
  LinkPair pair;
  uint32_t round;

  init_pair(&pair, 1);
  fake_link_network_set_latency(&pair.network, 0, 250000u);
  fake_link_network_set_latency(&pair.network, 1, 250000u);
  CHECK(link_session_host(&pair.session[0], 0x12345678) == 1);
  CHECK(link_session_join(&pair.session[1]) == 1);

  for(round = 0; round < 8; round++)
  {
    pump_pair(&pair, 1);
    fake_link_network_advance(&pair.network, 250000u);
  }

  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].peer_hello.rom_crc32 == 0x22222222);
  CHECK(pair.session[1].peer_hello.rom_crc32 == 0x11111111);
}

static void test_negotiation_timeout_is_attributed(void)
{
  LinkPair pair;

  init_pair(&pair, 0);
  fake_link_network_set_drop(&pair.network, 1);
  CHECK(link_session_host(&pair.session[0], 0x12345678) == 1);
  CHECK(link_session_join(&pair.session[1]) == 1);
  pump_pair(&pair, 2u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_NEGOTIATING);

  fake_link_network_advance(&pair.network, LINK_SESSION_TIMEOUT_US);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_TIMEOUT);
  CHECK(pair.session[0].last_timeout_source == LINK_TIMEOUT_NEGOTIATION);
}

static void test_late_attach_delivers_abort_irq_once(void)
{
  LinkPair pair;
  uint32_t events;

  init_pair(&pair, 0);
  select_mode(&pair.sio[1], GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_SEND, 0x2468u);
  CHECK(gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL,
   0x6080u) == GBA_SIO_EVENT_START);
  CHECK(pair.sio[1].pending_start != 0);

  CHECK(link_session_join(&pair.session[1]) == 1);
  CHECK(pair.sio[1].pending_start == 0);
  CHECK(pair.sio[1].waiting == 0);
  CHECK(pair.sio[1].active == 0);
  CHECK((pair_reg(&pair, 1, GBA_SIO_REG_CONTROL) & 0x0080u) == 0);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0);
  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) == 0);
}

static void test_sio_state_message(void)
{
  LinkPair pair;
  LinkSioState state = { 2, 0, 2, 3, 0x6003, 0x000F };

  open_pair(&pair, 0);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].peer_sio_valid == 1);
  CHECK(memcmp(&pair.session[1].peer_sio, &state, sizeof(state)) == 0);
}

static void test_hot_readiness_waits_for_shared_clock_base(void)
{
  LinkPair pair;
  LinkSioState state;
  uint32_t clock_syncs;
  uint32_t heartbeats;

  open_pair(&pair, 0);
  select_mode(&pair.sio[0], GBA_SIO_MODE_MULTIPLAYER);
  select_mode(&pair.sio[1], GBA_SIO_MODE_MULTIPLAYER);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_CONTROL, 0x6003u);

  state = pair_sio_state(&pair, 1u, 1);
  CHECK(link_session_publish_sio_state(&pair.session[1], &state) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.session[1].clock.hot_until_us > pair.network.now_us);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].peer_sio_valid != 0);
  CHECK(pair.session[0].clock.peer_valid == 0);
  CHECK(pair.session[0].clock.hot_until_us > pair.network.now_us);
  CHECK(pair.sio[0].peer_active == 0);

  clock_syncs = pair.network.sent_types[1][LINK_MESSAGE_CLOCK_SYNC];
  heartbeats = pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT];
  CHECK(link_session_publish_clock(&pair.session[1], 9000u) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_CLOCK_SYNC] == clock_syncs);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] ==
   heartbeats + 1u);
  CHECK((link_session_pump(&pair.session[0]) &
   LINK_SESSION_EVENT_SIO_REPUBLISH) != 0);
  CHECK(pair.session[0].clock.peer_valid != 0);
  CHECK(pair.sio[0].peer_active != 0);
  CHECK(pair.session[0].clock.peer_base == 9000u);
  link_session_set_emulated_cycle(&pair.session[0],
   LINK_CLOCK_MAX_LEAD_CYCLES + 1u);
  CHECK(!link_session_should_throttle(&pair.session[0]));
  CHECK(link_session_pause_reason(&pair.session[0]) ==
   LINK_SESSION_PAUSE_NONE);
}

static void test_duplicate_sio_state_is_not_requeued(void)
{
  LinkPair pair;
  LinkSioState state;
  uint32_t baseline;
  uint32_t repeat;

  open_pair(&pair, 0);
  state = pair_sio_state(&pair, 0, 1);
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);
  baseline = pair.network.sent_types[0][LINK_MESSAGE_SIO_STATE];

  for(repeat = 0; repeat < 1000; repeat++)
    CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
     LINK_SESSION_EVENT_NONE);

  CHECK(pair.network.sent_types[0][LINK_MESSAGE_SIO_STATE] == baseline);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
}

static void test_stale_sio_state_after_transfer_is_ignored(void)
{
  LinkPair pair;
  LinkPacket packet;
  LinkSioState stale;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1357u, 0x2468u, 3);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 4);
  complete_pair_transfer(&pair);
  CHECK(pair.session[1].last_sequence == 1u);

  stale = pair_sio_state(&pair, 0, 1);
  link_protocol_packet_init(&packet, LINK_MESSAGE_SIO_STATE,
   pair.session[0].session_id, 0);
  CHECK(link_protocol_write_sio_state(&packet, &stale) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1, &packet, 0));
  CHECK(link_session_pump(&pair.session[1]) == LINK_SESSION_EVENT_NONE);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_menu_peer_collapses_sio_churn(void)
{
  LinkPair pair;
  LinkSioState state;
  uint32_t baseline;
  uint32_t update;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  publish_sio_state(&pair, 1, 0);
  CHECK((pair.session[0].peer_sio.flags &
   LINK_SIO_FLAG_GAMEPLAY_ACTIVE) == 0);
  baseline = pair.network.sent_types[0][LINK_MESSAGE_SIO_STATE];

  state = pair_sio_state(&pair, 0, 1);
  for(update = 1; update <= 4096; update++)
  {
    state.control = (uint16_t)update;
    CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
     LINK_SESSION_EVENT_NONE);
  }
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_SIO_STATE] == baseline);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);

  publish_sio_state(&pair, 1, 1);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_SIO_STATE] == baseline + 1u);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].peer_sio.control == state.control);
  CHECK((pair.session[1].peer_sio.flags &
   LINK_SIO_FLAG_GAMEPLAY_ACTIVE) != 0);
}

static void test_multiplayer_transfer(void)
{
  LinkPair pair;
  uint32_t events;
  uint8_t baud;

  for(baud = 0; baud < 4; baud++)
  {
    open_pair(&pair, 0);
    configure_multiplayer(&pair, 0x1234, 0xABCD, baud);
    link_session_set_emulated_cycle(&pair.session[0], 1000u + baud);
    link_session_set_emulated_cycle(&pair.session[1], 5000u + baud);

    events = link_session_begin_transfer(&pair.session[0]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
    CHECK(link_session_state(&pair.session[0]) ==
     LINK_SESSION_WAIT_STREAM_REPLY);
    CHECK(gba_sio_is_waiting(&pair.sio[0]));
    CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);

    events = link_session_pump(&pair.session[1]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_END) != 0);
    CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
    CHECK(pair.session[1].clock.peer_cycle == 1000u + baud);
    CHECK(gba_sio_next_event(&pair.sio[1]) ==
     gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER, baud, 2));

    events = link_session_pump(&pair.session[0]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_END) != 0);
    CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(pair.session[0].clock.peer_cycle == 5000u + baud);
    CHECK(gba_sio_next_event(&pair.sio[0]) ==
     gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER, baud, 2));

    CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_START] == 1u);
    CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_REPLY] == 1u);
    CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_PREPARE] == 0u);
    CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_ACCEPT] == 0u);
    CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_COMMIT] == 0u);
    CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_COMMIT_ACK] == 0u);
    CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_RELEASE] == 0u);
    CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_RELEASE_ACK] ==
     0u);

    complete_pair_transfer(&pair);
    CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == 0x1234);
    CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == 0xABCD);
    CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI0) == 0x1234);
    CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI1) == 0xABCD);
  }
}

static void test_stream_barrier_stops_at_hardware_finish(void)
{
  LinkPair pair;
  uint32_t duration;
  uint32_t events;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1234u, 0xABCDu, 3u);
  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  duration = pair.session[0].transfer.finish_cycle -
   pair.session[0].transfer.start_cycle;
  CHECK(duration == gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER,
   3u, 2u));
  CHECK(link_session_cycles_until_pause(&pair.session[0]) == duration);
  CHECK(link_session_pause_reason(&pair.session[0]) ==
   LINK_SESSION_PAUSE_NONE);

  link_session_set_emulated_cycle(&pair.session[0],
   pair.session[0].barrier_cycle + duration - 1u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_cycles_until_pause(&pair.session[0]) == 1u);
  CHECK(!link_session_should_pause(&pair.session[0]));

  link_session_set_emulated_cycle(&pair.session[0],
   pair.session[0].barrier_cycle + duration);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_cycles_until_pause(&pair.session[0]) == 0u);
  CHECK(link_session_pause_reason(&pair.session[0]) ==
   LINK_SESSION_PAUSE_SIO_DEPENDENCY);
  CHECK(link_session_should_pause(&pair.session[0]));

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_pause_reason(&pair.session[0]) ==
   LINK_SESSION_PAUSE_NONE);
  CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == 0x1234u);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == 0xABCDu);
}

static void test_stream_heartbeat_cumulatively_acks_reply(void)
{
  LinkPair pair;
  uint32_t clock_syncs;
  uint32_t heartbeats;
  uint32_t anchor_cycle;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1234u, 0xABCDu, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4u);
  CHECK(link_stream_reply_cache_depth(&pair.session[1].stream) == 1u);
  complete_pair_transfer(&pair);

  clock_syncs = pair.network.sent_types[0][LINK_MESSAGE_CLOCK_SYNC];
  heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];
  anchor_cycle = pair.session[0].clock.last_published_cycle +
   LINK_CLOCK_SYNC_INTERVAL_CYCLES;
  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  CHECK(link_session_publish_clock(&pair.session[0], anchor_cycle) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_CLOCK_SYNC] == clock_syncs);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   heartbeats + 1u);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].clock.peer_cycle == anchor_cycle);
  CHECK(link_stream_reply_cache_depth(&pair.session[1].stream) == 0u);
}

static void test_stream_clock_anchor_is_low_rate(void)
{
  LinkPair pair;
  uint32_t heartbeats;
  uint32_t tick;
  uint32_t cycle;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];
  cycle = pair.session[0].clock.local_cycle;

  for(tick = 0; tick < 999u; tick++)
  {
    cycle += LINK_CLOCK_SYNC_INTERVAL_CYCLES;
    fake_link_network_advance(&pair.network, 1000u);
    CHECK(link_session_publish_clock(&pair.session[0], cycle) ==
     LINK_SESSION_EVENT_NONE);
  }
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] == heartbeats);

  cycle += LINK_CLOCK_SYNC_INTERVAL_CYCLES;
  fake_link_network_advance(&pair.network, 1000u);
  CHECK(link_session_publish_clock(&pair.session[0], cycle) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   heartbeats + 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_transactional_multiplayer_requires_commit_release(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1234u, 0xABCDu, 3u);
  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_PROPOSED);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PREPARED);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);

  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_END) == 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_COMMIT_ACK);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_DECIDED);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_END) == 0);
  CHECK(link_session_state(&pair.session[1]) ==
   LINK_SESSION_WAIT_COMMIT_RELEASE);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_DECIDED);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);

  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) == 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_RELEASE_ACK);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_DECIDED);
  CHECK(pair.session[0].transaction_applied != 0);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].transaction_stage ==
   LINK_TRANSACTION_ACKNOWLEDGED);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].transaction_stage ==
   LINK_TRANSACTION_ACKNOWLEDGED);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_PREPARE] == 1u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_ACCEPT] == 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_COMMIT] == 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_COMMIT_ACK] == 0u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_COMMIT_ACK] == 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_RELEASE] == 1u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_RELEASE_ACK] == 1u);

  complete_pair_transfer(&pair);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == 0x1234u);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == 0xABCDu);
  CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI0) == 0x1234u);
  CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI1) == 0xABCDu);
}

static uint32_t defer_next_multiplayer_prepare(LinkPair *pair,
 uint16_t host_data)
{
  uint32_t cycles;

  open_transactional_pair(pair);
  configure_multiplayer(pair, 0x1111u, 0x2222u, 3u);
  CHECK((link_session_begin_transfer(&pair->session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(pair, 4);
  CHECK(link_session_state(&pair->session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair->session[1]) == LINK_SESSION_CONNECTED);

  cycles = gba_sio_next_event(&pair->sio[0]);
  CHECK(cycles != GBA_SIO_NO_EVENT);
  CHECK(gba_sio_advance(&pair->sio[0], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_next_event(&pair->sio[1]) == cycles);

  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_SEND, host_data);
  CHECK(gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL,
   0x6083u) == GBA_SIO_EVENT_START);
  CHECK((link_session_begin_transfer(&pair->session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_pump(&pair->session[1]) == LINK_SESSION_EVENT_NONE);
  CHECK(link_session_state(&pair->session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair->session[1].deferred_prepare_valid != 0);
  CHECK(gba_sio_next_event(&pair->sio[1]) == cycles);
  return cycles;
}

static void finish_deferred_multiplayer_prepare(LinkPair *pair,
 uint32_t cycles, uint16_t joiner_data)
{
  uint32_t events;

  CHECK(gba_sio_advance(&pair->sio[1], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_SEND, joiner_data);

  CHECK(link_session_pump(&pair->session[1]) == LINK_SESSION_EVENT_NONE);
  events = link_session_pump(&pair->session[1]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair->session[1]) == LINK_SESSION_WAIT_COMMIT);
  pump_pair(pair, 4);
}

static void test_next_prepare_waits_for_previous_serial_irq(void)
{
  LinkPair pair;
  uint32_t cycles = defer_next_multiplayer_prepare(&pair, 0x3333u);

  finish_deferred_multiplayer_prepare(&pair, cycles, 0x4444u);

  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 2u);
  CHECK(pair.session[1].last_sequence == 2u);
  CHECK(pair.session[0].reason == LINK_REASON_NONE);
  CHECK(pair.session[1].reason == LINK_REASON_NONE);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);

  complete_pair_transfer(&pair);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == 0x3333u);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == 0x4444u);
  CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI0) == 0x3333u);
  CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI1) == 0x4444u);
}

static void test_duplicate_deferred_prepare_is_idempotent(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint32_t cycles = defer_next_multiplayer_prepare(&pair, 0x3333u);
  uint32_t accepts =
   pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_ACCEPT];

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_PREPARE,
   pair.session[0].session_id, pair.session[0].active_sequence);
  CHECK(link_protocol_write_transfer(&packet,
   LINK_MESSAGE_TRANSFER_PREPARE, &pair.session[0].transfer) ==
   LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1, &packet, 0));
  CHECK(link_session_pump(&pair.session[1]) == LINK_SESSION_EVENT_NONE);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].deferred_prepare_valid != 0);

  finish_deferred_multiplayer_prepare(&pair, cycles, 0x4444u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_ACCEPT] ==
   accepts + 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_abort_of_deferred_prepare_preserves_previous_irq(void)
{
  LinkPair pair;
  uint32_t cycles = defer_next_multiplayer_prepare(&pair, 0x3333u);
  uint32_t events;

  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) == 0);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].deferred_prepare_valid == 0);
  CHECK(pair.session[1].last_sequence == 2u);
  CHECK(gba_sio_next_event(&pair.sio[1]) == cycles);

  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(gba_sio_advance(&pair.sio[1], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void configure_normal(LinkPair *pair, GbaSioMode mode,
 uint8_t clock_owner, uint8_t fast, uint32_t first, uint32_t second)
{
  uint16_t mode_bits = mode == GBA_SIO_MODE_NORMAL_32 ? 0x1000u : 0;
  uint16_t base = (uint16_t)(mode_bits | 0x4080u | (fast ? 2u : 0u));

  select_mode(&pair->sio[0], mode);
  select_mode(&pair->sio[1], mode);
  if(mode == GBA_SIO_MODE_NORMAL_32)
  {
    gba_sio_write32(&pair->sio[0], GBA_SIO_REG_DATA32_L, first);
    gba_sio_write32(&pair->sio[1], GBA_SIO_REG_DATA32_L, second);
  }
  else
  {
    gba_sio_write16(&pair->sio[0], GBA_SIO_REG_SEND, (uint16_t)first);
    gba_sio_write16(&pair->sio[1], GBA_SIO_REG_SEND, (uint16_t)second);
  }

  gba_sio_write16(&pair->sio[clock_owner ^ 1u], GBA_SIO_REG_CONTROL, base);
  gba_sio_write16(&pair->sio[clock_owner], GBA_SIO_REG_CONTROL,
   (uint16_t)(base | 1u));
  publish_pair_sio(pair, 1, 1);
}

static void run_normal_transfer(GbaSioMode mode, uint8_t clock_owner,
 uint8_t fast)
{
  LinkPair pair;
  uint32_t first = mode == GBA_SIO_MODE_NORMAL_32 ? 0x11223344 : 0x44;
  uint32_t second = mode == GBA_SIO_MODE_NORMAL_32 ? 0xAABBCCDD : 0xDD;
  uint32_t expected_cycles = gba_sio_transfer_cycles(mode, fast, 2);

  open_pair(&pair, 0);
  configure_normal(&pair, mode, clock_owner, fast, first, second);
  CHECK((link_session_begin_transfer(&pair.session[clock_owner]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(gba_sio_next_event(&pair.sio[0]) == expected_cycles);
  CHECK(gba_sio_next_event(&pair.sio[1]) == expected_cycles);
  complete_pair_transfer(&pair);

  if(mode == GBA_SIO_MODE_NORMAL_32)
  {
    CHECK(pair_reg(&pair, 0, GBA_SIO_REG_DATA32_L) == 0xCCDD);
    CHECK(pair_reg(&pair, 0, GBA_SIO_REG_DATA32_H) == 0xAABB);
    CHECK(pair_reg(&pair, 1, GBA_SIO_REG_DATA32_L) == 0x3344);
    CHECK(pair_reg(&pair, 1, GBA_SIO_REG_DATA32_H) == 0x1122);
  }
  else
  {
    CHECK(pair_reg(&pair, 0, GBA_SIO_REG_SEND) == 0x00DD);
    CHECK(pair_reg(&pair, 1, GBA_SIO_REG_SEND) == 0x0044);
  }
}

static void test_normal_transfers(void)
{
  run_normal_transfer(GBA_SIO_MODE_NORMAL_8, 0, 0);
  run_normal_transfer(GBA_SIO_MODE_NORMAL_8, 1, 1);
  run_normal_transfer(GBA_SIO_MODE_NORMAL_32, 0, 1);
  run_normal_transfer(GBA_SIO_MODE_NORMAL_32, 1, 0);
}

static void configure_reference_mode(LinkPair *pair, GbaSioMode mode)
{
  if(mode == GBA_SIO_MODE_MULTIPLAYER)
    configure_multiplayer(pair, 0x1234u, 0xABCDu, 3u);
  else
    configure_normal(pair, mode, 0u, 1u, 0x11223344u, 0xAABBCCDDu);
}

static GbaSioEvent change_reference_mode(GbaSio *sio, GbaSioMode old_mode)
{
  uint16_t control = old_mode == GBA_SIO_MODE_MULTIPLAYER ?
   0x4000u : 0x6000u;

  return gba_sio_write16(sio, GBA_SIO_REG_CONTROL, control);
}

static void check_reference_result(const LinkPair *pair, GbaSioMode mode)
{
  if(mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    CHECK(pair_reg(pair, 0, GBA_SIO_REG_MULTI0) == 0x1234u);
    CHECK(pair_reg(pair, 0, GBA_SIO_REG_MULTI1) == 0xABCDu);
    CHECK(pair_reg(pair, 1, GBA_SIO_REG_MULTI0) == 0x1234u);
    CHECK(pair_reg(pair, 1, GBA_SIO_REG_MULTI1) == 0xABCDu);
  }
  else if(mode == GBA_SIO_MODE_NORMAL_32)
  {
    CHECK(pair_reg(pair, 0, GBA_SIO_REG_DATA32_L) == 0xCCDDu);
    CHECK(pair_reg(pair, 0, GBA_SIO_REG_DATA32_H) == 0xAABBu);
    CHECK(pair_reg(pair, 1, GBA_SIO_REG_DATA32_L) == 0x3344u);
    CHECK(pair_reg(pair, 1, GBA_SIO_REG_DATA32_H) == 0x1122u);
  }
  else
  {
    CHECK(pair_reg(pair, 0, GBA_SIO_REG_SEND) == 0x00DDu);
    CHECK(pair_reg(pair, 1, GBA_SIO_REG_SEND) == 0x0044u);
  }
}

static void test_armed_mode_change_cancels_without_irq(void)
{
  GbaSioMode mode;

  for(mode = GBA_SIO_MODE_NORMAL_8; mode <= GBA_SIO_MODE_MULTIPLAYER;
      mode = (GbaSioMode)(mode + 1))
  {
    LinkPair pair;
    LinkSioState state;
    uint32_t events;

    open_transactional_pair(&pair);
    configure_reference_mode(&pair, mode);
    CHECK((link_session_begin_transfer(&pair.session[0]) &
     LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
    CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_PROPOSED);
    CHECK(change_reference_mode(&pair.sio[0], mode) ==
     GBA_SIO_EVENT_MODE_CHANGE);
    state = pair_sio_state(&pair, 0u, 1);
    events = link_session_publish_sio_state(&pair.session[0], &state);
    CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
    CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) == 0);
    CHECK(link_session_state(&pair.session[0]) ==
     LINK_SESSION_WAIT_ABORT_ACK);
    CHECK(!gba_sio_is_busy(&pair.sio[0]));

    events = pump_pair(&pair, 3u);
    CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) == 0);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
    CHECK(!gba_sio_is_busy(&pair.sio[1]));
    CHECK(pair.session[0].last_transfer_reason == LINK_REASON_MODE_MISMATCH);
    CHECK(pair.session[1].last_transfer_reason == LINK_REASON_MODE_MISMATCH);
  }
}

static void test_decided_mode_change_preserves_result(void)
{
  uint8_t endpoint;
  GbaSioMode mode;

  for(endpoint = 0; endpoint < 2u; endpoint++)
  {
    for(mode = GBA_SIO_MODE_NORMAL_8; mode <= GBA_SIO_MODE_MULTIPLAYER;
        mode = (GbaSioMode)(mode + 1))
    {
      LinkPair pair;
      LinkSioState state;
      uint32_t expected_cycles = gba_sio_transfer_cycles(mode,
       mode == GBA_SIO_MODE_MULTIPLAYER ? 3u : 1u, 2u);

      open_transactional_pair(&pair);
      configure_reference_mode(&pair, mode);
      CHECK((link_session_begin_transfer(&pair.session[0]) &
       LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
      link_session_pump(&pair.session[1]);
      link_session_pump(&pair.session[0]);
      CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_DECIDED);
      if(endpoint != 0)
      {
        link_session_pump(&pair.session[1]);
        CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_DECIDED);
      }

      CHECK(pair.sio[endpoint].decision_locked != 0);
      CHECK(change_reference_mode(&pair.sio[endpoint], mode) ==
       GBA_SIO_EVENT_MODE_CHANGE);
      CHECK(pair.sio[endpoint].decision_locked != 0);
      CHECK(gba_sio_is_waiting(&pair.sio[endpoint]));
      state = pair_sio_state(&pair, endpoint, 1);
      CHECK(link_session_publish_sio_state(&pair.session[endpoint], &state) ==
       LINK_SESSION_EVENT_NONE);
      CHECK(pair.session[endpoint].transaction_stage ==
       LINK_TRANSACTION_DECIDED);

      pump_pair(&pair, 4u);
      CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
      CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
      CHECK(gba_sio_next_event(&pair.sio[0]) == expected_cycles);
      CHECK(gba_sio_next_event(&pair.sio[1]) == expected_cycles);
      CHECK(pair.sio[0].transfer_finish_cycle -
       pair.sio[0].transfer_start_cycle == expected_cycles);
      CHECK(pair.sio[1].transfer_finish_cycle -
       pair.sio[1].transfer_start_cycle == expected_cycles);
      complete_pair_transfer(&pair);
      CHECK(pair.sio[endpoint].decision_locked == 0);
      check_reference_result(&pair, mode);
    }
  }
}

static void test_transactional_joiner_clock_requires_commit_release(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_normal(&pair, GBA_SIO_MODE_NORMAL_8, 1u, 1u,
   0x44u, 0xDDu);
  events = link_session_begin_transfer(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_PREPARE);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PROPOSED);

  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_PREPARED);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_END) == 0);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PREPARED);

  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_COMMIT_ACK);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_DECIDED);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) ==
   LINK_SESSION_WAIT_COMMIT_RELEASE);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_DECIDED);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) == 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_RELEASE_ACK);
  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_REQUEST] == 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_PREPARE] == 1u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_ACCEPT] == 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_COMMIT] == 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_COMMIT_ACK] == 0u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_COMMIT_ACK] == 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_RELEASE] == 1u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_RELEASE_ACK] == 1u);

  complete_pair_transfer(&pair);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_SEND) == 0x00DDu);
  CHECK(pair_reg(&pair, 1, GBA_SIO_REG_SEND) == 0x0044u);
}

static void test_host_schedules_shared_cycle_for_joiner_clock(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  link_session_set_emulated_cycle(&pair.session[0], 4000u);
  link_session_set_emulated_cycle(&pair.session[1], 9000u);
  configure_normal(&pair, GBA_SIO_MODE_NORMAL_8, 1u, 1u,
   0x44u, 0xDDu);

  events = link_session_begin_transfer(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pair.session[1].transfer.start_cycle == 9000u);
  CHECK(pair.session[1].barrier_cycle == 9000u);

  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pair.session[0].transfer.start_cycle == 4000u);
  CHECK(pair.session[0].transfer.finish_cycle == 4064u);
  CHECK(pair.session[0].barrier_cycle == 4000u);

  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].transfer.start_cycle == 4000u);
  CHECK(pair.session[1].transfer.finish_cycle == 4064u);
  pump_pair(&pair, 3u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.sio[0].transfer_start_cycle == 4000u);
  CHECK(pair.sio[0].transfer_finish_cycle == 4064u);
  CHECK(pair.sio[1].transfer_start_cycle == 9000u);
  CHECK(pair.sio[1].transfer_finish_cycle == 9064u);
  CHECK(gba_sio_advance(&pair.sio[0], 63u) == GBA_SIO_EVENT_NONE);
  CHECK(gba_sio_advance(&pair.sio[1], 63u) == GBA_SIO_EVENT_NONE);
  CHECK(gba_sio_advance(&pair.sio[0], 1u) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(gba_sio_advance(&pair.sio[1], 1u) == GBA_SIO_EVENT_SERIAL_IRQ);
}

static void test_barrier_cycle_crossing_fails_closed(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  link_session_set_emulated_cycle(&pair.session[0], 1000u);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  link_session_set_emulated_cycle(&pair.session[0], 1001u);
  CHECK((link_session_check_invariants(&pair.session[0]) &
   LINK_INVARIANT_SIO_HORIZON) != 0);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_PROTOCOL);
}

static void test_shared_cycle_schedule_wraps_serially(void)
{
  LinkPair pair;
  uint32_t host_start = UINT32_MAX - 31u;

  open_transactional_pair(&pair);
  link_session_set_emulated_cycle(&pair.session[0], host_start);
  link_session_set_emulated_cycle(&pair.session[1], 500u);
  configure_normal(&pair, GBA_SIO_MODE_NORMAL_8, 0u, 1u,
   0x44u, 0xDDu);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].applied_transfer.start_cycle == host_start);
  CHECK(pair.session[0].applied_transfer.finish_cycle == 32u);
  CHECK(pair.session[1].applied_transfer.start_cycle == host_start);
  CHECK(pair.session[1].applied_transfer.finish_cycle == 32u);
  CHECK(pair.sio[0].transfer_finish_cycle == 32u);
  CHECK(pair.sio[1].transfer_finish_cycle == 564u);
  complete_pair_transfer(&pair);
}

static void test_asymmetric_latency_freezes_sio(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_set_latency(&pair.network, 0, 100000);
  fake_link_network_set_latency(&pair.network, 1, 250000);
  link_session_begin_transfer(&pair.session[0]);
  CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);

  fake_link_network_advance(&pair.network, 99999);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);

  fake_link_network_advance(&pair.network, 1);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
  CHECK(gba_sio_next_event(&pair.sio[1]) == GBA_SIO_NO_EVENT);

  fake_link_network_advance(&pair.network, 250000);
  link_session_pump(&pair.session[0]);
  CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);
  CHECK(gba_sio_next_event(&pair.sio[1]) == GBA_SIO_NO_EVENT);

  fake_link_network_advance(&pair.network, 100000);
  link_session_pump(&pair.session[1]);
  CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);
  CHECK(gba_sio_next_event(&pair.sio[1]) == GBA_SIO_NO_EVENT);

  fake_link_network_advance(&pair.network, 250000);
  link_session_pump(&pair.session[0]);
  CHECK(gba_sio_next_event(&pair.sio[0]) == 5755);
  CHECK(gba_sio_next_event(&pair.sio[1]) == GBA_SIO_NO_EVENT);

  fake_link_network_advance(&pair.network, 100000);
  link_session_pump(&pair.session[1]);
  CHECK(gba_sio_next_event(&pair.sio[1]) == 5755);

  fake_link_network_advance(&pair.network, 250000);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_barrier_timeout(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_set_drop(&pair.network, 1);
  link_session_begin_transfer(&pair.session[0]);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0);
  events = link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) == 0);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(pair.network.recovery_requests[0] == 1u);
  CHECK(pair.network.recovery_timeout_sources[0] ==
   LINK_TIMEOUT_ACTIVE_TRANSFER);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_LIVENESS_TIMEOUT_US - LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_RECOVERING);
  CHECK(pair.session[0].reason == LINK_REASON_NONE);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) == 0);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0040) != 0);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0080) == 0);
}

static void test_abort_ack_timeout_is_attributed(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_PREPARE);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_ABORT_ACK);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  CHECK((link_session_pump(&pair.session[0]) &
   LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ABORT_ACK);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ABORT_ACK);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_ABORT_ACK_TIMEOUT_US);
  CHECK(fake_link_network_activity(&pair.network, 0, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_TIMEOUT);
  CHECK(pair.session[0].last_timeout_source == LINK_TIMEOUT_ABORT_ACK);
}

static void test_postdecision_transport_failure_preserves_result(void)
{
  LinkPair pair;
  uint32_t events;
  uint32_t cycles;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1357u, 0x2468u, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  link_session_pump(&pair.session[1]);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_DECIDED);
  CHECK(pair.session[0].transaction_applied == 0);
  link_session_pump(&pair.session[1]);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_RELEASE_ACK);
  CHECK(pair.session[0].transaction_applied != 0);
  cycles = gba_sio_next_event(&pair.sio[0]);
  CHECK(cycles != GBA_SIO_NO_EVENT);

  fake_link_network_close(&pair.network, 0);
  events = link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_DECIDED);
  CHECK(pair.session[0].transaction_applied != 0);
  CHECK(gba_sio_next_event(&pair.sio[0]) == cycles);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);

  CHECK(gba_sio_advance(&pair.sio[0], cycles) ==
   GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == 0x1357u);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == 0x2468u);
}

static void test_late_accept_does_not_close_new_transfer(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_set_latency(&pair.network, 1,
   LINK_SESSION_TRANSFER_TIMEOUT_US + 100000u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  link_session_pump(&pair.session[1]);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);

  fake_link_network_advance(&pair.network, 100000u);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) == 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) == 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);

  configure_multiplayer(&pair, 0x3333, 0x4444, 3);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);

  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
}

static void test_late_terminal_packets_do_not_close_new_transfer(void)
{
  LinkPair pair;
  LinkPacket packet;
  LinkTransferData previous;
  uint32_t previous_sequence;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  previous = pair.session[0].transfer;
  previous_sequence = pair.session[0].last_sequence;
  complete_pair_transfer(&pair);

  configure_multiplayer(&pair, 0x3333u, 0x4444u, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_COMMIT,
   pair.session[0].session_id, previous_sequence);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_COMMIT,
   &previous) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1, &packet, 0));

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_COMMIT_ACK,
   pair.session[0].session_id, previous_sequence);
  CHECK(inject_packet(&pair, 0, &packet, 0));
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_ABORT_ACK,
   pair.session[0].session_id, previous_sequence);
  CHECK(inject_packet(&pair, 0, &packet, 0));

  pump_pair(&pair, 4);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == previous_sequence + 1u);
  CHECK(pair.session[1].last_sequence == previous_sequence + 1u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
  complete_pair_transfer(&pair);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == 0x3333u);
  CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == 0x4444u);
  CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI0) == 0x3333u);
  CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI1) == 0x4444u);
}

static void test_staggered_gameplay_resume(void)
{
  LinkPair pair;
  uint32_t second;
  uint32_t events;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1234, 0x5678, 3);
  publish_sio_state(&pair, 1, 0);

  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) == 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(pair.sio[0].pending_start != 0);

  for(second = 0; second < 10; second++)
  {
    fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
    pump_pair(&pair, 2);
    events = link_session_begin_transfer(&pair.session[0]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) == 0);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  }

  publish_sio_state(&pair, 1, 1);
  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  complete_pair_transfer(&pair);
}

static void test_menu_presence_suspends_heartbeat_timeout(void)
{
  LinkPair pair;
  uint32_t second;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 0, 0);
  fake_link_network_set_drop(&pair.network, 1);

  for(second = 0; second < 30; second++)
  {
    fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
    link_session_pump(&pair.session[0]);
    link_session_pump(&pair.session[1]);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  }

  fake_link_network_set_drop(&pair.network, 0);
  publish_sio_state(&pair, 0, 1);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);

  fake_link_network_set_drop(&pair.network, 1);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_TIMEOUT_US);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(pair.network.recovery_requests[1] == 1u);
}

static void test_menu_presence_does_not_hide_transport_close(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 0);
  fake_link_network_close(&pair.network, 0);
  events = link_session_pump(&pair.session[0]);

  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_PEER_CLOSED);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
}

static void test_stale_peer_mode_aborts_only_transfer(void)
{
  LinkPair pair;
  LinkSioState claimed_peer;
  uint32_t events;

  open_transactional_pair(&pair);
  select_mode(&pair.sio[0], GBA_SIO_MODE_MULTIPLAYER);
  select_mode(&pair.sio[1], GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 0x1234);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6083);
  publish_sio_state(&pair, 0, 1);

  claimed_peer = pair_sio_state(&pair, 1, 1);
  claimed_peer.mode = GBA_SIO_MODE_MULTIPLAYER;
  claimed_peer.control = 0x6003;
  CHECK(link_session_publish_sio_state(&pair.session[1], &claimed_peer) ==
   LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_publish_clock(&pair.session[1],
   pair.session[1].clock.local_cycle) == LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair.session[0]);

  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[1]) ==
   LINK_SESSION_WAIT_ABORT_ACK);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_transfer_reason == LINK_REASON_MODE_MISMATCH);
  CHECK(pair.session[1].last_transfer_reason == LINK_REASON_MODE_MISMATCH);
  CHECK(pair.network.connected != 0);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0080) == 0);
}

static void test_active_mode_mismatch_times_out_only_transfer(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  select_mode(&pair.sio[0], GBA_SIO_MODE_MULTIPLAYER);
  select_mode(&pair.sio[1], GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 0x1234);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6083);
  publish_pair_sio(&pair, 1, 1);

  CHECK(link_session_begin_transfer(&pair.session[0]) ==
   LINK_SESSION_EVENT_NONE);
  fake_link_network_advance(&pair.network, LINK_SESSION_READY_TIMEOUT_US);
  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_transfer_reason == LINK_REASON_MODE_MISMATCH);
  CHECK(pair.network.connected != 0);
}

static void test_wrong_session_packet(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;

  open_pair(&pair, 0);
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   0x87654321, 0);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);
  CHECK(fake_link_network_inject(&pair.network, 0, frame, frame_size, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_PROTOCOL);
  CHECK(pair.session[0].terminal_valid != 0);
  CHECK(pair.session[0].terminal_previous_state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].terminal_message_type == LINK_MESSAGE_HEARTBEAT);
  CHECK(pair.session[0].terminal_sequence == 0);
}

static void test_malformed_packet(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;

  open_pair(&pair, 0);
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, pair.session[0].last_sequence);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);
  frame[20] ^= 0x80;
  CHECK(fake_link_network_inject(&pair.network, 0, frame, frame_size, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_PROTOCOL);
}

static void test_truncated_packet_matrix(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;
  size_t size;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   0x12345678, 0);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);

  for(size = 0; size < frame_size; size++)
  {
    open_pair(&pair, 0);
    CHECK(fake_link_network_inject(&pair.network, 0, frame, size, 0));
    link_session_pump(&pair.session[0]);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
    CHECK(pair.session[0].reason == LINK_REASON_PROTOCOL);
  }
}

static void test_stale_and_duplicate_packets(void)
{
  LinkPair pair;
  LinkPacket packet;
  LinkTransferData transfer;
  uint32_t heartbeat_rx_count;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 4);
  complete_pair_transfer(&pair);
  heartbeat_rx_count = pair.session[0].heartbeat_rx_count;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, 0);
  CHECK(inject_packet(&pair, 0, &packet, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].heartbeat_rx_count == heartbeat_rx_count + 1u);

  open_pair(&pair, 0);
  memset(&transfer, 0, sizeof(transfer));
  transfer.mode = GBA_SIO_MODE_MULTIPLAYER;
  transfer.clock_owner = 0;
  transfer.player_count = 2;
  transfer.participant_mask = 3;
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_COMMIT,
   pair.session[0].session_id, 1);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_COMMIT,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1, &packet, 0));
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_PROTOCOL);

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x3333, 0x4444, 3);
  link_session_begin_transfer(&pair.session[0]);
  link_session_pump(&pair.session[1]);
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_PREPARE,
   pair.session[0].session_id, pair.session[0].active_sequence);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_PREPARE,
   &pair.session[0].transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1, &packet, 0));
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_PROTOCOL);
}

static void test_completed_handshake_duplicates_are_idempotent(void)
{
  LinkPair pair;
  LinkPacket packet;
  LinkTransferData transfer;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x3333u, 0x4444u, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4u);
  complete_pair_transfer(&pair);
  CHECK(pair.session[0].journal.checkpoint == 1u);
  CHECK(pair.session[1].journal.checkpoint == 1u);

  transfer = pair.session[0].applied_transfer;
  transfer.participant_mask = 0x01u;
  transfer.data[1] = 0u;
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_PREPARE,
   pair.session[0].session_id, 1u);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_PREPARE,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1u, &packet, 0u));
  pump_pair(&pair, 2u);

  transfer = pair.session[0].applied_transfer;
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_ACCEPT,
   pair.session[0].session_id, 1u);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_ACCEPT,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 0u, &packet, 0u));
  link_session_pump(&pair.session[0]);

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_COMMIT,
   pair.session[0].session_id, 1u);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_COMMIT,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1u, &packet, 0u));
  pump_pair(&pair, 2u);

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_COMMIT_ACK,
   pair.session[0].session_id, 1u);
  CHECK(inject_packet(&pair, 0u, &packet, 0u));
  link_session_pump(&pair.session[0]);

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_RELEASE,
   pair.session[0].session_id, 1u);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_RELEASE,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1u, &packet, 0u));
  pump_pair(&pair, 2u);

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_RELEASE_ACK,
   pair.session[0].session_id, 1u);
  CHECK(link_protocol_write_transfer(&packet,
   LINK_MESSAGE_TRANSFER_RELEASE_ACK, &transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 0u, &packet, 0u));
  link_session_pump(&pair.session[0]);

  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].journal.checkpoint == 1u);
  CHECK(pair.session[1].journal.checkpoint == 1u);
  CHECK(pair.session[0].applied_sequence == 1u);
  CHECK(pair.session[1].applied_sequence == 1u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);

  open_transactional_pair(&pair);
  configure_normal(&pair, GBA_SIO_MODE_NORMAL_8, 1u, 1u,
   0x44u, 0xddu);
  CHECK((link_session_begin_transfer(&pair.session[1]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4u);
  complete_pair_transfer(&pair);
  transfer = pair.session[1].applied_transfer;
  transfer.participant_mask = 0x02u;
  transfer.data[0] = 0u;
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_REQUEST,
   pair.session[0].session_id, 1u);
  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_REQUEST,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 0u, &packet, 0u));
  pump_pair(&pair, 2u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].journal.checkpoint == 1u);
  CHECK(pair.session[1].journal.checkpoint == 1u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_unknown_optional_sequence(void)
{
  LinkPair pair;
  LinkPacket packet;

  open_pair(&pair, 0);
  link_protocol_packet_init(&packet, (LinkMessageType)0x80,
   pair.session[0].session_id, pair.session[0].last_sequence);
  packet.flags = LINK_PROTOCOL_FLAG_OPTIONAL;
  CHECK(inject_packet(&pair, 0, &packet, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);

  packet.sequence = pair.session[0].last_sequence - 1u;
  CHECK(inject_packet(&pair, 0, &packet, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
}

static void test_incompatible_capabilities(void)
{
  LinkPair pair;
  LinkHello first = make_hello(0x11111111, "FIRST GAME", "AAAA");
  LinkHello second = make_hello(0x22222222, "SECOND GAME", "BBBB");

  memset(&pair, 0, sizeof(pair));
  second.capabilities &= ~LINK_CAPABILITY_STREAM_TRANSFER;
  gba_sio_init(&pair.sio[0], pair.registers[0]);
  gba_sio_init(&pair.sio[1], pair.registers[1]);
  fake_link_network_init(&pair.network, &pair.transport[0],
   &pair.transport[1]);
  link_session_init(&pair.session[0], &pair.sio[0], pair.transport[0],
   &first);
  link_session_init(&pair.session[1], &pair.sio[1], pair.transport[1],
   &second);
  CHECK(link_session_host(&pair.session[0], 0x12345678));
  CHECK(link_session_join(&pair.session[1]));
  pump_pair(&pair, 8);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR ||
   link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_INCOMPATIBLE ||
   pair.session[1].reason == LINK_REASON_INCOMPATIBLE);
}

static void test_transport_loss(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  fake_link_network_close(&pair.network, 0);
  events = link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_PEER_CLOSED);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
}

static void test_invalid_disconnect_reason(void)
{
  LinkPair pair;
  LinkPacket packet;
  LinkReason reason = { UINT16_MAX, 0 };

  open_pair(&pair, 0);
  link_protocol_packet_init(&packet, LINK_MESSAGE_DISCONNECT,
   pair.session[0].session_id, pair.session[0].last_sequence);
  CHECK(link_protocol_write_reason(&packet, LINK_MESSAGE_DISCONNECT,
   &reason) == LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 0, &packet, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_PROTOCOL);
}

static void test_restart_while_waiting(void)
{
  LinkPair pair;
  LinkHello hello = make_hello(0x11111111, "FIRST GAME", "AAAA");

  memset(&pair, 0, sizeof(pair));
  gba_sio_init(&pair.sio[0], pair.registers[0]);
  gba_sio_init(&pair.sio[1], pair.registers[1]);
  fake_link_network_init(&pair.network, &pair.transport[0],
   &pair.transport[1]);
  link_session_init(&pair.session[0], &pair.sio[0], pair.transport[0],
   &hello);
  CHECK(link_session_host(&pair.session[0], 1));
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_HOSTING);
  CHECK(link_session_host(&pair.session[0], 2));
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_HOSTING);
  CHECK(pair.session[0].session_id == 2);
  CHECK(link_session_disconnect(&pair.session[0], LINK_REASON_USER) &
   LINK_SESSION_EVENT_SESSION_CLOSED);
}

static void test_disconnect_with_pending_start(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1234, 0x5678, 3);
  events = link_session_disconnect(&pair.session[0], LINK_REASON_USER);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0080) == 0);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0040) != 0);
  CHECK(pair.sio[0].player_count == 0);
}

static void test_intentional_disconnect_with_send_failure(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  fake_link_network_set_send_failure(&pair.network, 1);
  events = link_session_disconnect(&pair.session[0], LINK_REASON_USER);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_DISCONNECTED);
  CHECK(pair.session[0].reason == LINK_REASON_USER);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
}

static void test_repeated_disconnect_is_idempotent(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  events = link_session_disconnect(&pair.session[0], LINK_REASON_USER);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
  CHECK(pair.network.disconnect_calls[0] == 1u);

  events = link_session_disconnect(&pair.session[0], LINK_REASON_USER);
  CHECK(events == LINK_SESSION_EVENT_NONE);
  CHECK(pair.network.disconnect_calls[0] == 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_DISCONNECTED);
  CHECK(pair.session[0].reason == LINK_REASON_USER);
}

static void test_disconnect_after_transport_error_is_idempotent(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  fake_link_network_close(&pair.network, -1);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_TRANSPORT);
  CHECK(pair.network.disconnect_calls[0] == 1u);

  events = link_session_disconnect(&pair.session[0], LINK_REASON_USER);
  CHECK(events == LINK_SESSION_EVENT_NONE);
  CHECK(pair.network.disconnect_calls[0] == 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_TRANSPORT);
}

static void test_heartbeat_stability(void)
{
  LinkPair pair;
  uint32_t second;

  open_pair(&pair, 0);
  for(second = 0; second < 1800; second++)
  {
    fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
    pump_pair(&pair, 2);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  }
}

static void test_heartbeat_request_is_symmetric(void)
{
  LinkPair pair;
  uint32_t host_heartbeats;
  uint32_t joiner_heartbeats;

  open_pair(&pair, 0);
  host_heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];
  joiner_heartbeats = pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT];

  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  link_session_pump(&pair.session[1]);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] ==
   joiner_heartbeats + 1u);

  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats + 1u);
  link_session_pump(&pair.session[1]);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] ==
   joiner_heartbeats + 1u);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].heartbeat_tx_count == 1u);
  CHECK(pair.session[0].heartbeat_rx_count == 1u);
  CHECK(pair.session[0].heartbeat_request_rx_count == 1u);
  CHECK(pair.session[0].heartbeat_reply_tx_count == 1u);
  CHECK(pair.session[1].heartbeat_tx_count == 1u);
  CHECK(pair.session[1].heartbeat_rx_count == 1u);
  CHECK(pair.session[1].heartbeat_request_tx_count == 1u);
  CHECK(pair.session[1].heartbeat_reply_rx_count == 1u);
  CHECK(pair.session[1].heartbeat_probe_pending == 0);
  CHECK(pair.session[1].heartbeat_outstanding_token == 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_joiner_heartbeat_request_gets_host_reply(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint32_t host_heartbeats;
  uint32_t joiner_heartbeats;

  open_pair(&pair, 0);
  host_heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];
  joiner_heartbeats = pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT];
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, 0);
  packet.flags = (uint8_t)(7u << LINK_PROTOCOL_HEARTBEAT_TOKEN_SHIFT);
  CHECK(inject_packet(&pair, 0, &packet, 0));

  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats + 1u);
  CHECK(pair.network.sent_flags[0][LINK_MESSAGE_HEARTBEAT] ==
   (uint8_t)(packet.flags | LINK_PROTOCOL_HEARTBEAT_REPLY));
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].heartbeat_rx_count == 1u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] ==
   joiner_heartbeats);
  CHECK(pair.session[0].heartbeat_request_rx_count == 1u);
  CHECK(pair.session[0].heartbeat_reply_tx_count == 1u);
  CHECK(pair.session[1].heartbeat_reply_rx_count == 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_duplicate_heartbeat_probe_has_one_reply(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint32_t joiner_heartbeats;

  open_pair(&pair, 0);
  joiner_heartbeats = pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT];
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, pair.session[0].last_sequence);
  packet.flags = 0x0Au;
  CHECK(inject_packet(&pair, 1, &packet, 0));
  CHECK(inject_packet(&pair, 1, &packet, 0));

  link_session_pump(&pair.session[1]);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] ==
   joiner_heartbeats + 1u);
  CHECK(pair.session[1].heartbeat_tx_count == 1u);
  CHECK(pair.session[1].heartbeat_rx_count == 2u);
  CHECK(pair.session[1].heartbeat_request_rx_count == 2u);
  CHECK(pair.session[1].heartbeat_reply_tx_count == 1u);
  CHECK(pair.session[1].heartbeat_duplicate_request_count == 1u);
  CHECK(pair.network.sent_flags[1][LINK_MESSAGE_HEARTBEAT] ==
   (uint8_t)(packet.flags | LINK_PROTOCOL_HEARTBEAT_REPLY));
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].heartbeat_rx_count == 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_heartbeat_has_no_echo_path(void)
{
  LinkPair pair;
  LinkPacket packet;

  open_pair(&pair, 0);
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, pair.session[0].last_sequence);
  packet.flags = (uint8_t)(0x0Cu | LINK_PROTOCOL_HEARTBEAT_REPLY);
  CHECK(inject_packet(&pair, 0, &packet, 0));
  CHECK(inject_packet(&pair, 1, &packet, 0));

  pump_pair(&pair, 2);
  CHECK(pair.session[0].heartbeat_tx_count == 0u);
  CHECK(pair.session[0].heartbeat_rx_count == 1u);
  CHECK(pair.session[0].heartbeat_reply_rx_count == 1u);
  CHECK(pair.session[0].heartbeat_unmatched_reply_count == 1u);
  CHECK(pair.session[1].heartbeat_tx_count == 0u);
  CHECK(pair.session[1].heartbeat_rx_count == 1u);
  CHECK(pair.session[1].heartbeat_reply_rx_count == 1u);
  CHECK(pair.session[1].heartbeat_unmatched_reply_count == 1u);
}

static void test_reverse_request_repairs_missed_independent_anchor(void)
{
  LinkPair pair;
  uint32_t host_request_rx;
  uint32_t host_reply_tx;
  uint32_t joiner_reply_rx;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  host_request_rx = pair.session[0].heartbeat_request_rx_count;
  host_reply_tx = pair.session[0].heartbeat_reply_tx_count;
  joiner_reply_rx = pair.session[1].heartbeat_reply_rx_count;
  fake_link_network_drop_next(&pair.network, 0, LINK_MESSAGE_HEARTBEAT);
  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);

  CHECK(link_session_publish_clock(&pair.session[0], 1000u) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.session[0].heartbeat_probe_pending != 0);
  CHECK(link_session_publish_clock(&pair.session[1], 2000u) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.session[1].heartbeat_probe_pending != 0);

  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].heartbeat_request_rx_count == host_request_rx + 1u);
  CHECK(pair.session[0].heartbeat_reply_tx_count == host_reply_tx + 1u);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].heartbeat_reply_rx_count == joiner_reply_rx + 1u);
  CHECK(pair.session[1].heartbeat_probe_pending == 0);
  CHECK(pair.session[1].heartbeat_outstanding_token == 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_heartbeat_reply_defers_replaceable_request(void)
{
  LinkPair pair;
  LinkPacket request;
  uint32_t host_heartbeats;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  link_protocol_packet_init(&request, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, 0);
  request.flags = (uint8_t)(9u << LINK_PROTOCOL_HEARTBEAT_TOKEN_SHIFT);
  CHECK(inject_packet(&pair, 0, &request, 0));
  link_session_pump(&pair.session[0]);
  host_heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];

  CHECK(link_session_publish_clock(&pair.session[0], 5000u) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats);

  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  CHECK(link_session_publish_clock(&pair.session[0], 6000u) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats + 1u);
}

static void test_only_matching_heartbeat_reply_clears_outstanding(void)
{
  LinkPair pair;
  LinkPacket reply;
  uint8_t outstanding;

  open_pair(&pair, 0);
  fake_link_network_set_drop(&pair.network, 1);
  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].heartbeat_probe_pending != 0);
  outstanding = pair.session[0].heartbeat_outstanding_token;

  link_protocol_packet_init(&reply, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, 0);
  reply.flags = (uint8_t)(LINK_PROTOCOL_HEARTBEAT_REPLY |
   (outstanding == 2u ? 4u : 2u));
  CHECK(inject_packet(&pair, 0, &reply, 0));
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].heartbeat_probe_pending != 0);
  CHECK(pair.session[0].heartbeat_outstanding_token == outstanding);
  CHECK(pair.session[0].heartbeat_unmatched_reply_count == 1u);

  reply.flags = (uint8_t)(LINK_PROTOCOL_HEARTBEAT_REPLY | outstanding);
  CHECK(inject_packet(&pair, 0, &reply, 0));
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].heartbeat_probe_pending == 0);
  CHECK(pair.session[0].heartbeat_outstanding_token == 0);
  CHECK(pair.session[0].heartbeat_reply_rx_count == 2u);
  CHECK(pair.session[0].heartbeat_unmatched_reply_count == 1u);
}

static void test_received_traffic_defers_host_probe(void)
{
  LinkPair pair;
  uint32_t host_heartbeats;

  open_pair(&pair, 0);
  host_heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];
  fake_link_network_advance(&pair.network, 900000);
  publish_sio_state(&pair, 1, 0);
  fake_link_network_advance(&pair.network, 100000);
  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats);

  fake_link_network_advance(&pair.network, 900000);
  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats + 1u);
}

static void test_outbound_traffic_does_not_hide_receive_silence(void)
{
  LinkPair pair;
  LinkSioState state;
  uint32_t host_heartbeats;

  open_pair(&pair, 0);
  host_heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];
  fake_link_network_advance(&pair.network, 900000u);
  state = pair_sio_state(&pair, 0, 1);
  state.control = 0x1234u;
  CHECK(link_session_publish_sio_state(&pair.session[0], &state) ==
   LINK_SESSION_EVENT_NONE);

  fake_link_network_advance(&pair.network, 100000u);
  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats + 1u);
}

static void test_transport_activity_keeps_session_alive(void)
{
  LinkPair pair;
  uint32_t tick;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  fake_link_network_set_drop(&pair.network, 1);

  for(tick = 0; tick < 12; tick++)
  {
    fake_link_network_advance(&pair.network,
     LINK_SESSION_LIVENESS_TIMEOUT_US - 100000u);
    CHECK(fake_link_network_activity(&pair.network, 0, 0));
    CHECK(fake_link_network_activity(&pair.network, 1, 0));
    pump_pair(&pair, 2);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  }

  fake_link_network_advance(&pair.network,
   LINK_SESSION_TIMEOUT_US);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(pair.network.recovery_requests[0] == 1u);
}

static void test_receive_silence_requests_recovery_symmetrically(void)
{
  LinkPair pair;
  unsigned int endpoint;

  for(endpoint = 0; endpoint < 2u; endpoint++)
  {
    open_pair(&pair, 0);
    publish_pair_sio(&pair, 1, 1);
    fake_link_network_set_drop(&pair.network, 1);

    fake_link_network_advance(&pair.network, LINK_SESSION_TIMEOUT_US - 1u);
    link_session_pump(&pair.session[endpoint]);
    CHECK(link_session_state(&pair.session[endpoint]) ==
     LINK_SESSION_CONNECTED);
    CHECK(!session_health_is(&pair.session[endpoint],
     LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
    CHECK(pair.network.recovery_requests[endpoint] == 0u);

    fake_link_network_advance(&pair.network, 1u);
    link_session_pump(&pair.session[endpoint]);
    CHECK(link_session_state(&pair.session[endpoint]) ==
     LINK_SESSION_CONNECTED);
    CHECK(session_health_is(&pair.session[endpoint],
     LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
    CHECK(pair.network.recovery_requests[endpoint] == 1u);
    CHECK(pair.network.recovery_reasons[endpoint] == LINK_REASON_TIMEOUT);
    CHECK(pair.network.recovery_timeout_sources[endpoint] ==
     LINK_TIMEOUT_RECEIVE_SILENCE);

    link_session_pump(&pair.session[endpoint]);
    CHECK(session_health_is(&pair.session[endpoint],
     LINK_TRANSPORT_HEALTH_RECOVERING));
    CHECK(pair.network.recovery_requests[endpoint] == 1u);
  }
}

static void test_receive_silence_recovery_rejection_fails_closed(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  fake_link_network_set_drop(&pair.network, 1);
  fake_link_network_set_recovery_failure(&pair.network, 1);
  fake_link_network_advance(&pair.network, LINK_SESSION_TIMEOUT_US);
  link_session_pump(&pair.session[1]);

  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_TRANSPORT);
  CHECK(pair.session[1].reason_detail == LINK_REASON_TIMEOUT);
  CHECK(!session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(pair.network.recovery_requests[1] == 1u);
  CHECK(pair.network.recovery_timeout_sources[1] ==
   LINK_TIMEOUT_RECEIVE_SILENCE);
}

static void test_receive_silence_recovery_request_has_bounded_wait(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  fake_link_network_set_drop(&pair.network, 1);
  fake_link_network_set_recovery_deferred(&pair.network, 1);
  fake_link_network_advance(&pair.network, LINK_SESSION_TIMEOUT_US);
  link_session_pump(&pair.session[1]);
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_LIVENESS_TIMEOUT_US - 1u);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.network.recovery_requests[1] == 1u);

  fake_link_network_advance(&pair.network, 1u);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_TRANSPORT);
  CHECK(pair.session[1].reason_detail == LINK_REASON_TIMEOUT);
  CHECK(pair.network.recovery_requests[1] == 1u);
  CHECK(pair.session[1].last_timeout_source ==
   LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
}

static void test_pressured_transport_alone_does_not_pause(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  CHECK(fake_link_network_health(&pair.network, 0, 1, 0));
  link_session_pump(&pair.session[0]);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_PRESSURED));
  CHECK(!link_session_in_barrier(&pair.session[0]));
  CHECK(!link_session_should_pause(&pair.session[0]));
  CHECK(!link_session_should_throttle(&pair.session[0]));
  CHECK(link_session_pause_reason(&pair.session[0]) ==
   LINK_SESSION_PAUSE_NONE);
}

static void test_stale_health_cannot_overlap_recovery(void)
{
  LinkPair pair;
  uint32_t generation;
  uint32_t stale_events;

  init_pair(&pair, 0);
  pair.network.generation = 6u;
  CHECK(link_session_host(&pair.session[0], 0x12345678) == 1);
  CHECK(link_session_join(&pair.session[1]) == 1);
  pump_pair(&pair, 8u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  generation = pair.session[0].transport_generation;
  CHECK(generation == 7u);

  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  link_session_pump(&pair.session[0]);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  stale_events = pair.session[0].stale_transport_events;

  CHECK(fake_link_network_health_generation(&pair.network, 0, 1,
   generation - 1u, 0));
  CHECK(fake_link_network_health_generation(&pair.network, 0, 1,
   generation, 0));
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].transport_generation == generation);
  CHECK(pair.session[0].stale_transport_events == stale_events + 1u);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  CHECK((link_session_check_invariants(&pair.session[0]) &
   LINK_INVARIANT_TRANSPORT_HEALTH) == 0u);
}

static void test_transport_degraded_shifts_deadlines(void)
{
  LinkPair pair;
  uint64_t barrier_started;
  uint32_t events;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  fake_link_network_set_drop(&pair.network, 1);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  barrier_started = pair.session[0].barrier_started_us;
  CHECK(fake_link_network_health(&pair.network, 0, 1, 0));
  link_session_pump(&pair.session[0]);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_PRESSURED));
  CHECK(!link_session_should_pause(&pair.session[0]));
  CHECK(link_session_pause_reason(&pair.session[0]) ==
   LINK_SESSION_PAUSE_NONE);

  fake_link_network_advance(&pair.network, LINK_SESSION_TIMEOUT_US - 1u);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0);
  CHECK(pair.session[0].barrier_active != 0);
  CHECK(pair.network.recovery_requests[0] == 0u);

  CHECK(fake_link_network_health(&pair.network, 0, 0, 0));
  link_session_pump(&pair.session[0]);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(pair.session[0].barrier_started_us == barrier_started +
   LINK_SESSION_TIMEOUT_US - 1u);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US - 1u);
  CHECK(fake_link_network_activity(&pair.network, 0, 0));
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0);
  fake_link_network_advance(&pair.network, 1u);
  events = link_session_pump(&pair.session[0]);
  /* A missing stream REPLY cannot prove that the peer did not apply it. */
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0);
  CHECK(pair.network.recovery_requests[0] == 1u);
  CHECK(pair.session[0].transport_health ==
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED);
}

static void test_prolonged_degradation_requests_recovery_once(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  CHECK(fake_link_network_health(&pair.network, 0, 1, 0));
  link_session_pump(&pair.session[0]);
  fake_link_network_advance(&pair.network, LINK_SESSION_TIMEOUT_US);
  link_session_pump(&pair.session[0]);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(pair.network.recovery_requests[0] == 1u);
  CHECK(pair.network.recovery_timeout_sources[0] == LINK_TIMEOUT_PRESSURE);
  link_session_pump(&pair.session[0]);
  CHECK(pair.network.recovery_requests[0] == 1u);
  pump_pair(&pair, 2u);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERING));
}

static void test_prepared_joiner_timeout_recovers_host_decision(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1357u, 0x2468u, 3u);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_COMMIT);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PREPARED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_DECIDED);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_DECIDED);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PREPARED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);
  CHECK(pair.session[1].barrier_active != 0);
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(pair.network.recovery_requests[1] == 1u);
  CHECK(pair.network.recovery_timeout_sources[1] ==
   LINK_TIMEOUT_ACTIVE_TRANSFER);

  pump_pair(&pair, 2u);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 12u);
  check_recovered_transfer(&pair);
}

static void test_prepared_joiner_mode_change_requests_recovery(void)
{
  LinkPair pair;
  LinkSioState state;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_ACCEPT);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PREPARED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);

  CHECK(change_reference_mode(&pair.sio[1], GBA_SIO_MODE_MULTIPLAYER) ==
   GBA_SIO_EVENT_MODE_CHANGE);
  state = pair_sio_state(&pair, 1u, 1);
  events = link_session_publish_sio_state(&pair.session[1], &state);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PREPARED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED));
  CHECK(pair.network.recovery_requests[1] == 1u);
}

static void test_recovery_replays_lost_prepare(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_set_drop(&pair.network, 1);
  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  pump_pair(&pair, 2);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_RECOVERING));

  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) == 0);
  CHECK(pair.session[0].barrier_active != 0);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_RECOVERING));

  fake_link_network_set_drop(&pair.network, 0);
  fake_link_network_set_duplicates(&pair.network, 1);
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 12);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);
}

static void recover_pair(LinkPair *pair)
{
  CHECK(fake_link_network_recovery(&pair->network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair->network, 1, 1, 0));
  pump_pair(pair, 2);
  CHECK(session_health_is(&pair->session[0],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  CHECK(session_health_is(&pair->session[1],
   LINK_TRANSPORT_HEALTH_RECOVERING));
  CHECK(fake_link_network_recovery(&pair->network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair->network, 1, 0, 0));
  pump_pair(pair, 12);
}

static void check_recovered_transfer(LinkPair *pair)
{
  CHECK(link_session_state(&pair->session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair->session[1]) == LINK_SESSION_CONNECTED);
  CHECK(session_health_is(&pair->session[0],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(session_health_is(&pair->session[1],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(pair->session[0].last_sequence == 1u);
  CHECK(pair->session[1].last_sequence == 1u);
  CHECK(pair->session[0].barrier_active == 0);
  CHECK(pair->session[1].barrier_active == 0);
  CHECK(pair->session[0].transaction_stage ==
   LINK_TRANSACTION_ACKNOWLEDGED);
  CHECK(pair->session[1].transaction_stage ==
   LINK_TRANSACTION_ACKNOWLEDGED);
  CHECK(pair->session[0].transaction_applied != 0);
  CHECK(pair->session[1].transaction_applied != 0);
  CHECK(link_session_check_invariants(&pair->session[0]) == 0);
  CHECK(link_session_check_invariants(&pair->session[1]) == 0);
}

static void test_stream_recovery_reencodes_unseen_start(void)
{
  LinkPair pair;
  uint32_t old_generation;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1357u, 0x2468u, 3u);
  old_generation = link_stream_generation(&pair.session[0].stream);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_START);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_STREAM_REPLY);
  CHECK(link_stream_latch_count(&pair.session[1].stream) == 0u);

  recover_pair(&pair);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);
  CHECK(link_stream_generation(&pair.session[0].stream) ==
   old_generation + 1u);
  CHECK(link_stream_generation(&pair.session[1].stream) ==
   old_generation + 1u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_START] == 2u);
  CHECK(link_stream_latch_count(&pair.session[1].stream) == 1u);
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 1u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 1u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_stream_recovery_replays_committed_reply_once(void)
{
  LinkPair pair;
  uint32_t joiner_latches;
  uint32_t joiner_applies;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1357u, 0x2468u, 3u);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_REPLY);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK((link_session_pump(&pair.session[1]) &
   LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_STREAM_REPLY);
  joiner_latches = link_stream_latch_count(&pair.session[1].stream);
  joiner_applies = link_stream_apply_count(&pair.session[1].stream);

  fake_link_network_set_duplicates(&pair.network, 1u);
  recover_pair(&pair);
  fake_link_network_set_duplicates(&pair.network, 0u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_REPLY] >= 2u);
  CHECK(link_stream_latch_count(&pair.session[1].stream) ==
   joiner_latches);
  CHECK(link_stream_apply_count(&pair.session[1].stream) ==
   joiner_applies);
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 1u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_stream_recovery_committed_pair_resumes_next_sequence(void)
{
  LinkPair pair;
  uint32_t starts;
  uint32_t replies;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4u);
  starts = pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_START];
  replies = pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_REPLY];
  recover_pair(&pair);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_TRANSFER_START] == starts);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_TRANSFER_REPLY] == replies);

  complete_pair_transfer(&pair);
  configure_multiplayer(&pair, 0x3333u, 0x4444u, 3u);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4u);
  CHECK(pair.session[0].last_sequence == 2u);
  CHECK(pair.session[1].last_sequence == 2u);
  CHECK(link_stream_apply_count(&pair.session[0].stream) == 2u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 2u);
}

static void test_stream_recovery_ignores_old_generation_start(void)
{
  LinkPair pair;
  LinkPacket packet;
  LinkStreamStart old_start;
  uint32_t stale;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1357u, 0x2468u, 3u);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_START);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  old_start = pair.session[0].stream.active_start;
  recover_pair(&pair);
  stale = pair.session[1].stream.stale_generation_count;

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_START,
   pair.session[0].session_id, 1u);
  CHECK(link_protocol_write_stream_start(&packet, &old_start) ==
   LINK_PROTOCOL_OK);
  CHECK(inject_packet(&pair, 1u, &packet, 0u));
  CHECK(link_session_pump(&pair.session[1]) == LINK_SESSION_EVENT_NONE);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].stream.stale_generation_count == stale + 1u);
  CHECK(link_stream_latch_count(&pair.session[1].stream) == 1u);
  CHECK(link_stream_apply_count(&pair.session[1].stream) == 1u);
}

static void test_stream_recovery_missing_reply_cache_fails_precisely(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1357u, 0x2468u, 3u);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_REPLY);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK((link_session_pump(&pair.session[1]) &
   LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
  pair.session[1].stream.reply_cache.valid = 0;

  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  pump_pair(&pair, 2u);
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 4u);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_PROTOCOL);
  CHECK(pair.session[1].reason_detail == 0x5305u);
}

static void check_recovered_cancellation(LinkPair *pair,
 uint32_t sequence)
{
  CHECK(link_session_state(&pair->session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair->session[1]) == LINK_SESSION_CONNECTED);
  CHECK(session_health_is(&pair->session[0],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(session_health_is(&pair->session[1],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(pair->session[0].last_sequence == sequence);
  CHECK(pair->session[1].last_sequence == sequence);
  CHECK(pair->session[0].barrier_active == 0);
  CHECK(pair->session[1].barrier_active == 0);
  CHECK(pair->session[0].transaction_stage == LINK_TRANSACTION_ABORTED);
  CHECK(pair->session[1].transaction_stage == LINK_TRANSACTION_ABORTED);
  CHECK(pair->session[0].transaction_applied == 0);
  CHECK(pair->session[1].transaction_applied == 0);
  CHECK(link_session_check_invariants(&pair->session[0]) == 0);
  CHECK(link_session_check_invariants(&pair->session[1]) == 0);
}

static void test_recovery_cancels_deferred_prepare(void)
{
  LinkPair pair;
  uint32_t cycles = defer_next_multiplayer_prepare(&pair, 0x3333u);

  recover_pair(&pair);
  CHECK(session_health_is(&pair.session[0],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(session_health_is(&pair.session[1],
   LINK_TRANSPORT_HEALTH_HEALTHY));
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 2u);
  CHECK(pair.session[1].last_sequence == 2u);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_ABORTED);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_ABORTED);
  CHECK(pair.session[1].deferred_prepare_valid == 0);
  CHECK(gba_sio_next_event(&pair.sio[1]) == cycles);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_recovery_confirms_lost_abort_ack(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_PREPARE);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_ABORT_ACK);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);

  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_ABORTED);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_ABORTED);

  recover_pair(&pair);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 0u);
  CHECK(pair.session[1].last_sequence == 0u);
  CHECK(pair.session[0].transaction_applied == 0);
  CHECK(pair.session[1].transaction_applied == 0);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_EMPTY);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_EMPTY);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_recovery_propagates_locally_aborted_transfer(void)
{
  LinkPair pair;
  uint32_t events;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_PREPARE);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_ABORT);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_TRANSFER_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_ABORT_ACK);
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[0].active_sequence == 1u);
  CHECK(pair.session[0].transaction_sequence == 1u);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_ABORTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].last_sequence == 0u);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_EMPTY);

  recover_pair(&pair);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 0u);
  CHECK(pair.session[1].last_sequence == 0u);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_EMPTY);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_EMPTY);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_recovery_republishes_presence(void)
{
  LinkPair pair;
  uint32_t sent[2];

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  sent[0] = pair.network.sent_types[0][LINK_MESSAGE_SIO_STATE];
  sent[1] = pair.network.sent_types[1][LINK_MESSAGE_SIO_STATE];
  pair.session[0].heartbeat_last_request_token = 2u;
  pair.session[0].heartbeat_last_request_valid = 1u;
  pair.session[0].heartbeat_probe_pending = 1u;
  pair.session[0].heartbeat_outstanding_token = 4u;
  pair.session[1].heartbeat_last_request_token = 4u;
  pair.session[1].heartbeat_last_request_valid = 1u;

  recover_pair(&pair);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].local_sio_sent_valid == 0);
  CHECK(pair.session[1].local_sio_sent_valid == 0);
  CHECK(pair.session[0].heartbeat_last_request_valid == 0);
  CHECK(pair.session[0].heartbeat_last_request_token == 0);
  CHECK(pair.session[0].heartbeat_probe_pending == 0);
  CHECK(pair.session[0].heartbeat_outstanding_token == 0);
  CHECK(pair.session[1].heartbeat_last_request_valid == 0);
  CHECK(pair.session[1].heartbeat_last_request_token == 0);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_SIO_STATE] == sent[0]);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_SIO_STATE] == sent[1]);
}

static void test_recovery_scopes_heartbeat_tokens_to_generation(void)
{
  uint8_t endpoint;

  for(endpoint = 0; endpoint < 2u; endpoint++)
  {
    LinkPair pair;
    LinkPacket packet;
    LinkStreamHeartbeat heartbeat;
    uint32_t replies;
    uint32_t requests;
    uint32_t events;
    uint8_t token = (uint8_t)(5u << LINK_PROTOCOL_HEARTBEAT_TOKEN_SHIFT);

    open_pair(&pair, 0);
    publish_pair_sio(&pair, 1, 1);
    heartbeat.generation = link_stream_generation(&pair.session[endpoint].stream);
    heartbeat.clock_cycle = 0;
    heartbeat.acknowledged_sequence = 0;
    link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
     pair.session[endpoint].session_id, 0);
    CHECK(link_protocol_write_stream_heartbeat(&packet, &heartbeat) ==
     LINK_PROTOCOL_OK);
    packet.flags = token;
    replies = pair.session[endpoint].heartbeat_reply_tx_count;
    CHECK(inject_packet(&pair, endpoint, &packet, 0));
    pump_pair(&pair, 2);
    CHECK(pair.session[endpoint].heartbeat_reply_tx_count == replies + 1u);
    replies = pair.session[endpoint].heartbeat_reply_tx_count;
    requests = pair.session[endpoint].heartbeat_request_rx_count;

    recover_pair(&pair);
    CHECK(link_session_state(&pair.session[endpoint]) == LINK_SESSION_CONNECTED);
    CHECK(link_stream_generation(&pair.session[endpoint].stream) !=
     heartbeat.generation);
    CHECK(pair.session[endpoint].heartbeat_reply_tx_count == replies);
    CHECK(pair.session[endpoint].heartbeat_request_rx_count == requests);

    /* A delayed request from the previous generation must not seed the
       fresh duplicate filter or elicit a reply. */
    CHECK(inject_packet(&pair, endpoint, &packet, 0));
    events = pump_pair(&pair, 2);
    CHECK(pair.session[endpoint].heartbeat_reply_tx_count == replies);
    CHECK(pair.session[endpoint].heartbeat_request_rx_count == requests);

    heartbeat.generation = link_stream_generation(&pair.session[endpoint].stream);
    CHECK(link_protocol_write_stream_heartbeat(&packet, &heartbeat) ==
     LINK_PROTOCOL_OK);
    packet.flags = token;
    CHECK(inject_packet(&pair, endpoint, &packet, 0));
    events |= pump_pair(&pair, 2);
    CHECK(pair.session[endpoint].heartbeat_reply_tx_count == replies + 1u);
    CHECK(pair.network.sent_flags[endpoint][LINK_MESSAGE_HEARTBEAT] ==
     (uint8_t)(token | LINK_PROTOCOL_HEARTBEAT_REPLY));
    CHECK(pair.session[endpoint].heartbeat_duplicate_request_count == 0u);

    CHECK(inject_packet(&pair, endpoint, &packet, 0));
    events |= pump_pair(&pair, 2);
    CHECK(pair.session[endpoint].heartbeat_reply_tx_count == replies + 1u);
    CHECK(pair.session[endpoint].heartbeat_duplicate_request_count == 1u);
    CHECK((events & (LINK_SESSION_EVENT_SERIAL_IRQ |
     LINK_SESSION_EVENT_TRANSFER_COMMITTED |
     LINK_SESSION_EVENT_TRANSFER_ABORTED)) == 0);
    CHECK(pair.session[0].last_sequence == 0u);
    CHECK(pair.session[1].last_sequence == 0u);
    CHECK(link_session_check_invariants(&pair.session[0]) == 0);
    CHECK(link_session_check_invariants(&pair.session[1]) == 0);
  }
}

static void test_recovery_replays_lost_accept(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_ACCEPT);
  link_session_begin_transfer(&pair.session[0]);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
  recover_pair(&pair);
  check_recovered_cancellation(&pair, 1u);
}

static void test_invariant_audit_rejects_mismatched_record(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111u, 0x2222u, 3u);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_ACCEPT);
  link_session_begin_transfer(&pair.session[0]);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
  CHECK(pair.session[0].last_sequence == 0u);
  CHECK(pair.session[1].last_sequence == 0u);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PREPARED);
  pair.session[1].transfer.data[0] ^= 1u;
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_PROTOCOL);
  CHECK((pair.session[1].invariant_violations &
   LINK_INVARIANT_TRANSACTION_RECORD) != 0);
  CHECK(pair.session[0].transaction_applied == 0);
  CHECK(pair.session[1].transaction_applied == 0);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
}

static void test_recovery_replays_lost_commit(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_COMMIT);
  link_session_begin_transfer(&pair.session[0]);
  link_session_pump(&pair.session[1]);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_COMMIT_ACK);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
  recover_pair(&pair);
  check_recovered_transfer(&pair);
}

static void test_recovery_confirms_lost_commit_ack(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_COMMIT_ACK);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 3);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_COMMIT_ACK);
  CHECK(link_session_state(&pair.session[1]) ==
   LINK_SESSION_WAIT_COMMIT_RELEASE);
  recover_pair(&pair);
  check_recovered_transfer(&pair);
}

static void test_recovery_commit_ack_across_sequence_wrap(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  pair.session[0].last_sequence = UINT32_MAX;
  pair.session[1].last_sequence = UINT32_MAX;
  pair.session[0].journal.checkpoint = UINT32_MAX;
  pair.session[1].journal.checkpoint = UINT32_MAX;
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_COMMIT_ACK);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 3);
  CHECK(pair.session[0].last_sequence == UINT32_MAX);
  CHECK(pair.session[1].last_sequence == UINT32_MAX);
  recover_pair(&pair);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].last_sequence == 0u);
  CHECK(pair.session[1].last_sequence == 0u);
}

static void test_recovery_replays_joiner_transfer_request(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  configure_normal(&pair, GBA_SIO_MODE_NORMAL_8, 1, 1, 0x44u, 0xDDu);
  fake_link_network_drop_next(&pair.network, 1,
   LINK_MESSAGE_TRANSFER_REQUEST);
  link_session_begin_transfer(&pair.session[1]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_PREPARE);
  recover_pair(&pair);
  check_recovered_cancellation(&pair, 1u);
}

static void test_recovery_replays_joiner_prepare(void)
{
  LinkPair pair;

  open_transactional_pair(&pair);
  configure_normal(&pair, GBA_SIO_MODE_NORMAL_8, 1u, 1u,
   0x44u, 0xDDu);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_TRANSFER_PREPARE);
  link_session_begin_transfer(&pair.session[1]);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_PREPARE);
  CHECK(pair.session[0].last_sequence == 0u);
  CHECK(pair.session[1].last_sequence == 0u);
  CHECK(pair.session[0].transaction_stage == LINK_TRANSACTION_PREPARED);
  CHECK(pair.session[1].transaction_stage == LINK_TRANSACTION_PROPOSED);
  recover_pair(&pair);
  check_recovered_cancellation(&pair, 1u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_resume_handshake_waits_for_transport_error(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  pump_pair(&pair, 2);
  fake_link_network_set_drop(&pair.network, 1);
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 2);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_RESUME_ACK);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_LIVENESS_TIMEOUT_US * 4u);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_RESUME_ACK);

  fake_link_network_close(&pair.network, -99);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_TRANSPORT);
}

static void test_nested_transport_recovery_reuses_coordinator(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  pump_pair(&pair, 2);
  fake_link_network_set_drop(&pair.network, 1);
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 2);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_RESUME_ACK);

  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  pump_pair(&pair, 2);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_RECOVERING);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_RECOVERING);
  CHECK(pair.session[0].recovery_previous_state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].recovery_previous_state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].recovery.round == 1u);
  CHECK(pair.session[1].recovery.round == 1u);

  fake_link_network_set_drop(&pair.network, 0);
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 12);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].recovery.round == 1u);
  CHECK(pair.session[1].recovery.round == 1u);
  CHECK(pair.session[0].journal.epoch == 2u);
  CHECK(pair.session[1].journal.epoch == 2u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_joiner_recovery_waits_for_transport_error(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_RECOVERING);
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_RECOVERING);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_LIVENESS_TIMEOUT_US * 4u);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_RECOVERING);

  fake_link_network_close(&pair.network, -99);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_TRANSPORT);
}

static void test_duplicate_resume_ack_replays_resume_done(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  fake_link_network_set_duplicates(&pair.network, 1);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_RESUME_DONE);
  recover_pair(&pair);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_RESUME_DONE] >= 2u);
}

static void test_resume_ack_retry_replays_lost_resume_done(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  fake_link_network_drop_next(&pair.network, 0,
   LINK_MESSAGE_RESUME_DONE);
  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  pump_pair(&pair, 2);
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 8);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) ==
   LINK_SESSION_WAIT_RESUME_DONE);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_RESUME_RETRY_US);
  pump_pair(&pair, 8);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_RESUME_ACK] >= 2u);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_RESUME_DONE] >= 2u);
}

static void test_resume_retry_survives_long_finite_loss(void)
{
  LinkPair pair;
  uint32_t initial_resumes;
  unsigned int retry;

  open_pair(&pair, 0);
  CHECK(fake_link_network_recovery(&pair.network, 0, 1, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 1, 0));
  pump_pair(&pair, 2);
  fake_link_network_set_drop(&pair.network, 1);
  CHECK(fake_link_network_recovery(&pair.network, 0, 0, 0));
  CHECK(fake_link_network_recovery(&pair.network, 1, 0, 0));
  pump_pair(&pair, 2);
  CHECK(link_session_state(&pair.session[0]) ==
   LINK_SESSION_WAIT_RESUME_ACK);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_RECOVERING);
  initial_resumes = pair.network.sent_types[0][LINK_MESSAGE_RESUME];

  for(retry = 0; retry < 128u; retry++)
  {
    fake_link_network_advance(&pair.network,
     LINK_SESSION_RESUME_RETRY_US);
    pump_pair(&pair, 2);
    CHECK(link_session_state(&pair.session[0]) ==
     LINK_SESSION_WAIT_RESUME_ACK);
    CHECK(link_session_state(&pair.session[1]) ==
     LINK_SESSION_RECOVERING);
    CHECK(link_session_check_invariants(&pair.session[0]) == 0u);
    CHECK(link_session_check_invariants(&pair.session[1]) == 0u);
  }
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_RESUME] ==
   initial_resumes + 128u);

  fake_link_network_set_drop(&pair.network, 0);
  fake_link_network_advance(&pair.network,
   LINK_SESSION_RESUME_RETRY_US);
  pump_pair(&pair, 12);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0u);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0u);
}

static void test_clock_sync_bounds_hot_peer_lead(void)
{
  LinkPair pair;
  uint32_t host_cycle = LINK_CLOCK_SYNC_INTERVAL_CYCLES;
  uint32_t joiner_cycle = host_cycle + LINK_CLOCK_MAX_LEAD_CYCLES + 1u;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  CHECK(link_session_publish_clock(&pair.session[0], 0u) ==
   LINK_SESSION_EVENT_NONE);
  CHECK(link_session_publish_clock(&pair.session[1], 0u) ==
   LINK_SESSION_EVENT_NONE);
  pump_pair(&pair, 2);

  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 5);
  complete_pair_transfer(&pair);

  CHECK(link_session_publish_clock(&pair.session[0], host_cycle) ==
   LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair.session[1]);
  link_session_set_emulated_cycle(&pair.session[1], joiner_cycle);
  CHECK(link_session_should_throttle(&pair.session[1]));
  CHECK(link_session_pause_reason(&pair.session[1]) ==
   LINK_SESSION_PAUSE_AHEAD_PACING);

  fake_link_network_advance(&pair.network,
   LINK_SESSION_STREAM_PACING_US);
  CHECK(!link_session_should_throttle(&pair.session[1]));
  CHECK(pair.network.recovery_requests[1] == 0u);
}

static void test_lost_heartbeat_reply_uses_new_token_each_interval(void)
{
  LinkPair pair;
  uint32_t host_heartbeats;
  uint32_t second;
  uint8_t previous_token = 0;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 1);
  fake_link_network_set_drop(&pair.network, 1);
  host_heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];

  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats + 1u);
  CHECK(pair.session[0].heartbeat_probe_pending != 0);
  previous_token = pair.session[0].heartbeat_outstanding_token;
  CHECK(previous_token != 0);
  CHECK(pair.network.sent_flags[0][LINK_MESSAGE_HEARTBEAT] == previous_token);

  for(second = 0; second < 3; second++)
  {
    fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
    link_session_pump(&pair.session[0]);
    CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
     host_heartbeats + second + 2u);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(pair.session[0].heartbeat_probe_pending != 0);
    CHECK(pair.session[0].heartbeat_outstanding_token != previous_token);
    previous_token = pair.session[0].heartbeat_outstanding_token;
    CHECK(pair.network.sent_flags[0][LINK_MESSAGE_HEARTBEAT] ==
     previous_token);
  }

  CHECK(fake_link_network_activity(&pair.network, 0, 0));
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].heartbeat_probe_pending != 0);
  CHECK(pair.session[0].heartbeat_outstanding_token == previous_token);
  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] ==
   host_heartbeats + 5u);
  CHECK(pair.session[0].heartbeat_outstanding_token != previous_token);
}

static void test_heartbeat_is_independent_of_sio_sequence(void)
{
  LinkPair pair;
  LinkPacket delayed_reply;
  uint32_t events;
  uint32_t heartbeat_rx_count;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1234, 0x5678, 3);
  events = link_session_begin_transfer(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  pump_pair(&pair, 4);
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);
  complete_pair_transfer(&pair);
  heartbeat_rx_count = pair.session[0].heartbeat_rx_count;

  link_protocol_packet_init(&delayed_reply, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, 0);
  delayed_reply.flags = LINK_PROTOCOL_HEARTBEAT_REPLY;
  CHECK(inject_packet(&pair, 0, &delayed_reply, 0));
  events = link_session_pump(&pair.session[0]);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) == 0);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].heartbeat_rx_count == heartbeat_rx_count + 1u);

  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  link_session_pump(&pair.session[0]);
  CHECK(pair.network.sent_sequences[0][LINK_MESSAGE_HEARTBEAT] == 0u);
  pump_pair(&pair, 2);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_asymmetric_liveness_under_network_faults(void)
{
  LinkPair pair;
  uint32_t tick;
  uint32_t host_heartbeats;
  uint32_t joiner_heartbeats;
  uint32_t host_heartbeat_tx_count;
  uint32_t joiner_heartbeat_tx_count;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 1, 0);
  fake_link_network_set_latency(&pair.network, 0, 200000);
  fake_link_network_set_latency(&pair.network, 1, 200000);
  fake_link_network_set_duplicates(&pair.network, 1);
  host_heartbeats = pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT];
  joiner_heartbeats = pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT];
  host_heartbeat_tx_count = pair.session[0].heartbeat_tx_count;
  joiner_heartbeat_tx_count = pair.session[1].heartbeat_tx_count;

  for(tick = 0; tick < 300; tick++)
  {
    fake_link_network_set_drop(&pair.network, tick >= 80 && tick < 100);
    fake_link_network_advance(&pair.network, 100000);
    if(tick == 100)
    {
      CHECK(fake_link_network_activity(&pair.network, 0, 0));
      CHECK(fake_link_network_activity(&pair.network, 1, 0));
    }
    pump_pair(&pair, 4);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  }

  fake_link_network_set_drop(&pair.network, 0);
  CHECK(pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] >=
   host_heartbeats + 20u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] >=
   pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] - host_heartbeats +
   joiner_heartbeats - 3u);
  CHECK(pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] <=
   pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] - host_heartbeats +
   joiner_heartbeats);
  CHECK(pair.session[0].heartbeat_tx_count - host_heartbeat_tx_count ==
   pair.network.sent_types[0][LINK_MESSAGE_HEARTBEAT] - host_heartbeats);
  CHECK(pair.session[1].heartbeat_tx_count - joiner_heartbeat_tx_count ==
   pair.network.sent_types[1][LINK_MESSAGE_HEARTBEAT] - joiner_heartbeats);
}

static void test_queue_full_aborts_transfer(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;
  uint32_t i;
  uint32_t events;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, pair.session[0].last_sequence);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);
  for(i = 0; i < FAKE_LINK_QUEUE_CAPACITY; i++)
    CHECK(fake_link_network_inject(&pair.network, 1, frame, frame_size, 0));

  events = link_session_begin_transfer(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_QUEUE_FULL);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0);
}

static void test_disconnect_reconnect(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  link_session_disconnect(&pair.session[1], LINK_REASON_USER);
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_DISCONNECTED);
  CHECK(pair.session[0].reason == LINK_REASON_USER);

  CHECK(link_session_host(&pair.session[0], 0xABCDEF01));
  CHECK(link_session_join(&pair.session[1]));
  pump_pair(&pair, 8);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].heartbeat_tx_count == 0u);
  CHECK(pair.session[0].heartbeat_rx_count == 0u);
  CHECK(pair.session[1].heartbeat_tx_count == 0u);
  CHECK(pair.session[1].heartbeat_rx_count == 0u);
}

static void test_sequence_wrap(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  pair.session[0].last_sequence = UINT32_MAX;
  pair.session[1].last_sequence = UINT32_MAX;
  pair.session[0].journal.checkpoint = UINT32_MAX;
  pair.session[1].journal.checkpoint = UINT32_MAX;
  configure_multiplayer(&pair, 0x0102, 0x0304, 3);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 4);
  CHECK(pair.session[0].last_sequence == 1u);
  CHECK(pair.session[1].last_sequence == 1u);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
}

static void test_ten_consecutive_transfers(void)
{
  LinkPair pair;
  uint32_t transfer;

  open_pair(&pair, 0);
  for(transfer = 0; transfer < 10; transfer++)
  {
    configure_multiplayer(&pair, (uint16_t)(0x1000 + transfer),
     (uint16_t)(0x2000 + transfer), 3);
    link_session_begin_transfer(&pair.session[0]);
    fake_link_network_advance(&pair.network, 1000u);
    pump_pair(&pair, 4);
    complete_pair_transfer(&pair);
    CHECK(pair.session[0].last_sequence == transfer + 1);
    CHECK(pair.session[1].last_sequence == transfer + 1);
  }
  CHECK(pair.session[0].barrier_last_us == 1000u);
  CHECK(pair.session[0].barrier_max_us == 1000u);
  CHECK(pair.session[0].stream_burst_last_us == 9000u);
  CHECK(pair.session[0].stream_burst_max_us == 9000u);
  CHECK(pair.session[0].stream_burst_words == 1u);
}

static void test_benchmark_uses_stream_without_gba_barrier(void)
{
  LinkPair pair;
  LinkBenchmarkControl control;
  LinkBenchmarkSummary host;
  LinkBenchmarkSummary joiner;
  uint32_t word;

  init_no_rom_pair(&pair);
  CHECK(link_session_host(&pair.session[0], 0x12345678u));
  CHECK(link_session_join(&pair.session[1]));
  pump_pair(&pair, 8);
  link_benchmark_default_control(&control, 0xCAFEBABEu, 0x12345678u);
  control.duration_ms = 18u;
  control.target_words_per_second = 1000u;
  CHECK(link_session_benchmark_available(&pair.session[0]));
  CHECK(link_session_benchmark_start(&pair.session[0], &control));
  pump_pair(&pair, 2u);

  for(word = 0; word < 18u; word++)
  {
    fake_link_network_advance(&pair.network, 500u);
    link_session_pump(&pair.session[1]);
    fake_link_network_advance(&pair.network, 500u);
    link_session_pump(&pair.session[0]);
    CHECK(!link_session_in_barrier(&pair.session[0]));
    CHECK(!link_session_in_barrier(&pair.session[1]));
    CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);
    CHECK(gba_sio_next_event(&pair.sio[1]) == GBA_SIO_NO_EVENT);
  }
  pump_pair(&pair, 3u);
  link_session_benchmark_summary(&pair.session[0], &host);
  link_session_benchmark_summary(&pair.session[1], &joiner);
  CHECK(host.status == LINK_BENCHMARK_PASS);
  CHECK(joiner.status == LINK_BENCHMARK_PASS);
  CHECK(host.completed_words == 18u);
  CHECK(host.completed_words == joiner.completed_words);
  CHECK(host.digest == joiner.digest);
  CHECK(pair.session[0].stream_start_tx_count == 18u);
  CHECK(pair.session[1].stream_reply_tx_count == 18u);
  CHECK(pair.session[0].last_sequence == 0u);
  CHECK(pair.session[1].last_sequence == 0u);
}

static void start_long_benchmark(LinkPair *pair)
{
  LinkBenchmarkControl control;

  init_no_rom_pair(pair);
  CHECK(link_session_host(&pair->session[0], 0x12345678u));
  CHECK(link_session_join(&pair->session[1]));
  pump_pair(pair, 8);
  publish_pair_sio(pair, 0, 0);
  link_benchmark_default_control(&control, 0xCAFEBABEu, 0x12345678u);
  CHECK(link_session_benchmark_start(&pair->session[0], &control));
  pump_pair(pair, 4);
  CHECK(link_benchmark_active(&pair->session[0].benchmark));
  CHECK(link_benchmark_active(&pair->session[1].benchmark));
}

static void test_benchmark_terminates_when_session_closes(void)
{
  LinkPair pair;
  LinkSessionReason reasons[] = { LINK_REASON_USER, LINK_REASON_SUSPEND,
   LINK_REASON_ROM_CHANGE };
  unsigned int index;
  uint32_t events;

  for(index = 0; index < sizeof(reasons) / sizeof(reasons[0]); index++)
  {
    start_long_benchmark(&pair);
    events = link_session_disconnect(&pair.session[0], reasons[index]);
    CHECK(events & LINK_SESSION_EVENT_BENCHMARK_UPDATE);
    CHECK(link_benchmark_terminal(&pair.session[0].benchmark));
    CHECK(pair.session[0].benchmark.status == LINK_BENCHMARK_ABORT);
    CHECK(pair.session[0].benchmark.failure ==
     LINK_BENCHMARK_FAILURE_LOCAL_ABORT);
  }

  start_long_benchmark(&pair);
  fake_link_network_close(&pair.network, -1);
  events = link_session_pump(&pair.session[0]);
  CHECK(events & LINK_SESSION_EVENT_BENCHMARK_UPDATE);
  CHECK(pair.session[0].benchmark.status == LINK_BENCHMARK_FAIL);
  CHECK(pair.session[0].benchmark.failure == LINK_BENCHMARK_FAILURE_STREAM);
}

static void test_benchmark_joiner_detects_silent_host_in_menu(void)
{
  LinkPair pair;

  start_long_benchmark(&pair);
  fake_link_network_set_drop(&pair.network, 1);
  fake_link_network_advance(&pair.network, LINK_BENCHMARK_WORD_TIMEOUT_US);
  link_session_pump(&pair.session[1]);
  fake_link_network_advance(&pair.network, LINK_BENCHMARK_CONTROL_TIMEOUT_US);
  link_session_pump(&pair.session[1]);
  CHECK(link_benchmark_terminal(&pair.session[1].benchmark));
  CHECK(pair.session[1].benchmark.failure == LINK_BENCHMARK_FAILURE_TIMEOUT);
}

static void test_lost_sio_presence_is_refreshed_without_recovery(void)
{
  LinkPair pair;
  LinkSioState state;

  open_pair(&pair, 0);
  publish_pair_sio(&pair, 0, 0);
  select_mode(&pair.sio[0], GBA_SIO_MODE_MULTIPLAYER);
  select_mode(&pair.sio[1], GBA_SIO_MODE_MULTIPLAYER);
  fake_link_network_set_drop(&pair.network, 1);
  state = pair_sio_state(&pair, 0, 1);
  link_session_publish_sio_state(&pair.session[0], &state);
  state = pair_sio_state(&pair, 1, 1);
  link_session_publish_sio_state(&pair.session[1], &state);
  fake_link_network_set_drop(&pair.network, 0);
  fake_link_network_advance(&pair.network, LINK_SESSION_HEARTBEAT_US);
  pump_pair(&pair, 4);
  CHECK(pair.session[0].peer_sio.flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE);
  CHECK(pair.session[1].peer_sio.flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE);
  CHECK(pair.session[0].peer_sio.mode == GBA_SIO_MODE_MULTIPLAYER);
  CHECK(pair.session[1].peer_sio.mode == GBA_SIO_MODE_MULTIPLAYER);
  CHECK(pair.network.recovery_requests[0] == 0u);
  CHECK(pair.network.recovery_requests[1] == 0u);
}

static void test_stream_next_start_waits_for_guest_serial_handler(
 uint32_t initial_cycle, int recover)
{
  LinkPair pair;
  uint32_t cycles;
  uint32_t events;
  const uint32_t timer3_gap = 197u * 64u;
  const uint32_t joiner_start = initial_cycle + 0x100u;

  open_pair(&pair, 0);
  link_session_set_emulated_cycle(&pair.session[0], initial_cycle);
  link_session_set_emulated_cycle(&pair.session[1], joiner_start);
  configure_multiplayer(&pair, 0x1234u, 0x5678u, 3u);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 4);
  cycles = gba_sio_next_event(&pair.sio[0]);
  CHECK(gba_sio_advance(&pair.sio[0], cycles) == GBA_SIO_EVENT_SERIAL_IRQ);
  link_session_set_emulated_cycle(&pair.session[0],
   initial_cycle + cycles + timer3_gap);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 0xABCDu);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6083u);
  link_session_begin_transfer(&pair.session[0]);

  /* The network thread may deliver START(2) before the slower PSP has
     completed word 1. Pumping packets is not executing the guest IRQ. */
  events = link_session_pump(&pair.session[1]);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) == 0);
  CHECK(link_stream_latch_count(&pair.session[1].stream) == 2u);
  CHECK(pair.session[1].last_sequence == 1u);
  CHECK(gba_sio_advance(&pair.sio[1], cycles) == GBA_SIO_EVENT_SERIAL_IRQ);
  pump_pair(&pair, 4);
  CHECK(pair.session[1].last_sequence == 1u);
  CHECK(pair.sio[1].outgoing == 0x5678u);
  if(recover)
  {
    recover_pair(&pair);
    CHECK(pair.session[0].last_sequence == 1u);
    CHECK(pair.session[1].last_sequence == 1u);
    CHECK(link_session_check_invariants(&pair.session[0]) == 0);
    CHECK(link_session_check_invariants(&pair.session[1]) == 0);
  }

  /* FireRed's SerialCB computes the next word, then Timer3 grants the
     master a 12608-cycle gap. The next latch must see that new word. */
  gba_sio_write16(&pair.sio[1], GBA_SIO_REG_SEND, 0xEF01u);
  link_session_set_emulated_cycle(&pair.session[1],
   joiner_start + cycles + timer3_gap);
  pump_pair(&pair, 4);
  CHECK(pair.session[0].last_sequence == 2u);
  CHECK(pair.session[1].last_sequence == 2u);
  CHECK(pair.session[0].applied_transfer.data[1] == 0xEF01u);
  CHECK(pair.session[1].applied_transfer.data[1] == 0xEF01u);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void test_stream_disconnect_preserves_scheduled_result(void)
{
  LinkPair pair;
  uint32_t cycles;

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1234u, 0x5678u, 3u);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 4);
  cycles = gba_sio_next_event(&pair.sio[1]);
  CHECK(cycles != GBA_SIO_NO_EVENT);
  CHECK((link_session_disconnect(&pair.session[1], LINK_REASON_USER) &
   LINK_SESSION_EVENT_SERIAL_IRQ) == 0);
  CHECK(gba_sio_advance(&pair.sio[1], cycles) == GBA_SIO_EVENT_SERIAL_IRQ);
  CHECK(pair.registers[1][GBA_SIO_REG_MULTI0 >> 1] == 0x1234u);
  CHECK(pair.registers[1][GBA_SIO_REG_MULTI1 >> 1] == 0x5678u);
}

static uint32_t stress_random(uint32_t *state)
{
  uint32_t value = *state;

  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;
  return value;
}

static void wait_for_transfer(LinkPair *pair)
{
  uint32_t step;

  for(step = 0; step < 200; step++)
  {
    pump_pair(pair, 1);
    link_session_begin_transfer(&pair->session[0]);
    link_session_begin_transfer(&pair->session[1]);
    if(link_session_state(&pair->session[0]) == LINK_SESSION_CONNECTED &&
       link_session_state(&pair->session[1]) == LINK_SESSION_CONNECTED &&
       gba_sio_next_event(&pair->sio[0]) != GBA_SIO_NO_EVENT &&
       gba_sio_next_event(&pair->sio[1]) != GBA_SIO_NO_EVENT)
      return;
    if(link_session_state(&pair->session[0]) == LINK_SESSION_ERROR ||
       link_session_state(&pair->session[1]) == LINK_SESSION_ERROR)
      break;
    fake_link_network_advance(&pair->network, 1000);
  }

  CHECK(0 && "link transfer did not complete");
}

static void test_transfer_stress(void)
{
  LinkPair pair;
  uint32_t random_state = 0x6A09E667u;
  uint32_t transfer;

  open_pair(&pair, 1);
  for(transfer = 0; transfer < 5000; transfer++)
  {
    GbaSioMode mode = (GbaSioMode)(stress_random(&random_state) % 3u);
    uint8_t setting = (uint8_t)(stress_random(&random_state) &
     (mode == GBA_SIO_MODE_MULTIPLAYER ? 3u : 1u));
    uint8_t clock_owner = mode == GBA_SIO_MODE_MULTIPLAYER ? 0 :
     (uint8_t)(stress_random(&random_state) & 1u);
    uint32_t first = stress_random(&random_state);
    uint32_t second = stress_random(&random_state);
    uint32_t expected_cycles;
    uint32_t events;

    fake_link_network_set_latency(&pair.network, 0,
     stress_random(&random_state) % 20001u);
    fake_link_network_set_latency(&pair.network, 1,
     stress_random(&random_state) % 20001u);

    if(mode == GBA_SIO_MODE_MULTIPLAYER)
      configure_multiplayer(&pair, (uint16_t)first, (uint16_t)second,
       setting);
    else
      configure_normal(&pair, mode, clock_owner, setting, first, second);

    events = link_session_begin_transfer(&pair.session[clock_owner]);
    CHECK(events == LINK_SESSION_EVENT_NONE ||
     (events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
    wait_for_transfer(&pair);

    expected_cycles = gba_sio_transfer_cycles(mode, setting, 2);
    CHECK(gba_sio_next_event(&pair.sio[0]) == expected_cycles);
    CHECK(gba_sio_next_event(&pair.sio[1]) == expected_cycles);
    complete_pair_transfer(&pair);

    if(mode == GBA_SIO_MODE_MULTIPLAYER)
    {
      CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == (uint16_t)first);
      CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == (uint16_t)second);
      CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI0) == (uint16_t)first);
      CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI1) == (uint16_t)second);
    }
    else if(mode == GBA_SIO_MODE_NORMAL_32)
    {
      CHECK(pair_reg(&pair, 0, GBA_SIO_REG_DATA32_L) ==
       (uint16_t)second);
      CHECK(pair_reg(&pair, 0, GBA_SIO_REG_DATA32_H) ==
       (uint16_t)(second >> 16));
      CHECK(pair_reg(&pair, 1, GBA_SIO_REG_DATA32_L) == (uint16_t)first);
      CHECK(pair_reg(&pair, 1, GBA_SIO_REG_DATA32_H) ==
       (uint16_t)(first >> 16));
    }
    else
    {
      CHECK(pair_reg(&pair, 0, GBA_SIO_REG_SEND) ==
       (uint16_t)(second & 0xFFu));
      CHECK(pair_reg(&pair, 1, GBA_SIO_REG_SEND) ==
       (uint16_t)(first & 0xFFu));
    }

    CHECK(pair.session[0].last_sequence == transfer + 1u);
    CHECK(pair.session[1].last_sequence == transfer + 1u);
  }
}

static void test_no_rom_cancel_retry_and_role_change(void)
{
  LinkPair pair;
  uint32_t endpoint;
  uint32_t attempt;
  uint32_t events;

  init_no_rom_pair(&pair);
  for(endpoint = 0; endpoint < 2; endpoint++)
  {
    for(attempt = 0; attempt < 3; attempt++)
    {
      int host = ((endpoint + attempt) & 1u) == 0;

      CHECK(gba_sio_prepare(&pair.sio[endpoint], pair.registers[endpoint]));
      CHECK((host ? link_session_host(&pair.session[endpoint], 1u) :
       link_session_join(&pair.session[endpoint])) == 1);
      CHECK(pair.session[endpoint].state ==
       (host ? LINK_SESSION_HOSTING : LINK_SESSION_JOINING));
      events = link_session_disconnect(&pair.session[endpoint], LINK_REASON_USER);
      CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
      CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) == 0);
      CHECK(pair.session[endpoint].state == LINK_SESSION_DISCONNECTED);
      CHECK(pair.network.disconnect_calls[endpoint] == attempt + 1u);
      CHECK(link_session_disconnect(&pair.session[endpoint], LINK_REASON_USER) == 0);
      CHECK(pair.network.disconnect_calls[endpoint] == attempt + 1u);
    }
  }

  for(attempt = 0; attempt < 2; attempt++)
  {
    CHECK(link_session_host(&pair.session[attempt], 0x12340000u + attempt));
    CHECK(link_session_join(&pair.session[attempt ^ 1u]));
    pump_pair(&pair, 8);
    CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
    CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
    link_session_disconnect(&pair.session[0], LINK_REASON_USER);
    /* The application stops pumping a session once it is disconnected. */
    link_session_pump(&pair.session[1]);
    CHECK(pair.session[0].state == LINK_SESSION_DISCONNECTED);
    CHECK(pair.session[1].state == LINK_SESSION_DISCONNECTED);
    for(endpoint = 0; endpoint < 2; endpoint++)
    {
      gba_sio_reset(&pair.sio[endpoint]);
      CHECK(gba_sio_prepare(&pair.sio[endpoint], pair.registers[endpoint]));
      CHECK(pair.sio[endpoint].mode == GBA_SIO_MODE_GPIO);
    }
  }
}

static void test_start_rejects_unprepared_sio(void)
{
  LinkSession session;
  LinkSession before;
  GbaSio unprepared;
  FakeLinkNetwork network;
  FakeLinkNetwork network_before;
  LinkTransport transport[2];
  uint32_t role;
  uint32_t missing_object;

  CHECK(link_session_host(NULL, 1u) == 0);
  CHECK(link_session_join(NULL) == 0);
  for(role = 0; role < 2; role++)
  {
    for(missing_object = 0; missing_object < 2; missing_object++)
    {
      memset(&unprepared, 0, sizeof(unprepared));
      fake_link_network_init(&network, &transport[0], &transport[1]);
      link_session_init(&session, missing_object ? NULL : &unprepared,
       transport[role], NULL);
      memcpy(&before, &session, sizeof(before));
      memcpy(&network_before, &network, sizeof(network_before));
      CHECK((role == 0 ? link_session_host(&session, 1u) :
       link_session_join(&session)) == 0);
      CHECK(memcmp(&before, &session, sizeof(session)) == 0);
      CHECK(memcmp(&network_before, &network, sizeof(network)) == 0);
      CHECK(unprepared.registers == NULL);
    }
  }
}

int main(void)
{
  test_start_rejects_unprepared_sio();
  test_no_rom_cancel_retry_and_role_change();
  test_handshake_and_different_roms();
  test_delayed_handshake();
  test_negotiation_timeout_is_attributed();
  test_late_attach_delivers_abort_irq_once();
  test_sio_state_message();
  test_hot_readiness_waits_for_shared_clock_base();
  test_duplicate_sio_state_is_not_requeued();
  test_stale_sio_state_after_transfer_is_ignored();
  test_menu_peer_collapses_sio_churn();
  test_multiplayer_transfer();
  test_stream_barrier_stops_at_hardware_finish();
  test_stream_heartbeat_cumulatively_acks_reply();
  test_stream_clock_anchor_is_low_rate();
  test_transactional_multiplayer_requires_commit_release();
  test_next_prepare_waits_for_previous_serial_irq();
  test_duplicate_deferred_prepare_is_idempotent();
  test_abort_of_deferred_prepare_preserves_previous_irq();
  test_normal_transfers();
  test_armed_mode_change_cancels_without_irq();
  test_decided_mode_change_preserves_result();
  test_transactional_joiner_clock_requires_commit_release();
  test_host_schedules_shared_cycle_for_joiner_clock();
  test_barrier_cycle_crossing_fails_closed();
  test_shared_cycle_schedule_wraps_serially();
  test_asymmetric_latency_freezes_sio();
  test_barrier_timeout();
  test_abort_ack_timeout_is_attributed();
  test_postdecision_transport_failure_preserves_result();
  test_late_accept_does_not_close_new_transfer();
  test_late_terminal_packets_do_not_close_new_transfer();
  test_staggered_gameplay_resume();
  test_menu_presence_suspends_heartbeat_timeout();
  test_menu_presence_does_not_hide_transport_close();
  test_stale_peer_mode_aborts_only_transfer();
  test_active_mode_mismatch_times_out_only_transfer();
  test_wrong_session_packet();
  test_malformed_packet();
  test_truncated_packet_matrix();
  test_stale_and_duplicate_packets();
  test_completed_handshake_duplicates_are_idempotent();
  test_unknown_optional_sequence();
  test_incompatible_capabilities();
  test_transport_loss();
  test_invalid_disconnect_reason();
  test_restart_while_waiting();
  test_disconnect_with_pending_start();
  test_intentional_disconnect_with_send_failure();
  test_repeated_disconnect_is_idempotent();
  test_disconnect_after_transport_error_is_idempotent();
  test_heartbeat_stability();
  test_heartbeat_request_is_symmetric();
  test_joiner_heartbeat_request_gets_host_reply();
  test_duplicate_heartbeat_probe_has_one_reply();
  test_heartbeat_has_no_echo_path();
  test_reverse_request_repairs_missed_independent_anchor();
  test_heartbeat_reply_defers_replaceable_request();
  test_only_matching_heartbeat_reply_clears_outstanding();
  test_received_traffic_defers_host_probe();
  test_outbound_traffic_does_not_hide_receive_silence();
  test_transport_activity_keeps_session_alive();
  test_receive_silence_requests_recovery_symmetrically();
  test_receive_silence_recovery_rejection_fails_closed();
  test_receive_silence_recovery_request_has_bounded_wait();
  test_pressured_transport_alone_does_not_pause();
  test_stale_health_cannot_overlap_recovery();
  test_transport_degraded_shifts_deadlines();
  test_prolonged_degradation_requests_recovery_once();
  test_prepared_joiner_timeout_recovers_host_decision();
  test_prepared_joiner_mode_change_requests_recovery();
  test_recovery_replays_lost_prepare();
  test_stream_recovery_reencodes_unseen_start();
  test_stream_recovery_replays_committed_reply_once();
  test_stream_recovery_committed_pair_resumes_next_sequence();
  test_stream_recovery_ignores_old_generation_start();
  test_stream_recovery_missing_reply_cache_fails_precisely();
  test_recovery_cancels_deferred_prepare();
  test_recovery_replays_lost_accept();
  test_recovery_confirms_lost_abort_ack();
  test_recovery_propagates_locally_aborted_transfer();
  test_recovery_republishes_presence();
  test_recovery_scopes_heartbeat_tokens_to_generation();
  test_invariant_audit_rejects_mismatched_record();
  test_recovery_replays_lost_commit();
  test_recovery_confirms_lost_commit_ack();
  test_recovery_commit_ack_across_sequence_wrap();
  test_recovery_replays_joiner_transfer_request();
  test_recovery_replays_joiner_prepare();
  test_resume_handshake_waits_for_transport_error();
  test_nested_transport_recovery_reuses_coordinator();
  test_joiner_recovery_waits_for_transport_error();
  test_duplicate_resume_ack_replays_resume_done();
  test_resume_ack_retry_replays_lost_resume_done();
  test_resume_retry_survives_long_finite_loss();
  test_clock_sync_bounds_hot_peer_lead();
  test_lost_heartbeat_reply_uses_new_token_each_interval();
  test_heartbeat_is_independent_of_sio_sequence();
  test_asymmetric_liveness_under_network_faults();
  test_queue_full_aborts_transfer();
  test_disconnect_reconnect();
  test_sequence_wrap();
  test_ten_consecutive_transfers();
  test_benchmark_uses_stream_without_gba_barrier();
  test_benchmark_terminates_when_session_closes();
  test_benchmark_joiner_detects_silent_host_in_menu();
  test_lost_sio_presence_is_refreshed_without_recovery();
  test_stream_disconnect_preserves_scheduled_result();
  test_stream_next_start_waits_for_guest_serial_handler(0, 0);
  test_stream_next_start_waits_for_guest_serial_handler(0xFFFFFF00u, 0);
  test_stream_next_start_waits_for_guest_serial_handler(0xFFFFFF00u, 1);
  test_transfer_stress();

  if(failures)
  {
    fprintf(stderr, "%d link session test(s) failed\n", failures);
    return 1;
  }

  puts("Link session tests passed");
  return 0;
}
