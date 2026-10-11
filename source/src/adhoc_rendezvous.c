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

#include "adhoc_rendezvous.h"

#include <string.h>

#include "zlib/zlib.h"

#define ADHOC_RENDEZVOUS_MAGIC   0x47425256u
#define ADHOC_RENDEZVOUS_VERSION 2u

static void adhoc_rendezvous_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8);
  output[1] = (uint8_t)value;
}

static void adhoc_rendezvous_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static uint16_t adhoc_rendezvous_read_u16(const uint8_t *input)
{
  return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t adhoc_rendezvous_read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) |
   ((uint32_t)input[1] << 16) |
   ((uint32_t)input[2] << 8) |
   input[3];
}

static uint32_t adhoc_rendezvous_crc(const uint8_t *packet)
{
  uLong crc = crc32(0L, Z_NULL, 0);

  return (uint32_t)crc32(crc, packet, 20);
}

static int adhoc_rendezvous_kind_valid(AdhocRendezvousKind kind)
{
  return kind == ADHOC_RENDEZVOUS_CONNECT ||
   kind == ADHOC_RENDEZVOUS_CONNECT_ACK ||
   kind == ADHOC_RENDEZVOUS_RESYNC_REQUEST ||
   kind == ADHOC_RENDEZVOUS_RESYNC_BEGIN ||
   kind == ADHOC_RENDEZVOUS_RESYNC_READY ||
   kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT ||
   kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK ||
   kind == ADHOC_RENDEZVOUS_RESYNC_COMPLETE ||
   kind == ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK ||
   kind == ADHOC_RENDEZVOUS_RESYNC_DATA_READY;
}

static int adhoc_rendezvous_recovery_id_valid(
 const AdhocRendezvousMessage *message)
{
  if(message->kind == ADHOC_RENDEZVOUS_CONNECT ||
     message->kind == ADHOC_RENDEZVOUS_CONNECT_ACK)
    return message->recovery_id == 0;
  return message->recovery_id != 0;
}

int adhoc_rendezvous_encode(const AdhocRendezvousMessage *message,
 uint8_t *output, size_t capacity, size_t *output_size)
{
  if(message == NULL || output == NULL || output_size == NULL ||
     capacity < ADHOC_RENDEZVOUS_PACKET_SIZE ||
     !adhoc_rendezvous_kind_valid(message->kind) ||
     message->connection_id == 0 || message->epoch == 0 ||
     !adhoc_rendezvous_recovery_id_valid(message))
    return 0;

  memset(output, 0, ADHOC_RENDEZVOUS_PACKET_SIZE);
  adhoc_rendezvous_write_u32(output, ADHOC_RENDEZVOUS_MAGIC);
  output[4] = ADHOC_RENDEZVOUS_VERSION;
  output[5] = (uint8_t)message->kind;
  adhoc_rendezvous_write_u16(output + 6,
   ADHOC_RENDEZVOUS_PACKET_SIZE);
  adhoc_rendezvous_write_u32(output + 8, message->connection_id);
  adhoc_rendezvous_write_u32(output + 12, message->epoch);
  adhoc_rendezvous_write_u32(output + 16, message->recovery_id);
  adhoc_rendezvous_write_u32(output + 20,
   adhoc_rendezvous_crc(output));
  *output_size = ADHOC_RENDEZVOUS_PACKET_SIZE;
  return 1;
}

int adhoc_rendezvous_decode(const uint8_t *input, size_t input_size,
 AdhocRendezvousMessage *message)
{
  AdhocRendezvousMessage decoded;

  if(input == NULL || message == NULL ||
     input_size != ADHOC_RENDEZVOUS_PACKET_SIZE ||
     adhoc_rendezvous_read_u32(input) != ADHOC_RENDEZVOUS_MAGIC ||
     input[4] != ADHOC_RENDEZVOUS_VERSION ||
     adhoc_rendezvous_read_u16(input + 6) !=
      ADHOC_RENDEZVOUS_PACKET_SIZE ||
     adhoc_rendezvous_read_u32(input + 20) !=
      adhoc_rendezvous_crc(input))
    return 0;

  decoded.kind = (AdhocRendezvousKind)input[5];
  decoded.connection_id = adhoc_rendezvous_read_u32(input + 8);
  decoded.epoch = adhoc_rendezvous_read_u32(input + 12);
  decoded.recovery_id = adhoc_rendezvous_read_u32(input + 16);
  if(!adhoc_rendezvous_kind_valid(decoded.kind) ||
     decoded.connection_id == 0 || decoded.epoch == 0 ||
     !adhoc_rendezvous_recovery_id_valid(&decoded))
    return 0;
  *message = decoded;
  return 1;
}
