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

#ifndef PSP_ADHOC_H
#define PSP_ADHOC_H

#include "link_transport.h"

typedef enum
{
  PSP_ADHOC_STOPPED,
  PSP_ADHOC_STARTING,
  PSP_ADHOC_READY,
  PSP_ADHOC_HOSTING,
  PSP_ADHOC_SEARCHING,
  PSP_ADHOC_CONNECTING,
  PSP_ADHOC_CONNECTED,
  PSP_ADHOC_RECOVERING,
  PSP_ADHOC_FAILED
} PspAdhocState;

typedef enum
{
  PSP_ADHOC_PHASE_NONE,
  PSP_ADHOC_PHASE_WLAN,
  PSP_ADHOC_PHASE_THREAD,
  PSP_ADHOC_PHASE_LOAD_COMMON,
  PSP_ADHOC_PHASE_LOAD_ADHOC,
  PSP_ADHOC_PHASE_NET_INIT,
  PSP_ADHOC_PHASE_ADHOC_INIT,
  PSP_ADHOC_PHASE_CONTROL_INIT,
  PSP_ADHOC_PHASE_HOST_CREATE,
  PSP_ADHOC_PHASE_CONTROL_WAIT,
  PSP_ADHOC_PHASE_MAC_ADDRESS,
  PSP_ADHOC_PHASE_SCAN_START,
  PSP_ADHOC_PHASE_SCAN_WAIT,
  PSP_ADHOC_PHASE_SCAN_RESULTS,
  PSP_ADHOC_PHASE_JOIN_GROUP,
  PSP_ADHOC_PHASE_PEER_LIST,
  PSP_ADHOC_PHASE_PDP_CREATE,
  PSP_ADHOC_PHASE_PDP_HANDSHAKE,
  PSP_ADHOC_PHASE_PDP_SEND,
  PSP_ADHOC_PHASE_PDP_RECEIVE,
  PSP_ADHOC_PHASE_TRANSPORT_RECOVER,
  PSP_ADHOC_PHASE_GROUP_RECOVER,
  PSP_ADHOC_PHASE_STACK_RECOVER,
  PSP_ADHOC_PHASE_RECOVERY_FAILED,
  PSP_ADHOC_PHASE_HEALTH_CHECK,
  PSP_ADHOC_PHASE_SHUTDOWN
} PspAdhocPhase;

typedef enum
{
  PSP_ADHOC_ERROR_NONE = 0,
  PSP_ADHOC_ERROR_WLAN_OFF = -1,
  PSP_ADHOC_ERROR_THREAD = -2,
  PSP_ADHOC_ERROR_COMMAND_QUEUE = -3,
  PSP_ADHOC_ERROR_EVENT_QUEUE = -4,
  PSP_ADHOC_ERROR_PROTOCOL = -5,
  PSP_ADHOC_ERROR_SHUTDOWN = -6,
  PSP_ADHOC_ERROR_SESSION_RECOVERY = -7,
  PSP_ADHOC_ERROR_TIMEOUT = -8,
  PSP_ADHOC_ERROR_PEER_RECOVERY = -9,
  PSP_ADHOC_ERROR_TRANSPORT_RECOVERY = -10
} PspAdhocError;

typedef enum
{
  PSP_ADHOC_ERROR_DOMAIN_NONE,
  PSP_ADHOC_ERROR_DOMAIN_PSP_API,
  PSP_ADHOC_ERROR_DOMAIN_TRANSPORT,
  PSP_ADHOC_ERROR_DOMAIN_SESSION,
  PSP_ADHOC_ERROR_DOMAIN_LIFECYCLE,
  PSP_ADHOC_ERROR_DOMAIN_INTERNAL
} PspAdhocErrorDomain;

typedef enum
{
  PSP_ADHOC_TRANSITION_NONE,
  PSP_ADHOC_TRANSITION_MENU,
  PSP_ADHOC_TRANSITION_LINK_START,
  PSP_ADHOC_TRANSITION_MENU_EXIT,
  PSP_ADHOC_TRANSITION_ROM_READY,
  PSP_ADHOC_TRANSITION_UI_READY,
  PSP_ADHOC_TRANSITION_CLOCK_READY,
  PSP_ADHOC_TRANSITION_GAMEPLAY
} PspAdhocTransitionPhase;

#define PSP_ADHOC_HEALTH_CAUSE_ACK_STALL    (1u << 0)
#define PSP_ADHOC_HEALTH_CAUSE_BACKPRESSURE (1u << 1)

