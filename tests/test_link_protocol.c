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
    0x03, 0x00, 0x09, 0x00,
    0x00, 0x00, 0x00, 0x18,
    0x11, 0x22, 0x33, 0x44,
    0x55, 0x66, 0x77, 0x88,
    0x4B, 0xB0, 0x03, 0xEA
  };

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   0x11223344, 0x55667788);
  frame_size = encode_packet(&packet, frame);
  CHECK(frame_size == LINK_PROTOCOL_HEADER_SIZE);
  CHECK(memcmp(frame, expected, sizeof(expected)) == 0);
}

static void test_stream_payloads(void)
{
  LinkPacket packet;
  LinkPacket decoded_packet;
  LinkStreamStart start;
  LinkStreamStart decoded_start;
  LinkStreamReply reply;
  LinkStreamReply decoded_reply;
  LinkStreamHeartbeat heartbeat;
  LinkStreamHeartbeat decoded_heartbeat;
  LinkBenchmarkControl control;
  LinkBenchmarkControl decoded_control;
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;

  CHECK(LINK_PROTOCOL_MAJOR == 3u);
  CHECK((LINK_CAPABILITY_STREAM_TRANSFER &
   LINK_CAPABILITY_TRANSACTIONAL) == 0u);

  memset(&start, 0, sizeof(start));
  start.generation = 7u;
  start.start_cycle = 0x10203040u;
  start.finish_cycle = 0x10205736u;
  start.clock_cycle = 0x10203040u;
  start.acknowledged_sequence = 41u;
  start.outgoing = 0x0000A55Au;
  start.identity = 0x89ABCDEFu;
  start.mode = 2u;
  start.setting = 3u;
  start.player_count = 2u;
  start.flags = LINK_STREAM_FLAG_CLOCK_OWNER;

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_START,
   0x11223344u, 42u);
  CHECK(link_protocol_write_stream_start(&packet, &start) ==
   LINK_PROTOCOL_OK);
  CHECK(packet.payload_size == 32u);
  CHECK(link_protocol_read_stream_start(&packet, &decoded_start) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&start, &decoded_start, sizeof(start)) == 0);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);
  CHECK(link_protocol_decode(frame, frame_size, &decoded_packet) ==
   LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_stream_start(&decoded_packet, &decoded_start) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&start, &decoded_start, sizeof(start)) == 0);

  memset(&reply, 0, sizeof(reply));
  reply.generation = 7u;
  reply.clock_cycle = 0x10204567u;
  reply.acknowledged_sequence = 42u;
  reply.outgoing = 0x00005AA5u;
  reply.start_identity = start.identity;
  reply.start_cycle = start.start_cycle;
  reply.mode = 2u;
  reply.setting = 3u;
  reply.player_count = 2u;

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_REPLY,
   0x11223344u, 42u);
  CHECK(link_protocol_write_stream_reply(&packet, &reply) ==
   LINK_PROTOCOL_OK);
  CHECK(packet.payload_size == 28u);
  CHECK(link_protocol_read_stream_reply(&packet, &decoded_reply) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&reply, &decoded_reply, sizeof(reply)) == 0);

  heartbeat.generation = 7u;
  heartbeat.clock_cycle = 0x10205678u;
  heartbeat.acknowledged_sequence = 42u;
  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   0x11223344u, 0u);
  CHECK(link_protocol_write_stream_heartbeat(&packet, &heartbeat) ==
   LINK_PROTOCOL_OK);
  CHECK(packet.payload_size == 12u);
  CHECK(link_protocol_read_stream_heartbeat(&packet, &decoded_heartbeat) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&heartbeat, &decoded_heartbeat, sizeof(heartbeat)) == 0);

  memset(&control, 0, sizeof(control));
  control.run_id = 0x55667788u;
  control.duration_ms = 600000u;
  control.target_words_per_second = 600u;
  control.maximum_burst_us = 20000u;
  control.seed = 0x13579BDFu;
  control.burst_words = 9u;
  control.action = LINK_BENCHMARK_START;

  link_protocol_packet_init(&packet, LINK_MESSAGE_BENCHMARK_CONTROL,
   0x11223344u, 43u);
  CHECK(link_protocol_write_benchmark_control(&packet, &control) ==
   LINK_PROTOCOL_OK);
  CHECK(packet.flags == LINK_PROTOCOL_FLAG_OPTIONAL);
  CHECK(packet.payload_size == 24u);
  CHECK(link_protocol_read_benchmark_control(&packet, &decoded_control) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&control, &decoded_control, sizeof(control)) == 0);

  start.generation = 0u;
  CHECK(link_protocol_write_stream_start(&packet, &start) ==
   LINK_PROTOCOL_ERROR_VALUE);
  start.generation = 7u;
  start.mode = 3u;
  CHECK(link_protocol_write_stream_start(&packet, &start) ==
   LINK_PROTOCOL_ERROR_VALUE);
  start.mode = 2u;
  start.player_count = 3u;
  CHECK(link_protocol_write_stream_start(&packet, &start) ==
   LINK_PROTOCOL_ERROR_VALUE);

  reply.generation = 0u;
  CHECK(link_protocol_write_stream_reply(&packet, &reply) ==
   LINK_PROTOCOL_ERROR_VALUE);

  control.burst_words = 0u;
  CHECK(link_protocol_write_benchmark_control(&packet, &control) ==
   LINK_PROTOCOL_ERROR_VALUE);

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT, 1u, 1u);
  CHECK(link_protocol_read_stream_start(&packet, &decoded_start) ==
   LINK_PROTOCOL_ERROR_TYPE);
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
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;
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
  transfer.start_cycle = 0x12345678u;
  transfer.finish_cycle = 0x1234ABCDu;
  for(i = 0; i < 4; i++)
    transfer.data[i] = 0x11111111u * (i + 1);

  CHECK(link_protocol_write_transfer(&packet, LINK_MESSAGE_TRANSFER_PREPARE,
   &transfer) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_transfer(&packet, &decoded_transfer) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&transfer, &decoded_transfer, sizeof(transfer)) == 0);

  CHECK(link_protocol_write_transfer(&packet,
   LINK_MESSAGE_TRANSFER_RELEASE, &transfer) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_transfer(&packet, &decoded_transfer) ==
   LINK_PROTOCOL_OK);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);
  CHECK(link_protocol_decode(frame, frame_size, &packet) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_write_transfer(&packet,
   LINK_MESSAGE_TRANSFER_RELEASE_ACK, &transfer) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);

  CHECK(link_protocol_write_reason(&packet, LINK_MESSAGE_TRANSFER_ABORT,
   &reason) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_reason(&packet, &decoded_reason) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&reason, &decoded_reason, sizeof(reason)) == 0);

  packet.payload_size--;
  CHECK(link_protocol_read_reason(&packet, &decoded_reason) ==
   LINK_PROTOCOL_ERROR_PAYLOAD_SIZE);

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_ABORT_ACK, 1, 7);
  CHECK(link_protocol_encode(&packet, frame, sizeof(frame), &frame_size) ==
   LINK_PROTOCOL_OK);
  CHECK(frame_size == LINK_PROTOCOL_HEADER_SIZE);
}

