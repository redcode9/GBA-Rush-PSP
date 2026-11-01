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

/* Message types and payloads (little-endian). The values go on the wire:
   append new types only, and keep 0 free (link_net rejects it). */
enum
{
  LINK_MSG_HELLO = 1,      /* protocol, host flag, 4-letter game code */
  LINK_MSG_BYE,            /* empty */
  LINK_MSG_SIO_STATE,      /* mode, SIOCNT SO bit (u16), RCNT (u16) */
  LINK_MSG_START,          /* sequence, cycle, word (u32 each), mode,
                              setting (baud rate index in Multiplayer, 2 MHz
                              flag in Normal) */
  LINK_MSG_REPLY,          /* sequence, word (u32 each), ready flag */
  LINK_MSG_POKEMON_STATE,  /* the sender's LinkPokemonState */
  LINK_MSG_POKEMON_PAIR,   /* host to joiner: both players' words of a frame */
  LINK_MSG_POKEMON_WORDS,  /* joiner to host: its words of a frame, then
                              how many host frames it has seen (u32) */
  LINK_MSG_NEXT_REPLY      /* lockstep receiver: sequence of the START it
                              applied last, then its reply to the next one:
                              word (u32), ready flag */
};

#define LINK_SIO_CONTROL_IRQ 0x4000u
/* SIOCNT bit 0 in Normal mode: this console drives the clock. */
#define LINK_SIO_CONTROL_CLOCK 0x0001u
/* SIOCNT bit 3: SO in Normal mode, READY in Multiplayer. */
#define LINK_SIO_CONTROL_SO 0x0008u

#define LINK_CYCLES_PER_FRAME 280896u
#define LINK_SILENCE_TIMEOUT_US 8000000u
#define LINK_HEARTBEAT_IDLE_US   250000u
#define LINK_HEARTBEAT_ACTIVE_US  16000u
/* A lockstep receiver may only run up to the clock the sender last
   reported, and its announced reply must reach the sender before the next
   START, so the sender reports its clock often while the link is used. The
   receiver's clock only paces the sender, which allows four frames of lead,
   so it keeps the ordinary rate. */
#define LINK_HEARTBEAT_LOCKSTEP_US 2000u

/* Lockstep pacing: while the link is in use, a console may run at most
   this far ahead of the other one's guest clock. The figure covers network
   delay and the heartbeat interval, so it only bites on a real speed
   difference or when the other console is paused. */
#define LINK_MAX_LEAD (4u * LINK_CYCLES_PER_FRAME)
/* A lockstep link counts as in use for one second after a transfer. */
#define LINK_LOCKSTEP_ACTIVE (60u * LINK_CYCLES_PER_FRAME)
/* The clocking console completes a transfer with the other console's
   announced reply only once this many announcements in a row matched the
   real reply, about five seconds of a game that sends a word per frame.
   Each announcement that was used and turned out wrong doubles the figure,
   so a game that keeps changing its word late goes back to waiting. */
#define LINK_TRUST_AFTER     300u
#define LINK_TRUST_AFTER_MAX (1u << 20)
/* The receiving console announces its next reply once the game has loaded
   its next word and left the serial registers alone for this long. Games
   often write SIOCNT and a placeholder word just before the real one. */
#define LINK_ANNOUNCE_QUIET 4096u

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
/* LINKCMD_SEND_HELD_KEYS: in the Cable Club rooms every game sends its
   held keys this way on every frame. */
#define POKEMON_HELD_KEYS 0xCAFEu
/* Frames kept in the loop between the two games in the rooms: a Wi-Fi
   round trip plus a margin for jitter, within these bounds. A smaller
   margin shortens the delay in the rooms but makes the consoles wait for
   each other more often. */
#define POKEMON_LOOP_MARGIN  5u
#define POKEMON_LOOP_MIN     3u
#define POKEMON_LOOP_MAX    15u
/* A console never waits longer than this for the other one's frame. */
#define POKEMON_WAIT_LIMIT_US 1000000u
/* Messages the other console may leave unconsumed before this one waits,
   which keeps a long outage from overflowing the channel. */
