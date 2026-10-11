#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_link_transport.h"
#include "gba_sio.h"
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
  uint16_t registers[2][GBA_SIO_REGISTER_BYTES / 2u];
  GbaSio sio[2];
  FakeLinkNetwork network;
  LinkTransport transport[2];
  LinkSession session[2];
} JournalPair;

static LinkHello journal_hello(void)
{
  LinkHello hello;

  memset(&hello, 0, sizeof(hello));
  hello.capabilities = LINK_SESSION_SUPPORTED_CAPABILITIES;
  hello.rom_crc32 = 0x12345678u;
  hello.rom_size = 0x01000000u;
  memcpy(hello.title, "JOURNAL TEST", 12u);
  memcpy(hello.game_code, "JRNL", 4u);
  hello.max_players = 2;
  return hello;
}

static void journal_pair_pump(JournalPair *pair, uint32_t rounds)
{
  uint32_t round;

  for(round = 0; round < rounds; round++)
  {
    link_session_pump(&pair->session[0]);
    link_session_pump(&pair->session[1]);
  }
}

static LinkSioState journal_sio_state(const JournalPair *pair,
 uint8_t endpoint)
{
  LinkSioState state;

  memset(&state, 0, sizeof(state));
  state.mode = GBA_SIO_MODE_MULTIPLAYER;
  state.player_id = endpoint;
  state.player_count = 2u;
  state.flags = LINK_SIO_FLAG_GAMEPLAY_ACTIVE;
  state.control = pair->registers[endpoint][GBA_SIO_REG_CONTROL >> 1];
  state.rcnt = pair->registers[endpoint][GBA_SIO_REG_RCNT >> 1];
  return state;
}

static void journal_pair_open(JournalPair *pair)
{
  LinkHello hello = journal_hello();
  LinkSioState state;

  memset(pair, 0, sizeof(*pair));
  gba_sio_init(&pair->sio[0], pair->registers[0]);
  gba_sio_init(&pair->sio[1], pair->registers[1]);
  fake_link_network_init(&pair->network, &pair->transport[0],
   &pair->transport[1]);
  link_session_init(&pair->session[0], &pair->sio[0], pair->transport[0],
   &hello);
  link_session_init(&pair->session[1], &pair->sio[1], pair->transport[1],
   &hello);
  CHECK(link_session_host(&pair->session[0], 0x12345678u));
  CHECK(link_session_join(&pair->session[1]));
  journal_pair_pump(pair, 8u);
  CHECK(pair->session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair->session[1].state == LINK_SESSION_CONNECTED);
  /* This suite proves the retained six-phase journal independently. */
  pair->session[0].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;
  pair->session[1].negotiated_capabilities &=
   ~LINK_CAPABILITY_STREAM_TRANSFER;

  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_RCNT, 0u);
  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL, 0x2000u);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_CONTROL, 0x2000u);
  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_SEND, 0x1234u);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_SEND, 0xabcdu);
  gba_sio_write16(&pair->sio[1], GBA_SIO_REG_CONTROL, 0x6003u);
  gba_sio_write16(&pair->sio[0], GBA_SIO_REG_CONTROL, 0x6083u);
  state = journal_sio_state(pair, 0u);
  CHECK(link_session_publish_sio_state(&pair->session[0], &state) == 0);
  link_session_pump(&pair->session[1]);
  state = journal_sio_state(pair, 1u);
  CHECK(link_session_publish_sio_state(&pair->session[1], &state) == 0);
  link_session_pump(&pair->session[0]);
  CHECK(link_session_publish_clock(&pair->session[0],
   pair->session[0].clock.local_cycle) == LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair->session[1]);
  CHECK(link_session_publish_clock(&pair->session[1],
   pair->session[1].clock.local_cycle) == LINK_SESSION_EVENT_NONE);
  link_session_pump(&pair->session[0]);
}

