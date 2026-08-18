#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_protocol.h"

static int failures;

#define CHECK(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
       #condition); \
      failures++; \
    } \
  } while(0)

static size_t encode_packet(const LinkPacket *packet, uint8_t *frame)
{
  size_t frame_size = 0;
  CHECK(link_protocol_encode(packet, frame, LINK_PROTOCOL_MAX_PACKET,
   &frame_size) == LINK_PROTOCOL_OK);
  return frame_size;
}

static void test_header_vector(void)
{
  LinkPacket packet;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size;
  static const uint8_t expected[LINK_PROTOCOL_HEADER_SIZE] =
  {
    0x47, 0x42, 0x4C, 0x4B,
    0x01, 0x00, 0x09, 0x00,
    0x00, 0x00, 0x00, 0x18,
    0x11, 0x22, 0x33, 0x44,
    0x55, 0x66, 0x77, 0x88,
    0x81, 0x7E, 0x75, 0x66
  };

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   0x11223344, 0x55667788);
  frame_size = encode_packet(&packet, frame);
  CHECK(frame_size == LINK_PROTOCOL_HEADER_SIZE);
  CHECK(memcmp(frame, expected, sizeof(expected)) == 0);
}

static void test_round_trip(void)
{
  LinkPacket source;
  LinkPacket decoded;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size;
  uint32_t i;

  link_protocol_packet_init(&source, LINK_MESSAGE_HEARTBEAT,
   0xA1B2C3D4, 7);
  source.type = 0x7E;
  source.flags = LINK_PROTOCOL_FLAG_OPTIONAL;
  source.payload_size = LINK_PROTOCOL_MAX_PAYLOAD;
  for(i = 0; i < LINK_PROTOCOL_MAX_PAYLOAD; i++)
    source.payload[i] = (uint8_t)(i ^ 0xA5u);

  frame_size = encode_packet(&source, frame);
  CHECK(frame_size == LINK_PROTOCOL_MAX_PACKET);
  CHECK(link_protocol_decode(frame, frame_size, &decoded) ==
   LINK_PROTOCOL_OK);
  CHECK(decoded.major == LINK_PROTOCOL_MAJOR);
  CHECK(decoded.minor == LINK_PROTOCOL_MINOR);
  CHECK(decoded.type == source.type);
  CHECK(decoded.flags == source.flags);
  CHECK(decoded.session_id == source.session_id);
  CHECK(decoded.sequence == source.sequence);
  CHECK(decoded.payload_size == source.payload_size);
  CHECK(memcmp(decoded.payload, source.payload, source.payload_size) == 0);
}

static void test_hello_payload(void)
{
  LinkPacket packet;
  LinkHello hello;
  LinkHello decoded;

  memset(&hello, 0, sizeof(hello));
  hello.capabilities = 0x01020304;
  hello.rom_crc32 = 0x11223344;
  hello.rom_size = 0x02000000;
  memcpy(hello.title, "POKEMON FIRE", sizeof(hello.title));
  memcpy(hello.game_code, "BPRE", sizeof(hello.game_code));
  hello.revision = 1;
  hello.max_players = 2;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HELLO, 1, 0);
  CHECK(link_protocol_write_hello(&packet, &hello) == LINK_PROTOCOL_OK);
  CHECK(packet.payload_size == 32);
  CHECK(packet.payload[0] == 0x01);
  CHECK(packet.payload[1] == 0x02);
  CHECK(packet.payload[2] == 0x03);
  CHECK(packet.payload[3] == 0x04);
  CHECK(packet.payload[4] == 0x11);
  CHECK(packet.payload[8] == 0x02);
  CHECK(packet.payload[28] == 1);
  CHECK(packet.payload[29] == 2);
  CHECK(link_protocol_read_hello(&packet, &decoded) == LINK_PROTOCOL_OK);
  CHECK(memcmp(&hello, &decoded, sizeof(hello)) == 0);
}

