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

#include "gba_sio.h"

#include <stddef.h>
#include <string.h>

#define GBA_SIO_CONTROL_START 0x0080u
#define GBA_SIO_CONTROL_IRQ   0x4000u
#define GBA_SIO_NORMAL_CLOCK  0x0001u
#define GBA_SIO_NORMAL_FAST   0x0002u
#define GBA_SIO_NORMAL_SI     0x0004u
#define GBA_SIO_MULTI_SLAVE   0x0004u
#define GBA_SIO_MULTI_READY   0x0008u
#define GBA_SIO_MULTI_ID      0x0030u
#define GBA_SIO_MULTI_ERROR   0x0040u

#define GBA_SIO_NORMAL_WRITABLE 0x708Bu
#define GBA_SIO_MULTI_WRITABLE  0x7083u

static uint16_t *gba_sio_register(GbaSio *sio, uint32_t address)
{
  return sio->registers + (address >> 1);
}

static const uint16_t *gba_sio_const_register(const GbaSio *sio,
 uint32_t address)
{
  return sio->registers + (address >> 1);
}

static GbaSioMode gba_sio_decode_mode(uint16_t control, uint16_t rcnt)
{
  if((rcnt & 0x8000u) == 0)
  {
    switch(control & 0x3000u)
    {
      case 0x0000:
        return GBA_SIO_MODE_NORMAL_8;

      case 0x1000:
        return GBA_SIO_MODE_NORMAL_32;

      case 0x2000:
        return GBA_SIO_MODE_MULTIPLAYER;

      default:
        return GBA_SIO_MODE_UART;
    }
  }

  if((rcnt & 0x4000u) != 0)
    return GBA_SIO_MODE_JOYBUS;

  return GBA_SIO_MODE_GPIO;
}

static int gba_sio_supported_mode(GbaSioMode mode)
{
  return mode == GBA_SIO_MODE_NORMAL_8 ||
   mode == GBA_SIO_MODE_NORMAL_32 ||
   mode == GBA_SIO_MODE_MULTIPLAYER;
}

static int gba_sio_transfer_busy(const GbaSio *sio)
{
  return sio->pending_start != 0 || sio->waiting != 0 || sio->active != 0;
}

static void gba_sio_clear_transient(GbaSio *sio)
{
  sio->pending_start = 0;
  sio->waiting = 0;
  sio->active = 0;
  sio->completion_error = 0;
  sio->cycles_remaining = 0;
  sio->outgoing = 0;
  memset(sio->incoming, 0, sizeof(sio->incoming));
}

static void gba_sio_refresh_control(GbaSio *sio)
{
  uint16_t control = *gba_sio_register(sio, GBA_SIO_REG_CONTROL);

  switch(sio->mode)
  {
    case GBA_SIO_MODE_NORMAL_8:
    case GBA_SIO_MODE_NORMAL_32:
      control &= GBA_SIO_NORMAL_WRITABLE;
      control |= GBA_SIO_NORMAL_SI;
      if(gba_sio_transfer_busy(sio))
        control |= GBA_SIO_CONTROL_START;
      break;

    case GBA_SIO_MODE_MULTIPLAYER:
      control &= GBA_SIO_MULTI_WRITABLE & ~GBA_SIO_CONTROL_START;

      if(sio->player_count == 0 || sio->player_id != 0)
        control |= GBA_SIO_MULTI_SLAVE;

      control |= GBA_SIO_MULTI_READY;
      control |= (uint16_t)((sio->player_id & 0x03u) << 4);

      if(sio->transfer_error != 0)
        control |= GBA_SIO_MULTI_ERROR;

      if(gba_sio_transfer_busy(sio))
        control |= GBA_SIO_CONTROL_START;
      break;

    case GBA_SIO_MODE_UART:
    case GBA_SIO_MODE_GPIO:
    case GBA_SIO_MODE_JOYBUS:
      break;
  }

  *gba_sio_register(sio, GBA_SIO_REG_CONTROL) = control;
}