static void test_release_handshake(void)
{
  JournalPair pair;

  journal_pair_open(&pair);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_ARMED);

  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].state == LINK_SESSION_WAIT_COMMIT);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].state == LINK_SESSION_WAIT_COMMIT_ACK);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_DECIDED);
  CHECK(!pair.session[0].transaction_applied);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].state == LINK_SESSION_WAIT_COMMIT_RELEASE);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_DECIDED);
  CHECK(!pair.session[1].transaction_applied);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].state == LINK_SESSION_WAIT_RELEASE_ACK);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_RELEASED);
  CHECK(pair.session[0].transaction_applied);
  link_session_pump(&pair.session[1]);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[1].journal.checkpoint == 1u);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[0].journal.checkpoint == 1u);
  CHECK(pair.session[0].journal.checkpoint_hash ==
   pair.session[1].journal.checkpoint_hash);
  CHECK(link_session_check_invariants(&pair.session[0]) == 0);
  CHECK(link_session_check_invariants(&pair.session[1]) == 0);
}

static void journal_pair_recover(JournalPair *pair)
{
  CHECK(fake_link_network_recovery(&pair->network, 0u, 1, 0u));
  CHECK(fake_link_network_recovery(&pair->network, 1u, 1, 0u));
  journal_pair_pump(pair, 2u);
  CHECK(pair->session[0].state == LINK_SESSION_RECOVERING);
  CHECK(pair->session[1].state == LINK_SESSION_RECOVERING);
  CHECK(fake_link_network_recovery(&pair->network, 0u, 0, 0u));
  CHECK(fake_link_network_recovery(&pair->network, 1u, 0, 0u));
  journal_pair_pump(pair, 12u);
}

static void test_empty_recovery_advances_one_epoch(void)
{
  JournalPair pair;

  journal_pair_open(&pair);
  journal_pair_recover(&pair);
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[0].journal.epoch == 2u);
  CHECK(pair.session[1].journal.epoch == 2u);
  CHECK(pair.session[0].recovery.state == LINK_RECOVERY_NORMAL);
  CHECK(pair.session[1].recovery.state == LINK_RECOVERY_NORMAL);
}

static void test_recovery_replays_host_decision(void)
{
  JournalPair pair;

  journal_pair_open(&pair);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  link_session_pump(&pair.session[1]);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_DECIDED);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_ARMED);
  memset(&pair.network.queues[0], 0, sizeof(pair.network.queues[0]));
  memset(&pair.network.queues[1], 0, sizeof(pair.network.queues[1]));
  journal_pair_recover(&pair);
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[0].journal.checkpoint == 1u);
  CHECK(pair.session[1].journal.checkpoint == 1u);
  CHECK(pair.session[0].journal.checkpoint_hash ==
   pair.session[1].journal.checkpoint_hash);
  CHECK(pair.session[0].transaction_applied);
  CHECK(pair.session[1].transaction_applied);
}

static void test_recovery_finishes_released_decision(void)
{
  JournalPair pair;

  journal_pair_open(&pair);
  CHECK((link_session_begin_transfer(&pair.session[0]) &
   LINK_SESSION_EVENT_BARRIER_BEGIN) != 0);
  link_session_pump(&pair.session[1]);
  link_session_pump(&pair.session[0]);
  link_session_pump(&pair.session[1]);
  link_session_pump(&pair.session[0]);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_RELEASED);
  CHECK(pair.session[0].transaction_applied);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_DECIDED);
  memset(&pair.network.queues[0], 0, sizeof(pair.network.queues[0]));
  memset(&pair.network.queues[1], 0, sizeof(pair.network.queues[1]));
  journal_pair_recover(&pair);
  CHECK(pair.session[0].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[1].state == LINK_SESSION_CONNECTED);
  CHECK(pair.session[0].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[1].journal.stage == LINK_JOURNAL_EMPTY);
  CHECK(pair.session[0].journal.checkpoint == 1u);
  CHECK(pair.session[1].journal.checkpoint == 1u);
  CHECK(pair.session[0].journal.epoch == 2u);
  CHECK(pair.session[1].journal.epoch == 2u);
}

int main(void)
{
  test_release_handshake();
  test_empty_recovery_advances_one_epoch();
  test_recovery_replays_host_decision();
  test_recovery_finishes_released_decision();
  if(failures != 0)
  {
    fprintf(stderr, "%d link session journal test(s) failed\n", failures);
    return 1;
  }
  puts("link session journal tests passed");
  return 0;
}
