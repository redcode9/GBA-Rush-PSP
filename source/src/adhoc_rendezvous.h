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

#ifndef ADHOC_RENDEZVOUS_H
#define ADHOC_RENDEZVOUS_H

#include <stddef.h>
#include <stdint.h>

#define ADHOC_RENDEZVOUS_PACKET_SIZE 24u

typedef enum
{
  ADHOC_RENDEZVOUS_CONNECT = 1,
  ADHOC_RENDEZVOUS_CONNECT_ACK = 2,
  ADHOC_RENDEZVOUS_RESYNC_REQUEST = 3,
  ADHOC_RENDEZVOUS_RESYNC_BEGIN = 4,
  ADHOC_RENDEZVOUS_RESYNC_READY = 5,
  ADHOC_RENDEZVOUS_RESYNC_COMMIT = 6,
  ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK = 7,
  ADHOC_RENDEZVOUS_RESYNC_COMPLETE = 8,
  ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK = 9,
  ADHOC_RENDEZVOUS_RESYNC_DATA_READY = 10
} AdhocRendezvousKind;

typedef struct
{
  AdhocRendezvousKind kind;
  uint32_t connection_id;
  uint32_t epoch;
  uint32_t recovery_id;
} AdhocRendezvousMessage;

int adhoc_rendezvous_encode(const AdhocRendezvousMessage *message,
 uint8_t *output, size_t capacity, size_t *output_size);
int adhoc_rendezvous_decode(const uint8_t *input, size_t input_size,
 AdhocRendezvousMessage *message);

#endif /* ADHOC_RENDEZVOUS_H */