static void test_typed_payloads(void)
{
  LinkPacket packet;
  LinkSessionReady ready = { 0x01020304, 1, 2 };
  LinkSessionReady decoded_ready;
  LinkSioState state = { 2, 1, 2, 0xA5, 0x6083, 0x000F };
  LinkSioState decoded_state;
  LinkTransferData transfer;
  LinkTransferData decoded_transfer;
  LinkReason reason = { 7, 0xBEEF };
  LinkReason decoded_reason;
  uint32_t i;

  link_protocol_packet_init(&packet, LINK_MESSAGE_SESSION_READY, 1, 0);
  CHECK(link_protocol_write_session_ready(&packet, &ready) ==
   LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_session_ready(&packet, &decoded_ready) ==
   LINK_PROTOCOL_OK);
  CHECK(decoded_ready.capabilities == ready.capabilities);
  CHECK(decoded_ready.player_id == ready.player_id);
  CHECK(decoded_ready.player_count == ready.player_count);

  CHECK(link_protocol_write_sio_state(&packet, &state) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_sio_state(&packet, &decoded_state) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&state, &decoded_state, sizeof(state)) == 0);

  memset(&transfer, 0, sizeof(transfer));
  transfer.mode = 2;
  transfer.setting = 3;
  transfer.clock_owner = 1;
  transfer.player_count = 2;
  transfer.participant_mask = 3;
  for(i = 0; i < 4; i++)
    transfer.data[i] = 0x11111111u * (i + 1);

  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_PREPARE,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_transfer(&packet, &decoded_transfer) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&transfer, &decoded_transfer, sizeof(transfer)) == 0);

  CHECK(link_protocol_write_reason(&packet, LINK_MESSAGE_TRANSFER_ABORT,
   &reason) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_reason(&packet, &decoded_reason) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&reason, &decoded_reason, sizeof(reason)) == 0);

  packet.payload_size--;
  CHECK(link_protocol_read_reason(&packet, &decoded_reason) ==
   LINK_PROTOCOL_ERROR_PAYLOAD_SIZE);
}

static void test_invalid_frames(void)
{
  LinkPacket packet;
  LinkPacket decoded;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET + 1];
  uint8_t changed[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size;
  size_t output_size = 0;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT, 1, 2);
  frame_size = encode_packet(&packet, frame);

  CHECK(link_protocol_decode(frame, LINK_PROTOCOL_HEADER_SIZE - 1, &decoded) ==
   LINK_PROTOCOL_ERROR_LENGTH);

  memcpy(changed, frame, frame_size);
  changed[0] ^= 1;
  CHECK(link_protocol_decode(changed, frame_size, &decoded) ==
   LINK_PROTOCOL_ERROR_MAGIC);

  memcpy(changed, frame, frame_size);
  changed[4] = 2;
  CHECK(link_protocol_decode(changed, frame_size, &decoded) ==
   LINK_PROTOCOL_ERROR_VERSION);

  memcpy(changed, frame, frame_size);
  changed[6] = 0x7F;
  CHECK(link_protocol_decode(changed, frame_size, &decoded) ==
   LINK_PROTOCOL_ERROR_TYPE);

  memcpy(changed, frame, frame_size);
  changed[10] = 0;
  changed[11] = 23;
  CHECK(link_protocol_decode(changed, frame_size, &decoded) ==
   LINK_PROTOCOL_ERROR_HEADER_SIZE);

  memcpy(changed, frame, frame_size);
  changed[8] = 0;
  changed[9] = LINK_PROTOCOL_MAX_PAYLOAD + 1;
  CHECK(link_protocol_decode(changed, frame_size, &decoded) ==
   LINK_PROTOCOL_ERROR_PAYLOAD_SIZE);

  memcpy(changed, frame, frame_size);
  changed[20] ^= 1;
  CHECK(link_protocol_decode(changed, frame_size, &decoded) ==
   LINK_PROTOCOL_ERROR_CRC);

  frame[frame_size] = 0;
  CHECK(link_protocol_decode(frame, frame_size + 1, &decoded) ==
   LINK_PROTOCOL_ERROR_LENGTH);

  packet.payload_size = LINK_PROTOCOL_MAX_PAYLOAD + 1;
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &output_size) ==
   LINK_PROTOCOL_ERROR_PAYLOAD_SIZE);

  packet.payload_size = 0;
  CHECK(link_protocol_encode(&packet, frame, LINK_PROTOCOL_HEADER_SIZE - 1,
   &output_size) == LINK_PROTOCOL_ERROR_LENGTH);
}

static void test_optional_unknown_message(void)
{
  LinkPacket packet;
  LinkPacket decoded;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT, 1, 2);
  packet.type = 0x7F;
  packet.flags = LINK_PROTOCOL_FLAG_OPTIONAL;
  frame_size = encode_packet(&packet, frame);
  CHECK(link_protocol_decode(frame, frame_size, &decoded) ==
   LINK_PROTOCOL_OK);
  CHECK(decoded.type == 0x7F);
}

static void test_minor_version_and_payload_contract(void)
{
  LinkPacket packet;
  LinkPacket decoded;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size;
  size_t output_size = 0;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT, 1, 2);
  packet.minor = 7;
  frame_size = encode_packet(&packet, frame);
  CHECK(link_protocol_decode(frame, frame_size, &decoded) ==
   LINK_PROTOCOL_OK);
  CHECK(decoded.minor == 7);

  packet.payload_size = 1;
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &output_size) ==
   LINK_PROTOCOL_ERROR_PAYLOAD_SIZE);
}