static uint32_t gba_sio_read_outgoing(const GbaSio *sio)
{
  switch(sio->mode)
  {
    case GBA_SIO_MODE_NORMAL_8:
      return *gba_sio_const_register(sio, GBA_SIO_REG_SEND) & 0x00FFu;

    case GBA_SIO_MODE_NORMAL_32:
      return *gba_sio_const_register(sio, GBA_SIO_REG_DATA32_L) |
       ((uint32_t)*gba_sio_const_register(sio, GBA_SIO_REG_DATA32_H) << 16);

    case GBA_SIO_MODE_MULTIPLAYER:
      return *gba_sio_const_register(sio, GBA_SIO_REG_SEND);

    case GBA_SIO_MODE_UART:
    case GBA_SIO_MODE_GPIO:
    case GBA_SIO_MODE_JOYBUS:
      return 0;
  }

  return 0;
}

static void gba_sio_latch_transfer(GbaSio *sio, uint8_t setting,
 uint8_t internal_clock)
{
  uint32_t i;

  sio->pending_start = 1;
  sio->waiting = 0;
  sio->active = 0;
  sio->transfer_error = 0;
  sio->completion_error = 0;
  sio->transfer_mode = sio->mode;
  sio->transfer_setting = setting;
  sio->transfer_players = sio->player_count == 0 ? 1 : sio->player_count;
  sio->outgoing = gba_sio_read_outgoing(sio);

  if(internal_clock == 0)
    sio->pending_start = 0;

  if(sio->mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    for(i = 0; i < 4; i++)
      *gba_sio_register(sio, GBA_SIO_REG_MULTI0 + (i * 2)) = 0xFFFFu;
  }

  gba_sio_refresh_control(sio);
}

static void gba_sio_fill_transfer(const GbaSio *sio,
 GbaSioTransfer *transfer)
{
  transfer->mode = sio->transfer_mode;
  transfer->player_id = sio->player_id;
  transfer->player_count = sio->transfer_players;
  transfer->setting = sio->transfer_setting;
  transfer->internal_clock = sio->transfer_mode == GBA_SIO_MODE_MULTIPLAYER ?
   (sio->player_id == 0) :
   ((*gba_sio_const_register(sio, GBA_SIO_REG_CONTROL) &
     GBA_SIO_NORMAL_CLOCK) != 0);
  transfer->outgoing = sio->outgoing;
}

static GbaSioEvent gba_sio_finish(GbaSio *sio)
{
  uint16_t control = *gba_sio_register(sio, GBA_SIO_REG_CONTROL);
  GbaSioEvent event = GBA_SIO_EVENT_NONE;
  uint32_t i;

  switch(sio->transfer_mode)
  {
    case GBA_SIO_MODE_NORMAL_8:
      *gba_sio_register(sio, GBA_SIO_REG_SEND) =
       (uint16_t)(sio->incoming[0] & 0x00FFu);
      break;

    case GBA_SIO_MODE_NORMAL_32:
      *gba_sio_register(sio, GBA_SIO_REG_DATA32_L) =
       (uint16_t)sio->incoming[0];
      *gba_sio_register(sio, GBA_SIO_REG_DATA32_H) =
       (uint16_t)(sio->incoming[0] >> 16);
      break;

    case GBA_SIO_MODE_MULTIPLAYER:
      for(i = 0; i < 4; i++)
        *gba_sio_register(sio, GBA_SIO_REG_MULTI0 + (i * 2)) =
         (uint16_t)sio->incoming[i];
      break;

    case GBA_SIO_MODE_UART:
    case GBA_SIO_MODE_GPIO:
    case GBA_SIO_MODE_JOYBUS:
      break;
  }

  sio->transfer_error = sio->completion_error;
  sio->pending_start = 0;
  sio->waiting = 0;
  sio->active = 0;
  sio->cycles_remaining = 0;
  *gba_sio_register(sio, GBA_SIO_REG_CONTROL) =
   (uint16_t)(control & ~GBA_SIO_CONTROL_START);
  gba_sio_refresh_control(sio);

  if((control & GBA_SIO_CONTROL_IRQ) != 0)
    event = GBA_SIO_EVENT_SERIAL_IRQ;

  return event;
}

