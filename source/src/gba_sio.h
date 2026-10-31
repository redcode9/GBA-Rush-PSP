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
 * The GBA serial port (SIOCNT, SIODATA, SIOMULTI, RCNT) as seen by the game.
 * memory.c forwards register writes here and update_gba advances running
 * transfers; link_cable.c takes the transfers the game starts and supplies
 * what the other console sent. Normal 8/32-bit and Multiplayer modes are
 * emulated; in UART, GPIO and JOY Bus modes the registers are only stored.
 */

#ifndef GBA_SIO_H
#define GBA_SIO_H

#include <stdint.h>

/* Size of the I/O register bank the port works on. */
#define GBA_SIO_REGISTER_BYTES 0x400u
/* gba_sio_next_event: no transfer is running. */
#define GBA_SIO_NO_EVENT       UINT32_MAX

/* Register offsets in the I/O bank. SEND is SIOMLT_SEND / SIODATA8. */
enum
{
  GBA_SIO_REG_DATA32_L = 0x120,
  GBA_SIO_REG_MULTI0   = 0x120,
  GBA_SIO_REG_DATA32_H = 0x122,
  GBA_SIO_REG_MULTI1   = 0x122,
  GBA_SIO_REG_MULTI2   = 0x124,
  GBA_SIO_REG_MULTI3   = 0x126,
  GBA_SIO_REG_CONTROL  = 0x128,
  GBA_SIO_REG_SEND     = 0x12A,
  GBA_SIO_REG_RCNT     = 0x134
};

typedef enum
{
  GBA_SIO_MODE_NORMAL_8,
  GBA_SIO_MODE_NORMAL_32,
  GBA_SIO_MODE_MULTIPLAYER,
  GBA_SIO_MODE_UART,
  GBA_SIO_MODE_GPIO,
  GBA_SIO_MODE_JOYBUS
} GbaSioMode;

typedef enum
{
  GBA_SIO_EVENT_NONE        = 0,
  GBA_SIO_EVENT_START       = 1u << 0,
  GBA_SIO_EVENT_SERIAL_IRQ  = 1u << 1,
  GBA_SIO_EVENT_MODE_CHANGE = 1u << 2
} GbaSioEvent;

/* A transfer the game started. setting is the baud rate index in
   Multiplayer mode and the 2 MHz flag in Normal mode; internal_clock means
   this console drives the clock. */
typedef struct
{
  GbaSioMode mode;
  uint8_t player_id;
  uint8_t player_count;
  uint8_t setting;
  uint8_t internal_clock;
  uint32_t outgoing;
} GbaSioTransfer;

/* What a transfer received: one word per player in Multiplayer mode,
   data[0] otherwise. error sets the Multiplayer error bit. */
typedef struct
{
  uint32_t data[4];
  uint8_t player_count;
  uint8_t error;
} GbaSioResult;

/* Savestate form of GbaSio ("SIO1"); its layout is part of the file. */
#define GBA_SIO_SNAPSHOT_MAGIC   0x53494F31u
#define GBA_SIO_SNAPSHOT_VERSION 1u

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint8_t mode;
  uint8_t pending_start;
  uint8_t waiting;
  uint8_t active;
  uint8_t transfer_error;
  uint8_t completion_error;
  uint8_t transfer_mode;
  uint8_t transfer_setting;
  uint8_t transfer_players;
  uint8_t decision_locked;
  uint8_t reserved[2];
  uint32_t cycles_remaining;
  uint32_t outgoing;
  uint32_t incoming[4];
} GbaSioSnapshot;

