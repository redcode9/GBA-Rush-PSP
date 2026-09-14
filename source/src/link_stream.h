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

#ifndef LINK_STREAM_H
#define LINK_STREAM_H

#include <stdint.h>

#include "link_protocol.h"

typedef enum
{
  LINK_STREAM_ROLE_NONE,
  LINK_STREAM_ROLE_HOST,
  LINK_STREAM_ROLE_JOINER
} LinkStreamRole;

typedef enum
{
  LINK_STREAM_STATE_IDLE,
  LINK_STREAM_STATE_HOST_WAIT_REPLY,
  LINK_STREAM_STATE_HOST_WAIT_APPLY,
  LINK_STREAM_STATE_JOIN_WAIT_START,
  LINK_STREAM_STATE_JOIN_WAIT_LATCH,
  LINK_STREAM_STATE_JOIN_WAIT_APPLY
} LinkStreamState;

typedef enum
{
  LINK_STREAM_ACTION_NONE             = 0,
  LINK_STREAM_ACTION_SEND_START       = 1u << 0,
  LINK_STREAM_ACTION_LATCH_JOIN       = 1u << 1,
  LINK_STREAM_ACTION_SEND_REPLY       = 1u << 2,
  LINK_STREAM_ACTION_APPLY_HOST       = 1u << 3,
  LINK_STREAM_ACTION_APPLY_JOIN       = 1u << 4,
  LINK_STREAM_ACTION_REPLAY_REPLY     = 1u << 5,
  LINK_STREAM_ACTION_COMMITTED        = 1u << 6,
  LINK_STREAM_ACTION_DUPLICATE        = 1u << 7,
  LINK_STREAM_ACTION_STALE            = 1u << 8,
  LINK_STREAM_ACTION_FUTURE           = 1u << 9,
  LINK_STREAM_ACTION_STALE_GENERATION = 1u << 10,
  LINK_STREAM_ACTION_CONFLICT         = 1u << 11
} LinkStreamAction;

typedef struct
{
  LinkStreamStart start;
  LinkStreamReply reply;
  uint32_t sequence;
  uint8_t valid;
} LinkStreamReplyCache;

typedef struct
{
  LinkStreamRole role;
  LinkStreamState state;
  uint32_t generation;
  uint32_t last_committed;
  uint32_t active_sequence;
  LinkStreamStart active_start;
  LinkStreamReply active_reply;
  LinkStreamReply last_host_reply;
  LinkStreamReplyCache reply_cache;
  uint32_t last_host_reply_sequence;
  uint32_t latch_count;
  uint32_t apply_count;
  uint32_t replay_count;
  uint32_t duplicate_count;
  uint32_t stale_count;
  uint32_t future_count;
  uint32_t stale_generation_count;
  uint32_t conflict_count;
  uint8_t active_start_valid;
  uint8_t active_reply_valid;
  uint8_t last_host_reply_valid;
} LinkStream;

void link_stream_init(LinkStream *stream, LinkStreamRole role,
 uint32_t generation);
void link_stream_init_at(LinkStream *stream, LinkStreamRole role,
 uint32_t generation, uint32_t last_committed);
void link_stream_reset_generation(LinkStream *stream, uint32_t generation);
/* Recovery may discard an admitted START only before the GBA word is latched. */
int link_stream_discard_unlatched_start(LinkStream *stream);
int link_stream_recover_host_pending(LinkStream *stream,
 uint32_t generation);
int link_stream_recover_joiner_pending(LinkStream *stream,
 uint32_t generation, uint32_t sequence, uint32_t start_identity);
int link_stream_recover_committed(LinkStream *stream,
 uint32_t generation, uint32_t last_committed);

uint32_t link_stream_host_begin(LinkStream *stream, uint32_t sequence,
 const LinkStreamStart *start);
uint32_t link_stream_host_receive_reply(LinkStream *stream,
 uint32_t sequence, const LinkStreamReply *reply);

uint32_t link_stream_join_receive_start(LinkStream *stream,
 uint32_t sequence, const LinkStreamStart *start,
 LinkStreamReply *replay);
uint32_t link_stream_join_latched(LinkStream *stream, uint32_t sequence,
 uint32_t outgoing, uint32_t clock_cycle, LinkStreamReply *reply);

uint32_t link_stream_note_applied(LinkStream *stream, uint32_t sequence);
uint32_t link_stream_acknowledge(LinkStream *stream, uint32_t sequence);

LinkStreamState link_stream_state(const LinkStream *stream);
uint32_t link_stream_generation(const LinkStream *stream);
uint32_t link_stream_last_committed(const LinkStream *stream);
uint32_t link_stream_active_sequence(const LinkStream *stream);
uint32_t link_stream_latch_count(const LinkStream *stream);
uint32_t link_stream_apply_count(const LinkStream *stream);
uint32_t link_stream_replay_count(const LinkStream *stream);
uint32_t link_stream_conflict_count(const LinkStream *stream);
unsigned int link_stream_reply_cache_depth(const LinkStream *stream);

#endif /* LINK_STREAM_H */