void gba_sio_init(GbaSio *sio, uint16_t *registers)
{
  memset(sio, 0, sizeof(*sio));
  sio->registers = registers;
  gba_sio_reset(sio);
}

void gba_sio_reset(GbaSio *sio)
{
  uint16_t *registers = sio->registers;

  memset(sio, 0, sizeof(*sio));
  sio->registers = registers;

  if(registers == NULL)
    return;

  *gba_sio_register(sio, GBA_SIO_REG_RCNT) = 0x800Fu;
  *gba_sio_register(sio, GBA_SIO_REG_CONTROL) = 0x0004u;
  sio->mode = gba_sio_decode_mode(
   *gba_sio_register(sio, GBA_SIO_REG_CONTROL),
   *gba_sio_register(sio, GBA_SIO_REG_RCNT));
}

GbaSioMode gba_sio_mode(const GbaSio *sio)
{
  return sio->mode;
}

void gba_sio_set_connection(GbaSio *sio, uint8_t player_id,
 uint8_t player_count)
{
  if(player_count < 2)
  {
    sio->player_id = 0;
    sio->player_count = 0;
  }
  else
  {
    if(player_count > 4)
      player_count = 4;
    if(player_id >= player_count)
      player_id = (uint8_t)(player_count - 1);

    sio->player_id = player_id;
    sio->player_count = player_count;
  }

  gba_sio_refresh_control(sio);
}

GbaSioEvent gba_sio_disconnect(GbaSio *sio)
{
  GbaSioEvent event = gba_sio_abort(sio);

  sio->player_id = 0;
  sio->player_count = 0;
  gba_sio_refresh_control(sio);
  return event;
}

static GbaSioEvent gba_sio_write_control(GbaSio *sio, uint16_t value)
{
  uint16_t old_control = *gba_sio_register(sio, GBA_SIO_REG_CONTROL);
  GbaSioMode old_mode = sio->mode;
  GbaSioMode new_mode = gba_sio_decode_mode(value,
   *gba_sio_register(sio, GBA_SIO_REG_RCNT));
  GbaSioEvent event = GBA_SIO_EVENT_NONE;
  int start_edge = 0;

  if(new_mode != old_mode)
  {
    gba_sio_clear_transient(sio);
    sio->transfer_error = 0;
    sio->mode = new_mode;
    event = GBA_SIO_EVENT_MODE_CHANGE;
  }

  switch(new_mode)
  {
    case GBA_SIO_MODE_NORMAL_8:
    case GBA_SIO_MODE_NORMAL_32:
      *gba_sio_register(sio, GBA_SIO_REG_CONTROL) =
       (uint16_t)((value & GBA_SIO_NORMAL_WRITABLE) |
                  GBA_SIO_NORMAL_SI);

      start_edge = (value & GBA_SIO_CONTROL_START) != 0 &&
       ((old_control & GBA_SIO_CONTROL_START) == 0 || old_mode != new_mode);

      if(start_edge && (value & GBA_SIO_NORMAL_CLOCK) != 0)
      {
        gba_sio_latch_transfer(sio,
         (uint8_t)((value & GBA_SIO_NORMAL_FAST) != 0), 1);
        event = (GbaSioEvent)(event | GBA_SIO_EVENT_START);
      }
      else
      {
        gba_sio_refresh_control(sio);
      }
      break;

    case GBA_SIO_MODE_MULTIPLAYER:
      *gba_sio_register(sio, GBA_SIO_REG_CONTROL) =
       (uint16_t)(value & GBA_SIO_MULTI_WRITABLE);

      start_edge = (value & GBA_SIO_CONTROL_START) != 0 &&
       ((old_control & GBA_SIO_CONTROL_START) == 0 || old_mode != new_mode);

      if(start_edge && sio->player_id == 0)
      {
        gba_sio_latch_transfer(sio, (uint8_t)(value & 0x0003u), 1);
        event = (GbaSioEvent)(event | GBA_SIO_EVENT_START);
      }
      else
      {
        gba_sio_refresh_control(sio);
      }
      break;

    case GBA_SIO_MODE_UART:
    case GBA_SIO_MODE_GPIO:
    case GBA_SIO_MODE_JOYBUS:
      *gba_sio_register(sio, GBA_SIO_REG_CONTROL) = value;
      break;
  }

  return event;
}

