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

#include "link_session.h"

#include <string.h>

static uint32_t link_session_transfer_hash(const LinkTransferData *transfer);
static int link_session_transaction_begin(LinkSession *session,
 uint32_t sequence);
static int link_session_transaction_abort(LinkSession *session,
 uint32_t sequence);
static uint32_t link_session_send_heartbeat(LinkSession *session,
 uint8_t flags);
static uint32_t link_session_send_heartbeat_probe(LinkSession *session);

#define LINK_STREAM_RECOVERY_FLAGS_DETAIL      0x5301u
#define LINK_STREAM_RECOVERY_GENERATION_DETAIL 0x5302u
#define LINK_STREAM_RECOVERY_SEQUENCE_DETAIL   0x5303u
#define LINK_STREAM_RECOVERY_IDENTITY_DETAIL   0x5304u
#define LINK_STREAM_RECOVERY_CACHE_DETAIL      0x5305u
#define LINK_STREAM_RECOVERY_TRANSPORT_DETAIL  0x5306u
#define LINK_STREAM_RECOVERY_APPLY_DETAIL      0x5307u

static uint32_t link_session_next_stream_sequence(uint32_t sequence)
{
  sequence++;
  return sequence == 0 ? 1u : sequence;
}

static void link_session_sync_stream_checkpoint(LinkSession *session)
{
  LinkStreamRole role;
  LinkStreamState state;

  state = link_stream_state(&session->stream);
  if(link_stream_last_committed(&session->stream) ==
      session->last_sequence ||
     (state != LINK_STREAM_STATE_IDLE &&
      state != LINK_STREAM_STATE_JOIN_WAIT_START))
    return;
  role = session->role == LINK_ROLE_HOST ? LINK_STREAM_ROLE_HOST :
   LINK_STREAM_ROLE_JOINER;
  link_stream_init_at(&session->stream, role,
   link_stream_generation(&session->stream),
   session->last_sequence);
}

static void link_session_clear_legacy_transaction(LinkSession *session)
{
  session->transaction_stage = LINK_TRANSACTION_EMPTY;
  session->transaction_sequence = 0;
  session->transaction_hash = 0;
  session->transaction_applied = 0;
}

static void link_session_clear_deferred_prepare(LinkSession *session)
{
  session->deferred_prepare_sequence = 0;
  session->deferred_prepare_valid = 0;
  session->deferred_prepare_idle_seen = 0;
  memset(&session->deferred_prepare, 0,
   sizeof(session->deferred_prepare));
}

static uint64_t link_session_now(const LinkSession *session)
{
  return session->transport.ops->now_us(session->transport.context);
}

static uint32_t link_session_set_state(LinkSession *session,
 LinkSessionState state)
{
  if(session->state == state)
    return LINK_SESSION_EVENT_NONE;

  session->state = state;
  session->state_started_us = link_session_now(session);
  return LINK_SESSION_EVENT_STATE_CHANGE;
}

static int link_session_state_uses_connection(LinkSessionState state)
{
  return state == LINK_SESSION_NEGOTIATING ||
   state == LINK_SESSION_WAIT_READY ||
   state == LINK_SESSION_WAIT_READY_ACK ||
   state == LINK_SESSION_CONNECTED ||
   state == LINK_SESSION_WAIT_PREPARE ||
   state == LINK_SESSION_WAIT_ACCEPT ||
   state == LINK_SESSION_WAIT_COMMIT ||
   state == LINK_SESSION_WAIT_COMMIT_ACK ||
   state == LINK_SESSION_WAIT_COMMIT_RELEASE ||
   state == LINK_SESSION_WAIT_RELEASE_ACK ||
   state == LINK_SESSION_WAIT_ABORT_ACK ||
   state == LINK_SESSION_WAIT_STREAM_REPLY ||
   state == LINK_SESSION_RECOVERING ||
   state == LINK_SESSION_WAIT_RESUME_ACK ||
   state == LINK_SESSION_WAIT_RESUME_DONE;
}

static uint64_t link_session_shift_time(uint64_t value, uint64_t delta)
{
  return UINT64_MAX - value < delta ? UINT64_MAX : value + delta;
}

static int link_session_transport_recovery_requested(
 const LinkSession *session)
{
  return session->transport_health ==
   LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED;
}

static int link_session_transport_recovering(const LinkSession *session)
{
  return session->transport_health == LINK_TRANSPORT_HEALTH_RECOVERING;
}

static int link_session_transport_pressured(const LinkSession *session)
{
  return session->transport_health == LINK_TRANSPORT_HEALTH_PRESSURED;
}

static void link_session_begin_transport_pause(LinkSession *session)
{
  if(session->transport_health == LINK_TRANSPORT_HEALTH_HEALTHY)
    session->transport_pause_started_us = link_session_now(session);
}

static void link_session_resume_transport_deadlines(LinkSession *session,
 uint64_t now)
{
  uint64_t paused;

  if(now < session->transport_pause_started_us)
    paused = 0;
  else
    paused = now - session->transport_pause_started_us;
  session->state_started_us = link_session_shift_time(
   session->state_started_us, paused);
  session->last_rx_us = link_session_shift_time(session->last_rx_us, paused);
  session->last_tx_us = link_session_shift_time(session->last_tx_us, paused);
  if(session->barrier_active)
  {
    session->barrier_started_us = link_session_shift_time(
     session->barrier_started_us, paused);
  }
  if(session->ready_wait_active)
  {
    session->ready_wait_started_us = link_session_shift_time(
     session->ready_wait_started_us, paused);
  }
  session->transport_pause_started_us = 0;
}

static void link_session_note_peer_activity(LinkSession *session)
{
  if(!link_session_state_uses_connection(session->state))
    return;

  session->last_rx_us = link_session_now(session);
}

static int link_session_peer_in_menu(const LinkSession *session)
{
  return session->peer_sio_valid &&
   (session->negotiated_capabilities & LINK_CAPABILITY_PRESENCE) != 0 &&
   (session->peer_sio.flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE) == 0;
}

static int link_session_sio_state_hot(const LinkSioState *state)
{
  return state != NULL &&
   (state->flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE) != 0 &&
   state->mode <= GBA_SIO_MODE_MULTIPLAYER;
}

static int link_session_sio_hot(const LinkSession *session)
{
  return (session->local_sio_valid &&
    link_session_sio_state_hot(&session->local_sio_latest)) ||
   (session->peer_sio_valid &&
    link_session_sio_state_hot(&session->peer_sio));
}

int link_session_is_connected(const LinkSession *session)
{
  return session != NULL &&
   (session->state == LINK_SESSION_CONNECTED ||
    session->state == LINK_SESSION_WAIT_PREPARE ||
    session->state == LINK_SESSION_WAIT_ACCEPT ||
    session->state == LINK_SESSION_WAIT_COMMIT ||
    session->state == LINK_SESSION_WAIT_COMMIT_ACK ||
    session->state == LINK_SESSION_WAIT_COMMIT_RELEASE ||
    session->state == LINK_SESSION_WAIT_RELEASE_ACK ||
    session->state == LINK_SESSION_WAIT_ABORT_ACK ||
    session->state == LINK_SESSION_WAIT_STREAM_REPLY ||
    session->state == LINK_SESSION_RECOVERING ||
    session->state == LINK_SESSION_WAIT_RESUME_ACK ||
    session->state == LINK_SESSION_WAIT_RESUME_DONE);
}

int link_session_in_barrier(const LinkSession *session)
{
  return session != NULL && session->barrier_active != 0;
}

static int link_session_has_localized_stream_barrier(
 const LinkSession *session)
{
  return session != NULL && session->barrier_active != 0 &&
   (session->state == LINK_SESSION_WAIT_STREAM_REPLY ||
    (link_session_transport_recovering(session) &&
     session->recovery_previous_state ==
      LINK_SESSION_WAIT_STREAM_REPLY)) &&
   session->transfer.mode == GBA_SIO_MODE_MULTIPLAYER &&
   (session->negotiated_capabilities &
    LINK_CAPABILITY_STREAM_TRANSFER) != 0;
}

static uint32_t link_session_stream_latch_delay(const LinkSession *session)
{
  uint32_t host_elapsed;
  uint32_t target_cycle;
  uint32_t remaining;

  if(session == NULL || link_stream_state(&session->stream) !=
      LINK_STREAM_STATE_JOIN_WAIT_LATCH)
    return GBA_SIO_NO_EVENT;
  if(session->applied_sequence == 0 ||
     session->applied_transfer.mode != GBA_SIO_MODE_MULTIPLAYER)
    return 0;

  /* Preserve the master's inter-word guest time on the slower PSP. A REPLY
     acknowledges a latched word, not execution of the slave's serial ISR. */
  host_elapsed = session->stream.active_start.start_cycle -
   session->applied_transfer.start_cycle;
  if(host_elapsed >= 0x80000000u)
    return 0;
  target_cycle = session->stream_local_start_cycle + host_elapsed;
  remaining = target_cycle - session->sio->current_cycle;
  return remaining < 0x80000000u ? remaining : 0;
}

uint32_t link_session_cycles_until_pause(const LinkSession *session)
{
  uint32_t duration;
  uint32_t elapsed;

  if(!link_session_has_localized_stream_barrier(session))
  {
    uint32_t latch_delay = link_session_stream_latch_delay(session);

    /* Also bound the next main-thread service point for an early START. */
    return latch_delay == 0 ? GBA_SIO_NO_EVENT : latch_delay;
  }
  duration = session->transfer.finish_cycle -
   session->transfer.start_cycle;
  elapsed = session->sio->current_cycle - session->barrier_cycle;
  if(elapsed >= 0x80000000u || elapsed >= duration)
    return 0u;
  return duration - elapsed;
}

LinkSessionPauseReason link_session_pause_reason(const LinkSession *session)
{
  int stream_session;

  if(session == NULL)
    return LINK_SESSION_PAUSE_NONE;
  if(link_session_transport_recovering(session) ||
     link_session_transport_recovery_requested(session))
    return LINK_SESSION_PAUSE_RECOVERY;
  if(session->barrier_active)
  {
    if(!link_session_has_localized_stream_barrier(session) ||
       link_session_cycles_until_pause(session) == 0u)
      return LINK_SESSION_PAUSE_SIO_DEPENDENCY;
  }
  if(link_session_transport_pressured(session) ||
     session->state != LINK_SESSION_CONNECTED)
    return LINK_SESSION_PAUSE_NONE;

  stream_session = (session->negotiated_capabilities &
   LINK_CAPABILITY_STREAM_TRANSFER) != 0;
  if(stream_session && (session->role == LINK_ROLE_HOST ||
      link_stream_state(&session->stream) == LINK_STREAM_STATE_JOIN_WAIT_LATCH))
    return LINK_SESSION_PAUSE_NONE;
  if(stream_session)
  {
    uint64_t now = link_session_now(session);

    /* A one-second heartbeat cannot pace a 2 ms guest lead for 500 ms.
       Only recent gameplay samples may impose this advisory wait. */
    if(!session->local_sio_valid || !session->peer_sio_valid ||
       !link_session_sio_state_hot(&session->local_sio_latest) ||
       !link_session_sio_state_hot(&session->peer_sio) ||
       now < session->clock.last_peer_us ||
       now - session->clock.last_peer_us >= LINK_SESSION_STREAM_PACING_US)
      return LINK_SESSION_PAUSE_NONE;
  }
  if(link_clock_should_throttle(&session->clock, link_session_now(session)))
    return LINK_SESSION_PAUSE_AHEAD_PACING;
  return LINK_SESSION_PAUSE_NONE;
}

int link_session_should_pause(const LinkSession *session)
{
  LinkSessionPauseReason reason = link_session_pause_reason(session);

  return reason == LINK_SESSION_PAUSE_SIO_DEPENDENCY ||
   reason == LINK_SESSION_PAUSE_RECOVERY;
}

int link_session_should_throttle(const LinkSession *session)
{
  return link_session_pause_reason(session) ==
   LINK_SESSION_PAUSE_AHEAD_PACING;
}

LinkSessionState link_session_state(const LinkSession *session)
{
  return session->state;
}

static void link_session_reset_runtime(LinkSession *session)
{
  GbaSioEvent sio_event;

  session->peer_minor = 0;
  session->peer_sio_valid = 0;
  session->local_sio_valid = 0;
  session->local_sio_sent_valid = 0;
  session->transport_connected = 0;
  session->transport_health = LINK_TRANSPORT_HEALTH_HEALTHY;
  session->transport_resume_ready = 0;
  session->barrier_active = 0;
  session->ready_wait_active = 0;
  session->reason = LINK_REASON_NONE;
  session->timeout_source = LINK_TIMEOUT_NONE;
  session->last_timeout_source = LINK_TIMEOUT_NONE;
  session->reason_detail = 0;
  session->last_transfer_reason = LINK_REASON_NONE;
  session->last_transfer_detail = 0;
  session->terminal_previous_state = LINK_SESSION_IDLE;
  session->recovery_previous_state = LINK_SESSION_IDLE;
  session->negotiated_capabilities = 0;
  session->pending_events = 0;
  session->invariant_violations = 0;
  session->last_sequence = 0;
  session->active_sequence = 0;
  session->barrier_cycle = 0;
  session->transport_generation = 0;
  session->stale_transport_events = 0;
  session->transaction_sequence = 0;
  session->transaction_hash = 0;
  session->applied_sequence = 0;
  session->applied_hash = 0;
  session->sio_revision = 0;
  session->barrier_started_us = 0;
  session->last_rx_us = 0;
  session->last_tx_us = 0;
  session->last_sio_publish_us = 0;
  session->ready_wait_started_us = 0;
  session->transport_pause_started_us = 0;
  session->recovery_requested_us = 0;
  session->barrier_last_us = 0;
  session->barrier_max_us = 0;
  session->stream_burst_started_us = 0;
  session->stream_burst_last_us = 0;
  session->stream_burst_max_us = 0;
  session->last_message_sequence = 0;
  session->terminal_sequence = 0;
  session->heartbeat_tx_count = 0;
  session->heartbeat_rx_count = 0;
  session->heartbeat_request_tx_count = 0;
  session->heartbeat_request_rx_count = 0;
  session->heartbeat_reply_tx_count = 0;
  session->heartbeat_reply_rx_count = 0;
  session->heartbeat_duplicate_request_count = 0;
  session->heartbeat_unmatched_reply_count = 0;
  session->stream_start_tx_count = 0;
  session->stream_reply_tx_count = 0;
  session->legacy_transfer_tx_count = 0;
  session->stream_recovery_generation = 0;
  session->stream_burst_words = 0;
  session->stream_local_start_cycle = 0;
  session->stream_start_received_us = 0;
  session->last_message_type = 0;
  session->terminal_message_type = 0;
  session->terminal_valid = 0;
  session->heartbeat_probe_pending = 0;
  session->heartbeat_outstanding_token = 0;
  session->heartbeat_next_token = 0;
  session->heartbeat_last_request_token = 0;
  session->heartbeat_last_request_valid = 0;
  session->peer_resume_valid = 0;
  session->resume_done_valid = 0;
  session->stream_recovery_valid = 0;
  session->stream_recovery_pending = 0;
  session->transaction_stage = LINK_TRANSACTION_EMPTY;
  session->transaction_applied = 0;
  session->journal_valid = 0;
  link_session_clear_deferred_prepare(session);
  memset(&session->peer_hello, 0, sizeof(session->peer_hello));
  memset(&session->peer_sio, 0, sizeof(session->peer_sio));
  memset(&session->local_sio_latest, 0, sizeof(session->local_sio_latest));
  memset(&session->local_sio_sent, 0, sizeof(session->local_sio_sent));
  memset(&session->transfer, 0, sizeof(session->transfer));
  memset(&session->applied_transfer, 0,
   sizeof(session->applied_transfer));
  memset(&session->peer_resume, 0, sizeof(session->peer_resume));
  memset(&session->resume_done, 0, sizeof(session->resume_done));
  memset(&session->journal, 0, sizeof(session->journal));
  memset(&session->recovery, 0, sizeof(session->recovery));
  memset(&session->recovery_plan, 0, sizeof(session->recovery_plan));
  memset(&session->stream, 0, sizeof(session->stream));
  link_benchmark_init(&session->benchmark);
  link_clock_init(&session->clock, 1u, session->clock.local_cycle);
  sio_event = gba_sio_disconnect(session->sio);
  if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
    session->pending_events |= LINK_SESSION_EVENT_SERIAL_IRQ;
}

void link_session_init(LinkSession *session, GbaSio *sio,
 LinkTransport transport, const LinkHello *hello)
{
  memset(session, 0, sizeof(*session));
  session->sio = sio;
  session->transport = transport;
  if(hello != NULL)
    session->local_hello = *hello;
  session->state = LINK_SESSION_IDLE;
}

static int link_session_transport_valid(const LinkSession *session)
{
  const LinkTransportOps *ops = session->transport.ops;

  return session->sio != NULL && session->sio->registers != NULL &&
   ops != NULL && ops->host != NULL &&
   ops->join != NULL && ops->send != NULL && ops->poll != NULL &&
   ops->request_recovery != NULL && ops->disconnect != NULL &&
   ops->now_us != NULL;
}

int link_session_host(LinkSession *session, uint32_t session_id)
{
  uint32_t pending_events = 0;

  if(session == NULL || session_id == 0 ||
     !link_session_transport_valid(session))
    return 0;

  if(session->state != LINK_SESSION_IDLE &&
     session->state != LINK_SESSION_DISCONNECTED &&
     session->state != LINK_SESSION_ERROR)
    pending_events = link_session_disconnect(session, LINK_REASON_USER);

  link_session_reset_runtime(session);
  session->pending_events |= pending_events &
   LINK_SESSION_EVENT_SERIAL_IRQ;
  session->role = LINK_ROLE_HOST;
  session->session_id = session_id;
  link_journal_init(&session->journal, LINK_JOURNAL_ROLE_HOST,
   session_id, 1u, 0u, 0u);
  link_recovery_coordinator_init(&session->recovery, session_id, 1u);
  session->journal_valid = 1;
  link_clock_init(&session->clock, session_id, session->clock.local_cycle);
  session->state = LINK_SESSION_HOSTING;
  session->state_started_us = link_session_now(session);

  if(!session->transport.ops->host(session->transport.context))
  {
    session->state = LINK_SESSION_ERROR;
    session->reason = LINK_REASON_TRANSPORT;
    return 0;
  }
  return 1;
}

int link_session_join(LinkSession *session)
{
  uint32_t pending_events = 0;

  if(session == NULL || !link_session_transport_valid(session))
    return 0;

  if(session->state != LINK_SESSION_IDLE &&
     session->state != LINK_SESSION_DISCONNECTED &&
     session->state != LINK_SESSION_ERROR)
    pending_events = link_session_disconnect(session, LINK_REASON_USER);

  link_session_reset_runtime(session);
  session->pending_events |= pending_events &
   LINK_SESSION_EVENT_SERIAL_IRQ;
  session->role = LINK_ROLE_JOINER;
  session->session_id = 0;
  session->state = LINK_SESSION_JOINING;
  session->state_started_us = link_session_now(session);

  if(!session->transport.ops->join(session->transport.context))
  {
    session->state = LINK_SESSION_ERROR;
    session->reason = LINK_REASON_TRANSPORT;
    return 0;
  }
  return 1;
}

static int link_session_send(LinkSession *session, const LinkPacket *packet)
{
  uint8_t frame[LINK_PROTOCOL_MAX_PACKET];
  size_t frame_size = 0;

  if(link_protocol_encode(packet, frame, sizeof(frame), &frame_size) !=
     LINK_PROTOCOL_OK)
    return 0;
  if(!session->transport.ops->send(session->transport.context, frame,
     frame_size))
    return 0;

  session->last_tx_us = link_session_now(session);
  return 1;
}

