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

typedef struct
{
  uint16_t registers[2][GBA_SIO_REGISTER_BYTES / 2];
  GbaSio sio[2];
  FakeLinkNetwork network;
  LinkTransport transport[2];
  LinkSession session[2];
} LinkPair;

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

static void open_pair(LinkPair *pair, int different_roms)
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

  CHECK(link_session_host(&pair->session[0], 0x12345678) == 1);
  CHECK(link_session_join(&pair->session[1]) == 1);
  pump_pair(pair, 8);
  CHECK(link_session_state(&pair->session[0]) == LINK_SESSION_CONNECTED);
  CHECK(link_session_state(&pair->session[1]) == LINK_SESSION_CONNECTED);
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
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_CONTROL,
   (uint16_t)(control | 0x0080u));
  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL,
   (uint16_t)(control | 0x0080u));
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

static void test_multiplayer_transfer(void)
{
  LinkPair pair;
  uint32_t events;
  uint8_t baud;

  for(baud = 0; baud < 4; baud++)
  {
    open_pair(&pair, 0);
    configure_multiplayer(&pair, 0x1234, 0xABCD, baud);

    events = link_session_begin_transfer(&pair.session[0]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_WAIT_ACCEPT);
    CHECK(gba_sio_is_waiting(&pair.sio[0]));
    CHECK(gba_sio_next_event(&pair.sio[0]) == GBA_SIO_NO_EVENT);

    events = link_session_pump(&pair.session[1]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_WAIT_COMMIT);
    CHECK(gba_sio_is_waiting(&pair.sio[1]));

    events = link_session_pump(&pair.session[0]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_END) != 0);
    CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
    CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_CONNECTED);
    CHECK(gba_sio_next_event(&pair.sio[0]) ==
     gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER, baud, 2));

    events = link_session_pump(&pair.session[1]);
    CHECK((events & LINK_SESSION_EVENT_BARRIER_END) != 0);
    CHECK((events & LINK_SESSION_EVENT_TRANSFER_COMMITTED) != 0);
    CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_CONNECTED);
    CHECK(gba_sio_next_event(&pair.sio[1]) ==
     gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER, baud, 2));

    complete_pair_transfer(&pair);
    CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI0) == 0x1234);
    CHECK(pair_reg(&pair, 0, GBA_SIO_REG_MULTI1) == 0xABCD);
    CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI0) == 0x1234);
    CHECK(pair_reg(&pair, 1, GBA_SIO_REG_MULTI1) == 0xABCD);
  }
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

static void test_asymmetric_latency_freezes_sio(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  fake_link_network_set_latency(&pair.network, 0, 100000);
  fake_link_network_set_latency(&pair.network, 1, 250000);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
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
  CHECK(gba_sio_next_event(&pair.sio[0]) == 5755);
  CHECK(gba_sio_next_event(&pair.sio[1]) == GBA_SIO_NO_EVENT);

  fake_link_network_advance(&pair.network, 100000);
  link_session_pump(&pair.session[1]);
  CHECK(gba_sio_next_event(&pair.sio[1]) == 5755);
}

static void test_barrier_timeout(void)
{
  LinkPair pair;
  uint32_t events;

  open_pair(&pair, 0);
  fake_link_network_set_drop(&pair.network, 1);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  link_session_begin_transfer(&pair.session[0]);
  fake_link_network_advance(&pair.network, LINK_SESSION_TIMEOUT_US);
  events = link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);
  CHECK(pair.session[0].reason == LINK_REASON_TIMEOUT);
  CHECK((events & LINK_SESSION_EVENT_SERIAL_IRQ) != 0);
  CHECK((events & LINK_SESSION_EVENT_TRANSFER_ABORTED) != 0);
  CHECK((events & LINK_SESSION_EVENT_SESSION_CLOSED) != 0);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0040) != 0);
  CHECK((pair_reg(&pair, 0, GBA_SIO_REG_CONTROL) & 0x0080) == 0);
}

static void test_mode_mismatch(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  select_mode(&pair.sio[0], GBA_SIO_MODE_MULTIPLAYER);
  select_mode(&pair.sio[1], GBA_SIO_MODE_NORMAL_8);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_SEND, 1);
  gba_sio_write16(&pair.sio[0], GBA_SIO_REG_CONTROL, 0x6083);
  link_session_begin_transfer(&pair.session[0]);
  link_session_pump(&pair.session[1]);
  CHECK(link_session_state(&pair.session[1]) == LINK_SESSION_ERROR);
  CHECK(pair.session[1].reason == LINK_REASON_MODE_MISMATCH);
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

  open_pair(&pair, 0);
  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 4);
  complete_pair_transfer(&pair);

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, 0);
  CHECK(inject_packet(&pair, 0, &packet, 0));
  link_session_pump(&pair.session[0]);
  CHECK(link_session_state(&pair.session[0]) == LINK_SESSION_ERROR);

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
  second.capabilities &= ~LINK_CAPABILITY_NORMAL_32;
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

static void test_queue_full_aborts_transfer(void)
{
  LinkPair pair;
  LinkPacket packet;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;
  uint32_t i;
  uint32_t events;

  open_pair(&pair, 0);
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   pair.session[0].session_id, pair.session[0].last_sequence);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);
  for(i = 0; i < FAKE_LINK_QUEUE_CAPACITY; i++)
    CHECK(fake_link_network_inject(&pair.network, 1, frame, frame_size, 0));

  configure_multiplayer(&pair, 0x1111, 0x2222, 3);
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
}

static void test_sequence_wrap(void)
{
  LinkPair pair;

  open_pair(&pair, 0);
  pair.session[0].last_sequence = UINT32_MAX;
  pair.session[1].last_sequence = UINT32_MAX;
  configure_multiplayer(&pair, 0x0102, 0x0304, 3);
  link_session_begin_transfer(&pair.session[0]);
  pump_pair(&pair, 4);
  CHECK(pair.session[0].last_sequence == 0);
  CHECK(pair.session[1].last_sequence == 0);
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
    pump_pair(&pair, 4);
    complete_pair_transfer(&pair);
    CHECK(pair.session[0].last_sequence == transfer + 1);
    CHECK(pair.session[1].last_sequence == transfer + 1);
  }
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
    CHECK((events & LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
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

int main(void)
{
  test_handshake_and_different_roms();
  test_sio_state_message();
  test_multiplayer_transfer();
  test_normal_transfers();
  test_asymmetric_latency_freezes_sio();
  test_barrier_timeout();
  test_mode_mismatch();
  test_wrong_session_packet();
  test_malformed_packet();
  test_truncated_packet_matrix();
  test_stale_and_duplicate_packets();
  test_unknown_optional_sequence();
  test_incompatible_capabilities();
  test_transport_loss();
  test_invalid_disconnect_reason();
  test_restart_while_waiting();
  test_disconnect_with_pending_start();
  test_heartbeat_stability();
  test_queue_full_aborts_transfer();
  test_disconnect_reconnect();
  test_sequence_wrap();
  test_ten_consecutive_transfers();
  test_transfer_stress();

  if(failures)
  {
    fprintf(stderr, "%d link session test(s) failed\n", failures);
    return 1;
  }

  puts("Link session tests passed");
  return 0;
}
