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

#include "adhoc_resync.h"

#include <stddef.h>
#include <string.h>

#define ADHOC_RESYNC_REQUEST_MIN_RETRY_US  50000u
#define ADHOC_RESYNC_REQUEST_WINDOW_US    500000u
#define ADHOC_RESYNC_HOST_RETRY_US          97000u
#define ADHOC_RESYNC_JOIN_INITIAL_US         25000u
#define ADHOC_RESYNC_JOIN_RETRY_US          137000u
#define ADHOC_RESYNC_PROBE_MAGIC         0x47425244u
#define ADHOC_RESYNC_PROBE_VERSION                1u

static void adhoc_resync_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8);
  output[1] = (uint8_t)value;
}

static void adhoc_resync_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static uint16_t adhoc_resync_read_u16(const uint8_t *input)
{
  return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t adhoc_resync_read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) |
   ((uint32_t)input[1] << 16) |
   ((uint32_t)input[2] << 8) |
   input[3];
}

static uint32_t adhoc_resync_next_epoch(uint32_t epoch)
{
  epoch++;
  return epoch == 0 ? 1u : epoch;
}

void adhoc_resync_init(AdhocResync *resync, AdhocResyncRole role,
 uint32_t current_epoch, uint32_t recovery_id)
{
  if(resync == NULL)
    return;
  memset(resync, 0, sizeof(*resync));
  resync->role = role;
  resync->current_epoch = current_epoch;
  resync->target_epoch = role == ADHOC_RESYNC_HOST ?
   adhoc_resync_next_epoch(current_epoch) : current_epoch;
  resync->recovery_id = recovery_id;
  resync->state = role == ADHOC_RESYNC_HOST ?
   ADHOC_RESYNC_WAIT_READY : ADHOC_RESYNC_WAIT_BEGIN;
}

int adhoc_resync_request_matches_epoch(uint32_t current_epoch,
 uint32_t request_epoch)
{
  return current_epoch != 0 && request_epoch == current_epoch;
}

void adhoc_resync_request_gate_init(AdhocResyncRequestGate *gate)
{
  if(gate != NULL)
    memset(gate, 0, sizeof(*gate));
}

void adhoc_resync_request_gate_settle(AdhocResyncRequestGate *gate,
 uint64_t now_us, uint64_t duration_us)
{
  if(gate == NULL)
    return;
  adhoc_resync_request_gate_init(gate);
  gate->settled_until_us = UINT64_MAX - now_us < duration_us ?
   UINT64_MAX : now_us + duration_us;
}

AdhocResyncRequestResult adhoc_resync_request_gate_note(
 AdhocResyncRequestGate *gate, uint32_t current_epoch,
 uint32_t request_epoch, uint32_t recovery_id, uint64_t now_us)
{
  if(gate == NULL || recovery_id == 0 ||
     !adhoc_resync_request_matches_epoch(current_epoch, request_epoch) ||
     now_us < gate->settled_until_us)
    return ADHOC_RESYNC_REQUEST_IGNORED;

  if(gate->epoch != request_epoch ||
     gate->recovery_id != recovery_id || now_us < gate->last_us ||
     now_us - gate->first_us > ADHOC_RESYNC_REQUEST_WINDOW_US)
  {
    gate->epoch = request_epoch;
    gate->recovery_id = recovery_id;
    gate->first_us = now_us;
    gate->last_us = now_us;
    gate->confirmations = 1;
    return ADHOC_RESYNC_REQUEST_PENDING;
  }

  if(now_us - gate->last_us < ADHOC_RESYNC_REQUEST_MIN_RETRY_US)
    return ADHOC_RESYNC_REQUEST_IGNORED;
  gate->last_us = now_us;
  if(gate->confirmations != UINT32_MAX)
    gate->confirmations++;
  if(gate->confirmations < 2u)
    return ADHOC_RESYNC_REQUEST_PENDING;
  adhoc_resync_request_gate_init(gate);
  return ADHOC_RESYNC_REQUEST_ACCEPTED;
}