static int link_session_send_hello(LinkSession *session)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HELLO,
   session->session_id, 0);
  if(link_protocol_write_hello(&packet, &session->local_hello) !=
     LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static int link_session_send_ready(LinkSession *session,
 const LinkSessionReady *ready)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_SESSION_READY,
   session->session_id, 0);
  if(link_protocol_write_session_ready(&packet, ready) != LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static int link_session_send_reason_sequence(LinkSession *session,
 LinkMessageType type, LinkSessionReason reason, uint16_t detail,
 uint32_t sequence)
{
  LinkPacket packet;
  LinkReason wire_reason;

  wire_reason.reason = (uint16_t)reason;
  wire_reason.detail = detail;
  link_protocol_packet_init(&packet, type, session->session_id, sequence);
  if(link_protocol_write_reason(&packet, type, &wire_reason) !=
     LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static int link_session_send_reason(LinkSession *session,
 LinkMessageType type, LinkSessionReason reason, uint16_t detail)
{
  uint32_t sequence = session->barrier_active ?
   session->active_sequence : session->last_sequence;

  return link_session_send_reason_sequence(session, type, reason, detail,
   sequence);
}

static uint32_t link_session_end_barrier(LinkSession *session, int abort,
 int cancel)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;
  GbaSioEvent sio_event;
  uint64_t now;
  uint64_t duration;

  if(session->barrier_active == 0)
    return events;

  if(abort)
  {
    sio_event = cancel ? gba_sio_cancel(session->sio) :
     gba_sio_abort(session->sio);
    if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
      events |= LINK_SESSION_EVENT_SERIAL_IRQ;
  }

  now = link_session_now(session);
  duration = session->barrier_started_us != 0 &&
   now >= session->barrier_started_us ? now - session->barrier_started_us : 0;
  session->barrier_last_us = duration;
  if(duration > session->barrier_max_us)
    session->barrier_max_us = duration;

  session->barrier_active = 0;
  session->barrier_cycle = 0;
  session->barrier_started_us = 0;
  events |= LINK_SESSION_EVENT_BARRIER_END;
  events |= abort ? LINK_SESSION_EVENT_TRANSFER_ABORTED :
   LINK_SESSION_EVENT_TRANSFER_COMMITTED;
  return events;
}

static int link_session_has_committed_transfer(const LinkSession *session)
{
  /* A stream result is immutable once scheduled, even before its IRQ.
     Detaching the cable must not replace it with an abort result. */
  if(session->sio->active && session->applied_sequence != 0 &&
     session->applied_sequence ==
      link_stream_last_committed(&session->stream))
    return 1;
  return session->transaction_applied != 0 &&
   (session->transaction_stage == LINK_TRANSACTION_DECIDED ||
    session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED);
}

static GbaSioEvent link_session_detach_sio(LinkSession *session)
{
  if(link_session_has_committed_transfer(session))
  {
    gba_sio_set_connection(session->sio, 0, 0);
    return GBA_SIO_EVENT_NONE;
  }
  return gba_sio_disconnect(session->sio);
}

static int link_session_reason_valid(uint16_t reason)
{
  return reason <= LINK_REASON_ROM_CHANGE;
}

static void link_session_capture_terminal(LinkSession *session)
{
  if(session->terminal_valid)
    return;

  session->terminal_previous_state = session->state;
  session->terminal_message_type = session->last_message_type;
  session->terminal_sequence = session->last_message_type != 0 ?
   session->last_message_sequence : session->last_sequence;
  session->terminal_valid = 1;
}

static uint32_t link_session_terminate_benchmark(LinkSession *session,
 LinkBenchmarkFailure failure)
{
  return link_benchmark_fail(&session->benchmark, failure,
   link_session_now(session)) != LINK_BENCHMARK_ACTION_NONE ?
    LINK_SESSION_EVENT_BENCHMARK_UPDATE : LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_fail(LinkSession *session,
 LinkSessionReason reason, uint16_t detail, int notify_peer)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;
  GbaSioEvent sio_event;

  if(session->state == LINK_SESSION_ERROR)
    return events;

  link_session_capture_terminal(session);
  link_session_clear_deferred_prepare(session);
  events |= link_session_terminate_benchmark(session,
   LINK_BENCHMARK_FAILURE_STREAM);

  if(notify_peer && session->transport_connected && session->session_id != 0)
  {
    if(session->barrier_active &&
       !link_session_has_committed_transfer(session))
      link_session_send_reason(session, LINK_MESSAGE_TRANSFER_ABORT,
       reason, detail);
    link_session_send_reason(session, LINK_MESSAGE_DISCONNECT, reason, detail);
  }

  events |= link_session_end_barrier(session,
   !link_session_has_committed_transfer(session), 0);
  sio_event = link_session_detach_sio(session);
  if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
    events |= LINK_SESSION_EVENT_SERIAL_IRQ;
  session->reason = reason;
  session->reason_detail = detail;
  session->transport_connected = 0;
  session->transport_health = LINK_TRANSPORT_HEALTH_FAILED;
  session->transport_resume_ready = 0;
  session->transport.ops->disconnect(session->transport.context);
  events |= link_session_set_state(session, LINK_SESSION_ERROR);
  events |= LINK_SESSION_EVENT_SESSION_CLOSED;
  return events;
}

static int link_session_transfer_reason_recoverable(
 LinkSessionReason reason)
{
  return reason == LINK_REASON_TIMEOUT ||
   reason == LINK_REASON_MODE_MISMATCH;
}

static uint32_t link_session_abort_transfer(LinkSession *session,
 LinkSessionReason reason, uint16_t detail, uint32_t sequence,
 int notify_peer)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;
  GbaSioEvent sio_event;
  int deferred_prepare = session->deferred_prepare_valid &&
   session->deferred_prepare_sequence == sequence;

  if(session->role == LINK_ROLE_JOINER && session->barrier_active &&
     session->state == LINK_SESSION_WAIT_PREPARE &&
     session->transaction_stage == LINK_TRANSACTION_PROPOSED &&
     session->journal.stage == LINK_JOURNAL_EMPTY &&
     !link_session_transaction_begin(session, sequence))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  if(!session->barrier_active &&
     sequence == session->last_sequence + 1u &&
     (session->transaction_sequence != sequence ||
      session->transaction_stage == LINK_TRANSACTION_DECIDED ||
      session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED ||
      session->transaction_stage == LINK_TRANSACTION_ABORTED ||
      session->transaction_applied))
  {
    session->active_sequence = sequence;
    if(!link_session_transaction_begin(session, sequence))
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  }

  if((session->barrier_active ||
      sequence == session->last_sequence + 1u) &&
     !link_session_transaction_abort(session, sequence))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  if(deferred_prepare)
    link_session_clear_deferred_prepare(session);

  if(notify_peer && session->transport_connected && session->session_id != 0 &&
     !link_session_send_reason_sequence(session, LINK_MESSAGE_TRANSFER_ABORT,
      reason, detail, sequence))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);

  if(session->barrier_active)
  {
    events |= link_session_end_barrier(session, 1,
     reason == LINK_REASON_MODE_MISMATCH);
  }
  else
  {
    if(!deferred_prepare)
    {
      sio_event = gba_sio_abort(session->sio);
      if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
        events |= LINK_SESSION_EVENT_SERIAL_IRQ;
    }
    events |= LINK_SESSION_EVENT_TRANSFER_ABORTED;
  }

  session->last_sequence = sequence;
  session->active_sequence = sequence;
  session->ready_wait_started_us = 0;
  session->ready_wait_active = 0;
  session->last_transfer_reason = reason;
  session->last_transfer_detail = detail;
  events |= link_session_set_state(session, notify_peer ?
   LINK_SESSION_WAIT_ABORT_ACK : LINK_SESSION_CONNECTED);
  return events;
}

static int link_session_local_abort_is_uncertain(
 const LinkSession *session)
{
  /* Stream multiplayer has no legacy journal decision. The joiner may
     already have scheduled the result while its REPLY is still in flight. */
  return session->state == LINK_SESSION_WAIT_STREAM_REPLY ||
   !link_journal_local_cancel_permitted(&session->journal);
}

static uint32_t link_session_request_transport_recovery(
 LinkSession *session, LinkSessionReason reason, uint16_t detail,
 LinkTimeoutSource timeout_source)
{
  if(link_session_transport_recovering(session) ||
     link_session_transport_recovery_requested(session))
    return LINK_SESSION_EVENT_NONE;
  link_session_begin_transport_pause(session);
  session->last_transfer_reason = reason;
  session->last_transfer_detail = detail;
  if(reason == LINK_REASON_TIMEOUT)
  {
    session->timeout_source = timeout_source;
    session->last_timeout_source = timeout_source;
  }
  if(!session->transport.ops->request_recovery(session->transport.context,
     (int)reason, timeout_source))
  {
    session->transport_pause_started_us = 0;
    return link_session_fail(session, LINK_REASON_TRANSPORT,
     (uint16_t)reason, 0);
  }
  session->transport_health = LINK_TRANSPORT_HEALTH_RECOVERY_REQUESTED;
  session->recovery_requested_us = link_session_now(session);
  return LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_abort_or_recover(LinkSession *session,
 LinkSessionReason reason, uint16_t detail, uint32_t sequence,
 int notify_peer, LinkTimeoutSource timeout_source)
{
  if(link_session_transfer_reason_recoverable(reason) &&
     link_session_local_abort_is_uncertain(session))
    return link_session_request_transport_recovery(session, reason, detail,
     timeout_source);
  if(reason == LINK_REASON_TIMEOUT)
  {
    session->timeout_source = timeout_source;
    session->last_timeout_source = timeout_source;
  }
  return link_session_abort_transfer(session, reason, detail, sequence,
   notify_peer);
}

uint32_t link_session_disconnect(LinkSession *session,
 LinkSessionReason reason)
{
  uint32_t events;
  GbaSioEvent sio_event;

  if(session == NULL || !link_session_transport_valid(session))
    return LINK_SESSION_EVENT_NONE;
  if(session->state == LINK_SESSION_IDLE ||
     session->state == LINK_SESSION_DISCONNECTED ||
     session->state == LINK_SESSION_ERROR)
    return LINK_SESSION_EVENT_NONE;

  if(session->transport_connected && session->session_id != 0)
    link_session_send_reason(session, LINK_MESSAGE_DISCONNECT, reason, 0);

  link_session_clear_deferred_prepare(session);
  events = link_session_terminate_benchmark(session,
   LINK_BENCHMARK_FAILURE_LOCAL_ABORT);
  events |= link_session_end_barrier(session,
   !link_session_has_committed_transfer(session), 0);
  sio_event = link_session_detach_sio(session);
  if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
    events |= LINK_SESSION_EVENT_SERIAL_IRQ;
  session->reason = reason;
  session->reason_detail = 0;
  session->transport_connected = 0;
  session->transport.ops->disconnect(session->transport.context);
  events |= link_session_set_state(session, LINK_SESSION_DISCONNECTED);
  events |= LINK_SESSION_EVENT_SESSION_CLOSED;
  return events;
}

static int link_session_capabilities_valid(LinkSession *session,
 const LinkHello *peer)
{
  uint32_t negotiated = session->local_hello.capabilities &
   peer->capabilities & LINK_SESSION_SUPPORTED_CAPABILITIES;

  if((negotiated & LINK_SESSION_REQUIRED_CAPABILITIES) !=
     LINK_SESSION_REQUIRED_CAPABILITIES)
    return 0;
  if(session->local_hello.max_players < 2 || peer->max_players < 2)
    return 0;
  if(session->local_hello.rom_size == 0 || peer->rom_size == 0)
    return 0;

  session->negotiated_capabilities = negotiated;
  return 1;
}

static int link_session_sequence_is_current(const LinkSession *session,
 uint32_t sequence)
{
  if(sequence == session->last_sequence)
    return 1;
  return session->barrier_active && sequence == session->active_sequence;
}

static int link_session_sequence_is_next(const LinkSession *session,
 uint32_t sequence)
{
  return sequence == session->last_sequence + 1u;
}

static int link_session_early_transfer_packet_stale(
 const LinkSession *session, const LinkPacket *packet)
{
  uint32_t reference;
  int terminal;

  terminal = packet->type == LINK_MESSAGE_TRANSFER_COMMIT ||
   packet->type == LINK_MESSAGE_TRANSFER_COMMIT_ACK ||
   packet->type == LINK_MESSAGE_TRANSFER_RELEASE ||
   packet->type == LINK_MESSAGE_TRANSFER_RELEASE_ACK ||
   packet->type == LINK_MESSAGE_TRANSFER_ABORT_ACK;
  if(packet->sequence == 0 ||
     (!terminal &&
      packet->type != LINK_MESSAGE_TRANSFER_REQUEST &&
      packet->type != LINK_MESSAGE_TRANSFER_PREPARE &&
      packet->type != LINK_MESSAGE_TRANSFER_ACCEPT &&
      packet->type != LINK_MESSAGE_TRANSFER_RELEASE &&
      packet->type != LINK_MESSAGE_TRANSFER_RELEASE_ACK &&
      packet->type != LINK_MESSAGE_TRANSFER_ABORT))
    return 0;

  /* Outside a barrier the terminal handlers must replay their acknowledgments.
     During the next barrier the same packets are completed history. */
  if(terminal && !session->barrier_active)
    return 0;

  if(packet->type == LINK_MESSAGE_TRANSFER_ABORT &&
     session->transaction_stage == LINK_TRANSACTION_ABORTED &&
     packet->sequence == session->last_sequence)
    return 0;

  reference = session->barrier_active ? session->active_sequence :
   session->last_sequence;
  if(session->barrier_active && packet->sequence == reference)
    return 0;
  return packet->sequence == reference ||
   link_protocol_sequence_after(reference, packet->sequence);
}

static int link_session_legacy_multiplayer_packet(const LinkPacket *packet)
{
  LinkTransferData transfer;

  switch(packet->type)
  {
    case LINK_MESSAGE_TRANSFER_REQUEST:
    case LINK_MESSAGE_TRANSFER_PREPARE:
    case LINK_MESSAGE_TRANSFER_ACCEPT:
    case LINK_MESSAGE_TRANSFER_COMMIT:
    case LINK_MESSAGE_TRANSFER_RELEASE:
    case LINK_MESSAGE_TRANSFER_RELEASE_ACK:
      return link_protocol_read_transfer(packet, &transfer) ==
       LINK_PROTOCOL_OK && transfer.mode == GBA_SIO_MODE_MULTIPLAYER;

    default:
      return 0;
  }
}

static void link_session_transfer_init(LinkSession *session,
 LinkTransferData *transfer, const GbaSioTransfer *sio_transfer)
{
  uint32_t i;

  memset(transfer, 0, sizeof(*transfer));
  transfer->mode = (uint8_t)sio_transfer->mode;
  transfer->setting = sio_transfer->setting;
  transfer->clock_owner = sio_transfer->player_id;
  transfer->player_count = 2;
  transfer->participant_mask = (uint8_t)(1u << sio_transfer->player_id);
  transfer->start_cycle = session->clock.local_cycle;
  transfer->finish_cycle = transfer->start_cycle +
   gba_sio_transfer_cycles(sio_transfer->mode, sio_transfer->setting, 2);
  for(i = 0; i < 4; i++)
    transfer->data[i] = 0xFFFFFFFFu;
  transfer->data[sio_transfer->player_id] = sio_transfer->outgoing;
}

static int link_session_transfer_shape_valid(const LinkTransferData *transfer)
{
  if(transfer->mode > GBA_SIO_MODE_MULTIPLAYER ||
     transfer->player_count != 2 || transfer->clock_owner > 1)
    return 0;
  if(transfer->mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    if(transfer->setting > 3 || transfer->clock_owner != 0)
      return 0;
  }
  else if(transfer->setting > 1)
  {
    return 0;
  }
  if(transfer->finish_cycle - transfer->start_cycle !=
     gba_sio_transfer_cycles((GbaSioMode)transfer->mode,
      transfer->setting, transfer->player_count))
    return 0;
  return 1;
}

static int link_session_transfer_matches(const LinkTransferData *left,
 const LinkTransferData *right)
{
  return left->mode == right->mode &&
   left->setting == right->setting &&
   left->clock_owner == right->clock_owner &&
   left->player_count == right->player_count &&
   left->start_cycle == right->start_cycle &&
   left->finish_cycle == right->finish_cycle;
}

static int link_session_transfer_mode_matches(const LinkTransferData *left,
 const LinkTransferData *right)
{
  return left->mode == right->mode && left->setting == right->setting &&
   left->clock_owner == right->clock_owner &&
   left->player_count == right->player_count;
}

static void link_session_host_schedule_transfer(LinkSession *session,
 LinkTransferData *transfer)
{
  transfer->start_cycle = session->clock.local_cycle;
  transfer->finish_cycle = transfer->start_cycle +
   gba_sio_transfer_cycles((GbaSioMode)transfer->mode, transfer->setting,
    transfer->player_count);
}

static int link_session_transfer_equal(const LinkTransferData *left,
 const LinkTransferData *right)
{
  return link_session_transfer_matches(left, right) &&
   left->participant_mask == right->participant_mask &&
   memcmp(left->data, right->data, sizeof(left->data)) == 0;
}

static int link_session_send_transfer(LinkSession *session,
 LinkMessageType type, const LinkTransferData *transfer, uint32_t sequence)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, type, session->session_id, sequence);
  if(link_protocol_write_transfer(&packet, type, transfer) != LINK_PROTOCOL_OK)
    return 0;
  if(!link_session_send(session, &packet))
    return 0;
  session->legacy_transfer_tx_count++;
  return 1;
}

static int link_session_send_stream_start(LinkSession *session,
 const LinkStreamStart *start, uint32_t sequence)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_START,
   session->session_id, sequence);
  if(link_protocol_write_stream_start(&packet, start) != LINK_PROTOCOL_OK ||
     !link_session_send(session, &packet))
    return 0;
  session->stream_start_tx_count++;
  link_clock_note_published_at(&session->clock, link_session_now(session));
  return 1;
}

static int link_session_send_stream_reply(LinkSession *session,
 const LinkStreamReply *reply, uint32_t sequence)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_REPLY,
   session->session_id, sequence);
  if(link_protocol_write_stream_reply(&packet, reply) != LINK_PROTOCOL_OK ||
     !link_session_send(session, &packet))
    return 0;
  session->stream_reply_tx_count++;
  link_clock_note_published_at(&session->clock, link_session_now(session));
  return 1;
}

static int link_session_send_benchmark_control(LinkSession *session,
 const LinkBenchmarkControl *control)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_BENCHMARK_CONTROL,
   session->session_id, session->last_sequence);
  if(link_protocol_write_benchmark_control(&packet, control) !=
      LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

int link_session_benchmark_available(const LinkSession *session)
{
  return session != NULL && session->role == LINK_ROLE_HOST &&
   session->state == LINK_SESSION_CONNECTED && !session->barrier_active &&
   !gba_sio_is_busy(session->sio) && !gba_sio_is_waiting(session->sio) &&
   (session->negotiated_capabilities &
    LINK_CAPABILITY_STREAM_TRANSFER) != 0 &&
   !link_benchmark_active(&session->benchmark);
}

int link_session_benchmark_start(LinkSession *session,
 const LinkBenchmarkControl *control)
{
  LinkBenchmarkControl outbound;

  if(!link_session_benchmark_available(session))
    return 0;
  if(link_benchmark_terminal(&session->benchmark))
    link_benchmark_init(&session->benchmark);
  if(!link_benchmark_host_start(&session->benchmark, control,
     session->session_id, link_session_now(session), &outbound))
    return 0;
  if(link_session_send_benchmark_control(session, &outbound))
    return 1;
  link_benchmark_fail(&session->benchmark, LINK_BENCHMARK_FAILURE_STREAM,
   link_session_now(session));
  return 0;
}

uint32_t link_session_benchmark_stop(LinkSession *session)
{
  LinkBenchmarkControl outbound;
  uint32_t actions;

  if(session == NULL)
    return LINK_SESSION_EVENT_NONE;
  actions = link_benchmark_request_abort(&session->benchmark,
   link_session_now(session), &outbound);
  if((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0 &&
     !link_session_send_benchmark_control(session, &outbound))
    actions |= link_benchmark_fail(&session->benchmark,
     LINK_BENCHMARK_FAILURE_STREAM, link_session_now(session));
  return actions == LINK_BENCHMARK_ACTION_NONE ? LINK_SESSION_EVENT_NONE :
   LINK_SESSION_EVENT_BENCHMARK_UPDATE;
}

void link_session_benchmark_clear(LinkSession *session)
{
  if(session != NULL && link_benchmark_terminal(&session->benchmark))
    link_benchmark_init(&session->benchmark);
}

void link_session_benchmark_summary(const LinkSession *session,
 LinkBenchmarkSummary *summary)
{
  link_benchmark_summary(session != NULL ? &session->benchmark : NULL,
   summary);
}

static int link_session_send_empty(LinkSession *session,
 LinkMessageType type, uint32_t sequence)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, type, session->session_id, sequence);
  return link_session_send(session, &packet);
}

