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

#include "link_protocol.h"

#include <string.h>

#include "zlib/zlib.h"

#define LINK_PROTOCOL_MAGIC 0x47424C4Bu

#define LINK_HELLO_SIZE          32u
#define LINK_SESSION_READY_SIZE   8u
#define LINK_SIO_STATE_SIZE       8u
#define LINK_TRANSFER_SIZE       24u
#define LINK_REASON_SIZE          4u

static void link_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)(value >> 8);
  output[1] = (uint8_t)value;
}

static void link_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

static uint16_t link_read_u16(const uint8_t *input)
{
  return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t link_read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) |
   ((uint32_t)input[1] << 16) |
   ((uint32_t)input[2] << 8) |
   input[3];
}

static uint32_t link_protocol_crc(const uint8_t *frame, size_t frame_size)
{
  static const uint8_t zero_crc[4] = { 0, 0, 0, 0 };
  uLong crc = crc32(0L, Z_NULL, 0);

  crc = crc32(crc, frame, 20);
  crc = crc32(crc, zero_crc, sizeof(zero_crc));
  if(frame_size > LINK_PROTOCOL_HEADER_SIZE)
  {
    crc = crc32(crc, frame + LINK_PROTOCOL_HEADER_SIZE,
     (uInt)(frame_size - LINK_PROTOCOL_HEADER_SIZE));
  }
  return (uint32_t)crc;
}

int link_protocol_message_known(uint8_t type)
{
  return type >= LINK_MESSAGE_HELLO && type <= LINK_MESSAGE_DISCONNECT;
}

static int link_protocol_payload_size_valid(uint8_t type,
 uint16_t payload_size)
{
  switch(type)
  {
    case LINK_MESSAGE_HELLO:
      return payload_size == LINK_HELLO_SIZE;

    case LINK_MESSAGE_SESSION_READY:
      return payload_size == LINK_SESSION_READY_SIZE;

    case LINK_MESSAGE_SIO_STATE:
      return payload_size == LINK_SIO_STATE_SIZE;

    case LINK_MESSAGE_TRANSFER_REQUEST:
    case LINK_MESSAGE_TRANSFER_PREPARE:
    case LINK_MESSAGE_TRANSFER_ACCEPT:
    case LINK_MESSAGE_TRANSFER_COMMIT:
      return payload_size == LINK_TRANSFER_SIZE;

    case LINK_MESSAGE_TRANSFER_ABORT:
    case LINK_MESSAGE_DISCONNECT:
      return payload_size == LINK_REASON_SIZE;

    case LINK_MESSAGE_HEARTBEAT:
      return payload_size == 0;

    default:
      return payload_size <= LINK_PROTOCOL_MAX_PAYLOAD;
  }
}