typedef struct
{
  int control_result;
  int control_state;
  int socket_result;
  int socket_present;
  int api_error;
  unsigned int api_phase;
  unsigned int error_domain;
  int internal_error_code;
  unsigned int internal_error_phase;
  unsigned int timeout_source;
  unsigned int sent_bytes;
  unsigned int received_bytes;
  unsigned int cpu_clock;
  unsigned int bus_clock;
  unsigned int backpressure_events;
  unsigned int backpressure_age_ms;
  unsigned int backpressure_streak;
  unsigned int backpressure_episode_count;
  unsigned int backpressure_max_age_ms;
  unsigned int backpressure_streak_high_water;
  unsigned int last_blocked_kind;
  unsigned int last_blocked_state_class;
  unsigned int clock_backoff_us;
  unsigned int clock_next_delay_us;
  unsigned int clock_success_streak;
  unsigned int clock_backoff_raises;
  unsigned int clock_deferred;
  unsigned int health_causes;
  unsigned int session_recovery_requests;
  unsigned int session_recovery_reason;
  unsigned int tx_datagrams;
  unsigned int rx_datagrams;
  unsigned int retransmissions;
  unsigned int duplicates;
  unsigned int out_of_window;
  unsigned int invalid_packets;
  unsigned int ack_packets;
  unsigned int received_data_packets;
  unsigned int received_ack_packets;
  unsigned int ack_advances;
  unsigned int ack_progress_age_ms;
  unsigned int probes;
  unsigned int rto_us;
  unsigned int rtt_estimate_us;
  unsigned int queue_sojourn_high_water_us;
  unsigned int api_retries;
  unsigned int tx_base_sequence;
  unsigned int tx_base_attempts;
  unsigned int tx_base_message;
  unsigned int tx_base_age_ms;
  unsigned int next_tx_sequence;
  unsigned int next_rx_sequence;
  unsigned int buffered_packets;
  unsigned int outstanding_packets;
  unsigned int tx_queue_depth;
  unsigned int tx_queue_high_water;
  unsigned int tx_queue_coalesced;
  unsigned int tx_queue_rejected;
  unsigned int tx_critical_handoff_depth;
  unsigned int tx_critical_handoff_high_water;
  unsigned int tx_critical_pending_depth;
  unsigned int tx_critical_pending_high_water;
  unsigned int tx_critical_retry_suppressed;
  unsigned int hot_wake_count;
  unsigned int hot_wake_latency_max_us;
  unsigned int hot_drain_count;
  unsigned int hot_drain_packets;
  unsigned int hot_drain_high_water;
  unsigned int hot_yields;
  unsigned int hot_time_ms;
  unsigned int cold_time_ms;
  unsigned int tx_sio_mailbox_depth;
  unsigned int tx_sio_published;
  unsigned int tx_sio_coalesced;
  unsigned int tx_sio_high_water;
  unsigned int tx_heartbeat_mailbox_depth;
  unsigned int tx_heartbeat_published;
  unsigned int tx_heartbeat_coalesced;
  unsigned int tx_heartbeat_high_water;
  unsigned int tx_clock_mailbox_depth;
  unsigned int tx_clock_published;
  unsigned int tx_clock_coalesced;
  unsigned int tx_clock_high_water;
  unsigned int transport_resyncs;
  unsigned int group_recoveries;
  unsigned int stack_recoveries;
  unsigned int recovery_state;
  unsigned int resync_state;
  int recovery_terminal_error;
  unsigned int recovery_terminal_phase;
  uint32_t transport_epoch;
  uint32_t last_received_sequence;
  unsigned int peer_activity;
  unsigned int consumed_activity;
  PspAdhocTransitionPhase transition_phase;
  unsigned int shutdown_phase;
  uint32_t cancellation_token;
} PspAdhocFailureSnapshot;

int psp_adhoc_start(void);
void psp_adhoc_stop(void);
void psp_adhoc_stop_for_exit(void);
int psp_adhoc_service_shutdown(void);
int psp_adhoc_shutdown_pending(void);
LinkTransport psp_adhoc_transport(void);
int psp_adhoc_wlan_enabled(void);
PspAdhocState psp_adhoc_state(void);
int psp_adhoc_last_error(void);
PspAdhocPhase psp_adhoc_failure_phase(void);
const char *psp_adhoc_phase_code(PspAdhocPhase phase);
unsigned int psp_adhoc_failure_free_memory(void);
void psp_adhoc_set_transition_phase(PspAdhocTransitionPhase phase);
void psp_adhoc_get_failure_snapshot(PspAdhocFailureSnapshot *snapshot);

#endif /* PSP_ADHOC_H */