static int link_session_commit_sio(LinkSession *session,
 const LinkTransferData *transfer, int localized, uint32_t *events)
{
  GbaSioResult result;
  GbaSioEvent sio_event = GBA_SIO_EVENT_NONE;
  uint32_t i;
  uint8_t local_player = session->role == LINK_ROLE_HOST ? 0 : 1;
  int committed;

  memset(&result, 0, sizeof(result));
  result.player_count = 2;
  if(transfer->mode == GBA_SIO_MODE_MULTIPLAYER)
  {
    for(i = 0; i < 4; i++)
      result.data[i] = transfer->data[i] & 0xFFFFu;
  }
  else
  {
    result.data[0] = transfer->data[local_player ^ 1u];
  }
  if(localized)
  {
    committed = gba_sio_commit_at_horizon(session->sio, &result,
     session->barrier_cycle, session->barrier_cycle +
      (transfer->finish_cycle - transfer->start_cycle), &sio_event);
  }
  else
  {
    committed = gba_sio_commit_at(session->sio, &result,
     session->barrier_cycle, session->barrier_cycle +
      (transfer->finish_cycle - transfer->start_cycle));
  }
  if(committed && events != NULL &&
     (sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
    *events |= LINK_SESSION_EVENT_SERIAL_IRQ;
  return committed;
}

static void link_session_journal_record(const LinkSession *session,
 uint32_t hash, int release_shared, LinkJournalRecord *record)
{
  link_journal_snapshot(&session->journal, record);
  record->sequence = session->transaction_sequence;
  record->hash = hash;
  record->start_cycle = session->transfer.start_cycle;
  record->finish_cycle = session->transfer.finish_cycle;
  record->release_shared = release_shared != 0;
}

static int link_session_transaction_begin(LinkSession *session,
 uint32_t sequence)
{
  LinkJournalRecord record;

  if(session == NULL || !session->journal_valid ||
     session->journal.stage != LINK_JOURNAL_EMPTY)
    return 0;
  session->transaction_stage = LINK_TRANSACTION_PROPOSED;
  session->transaction_sequence = sequence;
  session->transaction_hash = 0;
  session->applied_sequence = 0;
  session->applied_hash = 0;
  session->transaction_applied = 0;
  memset(&session->applied_transfer, 0,
   sizeof(session->applied_transfer));
  link_session_journal_record(session, 0u, 0, &record);
  if(!link_journal_apply(&session->journal,
     LINK_JOURNAL_TRANSITION_ARM, &record))
  {
    session->transaction_stage = LINK_TRANSACTION_EMPTY;
    session->transaction_sequence = 0;
    return 0;
  }
  return 1;
}

static int link_session_transaction_prepare(LinkSession *session)
{
  uint32_t hash;

  if(session->transaction_sequence != session->active_sequence ||
     session->transfer.participant_mask != 0x03 ||
     !link_session_transfer_shape_valid(&session->transfer))
    return 0;

  hash = link_session_transfer_hash(&session->transfer);
  if(session->transaction_stage == LINK_TRANSACTION_PREPARED)
    return session->transaction_hash == hash;
  if(session->transaction_stage != LINK_TRANSACTION_PROPOSED)
    return 0;

  session->transaction_hash = hash;
  session->transaction_stage = LINK_TRANSACTION_PREPARED;
  return 1;
}

static int link_session_transaction_decide(LinkSession *session)
{
  LinkJournalRecord record;
  LinkJournalTransitionId transition;

  if(session->transaction_stage != LINK_TRANSACTION_PREPARED ||
     session->transaction_sequence != session->active_sequence ||
     session->transaction_hash !=
      link_session_transfer_hash(&session->transfer) ||
     !gba_sio_can_commit(session->sio))
    return 0;

  transition = session->role == LINK_ROLE_HOST ?
   LINK_JOURNAL_TRANSITION_ISSUE_DECISION :
   LINK_JOURNAL_TRANSITION_RECORD_DECISION;
  link_session_journal_record(session, session->transaction_hash, 0,
   &record);
  if(!link_journal_apply(&session->journal, transition, &record))
    return 0;
  if(!gba_sio_lock_decision(session->sio))
    return 0;
  session->transaction_stage = LINK_TRANSACTION_DECIDED;
  return 1;
}

static int link_session_transaction_release(LinkSession *session)
{
  LinkJournalRecord record;
  LinkJournalTransitionId transition;

  if(session == NULL || session->transaction_stage !=
      LINK_TRANSACTION_DECIDED || session->journal.stage !=
      LINK_JOURNAL_DECIDED)
    return 0;
  transition = session->role == LINK_ROLE_HOST ?
   LINK_JOURNAL_TRANSITION_MARK_RELEASED :
   LINK_JOURNAL_TRANSITION_RECORD_RELEASE;
  link_session_journal_record(session, session->transaction_hash,
   session->role == LINK_ROLE_JOINER, &record);
  return link_journal_apply(&session->journal, transition, &record);
}

static int link_session_transaction_apply(LinkSession *session,
 const LinkTransferData *transfer)
{
  uint32_t hash;

  if(session == NULL || transfer == NULL ||
     session->transaction_stage != LINK_TRANSACTION_DECIDED ||
     session->journal.stage != LINK_JOURNAL_RELEASED ||
     session->transaction_sequence != session->active_sequence)
    return 0;
  hash = link_session_transfer_hash(transfer);
  if(hash != session->transaction_hash ||
     memcmp(transfer, &session->transfer, sizeof(*transfer)) != 0)
    return 0;
  if(session->transaction_applied)
    return session->applied_sequence == session->transaction_sequence &&
     session->applied_hash == hash &&
     memcmp(&session->applied_transfer, transfer, sizeof(*transfer)) == 0;
  if(!link_session_commit_sio(session, transfer, 0, NULL))
    return 0;

  {
    LinkJournalRecord record;

    link_session_journal_record(session, hash,
     session->journal.release_shared, &record);
    if(link_journal_claim_effect(&session->journal, &record) !=
       LINK_JOURNAL_EFFECT_FIRST)
      return 0;
  }

  session->applied_transfer = *transfer;
  session->applied_sequence = session->transaction_sequence;
  session->applied_hash = hash;
  session->transaction_applied = 1;
  return 1;
}

static int link_session_transaction_acknowledge(LinkSession *session)
{
  LinkJournalRecord record;

  if(session->transaction_stage != LINK_TRANSACTION_DECIDED ||
     !session->transaction_applied ||
     session->applied_sequence != session->transaction_sequence ||
     session->journal.stage != LINK_JOURNAL_RELEASED)
    return 0;
  link_session_journal_record(session, session->transaction_hash, 1,
   &record);
  if(!link_journal_apply(&session->journal,
     LINK_JOURNAL_TRANSITION_REPLAY_RELEASE, &record) ||
     !link_journal_apply(&session->journal,
      LINK_JOURNAL_TRANSITION_CLEAR_RELEASED, &record))
    return 0;
  session->transaction_stage = LINK_TRANSACTION_ACKNOWLEDGED;
  return 1;
}

static int link_session_transaction_abort(LinkSession *session,
 uint32_t sequence)
{
  LinkJournalRecord record;

  if(session->transaction_stage == LINK_TRANSACTION_DECIDED ||
     session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED ||
     session->transaction_applied ||
     session->journal.stage != LINK_JOURNAL_ARMED)
    return 0;
  link_session_journal_record(session, 0u, 0, &record);
  if(record.sequence != sequence ||
     !link_journal_apply(&session->journal,
      LINK_JOURNAL_TRANSITION_CANCEL_ARM, &record))
    return 0;
  session->transaction_sequence = sequence;
  session->transaction_stage = LINK_TRANSACTION_ABORTED;
  session->transaction_hash = 0;
  return 1;
}

static uint32_t link_session_finish_joiner_accept(LinkSession *session,
 uint32_t events)
{
  events |= link_session_set_state(session, LINK_SESSION_WAIT_COMMIT);
  if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_ACCEPT,
     &session->transfer, session->active_sequence))
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return events;
}

static int link_session_peer_gameplay_active(const LinkSession *session)
{
  return session->peer_sio_valid != 0 &&
   (session->peer_sio.flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE) != 0;
}

static int link_session_peer_ready(const LinkSession *session,
 const GbaSioTransfer *transfer)
{
  const LinkSioState *peer = &session->peer_sio;
  uint8_t expected_player = session->role == LINK_ROLE_HOST ? 1 : 0;

  if(!session->clock.peer_valid ||
     !link_session_peer_gameplay_active(session) ||
     peer->mode != (uint8_t)transfer->mode || peer->player_count != 2 ||
     peer->player_id != expected_player)
    return 0;

  if(transfer->mode == GBA_SIO_MODE_MULTIPLAYER)
    return session->role == LINK_ROLE_HOST;

  return transfer->mode == GBA_SIO_MODE_NORMAL_8 ||
   transfer->mode == GBA_SIO_MODE_NORMAL_32;
}

uint32_t link_session_begin_transfer(LinkSession *session)
{
  GbaSioTransfer sio_transfer;
  LinkStreamStart stream_start;
  LinkMessageType type;
  uint32_t stream_actions;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint64_t now;

  if(session == NULL || session->state != LINK_SESSION_CONNECTED ||
     session->deferred_prepare_valid ||
     link_benchmark_active(&session->benchmark) ||
     session->transport_health != LINK_TRANSPORT_HEALTH_HEALTHY)
    return events;
  if(!gba_sio_peek_start(session->sio, &sio_transfer))
  {
    session->ready_wait_started_us = 0;
    session->ready_wait_active = 0;
    return events;
  }
  if(!link_session_peer_ready(session, &sio_transfer))
  {
    if(!link_session_peer_gameplay_active(session))
    {
      session->ready_wait_started_us = 0;
      session->ready_wait_active = 0;
      return events;
    }

    now = link_session_now(session);
    if(!session->ready_wait_active)
    {
      session->ready_wait_started_us = now;
      session->ready_wait_active = 1;
    }
    else if(now - session->ready_wait_started_us >=
            LINK_SESSION_READY_TIMEOUT_US)
      events |= link_session_abort_transfer(session,
       LINK_REASON_MODE_MISMATCH, 0, session->last_sequence, 0);
    return events;
  }
  session->ready_wait_started_us = 0;
  session->ready_wait_active = 0;
  if(!gba_sio_take_start(session->sio, &sio_transfer))
    return events;

  if(sio_transfer.player_id > 1 || sio_transfer.player_count != 2)
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);
  if(sio_transfer.mode == GBA_SIO_MODE_MULTIPLAYER &&
     session->role != LINK_ROLE_HOST)
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  session->active_sequence = sio_transfer.mode == GBA_SIO_MODE_MULTIPLAYER &&
   (session->negotiated_capabilities &
    LINK_CAPABILITY_STREAM_TRANSFER) != 0 ?
   link_session_next_stream_sequence(session->last_sequence) :
   session->last_sequence + 1u;
  link_session_transfer_init(session, &session->transfer, &sio_transfer);
  if(sio_transfer.mode == GBA_SIO_MODE_MULTIPLAYER &&
     (session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) != 0)
  {
    if(session->role != LINK_ROLE_HOST)
      return link_session_fail(session, LINK_REASON_INCOMPATIBLE, 0, 1);

    link_session_clear_legacy_transaction(session);
    link_session_sync_stream_checkpoint(session);

    memset(&stream_start, 0, sizeof(stream_start));
    stream_start.generation = link_stream_generation(&session->stream);
    stream_start.start_cycle = session->transfer.start_cycle;
    stream_start.finish_cycle = session->transfer.finish_cycle;
    stream_start.clock_cycle = session->clock.local_cycle;
    stream_start.acknowledged_sequence = session->last_sequence;
    stream_start.outgoing = sio_transfer.outgoing;
    stream_start.identity = link_session_transfer_hash(&session->transfer) ^
     session->session_id ^ session->active_sequence;
    if(stream_start.identity == 0)
      stream_start.identity = 1u;
    stream_start.mode = (uint8_t)GBA_SIO_MODE_MULTIPLAYER;
    stream_start.setting = sio_transfer.setting;
    stream_start.player_count = 2u;
    stream_start.flags = LINK_STREAM_FLAG_CLOCK_OWNER;

    stream_actions = link_stream_host_begin(&session->stream,
     session->active_sequence, &stream_start);
    if(stream_actions != LINK_STREAM_ACTION_SEND_START)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

    session->barrier_active = 1;
    session->barrier_cycle = session->sio->current_cycle;
    session->barrier_started_us = link_session_now(session);
    link_clock_mark_hot(&session->clock, session->barrier_started_us);
    events |= LINK_SESSION_EVENT_BARRIER_BEGIN;
    events |= link_session_set_state(session,
     LINK_SESSION_WAIT_STREAM_REPLY);
    if(!link_session_send_stream_start(session, &stream_start,
       session->active_sequence))
      events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return events;
  }

  if(session->role == LINK_ROLE_HOST)
  {
    if(!link_session_transaction_begin(session, session->active_sequence))
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  }
  else
  {
    session->transaction_stage = LINK_TRANSACTION_PROPOSED;
    session->transaction_sequence = session->active_sequence;
    session->transaction_hash = 0;
    session->applied_sequence = 0;
    session->applied_hash = 0;
    session->transaction_applied = 0;
    memset(&session->applied_transfer, 0,
     sizeof(session->applied_transfer));
  }
  session->barrier_active = 1;
  session->barrier_cycle = session->sio->current_cycle;
  session->barrier_started_us = link_session_now(session);
  link_clock_mark_hot(&session->clock, session->barrier_started_us);
  events |= LINK_SESSION_EVENT_BARRIER_BEGIN;

  if(session->role == LINK_ROLE_HOST)
  {
    type = LINK_MESSAGE_TRANSFER_PREPARE;
    events |= link_session_set_state(session, LINK_SESSION_WAIT_ACCEPT);
  }
  else
  {
    type = LINK_MESSAGE_TRANSFER_REQUEST;
    events |= link_session_set_state(session, LINK_SESSION_WAIT_PREPARE);
  }

  if(!link_session_send_transfer(session, type, &session->transfer,
     session->active_sequence))
  {
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  }
  return events;
}

static int link_session_send_sio_state(LinkSession *session,
 const LinkSioState *state)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, LINK_MESSAGE_SIO_STATE,
   session->session_id, session->last_sequence);
  if(link_protocol_write_sio_state(&packet, state) != LINK_PROTOCOL_OK ||
     !link_session_send(session, &packet))
    return 0;

  session->local_sio_sent = *state;
  session->local_sio_sent_valid = 1;
  session->last_sio_publish_us = link_session_now(session);
  return 1;
}

/* Latest-wins delivery is not reliable delivery. Refresh even unchanged menu
   presence so losing the final update cannot permanently hide a cable peer. */
static uint32_t link_session_refresh_sio_presence(LinkSession *session,
 uint64_t now)
{
  if(session->state != LINK_SESSION_CONNECTED || !session->local_sio_valid ||
     (now >= session->last_sio_publish_us &&
      now - session->last_sio_publish_us < LINK_SESSION_HEARTBEAT_US))
    return LINK_SESSION_EVENT_NONE;
  if(!link_session_send_sio_state(session, &session->local_sio_latest))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  /* Presence traffic must not starve the correlated liveness probe. */
  return link_session_send_heartbeat_probe(session);
}

uint32_t link_session_publish_sio_state(LinkSession *session,
 const LinkSioState *state)
{
  uint8_t presence_changed;

  if(session == NULL || state == NULL || !link_session_is_connected(session))
    return LINK_SESSION_EVENT_NONE;

  session->local_sio_latest = *state;
  session->local_sio_valid = 1;
  if(link_session_sio_state_hot(state))
    link_clock_mark_hot(&session->clock, link_session_now(session));

  if(session->barrier_active)
  {
    if(session->transport_health != LINK_TRANSPORT_HEALTH_HEALTHY)
      return LINK_SESSION_EVENT_NONE;
    if((session->transaction_stage == LINK_TRANSACTION_PROPOSED ||
        session->transaction_stage == LINK_TRANSACTION_PREPARED) &&
       state->mode != session->transfer.mode)
      return link_session_abort_or_recover(session,
       LINK_REASON_MODE_MISMATCH, 0, session->active_sequence, 1,
       LINK_TIMEOUT_NONE);
    return LINK_SESSION_EVENT_NONE;
  }
  if(session->state != LINK_SESSION_CONNECTED)
    return LINK_SESSION_EVENT_NONE;
  if(session->local_sio_sent_valid &&
     memcmp(&session->local_sio_sent, state, sizeof(*state)) == 0)
    return LINK_SESSION_EVENT_NONE;

  session->sio_revision++;
  if(session->sio_revision == 0)
    session->sio_revision = 1u;

  presence_changed = session->local_sio_sent_valid &&
   ((session->local_sio_sent.flags ^ state->flags) &
    LINK_SIO_FLAG_GAMEPLAY_ACTIVE) != 0;
  if(link_session_peer_in_menu(session) &&
     session->local_sio_sent_valid && !presence_changed)
    return LINK_SESSION_EVENT_NONE;

  if(!link_session_send_sio_state(session, state))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return LINK_SESSION_EVENT_NONE;
}

void link_session_set_emulated_cycle(LinkSession *session,
 uint32_t emulated_cycle)
{
  if(session != NULL)
  {
    link_clock_set_local(&session->clock, emulated_cycle);
    (void)gba_sio_set_cycle(session->sio, emulated_cycle);
  }
}

uint32_t link_session_publish_clock(LinkSession *session,
 uint32_t emulated_cycle)
{
  LinkClockSync clock;
  LinkPacket packet;

  if(session == NULL)
    return LINK_SESSION_EVENT_NONE;
  link_clock_set_local(&session->clock, emulated_cycle);
  if(link_session_sio_hot(session))
    link_clock_mark_hot(&session->clock, link_session_now(session));
  if(session->state != LINK_SESSION_CONNECTED ||
     !session->local_sio_valid ||
     (session->local_sio_latest.flags &
      LINK_SIO_FLAG_GAMEPLAY_ACTIVE) == 0)
    return LINK_SESSION_EVENT_NONE;

  if((session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) != 0)
    return link_session_send_heartbeat_probe(session);

  if(!link_clock_should_publish(&session->clock))
    return LINK_SESSION_EVENT_NONE;

  clock.generation = session->session_id;
  clock.cycle = emulated_cycle;
  clock.last_sequence = session->last_sequence;
  link_protocol_packet_init(&packet, LINK_MESSAGE_CLOCK_SYNC,
   session->session_id, session->last_sequence);
  if(link_protocol_write_clock(&packet, &clock) != LINK_PROTOCOL_OK ||
     !link_session_send(session, &packet))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  link_clock_note_published(&session->clock);
  return LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_handle_hello(LinkSession *session,
 const LinkPacket *packet)
{
  LinkHello hello;
  LinkSessionReady ready;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(packet->sequence != 0 ||
     link_protocol_read_hello(packet, &hello) != LINK_PROTOCOL_OK)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(session->role == LINK_ROLE_JOINER && session->session_id == 0 &&
     session->state == LINK_SESSION_NEGOTIATING)
  {
    if(packet->session_id == 0)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
    session->session_id = packet->session_id;
    link_journal_init(&session->journal, LINK_JOURNAL_ROLE_JOINER,
     session->session_id, 1u, 0u, 0u);
    link_recovery_coordinator_init(&session->recovery,
     session->session_id, 1u);
    session->journal_valid = 1;
    link_clock_init(&session->clock, session->session_id,
     session->clock.local_cycle);
  }

  if(packet->session_id != session->session_id ||
     session->state != LINK_SESSION_NEGOTIATING)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_session_capabilities_valid(session, &hello))
    return link_session_fail(session, LINK_REASON_INCOMPATIBLE, 0, 1);

  session->peer_hello = hello;
  session->peer_minor = packet->minor;

  if(session->role == LINK_ROLE_HOST)
  {
    ready.capabilities = session->negotiated_capabilities;
    ready.player_id = 1;
    ready.player_count = 2;
    if(!link_session_send_ready(session, &ready))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    events |= link_session_set_state(session, LINK_SESSION_WAIT_READY_ACK);
  }
  else
  {
    if(!link_session_send_hello(session))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    events |= link_session_set_state(session, LINK_SESSION_WAIT_READY);
  }
  return events;
}