static LinkProtocolError link_protocol_validate_header(const uint8_t *input,
 size_t input_size, size_t *frame_size)
{
  uint16_t payload_size;
  uint16_t header_size;
  uint8_t type;
  uint8_t flags;

  if(input == NULL || frame_size == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(input_size < LINK_PROTOCOL_HEADER_SIZE)
    return LINK_PROTOCOL_ERROR_LENGTH;
  if(link_read_u32(input) != LINK_PROTOCOL_MAGIC)
    return LINK_PROTOCOL_ERROR_MAGIC;
  if(input[4] != LINK_PROTOCOL_MAJOR)
    return LINK_PROTOCOL_ERROR_VERSION;

  type = input[6];
  flags = input[7];
  if(!link_protocol_message_known(type) &&
     (flags & LINK_PROTOCOL_FLAG_OPTIONAL) == 0)
    return LINK_PROTOCOL_ERROR_TYPE;

  payload_size = link_read_u16(input + 8);
  header_size = link_read_u16(input + 10);
  if(header_size != LINK_PROTOCOL_HEADER_SIZE)
    return LINK_PROTOCOL_ERROR_HEADER_SIZE;
  if(payload_size > LINK_PROTOCOL_MAX_PAYLOAD)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;
  if(!link_protocol_payload_size_valid(type, payload_size))
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  *frame_size = (size_t)header_size + payload_size;
  return LINK_PROTOCOL_OK;
}

void link_protocol_packet_init(LinkPacket *packet, LinkMessageType type,
 uint32_t session_id, uint32_t sequence)
{
  memset(packet, 0, sizeof(*packet));
  packet->major = LINK_PROTOCOL_MAJOR;
  packet->minor = LINK_PROTOCOL_MINOR;
  packet->type = (uint8_t)type;
  packet->session_id = session_id;
  packet->sequence = sequence;
}

LinkProtocolError link_protocol_encode(const LinkPacket *packet,
 uint8_t *output, size_t capacity, size_t *output_size)
{
  size_t frame_size;
  uint32_t crc;

  if(packet == NULL || output == NULL || output_size == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->major != LINK_PROTOCOL_MAJOR)
    return LINK_PROTOCOL_ERROR_VERSION;
  if(!link_protocol_message_known(packet->type) &&
     (packet->flags & LINK_PROTOCOL_FLAG_OPTIONAL) == 0)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size > LINK_PROTOCOL_MAX_PAYLOAD)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;
  if(!link_protocol_payload_size_valid(packet->type, packet->payload_size))
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  frame_size = LINK_PROTOCOL_HEADER_SIZE + packet->payload_size;
  if(capacity < frame_size)
    return LINK_PROTOCOL_ERROR_LENGTH;

  memset(output, 0, LINK_PROTOCOL_HEADER_SIZE);
  link_write_u32(output, LINK_PROTOCOL_MAGIC);
  output[4] = packet->major;
  output[5] = packet->minor;
  output[6] = packet->type;
  output[7] = packet->flags;
  link_write_u16(output + 8, packet->payload_size);
  link_write_u16(output + 10, LINK_PROTOCOL_HEADER_SIZE);
  link_write_u32(output + 12, packet->session_id);
  link_write_u32(output + 16, packet->sequence);

  if(packet->payload_size != 0)
  {
    memcpy(output + LINK_PROTOCOL_HEADER_SIZE, packet->payload,
     packet->payload_size);
  }

  crc = link_protocol_crc(output, frame_size);
  link_write_u32(output + 20, crc);
  *output_size = frame_size;
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_decode(const uint8_t *input, size_t input_size,
 LinkPacket *packet)
{
  LinkProtocolError error;
  size_t frame_size = 0;
  uint16_t payload_size;

  if(packet == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;

  error = link_protocol_validate_header(input, input_size, &frame_size);
  if(error != LINK_PROTOCOL_OK)
    return error;
  if(input_size != frame_size)
    return LINK_PROTOCOL_ERROR_LENGTH;
  if(link_read_u32(input + 20) != link_protocol_crc(input, input_size))
    return LINK_PROTOCOL_ERROR_CRC;

  payload_size = link_read_u16(input + 8);
  memset(packet, 0, sizeof(*packet));
  packet->major = input[4];
  packet->minor = input[5];
  packet->type = input[6];
  packet->flags = input[7];
  packet->session_id = link_read_u32(input + 12);
  packet->sequence = link_read_u32(input + 16);
  packet->payload_size = payload_size;
  if(payload_size != 0)
  {
    memcpy(packet->payload, input + LINK_PROTOCOL_HEADER_SIZE, payload_size);
  }
  return LINK_PROTOCOL_OK;
}

void link_protocol_parser_init(LinkProtocolParser *parser)
{
  memset(parser, 0, sizeof(*parser));
}

static LinkParseResult link_protocol_parser_fail(LinkProtocolParser *parser,
 LinkProtocolError error)
{
  parser->used = 0;
  parser->expected = 0;
  parser->last_error = error;
  return LINK_PARSE_ERROR;
}

LinkParseResult link_protocol_parser_feed(LinkProtocolParser *parser,
 const uint8_t *input, size_t input_size, size_t *consumed,
 LinkPacket *packet)
{
  size_t position = 0;
  size_t wanted;
  size_t copy_size;
  LinkProtocolError error;

  if(parser == NULL || consumed == NULL || packet == NULL ||
     (input == NULL && input_size != 0))
  {
    if(consumed != NULL)
      *consumed = 0;
    if(parser != NULL)
      parser->last_error = LINK_PROTOCOL_ERROR_ARGUMENT;
    return LINK_PARSE_ERROR;
  }

  *consumed = 0;
  while(1)
  {
    if(parser->used < LINK_PROTOCOL_HEADER_SIZE)
    {
      if(position == input_size)
      {
        *consumed = position;
        return LINK_PARSE_MORE;
      }

      wanted = LINK_PROTOCOL_HEADER_SIZE - parser->used;
      copy_size = input_size - position;
      if(copy_size > wanted)
        copy_size = wanted;
      memcpy(parser->buffer + parser->used, input + position, copy_size);
      parser->used += copy_size;
      position += copy_size;

      if(parser->used < LINK_PROTOCOL_HEADER_SIZE)
        continue;
    }

    if(parser->expected == 0)
    {
      error = link_protocol_validate_header(parser->buffer,
       LINK_PROTOCOL_HEADER_SIZE, &parser->expected);
      if(error != LINK_PROTOCOL_OK)
      {
        *consumed = position;
        return link_protocol_parser_fail(parser, error);
      }
    }

    if(parser->used < parser->expected)
    {
      if(position == input_size)
      {
        *consumed = position;
        return LINK_PARSE_MORE;
      }

      wanted = parser->expected - parser->used;
      copy_size = input_size - position;
      if(copy_size > wanted)
        copy_size = wanted;
      memcpy(parser->buffer + parser->used, input + position, copy_size);
      parser->used += copy_size;
      position += copy_size;
      continue;
    }

    error = link_protocol_decode(parser->buffer, parser->expected, packet);
    parser->used = 0;
    parser->expected = 0;
    parser->last_error = error;
    *consumed = position;
    if(error != LINK_PROTOCOL_OK)
      return LINK_PARSE_ERROR;
    return LINK_PARSE_PACKET;
  }
}

int link_protocol_sequence_after(uint32_t value, uint32_t reference)
{
  uint32_t difference = value - reference;
  return difference != 0 && difference < 0x80000000u;
}

LinkProtocolError link_protocol_write_hello(LinkPacket *packet,
 const LinkHello *hello)
{
  uint8_t *payload;

  if(packet == NULL || hello == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;

  packet->type = LINK_MESSAGE_HELLO;
  packet->payload_size = LINK_HELLO_SIZE;
  payload = packet->payload;
  memset(payload, 0, LINK_HELLO_SIZE);
  link_write_u32(payload, hello->capabilities);
  link_write_u32(payload + 4, hello->rom_crc32);
  link_write_u32(payload + 8, hello->rom_size);
  memcpy(payload + 12, hello->title, sizeof(hello->title));
  memcpy(payload + 24, hello->game_code, sizeof(hello->game_code));
  payload[28] = hello->revision;
  payload[29] = hello->max_players;
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_hello(const LinkPacket *packet,
 LinkHello *hello)
{
  const uint8_t *payload;

  if(packet == NULL || hello == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_HELLO)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_HELLO_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  payload = packet->payload;
  memset(hello, 0, sizeof(*hello));
  hello->capabilities = link_read_u32(payload);
  hello->rom_crc32 = link_read_u32(payload + 4);
  hello->rom_size = link_read_u32(payload + 8);
  memcpy(hello->title, payload + 12, sizeof(hello->title));
  memcpy(hello->game_code, payload + 24, sizeof(hello->game_code));
  hello->revision = payload[28];
  hello->max_players = payload[29];
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_write_session_ready(LinkPacket *packet,
 const LinkSessionReady *ready)
{
  if(packet == NULL || ready == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;

  packet->type = LINK_MESSAGE_SESSION_READY;
  packet->payload_size = LINK_SESSION_READY_SIZE;
  memset(packet->payload, 0, LINK_SESSION_READY_SIZE);
  link_write_u32(packet->payload, ready->capabilities);
  packet->payload[4] = ready->player_id;
  packet->payload[5] = ready->player_count;
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_session_ready(const LinkPacket *packet,
 LinkSessionReady *ready)
{
  if(packet == NULL || ready == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_SESSION_READY)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_SESSION_READY_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  memset(ready, 0, sizeof(*ready));
  ready->capabilities = link_read_u32(packet->payload);
  ready->player_id = packet->payload[4];
  ready->player_count = packet->payload[5];
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_write_sio_state(LinkPacket *packet,
 const LinkSioState *state)
{
  if(packet == NULL || state == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;

  packet->type = LINK_MESSAGE_SIO_STATE;
  packet->payload_size = LINK_SIO_STATE_SIZE;
  packet->payload[0] = state->mode;
  packet->payload[1] = state->player_id;
  packet->payload[2] = state->player_count;
  packet->payload[3] = state->flags;
  link_write_u16(packet->payload + 4, state->control);
  link_write_u16(packet->payload + 6, state->rcnt);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_sio_state(const LinkPacket *packet,
 LinkSioState *state)
{
  if(packet == NULL || state == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_SIO_STATE)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_SIO_STATE_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  state->mode = packet->payload[0];
  state->player_id = packet->payload[1];
  state->player_count = packet->payload[2];
  state->flags = packet->payload[3];
  state->control = link_read_u16(packet->payload + 4);
  state->rcnt = link_read_u16(packet->payload + 6);
  return LINK_PROTOCOL_OK;
}

static int link_protocol_transfer_type(uint8_t type)
{
  return type == LINK_MESSAGE_TRANSFER_REQUEST ||
   type == LINK_MESSAGE_TRANSFER_PREPARE ||
   type == LINK_MESSAGE_TRANSFER_ACCEPT ||
   type == LINK_MESSAGE_TRANSFER_COMMIT;
}

LinkProtocolError link_protocol_write_transfer(LinkPacket *packet,
 LinkMessageType type, const LinkTransferData *transfer)
{
  uint32_t i;

  if(packet == NULL || transfer == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_transfer_type((uint8_t)type))
    return LINK_PROTOCOL_ERROR_TYPE;

  packet->type = (uint8_t)type;
  packet->payload_size = LINK_TRANSFER_SIZE;
  memset(packet->payload, 0, LINK_TRANSFER_SIZE);
  packet->payload[0] = transfer->mode;
  packet->payload[1] = transfer->setting;
  packet->payload[2] = transfer->clock_owner;
  packet->payload[3] = transfer->player_count;
  packet->payload[4] = transfer->participant_mask;
  for(i = 0; i < 4; i++)
    link_write_u32(packet->payload + 8 + (i * 4), transfer->data[i]);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_transfer(const LinkPacket *packet,
 LinkTransferData *transfer)
{
  uint32_t i;

  if(packet == NULL || transfer == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_transfer_type(packet->type))
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_TRANSFER_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  memset(transfer, 0, sizeof(*transfer));
  transfer->mode = packet->payload[0];
  transfer->setting = packet->payload[1];
  transfer->clock_owner = packet->payload[2];
  transfer->player_count = packet->payload[3];
  transfer->participant_mask = packet->payload[4];
  for(i = 0; i < 4; i++)
    transfer->data[i] = link_read_u32(packet->payload + 8 + (i * 4));
  return LINK_PROTOCOL_OK;
}

static int link_protocol_reason_type(uint8_t type)
{
  return type == LINK_MESSAGE_TRANSFER_ABORT ||
   type == LINK_MESSAGE_DISCONNECT;
}

LinkProtocolError link_protocol_write_reason(LinkPacket *packet,
 LinkMessageType type, const LinkReason *reason)
{
  if(packet == NULL || reason == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_reason_type((uint8_t)type))
    return LINK_PROTOCOL_ERROR_TYPE;

  packet->type = (uint8_t)type;
  packet->payload_size = LINK_REASON_SIZE;
  link_write_u16(packet->payload, reason->reason);
  link_write_u16(packet->payload + 2, reason->detail);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_reason(const LinkPacket *packet,
 LinkReason *reason)
{
  if(packet == NULL || reason == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_reason_type(packet->type))
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_REASON_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  reason->reason = link_read_u16(packet->payload);
  reason->detail = link_read_u16(packet->payload + 2);
  return LINK_PROTOCOL_OK;
}