static GbaSioEvent gba_sio_write_rcnt(GbaSio *sio, uint16_t value)
{
  uint16_t old_rcnt = *gba_sio_register(sio, GBA_SIO_REG_RCNT);
  uint16_t rcnt = (uint16_t)((old_rcnt & 0x01FFu) | (value & 0xC000u));
  GbaSioMode new_mode = gba_sio_decode_mode(
   *gba_sio_register(sio, GBA_SIO_REG_CONTROL), rcnt);
  GbaSioEvent event = GBA_SIO_EVENT_NONE;

  if(new_mode == GBA_SIO_MODE_GPIO)
    rcnt = (uint16_t)((value & 0xC1FFu));
  else
    rcnt = (uint16_t)((rcnt & 0xC00Fu) | (value & 0x01F0u));

  *gba_sio_register(sio, GBA_SIO_REG_RCNT) = rcnt;

  if(new_mode != sio->mode)
  {
    gba_sio_clear_transient(sio);
    sio->transfer_error = 0;
    sio->mode = new_mode;
    event = GBA_SIO_EVENT_MODE_CHANGE;
  }

  gba_sio_refresh_control(sio);
  return event;
}

GbaSioEvent gba_sio_write16(GbaSio *sio, uint32_t address,
 uint16_t value)
{
  if(sio == NULL || sio->registers == NULL ||
     address >= GBA_SIO_REGISTER_BYTES || (address & 1u) != 0)
    return GBA_SIO_EVENT_NONE;

  switch(address)
  {
    case GBA_SIO_REG_CONTROL:
      return gba_sio_write_control(sio, value);

    case GBA_SIO_REG_RCNT:
      return gba_sio_write_rcnt(sio, value);

    case GBA_SIO_REG_DATA32_L:
    case GBA_SIO_REG_DATA32_H:
      if(sio->mode == GBA_SIO_MODE_NORMAL_32 ||
         !gba_sio_supported_mode(sio->mode))
        *gba_sio_register(sio, address) = value;
      break;

    case GBA_SIO_REG_MULTI2:
    case GBA_SIO_REG_MULTI3:
      if(!gba_sio_supported_mode(sio->mode))
        *gba_sio_register(sio, address) = value;
      break;

    case GBA_SIO_REG_SEND:
      if(sio->mode == GBA_SIO_MODE_NORMAL_8)
        *gba_sio_register(sio, address) = (uint16_t)(value & 0x00FFu);
      else
        *gba_sio_register(sio, address) = value;
      break;

    default:
      *gba_sio_register(sio, address) = value;
      break;
  }

  return GBA_SIO_EVENT_NONE;
}

GbaSioEvent gba_sio_write32(GbaSio *sio, uint32_t address,
 uint32_t value)
{
  GbaSioEvent low;
  GbaSioEvent high;

  if(address == GBA_SIO_REG_DATA32_L &&
     sio != NULL && sio->mode == GBA_SIO_MODE_NORMAL_32)
  {
    *gba_sio_register(sio, GBA_SIO_REG_DATA32_L) = (uint16_t)value;
    *gba_sio_register(sio, GBA_SIO_REG_DATA32_H) = (uint16_t)(value >> 16);
    return GBA_SIO_EVENT_NONE;
  }

  low = gba_sio_write16(sio, address, (uint16_t)value);
  high = gba_sio_write16(sio, address + 2, (uint16_t)(value >> 16));
  return (GbaSioEvent)(low | high);
}

int gba_sio_take_start(GbaSio *sio, GbaSioTransfer *transfer)
{
  if(sio == NULL || transfer == NULL || sio->pending_start == 0)
    return 0;

  sio->pending_start = 0;
  sio->waiting = 1;
  gba_sio_refresh_control(sio);
  gba_sio_fill_transfer(sio, transfer);
  return 1;
}

