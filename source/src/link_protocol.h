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

#ifndef LINK_PROTOCOL_H
#define LINK_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define LINK_PROTOCOL_MAJOR        3u
#define LINK_PROTOCOL_MINOR        0u
#define LINK_PROTOCOL_HEADER_SIZE  24u
#define LINK_PROTOCOL_MAX_PAYLOAD  128u
#define LINK_PROTOCOL_RESUME_PAYLOAD_SIZE 112u
#define LINK_PROTOCOL_MAX_PACKET   \
 (LINK_PROTOCOL_HEADER_SIZE + LINK_PROTOCOL_MAX_PAYLOAD)

#define LINK_PROTOCOL_FLAG_OPTIONAL 0x01u
#define LINK_PROTOCOL_HEARTBEAT_TOKEN_SHIFT 1u
#define LINK_PROTOCOL_HEARTBEAT_TOKEN_MASK  0x7Eu
#define LINK_PROTOCOL_HEARTBEAT_REPLY       0x80u

typedef enum
{
  LINK_MESSAGE_HELLO = 1,
  LINK_MESSAGE_SESSION_READY,
  LINK_MESSAGE_SIO_STATE,
  LINK_MESSAGE_TRANSFER_REQUEST,
  LINK_MESSAGE_TRANSFER_PREPARE,
  LINK_MESSAGE_TRANSFER_ACCEPT,
  LINK_MESSAGE_TRANSFER_COMMIT,
  LINK_MESSAGE_TRANSFER_ABORT,
  LINK_MESSAGE_HEARTBEAT,
  LINK_MESSAGE_DISCONNECT,
  LINK_MESSAGE_TRANSFER_COMMIT_ACK,
  LINK_MESSAGE_CLOCK_SYNC,
  LINK_MESSAGE_RESUME,
  LINK_MESSAGE_RESUME_ACK,
  LINK_MESSAGE_RESUME_DONE,
  LINK_MESSAGE_TRANSFER_ABORT_ACK,
  LINK_MESSAGE_TRANSFER_RELEASE,
  LINK_MESSAGE_TRANSFER_RELEASE_ACK,
  LINK_MESSAGE_TRANSFER_START,
  LINK_MESSAGE_TRANSFER_REPLY,
  LINK_MESSAGE_BENCHMARK_CONTROL
} LinkMessageType;

#define LINK_MESSAGE_LAST LINK_MESSAGE_BENCHMARK_CONTROL

typedef enum
{
  LINK_CAPABILITY_NORMAL_8   = 1u << 0,
  LINK_CAPABILITY_NORMAL_32  = 1u << 1,
  LINK_CAPABILITY_MULTI      = 1u << 2,
  LINK_CAPABILITY_TWO_PLAYER = 1u << 3,
  LINK_CAPABILITY_PRESENCE   = 1u << 4,
  LINK_CAPABILITY_CLOCK_SYNC = 1u << 5,
  LINK_CAPABILITY_RECOVERY   = 1u << 6,
  LINK_CAPABILITY_FAST_TRANSFER = 1u << 7,
  LINK_CAPABILITY_TRANSACTIONAL = 1u << 8,
  LINK_CAPABILITY_STREAM_TRANSFER = 1u << 9
} LinkCapability;

typedef enum
{
  LINK_PROTOCOL_OK = 0,
  LINK_PROTOCOL_ERROR_ARGUMENT,
  LINK_PROTOCOL_ERROR_MAGIC,
  LINK_PROTOCOL_ERROR_VERSION,
  LINK_PROTOCOL_ERROR_HEADER_SIZE,
  LINK_PROTOCOL_ERROR_PAYLOAD_SIZE,
  LINK_PROTOCOL_ERROR_LENGTH,
  LINK_PROTOCOL_ERROR_TYPE,
  LINK_PROTOCOL_ERROR_VALUE,
  LINK_PROTOCOL_ERROR_CRC
} LinkProtocolError;

typedef enum
{
  LINK_PARSE_ERROR = -1,
  LINK_PARSE_MORE = 0,
  LINK_PARSE_PACKET = 1
} LinkParseResult;

