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

#ifndef LINK_SESSION_H
#define LINK_SESSION_H

#include <stdint.h>

#include "gba_sio.h"
#include "link_benchmark.h"
#include "link_clock.h"
#include "link_recovery_coordinator.h"
#include "link_protocol.h"
#include "link_stream.h"
#include "link_transport.h"

#define LINK_SESSION_REQUIRED_CAPABILITIES \
 (LINK_CAPABILITY_NORMAL_8 | LINK_CAPABILITY_NORMAL_32 | \
  LINK_CAPABILITY_MULTI | LINK_CAPABILITY_TWO_PLAYER | \
  LINK_CAPABILITY_PRESENCE | LINK_CAPABILITY_CLOCK_SYNC | \
  LINK_CAPABILITY_RECOVERY | LINK_CAPABILITY_TRANSACTIONAL | \
  LINK_CAPABILITY_STREAM_TRANSFER)

#define LINK_SESSION_SUPPORTED_CAPABILITIES LINK_SESSION_REQUIRED_CAPABILITIES

/* A low-rate v3 clock sample is only a short pacing hint, not a barrier. */
#define LINK_SESSION_STREAM_PACING_US 2000u
#define LINK_SESSION_HEARTBEAT_US 1000000u
#define LINK_SESSION_TIMEOUT_US   5000000u
#define LINK_SESSION_TRANSFER_TIMEOUT_US 12000000u
#define LINK_SESSION_LIVENESS_TIMEOUT_US 45000000u
#define LINK_SESSION_READY_TIMEOUT_US 2000000u
#define LINK_SESSION_RESUME_RETRY_US 500000u
#define LINK_SESSION_ABORT_ACK_TIMEOUT_US 15000000u

typedef enum
{
  LINK_ROLE_NONE,
  LINK_ROLE_HOST,
  LINK_ROLE_JOINER
} LinkRole;

typedef enum
{
  LINK_TRANSACTION_EMPTY,
  LINK_TRANSACTION_PROPOSED,
  LINK_TRANSACTION_PREPARED,
  LINK_TRANSACTION_DECIDED,
  LINK_TRANSACTION_ACKNOWLEDGED,
  LINK_TRANSACTION_ABORTED
} LinkTransactionStage;

typedef enum
{
  LINK_INVARIANT_OK = 0,
  LINK_INVARIANT_TRANSACTION_SEQUENCE = 1u << 0,
  LINK_INVARIANT_TRANSACTION_RECORD = 1u << 1,
  LINK_INVARIANT_COMMIT_STATE = 1u << 2,
  LINK_INVARIANT_COMMIT_IDENTITY = 1u << 3,
  LINK_INVARIANT_BARRIER_STATE = 1u << 4,
  LINK_INVARIANT_ROLE_STATE = 1u << 5,
  LINK_INVARIANT_DEFERRED_PREPARE = 1u << 6,
  LINK_INVARIANT_JOURNAL = 1u << 7,
  LINK_INVARIANT_SIO_HORIZON = 1u << 8,
  LINK_INVARIANT_TRANSPORT_HEALTH = 1u << 9
} LinkInvariantViolation;

typedef enum
{
  LINK_SESSION_IDLE,
  LINK_SESSION_HOSTING,
  LINK_SESSION_JOINING,
  LINK_SESSION_NEGOTIATING,
  LINK_SESSION_WAIT_READY,
  LINK_SESSION_WAIT_READY_ACK,
  LINK_SESSION_CONNECTED,
  LINK_SESSION_WAIT_PREPARE,
  LINK_SESSION_WAIT_ACCEPT,
  LINK_SESSION_WAIT_COMMIT,
  LINK_SESSION_WAIT_COMMIT_ACK,
  LINK_SESSION_WAIT_COMMIT_RELEASE,
  LINK_SESSION_WAIT_RELEASE_ACK,
  LINK_SESSION_WAIT_ABORT_ACK,
  LINK_SESSION_WAIT_STREAM_REPLY,
  LINK_SESSION_RECOVERING,
  LINK_SESSION_WAIT_RESUME_ACK,
  LINK_SESSION_WAIT_RESUME_DONE,
  LINK_SESSION_DISCONNECTED,
  LINK_SESSION_ERROR
} LinkSessionState;

typedef enum
{
  LINK_REASON_NONE,
  LINK_REASON_USER,
  LINK_REASON_TIMEOUT,
  LINK_REASON_TRANSPORT,
  LINK_REASON_PROTOCOL,
  LINK_REASON_INCOMPATIBLE,
  LINK_REASON_MODE_MISMATCH,
  LINK_REASON_QUEUE_FULL,
  LINK_REASON_PEER_CLOSED,
  LINK_REASON_SUSPEND,
  LINK_REASON_ROM_CHANGE
} LinkSessionReason;

typedef enum
{
  LINK_SESSION_PAUSE_NONE,
  LINK_SESSION_PAUSE_SIO_DEPENDENCY,
  LINK_SESSION_PAUSE_AHEAD_PACING,
  LINK_SESSION_PAUSE_RECOVERY
} LinkSessionPauseReason;

typedef enum
{
  LINK_SESSION_EVENT_NONE               = 0,
  LINK_SESSION_EVENT_STATE_CHANGE       = 1u << 0,
  LINK_SESSION_EVENT_BARRIER_BEGIN      = 1u << 1,
  LINK_SESSION_EVENT_BARRIER_END        = 1u << 2,
  LINK_SESSION_EVENT_SERIAL_IRQ         = 1u << 3,
  LINK_SESSION_EVENT_TRANSFER_COMMITTED = 1u << 4,
  LINK_SESSION_EVENT_TRANSFER_ABORTED   = 1u << 5,
  LINK_SESSION_EVENT_SESSION_CLOSED     = 1u << 6,
  LINK_SESSION_EVENT_SIO_REPUBLISH      = 1u << 7,
  LINK_SESSION_EVENT_BENCHMARK_UPDATE   = 1u << 8
} LinkSessionEvent;