int adhoc_resync_outbound(const AdhocResync *resync,
 AdhocRendezvousKind *kind, uint32_t *epoch, uint32_t *recovery_id)
{
  if(resync == NULL || kind == NULL || epoch == NULL ||
     recovery_id == NULL || resync->target_epoch == 0 ||
     resync->recovery_id == 0)
    return 0;

  switch(resync->state)
  {
    case ADHOC_RESYNC_WAIT_BEGIN:
      *kind = ADHOC_RENDEZVOUS_RESYNC_REQUEST;
      break;
    case ADHOC_RESYNC_WAIT_READY:
      *kind = ADHOC_RENDEZVOUS_RESYNC_BEGIN;
      break;
    case ADHOC_RESYNC_WAIT_COMMIT:
      *kind = ADHOC_RENDEZVOUS_RESYNC_READY;
      break;
    case ADHOC_RESYNC_WAIT_COMMIT_ACK:
      *kind = ADHOC_RENDEZVOUS_RESYNC_COMMIT;
      break;
    case ADHOC_RESYNC_WAIT_COMPLETE:
      *kind = resync->local_data_ready ?
       ADHOC_RENDEZVOUS_RESYNC_DATA_READY :
       ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK;
      break;
    case ADHOC_RESYNC_WAIT_COMPLETE_ACK:
      if(!resync->local_data_ready || !resync->peer_data_ready)
        return 0;
      *kind = ADHOC_RENDEZVOUS_RESYNC_COMPLETE;
      break;
    case ADHOC_RESYNC_COMPLETE:
    default:
      return 0;
  }
  *epoch = resync->target_epoch;
  *recovery_id = resync->recovery_id;
  return 1;
}

static unsigned int adhoc_resync_host_receive(AdhocResync *resync,
 AdhocRendezvousKind kind, uint32_t epoch)
{
  unsigned int actions = ADHOC_RESYNC_ACTION_NONE;

  if(kind == ADHOC_RENDEZVOUS_RESYNC_REQUEST)
  {
    if(!adhoc_resync_request_matches_epoch(resync->current_epoch, epoch))
      return actions;
    return ADHOC_RESYNC_ACTION_SEND_NOW;
  }
  if(epoch != resync->target_epoch)
    return actions;
  if(kind == ADHOC_RENDEZVOUS_RESYNC_READY)
  {
    if(resync->state == ADHOC_RESYNC_WAIT_READY)
    {
      if(resync->current_epoch != resync->target_epoch)
      {
        resync->current_epoch = resync->target_epoch;
        actions |= ADHOC_RESYNC_ACTION_RESET_EPOCH;
      }
      resync->state = ADHOC_RESYNC_WAIT_COMMIT_ACK;
    }
    return actions | ADHOC_RESYNC_ACTION_SEND_NOW;
  }
  if(kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK &&
     resync->current_epoch == resync->target_epoch)
  {
    if(resync->state == ADHOC_RESYNC_WAIT_COMMIT_ACK)
    {
      resync->state = ADHOC_RESYNC_WAIT_COMPLETE_ACK;
      return actions;
    }
    return actions;
  }
  if(kind == ADHOC_RENDEZVOUS_RESYNC_DATA_READY &&
     resync->current_epoch == resync->target_epoch &&
     (resync->state == ADHOC_RESYNC_WAIT_COMMIT_ACK ||
      resync->state == ADHOC_RESYNC_WAIT_COMPLETE_ACK))
  {
    resync->peer_data_ready = 1;
    resync->state = ADHOC_RESYNC_WAIT_COMPLETE_ACK;
    return resync->local_data_ready ?
     ADHOC_RESYNC_ACTION_SEND_NOW : actions;
  }
  if(kind == ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK &&
     resync->state == ADHOC_RESYNC_WAIT_COMPLETE_ACK &&
     resync->local_data_ready && resync->peer_data_ready)
  {
    resync->state = ADHOC_RESYNC_COMPLETE;
    return ADHOC_RESYNC_ACTION_COMPLETE;
  }
  return actions;
}

static unsigned int adhoc_resync_joiner_receive(AdhocResync *resync,
 AdhocRendezvousKind kind, uint32_t epoch, uint32_t recovery_id)
{
  unsigned int actions = ADHOC_RESYNC_ACTION_NONE;

  if(kind == ADHOC_RENDEZVOUS_RESYNC_BEGIN)
  {
    if(resync->state == ADHOC_RESYNC_WAIT_BEGIN)
      resync->recovery_id = recovery_id;
    else if(recovery_id != resync->recovery_id)
      return actions;
    if(epoch != resync->target_epoch)
    {
      resync->target_epoch = epoch;
      resync->state = ADHOC_RESYNC_WAIT_COMMIT;
    }
    else if(resync->state == ADHOC_RESYNC_WAIT_BEGIN)
    {
      resync->state = ADHOC_RESYNC_WAIT_COMMIT;
    }
    return actions | ADHOC_RESYNC_ACTION_SEND_NOW;
  }
  if(epoch != resync->target_epoch)
    return actions;
  if(kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT &&
     (resync->state == ADHOC_RESYNC_WAIT_COMMIT ||
      resync->state == ADHOC_RESYNC_WAIT_COMPLETE))
  {
    if(resync->current_epoch != resync->target_epoch)
    {
      resync->current_epoch = resync->target_epoch;
      actions |= ADHOC_RESYNC_ACTION_RESET_EPOCH;
    }
    resync->state = ADHOC_RESYNC_WAIT_COMPLETE;
    return actions | ADHOC_RESYNC_ACTION_SEND_NOW;
  }
  if(kind == ADHOC_RENDEZVOUS_RESYNC_COMPLETE &&
     (resync->state == ADHOC_RESYNC_WAIT_COMPLETE ||
      resync->state == ADHOC_RESYNC_COMPLETE) &&
     resync->local_data_ready)
  {
    resync->state = ADHOC_RESYNC_COMPLETE;
    return ADHOC_RESYNC_ACTION_FINAL_ACK |
     ADHOC_RESYNC_ACTION_COMPLETE;
  }
  return actions;
}

