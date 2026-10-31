/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include "link_cable.h"

#include <string.h>

enum
{
  LINK_MSG_HELLO = 1,
  LINK_MSG_BYE,
  LINK_MSG_SIO_STATE,
  LINK_MSG_START,
  LINK_MSG_REPLY,
  LINK_MSG_POKEMON_STATE,
  LINK_MSG_POKEMON_PAIR,   /* host to joiner: both players' words of a frame */
  LINK_MSG_POKEMON_WORDS   /* joiner to host: the joiner's words of a frame */
};

#define LINK_SIO_CONTROL_IRQ 0x4000u

#define LINK_CYCLES_PER_FRAME 280896u
#define LINK_SILENCE_TIMEOUT_US 8000000u
#define LINK_HEARTBEAT_IDLE_US   250000u
#define LINK_HEARTBEAT_ACTIVE_US  16000u

/* Pacing: while the link is in use, a console may run at most this far
   ahead of the other one's guest clock. The figure covers network delay
   and the heartbeat interval, so it only bites on a real speed difference
   or when the other console is paused. */
#define LINK_MAX_LEAD (4u * LINK_CYCLES_PER_FRAME)
/* A lockstep link counts as in use for one second after a transfer. */
#define LINK_LOCKSTEP_ACTIVE (60u * LINK_CYCLES_PER_FRAME)

/* Pokemon link layer constants, from the pret decompilations. */
#define POKEMON_MASTER_HANDSHAKE 0x8FFFu
#define POKEMON_SLAVE_HANDSHAKE  0xB9A0u
/* The joiner's local transfers: one per frame during the handshake, then
   about ten per frame, slightly faster than the host's nine so the joiner
   never falls behind. These match gpSP's Pokemon backend. */
#define POKEMON_HANDSHAKE_INTERVAL 280064u
#define POKEMON_FRAME_INTERVAL      28672u
/* A connected game that keeps sending handshake words has restarted. */
#define POKEMON_RESTART_WORDS          18u

static void put16(uint8_t *out, uint16_t value)
{
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}

static uint16_t get16(const uint8_t *in)
{
  return (uint16_t)(in[0] | (in[1] << 8));
}

static void put32(uint8_t *out, uint32_t value)
{
  put16(out, (uint16_t)value);
  put16(out + 2, (uint16_t)(value >> 16));
}

static uint32_t get32(const uint8_t *in)
{
  return (uint32_t)get16(in) | ((uint32_t)get16(in + 2) << 16);
}

static uint32_t cycles_until(uint32_t target, uint32_t cycle)
{
  int32_t distance = (int32_t)(target - cycle);

  return distance > 0 ? (uint32_t)distance : 1u;
}

