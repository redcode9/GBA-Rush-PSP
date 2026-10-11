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
#define LINK_TRANSFER_SIZE       32u
#define LINK_REASON_SIZE          4u
#define LINK_CLOCK_SIZE          12u
#define LINK_RESUME_SIZE LINK_PROTOCOL_RESUME_PAYLOAD_SIZE
#define LINK_STREAM_START_SIZE   32u
#define LINK_STREAM_REPLY_SIZE   28u
#define LINK_STREAM_HEARTBEAT_SIZE 12u
#define LINK_BENCHMARK_SIZE      24u

static void link_write_u16(uint8_t *output, uint16_t value)
{
  output[0] = (uint8_t)((value >> 8) & 0xFFu);
  output[1] = (uint8_t)(value & 0xFFu);
}

static void link_write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)((value >> 24) & 0xFFu);
  output[1] = (uint8_t)((value >> 16) & 0xFFu);
  output[2] = (uint8_t)((value >> 8) & 0xFFu);
  output[3] = (uint8_t)(value & 0xFFu);
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
  return type >= LINK_MESSAGE_HELLO && type <= LINK_MESSAGE_LAST;
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
    case LINK_MESSAGE_TRANSFER_RELEASE:
    case LINK_MESSAGE_TRANSFER_RELEASE_ACK:
      return payload_size == LINK_TRANSFER_SIZE;

    case LINK_MESSAGE_TRANSFER_ABORT:
    case LINK_MESSAGE_DISCONNECT:
      return payload_size == LINK_REASON_SIZE;

    case LINK_MESSAGE_HEARTBEAT:
      return payload_size == 0 ||
       payload_size == LINK_STREAM_HEARTBEAT_SIZE;

    case LINK_MESSAGE_TRANSFER_COMMIT_ACK:
    case LINK_MESSAGE_TRANSFER_ABORT_ACK:
      return payload_size == 0;

    case LINK_MESSAGE_CLOCK_SYNC:
      return payload_size == LINK_CLOCK_SIZE;

    case LINK_MESSAGE_RESUME:
    case LINK_MESSAGE_RESUME_ACK:
    case LINK_MESSAGE_RESUME_DONE:
      return payload_size == LINK_RESUME_SIZE;

    case LINK_MESSAGE_TRANSFER_START:
      return payload_size == LINK_STREAM_START_SIZE;

    case LINK_MESSAGE_TRANSFER_REPLY:
      return payload_size == LINK_STREAM_REPLY_SIZE;

    case LINK_MESSAGE_BENCHMARK_CONTROL:
      return payload_size == LINK_BENCHMARK_SIZE;

    default:
      return payload_size <= LINK_PROTOCOL_MAX_PAYLOAD;
  }
}

static int link_protocol_stream_start_valid(const uint8_t *payload)
{
  return link_read_u32(payload) != 0 && payload[28] == 2u &&
   payload[29] <= 3u && payload[30] == 2u &&
   (payload[31] & LINK_STREAM_FLAG_MASK) == payload[31];
}

static int link_protocol_stream_reply_valid(const uint8_t *payload)
{
  return link_read_u32(payload) != 0 && payload[24] == 2u &&
   payload[25] <= 3u && payload[26] == 2u &&
   (payload[27] & LINK_STREAM_FLAG_MASK) == payload[27];
}

static int link_protocol_benchmark_valid(const uint8_t *payload)
{
  uint8_t action = payload[22];
  uint16_t burst_words = link_read_u16(payload + 20);

  if(link_read_u32(payload) == 0 ||
     action < LINK_BENCHMARK_START || action > LINK_BENCHMARK_STOP ||
     payload[23] != 0)
    return 0;
  if(action == LINK_BENCHMARK_STOP)
    return 1;
  return link_read_u32(payload + 4) != 0 &&
   link_read_u32(payload + 8) != 0 &&
   link_read_u32(payload + 12) != 0 &&
   burst_words != 0 && burst_words <= 64u;
}