typedef struct
{
  GbaSio *sio;
  LinkTransport transport;
  LinkHello local_hello;
  LinkHello peer_hello;
  LinkSioState peer_sio;
  LinkSioState local_sio_latest;
  LinkSioState local_sio_sent;
  LinkTransferData transfer;
  LinkTransferData applied_transfer;
  LinkTransferData deferred_prepare;
  LinkResumeState peer_resume;
  LinkResumeState resume_done;
  LinkClock clock;
  LinkBenchmark benchmark;
  LinkJournal journal;
  LinkRecoveryCoordinator recovery;
  LinkRecoveryPlan recovery_plan;
  LinkStream stream;
  LinkRole role;
  LinkSessionState state;
  LinkTransportHealth transport_health;
  LinkTransactionStage transaction_stage;
  LinkSessionReason reason;
  LinkTimeoutSource timeout_source;
  LinkTimeoutSource last_timeout_source;
  uint16_t reason_detail;
  LinkSessionReason last_transfer_reason;
  uint16_t last_transfer_detail;
  LinkSessionState terminal_previous_state;
  LinkSessionState recovery_previous_state;
  uint32_t session_id;
  uint32_t negotiated_capabilities;
  uint32_t pending_events;
  uint32_t invariant_violations;
  uint32_t last_sequence;
  uint32_t active_sequence;
  uint32_t barrier_cycle;
  uint32_t transport_generation;
  uint32_t stale_transport_events;
  uint32_t transaction_sequence;
  uint32_t transaction_hash;
  uint32_t applied_sequence;
  uint32_t applied_hash;
  uint32_t deferred_prepare_sequence;
  uint32_t sio_revision;
  uint64_t state_started_us;
  uint64_t barrier_started_us;
  uint64_t last_rx_us;
  uint64_t last_tx_us;
  uint64_t last_sio_publish_us;
  uint64_t ready_wait_started_us;
  uint64_t transport_pause_started_us;
  uint64_t recovery_requested_us;
  uint64_t barrier_last_us;
  uint64_t barrier_max_us;
  uint64_t stream_burst_started_us;
  uint64_t stream_burst_last_us;
  uint64_t stream_burst_max_us;
  uint32_t last_message_sequence;
  uint32_t terminal_sequence;
  uint32_t heartbeat_tx_count;
  uint32_t heartbeat_rx_count;
  uint32_t heartbeat_request_tx_count;
  uint32_t heartbeat_request_rx_count;
  uint32_t heartbeat_reply_tx_count;
  uint32_t heartbeat_reply_rx_count;
  uint32_t heartbeat_duplicate_request_count;
  uint32_t heartbeat_unmatched_reply_count;
  uint32_t stream_start_tx_count;
  uint32_t stream_reply_tx_count;
  uint32_t legacy_transfer_tx_count;
  uint32_t stream_recovery_generation;
  uint32_t stream_burst_words;
  uint32_t stream_local_start_cycle;
  uint64_t stream_start_received_us;
  uint8_t peer_minor;
  uint8_t peer_sio_valid;
  uint8_t local_sio_valid;
  uint8_t local_sio_sent_valid;
  uint8_t transport_connected;
  uint8_t transport_resume_ready;
  uint8_t barrier_active;
  uint8_t ready_wait_active;
  uint8_t last_message_type;
  uint8_t terminal_message_type;
  uint8_t terminal_valid;
  uint8_t heartbeat_probe_pending;
  uint8_t heartbeat_outstanding_token;
  uint8_t heartbeat_next_token;
  uint8_t heartbeat_last_request_token;
  uint8_t heartbeat_last_request_valid;
  uint8_t peer_resume_valid;
  uint8_t resume_done_valid;
  uint8_t stream_recovery_valid;
  uint8_t stream_recovery_pending;
  uint8_t transaction_applied;
  uint8_t deferred_prepare_valid;
  uint8_t deferred_prepare_idle_seen;
  uint8_t journal_valid;
} LinkSession;

void link_session_init(LinkSession *session, GbaSio *sio,
 LinkTransport transport, const LinkHello *hello);

int link_session_host(LinkSession *session, uint32_t session_id);
int link_session_join(LinkSession *session);
uint32_t link_session_disconnect(LinkSession *session,
 LinkSessionReason reason);

uint32_t link_session_pump(LinkSession *session);
uint32_t link_session_begin_transfer(LinkSession *session);
uint32_t link_session_publish_sio_state(LinkSession *session,
 const LinkSioState *state);
uint32_t link_session_publish_clock(LinkSession *session,
 uint32_t emulated_cycle);
void link_session_set_emulated_cycle(LinkSession *session,
 uint32_t emulated_cycle);

LinkSessionState link_session_state(const LinkSession *session);
int link_session_benchmark_available(const LinkSession *session);
int link_session_benchmark_start(LinkSession *session,
 const LinkBenchmarkControl *control);
uint32_t link_session_benchmark_stop(LinkSession *session);
void link_session_benchmark_clear(LinkSession *session);
void link_session_benchmark_summary(const LinkSession *session,
 LinkBenchmarkSummary *summary);
int link_session_is_connected(const LinkSession *session);
int link_session_in_barrier(const LinkSession *session);
LinkSessionPauseReason link_session_pause_reason(const LinkSession *session);
int link_session_should_pause(const LinkSession *session);
int link_session_should_throttle(const LinkSession *session);
uint32_t link_session_cycles_until_pause(const LinkSession *session);
uint32_t link_session_check_invariants(const LinkSession *session);

#endif /* LINK_SESSION_H */
