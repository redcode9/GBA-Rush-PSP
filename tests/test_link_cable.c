/*
 * End-to-end tests for the emulated link cable.
 *
 * Two simulated consoles each run a GbaSio, a LinkCable and a guest
 * program, connected by a simulated Wi-Fi link with delay, jitter, loss,
 * duplicates, reordering and outages. Each console advances its guest at
 * its own speed and stops whenever the cable says it must wait, like the
 * emulator's main loop.
 *
 * The Pokemon guest is a line-by-line port of the serial driver in the
 * pret decompilations of FireRed and Emerald (src/link.c): handshake,
 * 9-transfer frames with checksum, send and receive queues, lag detection,
 * block transfers and the link room's shared key stream. A test passes
 * only if both games finish their scripts without any link error, receive
 * each other's data intact, process identical key frames and see their
 * own keys come back within a bounded delay in the link room.
 *
 * The lockstep guests check the generic mode word by word.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gba_sio.h"
#include "link_cable.h"

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

#define CYCLES_PER_FRAME 280896u
#define CYCLES_PER_US 16.777216
#define TICK_US 100u
#define SERVICE_CYCLES 16384u

static uint32_t rng_state;

/* The simulated wall clock only moves forward, across runs too. */
static uint64_t sim_clock;

static uint32_t rng(void)
{
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state >> 8;
}

/* ---- Simulated console ------------------------------------------------ */

typedef struct Console Console;

typedef struct
{
  void (*serial_irq)(Console *console);
  void (*timer_irq)(Console *console);
  void (*vblank)(Console *console);
} Guest;

struct Console
{
  int index;
  uint16_t registers[GBA_SIO_REGISTER_BYTES / 2];
  GbaSio sio;
  LinkCable cable;
  const Guest *guest;
  void *state;
  double speed;
  double budget;
  uint32_t cycle;
  uint32_t next_vblank;
  uint32_t frames;
  uint32_t last_service;
  uint8_t service_due;
  uint8_t timer_on;
  uint32_t timer_at;
  uint32_t timer_period;
  uint64_t blocked_us;
  uint64_t now;
  /* The console stops completely for freeze_us once per freeze_period_us,
     like a PSP writing to its memory stick. */
  uint64_t freeze_period_us;
  uint64_t freeze_us;
};

static uint16_t reg_read(const Console *console, uint32_t address)
{
  return console->registers[address >> 1];
}

static void reg_write(Console *console, uint32_t address, uint16_t value)
{
  GbaSioEvent event = gba_sio_write16(&console->sio, address, value);

  /* On the PSP a START write raises a CPU alert that runs the link
     service at once. */
  if((event & GBA_SIO_EVENT_START) != 0)
    console->service_due = 1;
}

static void console_init(Console *console, int index, int host,
 const char *game, const Guest *guest, void *state, double speed)
{
  memset(console, 0, sizeof(*console));
  console->index = index;
  console->guest = guest;
  console->state = state;
  console->speed = speed;
  console->next_vblank = CYCLES_PER_FRAME;
  gba_sio_init(&console->sio, console->registers);
  link_cable_start(&console->cable, &console->sio, host, game,
   0x1000u + (uint32_t)index, sim_clock);
}