unsigned int adhoc_resync_receive(AdhocResync *resync,
 AdhocRendezvousKind kind, uint32_t epoch, uint32_t recovery_id)
{
  if(resync == NULL || epoch == 0 || recovery_id == 0 ||
     resync->current_epoch == 0)
    return ADHOC_RESYNC_ACTION_NONE;
  if(resync->role == ADHOC_RESYNC_HOST)
  {
    if(recovery_id != resync->recovery_id)
      return ADHOC_RESYNC_ACTION_NONE;
    return adhoc_resync_host_receive(resync, kind, epoch);
  }
  if(kind != ADHOC_RENDEZVOUS_RESYNC_BEGIN &&
     recovery_id != resync->recovery_id)
    return ADHOC_RESYNC_ACTION_NONE;
  return adhoc_resync_joiner_receive(resync, kind, epoch, recovery_id);
}

int adhoc_resync_data_phase(const AdhocResync *resync)
{
  if(resync == NULL || resync->current_epoch != resync->target_epoch)
    return 0;
  if(resync->role == ADHOC_RESYNC_HOST)
  {
    return resync->state == ADHOC_RESYNC_WAIT_COMMIT_ACK ||
     resync->state == ADHOC_RESYNC_WAIT_COMPLETE_ACK;
  }
  return resync->state == ADHOC_RESYNC_WAIT_COMPLETE;
}

int adhoc_resync_encode_probe(const AdhocResync *resync,
 uint8_t *output, size_t capacity)
{
  if(!adhoc_resync_data_phase(resync) || output == NULL ||
     capacity < ADHOC_RESYNC_PROBE_SIZE || resync->recovery_id == 0)
    return 0;
  memset(output, 0, ADHOC_RESYNC_PROBE_SIZE);
  adhoc_resync_write_u32(output, ADHOC_RESYNC_PROBE_MAGIC);
  output[4] = ADHOC_RESYNC_PROBE_VERSION;
  output[5] = (uint8_t)resync->role;
  adhoc_resync_write_u16(output + 6, ADHOC_RESYNC_PROBE_SIZE);
  adhoc_resync_write_u32(output + 8, resync->current_epoch);
  adhoc_resync_write_u32(output + 12, resync->recovery_id);
  return 1;
}

int adhoc_resync_accept_probe(const AdhocResync *resync,
 const uint8_t *input, size_t size)
{
  return adhoc_resync_data_phase(resync) && input != NULL &&
   size == ADHOC_RESYNC_PROBE_SIZE &&
   adhoc_resync_read_u32(input) == ADHOC_RESYNC_PROBE_MAGIC &&
   input[4] == ADHOC_RESYNC_PROBE_VERSION &&
   input[5] == (uint8_t)(resync->role == ADHOC_RESYNC_HOST ?
    ADHOC_RESYNC_JOINER : ADHOC_RESYNC_HOST) &&
   adhoc_resync_read_u16(input + 6) == ADHOC_RESYNC_PROBE_SIZE &&
   adhoc_resync_read_u32(input + 8) == resync->current_epoch &&
   adhoc_resync_read_u32(input + 12) == resync->recovery_id;
}

unsigned int adhoc_resync_mark_data_ready(AdhocResync *resync)
{
  if(!adhoc_resync_data_phase(resync))
    return ADHOC_RESYNC_ACTION_NONE;
  resync->local_data_ready = 1;
  if(resync->role == ADHOC_RESYNC_JOINER || resync->peer_data_ready)
    return ADHOC_RESYNC_ACTION_SEND_NOW;
  return ADHOC_RESYNC_ACTION_NONE;
}

int adhoc_resync_complete(const AdhocResync *resync)
{
  return resync != NULL && resync->state == ADHOC_RESYNC_COMPLETE;
}

uint64_t adhoc_resync_send_delay(AdhocResyncRole role, int initial)
{
  if(role == ADHOC_RESYNC_JOINER)
  {
    return initial ? ADHOC_RESYNC_JOIN_INITIAL_US :
     ADHOC_RESYNC_JOIN_RETRY_US;
  }
  return initial ? 0u : ADHOC_RESYNC_HOST_RETRY_US;
}