typedef struct
{
  uint8_t major;
  uint8_t minor;
  uint8_t type;
  uint8_t flags;
  uint32_t session_id;
  uint32_t sequence;
  uint16_t payload_size;
  uint8_t payload[LINK_PROTOCOL_MAX_PAYLOAD];
} LinkPacket;

typedef struct
{
  uint32_t capabilities;
  uint32_t rom_crc32;
  uint32_t rom_size;
  char title[12];
  char game_code[4];
  uint8_t revision;
  uint8_t max_players;
} LinkHello;

typedef struct
{
  uint32_t capabilities;
  uint8_t player_id;
  uint8_t player_count;
} LinkSessionReady;

typedef struct
{
  uint8_t mode;
  uint8_t player_id;
  uint8_t player_count;
  uint8_t flags;
  uint16_t control;
  uint16_t rcnt;
} LinkSioState;

#define LINK_SIO_FLAG_PENDING_START   (1u << 0)
#define LINK_SIO_FLAG_WAITING         (1u << 1)
#define LINK_SIO_FLAG_ACTIVE          (1u << 2)
#define LINK_SIO_FLAG_GAMEPLAY_ACTIVE (1u << 3)

typedef struct
{
  uint8_t mode;
  uint8_t setting;
  uint8_t clock_owner;
  uint8_t player_count;
  uint8_t participant_mask;
  uint32_t start_cycle;
  uint32_t finish_cycle;
  uint32_t data[4];
} LinkTransferData;

typedef struct
{
  uint32_t generation;
  uint32_t cycle;
  uint32_t last_sequence;
} LinkClockSync;

#define LINK_STREAM_FLAG_CLOCK_OWNER (1u << 0)
#define LINK_STREAM_FLAG_MASK LINK_STREAM_FLAG_CLOCK_OWNER

typedef struct
{
  uint32_t generation;
  uint32_t start_cycle;
  uint32_t finish_cycle;
  uint32_t clock_cycle;
  uint32_t acknowledged_sequence;
  uint32_t outgoing;
  uint32_t identity;
  uint8_t mode;
  uint8_t setting;
  uint8_t player_count;
  uint8_t flags;
} LinkStreamStart;

typedef struct
{
  uint32_t generation;
  uint32_t clock_cycle;
  uint32_t acknowledged_sequence;
  uint32_t outgoing;
  uint32_t start_identity;
  uint32_t start_cycle;
  uint8_t mode;
  uint8_t setting;
  uint8_t player_count;
  uint8_t flags;
} LinkStreamReply;

typedef struct
{
  uint32_t generation;
  uint32_t clock_cycle;
  uint32_t acknowledged_sequence;
} LinkStreamHeartbeat;

typedef enum
{
  LINK_BENCHMARK_START = 1,
  LINK_BENCHMARK_ACK,
  LINK_BENCHMARK_STOP
} LinkBenchmarkAction;

typedef struct
{
  uint32_t run_id;
  uint32_t duration_ms;
  uint32_t target_words_per_second;
  uint32_t maximum_burst_us;
  uint32_t seed;
  uint16_t burst_words;
  uint8_t action;
  uint8_t flags;
} LinkBenchmarkControl;

#define LINK_RESUME_FLAG_BARRIER  (1u << 0)
#define LINK_RESUME_FLAG_COMMITTED (1u << 1)
#define LINK_RESUME_FLAG_ABORT     (1u << 2)
#define LINK_RESUME_FLAG_STREAM    (1u << 3)

#define LINK_STREAM_CHECKPOINT_PENDING_START (1u << 0)
#define LINK_STREAM_CHECKPOINT_REPLY_CACHE   (1u << 1)

typedef struct
{
  uint32_t last_sequence;
  uint32_t active_sequence;
  LinkTransferData transfer;
  uint8_t phase;
  uint8_t flags;
  uint8_t transaction_stage;
  uint32_t epoch;
  uint32_t checkpoint;
  uint32_t checkpoint_hash;
  uint32_t journal_hash;
  uint32_t sio_revision;
  LinkSioState sio;
  uint8_t journal_stage;
  uint8_t journal_role;
  uint8_t journal_applied;
  uint8_t journal_irq_delivered;
  uint8_t release_shared;
  uint32_t stream_generation;
  uint32_t stream_transport_generation;
  uint32_t stream_last_committed;
  uint32_t stream_active_sequence;
  uint32_t stream_start_identity;
  uint32_t stream_reply_outgoing;
  uint32_t stream_commit_identity;
  uint8_t stream_flags;
} LinkResumeState;