static int link_session_ready_valid(const LinkSession *session,
 const LinkSessionReady *ready)
{
  return ready->capabilities == session->negotiated_capabilities &&
   ready->player_id == 1 && ready->player_count == 2;
}

static uint32_t link_session_handle_ready(LinkSession *session,
 const LinkPacket *packet)
{
  LinkSessionReady ready;
  LinkStreamRole stream_role;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(packet->session_id != session->session_id || packet->sequence != 0 ||
     link_protocol_read_session_ready(packet, &ready) != LINK_PROTOCOL_OK ||
     !link_session_ready_valid(session, &ready))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  stream_role = session->role == LINK_ROLE_HOST ? LINK_STREAM_ROLE_HOST :
   LINK_STREAM_ROLE_JOINER;
  link_stream_init_at(&session->stream, stream_role, session->session_id,
   session->last_sequence);

  if(session->role == LINK_ROLE_JOINER &&
     session->state == LINK_SESSION_WAIT_READY)
  {
    gba_sio_set_connection(session->sio, 1, 2);
    if(!link_session_send_ready(session, &ready))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  }
  else if(session->role == LINK_ROLE_HOST &&
          session->state == LINK_SESSION_WAIT_READY_ACK)
  {
    gba_sio_set_connection(session->sio, 0, 2);
    events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  }
  else
  {
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }
  return events;
}

static uint32_t link_session_handle_sio_state(LinkSession *session,
 const LinkPacket *packet)
{
  LinkSioState state;
  int peer_was_in_menu;
  uint32_t reference;

  if(!link_session_is_connected(session) ||
     link_protocol_read_sio_state(packet, &state) != LINK_PROTOCOL_OK ||
     state.mode > GBA_SIO_MODE_JOYBUS || state.player_count != 2 ||
     state.player_id > 1)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(!link_session_sequence_is_current(session, packet->sequence))
  {
    reference = session->barrier_active ? session->active_sequence :
     session->last_sequence;
    if(link_protocol_sequence_after(reference, packet->sequence))
      return LINK_SESSION_EVENT_NONE;
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }

  peer_was_in_menu = link_session_peer_in_menu(session);
  session->peer_sio = state;
  session->peer_sio_valid = 1;
  if(link_session_sio_state_hot(&state))
    link_clock_mark_hot(&session->clock, link_session_now(session));
  gba_sio_set_peer_state(session->sio, (GbaSioMode)state.mode,
   state.control, state.rcnt,
   (state.flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE) != 0 &&
    session->clock.peer_valid);
  if(peer_was_in_menu && !link_session_peer_in_menu(session) &&
     session->local_sio_valid &&
     (!session->local_sio_sent_valid ||
      memcmp(&session->local_sio_latest, &session->local_sio_sent,
       sizeof(session->local_sio_latest)) != 0) &&
     !link_session_send_sio_state(session, &session->local_sio_latest))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_handle_clock(LinkSession *session,
 const LinkPacket *packet)
{
  LinkClockSync clock;
  int had_peer_clock;

  if(!link_session_is_connected(session) ||
     link_protocol_read_clock(packet, &clock) != LINK_PROTOCOL_OK ||
     clock.generation != session->session_id ||
     packet->sequence != clock.last_sequence)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(link_protocol_sequence_after(clock.last_sequence,
      session->active_sequence))
    return LINK_SESSION_EVENT_NONE;
  had_peer_clock = session->clock.peer_valid != 0;
  link_clock_note_peer(&session->clock, clock.generation, clock.cycle,
   link_session_now(session));
  if(!had_peer_clock && session->peer_sio_valid)
  {
    gba_sio_set_peer_state(session->sio,
     (GbaSioMode)session->peer_sio.mode, session->peer_sio.control,
     session->peer_sio.rcnt,
     (session->peer_sio.flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE) != 0);
    return LINK_SESSION_EVENT_SIO_REPUBLISH;
  }
  return LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_finish_stream_apply(LinkSession *session,
 uint32_t sequence)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint64_t now;
  uint64_t duration;

  if(!link_session_commit_sio(session, &session->transfer, 1, &events) ||
     link_stream_note_applied(&session->stream, sequence) !=
      LINK_STREAM_ACTION_COMMITTED)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  session->stream_local_start_cycle = session->barrier_cycle;
  session->stream_start_received_us = 0;
  session->last_sequence = sequence;
  session->applied_sequence = sequence;
  session->applied_transfer = session->transfer;
  session->applied_hash = link_session_transfer_hash(&session->transfer);
  now = link_session_now(session);
  if(session->stream_burst_words == 0)
    session->stream_burst_started_us = session->barrier_started_us;
  if(session->stream_burst_words != UINT32_MAX)
    session->stream_burst_words++;
  if(session->stream_burst_words >= 9u)
  {
    duration = now >= session->stream_burst_started_us ?
     now - session->stream_burst_started_us : 0;
    session->stream_burst_last_us = duration;
    if(duration > session->stream_burst_max_us)
      session->stream_burst_max_us = duration;
    session->stream_burst_words = 0;
    session->stream_burst_started_us = 0;
  }
  events |= link_session_end_barrier(session, 0, 0);
  events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  return events;
}

static uint32_t link_session_benchmark_stop_peer(LinkSession *session)
{
  LinkBenchmarkControl stop;

  memset(&stop, 0, sizeof(stop));
  stop.run_id = session->benchmark.control.run_id;
  stop.action = LINK_BENCHMARK_STOP;
  link_session_send_benchmark_control(session, &stop);
  return LINK_SESSION_EVENT_BENCHMARK_UPDATE;
}

static uint32_t link_session_handle_benchmark_control(LinkSession *session,
 const LinkPacket *packet)
{
  LinkBenchmarkControl control;
  LinkBenchmarkControl outbound;
  LinkBenchmarkRole role;
  uint32_t actions;

  if((packet->flags & LINK_PROTOCOL_FLAG_OPTIONAL) == 0 ||
     link_protocol_read_benchmark_control(packet, &control) !=
      LINK_PROTOCOL_OK || session->state != LINK_SESSION_CONNECTED ||
     session->barrier_active)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  role = session->role == LINK_ROLE_HOST ? LINK_BENCHMARK_ROLE_HOST :
   LINK_BENCHMARK_ROLE_JOINER;
  actions = link_benchmark_receive_control(&session->benchmark, role,
   &control, session->session_id, link_session_now(session), &outbound);
  if((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0 &&
     !link_session_send_benchmark_control(session, &outbound))
  {
    link_benchmark_fail(&session->benchmark,
     LINK_BENCHMARK_FAILURE_STREAM, link_session_now(session));
    return LINK_SESSION_EVENT_BENCHMARK_UPDATE;
  }
  return actions == LINK_BENCHMARK_ACTION_NONE ? LINK_SESSION_EVENT_NONE :
   LINK_SESSION_EVENT_BENCHMARK_UPDATE;
}

static uint32_t link_session_handle_benchmark_start(LinkSession *session,
 const LinkPacket *packet)
{
  LinkStreamStart start;
  LinkStreamReply reply;
  uint32_t actions;

  if(session->role != LINK_ROLE_JOINER ||
     link_protocol_read_stream_start(packet, &start) != LINK_PROTOCOL_OK)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  link_clock_note_peer(&session->clock, start.generation,
   start.clock_cycle, link_session_now(session));
  actions = link_benchmark_join_receive_start(&session->benchmark,
   packet->sequence, &start, link_session_now(session),
   session->clock.local_cycle, &reply);
  if((actions & LINK_BENCHMARK_ACTION_SEND_REPLY) != 0 &&
     !link_session_send_stream_reply(session, &reply, packet->sequence))
  {
    link_benchmark_fail(&session->benchmark,
     LINK_BENCHMARK_FAILURE_STREAM, link_session_now(session));
    return link_session_benchmark_stop_peer(session);
  }
  if((actions & LINK_BENCHMARK_ACTION_TERMINAL) != 0)
    return link_session_benchmark_stop_peer(session);
  return actions == LINK_BENCHMARK_ACTION_NONE ? LINK_SESSION_EVENT_NONE :
   LINK_SESSION_EVENT_BENCHMARK_UPDATE;
}

static uint32_t link_session_handle_benchmark_reply(LinkSession *session,
 const LinkPacket *packet)
{
  LinkStreamReply reply;
  uint32_t actions;

  if(session->role != LINK_ROLE_HOST ||
     link_protocol_read_stream_reply(packet, &reply) != LINK_PROTOCOL_OK)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  link_clock_note_peer(&session->clock, reply.generation,
   reply.clock_cycle, link_session_now(session));
  actions = link_benchmark_host_receive_reply(&session->benchmark,
   packet->sequence, &reply, link_session_now(session));
  if((actions & LINK_BENCHMARK_ACTION_TERMINAL) != 0)
    return link_session_benchmark_stop_peer(session);
  return actions == LINK_BENCHMARK_ACTION_NONE ? LINK_SESSION_EVENT_NONE :
   LINK_SESSION_EVENT_BENCHMARK_UPDATE;
}

static uint32_t link_session_service_stream_start(LinkSession *session)
{
  LinkStreamStart start;
  LinkStreamReply reply;
  GbaSioTransfer local;
  uint32_t sequence;
  uint32_t actions;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint32_t index;

  if(session->state != LINK_SESSION_CONNECTED ||
     session->transport_health != LINK_TRANSPORT_HEALTH_HEALTHY ||
     link_stream_state(&session->stream) != LINK_STREAM_STATE_JOIN_WAIT_LATCH)
    return LINK_SESSION_EVENT_NONE;
  if(link_session_now(session) - session->stream_start_received_us >=
      LINK_SESSION_TRANSFER_TIMEOUT_US)
    return link_session_request_transport_recovery(session,
     LINK_REASON_TIMEOUT, 0, LINK_TIMEOUT_ACTIVE_TRANSFER);
  if(gba_sio_is_busy(session->sio) ||
     link_session_stream_latch_delay(session) != 0)
    return LINK_SESSION_EVENT_NONE;

  start = session->stream.active_start;
  sequence = session->stream.active_sequence;
  if(!gba_sio_latch_remote_start(session->sio,
      GBA_SIO_MODE_MULTIPLAYER, start.setting, &local))
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  link_session_clear_legacy_transaction(session);
  memset(&session->transfer, 0, sizeof(session->transfer));
  session->transfer.mode = (uint8_t)GBA_SIO_MODE_MULTIPLAYER;
  session->transfer.setting = start.setting;
  session->transfer.clock_owner = 0u;
  session->transfer.player_count = 2u;
  session->transfer.participant_mask = 0x03u;
  session->transfer.start_cycle = start.start_cycle;
  session->transfer.finish_cycle = start.finish_cycle;
  for(index = 0; index < 4u; index++)
    session->transfer.data[index] = 0xFFFFFFFFu;
  session->transfer.data[0] = start.outgoing;
  session->transfer.data[1] = local.outgoing;
  session->active_sequence = sequence;
  session->barrier_active = 1;
  session->barrier_cycle = session->sio->current_cycle;
  session->barrier_started_us = link_session_now(session);
  link_clock_mark_hot(&session->clock, session->barrier_started_us);
  events |= LINK_SESSION_EVENT_BARRIER_BEGIN;

  actions = link_stream_join_latched(&session->stream, sequence,
   local.outgoing, session->clock.local_cycle, &reply);
  if((actions & (LINK_STREAM_ACTION_SEND_REPLY |
      LINK_STREAM_ACTION_APPLY_JOIN)) !=
     (LINK_STREAM_ACTION_SEND_REPLY | LINK_STREAM_ACTION_APPLY_JOIN))
    return events | link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_session_send_stream_reply(session, &reply, sequence))
    return events | link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return events | link_session_finish_stream_apply(session, sequence);
}

static uint32_t link_session_handle_stream_start(LinkSession *session,
 const LinkPacket *packet)
{
  LinkStreamStart start;
  LinkStreamReply reply;
  uint32_t actions;

  if(session->role != LINK_ROLE_JOINER ||
     (session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) == 0)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(link_session_transport_recovering(session))
    return LINK_SESSION_EVENT_NONE;
  if(link_protocol_read_stream_start(packet, &start) != LINK_PROTOCOL_OK ||
     start.mode != GBA_SIO_MODE_MULTIPLAYER ||
     start.player_count != 2u ||
     start.flags != LINK_STREAM_FLAG_CLOCK_OWNER ||
     start.finish_cycle - start.start_cycle !=
      gba_sio_transfer_cycles(GBA_SIO_MODE_MULTIPLAYER, start.setting, 2u))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  link_session_sync_stream_checkpoint(session);
  actions = link_stream_join_receive_start(&session->stream,
   packet->sequence, &start, &reply);
  if((actions & LINK_STREAM_ACTION_STALE_GENERATION) != 0)
    return LINK_SESSION_EVENT_NONE;
  link_stream_acknowledge(&session->stream,
   start.acknowledged_sequence);
  link_clock_note_peer(&session->clock, start.generation,
   start.clock_cycle, link_session_now(session));
  if((actions & LINK_STREAM_ACTION_REPLAY_REPLY) != 0)
  {
    if(!link_session_send_stream_reply(session, &reply, packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return LINK_SESSION_EVENT_NONE;
  }
  if((actions & (LINK_STREAM_ACTION_CONFLICT |
      LINK_STREAM_ACTION_FUTURE)) != 0)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if((actions & LINK_STREAM_ACTION_LATCH_JOIN) == 0)
    return LINK_SESSION_EVENT_NONE;
  if(session->state != LINK_SESSION_CONNECTED)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  session->stream_start_received_us = link_session_now(session);
  return link_session_service_stream_start(session);
}

static uint32_t link_session_handle_stream_reply(LinkSession *session,
 const LinkPacket *packet)
{
  LinkStreamReply reply;
  uint32_t actions;

  if(session->role != LINK_ROLE_HOST ||
     (session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) == 0)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(link_session_transport_recovering(session))
    return LINK_SESSION_EVENT_NONE;
  if(link_protocol_read_stream_reply(packet, &reply) != LINK_PROTOCOL_OK)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  actions = link_stream_host_receive_reply(&session->stream,
   packet->sequence, &reply);
  if((actions & LINK_STREAM_ACTION_STALE_GENERATION) != 0)
    return LINK_SESSION_EVENT_NONE;
  link_clock_note_peer(&session->clock, reply.generation,
   reply.clock_cycle, link_session_now(session));
  if((actions & (LINK_STREAM_ACTION_CONFLICT |
      LINK_STREAM_ACTION_FUTURE)) != 0)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if((actions & LINK_STREAM_ACTION_APPLY_HOST) == 0)
    return LINK_SESSION_EVENT_NONE;
  if(session->state != LINK_SESSION_WAIT_STREAM_REPLY ||
     !session->barrier_active ||
     packet->sequence != session->active_sequence)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  session->transfer.data[1] = reply.outgoing;
  session->transfer.participant_mask = 0x03u;
  return link_session_finish_stream_apply(session, packet->sequence);
}

static uint32_t link_session_handle_request(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  GbaSioTransfer local;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_HOST ||
     link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK ||
     !link_session_transfer_shape_valid(&transfer) ||
     transfer.mode == GBA_SIO_MODE_MULTIPLAYER ||
     transfer.clock_owner != 1 || transfer.participant_mask != 0x02)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(session->state == LINK_SESSION_CONNECTED &&
     packet->sequence == session->last_sequence &&
     session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED &&
     session->transaction_applied &&
     session->journal.stage == LINK_JOURNAL_EMPTY &&
     session->journal.checkpoint == packet->sequence &&
     link_session_transfer_mode_matches(&transfer,
      &session->applied_transfer) &&
     transfer.data[1] == session->applied_transfer.data[1])
  {
    if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_PREPARE,
       &session->applied_transfer, packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return events;
  }

  if(session->state == LINK_SESSION_WAIT_ACCEPT &&
     packet->sequence == session->active_sequence &&
     session->transaction_stage == LINK_TRANSACTION_PREPARED &&
     session->transfer.clock_owner == 1 &&
     session->transfer.participant_mask == 0x03 &&
     link_session_transfer_mode_matches(&transfer, &session->transfer) &&
     transfer.data[1] == session->transfer.data[1])
  {
    if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_PREPARE,
       &session->transfer, session->active_sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return LINK_SESSION_EVENT_NONE;
  }

  if(session->state != LINK_SESSION_CONNECTED ||
     !link_session_sequence_is_next(session, packet->sequence))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(!gba_sio_latch_remote_start(session->sio,
     (GbaSioMode)transfer.mode, transfer.setting, &local))
  {
    session->active_sequence = packet->sequence;
    return link_session_abort_transfer(session, LINK_REASON_MODE_MISMATCH,
     0, packet->sequence, 1);
  }

  transfer.data[0] = local.outgoing;
  transfer.participant_mask = 0x03;
  link_session_host_schedule_transfer(session, &transfer);
  session->transfer = transfer;
  session->active_sequence = packet->sequence;
  if(!link_session_transaction_begin(session, session->active_sequence))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_session_transaction_prepare(session))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  session->barrier_active = 1;
  session->barrier_cycle = session->sio->current_cycle;
  session->barrier_started_us = link_session_now(session);
  link_clock_mark_hot(&session->clock, session->barrier_started_us);
  events |= LINK_SESSION_EVENT_BARRIER_BEGIN;

  if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_PREPARE,
     &session->transfer, session->active_sequence))
    return events | link_session_fail(session, LINK_REASON_QUEUE_FULL,
     0, 0);

  events |= link_session_set_state(session, LINK_SESSION_WAIT_ACCEPT);
  return events;
}

static int link_session_defer_prepare(LinkSession *session,
 const LinkTransferData *transfer, uint32_t sequence)
{
  if(session->deferred_prepare_valid)
    return session->deferred_prepare_sequence == sequence &&
     link_session_transfer_equal(&session->deferred_prepare, transfer);

  session->deferred_prepare = *transfer;
  session->deferred_prepare_sequence = sequence;
  session->deferred_prepare_valid = 1;
  session->deferred_prepare_idle_seen = 0;
  return 1;
}

static uint32_t link_session_accept_host_prepare(LinkSession *session,
 LinkTransferData transfer, uint32_t sequence)
{
  GbaSioTransfer local;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(!gba_sio_latch_remote_start(session->sio,
     (GbaSioMode)transfer.mode, transfer.setting, &local))
  {
    session->active_sequence = sequence;
    return link_session_abort_transfer(session, LINK_REASON_MODE_MISMATCH,
     0, sequence, 1);
  }

  transfer.data[1] = local.outgoing;
  transfer.participant_mask = 0x03;
  link_session_clear_deferred_prepare(session);
  session->transfer = transfer;
  session->active_sequence = sequence;
  if(!link_session_transaction_begin(session, sequence))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  session->barrier_active = 1;
  session->barrier_cycle = session->sio->current_cycle;
  session->barrier_started_us = link_session_now(session);
  link_clock_mark_hot(&session->clock, session->barrier_started_us);
  events |= LINK_SESSION_EVENT_BARRIER_BEGIN;

  if(!link_session_transaction_prepare(session))
    return events | link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  return link_session_finish_joiner_accept(session, events);
}