typedef struct
{
  uint16_t *registers;
  GbaSioMode mode;
  /* The other console's port, as last reported by the link. */
  GbaSioMode peer_mode;
  uint8_t player_id;
  uint8_t player_count;
  uint8_t peer_active;
  /* A transfer moves through pending_start (started by this game as clock
     master, not yet taken by the link), waiting (taken, result not yet
     known) and active (result known, completes after cycles_remaining).
     decision_locked: the link committed to it, so mode changes and new
     starts no longer cancel it. */
  uint8_t pending_start;
  uint8_t waiting;
  uint8_t active;
  uint8_t decision_locked;
  uint8_t transfer_error;
  uint8_t completion_error;
  GbaSioMode transfer_mode;
  uint8_t transfer_setting;
  uint8_t transfer_players;
  /* Guest cycle the port has been advanced to. */
  uint32_t current_cycle;
  uint32_t transfer_start_cycle;
  uint32_t transfer_finish_cycle;
  uint32_t cycles_remaining;
  uint32_t outgoing;
  uint32_t incoming[4];
  uint16_t peer_control;
  uint16_t peer_rcnt;
  /* Counts SIOCNT writes with the serial interrupt bit clear, even when it
     was already off. Games write those when they close or reopen their
     link layer. */
  uint32_t interrupt_disables;
} GbaSio;

void gba_sio_init(GbaSio *sio, uint16_t *registers);
/* Bind cold SIO once; preserve an existing binding and all live state.
 * Reject missing or conflicting storage without modifying either bank. */
int gba_sio_prepare(GbaSio *sio, uint16_t *registers);
void gba_sio_reset(GbaSio *sio);

GbaSioMode gba_sio_mode(const GbaSio *sio);
void gba_sio_set_connection(GbaSio *sio, uint8_t player_id,
 uint8_t player_count);
void gba_sio_set_peer_state(GbaSio *sio, GbaSioMode mode,
 uint16_t control, uint16_t rcnt, int active);
GbaSioEvent gba_sio_disconnect(GbaSio *sio);

GbaSioEvent gba_sio_write16(GbaSio *sio, uint32_t address,
 uint16_t value);
GbaSioEvent gba_sio_write32(GbaSio *sio, uint32_t address,
 uint32_t value);

/* Link side. take_start moves a pending transfer to waiting; peek_start
   only reads it. latch_remote_start starts a transfer here because the other
   console clocked one (Normal mode external clock, or a Multiplayer child). */
int gba_sio_take_start(GbaSio *sio, GbaSioTransfer *transfer);
int gba_sio_peek_start(const GbaSio *sio, GbaSioTransfer *transfer);
int gba_sio_latch_remote_start(GbaSio *sio, GbaSioMode mode,
 uint8_t setting, GbaSioTransfer *transfer);
int gba_sio_can_commit(const GbaSio *sio);
int gba_sio_lock_decision(GbaSio *sio);
/* Tells the port the current guest cycle; while a transfer is active only
   the cycle it already has is accepted. */
int gba_sio_set_cycle(GbaSio *sio, uint32_t cycle);
/* Deliver the result of a waiting transfer that runs from start_cycle to
   finish_cycle, a span that must equal the hardware duration. commit_at
   needs start_cycle == the current cycle. commit_at_horizon also accepts a
   start in the past and, if finish_cycle has passed too, completes at once
   and returns the event. commit starts the transfer now. */
int gba_sio_commit_at(GbaSio *sio, const GbaSioResult *result,
 uint32_t start_cycle, uint32_t finish_cycle);
int gba_sio_commit_at_horizon(GbaSio *sio, const GbaSioResult *result,
 uint32_t start_cycle, uint32_t finish_cycle, GbaSioEvent *event);
int gba_sio_commit(GbaSio *sio, const GbaSioResult *result);
/* cancel drops a transfer without an IRQ; abort completes it as failed
   (receive registers all ones, IRQ if enabled). */
GbaSioEvent gba_sio_cancel(GbaSio *sio);
GbaSioEvent gba_sio_abort(GbaSio *sio);
/* Runs an active transfer for cycles; may complete it. */
GbaSioEvent gba_sio_advance(GbaSio *sio, uint32_t cycles);
uint32_t gba_sio_next_event(const GbaSio *sio);
int gba_sio_is_waiting(const GbaSio *sio);
int gba_sio_is_busy(const GbaSio *sio);

/* Hardware duration of a transfer in CPU cycles; 0 if not emulated. */
uint32_t gba_sio_transfer_cycles(GbaSioMode mode, uint8_t setting,
 uint8_t player_count);

void gba_sio_save_snapshot(const GbaSio *sio, GbaSioSnapshot *snapshot);
int gba_sio_load_snapshot(GbaSio *sio, const GbaSioSnapshot *snapshot);

#endif /* GBA_SIO_H */