int gba_sio_latch_remote_start(GbaSio *sio, GbaSioMode mode,
 uint8_t setting, GbaSioTransfer *transfer)
{
  uint16_t control;

  if(sio == NULL || transfer == NULL || sio->mode != mode ||
     !gba_sio_supported_mode(mode) || gba_sio_transfer_busy(sio))
    return 0;

  control = *gba_sio_register(sio, GBA_SIO_REG_CONTROL);

  if(mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    if(sio->player_count < 2 || sio->player_id == 0)
      return 0;
    setting &= 0x03u;
  }
  else
  {
    if((control & GBA_SIO_NORMAL_CLOCK) != 0 ||
       (control & GBA_SIO_CONTROL_START) == 0)
      return 0;
    setting = setting != 0;
  }

  gba_sio_latch_transfer(sio, setting, 0);
  sio->waiting = 1;
  gba_sio_refresh_control(sio);
  gba_sio_fill_transfer(sio, transfer);
  return 1;
}

uint32_t gba_sio_transfer_cycles(GbaSioMode mode, uint8_t setting,
 uint8_t player_count)
{
  static const uint32_t multiplayer_cycles[4][4] =
  {
    { 31976, 63427, 94884, 125829 },
    {  8378, 16241, 24104,  31457 },
    {  5750, 10998, 16241,  20972 },
    {  3140,  5755,  8376,  10486 }
  };

  switch(mode)
  {
    case GBA_SIO_MODE_NORMAL_8:
      return setting != 0 ? 64u : 512u;

    case GBA_SIO_MODE_NORMAL_32:
      return setting != 0 ? 256u : 2048u;

    case GBA_SIO_MODE_MULTIPLAYER:
      if(player_count == 0)
        player_count = 1;
      if(player_count > 4)
        player_count = 4;
      return multiplayer_cycles[setting & 0x03u][player_count - 1];

    case GBA_SIO_MODE_UART:
    case GBA_SIO_MODE_GPIO:
    case GBA_SIO_MODE_JOYBUS:
      return 0;
  }

  return 0;
}

int gba_sio_commit(GbaSio *sio, const GbaSioResult *result)
{
  uint8_t player_count;

  if(sio == NULL || result == NULL || sio->waiting == 0)
    return 0;

  memcpy(sio->incoming, result->data, sizeof(sio->incoming));
  sio->completion_error = result->error != 0;
  player_count = result->player_count;
  if(player_count == 0)
    player_count = sio->transfer_players;
  if(player_count > 4)
    player_count = 4;
  sio->transfer_players = player_count;
  sio->waiting = 0;
  sio->active = 1;
  sio->cycles_remaining = gba_sio_transfer_cycles(sio->transfer_mode,
   sio->transfer_setting, sio->transfer_players);
  gba_sio_refresh_control(sio);

  if(sio->cycles_remaining == 0)
    gba_sio_finish(sio);

  return 1;
}

GbaSioEvent gba_sio_abort(GbaSio *sio)
{
  uint32_t i;

  if(sio == NULL || !gba_sio_transfer_busy(sio))
    return GBA_SIO_EVENT_NONE;

  sio->completion_error = 1;
  if(sio->transfer_mode == GBA_SIO_MODE_NORMAL_8)
    sio->incoming[0] = 0xFFu;
  else if(sio->transfer_mode == GBA_SIO_MODE_NORMAL_32)
    sio->incoming[0] = 0xFFFFFFFFu;
  else
  {
    for(i = 0; i < 4; i++)
      sio->incoming[i] = 0xFFFFu;
  }

  return gba_sio_finish(sio);
}

GbaSioEvent gba_sio_advance(GbaSio *sio, uint32_t cycles)
{
  if(sio == NULL || sio->active == 0)
    return GBA_SIO_EVENT_NONE;

  if(cycles < sio->cycles_remaining)
  {
    sio->cycles_remaining -= cycles;
    return GBA_SIO_EVENT_NONE;
  }

  return gba_sio_finish(sio);
}

uint32_t gba_sio_next_event(const GbaSio *sio)
{
  if(sio == NULL || sio->active == 0)
    return GBA_SIO_NO_EVENT;

  return sio->cycles_remaining;
}