static uint32_t link_session_service_deferred_prepare(LinkSession *session)
{
  if(!session->deferred_prepare_valid ||
     session->state != LINK_SESSION_CONNECTED ||
     session->role != LINK_ROLE_JOINER)
    return LINK_SESSION_EVENT_NONE;

  if(gba_sio_is_busy(session->sio))
  {
    session->deferred_prepare_idle_seen = 0;
    return LINK_SESSION_EVENT_NONE;
  }

  if(!session->deferred_prepare_idle_seen)
  {
    session->deferred_prepare_idle_seen = 1;
    return LINK_SESSION_EVENT_NONE;
  }

  return link_session_accept_host_prepare(session,
   session->deferred_prepare, session->deferred_prepare_sequence);
}

static uint32_t link_session_handle_prepare(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_JOINER ||
     link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK ||
     !link_session_transfer_shape_valid(&transfer))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(session->state == LINK_SESSION_CONNECTED &&
     packet->sequence == session->last_sequence &&
     session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED &&
     session->transaction_applied &&
     session->journal.stage == LINK_JOURNAL_EMPTY &&
     session->journal.checkpoint == packet->sequence &&
     link_session_transfer_matches(&transfer, &session->applied_transfer) &&
     transfer.data[0] == session->applied_transfer.data[0] &&
     ((transfer.clock_owner == 0 && transfer.participant_mask == 0x01) ||
      (transfer.clock_owner == 1 && transfer.participant_mask == 0x03 &&
       transfer.data[1] == session->applied_transfer.data[1])))
  {
    if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_ACCEPT,
       &session->applied_transfer, packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return LINK_SESSION_EVENT_NONE;
  }

  if(session->state == LINK_SESSION_WAIT_COMMIT &&
     packet->sequence == session->active_sequence &&
     link_session_transfer_matches(&transfer, &session->transfer) &&
     transfer.data[0] == session->transfer.data[0] &&
     ((transfer.clock_owner == 0 && transfer.participant_mask == 0x01) ||
      (transfer.clock_owner == 1 && transfer.participant_mask == 0x03 &&
       transfer.data[1] == session->transfer.data[1])))
  {
    if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_ACCEPT,
       &session->transfer, session->active_sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return LINK_SESSION_EVENT_NONE;
  }

  if(session->state == LINK_SESSION_CONNECTED)
  {
    if(!link_session_sequence_is_next(session, packet->sequence) ||
       transfer.clock_owner != 0 || transfer.participant_mask != 0x01)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

    if(session->deferred_prepare_valid || gba_sio_is_busy(session->sio))
    {
      if(!link_session_defer_prepare(session, &transfer,
         packet->sequence))
        return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
      return LINK_SESSION_EVENT_NONE;
    }

    return link_session_accept_host_prepare(session, transfer,
     packet->sequence);
  }
  else if(session->state == LINK_SESSION_WAIT_PREPARE)
  {
    if(packet->sequence != session->active_sequence ||
       transfer.clock_owner != 1 || transfer.participant_mask != 0x03 ||
       !link_session_transfer_mode_matches(&transfer, &session->transfer) ||
       transfer.data[1] != session->transfer.data[1])
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
    session->transfer = transfer;
    if(!link_session_transaction_begin(session, packet->sequence))
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }
  else
  {
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }

  session->transfer = transfer;
  if(!link_session_transaction_prepare(session))
    return events | link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  return link_session_finish_joiner_accept(session, events);
}

static uint32_t link_session_handle_accept(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint16_t invalid = 0;

  memset(&transfer, 0, sizeof(transfer));
  if(session->role != LINK_ROLE_HOST)
    invalid |= 1u << 0;
  if(packet->sequence != session->active_sequence)
    invalid |= 1u << 1;
  if(link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK)
    invalid |= 1u << 2;
  else
  {
    if(!link_session_transfer_shape_valid(&transfer))
      invalid |= 1u << 3;
    if(transfer.participant_mask != 0x03)
      invalid |= 1u << 4;
    if(!link_session_transfer_matches(&transfer, &session->transfer))
      invalid |= 1u << 5;
    if(transfer.data[0] != session->transfer.data[0])
      invalid |= 1u << 6;
    if((session->transfer.participant_mask & 0x02) != 0 &&
       transfer.data[1] != session->transfer.data[1])
      invalid |= 1u << 7;
  }
  if(invalid != 0)
    return link_session_fail(session, LINK_REASON_PROTOCOL, invalid, 1);

  if(session->state == LINK_SESSION_CONNECTED &&
     packet->sequence == session->last_sequence &&
     session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED &&
     session->transaction_applied &&
     session->journal.stage == LINK_JOURNAL_EMPTY &&
     session->journal.checkpoint == packet->sequence &&
     session->journal.checkpoint_hash ==
      link_session_transfer_hash(&transfer) &&
     link_session_transfer_equal(&transfer, &session->applied_transfer))
    return LINK_SESSION_EVENT_NONE;

  if(session->state == LINK_SESSION_WAIT_COMMIT_ACK)
  {
    if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_COMMIT,
       &session->transfer, session->active_sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return LINK_SESSION_EVENT_NONE;
  }
  if(session->state != LINK_SESSION_WAIT_ACCEPT)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 1u << 8, 1);

  session->transfer = transfer;
  if(!link_session_transaction_prepare(session) ||
     !gba_sio_can_commit(session->sio))
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);
  if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_COMMIT,
     &session->transfer, session->active_sequence))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  if(!link_session_transaction_decide(session))
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  events |= link_session_set_state(session, LINK_SESSION_WAIT_COMMIT_ACK);
  return events;
}