#define POKEMON_MAX_BACKLOG 64u
/* One GBA frame in microseconds (59.73 Hz). */
#define LINK_FRAME_US 16743u

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

/* Guest cycles from `cycle` until `target`; at least 1, even if it passed. */
static uint32_t cycles_until(uint32_t target, uint32_t cycle)
{
  int32_t distance = (int32_t)(target - cycle);

  return distance > 0 ? (uint32_t)distance : 1u;
}

/* Matches the first three letters of the game code; the last is the
   region, so every language version counts. */
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

/* Records why the session ended (unless it already had) and unplugs the
   guest's cable. */
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
  cable->lockstep.trust_after = LINK_TRUST_AFTER;
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

/* The peer's HELLO: protocol, whether it hosts (exactly one side must)
   and its game code. Both games decide the mode: Pokemon or lockstep. */
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

/* Defined with the shared helpers near the end of this file. */
static int lockstep_is_receiver(const LinkCable *cable);

/* Sends SIO_STATE (mode, SIOCNT bit 3, RCNT) whenever the mode or bit 3
   changes; bit 3 is SO in Normal mode and READY in Multiplayer. */
static void lockstep_publish_state(LinkCable *cable)
{
  LinkLockstep *lockstep = &cable->lockstep;
  uint16_t control = cable->sio->registers[GBA_SIO_REG_CONTROL >> 1];
  uint8_t message[5];

  /* The peer needs our mode for its ready bit and, in normal mode, our
     SO output, which it reads as SI. */
  control &= LINK_SIO_CONTROL_SO;
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

/* Completes the transfer we clocked with the other console's reply. The
   guest kept running while the reply travelled; the transfer finishes at
   the cycle it would have finished on real hardware, or now if that moment
   has already passed. A reply that was not ready reads as all ones. */
static GbaSioEvent lockstep_finish(LinkCable *cable, uint32_t word,
 int ready, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  GbaSioResult result;
  GbaSioEvent event = GBA_SIO_EVENT_NONE;

  memset(&result, 0, sizeof(result));
  if(cable->sio->transfer_mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    result.data[0] = lockstep->outgoing;
    result.data[1] = ready ? (word & 0xFFFFu) : 0xFFFFu;
    result.data[2] = 0xFFFFu;
    result.data[3] = 0xFFFFu;
  }
  else
  {
    result.data[0] = ready ? word : 0xFFFFFFFFu;
  }

  gba_sio_set_cycle(cable->sio, cycle);
  gba_sio_commit_at_horizon(cable->sio, &result, lockstep->start_cycle,
   lockstep->finish_cycle, &event);
  return event;
}

/* Our guest clocked a transfer: send START with its word and wait for the
   REPLY, which real hardware would deliver by finish_cycle, unless a
   trusted announcement already gives the reply. */
static GbaSioEvent lockstep_begin(LinkCable *cable, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  GbaSioTransfer transfer;
  uint8_t message[14];
  int same_mode;

  if(lockstep->waiting || !gba_sio_take_start(cable->sio, &transfer))
    return GBA_SIO_EVENT_NONE;

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

  /* Once the other console's announcements have proved right, finish the
     transfer with the one it made after applying our previous START. */
  same_mode = lockstep->start_mode == (uint8_t)transfer.mode;
  lockstep->start_mode = (uint8_t)transfer.mode;
  if(!same_mode || lockstep->streak < lockstep->trust_after ||
     !lockstep->predicted ||
     lockstep->predicted_sequence + 1u != lockstep->sequence)
    return GBA_SIO_EVENT_NONE;
  lockstep->verify_used = 1;
  lockstep->verify_sequence = lockstep->sequence;
  lockstep->verify_word = lockstep->predicted_word;
  lockstep->verify_ready = lockstep->predicted_ready;
  lockstep->waiting = 0;
  lockstep->answered_early++;
  return lockstep_finish(cable, lockstep->predicted_word,
   lockstep->predicted_ready, cycle);
}

/* Compares the real reply to a START with the announcement for it: the one
   used to finish the transfer, or else the latest one, which on an ordered
   channel arrived before the reply. Only an announcement that was used and
   turned out wrong raises the bar for trusting them again. The guest has
   already received a used announcement's word; a wrong one is not undone. */
static void lockstep_check(LinkLockstep *lockstep, uint32_t sequence,
 uint32_t word, uint8_t ready)
{
  uint32_t expected_word;
  uint8_t expected_ready;
  int used = lockstep->verify_used && sequence == lockstep->verify_sequence;

  if(used)
  {
    expected_word = lockstep->verify_word;
    expected_ready = lockstep->verify_ready;
    lockstep->verify_used = 0;
  }
  else if(lockstep->predicted &&
          lockstep->predicted_sequence + 1u == sequence)
  {
    expected_word = lockstep->predicted_word;
    expected_ready = lockstep->predicted_ready;
  }
  else
    return;

  if((ready != 0) == (expected_ready != 0) &&
     (ready == 0 || word == expected_word))
  {
    if(lockstep->streak < UINT32_MAX)
      lockstep->streak++;
    return;
  }
  lockstep->streak = 0;
  if(!used)
    return;
  lockstep->mispredicted++;
  if(lockstep->trust_after < LINK_TRUST_AFTER_MAX)
    lockstep->trust_after *= 2u;
}

/* The peer's REPLY to one of our STARTs. Every reply scores the
   announcement made for it; only the one to the START still waiting (our
   latest) goes to the guest. */
static GbaSioEvent lockstep_reply(LinkCable *cable,
 const LinkNetMessage *message, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  uint32_t sequence;

  if(message->size < 9)
    return GBA_SIO_EVENT_NONE;
  sequence = get32(message->data);
  lockstep_check(lockstep, sequence, get32(message->data + 4),
   message->data[8]);
  if(!lockstep->waiting || sequence != lockstep->sequence)
    return GBA_SIO_EVENT_NONE;

  lockstep->waiting = 0;
  if(!gba_sio_is_waiting(cable->sio))
    return GBA_SIO_EVENT_NONE;
  return lockstep_finish(cable, get32(message->data + 4),
   message->data[8] != 0, cycle);
}

/* Whether the receiving side has an announcement pending, and in *at the
   cycle it falls due: LINK_ANNOUNCE_QUIET after the game's last write. */
static int lockstep_announce_due(const LinkCable *cable, uint32_t *at)
{
  const LinkLockstep *lockstep = &cable->lockstep;

  if(cable->mode != LINK_CABLE_MODE_LOCKSTEP || !lockstep->loaded ||
     !lockstep->anchored || lockstep->deferred ||
     !lockstep_is_receiver(cable))
    return 0;
  *at = lockstep->quiet_since + LINK_ANNOUNCE_QUIET;
  return 1;
}

/* Receiving side: once the game has loaded its next word after a transfer
   and left the serial registers alone for a moment, tells the clocking
   console the reply its next START would get. Most games do this right
   after a transfer, long before the next START arrives. Runs on every
   service; a busy port restarts the wait, and an unchanged reply is not
   announced twice. */
static void lockstep_announce(LinkCable *cable, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  uint32_t word = 0xFFFFFFFFu;
  uint32_t at;
  uint8_t message[9];
  uint8_t ready;

  if(cable->sio->outgoing_writes != lockstep->writes_seen)
  {
    lockstep->writes_seen = cable->sio->outgoing_writes;
    lockstep->loaded = 1;
    lockstep->quiet_since = cycle;
  }
  if(!lockstep_announce_due(cable, &at) || (int32_t)(cycle - at) < 0)
    return;
  if(gba_sio_is_busy(cable->sio))
  {
    lockstep->quiet_since = cycle;
    return;
  }
  lockstep->loaded = 0;
  ready = (uint8_t)gba_sio_remote_reply(cable->sio,
   (GbaSioMode)lockstep->applied_mode, &word);
  if(!ready)
    word = 0xFFFFFFFFu;
  if(lockstep->announced &&
     lockstep->announced_sequence == lockstep->applied_sequence &&
     lockstep->announced_ready == ready && lockstep->announced_word == word)
    return;

  lockstep->announced = 1;
  lockstep->announced_sequence = lockstep->applied_sequence;
  lockstep->announced_ready = ready;
  lockstep->announced_word = word;
  put32(message, lockstep->applied_sequence);
  put32(message + 4, word);
  message[8] = ready;
  link_cable_queue(cable, LINK_MSG_NEXT_REPLY, message, sizeof(message));
}

/* Returns 0 if the START must wait for a later guest cycle. */
static int lockstep_remote_start(LinkCable *cable,
 const LinkNetMessage *message, uint32_t cycle)
{
  LinkLockstep *lockstep = &cable->lockstep;
  GbaSioTransfer transfer;
  GbaSioResult result;
  uint8_t reply[9];
  uint32_t sender_cycle;
  uint32_t anchor;
  uint32_t word;
  int ready;

  if(message->size < 14)
    return 1;

  sender_cycle = get32(message->data + 4);
  word = get32(message->data + 8);
  anchor = cycle;

  /* Keep the spacing the other guest saw between two transfers, so this
     guest sees each one at the same point of its own frames as on a real
     cable and its interrupt handler has time to load the next word. Only
     after the link has been idle does a transfer start a new timeline. */
  if(lockstep->anchored)
  {
    uint32_t gap = sender_cycle - lockstep->peer_anchor;

    if(gap < LINK_LOCKSTEP_ACTIVE)
    {
      /* A transfer applied a little late must not shift the ones after
         it, or the delays add up until a frame goes without one. */
      anchor = lockstep->local_anchor + gap;
      if((int32_t)(cycle - anchor) < 0)
      {
        lockstep->deferred = 1;
        lockstep->deferred_until = anchor;
        return 0;
      }
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
  lockstep->local_anchor = anchor;
  lockstep->peer_anchor = sender_cycle;
  lockstep->last_transfer = cycle;
  lockstep->applied_sequence = get32(message->data);
  lockstep->applied_mode = message->data[12];
  lockstep->loaded = 0;
  lockstep->writes_seen = cable->sio->outgoing_writes;
  lockstep->transfers++;
  return 1;
}

/* ---- Pokemon mode ----------------------------------------------------- */

/* The master's handshake, or a slave's with its player id in bits 0-1. */
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

/* Moves our game's link layer to state and clears the old state's
   progress; the handshake count is kept only for HANDSHAKE. */
static void pokemon_set_local(LinkPokemon *pokemon, LinkPokemonState state)
{
  pokemon->local = state;
  pokemon->offset = 0;
  pokemon->handshake_repeats = 0;
  pokemon->pair_valid = 0;
  pokemon->keys = 0;
  pokemon->waiting = 0;
  if(state != LINK_POKEMON_HANDSHAKE)
    pokemon->handshake_count = 0;
}

/* A connected game that sends the slave handshake again and again has
   restarted its link layer; follow it back to the handshake. */
static void pokemon_watch_restart(LinkPokemon *pokemon, uint16_t word)
{
  if(word != POKEMON_SLAVE_HANDSHAKE)
    pokemon->handshake_repeats = 0;
  else if(++pokemon->handshake_repeats > POKEMON_RESTART_WORDS)
    pokemon_set_local(pokemon, LINK_POKEMON_HANDSHAKE);
}

/* Tells the peer whenever our game's link layer changes state. */
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
  if(cable->host && message->size >= 20)
    pokemon->answered = get32(message->data + 16);
  if(!cable->host)
    pokemon->pairs_seen++;
  pokemon->pair_valid = 1;
  pokemon->frames_received++;
  link_net_pop(&cable->net);
}

/* True when any of a frame's eight data words is set; an all-zero frame is
   only sent when the other side needs an answer. */
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
  pokemon->keys = (cable->host ? pokemon->local_words[0] :
   pokemon->pair[0][0]) == POKEMON_HELD_KEYS ||
   pokemon->pair[1][0] == POKEMON_HELD_KEYS;
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
  else if(pokemon_words_nonzero(pokemon->local_words) || pokemon->keys)
  {
    /* In the rooms the host waits for an answer to each of its frames,
       so the joiner answers even when it has nothing to say. */
    uint8_t message[20];

    for(i = 0; i < 8; i++)
      put16(message + i * 2, pokemon->local_words[i]);
    put32(message + 16, pokemon->pairs_seen);
    link_cable_queue(cable, LINK_MSG_POKEMON_WORDS, message,
     sizeof(message));
    pokemon->frames_sent++;
  }
}

/* Starts the guest's waiting multiplayer transfer now, delivering words as
   the four player slots (SIOMULTI0-3). */
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

/* In the Cable Club rooms both games send their held keys on every frame,
   and a game that receives nothing skips the frame but still sends. Each
   frame a console runs without the other's would leave one more frame
   queued between the two games for good, so the rooms would lag further
   behind after every stall or outage. There the consoles therefore work
   like a real cable: each one waits for the other's next frame before it
   starts its own. The host only goes ahead without one while the frames
   in the loop do not yet cover a round trip, which sets the delay. */
static int pokemon_must_wait(LinkCable *cable, uint64_t now_us)
{
  LinkPokemon *pokemon = &cable->pokemon;
  const LinkNetMessage *message = link_net_peek(&cable->net);
  uint8_t expected = cable->host ?
   LINK_MSG_POKEMON_WORDS : LINK_MSG_POKEMON_PAIR;

  if(pokemon->local != LINK_POKEMON_CONNECTED || pokemon->offset != 0 ||
     !pokemon->keys || pokemon->peer != LINK_POKEMON_CONNECTED ||
     (message != NULL && message->type == expected))
  {
    pokemon->waiting = 0;
    return 0;
  }

  if(cable->host)
  {
    uint32_t needed = cable->net.rtt_us / LINK_FRAME_US +
     POKEMON_LOOP_MARGIN;

    if(needed < POKEMON_LOOP_MIN)
      needed = POKEMON_LOOP_MIN;
    if(needed > POKEMON_LOOP_MAX)
      needed = POKEMON_LOOP_MAX;
    if(pokemon->frames_sent - pokemon->answered < needed)
    {
      pokemon->waiting = 0;
      return 0;
    }
  }

  if(!pokemon->waiting)
  {
    pokemon->waiting = 1;
    pokemon->wait_since_us = now_us;
  }
  /* A game that stopped answering for another reason must not hang the
     other console. */
  if(now_us - pokemon->wait_since_us >= POKEMON_WAIT_LIMIT_US)
  {
    pokemon->waiting = 0;
    return 0;
  }
  return 1;
}

/* 1 when a queued Pokemon frame message will never be used here: wrong
   mode or direction, the host's game not connected, or the joiner's closed. */
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

/* Pokemon mode's part of link_cable_service: follows the game's link
   layer, starts the host's transfers and clocks the joiner's. */
static GbaSioEvent pokemon_service(LinkCable *cable, uint32_t cycle,
 uint64_t now_us)
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

  /* A wait holds back one transfer and ends when it is no longer due. */
  if(cable->host && !sio->pending_start)
    pokemon->waiting = 0;

  if(sio->pending_start)
  {
    if(cable->host && sio->mode == GBA_SIO_MODE_MULTIPLAYER)
    {
      if(!pokemon_must_wait(cable, now_us) &&
         gba_sio_take_start(sio, &transfer))
        pokemon_host_transfer(cable, (uint16_t)transfer.outgoing, cycle);
    }
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
    {
      pokemon->transfer_scheduled = 0;
      pokemon->waiting = 0;
    }
    else if(!pokemon->transfer_scheduled)
    {
      pokemon->transfer_scheduled = 1;
      pokemon->next_transfer = cycle + POKEMON_HANDSHAKE_INTERVAL;
    }
    else if((int32_t)(cycle - pokemon->next_transfer) >= 0 &&
            !gba_sio_is_busy(sio) && !pokemon_must_wait(cable, now_us))
    {
      if(pokemon->offset == 0)
        pokemon->frame_start = cycle;
      pokemon_joiner_transfer(cable, cycle);
      pokemon->next_transfer = cycle +
       (pokemon->local == LINK_POKEMON_CONNECTED ?
        POKEMON_FRAME_INTERVAL : POKEMON_HANDSHAKE_INTERVAL);
      /* In the rooms the host sends one frame per video frame; starting
         frames faster would only make this console wait for them. */
      if(pokemon->offset == 0 && pokemon->keys &&
         (int32_t)(pokemon->frame_start + LINK_CYCLES_PER_FRAME -
          pokemon->next_transfer) > 0)
        pokemon->next_transfer = pokemon->frame_start +
         LINK_CYCLES_PER_FRAME;
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
        /* A deferred START stays at the head of the queue: BYE and every
           later message wait behind it. */
        if(cable->mode == LINK_CABLE_MODE_LOCKSTEP &&
           !lockstep_remote_start(cable, message, cycle))
          return events;
        break;

      case LINK_MSG_REPLY:
        if(cable->mode == LINK_CABLE_MODE_LOCKSTEP)
          events |= lockstep_reply(cable, message, cycle);
        break;

      case LINK_MSG_NEXT_REPLY:
        if(message->size >= 9)
        {
          cable->lockstep.predicted = 1;
          cable->lockstep.predicted_sequence = get32(message->data);
          cable->lockstep.predicted_word = get32(message->data + 4);
          cable->lockstep.predicted_ready = message->data[8];
        }
        break;

      case LINK_MSG_POKEMON_STATE:
        if(message->size >= 1 && message->data[0] <= LINK_POKEMON_CONNECTED)
          cable->pokemon.peer = (LinkPokemonState)message->data[0];
        break;

      case LINK_MSG_POKEMON_PAIR:
      case LINK_MSG_POKEMON_WORDS:
        if(!pokemon_drop_frame(cable, message->type))
          return events;
        if(!cable->host && message->type == LINK_MSG_POKEMON_PAIR)
          cable->pokemon.pairs_seen++;
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

/* A Pokemon link layer is open on either side, or a lockstep transfer
   happened in the last second. Only then do pacing and fast heartbeats
   apply. */
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
    events |= pokemon_service(cable, cycle, now_us);
  else
  {
    lockstep_publish_state(cable);
    if(cable->sio->pending_start)
      events |= lockstep_begin(cable, cycle);
    lockstep_announce(cable, cycle);
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
    link_net_set_heartbeat(&cable->net,
     cable->mode == LINK_CABLE_MODE_LOCKSTEP && !lockstep_is_receiver(cable) ?
     LINK_HEARTBEAT_LOCKSTEP_US : LINK_HEARTBEAT_ACTIVE_US);

  return events;
}

/* The console that cannot start transfers: the Multiplayer child, or a
   Normal-mode console running on the other one's clock. */
static int lockstep_is_receiver(const LinkCable *cable)
{
  const GbaSio *sio = cable->sio;

  if(sio->mode == GBA_SIO_MODE_MULTIPLAYER)
    return sio->player_id != 0;
  return (sio->mode == GBA_SIO_MODE_NORMAL_8 ||
   sio->mode == GBA_SIO_MODE_NORMAL_32) &&
   (sio->registers[GBA_SIO_REG_CONTROL >> 1] & LINK_SIO_CONTROL_CLOCK) == 0;
}

/* On a real cable the receiving console sees the sender's transfers at the
   pace the sender makes them. Here the sender waits a network round trip
   for every reply, so a receiver left free would run ahead and go whole
   frames without a transfer; Mario Kart: Super Circuit then reports a
   communication error. A receiving console therefore never runs past the
   sender's latest reported clock, mapped through the transfer it applied
   last; the sender reports its clock often while the link is in use (see
   LINK_HEARTBEAT_LOCKSTEP_US). Guessing further ahead is not safe: Mario
   Kart breaks on a single transfer applied late. The transfer it holds
   back is always within that limit, so the sender, which waits for our
   reply, is never kept waiting by it. Returns 0 when the console is not
   held this way. */
static int lockstep_receiver_limit(const LinkCable *cable, uint32_t cycle,
 uint32_t *limit)
{
  const LinkLockstep *lockstep = &cable->lockstep;
  int32_t peer_elapsed;

  if(cable->mode != LINK_CABLE_MODE_LOCKSTEP || !lockstep->anchored ||
     lockstep->deferred || !link_cable_in_use(cable, cycle) ||
     !lockstep_is_receiver(cable))
    return 0;
  peer_elapsed = (int32_t)(cable->net.peer_clock - lockstep->peer_anchor);
  if(peer_elapsed < 0)
    peer_elapsed = 0;
  *limit = lockstep->local_anchor + (uint32_t)peer_elapsed;
  return 1;
}

uint32_t link_cable_cycles_until_service(const LinkCable *cable,
 uint32_t cycle)
{
  uint32_t limit;
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
  if(lockstep_receiver_limit(cable, cycle, &limit))
  {
    uint32_t next = cycles_until(limit, cycle);

    if(next < cycles)
      cycles = next;
  }
  if(lockstep_announce_due(cable, &limit))
  {
    uint32_t next = cycles_until(limit, cycle);

    if(next < cycles)
      cycles = next;
  }
  return cycles;
}

int link_cable_due(const LinkCable *cable, uint32_t cycle)
{
  const LinkLockstep *lockstep = &cable->lockstep;
  uint32_t limit;

  if(cable->status != LINK_CABLE_CONNECTED)
    return 0;
  if(cable->mode == LINK_CABLE_MODE_LOCKSTEP)
    return (lockstep->waiting &&
     (int32_t)(cycle - lockstep->finish_cycle) >= 0) ||
     (lockstep->deferred &&
      (int32_t)(cycle - lockstep->deferred_until) >= 0) ||
     (lockstep_receiver_limit(cable, cycle, &limit) &&
      (int32_t)(cycle - limit) >= 0) ||
     (lockstep_announce_due(cable, &limit) &&
      (int32_t)(cycle - limit) >= 0);
  return !cable->host && cable->pokemon.transfer_scheduled &&
   (int32_t)(cycle - cable->pokemon.next_transfer) >= 0;
}

/* Pokemon mode does not pace on the guest clocks: a clock seen over Wi-Fi
   is tens of milliseconds old, and pacing on it made both consoles wait
   for each other on every bit of jitter and outage. The games wait for
   each other's frames in the rooms (see pokemon_must_wait); elsewhere a
   console only waits before its messages would overflow the channel, and
   the joiner never while a message is queued for it. */
static int pokemon_blocked(const LinkCable *cable)
{
  if(cable->pokemon.waiting)
    return 1;
  return link_net_backlog(&cable->net) > POKEMON_MAX_BACKLOG &&
   (cable->host || link_net_peek(&cable->net) == NULL);
}

int link_cable_blocked(const LinkCable *cable, uint32_t cycle)
{
  int32_t peer_elapsed;
  int32_t lead;
  uint32_t limit;

  if(cable->status != LINK_CABLE_CONNECTED)
    return 0;
  if(cable->mode == LINK_CABLE_MODE_POKEMON)
    return pokemon_blocked(cable);

  if(cable->mode == LINK_CABLE_MODE_LOCKSTEP && cable->lockstep.waiting &&
     gba_sio_is_waiting(cable->sio) &&
     (int32_t)(cycle - cable->lockstep.finish_cycle) >= 0)
    return 1;
  if(lockstep_receiver_limit(cable, cycle, &limit) &&
     (int32_t)(cycle - limit) >= 0)
    return 1;

  /* The other console is waiting for our reply to a START we hold back
     until our guest reaches its time. Pacing must let us get there, or
     both consoles would wait for each other. */
  if(!link_cable_in_use(cable, cycle) || cable->lockstep.deferred)
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