static int link_protocol_payload_value_valid(uint8_t type,
 const uint8_t *payload)
{
  switch(type)
  {
    case LINK_MESSAGE_TRANSFER_START:
      return link_protocol_stream_start_valid(payload);

    case LINK_MESSAGE_TRANSFER_REPLY:
      return link_protocol_stream_reply_valid(payload);

    case LINK_MESSAGE_BENCHMARK_CONTROL:
      return link_protocol_benchmark_valid(payload);

    default:
      return 1;
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
  if(!link_protocol_payload_value_valid(packet->type, packet->payload))
    return LINK_PROTOCOL_ERROR_VALUE;

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
  if(!link_protocol_payload_value_valid(input[6],
     input + LINK_PROTOCOL_HEADER_SIZE))
    return LINK_PROTOCOL_ERROR_VALUE;

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
   type == LINK_MESSAGE_TRANSFER_COMMIT ||
   type == LINK_MESSAGE_TRANSFER_RELEASE ||
   type == LINK_MESSAGE_TRANSFER_RELEASE_ACK;
}

static void link_protocol_write_transfer_payload(uint8_t *payload,
 const LinkTransferData *transfer)
{
  uint32_t i;

  memset(payload, 0, LINK_TRANSFER_SIZE);
  payload[0] = transfer->mode;
  payload[1] = transfer->setting;
  payload[2] = transfer->clock_owner;
  payload[3] = transfer->player_count;
  payload[4] = transfer->participant_mask;
  link_write_u32(payload + 8, transfer->start_cycle);
  link_write_u32(payload + 12, transfer->finish_cycle);
  for(i = 0; i < 4; i++)
    link_write_u32(payload + 16 + (i * 4), transfer->data[i]);
}

static void link_protocol_read_transfer_payload(const uint8_t *payload,
 LinkTransferData *transfer)
{
  uint32_t i;

  memset(transfer, 0, sizeof(*transfer));
  transfer->mode = payload[0];
  transfer->setting = payload[1];
  transfer->clock_owner = payload[2];
  transfer->player_count = payload[3];
  transfer->participant_mask = payload[4];
  transfer->start_cycle = link_read_u32(payload + 8);
  transfer->finish_cycle = link_read_u32(payload + 12);
  for(i = 0; i < 4; i++)
    transfer->data[i] = link_read_u32(payload + 16 + (i * 4));
}

LinkProtocolError link_protocol_write_transfer(LinkPacket *packet,
 LinkMessageType type, const LinkTransferData *transfer)
{
  if(packet == NULL || transfer == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_transfer_type((uint8_t)type))
    return LINK_PROTOCOL_ERROR_TYPE;

  packet->type = (uint8_t)type;
  packet->payload_size = LINK_TRANSFER_SIZE;
  link_protocol_write_transfer_payload(packet->payload, transfer);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_transfer(const LinkPacket *packet,
 LinkTransferData *transfer)
{
  if(packet == NULL || transfer == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_transfer_type(packet->type))
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_TRANSFER_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  link_protocol_read_transfer_payload(packet->payload, transfer);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_write_clock(LinkPacket *packet,
 const LinkClockSync *clock)
{
  if(packet == NULL || clock == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;

  packet->type = LINK_MESSAGE_CLOCK_SYNC;
  packet->payload_size = LINK_CLOCK_SIZE;
  link_write_u32(packet->payload, clock->generation);
  link_write_u32(packet->payload + 4, clock->cycle);
  link_write_u32(packet->payload + 8, clock->last_sequence);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_clock(const LinkPacket *packet,
 LinkClockSync *clock)
{
  if(packet == NULL || clock == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_CLOCK_SYNC)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_CLOCK_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  clock->generation = link_read_u32(packet->payload);
  clock->cycle = link_read_u32(packet->payload + 4);
  clock->last_sequence = link_read_u32(packet->payload + 8);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_write_stream_start(LinkPacket *packet,
 const LinkStreamStart *start)
{
  uint8_t *payload;

  if(packet == NULL || start == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(start->generation == 0 || start->mode != 2u || start->setting > 3u ||
     start->player_count != 2u ||
     (start->flags & LINK_STREAM_FLAG_MASK) != start->flags)
    return LINK_PROTOCOL_ERROR_VALUE;

  packet->type = LINK_MESSAGE_TRANSFER_START;
  packet->payload_size = LINK_STREAM_START_SIZE;
  payload = packet->payload;
  memset(payload, 0, LINK_STREAM_START_SIZE);
  link_write_u32(payload, start->generation);
  link_write_u32(payload + 4, start->start_cycle);
  link_write_u32(payload + 8, start->finish_cycle);
  link_write_u32(payload + 12, start->clock_cycle);
  link_write_u32(payload + 16, start->acknowledged_sequence);
  link_write_u32(payload + 20, start->outgoing);
  link_write_u32(payload + 24, start->identity);
  payload[28] = start->mode;
  payload[29] = start->setting;
  payload[30] = start->player_count;
  payload[31] = start->flags;
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_stream_start(const LinkPacket *packet,
 LinkStreamStart *start)
{
  const uint8_t *payload;

  if(packet == NULL || start == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_TRANSFER_START)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_STREAM_START_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;
  payload = packet->payload;
  if(!link_protocol_payload_value_valid(packet->type, payload))
    return LINK_PROTOCOL_ERROR_VALUE;

  memset(start, 0, sizeof(*start));
  start->generation = link_read_u32(payload);
  start->start_cycle = link_read_u32(payload + 4);
  start->finish_cycle = link_read_u32(payload + 8);
  start->clock_cycle = link_read_u32(payload + 12);
  start->acknowledged_sequence = link_read_u32(payload + 16);
  start->outgoing = link_read_u32(payload + 20);
  start->identity = link_read_u32(payload + 24);
  start->mode = payload[28];
  start->setting = payload[29];
  start->player_count = payload[30];
  start->flags = payload[31];
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_write_stream_reply(LinkPacket *packet,
 const LinkStreamReply *reply)
{
  uint8_t *payload;

  if(packet == NULL || reply == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(reply->generation == 0 || reply->mode != 2u || reply->setting > 3u ||
     reply->player_count != 2u ||
     (reply->flags & LINK_STREAM_FLAG_MASK) != reply->flags)
    return LINK_PROTOCOL_ERROR_VALUE;

  packet->type = LINK_MESSAGE_TRANSFER_REPLY;
  packet->payload_size = LINK_STREAM_REPLY_SIZE;
  payload = packet->payload;
  memset(payload, 0, LINK_STREAM_REPLY_SIZE);
  link_write_u32(payload, reply->generation);
  link_write_u32(payload + 4, reply->clock_cycle);
  link_write_u32(payload + 8, reply->acknowledged_sequence);
  link_write_u32(payload + 12, reply->outgoing);
  link_write_u32(payload + 16, reply->start_identity);
  link_write_u32(payload + 20, reply->start_cycle);
  payload[24] = reply->mode;
  payload[25] = reply->setting;
  payload[26] = reply->player_count;
  payload[27] = reply->flags;
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_stream_reply(const LinkPacket *packet,
 LinkStreamReply *reply)
{
  const uint8_t *payload;

  if(packet == NULL || reply == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_TRANSFER_REPLY)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_STREAM_REPLY_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;
  payload = packet->payload;
  if(!link_protocol_payload_value_valid(packet->type, payload))
    return LINK_PROTOCOL_ERROR_VALUE;

  memset(reply, 0, sizeof(*reply));
  reply->generation = link_read_u32(payload);
  reply->clock_cycle = link_read_u32(payload + 4);
  reply->acknowledged_sequence = link_read_u32(payload + 8);
  reply->outgoing = link_read_u32(payload + 12);
  reply->start_identity = link_read_u32(payload + 16);
  reply->start_cycle = link_read_u32(payload + 20);
  reply->mode = payload[24];
  reply->setting = payload[25];
  reply->player_count = payload[26];
  reply->flags = payload[27];
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_write_stream_heartbeat(LinkPacket *packet,
 const LinkStreamHeartbeat *heartbeat)
{
  if(packet == NULL || heartbeat == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(heartbeat->generation == 0)
    return LINK_PROTOCOL_ERROR_VALUE;

  packet->type = LINK_MESSAGE_HEARTBEAT;
  packet->payload_size = LINK_STREAM_HEARTBEAT_SIZE;
  link_write_u32(packet->payload, heartbeat->generation);
  link_write_u32(packet->payload + 4, heartbeat->clock_cycle);
  link_write_u32(packet->payload + 8, heartbeat->acknowledged_sequence);
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_stream_heartbeat(
 const LinkPacket *packet, LinkStreamHeartbeat *heartbeat)
{
  if(packet == NULL || heartbeat == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_HEARTBEAT)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_STREAM_HEARTBEAT_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  heartbeat->generation = link_read_u32(packet->payload);
  heartbeat->clock_cycle = link_read_u32(packet->payload + 4);
  heartbeat->acknowledged_sequence = link_read_u32(packet->payload + 8);
  return heartbeat->generation == 0 ? LINK_PROTOCOL_ERROR_VALUE :
   LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_write_benchmark_control(LinkPacket *packet,
 const LinkBenchmarkControl *control)
{
  uint8_t *payload;

  if(packet == NULL || control == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;

  packet->type = LINK_MESSAGE_BENCHMARK_CONTROL;
  packet->flags = LINK_PROTOCOL_FLAG_OPTIONAL;
  packet->payload_size = LINK_BENCHMARK_SIZE;
  payload = packet->payload;
  memset(payload, 0, LINK_BENCHMARK_SIZE);
  link_write_u32(payload, control->run_id);
  link_write_u32(payload + 4, control->duration_ms);
  link_write_u32(payload + 8, control->target_words_per_second);
  link_write_u32(payload + 12, control->maximum_burst_us);
  link_write_u32(payload + 16, control->seed);
  link_write_u16(payload + 20, control->burst_words);
  payload[22] = control->action;
  payload[23] = control->flags;
  if(!link_protocol_benchmark_valid(payload))
    return LINK_PROTOCOL_ERROR_VALUE;
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_benchmark_control(
 const LinkPacket *packet, LinkBenchmarkControl *control)
{
  const uint8_t *payload;

  if(packet == NULL || control == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(packet->type != LINK_MESSAGE_BENCHMARK_CONTROL)
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_BENCHMARK_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;
  payload = packet->payload;
  if(!link_protocol_benchmark_valid(payload))
    return LINK_PROTOCOL_ERROR_VALUE;

  memset(control, 0, sizeof(*control));
  control->run_id = link_read_u32(payload);
  control->duration_ms = link_read_u32(payload + 4);
  control->target_words_per_second = link_read_u32(payload + 8);
  control->maximum_burst_us = link_read_u32(payload + 12);
  control->seed = link_read_u32(payload + 16);
  control->burst_words = link_read_u16(payload + 20);
  control->action = payload[22];
  control->flags = payload[23];
  return LINK_PROTOCOL_OK;
}

static int link_protocol_resume_type(uint8_t type)
{
  return type == LINK_MESSAGE_RESUME ||
   type == LINK_MESSAGE_RESUME_ACK ||
   type == LINK_MESSAGE_RESUME_DONE;
}

LinkProtocolError link_protocol_write_resume(LinkPacket *packet,
 LinkMessageType type, const LinkResumeState *state)
{
  if(packet == NULL || state == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_resume_type((uint8_t)type))
    return LINK_PROTOCOL_ERROR_TYPE;

  packet->type = (uint8_t)type;
  packet->payload_size = LINK_RESUME_SIZE;
  memset(packet->payload, 0, LINK_RESUME_SIZE);
  link_write_u32(packet->payload, state->last_sequence);
  link_write_u32(packet->payload + 4, state->active_sequence);
  link_protocol_write_transfer_payload(packet->payload + 8,
   &state->transfer);
  packet->payload[40] = state->phase;
  packet->payload[41] = state->flags;
  packet->payload[42] = state->transaction_stage;
  link_write_u32(packet->payload + 44, state->epoch);
  link_write_u32(packet->payload + 48, state->checkpoint);
  link_write_u32(packet->payload + 52, state->checkpoint_hash);
  link_write_u32(packet->payload + 56, state->journal_hash);
  link_write_u32(packet->payload + 60, state->sio_revision);
  packet->payload[64] = state->sio.mode;
  packet->payload[65] = state->sio.player_id;
  packet->payload[66] = state->sio.player_count;
  packet->payload[67] = state->sio.flags;
  link_write_u16(packet->payload + 68, state->sio.control);
  link_write_u16(packet->payload + 70, state->sio.rcnt);
  packet->payload[72] = state->journal_stage;
  packet->payload[73] = state->journal_role;
  packet->payload[74] = state->journal_applied;
  packet->payload[75] = state->journal_irq_delivered;
  packet->payload[76] = state->release_shared;
  link_write_u32(packet->payload + 80, state->stream_generation);
  link_write_u32(packet->payload + 84,
   state->stream_transport_generation);
  link_write_u32(packet->payload + 88, state->stream_last_committed);
  link_write_u32(packet->payload + 92, state->stream_active_sequence);
  link_write_u32(packet->payload + 96, state->stream_start_identity);
  link_write_u32(packet->payload + 100, state->stream_reply_outgoing);
  link_write_u32(packet->payload + 104, state->stream_commit_identity);
  packet->payload[108] = state->stream_flags;
  return LINK_PROTOCOL_OK;
}

LinkProtocolError link_protocol_read_resume(const LinkPacket *packet,
 LinkResumeState *state)
{
  if(packet == NULL || state == NULL)
    return LINK_PROTOCOL_ERROR_ARGUMENT;
  if(!link_protocol_resume_type(packet->type))
    return LINK_PROTOCOL_ERROR_TYPE;
  if(packet->payload_size != LINK_RESUME_SIZE)
    return LINK_PROTOCOL_ERROR_PAYLOAD_SIZE;

  memset(state, 0, sizeof(*state));
  state->last_sequence = link_read_u32(packet->payload);
  state->active_sequence = link_read_u32(packet->payload + 4);
  link_protocol_read_transfer_payload(packet->payload + 8,
   &state->transfer);
  state->phase = packet->payload[40];
  state->flags = packet->payload[41];
  state->transaction_stage = packet->payload[42];
  state->epoch = link_read_u32(packet->payload + 44);
  state->checkpoint = link_read_u32(packet->payload + 48);
  state->checkpoint_hash = link_read_u32(packet->payload + 52);
  state->journal_hash = link_read_u32(packet->payload + 56);
  state->sio_revision = link_read_u32(packet->payload + 60);
  state->sio.mode = packet->payload[64];
  state->sio.player_id = packet->payload[65];
  state->sio.player_count = packet->payload[66];
  state->sio.flags = packet->payload[67];
  state->sio.control = link_read_u16(packet->payload + 68);
  state->sio.rcnt = link_read_u16(packet->payload + 70);
  state->journal_stage = packet->payload[72];
  state->journal_role = packet->payload[73];
  state->journal_applied = packet->payload[74];
  state->journal_irq_delivered = packet->payload[75];
  state->release_shared = packet->payload[76];
  state->stream_generation = link_read_u32(packet->payload + 80);
  state->stream_transport_generation =
   link_read_u32(packet->payload + 84);
  state->stream_last_committed = link_read_u32(packet->payload + 88);
  state->stream_active_sequence = link_read_u32(packet->payload + 92);
  state->stream_start_identity = link_read_u32(packet->payload + 96);
  state->stream_reply_outgoing = link_read_u32(packet->payload + 100);
  state->stream_commit_identity = link_read_u32(packet->payload + 104);
  state->stream_flags = packet->payload[108];
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