static uint32_t link_session_handle_commit(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_JOINER ||
     packet->sequence != session->active_sequence ||
     link_protocol_read_transfer(packet, &transfer) != LINK_PROTOCOL_OK ||
     transfer.participant_mask != 0x03 ||
     !link_session_transfer_matches(&transfer, &session->transfer) ||
     memcmp(transfer.data, session->transfer.data,
      sizeof(transfer.data)) != 0)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(session->state == LINK_SESSION_CONNECTED &&
     packet->sequence == session->last_sequence &&
     session->journal.stage == LINK_JOURNAL_EMPTY &&
     session->journal.checkpoint == packet->sequence &&
     session->journal.checkpoint_hash ==
      link_session_transfer_hash(&transfer))
  {
    if(!link_session_send_empty(session, LINK_MESSAGE_TRANSFER_COMMIT_ACK,
       packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return LINK_SESSION_EVENT_NONE;
  }
  if(session->state == LINK_SESSION_WAIT_COMMIT_RELEASE &&
     session->journal.stage == LINK_JOURNAL_DECIDED &&
     packet->sequence == session->active_sequence)
  {
    if(!link_session_send_empty(session, LINK_MESSAGE_TRANSFER_COMMIT_ACK,
       packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return events;
  }
  if(session->state != LINK_SESSION_WAIT_COMMIT ||
     !link_session_transaction_decide(session))
    return link_session_fail(session, LINK_REASON_MODE_MISMATCH, 0, 1);

  if(!link_session_send_empty(session, LINK_MESSAGE_TRANSFER_COMMIT_ACK,
     packet->sequence))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  events |= link_session_set_state(session,
   LINK_SESSION_WAIT_COMMIT_RELEASE);
  return events;
}

static uint32_t link_session_handle_commit_ack(LinkSession *session,
 const LinkPacket *packet)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(packet->payload_size != 0 || packet->sequence != session->active_sequence)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(session->role == LINK_ROLE_HOST)
  {
    if(session->state == LINK_SESSION_CONNECTED &&
       session->journal.stage == LINK_JOURNAL_EMPTY &&
       session->journal.checkpoint == packet->sequence)
      return events;
    if(session->state == LINK_SESSION_WAIT_RELEASE_ACK &&
       session->journal.stage == LINK_JOURNAL_RELEASED)
    {
      if(!link_session_send_transfer(session,
         LINK_MESSAGE_TRANSFER_RELEASE, &session->transfer,
         packet->sequence))
        return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
      return events;
    }
    if(session->state != LINK_SESSION_WAIT_COMMIT_ACK ||
       !session->barrier_active ||
       packet->sequence != session->active_sequence)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
    if(!link_session_transaction_release(session) ||
       !link_session_transaction_apply(session, &session->transfer))
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
    if(!link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_RELEASE,
       &session->transfer, packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    events |= link_session_set_state(session, LINK_SESSION_WAIT_RELEASE_ACK);
  }
  else
  {
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  }
  return events;
}

static int link_session_release_transfer_valid(const LinkSession *session,
 const LinkPacket *packet, LinkTransferData *transfer)
{
  return packet->sequence == session->active_sequence &&
   link_protocol_read_transfer(packet, transfer) == LINK_PROTOCOL_OK &&
   transfer->participant_mask == 0x03 &&
   link_session_transfer_equal(transfer, &session->transfer) &&
   link_session_transfer_hash(transfer) == session->transaction_hash;
}

static uint32_t link_session_handle_release(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_JOINER ||
     !link_session_release_transfer_valid(session, packet, &transfer))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(session->state == LINK_SESSION_CONNECTED &&
     session->journal.stage == LINK_JOURNAL_EMPTY &&
     session->journal.checkpoint == packet->sequence &&
     session->journal.checkpoint_hash ==
      link_session_transfer_hash(&transfer))
  {
    if(!link_session_send_transfer(session,
       LINK_MESSAGE_TRANSFER_RELEASE_ACK, &transfer, packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return events;
  }
  if(session->state != LINK_SESSION_WAIT_COMMIT_RELEASE ||
     !session->barrier_active ||
     (session->journal.stage != LINK_JOURNAL_DECIDED &&
      session->journal.stage != LINK_JOURNAL_RELEASED))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(session->journal.stage == LINK_JOURNAL_DECIDED &&
     !link_session_transaction_release(session))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_session_transaction_apply(session, &transfer))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_session_send_transfer(session,
     LINK_MESSAGE_TRANSFER_RELEASE_ACK, &transfer, packet->sequence) ||
     !link_session_transaction_acknowledge(session))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  session->last_sequence = session->active_sequence;
  events |= link_session_end_barrier(session, 0, 0);
  events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  return events;
}

static uint32_t link_session_handle_release_ack(LinkSession *session,
 const LinkPacket *packet)
{
  LinkTransferData transfer;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_HOST ||
     !link_session_release_transfer_valid(session, packet, &transfer))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(session->state == LINK_SESSION_CONNECTED &&
     session->journal.stage == LINK_JOURNAL_EMPTY &&
     session->journal.checkpoint == packet->sequence)
    return events;
  if(session->state != LINK_SESSION_WAIT_RELEASE_ACK ||
     !session->barrier_active || session->journal.stage !=
      LINK_JOURNAL_RELEASED || !session->transaction_applied ||
     !link_session_transaction_acknowledge(session))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  session->last_sequence = session->active_sequence;
  events |= link_session_end_barrier(session, 0, 0);
  events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  return events;
}

static uint32_t link_session_handle_abort(LinkSession *session,
 const LinkPacket *packet)
{
  LinkReason reason;
  uint32_t events;

  if(packet->sequence == session->last_sequence &&
     session->transaction_stage == LINK_TRANSACTION_ABORTED &&
     !session->barrier_active)
  {
    if(link_protocol_read_reason(packet, &reason) != LINK_PROTOCOL_OK ||
       !link_session_reason_valid(reason.reason))
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
    if(!link_session_send_empty(session, LINK_MESSAGE_TRANSFER_ABORT_ACK,
       packet->sequence))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return LINK_SESSION_EVENT_NONE;
  }

  if(link_protocol_read_reason(packet, &reason) != LINK_PROTOCOL_OK ||
     !link_session_reason_valid(reason.reason))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  if((!session->barrier_active ||
      packet->sequence != session->active_sequence) &&
     !(session->state == LINK_SESSION_CONNECTED &&
       link_session_sequence_is_next(session, packet->sequence)))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  if(link_session_transfer_reason_recoverable(
      (LinkSessionReason)reason.reason))
  {
    events = link_session_abort_transfer(session,
     (LinkSessionReason)reason.reason, reason.detail, packet->sequence, 0);
    if(session->state != LINK_SESSION_ERROR &&
       !link_session_send_empty(session, LINK_MESSAGE_TRANSFER_ABORT_ACK,
        packet->sequence))
      events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return events;
  }

  return link_session_fail(session, (LinkSessionReason)reason.reason,
   reason.detail, 0);
}

static uint32_t link_session_handle_abort_ack(LinkSession *session,
 const LinkPacket *packet)
{
  if(packet->payload_size != 0 ||
     packet->sequence != session->last_sequence ||
     session->transaction_stage != LINK_TRANSACTION_ABORTED)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  if(session->state == LINK_SESSION_CONNECTED)
    return LINK_SESSION_EVENT_NONE;
  if(session->state != LINK_SESSION_WAIT_ABORT_ACK)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  return link_session_set_state(session, LINK_SESSION_CONNECTED);
}

static uint32_t link_session_handle_disconnect(LinkSession *session,
 const LinkPacket *packet)
{
  LinkReason reason;
  uint32_t events;
  GbaSioEvent sio_event;

  if(!link_session_sequence_is_current(session, packet->sequence) ||
     link_protocol_read_reason(packet, &reason) != LINK_PROTOCOL_OK ||
     !link_session_reason_valid(reason.reason))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  link_session_capture_terminal(session);
  link_session_clear_deferred_prepare(session);
  events = link_session_terminate_benchmark(session,
   LINK_BENCHMARK_FAILURE_PEER_ABORT);
  events |= link_session_end_barrier(session,
   !link_session_has_committed_transfer(session), 0);
  sio_event = link_session_detach_sio(session);
  if((sio_event & GBA_SIO_EVENT_SERIAL_IRQ) != 0)
    events |= LINK_SESSION_EVENT_SERIAL_IRQ;
  session->reason = (LinkSessionReason)reason.reason;
  session->reason_detail = reason.detail;
  session->transport_connected = 0;
  session->transport.ops->disconnect(session->transport.context);
  events |= link_session_set_state(session, LINK_SESSION_DISCONNECTED);
  events |= LINK_SESSION_EVENT_SESSION_CLOSED;
  return events;
}

static uint32_t link_session_transfer_hash(const LinkTransferData *transfer)
{
  uint32_t hash = 2166136261u;
  uint32_t index;
  uint32_t values[6];

  hash = (hash ^ transfer->mode) * 16777619u;
  hash = (hash ^ transfer->setting) * 16777619u;
  hash = (hash ^ transfer->clock_owner) * 16777619u;
  hash = (hash ^ transfer->player_count) * 16777619u;
  hash = (hash ^ transfer->participant_mask) * 16777619u;
  values[0] = transfer->start_cycle;
  values[1] = transfer->finish_cycle;
  for(index = 0; index < 4; index++)
    values[index + 2u] = transfer->data[index];
  for(index = 0; index < 6; index++)
  {
    hash = (hash ^ (values[index] >> 24)) * 16777619u;
    hash = (hash ^ ((values[index] >> 16) & 0xffu)) * 16777619u;
    hash = (hash ^ ((values[index] >> 8) & 0xffu)) * 16777619u;
    hash = (hash ^ (values[index] & 0xffu)) * 16777619u;
  }
  return hash;
}

uint32_t link_session_check_invariants(const LinkSession *session)
{
  uint32_t violations = LINK_INVARIANT_OK;
  int complete_record;

  if(session == NULL ||
     session->transaction_stage > LINK_TRANSACTION_ABORTED)
    return LINK_INVARIANT_TRANSACTION_RECORD;

  complete_record = session->transfer.participant_mask == 0x03 &&
   link_session_transfer_shape_valid(&session->transfer);
  if(session->transaction_stage == LINK_TRANSACTION_EMPTY)
  {
    if(session->transaction_sequence != 0 ||
       session->transaction_applied)
      violations |= LINK_INVARIANT_TRANSACTION_RECORD;
  }
  else if(session->transaction_sequence != session->active_sequence)
  {
    violations |= LINK_INVARIANT_TRANSACTION_SEQUENCE;
  }

  if(session->transaction_stage == LINK_TRANSACTION_PREPARED ||
     session->transaction_stage == LINK_TRANSACTION_DECIDED ||
     session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED)
  {
    if(!complete_record || session->transaction_hash !=
       link_session_transfer_hash(&session->transfer))
      violations |= LINK_INVARIANT_TRANSACTION_RECORD;
  }

  if(session->transaction_applied)
  {
    if((session->transaction_stage != LINK_TRANSACTION_DECIDED &&
        session->transaction_stage != LINK_TRANSACTION_ACKNOWLEDGED) ||
       session->applied_sequence != session->transaction_sequence)
      violations |= LINK_INVARIANT_COMMIT_STATE;
    if(session->applied_hash != session->transaction_hash ||
       memcmp(&session->applied_transfer, &session->transfer,
        sizeof(session->transfer)) != 0)
      violations |= LINK_INVARIANT_COMMIT_IDENTITY;
  }
  if(session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED &&
     session->last_sequence != session->transaction_sequence)
    violations |= LINK_INVARIANT_TRANSACTION_SEQUENCE;
  if((session->transaction_stage == LINK_TRANSACTION_PROPOSED ||
      session->transaction_stage == LINK_TRANSACTION_PREPARED ||
      session->transaction_stage == LINK_TRANSACTION_DECIDED) &&
     session->active_sequence != session->last_sequence + 1u)
    violations |= LINK_INVARIANT_TRANSACTION_SEQUENCE;

  if(session->barrier_active &&
     session->transaction_stage != LINK_TRANSACTION_PROPOSED &&
     session->transaction_stage != LINK_TRANSACTION_PREPARED &&
     session->transaction_stage != LINK_TRANSACTION_DECIDED &&
     !link_session_has_localized_stream_barrier(session))
    violations |= LINK_INVARIANT_BARRIER_STATE;
  if(session->barrier_active)
  {
    if(link_session_has_localized_stream_barrier(session))
    {
      uint32_t duration = session->transfer.finish_cycle -
       session->transfer.start_cycle;
      uint32_t elapsed = session->sio->current_cycle -
       session->barrier_cycle;

      if(elapsed >= 0x80000000u || elapsed > duration)
        violations |= LINK_INVARIANT_SIO_HORIZON;
    }
    else if(session->sio->current_cycle != session->barrier_cycle)
      violations |= LINK_INVARIANT_SIO_HORIZON;
  }
  if(!session->barrier_active &&
     (session->state == LINK_SESSION_WAIT_PREPARE ||
      session->state == LINK_SESSION_WAIT_ACCEPT ||
      session->state == LINK_SESSION_WAIT_COMMIT ||
      session->state == LINK_SESSION_WAIT_COMMIT_ACK ||
      session->state == LINK_SESSION_WAIT_COMMIT_RELEASE ||
      session->state == LINK_SESSION_WAIT_STREAM_REPLY))
    violations |= LINK_INVARIANT_BARRIER_STATE;
  if(session->state == LINK_SESSION_WAIT_ABORT_ACK &&
     (session->barrier_active ||
      session->transaction_stage != LINK_TRANSACTION_ABORTED ||
      session->last_sequence != session->transaction_sequence))
    violations |= LINK_INVARIANT_BARRIER_STATE;
  if(!session->barrier_active &&
     session->transaction_stage == LINK_TRANSACTION_DECIDED &&
     session->state != LINK_SESSION_ERROR &&
     session->state != LINK_SESSION_DISCONNECTED)
    violations |= LINK_INVARIANT_COMMIT_STATE;
  if(session->state == LINK_SESSION_WAIT_COMMIT_ACK &&
     (session->transaction_stage != LINK_TRANSACTION_DECIDED ||
      session->transaction_applied ||
      session->journal.stage != LINK_JOURNAL_DECIDED))
    violations |= LINK_INVARIANT_COMMIT_STATE;
  if(session->state == LINK_SESSION_WAIT_COMMIT_RELEASE &&
     (session->transaction_stage != LINK_TRANSACTION_DECIDED ||
      ((session->journal.stage == LINK_JOURNAL_DECIDED) !=
       !session->transaction_applied) ||
      (session->journal.stage != LINK_JOURNAL_DECIDED &&
       session->journal.stage != LINK_JOURNAL_RELEASED)))
    violations |= LINK_INVARIANT_COMMIT_STATE;
  if(session->state == LINK_SESSION_WAIT_RELEASE_ACK &&
     (session->role != LINK_ROLE_HOST ||
      session->transaction_stage != LINK_TRANSACTION_DECIDED ||
      !session->transaction_applied))
    violations |= LINK_INVARIANT_COMMIT_STATE;
  if(session->state == LINK_SESSION_CONNECTED &&
     session->transaction_stage != LINK_TRANSACTION_EMPTY &&
     session->transaction_stage != LINK_TRANSACTION_ACKNOWLEDGED &&
     session->transaction_stage != LINK_TRANSACTION_ABORTED)
    violations |= LINK_INVARIANT_COMMIT_STATE;

  if((session->state == LINK_SESSION_WAIT_PREPARE &&
      session->role != LINK_ROLE_JOINER) ||
     (session->state == LINK_SESSION_WAIT_ACCEPT &&
      session->role != LINK_ROLE_HOST) ||
     (session->state == LINK_SESSION_WAIT_COMMIT &&
      session->role != LINK_ROLE_JOINER) ||
     (session->state == LINK_SESSION_WAIT_COMMIT_ACK &&
      session->role != LINK_ROLE_HOST) ||
     (session->state == LINK_SESSION_WAIT_COMMIT_RELEASE &&
      session->role != LINK_ROLE_JOINER) ||
     (session->state == LINK_SESSION_WAIT_RELEASE_ACK &&
      session->role != LINK_ROLE_HOST) ||
     (session->state == LINK_SESSION_WAIT_STREAM_REPLY &&
      (session->role != LINK_ROLE_HOST ||
       link_stream_state(&session->stream) !=
        LINK_STREAM_STATE_HOST_WAIT_REPLY ||
       link_stream_active_sequence(&session->stream) !=
        session->active_sequence)))
    violations |= LINK_INVARIANT_ROLE_STATE;

  if(session->transport_health > LINK_TRANSPORT_HEALTH_FAILED)
    violations |= LINK_INVARIANT_TRANSPORT_HEALTH;
  if(session->transport_health != LINK_TRANSPORT_HEALTH_HEALTHY &&
     session->transport_health != LINK_TRANSPORT_HEALTH_FAILED &&
     !link_session_state_uses_connection(session->state))
    violations |= LINK_INVARIANT_TRANSPORT_HEALTH;
  if(session->transport_health == LINK_TRANSPORT_HEALTH_FAILED &&
     session->state != LINK_SESSION_ERROR &&
     session->state != LINK_SESSION_DISCONNECTED)
    violations |= LINK_INVARIANT_TRANSPORT_HEALTH;
  if(session->transport_resume_ready &&
     !link_session_transport_recovering(session))
    violations |= LINK_INVARIANT_TRANSPORT_HEALTH;
  if(session->transport_health == LINK_TRANSPORT_HEALTH_HEALTHY &&
     session->transport_pause_started_us != 0)
    violations |= LINK_INVARIANT_TRANSPORT_HEALTH;

  if(!session->journal_valid ||
     session->journal.session_nonce != session->session_id ||
     session->journal.epoch == 0)
  {
    if(link_session_state_uses_connection(session->state) &&
       !(session->role == LINK_ROLE_JOINER && session->session_id == 0 &&
         session->state == LINK_SESSION_NEGOTIATING))
      violations |= LINK_INVARIANT_JOURNAL;
  }
  else if(!link_session_transport_recovering(session) &&
          (session->transaction_stage == LINK_TRANSACTION_PROPOSED ||
           session->transaction_stage == LINK_TRANSACTION_PREPARED))
  {
    int joiner_request = session->role == LINK_ROLE_JOINER &&
     session->state == LINK_SESSION_WAIT_PREPARE &&
     session->transaction_stage == LINK_TRANSACTION_PROPOSED &&
     session->journal.stage == LINK_JOURNAL_EMPTY;

    if(!joiner_request &&
       (session->journal.stage != LINK_JOURNAL_ARMED ||
        session->journal.active_sequence != session->transaction_sequence ||
        session->journal.transfer_hash != 0))
      violations |= LINK_INVARIANT_JOURNAL;
  }
  else if(!link_session_transport_recovering(session) &&
          session->transaction_stage == LINK_TRANSACTION_DECIDED)
  {
    if((session->journal.stage != LINK_JOURNAL_DECIDED &&
        session->journal.stage != LINK_JOURNAL_RELEASED) ||
       session->journal.active_sequence != session->transaction_sequence ||
       session->journal.transfer_hash != session->transaction_hash ||
       session->journal.applied != session->transaction_applied)
      violations |= LINK_INVARIANT_JOURNAL;
  }
  else if(!link_session_transport_recovering(session) &&
          session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED)
  {
    if(session->journal.stage != LINK_JOURNAL_EMPTY ||
       session->journal.checkpoint != session->transaction_sequence ||
       session->journal.checkpoint_hash != session->transaction_hash)
      violations |= LINK_INVARIANT_JOURNAL;
  }
  else if(!link_session_transport_recovering(session) &&
          session->transaction_stage == LINK_TRANSACTION_ABORTED)
  {
    if(session->journal.stage != LINK_JOURNAL_EMPTY)
      violations |= LINK_INVARIANT_JOURNAL;
  }

  if(session->deferred_prepare_valid)
  {
    if(session->role != LINK_ROLE_JOINER || session->barrier_active ||
       (session->state != LINK_SESSION_CONNECTED &&
        !link_session_transport_recovering(session)) ||
       session->deferred_prepare_sequence != session->last_sequence + 1u ||
       !link_session_transfer_shape_valid(&session->deferred_prepare) ||
       session->deferred_prepare.clock_owner != 0 ||
       session->deferred_prepare.participant_mask != 0x01)
      violations |= LINK_INVARIANT_DEFERRED_PREPARE;
  }
  else if(session->deferred_prepare_sequence != 0 ||
          session->deferred_prepare_idle_seen)
  {
    violations |= LINK_INVARIANT_DEFERRED_PREPARE;
  }

  return violations;
}

static uint32_t link_session_record_invariants(LinkSession *session)
{
  uint32_t violations = link_session_check_invariants(session);

  session->invariant_violations |= violations;
  return violations;
}

static uint32_t link_session_finish_pump(LinkSession *session,
 uint32_t events)
{
  uint32_t violations = link_session_record_invariants(session);

  if(violations != 0 && session->state != LINK_SESSION_ERROR &&
     session->state != LINK_SESSION_DISCONNECTED)
    events |= link_session_fail(session, LINK_REASON_PROTOCOL,
     (uint16_t)violations, 0);
  return events;
}

static void link_session_resume_state(const LinkSession *session,
 LinkResumeState *state)
{
  LinkJournalRecord record;
  LinkStreamState stream_state;

  memset(state, 0, sizeof(*state));
  link_journal_snapshot(&session->journal, &record);
  state->last_sequence = record.checkpoint;
  state->active_sequence = record.sequence;
  state->phase = (uint8_t)session->recovery_previous_state;
  if(session->journal.stage != LINK_JOURNAL_EMPTY ||
     session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED)
    state->transfer = session->transfer;
  state->transaction_stage = (uint8_t)session->transaction_stage;
  state->epoch = record.epoch;
  state->checkpoint = record.checkpoint;
  state->checkpoint_hash = record.checkpoint_hash;
  state->journal_hash = record.hash;
  state->sio_revision = session->sio_revision;
  if(session->local_sio_valid)
    state->sio = session->local_sio_latest;
  state->journal_stage = (uint8_t)session->journal.stage;
  state->journal_role = (uint8_t)session->journal.role;
  state->journal_applied = session->journal.applied;
  state->journal_irq_delivered = session->journal.irq_delivered;
  state->release_shared = record.release_shared;
  if(session->journal.stage != LINK_JOURNAL_EMPTY)
    state->flags |= LINK_RESUME_FLAG_BARRIER;
  if(session->journal.stage == LINK_JOURNAL_DECIDED ||
     session->journal.stage == LINK_JOURNAL_RELEASED)
    state->flags |= LINK_RESUME_FLAG_COMMITTED;

  if((session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) == 0 ||
     session->transaction_stage != LINK_TRANSACTION_EMPTY)
    return;

  state->flags |= LINK_RESUME_FLAG_STREAM;
  state->last_sequence = session->last_sequence;
  state->stream_generation = link_stream_generation(&session->stream);
  state->stream_transport_generation = session->transport_generation;
  state->stream_last_committed =
   link_stream_last_committed(&session->stream);
  stream_state = link_stream_state(&session->stream);
  if(session->role == LINK_ROLE_HOST &&
     stream_state == LINK_STREAM_STATE_HOST_WAIT_REPLY &&
     session->stream.active_start_valid)
  {
    state->stream_flags |= LINK_STREAM_CHECKPOINT_PENDING_START;
    state->stream_active_sequence = session->stream.active_sequence;
    state->stream_start_identity = session->stream.active_start.identity;
    state->transfer = session->transfer;
  }
  else if(session->role == LINK_ROLE_JOINER &&
          session->stream.reply_cache.valid)
  {
    state->stream_flags |= LINK_STREAM_CHECKPOINT_REPLY_CACHE;
    state->stream_active_sequence = session->stream.reply_cache.sequence;
    state->stream_start_identity =
     session->stream.reply_cache.start.identity;
    state->stream_reply_outgoing =
     session->stream.reply_cache.reply.outgoing;
    state->transfer = session->applied_transfer;
  }
  if(session->applied_sequence == state->stream_last_committed &&
     session->applied_sequence != 0 &&
     session->applied_transfer.mode == GBA_SIO_MODE_MULTIPLAYER &&
     session->applied_transfer.participant_mask == 0x03u)
  {
    state->stream_commit_identity =
     link_session_transfer_hash(&session->applied_transfer) ^
     session->session_id ^ session->applied_sequence;
    if(state->stream_commit_identity == 0)
      state->stream_commit_identity = 1u;
  }
}

static int link_session_send_resume(LinkSession *session,
 LinkMessageType type, LinkResumeState *state)
{
  LinkPacket packet;

  link_protocol_packet_init(&packet, type, session->session_id,
   state->last_sequence);
  if(link_protocol_write_resume(&packet, type, state) != LINK_PROTOCOL_OK)
    return 0;
  return link_session_send(session, &packet);
}

static uint32_t link_session_restore_after_recovery(LinkSession *session)
{
  uint32_t events;
  uint64_t now = link_session_now(session);

  session->transport_health = LINK_TRANSPORT_HEALTH_HEALTHY;
  session->transport_resume_ready = 0;
  session->transport_pause_started_us = 0;
  session->recovery_requested_us = 0;
  session->last_rx_us = now;
  session->last_tx_us = now;
  session->heartbeat_probe_pending = 0;
  session->heartbeat_outstanding_token = 0;
  /* Token identity is scoped to the recovered stream generation. Keep the
     cumulative diagnostics, but forget requests from the previous epoch. */
  session->heartbeat_last_request_token = 0;
  session->heartbeat_last_request_valid = 0;
  if(session->barrier_active)
    session->barrier_started_us = now;
  if(session->ready_wait_active)
    session->ready_wait_started_us = now;
  events = link_session_set_state(session, session->recovery_previous_state);
  if(session->local_sio_valid)
  {
    session->local_sio_sent_valid = 0;
    events |= LINK_SESSION_EVENT_SIO_REPUBLISH;
  }
  return events;
}

static uint32_t link_session_retry_resume(LinkSession *session,
 uint64_t now)
{
  LinkResumeState local;
  LinkMessageType type;

  if(!session->transport_resume_ready || now < session->last_tx_us ||
     now - session->last_tx_us < LINK_SESSION_RESUME_RETRY_US)
    return LINK_SESSION_EVENT_NONE;
  if(session->role == LINK_ROLE_HOST &&
     session->state == LINK_SESSION_WAIT_RESUME_ACK)
    type = LINK_MESSAGE_RESUME;
  else if(session->role == LINK_ROLE_JOINER &&
          session->state == LINK_SESSION_WAIT_RESUME_DONE)
  {
    type = LINK_MESSAGE_RESUME_ACK;
    if(!session->resume_done_valid)
      return LINK_SESSION_EVENT_NONE;
    local = session->resume_done;
  }
  else
    return LINK_SESSION_EVENT_NONE;

  if(type == LINK_MESSAGE_RESUME)
    link_session_resume_state(session, &local);
  if(!link_session_send_resume(session, type, &local))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return LINK_SESSION_EVENT_NONE;
}

static int link_session_recovery_phase_valid(uint8_t phase)
{
  return phase >= LINK_SESSION_CONNECTED &&
   phase <= LINK_SESSION_WAIT_STREAM_REPLY;
}

static int link_session_resume_state_valid(const LinkResumeState *state)
{
  uint8_t known_flags = LINK_RESUME_FLAG_BARRIER |
   LINK_RESUME_FLAG_COMMITTED | LINK_RESUME_FLAG_ABORT |
   LINK_RESUME_FLAG_STREAM;
  uint8_t known_stream_flags = LINK_STREAM_CHECKPOINT_PENDING_START |
   LINK_STREAM_CHECKPOINT_REPLY_CACHE;
  int active;

  if(state == NULL || !link_session_recovery_phase_valid(state->phase) ||
     state->transaction_stage > LINK_TRANSACTION_ABORTED ||
     state->epoch == 0 || state->journal_stage > LINK_JOURNAL_RELEASED ||
     state->journal_role > LINK_JOURNAL_ROLE_JOINER ||
     state->journal_applied > 1 || state->journal_irq_delivered > 1 ||
     state->release_shared > 1 ||
     (state->flags & (uint8_t)~known_flags) != 0)
    return 0;
  if((state->flags & LINK_RESUME_FLAG_STREAM) != 0)
  {
    if(state->stream_generation == 0 ||
       state->stream_transport_generation == 0 ||
       state->stream_last_committed != state->last_sequence ||
       (state->stream_flags & (uint8_t)~known_stream_flags) != 0 ||
       state->stream_flags == known_stream_flags)
      return 0;
    if(state->stream_flags != 0)
    {
      if(state->stream_active_sequence == 0 ||
         state->stream_start_identity == 0 ||
         !link_session_transfer_shape_valid(&state->transfer) ||
         state->transfer.mode != GBA_SIO_MODE_MULTIPLAYER ||
         ((state->stream_flags &
           LINK_STREAM_CHECKPOINT_PENDING_START) != 0 &&
          state->transfer.participant_mask != 0x01u) ||
         ((state->stream_flags &
           LINK_STREAM_CHECKPOINT_REPLY_CACHE) != 0 &&
          state->transfer.participant_mask != 0x03u))
        return 0;
    }
    else if(state->stream_active_sequence != 0 ||
            state->stream_start_identity != 0 ||
            state->stream_reply_outgoing != 0)
    {
      return 0;
    }
  }
  else if(state->stream_generation != 0 ||
          state->stream_transport_generation != 0 ||
          state->stream_last_committed != 0 ||
          state->stream_active_sequence != 0 ||
          state->stream_start_identity != 0 ||
          state->stream_reply_outgoing != 0 ||
          state->stream_commit_identity != 0 || state->stream_flags != 0)
  {
    return 0;
  }
  active = state->journal_stage != LINK_JOURNAL_EMPTY;
  if(((state->flags & LINK_RESUME_FLAG_BARRIER) != 0) != active ||
     ((state->flags & LINK_RESUME_FLAG_COMMITTED) != 0) !=
      (state->journal_stage == LINK_JOURNAL_DECIDED ||
       state->journal_stage == LINK_JOURNAL_RELEASED) ||
     (state->flags & LINK_RESUME_FLAG_ABORT) != 0 ||
     state->journal_applied != state->journal_irq_delivered)
    return 0;
  if(state->sio_revision != 0)
  {
    if(state->sio.player_count != 2 || state->sio.player_id > 1 ||
       state->sio.mode > GBA_SIO_MODE_JOYBUS)
      return 0;
  }
  if(!active)
    return !state->journal_applied && !state->release_shared;
  if(!link_session_transfer_shape_valid(&state->transfer))
    return 0;
  if(state->journal_stage == LINK_JOURNAL_ARMED)
    return state->journal_hash == 0 && !state->journal_applied &&
     !state->release_shared;
  return state->transfer.participant_mask == 0x03 &&
   state->journal_hash != 0 && state->journal_hash ==
    link_session_transfer_hash(&state->transfer) &&
   (state->journal_stage == LINK_JOURNAL_RELEASED ||
    (!state->journal_applied && !state->release_shared));
}

static int link_session_resume_matches(const LinkResumeState *left,
 const LinkResumeState *right)
{
  return left->last_sequence == right->last_sequence &&
   left->active_sequence == right->active_sequence &&
   memcmp(&left->transfer, &right->transfer,
    sizeof(left->transfer)) == 0 &&
   memcmp(&left->sio, &right->sio, sizeof(left->sio)) == 0 &&
   left->phase == right->phase && left->flags == right->flags &&
   left->transaction_stage == right->transaction_stage &&
   left->epoch == right->epoch && left->checkpoint == right->checkpoint &&
   left->checkpoint_hash == right->checkpoint_hash &&
   left->journal_hash == right->journal_hash &&
   left->sio_revision == right->sio_revision &&
   left->journal_stage == right->journal_stage &&
   left->journal_role == right->journal_role &&
   left->journal_applied == right->journal_applied &&
   left->journal_irq_delivered == right->journal_irq_delivered &&
   left->release_shared == right->release_shared &&
   left->stream_generation == right->stream_generation &&
   left->stream_transport_generation ==
    right->stream_transport_generation &&
   left->stream_last_committed == right->stream_last_committed &&
   left->stream_active_sequence == right->stream_active_sequence &&
   left->stream_start_identity == right->stream_start_identity &&
   left->stream_reply_outgoing == right->stream_reply_outgoing &&
   left->stream_commit_identity == right->stream_commit_identity &&
   left->stream_flags == right->stream_flags;
}

static int link_session_stream_transfer_prefix_equal(
 const LinkTransferData *host, const LinkTransferData *joiner)
{
  return host->mode == joiner->mode &&
   host->setting == joiner->setting &&
   host->clock_owner == joiner->clock_owner &&
   host->player_count == joiner->player_count &&
   host->start_cycle == joiner->start_cycle &&
   host->finish_cycle == joiner->finish_cycle &&
   host->data[0] == joiner->data[0];
}

static int link_session_stream_commit_matches(const LinkResumeState *host,
 const LinkResumeState *joiner)
{
  return host->stream_commit_identity == 0 ||
   joiner->stream_commit_identity == 0 ||
   host->stream_commit_identity == joiner->stream_commit_identity;
}

static uint16_t link_session_make_stream_recovery_plan(
 LinkSession *session, const LinkResumeState *host,
 const LinkResumeState *joiner)
{
  uint32_t expected_identity;
  uint32_t next_generation;
  int host_pending;
  int joiner_cached;

  session->stream_recovery_valid = 0;
  session->stream_recovery_pending = 0;
  session->stream_recovery_generation = 0;
  if((session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) == 0 ||
     (host->flags & LINK_RESUME_FLAG_STREAM) == 0 ||
     (joiner->flags & LINK_RESUME_FLAG_STREAM) == 0 ||
     (host->stream_flags & LINK_STREAM_CHECKPOINT_REPLY_CACHE) != 0 ||
     (joiner->stream_flags & LINK_STREAM_CHECKPOINT_PENDING_START) != 0)
    return LINK_STREAM_RECOVERY_FLAGS_DETAIL;
  if(host->stream_generation != joiner->stream_generation)
    return LINK_STREAM_RECOVERY_GENERATION_DETAIL;
  if(host->stream_transport_generation !=
      joiner->stream_transport_generation ||
     host->stream_transport_generation != session->transport_generation)
    return LINK_STREAM_RECOVERY_TRANSPORT_DETAIL;

  host_pending = (host->stream_flags &
   LINK_STREAM_CHECKPOINT_PENDING_START) != 0;
  joiner_cached = (joiner->stream_flags &
   LINK_STREAM_CHECKPOINT_REPLY_CACHE) != 0;
  if(host_pending)
  {
    if(host->stream_active_sequence !=
        link_session_next_stream_sequence(host->stream_last_committed))
      return LINK_STREAM_RECOVERY_SEQUENCE_DETAIL;
    expected_identity = link_session_transfer_hash(&host->transfer) ^
     session->session_id ^ host->stream_active_sequence;
    if(expected_identity == 0)
      expected_identity = 1u;
    if(host->stream_start_identity != expected_identity)
      return LINK_STREAM_RECOVERY_IDENTITY_DETAIL;

    if(joiner->stream_last_committed == host->stream_last_committed)
    {
      if(joiner_cached &&
         joiner->stream_active_sequence != joiner->stream_last_committed)
        return LINK_STREAM_RECOVERY_CACHE_DETAIL;
      if(!link_session_stream_commit_matches(host, joiner))
        return LINK_STREAM_RECOVERY_IDENTITY_DETAIL;
    }
    else if(joiner->stream_last_committed ==
            host->stream_active_sequence)
    {
      if(!joiner_cached ||
         joiner->stream_active_sequence != host->stream_active_sequence)
        return LINK_STREAM_RECOVERY_CACHE_DETAIL;
      if(joiner->stream_start_identity != host->stream_start_identity ||
         joiner->stream_reply_outgoing != joiner->transfer.data[1] ||
         !link_session_stream_transfer_prefix_equal(&host->transfer,
          &joiner->transfer))
        return LINK_STREAM_RECOVERY_IDENTITY_DETAIL;
    }
    else
    {
      return LINK_STREAM_RECOVERY_SEQUENCE_DETAIL;
    }
  }
  else
  {
    if(host->stream_active_sequence != 0 ||
       host->stream_last_committed != joiner->stream_last_committed)
      return LINK_STREAM_RECOVERY_SEQUENCE_DETAIL;
    if(joiner_cached && joiner->stream_active_sequence !=
       joiner->stream_last_committed)
      return LINK_STREAM_RECOVERY_CACHE_DETAIL;
    if(!link_session_stream_commit_matches(host, joiner))
      return LINK_STREAM_RECOVERY_IDENTITY_DETAIL;
  }

  next_generation = host->stream_generation + 1u;
  if(next_generation == 0)
    next_generation = 1u;
  session->stream_recovery_generation = next_generation;
  session->stream_recovery_pending = host_pending != 0;
  session->stream_recovery_valid = 1;
  return 0;
}

static int link_session_apply_stream_recovery(LinkSession *session)
{
  const LinkResumeState *host;
  int recovered;

  if(!session->stream_recovery_valid ||
     session->stream_recovery_generation == 0)
    return 0;
  if(session->role == LINK_ROLE_HOST)
  {
    recovered = session->stream_recovery_pending ?
     link_stream_recover_host_pending(&session->stream,
      session->stream_recovery_generation) :
     link_stream_recover_committed(&session->stream,
      session->stream_recovery_generation, session->last_sequence);
  }
  else
  {
    host = &session->peer_resume;
    recovered = session->stream_recovery_pending ?
     link_stream_recover_joiner_pending(&session->stream,
      session->stream_recovery_generation,
      host->stream_active_sequence, host->stream_start_identity) :
     link_stream_recover_committed(&session->stream,
      session->stream_recovery_generation, session->last_sequence);
  }
  if(!recovered)
    return 0;
  link_clock_init(&session->clock, session->stream_recovery_generation,
   session->clock.local_cycle);
  return 1;
}

static int link_session_stream_recovery_done_matches(
 const LinkSession *session, const LinkResumeState *state)
{
  const LinkResumeState *host = &session->peer_resume;
  uint8_t expected_flags = session->stream_recovery_pending ?
   LINK_STREAM_CHECKPOINT_PENDING_START : 0u;

  return session->stream_recovery_valid &&
   (state->flags & LINK_RESUME_FLAG_STREAM) != 0 &&
   state->stream_generation == session->stream_recovery_generation &&
   state->stream_transport_generation == session->transport_generation &&
   state->stream_last_committed == host->stream_last_committed &&
   state->stream_active_sequence == host->stream_active_sequence &&
   state->stream_start_identity == host->stream_start_identity &&
   state->stream_commit_identity == host->stream_commit_identity &&
   state->stream_flags == expected_flags;
}

static void link_session_recovery_snapshot(const LinkSession *session,
 const LinkResumeState *state, LinkRecoverySnapshot *snapshot)
{
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->role = (LinkJournalRole)state->journal_role;
  snapshot->stage = (LinkJournalStage)state->journal_stage;
  snapshot->applied = state->journal_applied;
  snapshot->irq_delivered = state->journal_irq_delivered;
  snapshot->record.session_nonce = session->session_id;
  snapshot->record.epoch = state->epoch;
  snapshot->record.sequence = snapshot->stage == LINK_JOURNAL_EMPTY ?
   state->checkpoint : state->active_sequence;
  snapshot->record.hash = state->journal_hash;
  snapshot->record.checkpoint = state->checkpoint;
  snapshot->record.checkpoint_hash = state->checkpoint_hash;
  if(snapshot->stage != LINK_JOURNAL_EMPTY)
  {
    snapshot->record.start_cycle = state->transfer.start_cycle;
    snapshot->record.finish_cycle = state->transfer.finish_cycle;
  }
  snapshot->record.release_shared = state->release_shared;
}

static int link_session_recovery_adopt_transfer(LinkSession *session,
 const LinkResumeState *peer, const LinkRecoveryPlan *plan)
{
  uint32_t local_hash;

  if(plan->kind != LINK_RECOVERY_PLAN_DECIDED &&
     plan->kind != LINK_RECOVERY_PLAN_RELEASED)
    return 1;
  local_hash = session->transfer.participant_mask == 0x03 &&
   link_session_transfer_shape_valid(&session->transfer) ?
   link_session_transfer_hash(&session->transfer) : 0u;
  if(local_hash == plan->record.hash)
    return 1;
  if(peer->transfer.participant_mask != 0x03 ||
     !link_session_transfer_shape_valid(&peer->transfer) ||
     link_session_transfer_hash(&peer->transfer) != plan->record.hash)
    return 0;
  session->transfer = peer->transfer;
  session->active_sequence = plan->record.sequence;
  return 1;
}

static int link_session_recovery_done_matches(const LinkSession *session,
 const LinkResumeState *state)
{
  uint32_t sequence = session->journal.stage == LINK_JOURNAL_EMPTY ?
   session->journal.checkpoint : session->journal.active_sequence;
  uint32_t hash = session->journal.stage == LINK_JOURNAL_EMPTY ?
   session->journal.checkpoint_hash : session->journal.transfer_hash;

  return state->journal_role == LINK_JOURNAL_ROLE_HOST &&
   state->epoch == session->journal.epoch &&
   state->journal_stage == session->journal.stage &&
   state->checkpoint == session->journal.checkpoint &&
   state->checkpoint_hash == session->journal.checkpoint_hash &&
   state->active_sequence == sequence && state->journal_hash == hash &&
   state->release_shared == session->journal.release_shared;
}

static uint32_t link_session_finish_recovery_plan(LinkSession *session,
 const LinkRecoveryPlan *plan)
{
  uint32_t events;
  LinkSessionState state = LINK_SESSION_CONNECTED;

  if(!link_recovery_complete(&session->recovery, plan))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  events = link_session_restore_after_recovery(session);
  if(plan->kind == LINK_RECOVERY_PLAN_EMPTY)
  {
    uint32_t checkpoint = session->journal.checkpoint;

    link_session_clear_deferred_prepare(session);
    session->active_sequence = plan->sequence_floor;
    session->last_sequence = plan->sequence_floor;
    if(link_journal_serial_after(plan->sequence_floor, checkpoint))
    {
      session->transaction_sequence = plan->sequence_floor;
      session->transaction_stage = LINK_TRANSACTION_ABORTED;
      session->transaction_hash = 0;
      session->transaction_applied = 0;
    }
    else
    {
      int acknowledged =
       session->transaction_stage == LINK_TRANSACTION_ACKNOWLEDGED &&
       session->transaction_sequence == checkpoint &&
       session->transaction_hash == session->journal.checkpoint_hash &&
       session->transaction_applied &&
       session->applied_sequence == checkpoint &&
       session->applied_hash == session->journal.checkpoint_hash;

      if(!acknowledged)
      {
        session->transaction_sequence = 0;
        session->transaction_stage = LINK_TRANSACTION_EMPTY;
        session->transaction_hash = 0;
        session->transaction_applied = 0;
        session->applied_sequence = 0;
        session->applied_hash = 0;
        memset(&session->applied_transfer, 0,
         sizeof(session->applied_transfer));
      }
    }
    if(session->barrier_active)
    {
      events |= link_session_end_barrier(session, 1, 0);
    }
  }
  else if(plan->kind == LINK_RECOVERY_PLAN_CHECKPOINT)
  {
    session->active_sequence = session->journal.checkpoint;
    session->last_sequence = session->journal.checkpoint;
    session->transaction_sequence = session->journal.checkpoint;
    session->transaction_hash = session->journal.checkpoint_hash;
    session->transaction_stage = LINK_TRANSACTION_ACKNOWLEDGED;
    session->transaction_applied = 1;
    session->applied_sequence = session->journal.checkpoint;
    session->applied_hash = session->journal.checkpoint_hash;
    session->applied_transfer = session->transfer;
    events |= link_session_end_barrier(session, 0, 0);
  }
  else
  {
    session->active_sequence = session->journal.active_sequence;
    session->last_sequence = session->journal.checkpoint;
    session->transaction_sequence = session->journal.active_sequence;
    session->transaction_hash = session->journal.transfer_hash;
    session->transaction_stage = LINK_TRANSACTION_DECIDED;
    if(session->journal.applied)
    {
      session->transaction_applied = 1;
      session->applied_sequence = session->journal.active_sequence;
      session->applied_hash = session->journal.transfer_hash;
      session->applied_transfer = session->transfer;
    }
    else
    {
      session->transaction_applied = 0;
      if(plan->kind == LINK_RECOVERY_PLAN_RELEASED &&
         !link_session_transaction_apply(session, &session->transfer))
        return events | link_session_fail(session, LINK_REASON_PROTOCOL,
         0, 0);
    }
    state = session->role == LINK_ROLE_HOST ?
     (plan->kind == LINK_RECOVERY_PLAN_DECIDED ?
      LINK_SESSION_WAIT_COMMIT_ACK : LINK_SESSION_WAIT_RELEASE_ACK) :
     LINK_SESSION_WAIT_COMMIT_RELEASE;
  }
  events |= link_session_set_state(session, state);
  if(session->role == LINK_ROLE_HOST &&
     plan->kind == LINK_RECOVERY_PLAN_DECIDED &&
     !link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_COMMIT,
      &session->transfer, session->active_sequence))
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  else if(session->role == LINK_ROLE_HOST &&
          plan->kind == LINK_RECOVERY_PLAN_RELEASED &&
          !link_session_send_transfer(session, LINK_MESSAGE_TRANSFER_RELEASE,
           &session->transfer, session->active_sequence))
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return events;
}

