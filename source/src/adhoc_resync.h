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

#ifndef ADHOC_RESYNC_H
#define ADHOC_RESYNC_H

#include <stddef.h>
#include <stdint.h>

#include "adhoc_rendezvous.h"

#define ADHOC_RESYNC_PROBE_SIZE 16u

typedef enum
{
  ADHOC_RESYNC_HOST,
  ADHOC_RESYNC_JOINER
} AdhocResyncRole;

typedef enum
{
  ADHOC_RESYNC_WAIT_BEGIN,
  ADHOC_RESYNC_WAIT_READY,
  ADHOC_RESYNC_WAIT_COMMIT,
  ADHOC_RESYNC_WAIT_COMMIT_ACK,
  ADHOC_RESYNC_WAIT_COMPLETE,
  ADHOC_RESYNC_WAIT_COMPLETE_ACK,
  ADHOC_RESYNC_COMPLETE
} AdhocResyncState;

typedef enum
{
  ADHOC_RESYNC_ACTION_NONE = 0,
  ADHOC_RESYNC_ACTION_SEND_NOW = 1 << 0,
  ADHOC_RESYNC_ACTION_RESET_EPOCH = 1 << 1,
  ADHOC_RESYNC_ACTION_FINAL_ACK = 1 << 2,
  ADHOC_RESYNC_ACTION_COMPLETE = 1 << 3
} AdhocResyncAction;

typedef enum
{
  ADHOC_RESYNC_REQUEST_IGNORED,
  ADHOC_RESYNC_REQUEST_PENDING,
  ADHOC_RESYNC_REQUEST_ACCEPTED
} AdhocResyncRequestResult;

typedef struct
{
  uint32_t epoch;
  uint32_t recovery_id;
  uint64_t first_us;
  uint64_t last_us;
  uint64_t settled_until_us;
  unsigned int confirmations;
} AdhocResyncRequestGate;

typedef struct
{
  AdhocResyncRole role;
  AdhocResyncState state;
  uint32_t current_epoch;
  uint32_t target_epoch;
  uint32_t recovery_id;
  uint8_t local_data_ready;
  uint8_t peer_data_ready;
} AdhocResync;

void adhoc_resync_init(AdhocResync *resync, AdhocResyncRole role,
 uint32_t current_epoch, uint32_t recovery_id);
int adhoc_resync_request_matches_epoch(uint32_t current_epoch,
 uint32_t request_epoch);
void adhoc_resync_request_gate_init(AdhocResyncRequestGate *gate);
void adhoc_resync_request_gate_settle(AdhocResyncRequestGate *gate,
 uint64_t now_us, uint64_t duration_us);
AdhocResyncRequestResult adhoc_resync_request_gate_note(
 AdhocResyncRequestGate *gate, uint32_t current_epoch,
 uint32_t request_epoch, uint32_t recovery_id, uint64_t now_us);
int adhoc_resync_outbound(const AdhocResync *resync,
 AdhocRendezvousKind *kind, uint32_t *epoch, uint32_t *recovery_id);
unsigned int adhoc_resync_receive(AdhocResync *resync,
 AdhocRendezvousKind kind, uint32_t epoch, uint32_t recovery_id);
int adhoc_resync_data_phase(const AdhocResync *resync);
int adhoc_resync_encode_probe(const AdhocResync *resync,
 uint8_t *output, size_t capacity);
int adhoc_resync_accept_probe(const AdhocResync *resync,
 const uint8_t *input, size_t size);
unsigned int adhoc_resync_mark_data_ready(AdhocResync *resync);
int adhoc_resync_complete(const AdhocResync *resync);
uint64_t adhoc_resync_send_delay(AdhocResyncRole role, int initial);

#endif /* ADHOC_RESYNC_H */