static void test_clock_and_resume_payloads(void)
{
  LinkPacket packet;
  LinkClockSync clock = { 7u, 0x12345678u, 99u };
  LinkClockSync decoded_clock;
  LinkResumeState resume;
  LinkResumeState decoded_resume;

  memset(&resume, 0, sizeof(resume));
  resume.last_sequence = 9u;
  resume.active_sequence = 10u;
  resume.transfer.mode = 2u;
  resume.transfer.setting = 3u;
  resume.transfer.clock_owner = 0u;
  resume.transfer.player_count = 2u;
  resume.transfer.participant_mask = 3u;
  resume.transfer.start_cycle = 0x10203040u;
  resume.transfer.finish_cycle = 0x10205736u;
  resume.transfer.data[0] = 0xAABBCCDDu;
  resume.transfer.data[1] = 0x11223344u;
  resume.phase = 9u;
  resume.flags = LINK_RESUME_FLAG_BARRIER;
  resume.transaction_stage = 2u;
  resume.epoch = 4u;
  resume.checkpoint = 8u;
  resume.checkpoint_hash = 0x55667788u;
  resume.journal_hash = 0x99aabbccu;
  resume.sio_revision = 12u;
  resume.sio.mode = 2u;
  resume.sio.player_id = 1u;
  resume.sio.player_count = 2u;
  resume.sio.flags = LINK_SIO_FLAG_ACTIVE;
  resume.sio.control = 0x6083u;
  resume.sio.rcnt = 0x000fu;
  resume.journal_stage = 2u;
  resume.journal_role = 1u;
  resume.journal_applied = 1u;
  resume.journal_irq_delivered = 1u;
  resume.release_shared = 1u;
  resume.stream_generation = 0x10203040u;
  resume.stream_transport_generation = 4u;
  resume.stream_last_committed = 8u;
  resume.stream_active_sequence = 9u;
  resume.stream_start_identity = 0x456789abu;
  resume.stream_reply_outgoing = 0xcafebabeu;
  resume.stream_commit_identity = 0x76543210u;
  resume.stream_flags = LINK_STREAM_CHECKPOINT_PENDING_START |
   LINK_STREAM_CHECKPOINT_REPLY_CACHE;

  link_protocol_packet_init(&packet, LINK_MESSAGE_CLOCK_SYNC, 1u, 9u);
  CHECK(link_protocol_write_clock(&packet, &clock) == LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_clock(&packet, &decoded_clock) ==
   LINK_PROTOCOL_OK);
  CHECK(memcmp(&clock, &decoded_clock, sizeof(clock)) == 0);

  link_protocol_packet_init(&packet, LINK_MESSAGE_RESUME, 1u, 9u);
  CHECK(link_protocol_write_resume(&packet, LINK_MESSAGE_RESUME, &resume) ==
   LINK_PROTOCOL_OK);
  CHECK(link_protocol_read_resume(&packet, &decoded_resume) ==
   LINK_PROTOCOL_OK);
  CHECK(decoded_resume.last_sequence == resume.last_sequence);
  CHECK(decoded_resume.active_sequence == resume.active_sequence);
  CHECK(memcmp(&decoded_resume.transfer, &resume.transfer,
   sizeof(resume.transfer)) == 0);
  CHECK(decoded_resume.phase == resume.phase);
  CHECK(decoded_resume.flags == resume.flags);
  CHECK(decoded_resume.transaction_stage == resume.transaction_stage);
  CHECK(decoded_resume.epoch == resume.epoch);
  CHECK(decoded_resume.checkpoint == resume.checkpoint);
  CHECK(decoded_resume.checkpoint_hash == resume.checkpoint_hash);
  CHECK(decoded_resume.journal_hash == resume.journal_hash);
  CHECK(decoded_resume.sio_revision == resume.sio_revision);
  CHECK(memcmp(&decoded_resume.sio, &resume.sio,
   sizeof(resume.sio)) == 0);
  CHECK(decoded_resume.journal_stage == resume.journal_stage);
  CHECK(decoded_resume.journal_role == resume.journal_role);
  CHECK(decoded_resume.journal_applied == resume.journal_applied);
  CHECK(decoded_resume.journal_irq_delivered ==
   resume.journal_irq_delivered);
  CHECK(decoded_resume.release_shared == resume.release_shared);
  CHECK(decoded_resume.stream_generation == resume.stream_generation);
  CHECK(decoded_resume.stream_transport_generation ==
   resume.stream_transport_generation);
  CHECK(decoded_resume.stream_last_committed ==
   resume.stream_last_committed);
  CHECK(decoded_resume.stream_active_sequence ==
   resume.stream_active_sequence);
  CHECK(decoded_resume.stream_start_identity ==
   resume.stream_start_identity);
  CHECK(decoded_resume.stream_reply_outgoing ==
   resume.stream_reply_outgoing);
  CHECK(decoded_resume.stream_commit_identity ==
   resume.stream_commit_identity);
  CHECK(decoded_resume.stream_flags == resume.stream_flags);
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
  changed[4] = (uint8_t)(LINK_PROTOCOL_MAJOR + 1u);
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
    LINK_PROTOCOL_MAJOR, LINK_PROTOCOL_MINOR, 0x09, 0x00,
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
  test_stream_payloads();
  test_clock_and_resume_payloads();
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