static void test_incremental_splits(void)
{
  LinkPacket source;
  LinkPacket decoded;
  LinkProtocolParser parser;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size;
  size_t split;

  link_protocol_packet_init(&source, LINK_MESSAGE_HELLO, 0x1234, 5);
  source.payload_size = 32;
  memset(source.payload, 0xA5, source.payload_size);
  frame_size = encode_packet(&source, frame);

  for(split = 0; split <= frame_size; split++)
  {
    size_t consumed = 0;
    LinkParseResult result;

    link_protocol_parser_init(&parser);
    result = link_protocol_parser_feed(&parser, frame, split, &consumed,
     &decoded);
    CHECK(consumed == split);
    if(split == frame_size)
    {
      CHECK(result == LINK_PARSE_PACKET);
    }
    else
    {
      CHECK(result == LINK_PARSE_MORE);
      result = link_protocol_parser_feed(&parser, frame + split,
       frame_size - split, &consumed, &decoded);
      CHECK(result == LINK_PARSE_PACKET);
      CHECK(consumed == frame_size - split);
    }
    CHECK(decoded.session_id == source.session_id);
    CHECK(decoded.payload_size == source.payload_size);
  }
}

static void test_incremental_bytes_and_coalescing(void)
{
  LinkPacket first;
  LinkPacket second;
  LinkPacket decoded;
  LinkProtocolParser parser;
  LinkReason reason = { 1, 0 };
  uint8_t frames[LINK_PROTOCOL_MAX_PACKET * 2];
  size_t first_size;
  size_t second_size;
  size_t position;

  link_protocol_packet_init(&first, LINK_MESSAGE_HEARTBEAT, 1, 10);
  link_protocol_packet_init(&second, LINK_MESSAGE_DISCONNECT, 1, 11);
  CHECK(link_protocol_write_reason(&second, LINK_MESSAGE_DISCONNECT, &reason) ==
   LINK_PROTOCOL_OK);
  first_size = encode_packet(&first, frames);
  second_size = encode_packet(&second, frames + first_size);

  link_protocol_parser_init(&parser);
  for(position = 0; position < first_size; position++)
  {
    size_t consumed = 0;
    LinkParseResult result = link_protocol_parser_feed(&parser,
     frames + position, 1, &consumed, &decoded);
    CHECK(consumed == 1);
    CHECK(result == (position + 1 == first_size ?
     LINK_PARSE_PACKET : LINK_PARSE_MORE));
  }

  link_protocol_parser_init(&parser);
  {
    size_t consumed = 0;
    LinkParseResult result = link_protocol_parser_feed(&parser, frames,
     first_size + second_size, &consumed, &decoded);
    CHECK(result == LINK_PARSE_PACKET);
    CHECK(consumed == first_size);
    CHECK(decoded.type == LINK_MESSAGE_HEARTBEAT);

    result = link_protocol_parser_feed(&parser, frames + consumed,
     first_size + second_size - consumed, &consumed, &decoded);
    CHECK(result == LINK_PARSE_PACKET);
    CHECK(consumed == second_size);
    CHECK(decoded.type == LINK_MESSAGE_DISCONNECT);
  }
}

static void test_parser_rejects_oversize(void)
{
  LinkProtocolParser parser;
  LinkPacket decoded;
  uint8_t header[LINK_PROTOCOL_HEADER_SIZE] =
  {
    0x47, 0x42, 0x4C, 0x4B,
    0x01, 0x00, 0x09, 0x00,
    0x00, LINK_PROTOCOL_MAX_PAYLOAD + 1,
    0x00, 0x18
  };
  size_t consumed = 0;

  link_protocol_parser_init(&parser);
  CHECK(link_protocol_parser_feed(&parser, header, sizeof(header), &consumed,
   &decoded) == LINK_PARSE_ERROR);
  CHECK(consumed == LINK_PROTOCOL_HEADER_SIZE);
  CHECK(parser.last_error == LINK_PROTOCOL_ERROR_PAYLOAD_SIZE);
  CHECK(parser.used == 0);
  CHECK(parser.expected == 0);
}

static void test_sequence_order(void)
{
  CHECK(link_protocol_sequence_after(2, 1));
  CHECK(!link_protocol_sequence_after(1, 1));
  CHECK(!link_protocol_sequence_after(1, 2));
  CHECK(link_protocol_sequence_after(0, 0xFFFFFFFFu));
  CHECK(!link_protocol_sequence_after(0xFFFFFFFFu, 0));
  CHECK(!link_protocol_sequence_after(0x80000000u, 0));
}

int main(void)
{
  test_header_vector();
  test_round_trip();
  test_hello_payload();
  test_typed_payloads();
  test_invalid_frames();
  test_optional_unknown_message();
  test_minor_version_and_payload_contract();
  test_incremental_splits();
  test_incremental_bytes_and_coalescing();
  test_parser_rejects_oversize();
  test_sequence_order();

  if(failures)
  {
    fprintf(stderr, "%d link protocol test(s) failed\n", failures);
    return 1;
  }

  puts("Link protocol tests passed");
  return 0;
}
