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

#include "link_stream.h"

#include <limits.h>
#include <string.h>

static void link_stream_increment(uint32_t *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

static uint32_t link_stream_next_sequence(uint32_t sequence)
{
  sequence++;
  if(sequence == 0)
    sequence = 1u;
  return sequence;
}

static int link_stream_sequence_after(uint32_t value, uint32_t reference)
{
  uint32_t difference = value - reference;
  return difference != 0 && difference < 0x80000000u;
}

static int link_stream_start_equal(const LinkStreamStart *left,
 const LinkStreamStart *right)
{
  return left->generation == right->generation &&
   left->start_cycle == right->start_cycle &&
   left->finish_cycle == right->finish_cycle &&
   left->clock_cycle == right->clock_cycle &&
   left->acknowledged_sequence == right->acknowledged_sequence &&
   left->outgoing == right->outgoing &&
   left->identity == right->identity && left->mode == right->mode &&
   left->setting == right->setting &&
   left->player_count == right->player_count &&
   left->flags == right->flags;
}

static int link_stream_reply_equal(const LinkStreamReply *left,
 const LinkStreamReply *right)
{
  return left->generation == right->generation &&
   left->clock_cycle == right->clock_cycle &&
   left->acknowledged_sequence == right->acknowledged_sequence &&
   left->outgoing == right->outgoing &&
   left->start_identity == right->start_identity &&
   left->start_cycle == right->start_cycle && left->mode == right->mode &&
   left->setting == right->setting &&
   left->player_count == right->player_count &&
   left->flags == right->flags;
}

static int link_stream_start_valid(const LinkStream *stream,
 uint32_t sequence, const LinkStreamStart *start)
{
  return stream != NULL && start != NULL && sequence != 0 &&
   start->generation == stream->generation && start->mode == 2u &&
   start->setting <= 3u && start->player_count == 2u &&
   (start->flags & LINK_STREAM_FLAG_MASK) == start->flags;
}

static uint32_t link_stream_conflict(LinkStream *stream)
{
  link_stream_increment(&stream->conflict_count);
  return LINK_STREAM_ACTION_CONFLICT;
}

static uint32_t link_stream_stale_generation(LinkStream *stream)
{
  link_stream_increment(&stream->stale_generation_count);
  return LINK_STREAM_ACTION_STALE_GENERATION;
}

static uint32_t link_stream_duplicate(LinkStream *stream, int stale)
{
  uint32_t actions = LINK_STREAM_ACTION_DUPLICATE;

  link_stream_increment(&stream->duplicate_count);
  if(stale)
  {
    link_stream_increment(&stream->stale_count);
    actions |= LINK_STREAM_ACTION_STALE;
  }
  return actions;
}

static uint32_t link_stream_future(LinkStream *stream)
{
  link_stream_increment(&stream->future_count);
  return LINK_STREAM_ACTION_FUTURE;
}

void link_stream_init_at(LinkStream *stream, LinkStreamRole role,
 uint32_t generation, uint32_t last_committed)
{
  if(stream == NULL)
    return;
  memset(stream, 0, sizeof(*stream));
  stream->role = role;
  stream->generation = generation;
  stream->last_committed = last_committed;
  stream->state = role == LINK_STREAM_ROLE_JOINER ?
   LINK_STREAM_STATE_JOIN_WAIT_START : LINK_STREAM_STATE_IDLE;
}

void link_stream_init(LinkStream *stream, LinkStreamRole role,
 uint32_t generation)
{
  link_stream_init_at(stream, role, generation, 0);
}

void link_stream_reset_generation(LinkStream *stream, uint32_t generation)
{
  LinkStreamRole role;

  if(stream == NULL)
    return;
  role = stream->role;
  link_stream_init(stream, role, generation);
}

static void link_stream_rebase_cached_semantics(LinkStream *stream,
 uint32_t generation)
{
  if(stream->reply_cache.valid)
  {
    stream->reply_cache.start.generation = generation;
    stream->reply_cache.reply.generation = generation;
  }
  if(stream->last_host_reply_valid)
    stream->last_host_reply.generation = generation;
}

int link_stream_discard_unlatched_start(LinkStream *stream)
{
  if(stream == NULL || stream->role != LINK_STREAM_ROLE_JOINER ||
     stream->state != LINK_STREAM_STATE_JOIN_WAIT_LATCH ||
     !stream->active_start_valid || stream->active_reply_valid)
    return 0;
  stream->active_sequence = 0;
  stream->active_start_valid = 0;
  memset(&stream->active_start, 0, sizeof(stream->active_start));
  stream->state = LINK_STREAM_STATE_JOIN_WAIT_START;
  return 1;
}

int link_stream_recover_host_pending(LinkStream *stream,
 uint32_t generation)
{
  if(stream == NULL || generation == 0 ||
     stream->role != LINK_STREAM_ROLE_HOST ||
     stream->state != LINK_STREAM_STATE_HOST_WAIT_REPLY ||
     !stream->active_start_valid || stream->active_reply_valid ||
     stream->active_sequence !=
      link_stream_next_sequence(stream->last_committed) ||
     stream->active_start.identity == 0)
    return 0;

  stream->generation = generation;
  stream->active_start.generation = generation;
  stream->active_start.acknowledged_sequence = stream->last_committed;
  link_stream_rebase_cached_semantics(stream, generation);
  return 1;
}

int link_stream_recover_joiner_pending(LinkStream *stream,
 uint32_t generation, uint32_t sequence, uint32_t start_identity)
{
  uint32_t previous;

  if(stream == NULL || generation == 0 || sequence == 0 ||
     start_identity == 0 || stream->role != LINK_STREAM_ROLE_JOINER ||
     stream->state != LINK_STREAM_STATE_JOIN_WAIT_START ||
     stream->active_start_valid || stream->active_reply_valid)
    return 0;

  previous = sequence - 1u;
  if(stream->last_committed == sequence)
  {
    if(!stream->reply_cache.valid ||
       stream->reply_cache.sequence != sequence ||
       stream->reply_cache.start.identity != start_identity ||
       stream->reply_cache.reply.start_identity != start_identity)
      return 0;
  }
  else if(stream->last_committed != previous &&
          !(sequence == 1u && stream->last_committed == UINT32_MAX))
  {
    return 0;
  }

  stream->generation = generation;
  link_stream_rebase_cached_semantics(stream, generation);
  return 1;
}

int link_stream_recover_committed(LinkStream *stream,
 uint32_t generation, uint32_t last_committed)
{
  LinkStreamState expected;

  if(stream == NULL || generation == 0 || stream->last_committed !=
      last_committed)
    return 0;
  expected = stream->role == LINK_STREAM_ROLE_HOST ?
   LINK_STREAM_STATE_IDLE : LINK_STREAM_STATE_JOIN_WAIT_START;
  if((stream->role != LINK_STREAM_ROLE_HOST &&
      stream->role != LINK_STREAM_ROLE_JOINER) ||
     stream->state != expected || stream->active_start_valid ||
     stream->active_reply_valid)
    return 0;

  stream->generation = generation;
  link_stream_rebase_cached_semantics(stream, generation);
  return 1;
}

uint32_t link_stream_host_begin(LinkStream *stream, uint32_t sequence,
 const LinkStreamStart *start)
{
  if(stream == NULL || start == NULL ||
     stream->role != LINK_STREAM_ROLE_HOST || stream->generation == 0)
    return LINK_STREAM_ACTION_CONFLICT;
  if(start->generation != stream->generation)
    return link_stream_stale_generation(stream);
  if(stream->state != LINK_STREAM_STATE_IDLE)
  {
    if(sequence == stream->active_sequence && stream->active_start_valid)
    {
      if(!link_stream_start_equal(&stream->active_start, start))
        return link_stream_conflict(stream);
      return link_stream_duplicate(stream, 0) |
       LINK_STREAM_ACTION_SEND_START;
    }
    return link_stream_conflict(stream);
  }
  if(sequence != link_stream_next_sequence(stream->last_committed))
  {
    if(link_stream_sequence_after(sequence, stream->last_committed))
      return link_stream_future(stream);
    return link_stream_duplicate(stream, 1);
  }
  if(!link_stream_start_valid(stream, sequence, start) ||
     start->acknowledged_sequence != stream->last_committed)
    return link_stream_conflict(stream);

  stream->active_sequence = sequence;
  stream->active_start = *start;
  memset(&stream->active_reply, 0, sizeof(stream->active_reply));
  stream->active_start_valid = 1;
  stream->active_reply_valid = 0;
  stream->state = LINK_STREAM_STATE_HOST_WAIT_REPLY;
  return LINK_STREAM_ACTION_SEND_START;
}

uint32_t link_stream_host_receive_reply(LinkStream *stream,
 uint32_t sequence, const LinkStreamReply *reply)
{
  if(stream == NULL || reply == NULL ||
     stream->role != LINK_STREAM_ROLE_HOST)
    return LINK_STREAM_ACTION_CONFLICT;
  if(reply->generation != stream->generation)
    return link_stream_stale_generation(stream);
  if(stream->state == LINK_STREAM_STATE_IDLE)
  {
    if(stream->last_host_reply_valid &&
       sequence == stream->last_host_reply_sequence)
    {
      if(!link_stream_reply_equal(&stream->last_host_reply, reply))
        return link_stream_conflict(stream);
      return link_stream_duplicate(stream, 1);
    }
    if(link_stream_sequence_after(sequence, stream->last_committed))
      return link_stream_future(stream);
    return link_stream_duplicate(stream, 1);
  }
  if(sequence != stream->active_sequence)
  {
    if(link_stream_sequence_after(sequence, stream->active_sequence))
      return link_stream_future(stream);
    return link_stream_duplicate(stream, 1);
  }
  if(stream->state == LINK_STREAM_STATE_HOST_WAIT_APPLY)
  {
    if(!stream->active_reply_valid ||
       !link_stream_reply_equal(&stream->active_reply, reply))
      return link_stream_conflict(stream);
    return link_stream_duplicate(stream, 0);
  }
  if(stream->state != LINK_STREAM_STATE_HOST_WAIT_REPLY ||
     !stream->active_start_valid ||
     reply->acknowledged_sequence != sequence ||
     reply->start_identity != stream->active_start.identity ||
     reply->start_cycle != stream->active_start.start_cycle ||
     reply->mode != stream->active_start.mode ||
     reply->setting != stream->active_start.setting ||
     reply->player_count != stream->active_start.player_count ||
     (reply->flags & LINK_STREAM_FLAG_MASK) != reply->flags)
    return link_stream_conflict(stream);

  stream->active_reply = *reply;
  stream->active_reply_valid = 1;
  stream->state = LINK_STREAM_STATE_HOST_WAIT_APPLY;
  return LINK_STREAM_ACTION_APPLY_HOST;
}

uint32_t link_stream_join_receive_start(LinkStream *stream,
 uint32_t sequence, const LinkStreamStart *start,
 LinkStreamReply *replay)
{
  uint32_t actions;
  uint32_t expected;

  if(stream == NULL || start == NULL || replay == NULL ||
     stream->role != LINK_STREAM_ROLE_JOINER)
    return LINK_STREAM_ACTION_CONFLICT;
  if(start->generation != stream->generation)
    return link_stream_stale_generation(stream);
  if(stream->reply_cache.valid &&
     sequence == stream->reply_cache.sequence)
  {
    if(!link_stream_start_equal(&stream->reply_cache.start, start))
      return link_stream_conflict(stream);
    *replay = stream->reply_cache.reply;
    link_stream_increment(&stream->replay_count);
    return link_stream_duplicate(stream, 0) |
     LINK_STREAM_ACTION_REPLAY_REPLY;
  }
  if(stream->active_start_valid && sequence == stream->active_sequence)
  {
    if(!link_stream_start_equal(&stream->active_start, start))
      return link_stream_conflict(stream);
    actions = link_stream_duplicate(stream, 0);
    if(stream->active_reply_valid)
    {
      *replay = stream->active_reply;
      link_stream_increment(&stream->replay_count);
      actions |= LINK_STREAM_ACTION_REPLAY_REPLY;
    }
    return actions;
  }

  expected = link_stream_next_sequence(stream->last_committed);
  if(sequence != expected)
  {
    if(link_stream_sequence_after(sequence, stream->last_committed))
      return link_stream_future(stream);
    return link_stream_duplicate(stream, 1);
  }
  if(stream->state != LINK_STREAM_STATE_JOIN_WAIT_START ||
     !link_stream_start_valid(stream, sequence, start) ||
     start->acknowledged_sequence != stream->last_committed)
    return link_stream_conflict(stream);

  stream->reply_cache.valid = 0;
  stream->active_sequence = sequence;
  stream->active_start = *start;
  memset(&stream->active_reply, 0, sizeof(stream->active_reply));
  stream->active_start_valid = 1;
  stream->active_reply_valid = 0;
  stream->state = LINK_STREAM_STATE_JOIN_WAIT_LATCH;
  link_stream_increment(&stream->latch_count);
  return LINK_STREAM_ACTION_LATCH_JOIN;
}

uint32_t link_stream_join_latched(LinkStream *stream, uint32_t sequence,
 uint32_t outgoing, uint32_t clock_cycle, LinkStreamReply *reply)
{
  if(stream == NULL || reply == NULL ||
     stream->role != LINK_STREAM_ROLE_JOINER ||
     stream->state != LINK_STREAM_STATE_JOIN_WAIT_LATCH ||
     !stream->active_start_valid || sequence != stream->active_sequence)
    return LINK_STREAM_ACTION_CONFLICT;

  memset(reply, 0, sizeof(*reply));
  reply->generation = stream->generation;
  reply->clock_cycle = clock_cycle;
  reply->acknowledged_sequence = sequence;
  reply->outgoing = outgoing;
  reply->start_identity = stream->active_start.identity;
  reply->start_cycle = stream->active_start.start_cycle;
  reply->mode = stream->active_start.mode;
  reply->setting = stream->active_start.setting;
  reply->player_count = stream->active_start.player_count;
  reply->flags = 0;

  stream->active_reply = *reply;
  stream->active_reply_valid = 1;
  stream->reply_cache.start = stream->active_start;
  stream->reply_cache.reply = *reply;
  stream->reply_cache.sequence = sequence;
  stream->reply_cache.valid = 1;
  stream->state = LINK_STREAM_STATE_JOIN_WAIT_APPLY;
  return LINK_STREAM_ACTION_SEND_REPLY | LINK_STREAM_ACTION_APPLY_JOIN;
}

uint32_t link_stream_note_applied(LinkStream *stream, uint32_t sequence)
{
  if(stream == NULL || sequence == 0 ||
     sequence != stream->active_sequence ||
     !stream->active_start_valid || !stream->active_reply_valid)
    return LINK_STREAM_ACTION_CONFLICT;

  if(stream->role == LINK_STREAM_ROLE_HOST)
  {
    if(stream->state != LINK_STREAM_STATE_HOST_WAIT_APPLY)
      return link_stream_conflict(stream);
    stream->last_host_reply = stream->active_reply;
    stream->last_host_reply_sequence = sequence;
    stream->last_host_reply_valid = 1;
    stream->state = LINK_STREAM_STATE_IDLE;
  }
  else if(stream->role == LINK_STREAM_ROLE_JOINER)
  {
    if(stream->state != LINK_STREAM_STATE_JOIN_WAIT_APPLY)
      return link_stream_conflict(stream);
    stream->state = LINK_STREAM_STATE_JOIN_WAIT_START;
  }
  else
  {
    return link_stream_conflict(stream);
  }

  stream->last_committed = sequence;
  stream->active_sequence = 0;
  stream->active_start_valid = 0;
  stream->active_reply_valid = 0;
  memset(&stream->active_start, 0, sizeof(stream->active_start));
  memset(&stream->active_reply, 0, sizeof(stream->active_reply));
  link_stream_increment(&stream->apply_count);
  return LINK_STREAM_ACTION_COMMITTED;
}

uint32_t link_stream_acknowledge(LinkStream *stream, uint32_t sequence)
{
  if(stream == NULL || stream->role != LINK_STREAM_ROLE_JOINER)
    return LINK_STREAM_ACTION_NONE;
  if(stream->reply_cache.valid &&
     !link_stream_sequence_after(stream->reply_cache.sequence, sequence))
  {
    memset(&stream->reply_cache, 0, sizeof(stream->reply_cache));
  }
  return LINK_STREAM_ACTION_NONE;
}

LinkStreamState link_stream_state(const LinkStream *stream)
{
  return stream == NULL ? LINK_STREAM_STATE_IDLE : stream->state;
}

uint32_t link_stream_generation(const LinkStream *stream)
{
  return stream == NULL ? 0 : stream->generation;
}

uint32_t link_stream_last_committed(const LinkStream *stream)
{
  return stream == NULL ? 0 : stream->last_committed;
}

uint32_t link_stream_active_sequence(const LinkStream *stream)
{
  return stream == NULL ? 0 : stream->active_sequence;
}

uint32_t link_stream_latch_count(const LinkStream *stream)
{
  return stream == NULL ? 0 : stream->latch_count;
}

uint32_t link_stream_apply_count(const LinkStream *stream)
{
  return stream == NULL ? 0 : stream->apply_count;
}

uint32_t link_stream_replay_count(const LinkStream *stream)
{
  return stream == NULL ? 0 : stream->replay_count;
}

uint32_t link_stream_conflict_count(const LinkStream *stream)
{
  return stream == NULL ? 0 : stream->conflict_count;
}

unsigned int link_stream_reply_cache_depth(const LinkStream *stream)
{
  return stream != NULL && stream->reply_cache.valid ? 1u : 0u;
}