typedef struct
{
  uint16_t reason;
  uint16_t detail;
} LinkReason;

typedef struct
{
  uint8_t buffer[LINK_PROTOCOL_MAX_PACKET];
  size_t used;
  size_t expected;
  LinkProtocolError last_error;
} LinkProtocolParser;

void link_protocol_packet_init(LinkPacket *packet, LinkMessageType type,
 uint32_t session_id, uint32_t sequence);

LinkProtocolError link_protocol_encode(const LinkPacket *packet,
 uint8_t *output, size_t capacity, size_t *output_size);
LinkProtocolError link_protocol_decode(const uint8_t *input, size_t input_size,
 LinkPacket *packet);

void link_protocol_parser_init(LinkProtocolParser *parser);
LinkParseResult link_protocol_parser_feed(LinkProtocolParser *parser,
 const uint8_t *input, size_t input_size, size_t *consumed,
 LinkPacket *packet);

int link_protocol_message_known(uint8_t type);
int link_protocol_sequence_after(uint32_t value, uint32_t reference);

LinkProtocolError link_protocol_write_hello(LinkPacket *packet,
 const LinkHello *hello);
LinkProtocolError link_protocol_read_hello(const LinkPacket *packet,
 LinkHello *hello);

LinkProtocolError link_protocol_write_session_ready(LinkPacket *packet,
 const LinkSessionReady *ready);
LinkProtocolError link_protocol_read_session_ready(const LinkPacket *packet,
 LinkSessionReady *ready);

LinkProtocolError link_protocol_write_sio_state(LinkPacket *packet,
 const LinkSioState *state);
LinkProtocolError link_protocol_read_sio_state(const LinkPacket *packet,
 LinkSioState *state);

LinkProtocolError link_protocol_write_transfer(LinkPacket *packet,
 LinkMessageType type, const LinkTransferData *transfer);
LinkProtocolError link_protocol_read_transfer(const LinkPacket *packet,
 LinkTransferData *transfer);

LinkProtocolError link_protocol_write_clock(LinkPacket *packet,
 const LinkClockSync *clock);
LinkProtocolError link_protocol_read_clock(const LinkPacket *packet,
 LinkClockSync *clock);

LinkProtocolError link_protocol_write_stream_start(LinkPacket *packet,
 const LinkStreamStart *start);
LinkProtocolError link_protocol_read_stream_start(const LinkPacket *packet,
 LinkStreamStart *start);

LinkProtocolError link_protocol_write_stream_reply(LinkPacket *packet,
 const LinkStreamReply *reply);
LinkProtocolError link_protocol_read_stream_reply(const LinkPacket *packet,
 LinkStreamReply *reply);

LinkProtocolError link_protocol_write_stream_heartbeat(LinkPacket *packet,
 const LinkStreamHeartbeat *heartbeat);
LinkProtocolError link_protocol_read_stream_heartbeat(
 const LinkPacket *packet, LinkStreamHeartbeat *heartbeat);

LinkProtocolError link_protocol_write_benchmark_control(LinkPacket *packet,
 const LinkBenchmarkControl *control);
LinkProtocolError link_protocol_read_benchmark_control(
 const LinkPacket *packet, LinkBenchmarkControl *control);

LinkProtocolError link_protocol_write_resume(LinkPacket *packet,
 LinkMessageType type, const LinkResumeState *state);
LinkProtocolError link_protocol_read_resume(const LinkPacket *packet,
 LinkResumeState *state);

LinkProtocolError link_protocol_write_reason(LinkPacket *packet,
 LinkMessageType type, const LinkReason *reason);
LinkProtocolError link_protocol_read_reason(const LinkPacket *packet,
 LinkReason *reason);

#endif /* LINK_PROTOCOL_H */