static uint32_t link_session_finish_stream_recovery(LinkSession *session)
{
  uint32_t events;

  if(!link_recovery_complete(&session->recovery,
      &session->recovery_plan))
    return link_session_fail(session, LINK_REASON_PROTOCOL,
     LINK_STREAM_RECOVERY_APPLY_DETAIL, 0);
  events = link_session_restore_after_recovery(session);
  if(session->stream_recovery_pending &&
     session->role == LINK_ROLE_HOST)
  {
    events |= link_session_set_state(session,
     LINK_SESSION_WAIT_STREAM_REPLY);
    if(!session->barrier_active)
      events |= link_session_fail(session, LINK_REASON_PROTOCOL,
       LINK_STREAM_RECOVERY_APPLY_DETAIL, 0);
    else if(!link_session_send_stream_start(session,
             &session->stream.active_start,
             session->stream.active_sequence))
      events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  }
  else
  {
    events |= link_session_set_state(session, LINK_SESSION_CONNECTED);
  }
  session->stream_recovery_valid = 0;
  return events;
}

static uint32_t link_session_handle_resume(LinkSession *session,
 const LinkPacket *packet)
{
  LinkResumeState local;
  LinkResumeState peer;
  LinkRecoverySnapshot snapshot;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint16_t stream_detail = 0;

  if(session->role != LINK_ROLE_JOINER ||
     link_protocol_read_resume(packet, &peer) != LINK_PROTOCOL_OK ||
     packet->sequence != peer.last_sequence ||
     !link_session_resume_state_valid(&peer) ||
     peer.journal_role != LINK_JOURNAL_ROLE_HOST)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  if(session->state == LINK_SESSION_WAIT_RESUME_DONE &&
     session->peer_resume_valid &&
     link_session_resume_matches(&session->peer_resume, &peer))
  {
    if(!link_session_send_resume(session, LINK_MESSAGE_RESUME_ACK,
       &session->resume_done))
      return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
    return events;
  }
  if(session->state != LINK_SESSION_RECOVERING ||
     session->recovery.state != LINK_RECOVERY_EXCHANGING)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  session->peer_resume = peer;
  session->peer_resume_valid = 1;
  link_session_resume_state(session, &local);
  session->resume_done = local;
  session->resume_done_valid = 1;
  if((peer.flags & LINK_RESUME_FLAG_STREAM) != 0 &&
     (local.flags & LINK_RESUME_FLAG_STREAM) != 0)
  {
    stream_detail = link_session_make_stream_recovery_plan(session,
     &peer, &local);
    if(stream_detail != 0)
      return link_session_fail(session, LINK_REASON_PROTOCOL,
       stream_detail, 0);
  }
  link_session_recovery_snapshot(session, &peer, &snapshot);
  if(!link_recovery_note_snapshot(&session->recovery, &snapshot) ||
     !link_recovery_make_plan(&session->recovery,
      &session->recovery_plan) ||
     session->recovery_plan.kind == LINK_RECOVERY_PLAN_CLOSE ||
     !link_session_recovery_adopt_transfer(session, &peer,
      &session->recovery_plan) ||
     !link_recovery_apply_plan(&session->journal,
      &session->recovery_plan))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  if(!link_session_send_resume(session, LINK_MESSAGE_RESUME_ACK,
     &session->resume_done))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  events |= link_session_set_state(session, LINK_SESSION_WAIT_RESUME_DONE);
  return events;
}

static uint32_t link_session_handle_resume_ack(LinkSession *session,
 const LinkPacket *packet)
{
  LinkResumeState peer;
  LinkResumeState local;
  LinkRecoverySnapshot snapshot;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint16_t stream_detail = 0;

  if(session->role != LINK_ROLE_HOST ||
     link_protocol_read_resume(packet, &peer) != LINK_PROTOCOL_OK ||
     packet->sequence != peer.last_sequence ||
     !link_session_resume_state_valid(&peer) ||
     peer.journal_role != LINK_JOURNAL_ROLE_JOINER)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  if(session->state != LINK_SESSION_WAIT_RESUME_ACK)
  {
    if(!link_session_transport_recovering(session) &&
       session->peer_resume_valid &&
       link_session_resume_matches(&session->peer_resume, &peer))
    {
      if(session->resume_done_valid &&
         !link_session_send_resume(session, LINK_MESSAGE_RESUME_DONE,
          &session->resume_done))
        return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
      return events;
    }
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  }

  session->peer_resume = peer;
  session->peer_resume_valid = 1;
  link_session_resume_state(session, &local);
  if((peer.flags & LINK_RESUME_FLAG_STREAM) != 0 &&
     (local.flags & LINK_RESUME_FLAG_STREAM) != 0)
  {
    stream_detail = link_session_make_stream_recovery_plan(session,
     &local, &peer);
    if(stream_detail != 0)
      return link_session_fail(session, LINK_REASON_PROTOCOL,
       stream_detail, 0);
  }
  link_session_recovery_snapshot(session, &peer, &snapshot);
  if(!link_recovery_note_snapshot(&session->recovery, &snapshot) ||
     !link_recovery_make_plan(&session->recovery,
      &session->recovery_plan) ||
     session->recovery_plan.kind == LINK_RECOVERY_PLAN_CLOSE ||
     !link_session_recovery_adopt_transfer(session, &peer,
      &session->recovery_plan) ||
     !link_recovery_apply_plan(&session->journal,
      &session->recovery_plan))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  if(session->stream_recovery_valid &&
     !link_session_apply_stream_recovery(session))
    return link_session_fail(session, LINK_REASON_PROTOCOL,
     LINK_STREAM_RECOVERY_APPLY_DETAIL, 0);
  link_session_resume_state(session, &session->resume_done);
  session->resume_done_valid = 1;
  if(!link_session_send_resume(session, LINK_MESSAGE_RESUME_DONE,
     &session->resume_done))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);

  if(session->stream_recovery_valid)
    events |= link_session_finish_stream_recovery(session);
  else
    events |= link_session_finish_recovery_plan(session,
     &session->recovery_plan);
  return events;
}

static uint32_t link_session_handle_resume_done(LinkSession *session,
 const LinkPacket *packet)
{
  LinkResumeState state;
  uint32_t events = LINK_SESSION_EVENT_NONE;

  if(session->role != LINK_ROLE_JOINER ||
     link_protocol_read_resume(packet, &state) != LINK_PROTOCOL_OK ||
     packet->sequence != state.last_sequence ||
     !link_session_resume_state_valid(&state) ||
     state.journal_role != LINK_JOURNAL_ROLE_HOST)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  if(session->state != LINK_SESSION_WAIT_RESUME_DONE)
  {
    if(!link_session_transport_recovering(session) &&
       session->resume_done_valid &&
       link_session_resume_matches(&session->resume_done, &state))
      return events;
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  }

  if(!link_session_recovery_done_matches(session, &state))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
  if(session->stream_recovery_valid &&
     (!link_session_stream_recovery_done_matches(session, &state) ||
      !link_session_apply_stream_recovery(session)))
    return link_session_fail(session, LINK_REASON_PROTOCOL,
     LINK_STREAM_RECOVERY_APPLY_DETAIL, 0);
  session->resume_done = state;
  session->resume_done_valid = 1;
  if(session->stream_recovery_valid)
    return link_session_finish_stream_recovery(session);
  return link_session_finish_recovery_plan(session,
   &session->recovery_plan);
}

static uint32_t link_session_send_heartbeat(LinkSession *session,
 uint8_t flags)
{
  LinkPacket packet;
  LinkStreamHeartbeat heartbeat;
  uint8_t token = flags & LINK_PROTOCOL_HEARTBEAT_TOKEN_MASK;
  int reply = (flags & LINK_PROTOCOL_HEARTBEAT_REPLY) != 0;

  link_protocol_packet_init(&packet, LINK_MESSAGE_HEARTBEAT,
   session->session_id, 0);
  packet.flags = flags;
  if((session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) != 0)
  {
    heartbeat.generation = link_stream_generation(&session->stream);
    heartbeat.clock_cycle = session->clock.local_cycle;
    heartbeat.acknowledged_sequence = session->last_sequence;
    if(link_protocol_write_stream_heartbeat(&packet, &heartbeat) !=
       LINK_PROTOCOL_OK)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);
    packet.flags = flags;
  }
  if(!link_session_send(session, &packet))
    return link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  session->heartbeat_tx_count++;
  if(reply)
    session->heartbeat_reply_tx_count++;
  else if(token != 0)
    session->heartbeat_request_tx_count++;
  if(token != 0)
    link_clock_note_published_at(&session->clock, link_session_now(session));
  return LINK_SESSION_EVENT_NONE;
}