int link_cable_is_pokemon_game(const char game_code[4])
{
  static const char codes[][3] =
  {
    { 'A', 'X', 'V' },  /* Ruby */
    { 'A', 'X', 'P' },  /* Sapphire */
    { 'B', 'P', 'E' },  /* Emerald */
    { 'B', 'P', 'R' },  /* FireRed */
    { 'B', 'P', 'G' }   /* LeafGreen */
  };
  size_t i;

  for(i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
  {
    if(memcmp(game_code, codes[i], 3) == 0)
      return 1;
  }
  return 0;
}

static void link_cable_queue(LinkCable *cable, uint8_t type,
 const void *data, uint8_t size)
{
  /* Pacing keeps the window far from full; overflowing it would lose
     data, so treat it like a lost connection. */
  if(!link_net_queue(&cable->net, type, data, size))
    cable->status = LINK_CABLE_LOST;
}

/* A transfer with nobody on the other end: the bus reads all ones. */
static GbaSioEvent link_cable_complete_alone(LinkCable *cable,
 uint32_t cycle)
{
  GbaSioTransfer transfer;
  GbaSioResult result;

  if(!gba_sio_take_start(cable->sio, &transfer))
    return GBA_SIO_EVENT_NONE;

  memset(&result, 0, sizeof(result));
  result.data[0] = 0xFFFFFFFFu;
  if(transfer.mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    result.data[0] = transfer.outgoing;
    result.data[1] = 0xFFFFu;
    result.data[2] = 0xFFFFu;
    result.data[3] = 0xFFFFu;
  }
  gba_sio_set_cycle(cable->sio, cycle);
  gba_sio_commit(cable->sio, &result);
  return GBA_SIO_EVENT_NONE;
}

static GbaSioEvent link_cable_end(LinkCable *cable, LinkCableStatus status)
{
  if(cable->status == LINK_CABLE_CONNECTED ||
     cable->status == LINK_CABLE_WAITING)
    cable->status = status;
  cable->lockstep.waiting = 0;
  return gba_sio_disconnect(cable->sio);
}

void link_cable_start(LinkCable *cable, GbaSio *sio, int host,
 const char game_code[4], uint32_t local_id, uint64_t now_us)
{
  uint8_t hello[6];

  memset(cable, 0, sizeof(*cable));
  cable->sio = sio;
  cable->host = host != 0;
  cable->status = LINK_CABLE_WAITING;
  memcpy(cable->local_game, game_code, sizeof(cable->local_game));
  link_net_init(&cable->net, local_id, now_us);
  link_net_set_heartbeat(&cable->net, LINK_HEARTBEAT_IDLE_US);
  gba_sio_disconnect(sio);

  hello[0] = LINK_CABLE_PROTOCOL;
  hello[1] = cable->host;
  memcpy(hello + 2, game_code, 4);
  link_cable_queue(cable, LINK_MSG_HELLO, hello, sizeof(hello));
}

void link_cable_leave(LinkCable *cable)
{
  if(cable->bye_queued)
    return;
  cable->bye_queued = 1;
  link_net_queue(&cable->net, LINK_MSG_BYE, NULL, 0);
}

static void link_cable_hello(LinkCable *cable, const LinkNetMessage *message)
{
  if(cable->hello_received)
    return;
  cable->hello_received = 1;

  if(message->size < 6 || message->data[0] != LINK_CABLE_PROTOCOL ||
     (message->data[1] != 0) == cable->host)
  {
    link_cable_end(cable, LINK_CABLE_INCOMPATIBLE);
    return;
  }

  memcpy(cable->peer_game, message->data + 2, sizeof(cable->peer_game));
  cable->mode = link_cable_is_pokemon_game(cable->local_game) &&
   link_cable_is_pokemon_game(cable->peer_game) ?
   LINK_CABLE_MODE_POKEMON : LINK_CABLE_MODE_LOCKSTEP;
  cable->status = LINK_CABLE_CONNECTED;
  cable->pokemon.interrupt_disables = cable->sio->interrupt_disables;

  gba_sio_set_connection(cable->sio, cable->host ? 0 : 1, 2);
  /* The Pokemon games only need the other side to look present; their
     own handshake decides when the link is up. */
  if(cable->mode == LINK_CABLE_MODE_POKEMON)
    gba_sio_set_peer_state(cable->sio, GBA_SIO_MODE_MULTIPLAYER, 0, 0, 1);
}

/* ---- Lockstep mode ---------------------------------------------------- */

static void lockstep_publish_state(LinkCable *cable)
{
  LinkLockstep *lockstep = &cable->lockstep;
  uint16_t control = cable->sio->registers[GBA_SIO_REG_CONTROL >> 1];
  uint8_t message[5];

  /* The peer needs our mode for its ready bit and, in normal mode, our
     SO output, which it reads as SI. */
  control &= 0x0008u;
  if(lockstep->state_sent && lockstep->state_mode == cable->sio->mode &&
     lockstep->state_control == control)
    return;

  lockstep->state_sent = 1;
  lockstep->state_mode = (uint8_t)cable->sio->mode;
  lockstep->state_control = control;
  message[0] = lockstep->state_mode;
  put16(message + 1, control);
  put16(message + 3, cable->sio->registers[GBA_SIO_REG_RCNT >> 1]);
  link_cable_queue(cable, LINK_MSG_SIO_STATE, message, sizeof(message));
}

static void lockstep_begin(LinkCable *cable, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  GbaSioTransfer transfer;
  uint8_t message[14];

  if(lockstep->waiting || !gba_sio_take_start(cable->sio, &transfer))
    return;

  lockstep->sequence++;
  lockstep->waiting = 1;
  lockstep->start_cycle = cycle;
  lockstep->finish_cycle = cycle + gba_sio_transfer_cycles(transfer.mode,
   transfer.setting, transfer.player_count);
  lockstep->outgoing = transfer.outgoing;
  lockstep->last_transfer = cycle;
  lockstep->transfers++;

  put32(message, lockstep->sequence);
  put32(message + 4, cycle);
  put32(message + 8, transfer.outgoing);
  message[12] = (uint8_t)transfer.mode;
  message[13] = transfer.setting;
  link_cable_queue(cable, LINK_MSG_START, message, sizeof(message));
}

static GbaSioEvent lockstep_reply(LinkCable *cable,
 const LinkNetMessage *message, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  GbaSioResult result;
  GbaSioEvent event = GBA_SIO_EVENT_NONE;
  uint32_t word;

  if(message->size < 9 || !lockstep->waiting ||
     get32(message->data) != lockstep->sequence)
    return GBA_SIO_EVENT_NONE;

  lockstep->waiting = 0;
  if(!gba_sio_is_waiting(cable->sio))
    return GBA_SIO_EVENT_NONE;

  word = message->data[8] ? get32(message->data + 4) : 0xFFFFFFFFu;
  memset(&result, 0, sizeof(result));
  if(cable->sio->transfer_mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    result.data[0] = lockstep->outgoing;
    result.data[1] = message->data[8] ? (word & 0xFFFFu) : 0xFFFFu;
    result.data[2] = 0xFFFFu;
    result.data[3] = 0xFFFFu;
  }
  else
  {
    result.data[0] = word;
  }

  /* The guest kept running while the reply travelled; finish the
     transfer at the cycle it would have finished on real hardware, or
     now if that moment has already passed. */
  gba_sio_set_cycle(cable->sio, cycle);
  gba_sio_commit_at_horizon(cable->sio, &result, lockstep->start_cycle,
   lockstep->finish_cycle, &event);
  return event;
}

/* Returns 0 if the START must wait for a later guest cycle. */
static int lockstep_remote_start(LinkCable *cable,
 const LinkNetMessage *message, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  GbaSioTransfer transfer;
  GbaSioResult result;
  uint8_t reply[9];
  uint32_t host_cycle;
  uint32_t word;
  int ready;

  if(message->size < 14)
    return 1;

  host_cycle = get32(message->data + 4);
  word = get32(message->data + 8);

  /* Keep the spacing the other guest saw between two transfers, so this
     guest's interrupt handler has time to load its next word. */
  if(lockstep->anchored)
  {
    uint32_t gap = host_cycle - lockstep->peer_anchor;

    if(gap < LINK_CYCLES_PER_FRAME &&
       (int32_t)(cycle - (lockstep->local_anchor + gap)) < 0)
    {
      lockstep->deferred = 1;
      lockstep->deferred_until = lockstep->local_anchor + gap;
      return 0;
    }
  }
  if(cable->sio->active)
  {
    lockstep->deferred = 1;
    lockstep->deferred_until = cycle + cable->sio->cycles_remaining;
    return 0;
  }
  lockstep->deferred = 0;

  ready = gba_sio_latch_remote_start(cable->sio,
   (GbaSioMode)message->data[12], message->data[13], &transfer);
  if(ready)
  {
    memset(&result, 0, sizeof(result));
    if(transfer.mode == GBA_SIO_MODE_MULTIPLAYER)
    {
      result.data[0] = word & 0xFFFFu;
      result.data[1] = transfer.outgoing;
      result.data[2] = 0xFFFFu;
      result.data[3] = 0xFFFFu;
    }
    else
    {
      result.data[0] = word;
    }
    gba_sio_set_cycle(cable->sio, cycle);
    gba_sio_commit(cable->sio, &result);
  }

  memcpy(reply, message->data, 4);
  put32(reply + 4, ready ? transfer.outgoing : 0xFFFFFFFFu);
  reply[8] = (uint8_t)(ready != 0);
  link_cable_queue(cable, LINK_MSG_REPLY, reply, sizeof(reply));

  lockstep->anchored = 1;
  lockstep->local_anchor = cycle;
  lockstep->peer_anchor = host_cycle;
  lockstep->last_transfer = cycle;
  lockstep->transfers++;
  return 1;
}

/* ---- Pokemon mode ----------------------------------------------------- */

static int pokemon_is_handshake(uint16_t word)
{
  return (word & ~3u) == POKEMON_SLAVE_HANDSHAKE ||
   word == POKEMON_MASTER_HANDSHAKE;
}

/* Player count exactly as the game's DoHandshake computes it. */
static unsigned int pokemon_handshake_count(const uint16_t words[4])
{
  unsigned int count = 0;
  unsigned int i;

  for(i = 0; i < 4; i++)
  {
    if(pokemon_is_handshake(words[i]))
      count++;
    else
    {
      if(words[i] != 0xFFFFu)
        count = 0;
      break;
    }
  }
  return count;
}

/* Mirrors DoHandshake on the delivered words; true when the game will
   enter its connected state on this transfer. */
static int pokemon_handshake(LinkPokemon *pokemon, const uint16_t words[4])
{
  unsigned int count = pokemon_handshake_count(words);

  if(count > 1 && count == pokemon->handshake_count &&
     words[0] == POKEMON_MASTER_HANDSHAKE)
    return 1;
  pokemon->handshake_count = (uint8_t)count;
  return 0;
}

static void pokemon_set_local(LinkPokemon *pokemon, LinkPokemonState state)
{
  pokemon->local = state;
  pokemon->offset = 0;
  pokemon->handshake_repeats = 0;
  pokemon->pair_valid = 0;
  if(state != LINK_POKEMON_HANDSHAKE)
    pokemon->handshake_count = 0;
}

static void pokemon_watch_restart(LinkPokemon *pokemon, uint16_t word)
{
  if(word != POKEMON_SLAVE_HANDSHAKE)
    pokemon->handshake_repeats = 0;
  else if(++pokemon->handshake_repeats > POKEMON_RESTART_WORDS)
    pokemon_set_local(pokemon, LINK_POKEMON_HANDSHAKE);
}

static void pokemon_report(LinkCable *cable)
{
  LinkPokemon *pokemon = &cable->pokemon;
  uint8_t state = (uint8_t)pokemon->local;

  if(pokemon->reported == pokemon->local)
    return;
  pokemon->reported = pokemon->local;
  link_cable_queue(cable, LINK_MSG_POKEMON_STATE, &state, 1);
}

static GbaSioEvent link_cable_drain(LinkCable *cable, uint32_t cycle);

/* Loads the other console's next frame, or an empty frame. */
static void pokemon_take_pair(LinkCable *cable, uint32_t cycle)
{
  LinkPokemon *pokemon = &cable->pokemon;
  const LinkNetMessage *message;
  uint8_t expected = cable->host ?
   LINK_MSG_POKEMON_WORDS : LINK_MSG_POKEMON_PAIR;
  unsigned int i;

  memset(pokemon->pair, 0, sizeof(pokemon->pair));
  pokemon->pair_valid = 0;

  link_cable_drain(cable, cycle);
  message = link_net_peek(&cable->net);
  if(message == NULL || message->type != expected)
    return;

  for(i = 0; i < 8; i++)
  {
    if(cable->host)
      pokemon->pair[1][i] = get16(message->data + i * 2);
    else
    {
      pokemon->pair[0][i] = get16(message->data + i * 2);
      pokemon->pair[1][i] = get16(message->data + 16 + i * 2);
    }
  }
  pokemon->pair_valid = 1;
  pokemon->frames_received++;
  link_net_pop(&cable->net);
}

static int pokemon_words_nonzero(const uint16_t words[8])
{
  unsigned int i;

  for(i = 0; i < 8; i++)
  {
    if(words[i] != 0)
      return 1;
  }
  return 0;
}

/* Steps the checksum and data words of a connected game's frame. Returns
   the words this guest receives in player slots 0 and 1. */
static void pokemon_frame_word(LinkCable *cable, uint16_t local_word,
 uint16_t words[4], uint32_t cycle)
{
  LinkPokemon *pokemon = &cable->pokemon;
  unsigned int i;

  if(pokemon->offset == 0)
  {
    /* Checksum word. Every player's copy must equal the sum this game
       computed itself, so echo its own word into every slot. */
    words[0] = local_word;
    words[1] = local_word;
    pokemon_take_pair(cable, cycle);
    pokemon->offset = 1;
    return;
  }

  i = pokemon->offset - 1u;
  pokemon->local_words[i] = local_word;
  if(cable->host)
  {
    /* The host's own word is real; the joiner's comes from its queue. */
    words[0] = local_word;
    words[1] = pokemon->pair[1][i];
  }
  else
  {
    /* The joiner replays the host's pairing, including its own words,
       so both games process identical frames. */
    words[0] = pokemon->pair[0][i];
    words[1] = pokemon->pair[1][i];
  }

  if(++pokemon->offset <= 8)
    return;

  pokemon->offset = 0;
  if(cable->host)
  {
    if(pokemon_words_nonzero(pokemon->local_words) || pokemon->pair_valid)
    {
      uint8_t message[32];

      for(i = 0; i < 8; i++)
      {
        put16(message + i * 2, pokemon->local_words[i]);
        put16(message + 16 + i * 2, pokemon->pair[1][i]);
      }
      link_cable_queue(cable, LINK_MSG_POKEMON_PAIR, message,
       sizeof(message));
      pokemon->frames_sent++;
    }
  }
  else if(pokemon_words_nonzero(pokemon->local_words))
  {
    uint8_t message[16];

    for(i = 0; i < 8; i++)
      put16(message + i * 2, pokemon->local_words[i]);
    link_cable_queue(cable, LINK_MSG_POKEMON_WORDS, message,
     sizeof(message));
    pokemon->frames_sent++;
  }
}

static void pokemon_commit(LinkCable *cable, const uint16_t words[4],
 uint32_t cycle)
{
  GbaSioResult result;
  unsigned int i;

  memset(&result, 0, sizeof(result));
  for(i = 0; i < 4; i++)
    result.data[i] = words[i];
  gba_sio_set_cycle(cable->sio, cycle);
  gba_sio_commit(cable->sio, &result);
}

/* The host's game started a multiplayer transfer. */
static void pokemon_host_transfer(LinkCable *cable, uint16_t local_word,
 uint32_t cycle)
{
  LinkPokemon *pokemon = &cable->pokemon;
  uint16_t words[4] = { local_word, 0xFFFFu, 0xFFFFu, 0xFFFFu };

  if(pokemon->local == LINK_POKEMON_CONNECTED)
    pokemon_watch_restart(pokemon, local_word);

  if(pokemon->local != LINK_POKEMON_CONNECTED)
  {
    if(pokemon->local == LINK_POKEMON_CLOSED &&
       pokemon_is_handshake(local_word))
      pokemon_set_local(pokemon, LINK_POKEMON_HANDSHAKE);
    if(pokemon->peer == LINK_POKEMON_HANDSHAKE)
      words[1] = POKEMON_SLAVE_HANDSHAKE;
    if(pokemon_handshake(pokemon, words))
      pokemon_set_local(pokemon, LINK_POKEMON_CONNECTED);
  }
  else
  {
    pokemon_frame_word(cable, local_word, words, cycle);
  }

  pokemon_commit(cable, words, cycle);
}

/* The joiner has no master to clock it, so the cable plays that part. */
static void pokemon_joiner_transfer(LinkCable *cable, uint32_t cycle)
{
  LinkPokemon *pokemon = &cable->pokemon;
  GbaSioTransfer transfer;
  uint16_t control = cable->sio->registers[GBA_SIO_REG_CONTROL >> 1];
  uint16_t words[4] = { POKEMON_SLAVE_HANDSHAKE, 0, 0xFFFFu, 0xFFFFu };
  uint16_t local_word;

  if(!gba_sio_latch_remote_start(cable->sio, GBA_SIO_MODE_MULTIPLAYER,
     (uint8_t)(control & 3u), &transfer))
    return;
  local_word = (uint16_t)transfer.outgoing;
  words[1] = local_word;

  if(pokemon->local == LINK_POKEMON_CONNECTED)
    pokemon_watch_restart(pokemon, local_word);

  if(pokemon->local != LINK_POKEMON_CONNECTED)
  {
    if(pokemon->local == LINK_POKEMON_CLOSED &&
       pokemon_is_handshake(local_word))
      pokemon_set_local(pokemon, LINK_POKEMON_HANDSHAKE);

    /* Once the host's game is connected, send its master handshake as
       soon as this game's own count would accept it. */
    if(pokemon->peer == LINK_POKEMON_CONNECTED)
    {
      uint16_t master[4];
      unsigned int count;

      memcpy(master, words, sizeof(master));
      master[0] = POKEMON_MASTER_HANDSHAKE;
      count = pokemon_handshake_count(master);
      if(count > 1 && count == pokemon->handshake_count)
        words[0] = POKEMON_MASTER_HANDSHAKE;
    }
    if(pokemon_handshake(pokemon, words))
      pokemon_set_local(pokemon, LINK_POKEMON_CONNECTED);
  }
  else
  {
    pokemon_frame_word(cable, local_word, words, cycle);
  }

  pokemon_commit(cable, words, cycle);
}

static int pokemon_drop_frame(const LinkCable *cable, uint8_t type)
{
  if(cable->mode != LINK_CABLE_MODE_POKEMON)
    return 1;
  if(cable->host)
    return type != LINK_MSG_POKEMON_WORDS ||
     cable->pokemon.local != LINK_POKEMON_CONNECTED;
  return type != LINK_MSG_POKEMON_PAIR ||
   cable->pokemon.local == LINK_POKEMON_CLOSED;
}

static GbaSioEvent pokemon_service(LinkCable *cable, uint32_t cycle)
{
  LinkPokemon *pokemon = &cable->pokemon;
  GbaSio *sio = cable->sio;
  GbaSioTransfer transfer;
  uint16_t control = sio->registers[GBA_SIO_REG_CONTROL >> 1];
  int open = sio->mode == GBA_SIO_MODE_MULTIPLAYER &&
   (control & LINK_SIO_CONTROL_IRQ) != 0;

  /* The game switched its serial interrupt off: its link layer closed
     or is starting over. */
  if(sio->interrupt_disables != pokemon->interrupt_disables || !open)
  {
    pokemon->interrupt_disables = sio->interrupt_disables;
    if(pokemon->local != LINK_POKEMON_CLOSED)
      pokemon_set_local(pokemon, LINK_POKEMON_CLOSED);
  }

  if(sio->pending_start)
  {
    if(cable->host && sio->mode == GBA_SIO_MODE_MULTIPLAYER &&
       gba_sio_take_start(sio, &transfer))
      pokemon_host_transfer(cable, (uint16_t)transfer.outgoing, cycle);
    else
      /* Normal-mode probes, such as the wireless adapter check, find
         nothing attached. */
      link_cable_complete_alone(cable, cycle);
  }

  if(!cable->host)
  {
    /* Keep clocking while the host's game is linked, and always finish a
       frame already started: its words may carry the host's last data. */
    int wanted = open && (pokemon->peer == LINK_POKEMON_HANDSHAKE ||
     pokemon->peer == LINK_POKEMON_CONNECTED || pokemon->offset != 0);

    if(!wanted)
      pokemon->transfer_scheduled = 0;
    else if(!pokemon->transfer_scheduled)
    {
      pokemon->transfer_scheduled = 1;
      pokemon->next_transfer = cycle + POKEMON_HANDSHAKE_INTERVAL;
    }
    else if((int32_t)(cycle - pokemon->next_transfer) >= 0 &&
            !gba_sio_is_busy(sio))
    {
      pokemon_joiner_transfer(cable, cycle);
      pokemon->next_transfer = cycle +
       (pokemon->local == LINK_POKEMON_CONNECTED ?
        POKEMON_FRAME_INTERVAL : POKEMON_HANDSHAKE_INTERVAL);
    }
  }

  pokemon_report(cable);
  return GBA_SIO_EVENT_NONE;
}

/* ---- Shared ----------------------------------------------------------- */

/* Applies delivered messages in order. Frames stay queued until the guest
   reaches the frame that consumes them. */
static GbaSioEvent link_cable_drain(LinkCable *cable, uint32_t cycle)
{
  const LinkNetMessage *message;
  GbaSioEvent events = GBA_SIO_EVENT_NONE;

  while((message = link_net_peek(&cable->net)) != NULL)
  {
    switch(message->type)
    {
      case LINK_MSG_HELLO:
        link_cable_hello(cable, message);
        break;

      case LINK_MSG_BYE:
        events |= link_cable_end(cable, LINK_CABLE_PEER_LEFT);
        break;

      case LINK_MSG_SIO_STATE:
        if(cable->mode == LINK_CABLE_MODE_LOCKSTEP && message->size >= 5)
          gba_sio_set_peer_state(cable->sio,
           (GbaSioMode)message->data[0], get16(message->data + 1),
           get16(message->data + 3), 1);
        break;

      case LINK_MSG_START:
        if(cable->mode == LINK_CABLE_MODE_LOCKSTEP &&
           !lockstep_remote_start(cable, message, cycle))
          return events;
        break;

      case LINK_MSG_REPLY:
        if(cable->mode == LINK_CABLE_MODE_LOCKSTEP)
          events |= lockstep_reply(cable, message, cycle);
        break;

      case LINK_MSG_POKEMON_STATE:
        if(message->size >= 1 && message->data[0] <= LINK_POKEMON_CONNECTED)
          cable->pokemon.peer = (LinkPokemonState)message->data[0];
        break;

      case LINK_MSG_POKEMON_PAIR:
      case LINK_MSG_POKEMON_WORDS:
        if(!pokemon_drop_frame(cable, message->type))
          return events;
        break;

      default:
        break;
    }
    link_net_pop(&cable->net);
    if(cable->status != LINK_CABLE_CONNECTED &&
       cable->status != LINK_CABLE_WAITING)
      break;
  }
  return events;
}

static int link_cable_in_use(const LinkCable *cable, uint32_t cycle)
{
  if(cable->mode == LINK_CABLE_MODE_POKEMON)
    return cable->pokemon.local != LINK_POKEMON_CLOSED ||
     cable->pokemon.peer != LINK_POKEMON_CLOSED;
  return cable->lockstep.transfers != 0 &&
   cycle - cable->lockstep.last_transfer < LINK_LOCKSTEP_ACTIVE;
}

GbaSioEvent link_cable_service(LinkCable *cable, uint32_t cycle,
 uint64_t now_us)
{
  GbaSioEvent events = GBA_SIO_EVENT_NONE;

  if(cable->status == LINK_CABLE_OFF)
    return GBA_SIO_EVENT_NONE;

  if((cable->status == LINK_CABLE_CONNECTED ||
      cable->status == LINK_CABLE_WAITING) && cable->net.peer_seen &&
     link_net_peer_silent(&cable->net, now_us, LINK_SILENCE_TIMEOUT_US))
    events |= link_cable_end(cable, LINK_CABLE_LOST);

  if(cable->status == LINK_CABLE_CONNECTED ||
     cable->status == LINK_CABLE_WAITING)
    events |= link_cable_drain(cable, cycle);

  if(cable->status != LINK_CABLE_CONNECTED)
  {
    link_cable_complete_alone(cable, cycle);
    return events;
  }

  if(cable->mode == LINK_CABLE_MODE_POKEMON)
    events |= pokemon_service(cable, cycle);
  else
  {
    lockstep_publish_state(cable);
    if(cable->sio->pending_start)
      lockstep_begin(cable, cycle);
  }

  /* While idle, keep the pacing reference current so a long pause before
     the link is used never counts as lead. */
  if(!link_cable_in_use(cable, cycle))
  {
    cable->pacing_local = cycle;
    cable->pacing_peer = cable->net.peer_clock;
    link_net_set_heartbeat(&cable->net, LINK_HEARTBEAT_IDLE_US);
  }
  else
    link_net_set_heartbeat(&cable->net, LINK_HEARTBEAT_ACTIVE_US);

  return events;
}

uint32_t link_cable_cycles_until_service(const LinkCable *cable,
 uint32_t cycle)
{
  uint32_t cycles = UINT32_MAX;

  if(cable->status != LINK_CABLE_CONNECTED)
    return cycles;

  if(cable->mode == LINK_CABLE_MODE_LOCKSTEP && cable->lockstep.waiting)
    cycles = cycles_until(cable->lockstep.finish_cycle, cycle);
  if(cable->mode == LINK_CABLE_MODE_LOCKSTEP && cable->lockstep.deferred)
  {
    uint32_t next = cycles_until(cable->lockstep.deferred_until, cycle);

    if(next < cycles)
      cycles = next;
  }
  if(cable->mode == LINK_CABLE_MODE_POKEMON && !cable->host &&
     cable->pokemon.transfer_scheduled)
  {
    uint32_t next = cycles_until(cable->pokemon.next_transfer, cycle);

    if(next < cycles)
      cycles = next;
  }
  return cycles;
}

int link_cable_due(const LinkCable *cable, uint32_t cycle)
{
  const LinkLockstep *lockstep = &cable->lockstep;

  if(cable->status != LINK_CABLE_CONNECTED)
    return 0;
  if(cable->mode == LINK_CABLE_MODE_LOCKSTEP)
    return (lockstep->waiting &&
     (int32_t)(cycle - lockstep->finish_cycle) >= 0) ||
     (lockstep->deferred &&
      (int32_t)(cycle - lockstep->deferred_until) >= 0);
  return !cable->host && cable->pokemon.transfer_scheduled &&
   (int32_t)(cycle - cable->pokemon.next_transfer) >= 0;
}

int link_cable_blocked(const LinkCable *cable, uint32_t cycle)
{
  int32_t peer_elapsed;
  int32_t lead;

  if(cable->status != LINK_CABLE_CONNECTED)
    return 0;

  if(cable->mode == LINK_CABLE_MODE_LOCKSTEP && cable->lockstep.waiting &&
     gba_sio_is_waiting(cable->sio) &&
     (int32_t)(cycle - cable->lockstep.finish_cycle) >= 0)
    return 1;

  if(!link_cable_in_use(cable, cycle))
    return 0;

  peer_elapsed = (int32_t)(cable->net.peer_clock - cable->pacing_peer);
  if(peer_elapsed < 0)
    peer_elapsed = 0;
  lead = (int32_t)(cycle - cable->pacing_local) - peer_elapsed;
  return lead > (int32_t)LINK_MAX_LEAD;
}

int link_cable_receive(LinkCable *cable, const uint8_t *datagram,
 size_t size, uint64_t now_us)
{
  if(cable->status == LINK_CABLE_OFF)
    return 0;
  return link_net_receive(&cable->net, datagram, size, now_us);
}

size_t link_cable_build(LinkCable *cable, uint8_t *datagram,
 size_t capacity, uint32_t cycle, uint64_t now_us)
{
  if(cable->status == LINK_CABLE_OFF)
    return 0;
  return link_net_build(&cable->net, datagram, capacity, cycle, now_us);
}
