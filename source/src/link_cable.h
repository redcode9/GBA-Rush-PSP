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

/*
 * Emulated GBA link cable between two consoles.
 *
 * A real cable exchanges one 16-bit word every few hundred microseconds,
 * while a Wi-Fi round trip takes several milliseconds. Waiting for the
 * network on every word is correct but slow, so the cable picks one of two
 * techniques once both consoles have said which game they run:
 *
 * Pokemon mode (Ruby, Sapphire, Emerald, FireRed, LeafGreen in any mix).
 *   These games wrap the cable in their own link layer: every frame the
 *   master sends one checksum word and eight data words, and an all-zero
 *   frame means "nothing to say". Each console therefore emulates the wire
 *   locally, at full speed, and only the non-empty frames cross the
 *   network. The host decides how its frames pair with the joiner's and
 *   sends the joiner the exact pair, so both games see identical data, as
 *   they would on a real cable. The checksum word is filled in locally.
 *   In the Cable Club rooms, where both games send keys on every frame,
 *   each console waits for the other's frames so the delay stays fixed.
 *
 * Lockstep mode (every other game).
 *   Each transfer is sent to the other console, and the console that
 *   clocks the transfer waits for the reply before the guest can see the
 *   result. The guest keeps running while the transfer would still be in
 *   flight on real hardware, which hides part of the round trip. The
 *   receiving console never runs past the clocking console's guest clock.
 *   Most games load the word for the next transfer right after the last
 *   one, so the receiving console announces that word in advance; once its
 *   announcements have matched its replies for long enough, the clocking
 *   console completes transfers with them and no longer waits.
 *
 * The cable owns no platform resources: the caller moves datagrams between
 * link_cable_receive / link_cable_build and the network.
 */

#ifndef LINK_CABLE_H
#define LINK_CABLE_H

#include <stddef.h>
#include <stdint.h>

#include "gba_sio.h"
#include "link_net.h"

/* Sent in HELLO; a peer with another value is INCOMPATIBLE. Bump it when a
   message changes or a new one must be understood. A new message the peer
   may ignore needs no bump: older peers drop unknown types. */
#define LINK_CABLE_PROTOCOL 2u

typedef enum
{
  LINK_CABLE_OFF,
  LINK_CABLE_WAITING,       /* network up, waiting for the peer's hello */
  LINK_CABLE_CONNECTED,
  LINK_CABLE_INCOMPATIBLE,  /* the peer runs another link protocol */
  LINK_CABLE_PEER_LEFT,     /* the peer disconnected on purpose */
  LINK_CABLE_LOST           /* the peer stopped answering */
} LinkCableStatus;

typedef enum
{
  LINK_CABLE_MODE_NONE,
  LINK_CABLE_MODE_LOCKSTEP,
  LINK_CABLE_MODE_POKEMON
} LinkCableMode;

/* Where a Pokemon game's link layer is, as observed on the wire. */
typedef enum
{
  LINK_POKEMON_CLOSED,
  LINK_POKEMON_HANDSHAKE,
  LINK_POKEMON_CONNECTED
} LinkPokemonState;

typedef struct
{
  LinkPokemonState local;
  LinkPokemonState peer;      /* applied in the order the peer sent it */
  LinkPokemonState reported;  /* last local state sent to the peer */
  uint8_t offset;             /* 0 = checksum word, 1..8 = data words */
  uint8_t handshake_count;    /* the game's previous handshake count */
  uint8_t handshake_repeats;  /* handshake words seen while connected */
  uint8_t pair_valid;
  uint8_t transfer_scheduled;
  uint8_t keys;               /* the last frame carried held keys: a room */
  uint8_t waiting;            /* holding our next frame for the other's */
  uint16_t local_words[8];
  uint16_t pair[2][8];        /* words delivered this frame, per player */
  uint32_t next_transfer;     /* joiner: cycle of the next local transfer */
  uint32_t frame_start;       /* cycle of this frame's first word */
  uint32_t pairs_seen;        /* joiner: host frames taken or dropped */
  /* Joiner: the host's cycle at the last host frame taken and ours when
     it was taken, once one has been (see pokemon_pair_early). */
  uint8_t pair_spaced;
  uint32_t pair_sent;
  uint32_t pair_taken;
  uint32_t answered;          /* host: pairs_seen in the joiner's last frame */
  uint64_t wait_since_us;
  uint32_t interrupt_disables;
  uint32_t frames_sent;
  uint32_t frames_received;
} LinkPokemon;