static void console_service(Console *console)
{
  GbaSioEvent event = link_cable_service(&console->cable, console->cycle,
   console->now);

  console->last_service = console->cycle;
  console->service_due = 0;
  if((event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
    console->guest->serial_irq(console);
}

static uint32_t clamp_step(uint32_t step, uint32_t limit)
{
  if(limit == 0)
    limit = 1;
  return limit < step ? limit : step;
}

static void console_run(Console *console, uint64_t now)
{
  uint32_t budget;

  console->now = now;
  console->budget += console->speed * TICK_US * CYCLES_PER_US;
  budget = (uint32_t)console->budget;
  console->budget -= budget;

  console_service(console);
  while(budget != 0)
  {
    uint32_t step = budget;

    if(console->service_due ||
       console->cycle - console->last_service >= SERVICE_CYCLES)
      console_service(console);
    if(link_cable_blocked(&console->cable, console->cycle))
    {
      console->blocked_us += TICK_US;
      console->budget = 0;
      return;
    }

    step = clamp_step(step, console->next_vblank - console->cycle);
    if(console->timer_on)
      step = clamp_step(step, console->timer_at - console->cycle);
    if(console->sio.active)
      step = clamp_step(step, console->sio.cycles_remaining);
    step = clamp_step(step,
     link_cable_cycles_until_service(&console->cable, console->cycle));
    step = clamp_step(step,
     SERVICE_CYCLES - (console->cycle - console->last_service));

    console->cycle += step;
    budget -= step;

    if(console->sio.active &&
       (gba_sio_advance(&console->sio, step) & GBA_SIO_EVENT_SERIAL_IRQ))
      console->guest->serial_irq(console);
    if(console->timer_on && console->timer_at == console->cycle)
    {
      console->timer_at += console->timer_period;
      console->guest->timer_irq(console);
    }
    if(console->cycle == console->next_vblank)
    {
      console->next_vblank += CYCLES_PER_FRAME;
      console->frames++;
      console->guest->vblank(console);
    }
  }
}

static void timer_start(Console *console, uint32_t period)
{
  console->timer_on = 1;
  console->timer_period = period;
  console->timer_at = console->cycle + period;
}

static void timer_stop(Console *console)
{
  console->timer_on = 0;
}

/* ---- Simulated network ------------------------------------------------ */

typedef struct
{
  const char *name;
  uint32_t latency_us;
  uint32_t jitter_us;
  uint32_t loss_percent;
  uint32_t duplicate_percent;
  uint64_t outage_start_us;
  uint64_t outage_end_us;
} NetProfile;

typedef struct
{
  uint8_t data[LINK_NET_MAX_DATAGRAM];
  size_t size;
  uint64_t arrival;
  int to;
} Flight;

#define MAX_FLIGHTS 4096u
static Flight flights[MAX_FLIGHTS];
static unsigned int flight_count;
static uint64_t run_start;

static void network_send(const NetProfile *net, int to, const uint8_t *data,
 size_t size, uint64_t now)
{
  unsigned int copies = 1;

  if(now - run_start >= net->outage_start_us &&
     now - run_start < net->outage_end_us)
    return;
  if(rng() % 100u < net->loss_percent)
    return;
  if(rng() % 100u < net->duplicate_percent)
    copies = 2;
  while(copies-- != 0 && flight_count < MAX_FLIGHTS)
  {
    Flight *flight = &flights[flight_count++];

    memcpy(flight->data, data, size);
    flight->size = size;
    flight->to = to;
    flight->arrival = now + net->latency_us +
     (net->jitter_us != 0 ? rng() % net->jitter_us : 0u);
  }
}

static void network_deliver(Console *consoles[2], uint64_t now)
{
  unsigned int i = 0;

  while(i < flight_count)
  {
    if(flights[i].arrival <= now)
    {
      link_cable_receive(&consoles[flights[i].to]->cable, flights[i].data,
       flights[i].size, now);
      flights[i] = flights[--flight_count];
    }
    else
      i++;
  }
}

typedef int (*DoneFunction)(Console *consoles[2]);

static int never_done(Console *consoles[2])
{
  (void)consoles;
  return 0;
}

/* Runs both consoles until done() or for limit_us. Returns the time used. */
static uint64_t run_link(Console *consoles[2], const NetProfile *net,
 uint64_t limit_us, DoneFunction done)
{
  uint64_t start = sim_clock;
  uint64_t now = sim_clock;
  int side;

  flight_count = 0;
  run_start = start;
  while(now - start < limit_us && !done(consoles))
  {
    now += TICK_US;
    network_deliver(consoles, now);
    for(side = 0; side < 2; side++)
    {
      uint8_t datagram[LINK_NET_MAX_DATAGRAM];
      size_t size;

      if(consoles[side]->freeze_period_us != 0 &&
         (now - start) % consoles[side]->freeze_period_us <
          consoles[side]->freeze_us)
        continue;
      console_run(consoles[side], now);
      size = link_cable_build(&consoles[side]->cable, datagram,
       sizeof(datagram), consoles[side]->cycle, now);
      if(size != 0)
        network_send(net, 1 - side, datagram, size, now);
    }
  }
  sim_clock = now;
  return now - start;
}

/* ---- Pokemon guest: port of pret's src/link.c ------------------------- */

#define CMD_LENGTH 8
#define QUEUE_CAPACITY 50
#define MASTER_HANDSHAKE 0x8FFF
#define SLAVE_HANDSHAKE 0xB9A0

#define LINK_STAT_PLAYER_COUNT 0x0000001Cu
#define LINK_STAT_MASTER 0x00000020u
#define LINK_STAT_CONN_ESTABLISHED 0x00000040u
#define LINK_STAT_ERRORS 0x0007F000u
#define LINK_STAT_ERROR_LAG_MASTER 0x00010000u
#define LINK_STAT_ERROR_INVALID_ID 0x00020000u
#define LINK_STAT_ERROR_LAG_SLAVE 0x00040000u

#define LINKCMD_SEND_LINK_TYPE 0x2222
#define LINKCMD_READY_CLOSE_LINK 0x5FFF
#define LINKCMD_CONT_BLOCK 0x8888
#define LINKCMD_INIT_BLOCK 0xBBBB
#define LINKCMD_SEND_HELD_KEYS 0xCAFE

#define REG_SIOCNT 0x128
#define REG_SIOMLT_SEND 0x12A
#define REG_RCNT 0x134
#define SIO_MULTI_MODE 0x2000
#define SIO_115200_BPS 0x0003
#define SIO_INTR_ENABLE 0x4000
#define SIO_START 0x0080
#define SIO_MULTI_SI 0x0004
#define SIO_MULTI_SD 0x0008

enum
{
  LINK_STATE_START0,
  LINK_STATE_START1,
  LINK_STATE_HANDSHAKE,
  LINK_STATE_INIT_TIMER,
  LINK_STATE_CONN_ESTABLISHED
};

enum { LAG_NONE, LAG_MASTER, LAG_SLAVE };

#define TIMER3_PERIOD (197u * 64u)

#define LINK_PLAYER_BLOCK_SIZE 60u
#define PARTY_BLOCK_SIZE 600u
#define KEY_FRAMES 240u
#define MAX_TUPLES 4096u
#define LINK_KEY_CODE_EMPTY 0x11
/* Each player's own keys must come back to it within this many frames on
   average in the link room, however the network or the consoles stall. */
#define ROOM_DELAY_LIMIT 20u

typedef enum
{
  SCRIPT_IDLE,
  SCRIPT_LINKING,
  SCRIPT_EXCHANGE,
  SCRIPT_ROOM,
  SCRIPT_TRADE,
  SCRIPT_CLOSING,
  SCRIPT_DONE
} ScriptPhase;

typedef struct
{
  int active;
  uint16_t size;
  uint16_t pos;
  uint8_t delay;
  uint8_t data[PARTY_BLOCK_SIZE + 14];
} BlockSend;

typedef struct
{
  uint16_t size;
  uint16_t pos;
  uint8_t data[PARTY_BLOCK_SIZE + 14];
} BlockReceive;

typedef enum
{
  CALLBACK_NONE,
  CALLBACK_REQUEST_EXCHANGE,
  CALLBACK_BLOCK_BEGIN,
  CALLBACK_BLOCK_SEND,
  CALLBACK_HELD_KEYS,
  CALLBACK_WAIT_CLOSE
} LinkCallback;

typedef struct
{
  /* gLink */
  uint8_t isMaster;
  uint8_t state;
  uint8_t localId;
  uint8_t playerCount;
  uint8_t handshakeAsMaster;
  uint8_t receivedNothing;
  uint8_t serialIntrCounter;
  uint8_t lag;
  uint8_t hardwareError;
  uint8_t badChecksum;
  uint8_t queueFull;
  uint8_t sendCmdIndex;
  uint8_t recvCmdIndex;
  uint16_t checksum;
  uint16_t tempRecvBuffer[4];
  uint16_t sendData[CMD_LENGTH][QUEUE_CAPACITY];
  uint8_t sendPos;
  uint8_t sendCount;
  uint16_t recvData[4][CMD_LENGTH][QUEUE_CAPACITY];
  uint8_t recvPos;
  uint8_t recvCount;

  /* File-scope statics in link.c */
  uint8_t numVBlanksWithoutSerialIntr;
  uint8_t checksumAvailable;
  uint8_t handshakePlayerCount;
  uint8_t sendBufferEmpty;
  uint16_t sendNonzeroCheck;
  uint16_t recvNonzeroCheck;

  /* Game state around the driver */
  uint8_t linkOpen;
  uint8_t vsyncDisabled;
  uint8_t shouldAdvanceLinkState;
  uint8_t triggerCountdown;
  uint16_t sendCmd[CMD_LENGTH];
  uint16_t recvCmds[4][CMD_LENGTH];
  uint32_t linkStatus;
  uint32_t errors;
  LinkCallback callback;
  BlockSend blockSend;
  BlockReceive blockRecv[4];
  uint8_t blockReceived;
  uint8_t readyToClose[2];
  uint16_t heldKeys[4];

  /* Test script */
  ScriptPhase phase;
  uint32_t openFrame;
  uint32_t aPressFrame;
  uint32_t phaseFrame;
  uint32_t keysSent;
  uint32_t peerKeysSeen;
  uint16_t heldKeyCode;
  uint32_t keySentFrame[KEY_FRAMES];
  uint32_t delaySum;
  uint32_t delayCount;
  uint32_t delayMax;
  uint32_t sessions;
  uint32_t sessionsWanted;
  uint32_t seed;
  uint32_t peerSeed;
  uint32_t blocksVerified;
  uint32_t tupleCount;
  uint16_t tuples[MAX_TUPLES][2];
  uint32_t closeFrame;
} PokemonGame;

static PokemonGame *game_of(Console *console)
{
  return (PokemonGame *)console->state;
}

static void DisableSerial(Console *console)
{
  PokemonGame *game = game_of(console);
  uint8_t keep_open = game->linkOpen;
  uint8_t keep_vsync = game->vsyncDisabled;

  timer_stop(console);
  reg_write(console, REG_SIOCNT, SIO_MULTI_MODE);
  reg_write(console, REG_SIOMLT_SEND, 0);
  reg_write(console, 0x120, 0);
  reg_write(console, 0x122, 0);
  reg_write(console, 0x124, 0);
  reg_write(console, 0x126, 0);
  memset(game, 0, offsetof(PokemonGame, numVBlanksWithoutSerialIntr));
  game->linkOpen = keep_open;
  game->vsyncDisabled = keep_vsync;
}

static void EnableSerial(Console *console)
{
  PokemonGame *game = game_of(console);

  reg_write(console, REG_RCNT, 0);
  reg_write(console, REG_SIOCNT, SIO_MULTI_MODE);
  reg_write(console, REG_SIOCNT, (uint16_t)(reg_read(console, REG_SIOCNT) |
   SIO_115200_BPS | SIO_INTR_ENABLE));
  reg_write(console, REG_SIOMLT_SEND, 0);
  memset(game, 0, offsetof(PokemonGame, numVBlanksWithoutSerialIntr));
  game->numVBlanksWithoutSerialIntr = 0;
  game->sendNonzeroCheck = 0;
  game->recvNonzeroCheck = 0;
  game->checksumAvailable = 0;
  game->handshakePlayerCount = 0;
}

static void ResetSerial(Console *console)
{
  EnableSerial(console);
  DisableSerial(console);
}

static void StartTransfer(Console *console)
{
  reg_write(console, REG_SIOCNT,
   (uint16_t)(reg_read(console, REG_SIOCNT) | SIO_START));
}

static void CheckMasterOrSlave(Console *console)
{
  PokemonGame *game = game_of(console);
  uint16_t terminals = reg_read(console, REG_SIOCNT) &
   (SIO_MULTI_SD | SIO_MULTI_SI);

  if(terminals == SIO_MULTI_SD && game->localId == 0)
    game->isMaster = 1;
  else
    game->isMaster = 0;
}

static void InitTimer(Console *console)
{
  if(game_of(console)->isMaster)
    timer_start(console, TIMER3_PERIOD);
}

static void EnqueueSendCmd(Console *console, uint16_t *sendCmd)
{
  PokemonGame *game = game_of(console);
  uint8_t i;
  uint8_t offset;

  if(game->sendCount < QUEUE_CAPACITY)
  {
    offset = (uint8_t)(game->sendPos + game->sendCount);
    if(offset >= QUEUE_CAPACITY)
      offset -= QUEUE_CAPACITY;
    for(i = 0; i < CMD_LENGTH; i++)
    {
      game->sendNonzeroCheck |= sendCmd[i];
      game->sendData[i][offset] = sendCmd[i];
      sendCmd[i] = 0;
    }
  }
  else
  {
    game->queueFull = 1;
  }
  if(game->sendNonzeroCheck)
  {
    game->sendCount++;
    game->sendNonzeroCheck = 0;
  }
}

static void DequeueRecvCmds(Console *console, uint16_t recvCmds[4][CMD_LENGTH])
{
  PokemonGame *game = game_of(console);
  uint8_t i;
  uint8_t j;

  if(game->recvCount == 0)
  {
    for(i = 0; i < game->playerCount; i++)
      for(j = 0; j < CMD_LENGTH; j++)
        recvCmds[i][j] = 0;
    game->receivedNothing = 1;
  }
  else
  {
    for(i = 0; i < game->playerCount; i++)
      for(j = 0; j < CMD_LENGTH; j++)
        recvCmds[i][j] = game->recvData[i][j][game->recvPos];
    game->recvCount--;
    game->recvPos++;
    if(game->recvPos >= QUEUE_CAPACITY)
      game->recvPos = 0;
    game->receivedNothing = 0;
  }
}

static uint32_t LinkMain1(Console *console)
{
  PokemonGame *game = game_of(console);
  uint32_t status;

  switch(game->state)
  {
    case LINK_STATE_START0:
      DisableSerial(console);
      game->state = LINK_STATE_START1;
      break;
    case LINK_STATE_START1:
      if(game->shouldAdvanceLinkState == 1)
      {
        EnableSerial(console);
        game->state = LINK_STATE_HANDSHAKE;
      }
      break;
    case LINK_STATE_HANDSHAKE:
      switch(game->shouldAdvanceLinkState)
      {
        default:
          CheckMasterOrSlave(console);
          break;
        case 1:
          if(game->isMaster && game->playerCount > 1)
            game->handshakeAsMaster = 1;
          break;
        case 2:
          game->state = LINK_STATE_START0;
          reg_write(console, REG_SIOMLT_SEND, 0);
          break;
      }
      break;
    case LINK_STATE_INIT_TIMER:
      InitTimer(console);
      game->state = LINK_STATE_CONN_ESTABLISHED;
      /* fallthrough */
    case LINK_STATE_CONN_ESTABLISHED:
      EnqueueSendCmd(console, game->sendCmd);
      DequeueRecvCmds(console, game->recvCmds);
      break;
  }
  game->shouldAdvanceLinkState = 0;

  status = game->localId | ((uint32_t)game->playerCount << 2);
  if(game->isMaster)
    status |= LINK_STAT_MASTER;
  if(game->state == LINK_STATE_CONN_ESTABLISHED)
    status |= LINK_STAT_CONN_ESTABLISHED;
  status |= (uint32_t)game->receivedNothing << 8;
  status |= (uint32_t)game->hardwareError << 12;
  status |= (uint32_t)game->badChecksum << 13;
  status |= (uint32_t)(game->queueFull != 0) << 14;
  if(game->lag == LAG_MASTER)
    status |= LINK_STAT_ERROR_LAG_MASTER;
  if(game->localId >= 4)
    status |= LINK_STAT_ERROR_INVALID_ID;
  if(game->lag == LAG_SLAVE)
    status |= LINK_STAT_ERROR_LAG_SLAVE;
  return status;
}

static void LinkVSync(Console *console)
{
  PokemonGame *game = game_of(console);

  if(game->isMaster)
  {
    switch(game->state)
    {
      case LINK_STATE_CONN_ESTABLISHED:
        if(game->serialIntrCounter < 9)
        {
          if(game->hardwareError != 1)
            game->lag = LAG_MASTER;
          else
            StartTransfer(console);
        }
        else if(game->lag != LAG_MASTER)
        {
          game->serialIntrCounter = 0;
          StartTransfer(console);
        }
        break;
      case LINK_STATE_HANDSHAKE:
        StartTransfer(console);
        break;
    }
  }
  else if(game->state == LINK_STATE_CONN_ESTABLISHED ||
          game->state == LINK_STATE_HANDSHAKE)
  {
    if(++game->numVBlanksWithoutSerialIntr > 10)
    {
      if(game->state == LINK_STATE_CONN_ESTABLISHED)
        game->lag = LAG_SLAVE;
      if(game->state == LINK_STATE_HANDSHAKE)
        game->playerCount = 0;
    }
  }
}

static int DoHandshake(Console *console)
{
  PokemonGame *game = game_of(console);
  uint8_t i;
  uint8_t playerCount = 0;

  if(game->handshakeAsMaster)
    reg_write(console, REG_SIOMLT_SEND, MASTER_HANDSHAKE);
  else
    reg_write(console, REG_SIOMLT_SEND, SLAVE_HANDSHAKE);
  for(i = 0; i < 4; i++)
    game->tempRecvBuffer[i] = reg_read(console, 0x120 + i * 2);
  for(i = 0; i < 4; i++)
    reg_write(console, 0x120 + i * 2, 0);
  game->handshakeAsMaster = 0;
  for(i = 0; i < 4; i++)
  {
    if((game->tempRecvBuffer[i] & ~0x3) == SLAVE_HANDSHAKE ||
       game->tempRecvBuffer[i] == MASTER_HANDSHAKE)
    {
      playerCount++;
    }
    else
    {
      if(game->tempRecvBuffer[i] != 0xFFFF)
        playerCount = 0;
      break;
    }
  }
  game->playerCount = playerCount;
  if(game->playerCount > 1 &&
     game->playerCount == game->handshakePlayerCount &&
     game->tempRecvBuffer[0] == MASTER_HANDSHAKE)
    return 1;
  game->handshakePlayerCount = game->playerCount;
  return 0;
}

static void DoRecv(Console *console)
{
  PokemonGame *game = game_of(console);
  uint16_t recv[4];
  uint8_t i;
  uint8_t index;

  for(i = 0; i < 4; i++)
    recv[i] = reg_read(console, 0x120 + i * 2);
  if(game->sendCmdIndex == 0)
  {
    for(i = 0; i < game->playerCount; i++)
    {
      if(game->checksum != recv[i] && game->checksumAvailable)
        game->badChecksum = 1;
    }
    game->checksum = 0;
    game->checksumAvailable = 1;
  }
  else
  {
    index = (uint8_t)(game->recvPos + game->recvCount);
    if(index >= QUEUE_CAPACITY)
      index -= QUEUE_CAPACITY;
    if(game->recvCount < QUEUE_CAPACITY)
    {
      for(i = 0; i < game->playerCount; i++)
      {
        game->checksum += recv[i];
        game->recvNonzeroCheck |= recv[i];
        game->recvData[i][game->recvCmdIndex][index] = recv[i];
      }
    }
    else
    {
      game->queueFull = 2;
    }
    game->recvCmdIndex++;
    if(game->recvCmdIndex == CMD_LENGTH && game->recvNonzeroCheck)
    {
      game->recvCount++;
      game->recvNonzeroCheck = 0;
    }
  }
}

static void DoSend(Console *console)
{
  PokemonGame *game = game_of(console);

  if(game->sendCmdIndex == CMD_LENGTH)
  {
    reg_write(console, REG_SIOMLT_SEND, game->checksum);
    if(!game->sendBufferEmpty)
    {
      game->sendCount--;
      game->sendPos++;
      if(game->sendPos >= QUEUE_CAPACITY)
        game->sendPos = 0;
    }
    else
    {
      game->sendBufferEmpty = 0;
    }
  }
  else
  {
    if(!game->sendBufferEmpty && game->sendCount == 0)
      game->sendBufferEmpty = 1;
    if(game->sendBufferEmpty)
      reg_write(console, REG_SIOMLT_SEND, 0);
    else
      reg_write(console, REG_SIOMLT_SEND,
       game->sendData[game->sendCmdIndex][game->sendPos]);
    game->sendCmdIndex++;
  }
}

static void StopTimer(Console *console)
{
  if(game_of(console)->isMaster)
    timer_stop(console);
}

static void SendRecvDone(Console *console)
{
  PokemonGame *game = game_of(console);

  if(game->recvCmdIndex == CMD_LENGTH)
  {
    game->sendCmdIndex = 0;
    game->recvCmdIndex = 0;
  }
  else if(game->isMaster)
  {
    timer_start(console, TIMER3_PERIOD);
  }
}

static void SerialCB(Console *console)
{
  PokemonGame *game = game_of(console);

  game->localId = (uint8_t)((reg_read(console, REG_SIOCNT) >> 4) & 3);
  switch(game->state)
  {
    case LINK_STATE_CONN_ESTABLISHED:
      game->hardwareError = (reg_read(console, REG_SIOCNT) >> 6) & 1;
      DoRecv(console);
      DoSend(console);
      SendRecvDone(console);
      break;
    case LINK_STATE_HANDSHAKE:
      if(DoHandshake(console))
      {
        if(game->isMaster)
        {
          game->state = LINK_STATE_INIT_TIMER;
          game->serialIntrCounter = 8;
        }
        else
        {
          game->state = LINK_STATE_CONN_ESTABLISHED;
        }
      }
      break;
  }
  game->serialIntrCounter++;
  game->numVBlanksWithoutSerialIntr = 0;
}

static void Timer3Intr(Console *console)
{
  StopTimer(console);
  StartTransfer(console);
}

/* ---- Pokemon guest: game logic above the driver ----------------------- */

static uint8_t block_byte(uint32_t seed, uint32_t kind, uint32_t index)
{
  return (uint8_t)((seed * 131u + kind * 17u + index * 7u + (index >> 3)) ^
   (seed >> 3));
}

static void InitBlockSend(PokemonGame *game, uint32_t kind, uint16_t size)
{
  uint16_t i;

  if(game->blockSend.active)
    return;
  game->blockSend.active = 1;
  game->blockSend.size = size;
  game->blockSend.pos = 0;
  game->blockSend.delay = 0;
  memset(game->blockSend.data, 0, sizeof(game->blockSend.data));
  for(i = 0; i < size; i++)
    game->blockSend.data[i] = block_byte(game->seed, kind, i);
  game->sendCmd[0] = LINKCMD_INIT_BLOCK;
  game->sendCmd[1] = size;
  game->sendCmd[2] = (uint16_t)(game->localId + 0x80);
  game->callback = CALLBACK_BLOCK_BEGIN;
}

static void RunLinkCallback(PokemonGame *game)
{
  int i;

  switch(game->callback)
  {
    case CALLBACK_REQUEST_EXCHANGE:
      if(game->linkStatus & LINK_STAT_MASTER)
        game->sendCmd[0] = LINKCMD_SEND_LINK_TYPE;
      game->callback = CALLBACK_NONE;
      break;

    case CALLBACK_BLOCK_BEGIN:
      if(++game->blockSend.delay > 2)
        game->callback = CALLBACK_BLOCK_SEND;
      break;

    case CALLBACK_BLOCK_SEND:
      game->sendCmd[0] = LINKCMD_CONT_BLOCK;
      for(i = 0; i < CMD_LENGTH - 1; i++)
        game->sendCmd[i + 1] = (uint16_t)(
         (game->blockSend.data[game->blockSend.pos + i * 2 + 1] << 8) |
         game->blockSend.data[game->blockSend.pos + i * 2]);
      game->blockSend.pos += 14;
      if(game->blockSend.size <= game->blockSend.pos)
      {
        game->blockSend.active = 0;
        game->callback = CALLBACK_NONE;
      }
      break;

    case CALLBACK_HELD_KEYS:
      /* LinkCB_SendHeldKeys runs on every frame, including the frames
         whose game logic is skipped. */
      game->sendCmd[0] = LINKCMD_SEND_HELD_KEYS;
      game->sendCmd[1] = game->heldKeyCode;
      break;

    case CALLBACK_WAIT_CLOSE:
    case CALLBACK_NONE:
      break;
  }
}

static void ProcessRecvCmds(PokemonGame *game)
{
  int i;
  int j;

  for(i = 0; i < 4; i++)
  {
    game->heldKeys[i] = 0;
    switch(game->recvCmds[i][0])
    {
      case LINKCMD_SEND_LINK_TYPE:
        InitBlockSend(game, 1, LINK_PLAYER_BLOCK_SIZE);
        break;
      case LINKCMD_SEND_HELD_KEYS:
        game->heldKeys[i] = game->recvCmds[i][1];
        break;
      case LINKCMD_INIT_BLOCK:
        game->blockRecv[i].pos = 0;
        game->blockRecv[i].size = game->recvCmds[i][1];
        break;
      case LINKCMD_CONT_BLOCK:
        for(j = 0; j < CMD_LENGTH - 1; j++)
        {
          uint16_t pos = (uint16_t)(game->blockRecv[i].pos + j * 2);

          if(pos + 1u < sizeof(game->blockRecv[i].data))
          {
            game->blockRecv[i].data[pos] = (uint8_t)game->recvCmds[i][j + 1];
            game->blockRecv[i].data[pos + 1] =
             (uint8_t)(game->recvCmds[i][j + 1] >> 8);
          }
        }
        game->blockRecv[i].pos += 14;
        if(game->blockRecv[i].pos >= game->blockRecv[i].size)
          game->blockReceived |= (uint8_t)(1u << i);
        break;
      case LINKCMD_READY_CLOSE_LINK:
        if(i < 2)
          game->readyToClose[i] = 1;
        break;
    }
  }
}

static int VerifyBlock(PokemonGame *game, int player, uint32_t seed,
 uint32_t kind, uint16_t size)
{
  uint16_t i;

  for(i = 0; i < size; i++)
  {
    if(game->blockRecv[player].data[i] != block_byte(seed, kind, i))
      return 0;
  }
  return 1;
}

static void OpenLink(Console *console)
{
  PokemonGame *game = game_of(console);

  ResetSerial(console);
  memset(game->sendCmd, 0, sizeof(game->sendCmd));
  game->linkOpen = 1;
  EnableSerial(console);
  game->callback = CALLBACK_REQUEST_EXCHANGE;
  game->vsyncDisabled = 0;
  game->blockReceived = 0;
  game->blockSend.active = 0;
  game->readyToClose[0] = game->readyToClose[1] = 0;
  game->triggerCountdown = 5;
  game->aPressFrame = 0;
  /* The partner may enter the room first, so count its keys from here. */
  game->keysSent = 0;
  game->peerKeysSeen = 0;
  game->heldKeyCode = LINK_KEY_CODE_EMPTY;
}

/* CB1_UpdateLinkState: the room's game logic for one frame. Each new key
   code is unique, so the frame it comes back in this player's own slot
   measures the delay the player sees in the room. */
static void RoomFrame(Console *console, PokemonGame *game, int self)
{
  uint16_t own = game->heldKeys[self];

  if(own >= 0x100u && own < 0x100u + KEY_FRAMES &&
     game->keySentFrame[own - 0x100u] != 0)
  {
    uint32_t delay = console->frames - game->keySentFrame[own - 0x100u];

    game->keySentFrame[own - 0x100u] = 0;
    game->delaySum += delay;
    game->delayCount++;
    if(delay > game->delayMax)
      game->delayMax = delay;
  }

  if(game->keysSent < KEY_FRAMES)
  {
    game->heldKeyCode = (uint16_t)(0x100u + game->keysSent);
    game->keySentFrame[game->keysSent] = console->frames;
    game->keysSent++;
  }
  else
    game->heldKeyCode = LINK_KEY_CODE_EMPTY;
}

static void CloseLink(Console *console)
{
  PokemonGame *game = game_of(console);

  game->linkOpen = 0;
  game->vsyncDisabled = 1;
  DisableSerial(console);
}

/* One pass of the game's main loop, run after each VBlank. */
static void PokemonFrame(Console *console)
{
  PokemonGame *game = game_of(console);
  int self = console->cable.host ? 0 : 1;
  int peer = 1 - self;
  uint32_t player_count;
  int keys_done;

  if(game->phase == SCRIPT_IDLE)
  {
    if(console->frames >= game->openFrame)
    {
      OpenLink(console);
      game->phase = SCRIPT_LINKING;
    }
    return;
  }
  if(game->phase == SCRIPT_DONE)
  {
    if(game->sessions < game->sessionsWanted &&
       console->frames >= game->closeFrame + 90u)
    {
      OpenLink(console);
      game->phase = SCRIPT_LINKING;
    }
    return;
  }
  if(!game->linkOpen)
    return;

  /* Task_TriggerHandshake */
  if(game->triggerCountdown != 0 && --game->triggerCountdown == 0)
    game->shouldAdvanceLinkState = 1;

  /* The host player presses A once the attendant reports both players. */
  player_count = (game->linkStatus & LINK_STAT_PLAYER_COUNT) >> 2;
  if(game->phase == SCRIPT_LINKING && game->isMaster && player_count > 1 &&
     !(game->linkStatus & LINK_STAT_CONN_ESTABLISHED))
  {
    if(game->aPressFrame == 0)
      game->aPressFrame = console->frames + 20u;
    else if(console->frames >= game->aPressFrame)
    {
      game->shouldAdvanceLinkState = 1;
      game->aPressFrame = console->frames + 60u;
    }
  }

  game->linkStatus = LinkMain1(console);
  game->errors |= game->linkStatus & LINK_STAT_ERRORS;

  /* LinkMain2 */
  memset(game->sendCmd, 0, sizeof(game->sendCmd));
  if(!(game->linkStatus & LINK_STAT_CONN_ESTABLISHED))
    return;
  keys_done = game->keysSent >= KEY_FRAMES;
  ProcessRecvCmds(game);
  RunLinkCallback(game);
  /* HandleLinkConnection: in the room a frame that received nothing skips
     the game logic, but its held keys have already gone out. */
  if(game->callback == CALLBACK_HELD_KEYS && game->receivedNothing)
    return;
  if(game->phase == SCRIPT_ROOM)
    RoomFrame(console, game, self);

  if(game->heldKeys[0] != 0 || game->heldKeys[1] != 0)
  {
    if(game->tupleCount < MAX_TUPLES)
    {
      game->tuples[game->tupleCount][0] = game->heldKeys[0];
      game->tuples[game->tupleCount][1] = game->heldKeys[1];
    }
    game->tupleCount++;
  }
  if(game->heldKeys[peer] != 0)
    game->peerKeysSeen++;

  switch(game->phase)
  {
    case SCRIPT_LINKING:
      game->phase = SCRIPT_EXCHANGE;
      break;

    case SCRIPT_EXCHANGE:
      if(game->blockReceived == 3)
      {
        if(VerifyBlock(game, peer, game->peerSeed, 1,
           LINK_PLAYER_BLOCK_SIZE) &&
           VerifyBlock(game, self, game->seed, 1, LINK_PLAYER_BLOCK_SIZE))
          game->blocksVerified++;
        game->blockReceived = 0;
        game->callback = CALLBACK_HELD_KEYS;
        game->phase = SCRIPT_ROOM;
      }
      break;

    case SCRIPT_ROOM:
      if(keys_done && game->peerKeysSeen >= KEY_FRAMES &&
         !game->blockSend.active)
      {
        game->callback = CALLBACK_NONE;
        InitBlockSend(game, 2, PARTY_BLOCK_SIZE);
        game->phase = SCRIPT_TRADE;
      }
      break;

    case SCRIPT_TRADE:
      if(game->blockReceived == 3 && !game->blockSend.active)
      {
        if(VerifyBlock(game, peer, game->peerSeed, 2, PARTY_BLOCK_SIZE) &&
           VerifyBlock(game, self, game->seed, 2, PARTY_BLOCK_SIZE))
          game->blocksVerified++;
        game->sendCmd[0] = LINKCMD_READY_CLOSE_LINK;
        game->callback = CALLBACK_WAIT_CLOSE;
        game->phase = SCRIPT_CLOSING;
      }
      break;

    case SCRIPT_CLOSING:
      if(game->readyToClose[0] && game->readyToClose[1])
      {
        CloseLink(console);
        game->sessions++;
        game->closeFrame = console->frames;
        game->phase = SCRIPT_DONE;
      }
      break;

    default:
      break;
  }
}

static void pokemon_vblank(Console *console)
{
  PokemonGame *game = game_of(console);

  if(game->linkOpen && !game->vsyncDisabled)
    LinkVSync(console);
  PokemonFrame(console);
}

static const Guest pokemon_guest =
{
  SerialCB,
  Timer3Intr,
  pokemon_vblank
};

static int pokemon_done(Console *consoles[2])
{
  PokemonGame *a = game_of(consoles[0]);
  PokemonGame *b = game_of(consoles[1]);

  return (a->phase == SCRIPT_DONE && b->phase == SCRIPT_DONE &&
   a->sessions >= a->sessionsWanted && b->sessions >= b->sessionsWanted) ||
   a->errors != 0 || b->errors != 0;
}

typedef struct
{
  const char *name;
  const char *host_game;
  const char *joiner_game;
  double host_speed;
  double joiner_speed;
  uint32_t host_open_frame;
  uint32_t joiner_open_frame;
  uint32_t sessions;
  int freeze_side;            /* -1 for none */
  uint32_t freeze_period_ms;
  uint32_t freeze_ms;
} PokemonCase;

static PokemonGame pokemon_games[2];

static void run_pokemon_case(const PokemonCase *test, const NetProfile *net)
{
  Console host;
  Console joiner;
  Console *consoles[2] = { &host, &joiner };
  PokemonGame *a = &pokemon_games[0];
  PokemonGame *b = &pokemon_games[1];
  uint64_t elapsed;
  uint32_t i;
  int tuples_match;

  memset(pokemon_games, 0, sizeof(pokemon_games));
  rng_state = 0xC0FFEEu ^ (uint32_t)strlen(test->name) ^
   (net->latency_us << 4) ^ net->loss_percent;
  a->seed = 11;
  b->seed = 29;
  a->peerSeed = b->seed;
  b->peerSeed = a->seed;
  a->openFrame = test->host_open_frame;
  b->openFrame = test->joiner_open_frame;
  a->sessionsWanted = test->sessions;
  b->sessionsWanted = test->sessions;

  console_init(&host, 0, 1, test->host_game, &pokemon_guest, a,
   test->host_speed);
  console_init(&joiner, 1, 0, test->joiner_game, &pokemon_guest, b,
   test->joiner_speed);
  if(test->freeze_side >= 0)
  {
    consoles[test->freeze_side]->freeze_period_us =
     (uint64_t)test->freeze_period_ms * 1000u;
    consoles[test->freeze_side]->freeze_us = (uint64_t)test->freeze_ms * 1000u;
  }

  elapsed = run_link(consoles, net, 120000000u, pokemon_done);

  CHECK(host.cable.mode == LINK_CABLE_MODE_POKEMON);
  CHECK(joiner.cable.mode == LINK_CABLE_MODE_POKEMON);
  CHECK(a->errors == 0);
  CHECK(b->errors == 0);
  CHECK(a->sessions == test->sessions);
  CHECK(b->sessions == test->sessions);
  CHECK(a->blocksVerified == 2u * test->sessions);
  CHECK(b->blocksVerified == 2u * test->sessions);

  /* Both games must have processed the same key frames in the same
     order, or the link room would differ between the two screens. */
  tuples_match = a->tupleCount == b->tupleCount;
  for(i = 0; tuples_match && i < a->tupleCount && i < MAX_TUPLES; i++)
    tuples_match = a->tuples[i][0] == b->tuples[i][0] &&
     a->tuples[i][1] == b->tuples[i][1];
  CHECK(tuples_match);
  CHECK(a->tupleCount >= KEY_FRAMES * test->sessions);
  CHECK(a->delayCount != 0 && b->delayCount != 0);
  CHECK(a->delaySum <= ROOM_DELAY_LIMIT * a->delayCount);
  CHECK(b->delaySum <= ROOM_DELAY_LIMIT * b->delayCount);

  printf("pokemon %-26s %-8s %5.1fs, host %4.1f%% / joiner %4.1f%% waiting,"
   " room delay %4.1f/%4.1f frames (max %u/%u), errors %05X/%05X%s\n",
   test->name, net->name, (double)elapsed / 1e6,
   100.0 * (double)host.blocked_us / (double)elapsed,
   100.0 * (double)joiner.blocked_us / (double)elapsed,
   a->delayCount ? (double)a->delaySum / a->delayCount : 0.0,
   b->delayCount ? (double)b->delaySum / b->delayCount : 0.0,
   a->delayMax, b->delayMax, a->errors >> 12, b->errors >> 12,
   (a->errors | b->errors) != 0 || !tuples_match ||
   a->delaySum > ROOM_DELAY_LIMIT * a->delayCount ||
   b->delaySum > ROOM_DELAY_LIMIT * b->delayCount ? "  FAILED" : "");
}

static int joiner_waits_in_room(Console *consoles[2])
{
  return game_of(consoles[0])->phase == SCRIPT_ROOM &&
   game_of(consoles[1])->phase == SCRIPT_ROOM &&
   game_of(consoles[0])->keysSent > 30u && consoles[1]->cable.pokemon.waiting;
}

/* The host's game closes its link in the middle of the link room, as after
   a link error, while the joiner waits for the host's next frame. The
   joiner must not stay stuck waiting for a frame that will never come. */
static void test_pokemon_peer_closes_in_room(void)
{
  static const NetProfile net = { "adhoc", 3000u, 4000u, 3u, 1u, 0u, 0u };
  Console host;
  Console joiner;
  Console *consoles[2] = { &host, &joiner };

  memset(pokemon_games, 0, sizeof(pokemon_games));
  rng_state = 0xC105EDu;
  pokemon_games[0].seed = 11;
  pokemon_games[1].seed = 29;
  pokemon_games[0].openFrame = 30;
  pokemon_games[1].openFrame = 30;
  pokemon_games[0].sessionsWanted = 1;
  pokemon_games[1].sessionsWanted = 1;
  /* A slow host makes the joiner wait for its frames. */
  console_init(&host, 0, 1, "BPRE", &pokemon_guest, &pokemon_games[0], 0.6);
  console_init(&joiner, 1, 0, "BPGE", &pokemon_guest, &pokemon_games[1],
   1.0);

  run_link(consoles, &net, 30000000u, joiner_waits_in_room);
  CHECK(joiner_waits_in_room(consoles));
  CloseLink(&host);
  run_link(consoles, &net, 2000000u, never_done);

  CHECK(joiner.cable.status == LINK_CABLE_CONNECTED);
  CHECK(joiner.cable.pokemon.peer == LINK_POKEMON_CLOSED);
  CHECK(!link_cable_blocked(&joiner.cable, joiner.cycle));
  printf("pokemon host closes in the room: joiner %s\n",
   link_cable_blocked(&joiner.cable, joiner.cycle) ? "stuck  FAILED" :
   "keeps running");
}

static const NetProfile net_profiles[] =
{
  { "lan",       1500u,  1000u,  0u, 0u, 0u, 0u },
  { "adhoc",     3000u,  4000u,  3u, 1u, 0u, 0u },
  { "noisy",     8000u, 15000u, 15u, 5u, 0u, 0u },
  { "outage",    3000u,  4000u,  3u, 1u, 3000000u, 3400000u },
  { "terrible", 20000u, 40000u, 30u, 10u, 0u, 0u }
};

static void test_pokemon_trades(void)
{
  static const PokemonCase cases[] =
  {
    { "FireRed host, FireRed",  "BPRI", "BPRI", 1.00, 1.00, 30, 45, 2, -1, 0, 0 },
    { "FireRed host, LeafGreen", "BPRE", "BPGE", 1.00, 1.00, 30, 200, 1, -1, 0, 0 },
    { "LeafGreen host, Emerald", "BPGE", "BPEE", 1.00, 1.00, 300, 30, 1, -1, 0, 0 },
    { "Ruby host, FireRed",     "AXVE", "BPRE", 1.00, 1.00, 30, 30, 1, -1, 0, 0 },
    { "slow joiner (PSP-1000)", "BPRE", "BPGE", 1.00, 0.80, 30, 30, 1, -1, 0, 0 },
    { "slow host (PSP-1000)",   "BPRE", "BPGE", 0.80, 1.00, 30, 30, 1, -1, 0, 0 },
    { "uneven speeds",          "BPRE", "BPRE", 1.05, 0.93, 30, 60, 2, -1, 0, 0 },
    { "joiner freezes",         "BPRE", "BPGE", 1.00, 1.00, 30, 30, 2,
      1, 1700, 300 },
    { "host freezes",           "BPGE", "BPRE", 1.00, 1.00, 30, 30, 2,
      0, 1300, 250 }
  };
  size_t c;
  size_t n;

  for(n = 0; n < sizeof(net_profiles) / sizeof(net_profiles[0]); n++)
    for(c = 0; c < sizeof(cases) / sizeof(cases[0]); c++)
      run_pokemon_case(&cases[c], &net_profiles[n]);
  test_pokemon_peer_closes_in_room();
}

/* ---- Lockstep guests -------------------------------------------------- */

/* A generic multiplayer game: the master sends a burst of words every
   frame, and the slave answers each word with a value derived from the
   previous one. Real games are free to do this, so only a true word for
   word exchange can satisfy it. The slave also treats two frames in a row
   without a serial interrupt as a link error, as strict as games such as
   Mario Kart: Super Circuit: on a real cable the slave hears from the
   master on every frame the master sends one. */
typedef struct
{
  int master;
  uint16_t next_word;
  uint32_t transfers;
  uint32_t mismatches;
  uint32_t burst_left;
  uint32_t words_per_frame;
  uint32_t frames_without_irq;
  uint32_t timeouts;
  uint16_t last_received;
  uint32_t normal_mode;
  uint32_t last_value;
} LockstepGame;

static LockstepGame lockstep_games[2];

static LockstepGame *lockstep_of(Console *console)
{
  return (LockstepGame *)console->state;
}

static void lockstep_start_multi(Console *console)
{
  LockstepGame *game = lockstep_of(console);

  reg_write(console, REG_SIOMLT_SEND, game->next_word);
  reg_write(console, REG_SIOCNT, (uint16_t)(reg_read(console, REG_SIOCNT) |
   SIO_START));
}

static void lockstep_multi_irq(Console *console)
{
  LockstepGame *game = lockstep_of(console);
  uint16_t master_word = reg_read(console, 0x120);
  uint16_t slave_word = reg_read(console, 0x122);

  game->frames_without_irq = 0;
  game->transfers++;
  if(game->master)
  {
    /* The slave always answers with the master's previous word plus 1. */
    if(game->transfers > 1 &&
       slave_word != (uint16_t)(game->last_received + 1u))
      game->mismatches++;
    game->last_received = master_word;
    game->next_word = (uint16_t)(game->next_word * 7u + 3u);
    if(game->burst_left != 0 && --game->burst_left != 0)
      timer_start(console, 2000u);
  }
  else
  {
    if(game->transfers > 1 && slave_word != (uint16_t)(game->next_word))
      game->mismatches++;
    game->next_word = (uint16_t)(master_word + 1u);
    reg_write(console, REG_SIOMLT_SEND, game->next_word);
  }
}

static void lockstep_multi_timer(Console *console)
{
  timer_stop(console);
  lockstep_start_multi(console);
}

static void lockstep_multi_vblank(Console *console)
{
  LockstepGame *game = lockstep_of(console);

  if(console->frames == 20)
  {
    reg_write(console, REG_RCNT, 0);
    reg_write(console, REG_SIOCNT, SIO_MULTI_MODE | SIO_INTR_ENABLE | 3u);
    reg_write(console, REG_SIOMLT_SEND, game->next_word);
  }
  if(console->frames < 40 || console->cable.status != LINK_CABLE_CONNECTED)
    return;
  if(game->master)
  {
    if(game->burst_left == 0)
    {
      game->burst_left = game->words_per_frame;
      lockstep_start_multi(console);
    }
  }
  else if(game->transfers != 0 && ++game->frames_without_irq > 1)
    game->timeouts++;
}

static const Guest lockstep_multi_guest =
{
  lockstep_multi_irq,
  lockstep_multi_timer,
  lockstep_multi_vblank
};

static int lockstep_done(Console *consoles[2])
{
  return lockstep_of(consoles[0])->transfers >= 3000u &&
   lockstep_of(consoles[1])->transfers >= 3000u;
}

static void test_lockstep_multiplayer(const NetProfile *net,
 uint32_t words_per_frame, double host_speed, double joiner_speed)
{
  Console host;
  Console joiner;
  Console *consoles[2] = { &host, &joiner };
  uint64_t elapsed;
  double fps;

  memset(lockstep_games, 0, sizeof(lockstep_games));
  rng_state = 0xBEEFu ^ net->latency_us;
  lockstep_games[0].master = 1;
  lockstep_games[0].next_word = 1;
  lockstep_games[0].words_per_frame = words_per_frame;
  lockstep_games[1].words_per_frame = words_per_frame;
  console_init(&host, 0, 1, "AMKE", &lockstep_multi_guest,
   &lockstep_games[0], host_speed);
  console_init(&joiner, 1, 0, "AMKE", &lockstep_multi_guest,
   &lockstep_games[1], joiner_speed);

  elapsed = run_link(consoles, net, 600000000u, lockstep_done);
  fps = (double)host.frames / ((double)elapsed / 1e6);

  CHECK(host.cable.mode == LINK_CABLE_MODE_LOCKSTEP);
  CHECK(lockstep_games[0].transfers >= 3000u);
  CHECK(lockstep_games[1].transfers >= 3000u);
  CHECK(lockstep_games[0].mismatches == 0);
  CHECK(lockstep_games[1].mismatches == 0);
  CHECK(lockstep_games[1].timeouts == 0);
  printf("lockstep multiplayer %u words/frame %-8s speeds %.2f/%.2f: "
   "%u transfers, %.1f guest fps, mismatches %u/%u, slave timeouts %u\n",
   words_per_frame, net->name, host_speed, joiner_speed,
   lockstep_games[0].transfers, fps, lockstep_games[0].mismatches,
   lockstep_games[1].mismatches, lockstep_games[1].timeouts);
}

/* A normal-mode game: the clocking side sends a counter and the other side
   answers with the last value it received, set after every interrupt. */
static void normal_start(Console *console)
{
  LockstepGame *game = lockstep_of(console);

  reg_write(console, 0x120, (uint16_t)game->next_word);
  reg_write(console, 0x122, (uint16_t)(game->next_word >> 4));
  reg_write(console, REG_SIOCNT, (uint16_t)(0x1000u | 0x4000u | 0x0001u |
   SIO_START));
}

static void normal_irq(Console *console)
{
  LockstepGame *game = lockstep_of(console);
  uint32_t received = reg_read(console, 0x120) |
   ((uint32_t)reg_read(console, 0x122) << 16);

  game->frames_without_irq = 0;
  game->transfers++;
  if(game->master)
  {
    uint32_t sent_before = game->last_value;

    if(game->transfers > 1 && received != sent_before)
      game->mismatches++;
    game->last_value = (uint32_t)game->next_word |
     ((uint32_t)(game->next_word >> 4) << 16);
    game->next_word++;
    if(game->burst_left != 0 && --game->burst_left != 0)
      timer_start(console, 3000u);
  }
  else
  {
    uint32_t counter = received & 0xFFFFu;

    /* The host sends counter n as n | (n >> 4) << 16, counting up. */
    if(received != (counter | ((counter >> 4) << 16)) ||
       (game->last_value != 0 && counter != game->last_value + 1u))
      game->mismatches++;
    game->last_value = counter;
    /* Answer with what we just got, then wait for the next clock. */
    reg_write(console, 0x120, (uint16_t)received);
    reg_write(console, 0x122, (uint16_t)(received >> 16));
    reg_write(console, REG_SIOCNT, (uint16_t)(0x1000u | 0x4000u |
     SIO_START));
  }
}

static void normal_timer(Console *console)
{
  timer_stop(console);
  normal_start(console);
}

static void normal_vblank(Console *console)
{
  LockstepGame *game = lockstep_of(console);

  if(console->frames == 20)
  {
    reg_write(console, REG_RCNT, 0);
    if(game->master)
      reg_write(console, REG_SIOCNT, 0x1000u | 0x4000u | 0x0001u);
    else
    {
      reg_write(console, 0x120, 0);
      reg_write(console, 0x122, 0);
      reg_write(console, REG_SIOCNT, 0x1000u | 0x4000u | SIO_START);
    }
  }
  if(game->master && console->frames >= 60 && game->burst_left == 0 &&
     console->cable.status == LINK_CABLE_CONNECTED)
  {
    game->burst_left = game->words_per_frame;
    normal_start(console);
  }
}

static const Guest normal_guest =
{
  normal_irq,
  normal_timer,
  normal_vblank
};

static void test_lockstep_normal32(const NetProfile *net)
{
  Console host;
  Console joiner;
  Console *consoles[2] = { &host, &joiner };
  uint64_t elapsed;

  memset(lockstep_games, 0, sizeof(lockstep_games));
  rng_state = 0xFACEu ^ net->latency_us;
  lockstep_games[0].master = 1;
  lockstep_games[0].next_word = 1;
  lockstep_games[0].words_per_frame = 4;
  console_init(&host, 0, 1, "BZME", &normal_guest, &lockstep_games[0], 1.0);
  console_init(&joiner, 1, 0, "BZME", &normal_guest, &lockstep_games[1],
   1.0);

  elapsed = run_link(consoles, net, 600000000u, lockstep_done);

  CHECK(lockstep_games[0].transfers >= 3000u);
  CHECK(lockstep_games[0].mismatches == 0);
  CHECK(lockstep_games[1].mismatches == 0);
  printf("lockstep normal 32-bit %-8s: %u transfers in %.1fs, "
   "mismatches %u/%u\n", net->name, lockstep_games[0].transfers,
   (double)elapsed / 1e6, lockstep_games[0].mismatches,
   lockstep_games[1].mismatches);
}

/* ---- Session handling -------------------------------------------------- */

static void idle_irq(Console *console)
{
  (void)console;
}

static const Guest idle_guest = { idle_irq, idle_irq, idle_irq };

static void test_mode_selection_and_roles(void)
{
  char pokemon[][5] = { "AXVE", "AXPE", "BPEE", "BPRI", "BPGE", "BPRJ" };
  char other[][5] = { "AMKE", "BZME", "AGSE" };
  size_t i;

  for(i = 0; i < sizeof(pokemon) / sizeof(pokemon[0]); i++)
    CHECK(link_cable_is_pokemon_game(pokemon[i]));
  for(i = 0; i < 3; i++)
    CHECK(!link_cable_is_pokemon_game(other[i]));
}

static void test_session_lifecycle(void)
{
  static const NetProfile net = { "lan", 1500u, 1000u, 0u, 0u, 0u, 0u };
  Console a;
  Console b;
  Console *consoles[2] = { &a, &b };

  /* A Pokemon game and another game fall back to lockstep. */
  console_init(&a, 0, 1, "BPRE", &idle_guest, NULL, 1.0);
  console_init(&b, 1, 0, "AMKE", &idle_guest, NULL, 1.0);
  run_link(consoles, &net, 200000u, never_done);
  CHECK(a.cable.status == LINK_CABLE_CONNECTED);
  CHECK(b.cable.status == LINK_CABLE_CONNECTED);
  CHECK(a.cable.mode == LINK_CABLE_MODE_LOCKSTEP);
  CHECK(a.sio.player_count == 2 && a.sio.player_id == 0);
  CHECK(b.sio.player_count == 2 && b.sio.player_id == 1);

  /* A goodbye reaches the peer. */
  link_cable_leave(&a.cable);
  run_link(consoles, &net, 100000u, never_done);
  CHECK(b.cable.status == LINK_CABLE_PEER_LEFT);
  CHECK(b.sio.player_count == 0);

  /* Two hosts cannot pair. */
  console_init(&a, 0, 1, "BPRE", &idle_guest, NULL, 1.0);
  console_init(&b, 1, 1, "BPRE", &idle_guest, NULL, 1.0);
  run_link(consoles, &net, 200000u, never_done);
  CHECK(a.cable.status == LINK_CABLE_INCOMPATIBLE);
  CHECK(b.cable.status == LINK_CABLE_INCOMPATIBLE);

  /* A peer that disappears is detected, and the guest is released. */
  {
    static const NetProfile dead = { "dead", 1500u, 0u, 0u, 0u, 300000u,
     100000000u };

    console_init(&a, 0, 1, "BPRE", &idle_guest, NULL, 1.0);
    console_init(&b, 1, 0, "BPRE", &idle_guest, NULL, 1.0);
    run_link(consoles, &dead, 9000000u, never_done);
    CHECK(a.cable.status == LINK_CABLE_LOST);
    CHECK(b.cable.status == LINK_CABLE_LOST);
    CHECK(!link_cable_blocked(&a.cable, a.cycle));
  }
}

int main(int argc, char **argv)
{
  size_t n;
  int quick = argc > 1 && strcmp(argv[1], "--quick") == 0;

  test_mode_selection_and_roles();
  test_session_lifecycle();
  test_pokemon_trades();

  for(n = 0; n < sizeof(net_profiles) / sizeof(net_profiles[0]); n++)
  {
    if(quick && n > 1)
      break;
    test_lockstep_multiplayer(&net_profiles[n], 1, 1.0, 1.0);
    test_lockstep_multiplayer(&net_profiles[n], 4, 1.0, 0.85);
    /* A fast joiner reaches the pacing limit while the host waits for its
       reply; the joiner must still be able to answer. */
    test_lockstep_multiplayer(&net_profiles[n], 9, 1.0, 1.1);
    test_lockstep_normal32(&net_profiles[n]);
  }

  if(failures != 0)
  {
    fprintf(stderr, "%d link cable check(s) failed\n", failures);
    return 1;
  }
  printf("link cable tests passed\n");
  return 0;
}