int gba_sio_is_waiting(const GbaSio *sio)
{
  return sio != NULL && sio->waiting != 0;
}

void gba_sio_save_snapshot(const GbaSio *sio, GbaSioSnapshot *snapshot)
{
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->magic = GBA_SIO_SNAPSHOT_MAGIC;
  snapshot->version = GBA_SIO_SNAPSHOT_VERSION;
  snapshot->size = (uint16_t)sizeof(*snapshot);
  snapshot->mode = (uint8_t)sio->mode;
  snapshot->pending_start = sio->pending_start;
  snapshot->waiting = sio->waiting;
  snapshot->active = sio->active;
  snapshot->transfer_error = sio->transfer_error;
  snapshot->completion_error = sio->completion_error;
  snapshot->transfer_mode = (uint8_t)sio->transfer_mode;
  snapshot->transfer_setting = sio->transfer_setting;
  snapshot->transfer_players = sio->transfer_players;
  snapshot->cycles_remaining = sio->cycles_remaining;
  snapshot->outgoing = sio->outgoing;
  memcpy(snapshot->incoming, sio->incoming, sizeof(snapshot->incoming));
}

int gba_sio_load_snapshot(GbaSio *sio, const GbaSioSnapshot *snapshot)
{
  GbaSioMode register_mode;

  if(sio == NULL || snapshot == NULL ||
     snapshot->magic != GBA_SIO_SNAPSHOT_MAGIC ||
     snapshot->version != GBA_SIO_SNAPSHOT_VERSION ||
     snapshot->size != sizeof(*snapshot) ||
     snapshot->mode > GBA_SIO_MODE_JOYBUS ||
     snapshot->transfer_mode > GBA_SIO_MODE_JOYBUS ||
     snapshot->transfer_players > 4)
  {
    if(sio != NULL && sio->registers != NULL)
    {
      gba_sio_clear_transient(sio);
      sio->player_id = 0;
      sio->player_count = 0;
      sio->transfer_error = 0;
      sio->mode = gba_sio_decode_mode(
       *gba_sio_register(sio, GBA_SIO_REG_CONTROL),
       *gba_sio_register(sio, GBA_SIO_REG_RCNT));
      *gba_sio_register(sio, GBA_SIO_REG_CONTROL) &=
       (uint16_t)~GBA_SIO_CONTROL_START;
      gba_sio_refresh_control(sio);
    }
    return 0;
  }

  register_mode = gba_sio_decode_mode(
   *gba_sio_register(sio, GBA_SIO_REG_CONTROL),
   *gba_sio_register(sio, GBA_SIO_REG_RCNT));
  if(register_mode != (GbaSioMode)snapshot->mode)
    return 0;

  sio->mode = (GbaSioMode)snapshot->mode;
  sio->player_id = 0;
  sio->player_count = 0;
  sio->pending_start = snapshot->pending_start != 0;
  sio->waiting = snapshot->waiting != 0;
  sio->active = snapshot->active != 0;
  sio->transfer_error = snapshot->transfer_error != 0;
  sio->completion_error = snapshot->completion_error != 0;
  sio->transfer_mode = (GbaSioMode)snapshot->transfer_mode;
  sio->transfer_setting = snapshot->transfer_setting;
  sio->transfer_players = snapshot->transfer_players;
  sio->cycles_remaining = snapshot->cycles_remaining;
  sio->outgoing = snapshot->outgoing;
  memcpy(sio->incoming, snapshot->incoming, sizeof(sio->incoming));

  if((sio->pending_start + sio->waiting + sio->active) > 1 ||
     (sio->active != 0 && sio->cycles_remaining == 0) ||
     (gba_sio_transfer_busy(sio) && sio->transfer_players == 0) ||
     !gba_sio_supported_mode(sio->transfer_mode))
  {
    gba_sio_clear_transient(sio);
    sio->transfer_error = 0;
    gba_sio_refresh_control(sio);
    return 0;
  }

  gba_sio_refresh_control(sio);
  return 1;
}