typedef struct
{
  uint32_t sequence;          /* number of the latest START we sent */
  uint32_t start_cycle;       /* our cycle when that transfer started */
  uint32_t finish_cycle;      /* when it would finish on real hardware */
  uint32_t outgoing;          /* our word in that transfer */
  uint8_t waiting;            /* a START is out and its reply is missing */
  uint8_t anchored;           /* local_anchor and peer_anchor are set */
  uint8_t deferred;           /* a received START waits for deferred_until */
  uint32_t deferred_until;
  uint32_t local_anchor;      /* our cycle when the last START was applied */
  uint32_t peer_anchor;       /* the sender's cycle carried by that START */
  uint32_t last_transfer;     /* our cycle at the latest transfer */
  uint8_t state_mode;         /* mode and SIOCNT bit 3 in the last */
  uint16_t state_control;     /* SIO_STATE we sent, if state_sent */
  uint8_t state_sent;         /* (see lockstep_publish_state) */
  uint32_t transfers;         /* transfers clocked or answered here */

  /* Receiving side: the START applied last, whether the game has loaded
     its next word since (and when it last touched the registers), and the
     reply announced for the next START. */
  uint32_t applied_sequence;
  uint8_t applied_mode;
  /* How far past the START applied last this console may run without word
   from the clocking console: learned from how far apart that console's
   STARTs come, 0 while not trusted. */
  uint32_t lookahead;
  uint32_t lookahead_after;   /* STARTs in a row needed to trust it */
  uint32_t spacing_run;
  uint32_t spacing_min;       /* closest pair of STARTs in that run */
  uint32_t late_starts;       /* applied after this console passed them */
  uint8_t bursting;           /* that START came close after the last */
  uint8_t loaded;
  uint32_t writes_seen;
  uint32_t quiet_since;
  uint8_t announced;
  uint8_t announced_ready;
  uint32_t announced_sequence;
  uint32_t announced_word;

  /* Clocking side: the other console's latest announcement (predicted_*,
     LINK_MSG_NEXT_REPLY in link_cable.c), the one used for the transfer
     whose reply is still to come (verify_*), and how far announcements are
     trusted. */
  uint8_t predicted;
  uint8_t predicted_ready;
  uint32_t predicted_sequence;
  uint32_t predicted_word;
  uint8_t start_mode;         /* mode of the START we sent last */
  uint8_t verify_used;        /* a transfer was finished with it */
  uint8_t verify_ready;
  uint32_t verify_sequence;
  uint32_t verify_word;
  uint32_t streak;            /* announcements in a row that were right */
  uint32_t trust_after;       /* streak needed before using them */
  uint32_t answered_early;    /* transfers completed from an announcement */
  uint32_t mispredicted;      /* ...of which the reply proved wrong */
} LinkLockstep;

typedef struct
{
  GbaSio *sio;
  LinkNet net;
  LinkCableStatus status;
  LinkCableMode mode;
  uint8_t host;
  uint8_t hello_received;
  uint8_t bye_queued;
  char local_game[4];
  char peer_game[4];
  LinkPokemon pokemon;
  LinkLockstep lockstep;
  /* Guest clocks of both consoles when the link was last idle. */
  uint32_t pacing_local;
  uint32_t pacing_peer;
} LinkCable;

/* Plugs the cable into `sio` and queues HELLO. One console hosts, and the
   two must use different local_id values. */
void link_cable_start(LinkCable *cable, GbaSio *sio, int host,
 const char game_code[4], uint32_t local_id, uint64_t now_us);

/* Queues a goodbye for the peer; the next datagram carries it. */
void link_cable_leave(LinkCable *cable);

/* Runs the cable at guest cycle `cycle`. Returns SIO events to deliver. */
GbaSioEvent link_cable_service(LinkCable *cable, uint32_t cycle,
 uint64_t now_us);

/* Guest cycles until the cable needs link_cable_service again, and whether
   that moment has come. */
uint32_t link_cable_cycles_until_service(const LinkCable *cable,
 uint32_t cycle);
int link_cable_due(const LinkCable *cable, uint32_t cycle);

/* True while the guest must not run: a reply or the other console's frame
   is due, or the guest is too far ahead of the other console. */
int link_cable_blocked(const LinkCable *cable, uint32_t cycle);

/* Datagrams from and to the network; see link_net.h for the format. */
int link_cable_receive(LinkCable *cable, const uint8_t *datagram,
 size_t size, uint64_t now_us);
size_t link_cable_build(LinkCable *cable, uint8_t *datagram,
 size_t capacity, uint32_t cycle, uint64_t now_us);

int link_cable_is_pokemon_game(const char game_code[4]);

#endif /* LINK_CABLE_H */