static uint32_t link_session_handle_heartbeat(LinkSession *session,
 const LinkPacket *packet)
{
  LinkStreamHeartbeat heartbeat;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  int had_peer_clock;
  uint8_t token;

  if(!link_session_is_connected(session))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);

  if(packet->payload_size != 0)
  {
    if((session->negotiated_capabilities &
        LINK_CAPABILITY_STREAM_TRANSFER) == 0 ||
       link_protocol_read_stream_heartbeat(packet, &heartbeat) !=
        LINK_PROTOCOL_OK)
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
    if(heartbeat.generation != link_stream_generation(&session->stream))
      return LINK_SESSION_EVENT_NONE;
    had_peer_clock = session->clock.peer_valid != 0;
    link_clock_note_peer(&session->clock, heartbeat.generation,
     heartbeat.clock_cycle, link_session_now(session));
    link_clock_mark_hot(&session->clock, link_session_now(session));
    if(!had_peer_clock && session->peer_sio_valid)
    {
      gba_sio_set_peer_state(session->sio,
       (GbaSioMode)session->peer_sio.mode, session->peer_sio.control,
       session->peer_sio.rcnt,
       (session->peer_sio.flags & LINK_SIO_FLAG_GAMEPLAY_ACTIVE) != 0);
      events |= LINK_SESSION_EVENT_SIO_REPUBLISH;
    }
    if(session->role == LINK_ROLE_JOINER)
      link_stream_acknowledge(&session->stream,
       heartbeat.acknowledged_sequence);
  }

  session->heartbeat_rx_count++;
  token = packet->flags & LINK_PROTOCOL_HEARTBEAT_TOKEN_MASK;
  if((packet->flags & LINK_PROTOCOL_HEARTBEAT_REPLY) != 0)
  {
    session->heartbeat_reply_rx_count++;
    if(token != 0 && session->heartbeat_probe_pending &&
       token == session->heartbeat_outstanding_token)
    {
      session->heartbeat_probe_pending = 0;
      session->heartbeat_outstanding_token = 0;
    }
    else
      session->heartbeat_unmatched_reply_count++;
    return events;
  }

  /* RC3 liveness requests always use a non-zero token. Token-zero anchors
     from RC2 remain safe to receive during a staggered installation, but
     never create a reply. */
  if(token == 0)
    return events;
  session->heartbeat_request_rx_count++;
  if(session->heartbeat_last_request_valid &&
     token == session->heartbeat_last_request_token)
  {
    session->heartbeat_duplicate_request_count++;
    return events;
  }

  session->heartbeat_last_request_token = token;
  session->heartbeat_last_request_valid = 1;
  return events | link_session_send_heartbeat(session,
   (uint8_t)(token | LINK_PROTOCOL_HEARTBEAT_REPLY));
}

static uint32_t link_session_send_heartbeat_probe(LinkSession *session)
{
  uint8_t token;
  uint32_t events;
  uint64_t now = link_session_now(session);

  if(!link_clock_should_publish_after(&session->clock, now,
      LINK_SESSION_HEARTBEAT_US))
    return LINK_SESSION_EVENT_NONE;

  if(session->heartbeat_next_token == 0)
    session->heartbeat_next_token = 1u;
  token = (uint8_t)(session->heartbeat_next_token <<
   LINK_PROTOCOL_HEARTBEAT_TOKEN_SHIFT);

  events = link_session_send_heartbeat(session, token);

  if(session->state != LINK_SESSION_ERROR)
  {
    session->heartbeat_probe_pending = 1;
    session->heartbeat_outstanding_token = token;
    session->heartbeat_next_token =
     (uint8_t)((session->heartbeat_next_token + 1u) & 0x3Fu);
    if(session->heartbeat_next_token == 0)
      session->heartbeat_next_token = 1u;
  }
  return events;
}

static uint32_t link_session_handle_packet(LinkSession *session,
 const uint8_t *data, size_t size)
{
  LinkPacket packet;
  LinkProtocolError error;
  uint32_t events = LINK_SESSION_EVENT_NONE;
  int adopt_session = session->role == LINK_ROLE_JOINER &&
   session->session_id == 0 && session->state == LINK_SESSION_NEGOTIATING;

  error = link_protocol_decode(data, size, &packet);
  if(error != LINK_PROTOCOL_OK)
    return link_session_fail(session, LINK_REASON_PROTOCOL,
     (uint16_t)error, 0);

  session->last_message_type = packet.type;
  session->last_message_sequence = packet.sequence;

  if(!adopt_session && packet.session_id != session->session_id)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(!link_protocol_message_known(packet.type))
  {
    if(!link_session_is_connected(session) ||
       !link_session_sequence_is_current(session, packet.sequence))
      return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
    link_session_note_peer_activity(session);
    return events;
  }
  if((session->negotiated_capabilities &
      LINK_CAPABILITY_STREAM_TRANSFER) != 0 &&
     link_session_legacy_multiplayer_packet(&packet))
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 1);
  if(link_session_early_transfer_packet_stale(session, &packet))
  {
    link_session_note_peer_activity(session);
    return events;
  }

  switch(packet.type)
  {
    case LINK_MESSAGE_HELLO:
      events = link_session_handle_hello(session, &packet);
      break;

    case LINK_MESSAGE_SESSION_READY:
      events = link_session_handle_ready(session, &packet);
      break;

    case LINK_MESSAGE_SIO_STATE:
      events = link_session_handle_sio_state(session, &packet);
      break;

    case LINK_MESSAGE_CLOCK_SYNC:
      events = link_session_handle_clock(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_REQUEST:
      events = link_session_handle_request(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_PREPARE:
      events = link_session_handle_prepare(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_ACCEPT:
      events = link_session_handle_accept(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_COMMIT:
      events = link_session_handle_commit(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_COMMIT_ACK:
      events = link_session_handle_commit_ack(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_RELEASE:
      events = link_session_handle_release(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_RELEASE_ACK:
      events = link_session_handle_release_ack(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_START:
      events = session->benchmark.status != LINK_BENCHMARK_IDLE ?
       link_session_handle_benchmark_start(session, &packet) :
       link_session_handle_stream_start(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_REPLY:
      events = session->benchmark.status != LINK_BENCHMARK_IDLE ?
       link_session_handle_benchmark_reply(session, &packet) :
       link_session_handle_stream_reply(session, &packet);
      break;

    case LINK_MESSAGE_BENCHMARK_CONTROL:
      events = link_session_handle_benchmark_control(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_ABORT:
      events = link_session_handle_abort(session, &packet);
      break;

    case LINK_MESSAGE_TRANSFER_ABORT_ACK:
      events = link_session_handle_abort_ack(session, &packet);
      break;

    case LINK_MESSAGE_HEARTBEAT:
      events = link_session_handle_heartbeat(session, &packet);
      break;

    case LINK_MESSAGE_DISCONNECT:
      events = link_session_handle_disconnect(session, &packet);
      break;

    case LINK_MESSAGE_RESUME:
      events = link_session_handle_resume(session, &packet);
      break;

    case LINK_MESSAGE_RESUME_ACK:
      events = link_session_handle_resume_ack(session, &packet);
      break;

    case LINK_MESSAGE_RESUME_DONE:
      events = link_session_handle_resume_done(session, &packet);
      break;
  }

  if(session->state != LINK_SESSION_ERROR)
    link_session_note_peer_activity(session);
  return events;
}

static uint32_t link_session_handle_connected(LinkSession *session)
{
  uint32_t events = LINK_SESSION_EVENT_NONE;
  uint64_t now = link_session_now(session);

  if(session->state != LINK_SESSION_HOSTING &&
     session->state != LINK_SESSION_JOINING)
    return link_session_fail(session, LINK_REASON_PROTOCOL, 0, 0);

  session->transport_connected = 1;
  session->transport_health = LINK_TRANSPORT_HEALTH_HEALTHY;
  session->transport_resume_ready = 0;
  session->last_rx_us = now;
  session->last_tx_us = now;
  events |= link_session_set_state(session, LINK_SESSION_NEGOTIATING);

  if(session->role == LINK_ROLE_HOST && !link_session_send_hello(session))
    events |= link_session_fail(session, LINK_REASON_QUEUE_FULL, 0, 0);
  return events;
}

static int link_session_transport_generation_after(uint32_t left,
 uint32_t right)
{
  uint32_t distance = left - right;

  return distance != 0u && distance < 0x80000000u;
}

static void link_session_note_stale_transport_event(LinkSession *session)
{
  if(session->stale_transport_events != UINT32_MAX)
    session->stale_transport_events++;
}

static uint32_t link_session_service_benchmark(LinkSession *session,
 uint64_t now)
{
  LinkBenchmarkControl control;
  LinkStreamStart start;
  uint32_t sequence;
  uint32_t actions;

  actions = link_benchmark_poll(&session->benchmark, now, &control);
  if((actions & LINK_BENCHMARK_ACTION_SEND_CONTROL) != 0 &&
     !link_session_send_benchmark_control(session, &control))
    actions |= link_benchmark_fail(&session->benchmark,
     LINK_BENCHMARK_FAILURE_STREAM, now);
  if(session->benchmark.role == LINK_BENCHMARK_ROLE_HOST &&
     session->benchmark.status == LINK_BENCHMARK_RUNNING)
  {
    uint32_t begin = link_benchmark_host_begin_word(&session->benchmark,
     now, session->clock.local_cycle, &start, &sequence);

    actions |= begin;
    if((begin & LINK_BENCHMARK_ACTION_SEND_START) != 0 &&
       !link_session_send_stream_start(session, &start, sequence))
      actions |= link_benchmark_fail(&session->benchmark,
       LINK_BENCHMARK_FAILURE_STREAM, now);
  }
  return actions == LINK_BENCHMARK_ACTION_NONE ? LINK_SESSION_EVENT_NONE :
   LINK_SESSION_EVENT_BENCHMARK_UPDATE;
}

uint32_t link_session_pump(LinkSession *session)
{
  LinkTransportEvent transport_event;
  uint32_t events;
  uint64_t now;
  int poll_result;
  uint32_t processed = 0;

  if(session == NULL || !link_session_transport_valid(session))
    return LINK_SESSION_EVENT_NONE;

  events = session->pending_events;
  session->pending_events = LINK_SESSION_EVENT_NONE;

  do
  {
    poll_result = session->transport.ops->poll(session->transport.context,
     &transport_event);
    if(poll_result < 0)
    {
      events |= link_session_fail(session, LINK_REASON_TRANSPORT, 0, 0);
      break;
    }
    if(poll_result == 0)
      break;

    processed++;
    if(transport_event.generation == 0u)
    {
      events |= link_session_fail(session, LINK_REASON_TRANSPORT, 0, 0);
      break;
    }
    if(session->transport_generation == 0u)
    {
      if(transport_event.type != LINK_TRANSPORT_EVENT_CONNECTED)
      {
        events |= link_session_fail(session, LINK_REASON_TRANSPORT, 0, 0);
        break;
      }
      session->transport_generation = transport_event.generation;
    }
    else if(transport_event.generation != session->transport_generation)
    {
      int newer = link_session_transport_generation_after(
       transport_event.generation, session->transport_generation);
      int adopt = newer &&
       (transport_event.type == LINK_TRANSPORT_EVENT_RECOVERED ||
        transport_event.type == LINK_TRANSPORT_EVENT_CLOSED ||
        transport_event.type == LINK_TRANSPORT_EVENT_ERROR);

      if(!adopt)
      {
        link_session_note_stale_transport_event(session);
        continue;
      }
      session->transport_generation = transport_event.generation;
    }
    switch(transport_event.type)
    {
      case LINK_TRANSPORT_EVENT_CONNECTED:
        events |= link_session_handle_connected(session);
        break;

      case LINK_TRANSPORT_EVENT_PACKET:
        events |= link_session_handle_packet(session, transport_event.data,
         transport_event.size);
        break;

      case LINK_TRANSPORT_EVENT_ACTIVITY:
        link_session_note_peer_activity(session);
        break;

      case LINK_TRANSPORT_EVENT_RECOVERING:
      {
        LinkRecoverySnapshot snapshot;

        if(link_benchmark_active(&session->benchmark))
        {
          link_benchmark_fail(&session->benchmark,
           LINK_BENCHMARK_FAILURE_STREAM, link_session_now(session));
          events |= LINK_SESSION_EVENT_BENCHMARK_UPDATE;
        }

        if(!link_session_is_connected(session))
        {
          events |= link_session_fail(session, LINK_REASON_TRANSPORT,
           0, 0);
          break;
        }
        if(!link_session_transport_recovering(session))
        {
          /* No guest state was changed by an early, unlatched START.
             Recovery can request the host's immutable START again. */
          if(link_stream_discard_unlatched_start(&session->stream))
            session->stream_start_received_us = 0;
          if(session->role == LINK_ROLE_JOINER &&
             session->state == LINK_SESSION_WAIT_PREPARE &&
             session->transaction_stage == LINK_TRANSACTION_PROPOSED &&
             session->journal.stage == LINK_JOURNAL_EMPTY &&
             !link_session_transaction_begin(session,
              session->active_sequence))
          {
            events |= link_session_fail(session, LINK_REASON_PROTOCOL,
             0, 0);
            break;
          }
          session->recovery_previous_state = session->state;
          if(!link_recovery_begin(&session->recovery))
          {
            events |= link_session_fail(session, LINK_REASON_PROTOCOL,
             0, 0);
            break;
          }
          link_recovery_snapshot_journal(&session->journal, &snapshot);
          if(!link_recovery_note_snapshot(&session->recovery, &snapshot))
          {
            events |= link_session_fail(session, LINK_REASON_PROTOCOL,
             0, 0);
            break;
          }
          session->peer_resume_valid = 0;
          session->resume_done_valid = 0;
          session->stream_recovery_valid = 0;
          session->stream_recovery_pending = 0;
          session->stream_recovery_generation = 0;
        }
        session->transport_pause_started_us = 0;
        session->recovery_requested_us = 0;
        session->transport_health = LINK_TRANSPORT_HEALTH_RECOVERING;
        session->transport_resume_ready = 0;
        events |= link_session_set_state(session,
         LINK_SESSION_RECOVERING);
        break;
      }

      case LINK_TRANSPORT_EVENT_RECOVERED:
      {
        uint64_t recovered_at = link_session_now(session);

        if(!link_session_transport_recovering(session))
          break;
        session->state_started_us = recovered_at;
        session->transport_resume_ready = 1;
        if(session->ready_wait_active)
          session->ready_wait_started_us = recovered_at;
        session->last_rx_us = recovered_at;
        session->last_tx_us = recovered_at;
        session->heartbeat_probe_pending = 0;
        session->heartbeat_outstanding_token = 0;
        if(session->role == LINK_ROLE_HOST)
        {
          LinkResumeState resume;

          link_session_resume_state(session, &resume);
          if(!link_session_send_resume(session, LINK_MESSAGE_RESUME,
             &resume))
            events |= link_session_fail(session, LINK_REASON_QUEUE_FULL,
             0, 0);
          else
            events |= link_session_set_state(session,
             LINK_SESSION_WAIT_RESUME_ACK);
        }
        else
        {
          events |= link_session_set_state(session,
           LINK_SESSION_RECOVERING);
        }
        break;
      }

      case LINK_TRANSPORT_EVENT_CLOSED:
        events |= link_session_fail(session, LINK_REASON_PEER_CLOSED, 0, 0);
        break;

      case LINK_TRANSPORT_EVENT_ERROR:
        events |= link_session_fail(session, LINK_REASON_TRANSPORT,
         (uint16_t)transport_event.error, 0);
        break;

      case LINK_TRANSPORT_EVENT_DEGRADED:
        if(link_session_state_uses_connection(session->state) &&
           session->transport_health == LINK_TRANSPORT_HEALTH_HEALTHY)
        {
          link_session_begin_transport_pause(session);
          session->transport_health = LINK_TRANSPORT_HEALTH_PRESSURED;
        }
        break;

      case LINK_TRANSPORT_EVENT_HEALTHY:
        if(link_session_transport_pressured(session))
        {
          session->transport_health = LINK_TRANSPORT_HEALTH_HEALTHY;
          link_session_resume_transport_deadlines(session,
           link_session_now(session));
        }
        break;
    }
    if(link_session_record_invariants(session) != 0 &&
       session->state != LINK_SESSION_ERROR &&
       session->state != LINK_SESSION_DISCONNECTED)
    {
      events |= link_session_fail(session, LINK_REASON_PROTOCOL,
       (uint16_t)session->invariant_violations, 0);
      break;
    }
  }
  while(processed < 64 && session->state != LINK_SESSION_ERROR &&
        session->state != LINK_SESSION_DISCONNECTED);

  if(!link_session_state_uses_connection(session->state))
    return link_session_finish_pump(session, events);

  now = link_session_now(session);
  if(session->state == LINK_SESSION_CONNECTED)
  {
    events |= link_session_refresh_sio_presence(session, now);
    if(session->state == LINK_SESSION_CONNECTED)
      events |= link_session_service_benchmark(session, now);
  }
  if(link_session_transport_recovering(session))
  {
    events |= link_session_retry_resume(session, now);
    return link_session_finish_pump(session, events);
  }
  if(link_session_transport_recovery_requested(session))
  {
    if(now >= session->recovery_requested_us &&
       now - session->recovery_requested_us >=
        LINK_SESSION_LIVENESS_TIMEOUT_US)
    {
      session->timeout_source = LINK_TIMEOUT_RECOVERY_RENDEZVOUS;
      session->last_timeout_source = LINK_TIMEOUT_RECOVERY_RENDEZVOUS;
      events |= link_session_fail(session, LINK_REASON_TRANSPORT,
       (uint16_t)session->last_transfer_reason, 0);
    }
    return link_session_finish_pump(session, events);
  }
  if(link_session_transport_pressured(session))
  {
    if(now >= session->transport_pause_started_us &&
       now - session->transport_pause_started_us >= LINK_SESSION_TIMEOUT_US)
    {
      events |= link_session_request_transport_recovery(session,
       LINK_REASON_TIMEOUT, 0, LINK_TIMEOUT_PRESSURE);
    }
    return link_session_finish_pump(session, events);
  }

  events |= link_session_service_stream_start(session);
  events |= link_session_service_deferred_prepare(session);
  if(session->state == LINK_SESSION_ERROR ||
     session->state == LINK_SESSION_DISCONNECTED)
    return link_session_finish_pump(session, events);

  if(session->barrier_active &&
     now - session->barrier_started_us >=
      LINK_SESSION_TRANSFER_TIMEOUT_US &&
     session->transaction_stage != LINK_TRANSACTION_DECIDED &&
     session->transaction_stage != LINK_TRANSACTION_ACKNOWLEDGED)
  {
    events |= link_session_abort_or_recover(session, LINK_REASON_TIMEOUT, 0,
     session->active_sequence, 1, LINK_TIMEOUT_ACTIVE_TRANSFER);
    return link_session_finish_pump(session, events);
  }

  if(session->state == LINK_SESSION_NEGOTIATING ||
     session->state == LINK_SESSION_WAIT_READY ||
     session->state == LINK_SESSION_WAIT_READY_ACK)
  {
    if(now - session->state_started_us >= LINK_SESSION_TIMEOUT_US)
    {
      session->timeout_source = LINK_TIMEOUT_NEGOTIATION;
      session->last_timeout_source = LINK_TIMEOUT_NEGOTIATION;
      events |= link_session_fail(session, LINK_REASON_TIMEOUT, 0, 1);
    }
    return link_session_finish_pump(session, events);
  }

  if(session->state == LINK_SESSION_WAIT_ABORT_ACK &&
     now - session->state_started_us >= LINK_SESSION_ABORT_ACK_TIMEOUT_US)
  {
    session->timeout_source = LINK_TIMEOUT_ABORT_ACK;
    session->last_timeout_source = LINK_TIMEOUT_ABORT_ACK;
    events |= link_session_fail(session, LINK_REASON_TIMEOUT, 0, 0);
    return link_session_finish_pump(session, events);
  }

  /* The network stack can report hard failures while a peer is in its menu.
     Do not mistake a paused application heartbeat for a dead connection.
     During gameplay, receive silence is a recovery cause before it is a
     terminal session error. */
  if(!link_session_peer_in_menu(session) &&
     now - session->last_rx_us >= LINK_SESSION_TIMEOUT_US)
  {
    LinkTimeoutSource silence_source =
     session->state == LINK_SESSION_WAIT_ABORT_ACK &&
     session->timeout_source != LINK_TIMEOUT_NONE ?
      session->timeout_source : LINK_TIMEOUT_RECEIVE_SILENCE;

    events |= link_session_request_transport_recovery(session,
     LINK_REASON_TIMEOUT, 0, silence_source);
    return link_session_finish_pump(session, events);
  }

  if(now - session->last_rx_us >= LINK_SESSION_HEARTBEAT_US)
    events |= link_session_send_heartbeat_probe(session);
  return link_session_finish_pump(session, events);
}
