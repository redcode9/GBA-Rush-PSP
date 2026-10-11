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

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <pspkernel.h>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <psppower.h>
#include <pspsysmem.h>
#include <psputility_netmodules.h>
#include <pspwlan.h>

#include "adhoc_datagram.h"
#include "adhoc_peer.h"
#include "adhoc_recovery.h"
#include "adhoc_rendezvous.h"
#include "adhoc_resync.h"
#include "lifecycle_teardown.h"
#include "link_protocol.h"
#include "link_transport_scheduler.h"
#include "psp_adhoc.h"
#include "runtime_log_psp.h"

#define PSP_ADHOC_COMMAND_CAPACITY 8u
#define PSP_ADHOC_EVENT_CAPACITY   64u
#define PSP_ADHOC_SCAN_CAPACITY    16u
#define PSP_ADHOC_PEER_CAPACITY    4u
#define PSP_ADHOC_SOCKET_BUFFER   32768u
#define PSP_ADHOC_RECEIVE_BATCH      32u
#define PSP_ADHOC_RECEIVE_SCRATCH   1472u
#define PSP_ADHOC_PORT             31000u
#define PSP_ADHOC_CLIENT_PORT      31001u
#define PSP_ADHOC_CONTROL_TIMEOUT 10000000u
#define PSP_ADHOC_CONNECT_TIMEOUT 10000000u
#define PSP_ADHOC_PEER_TIMEOUT     6000000u
#define PSP_ADHOC_HANDSHAKE_RETRY  100000u
#define PSP_ADHOC_HANDSHAKE_ACKS   3
#define PSP_ADHOC_POLL_DELAY       1000u
#define PSP_ADHOC_CONTROL_DELAY    10000u
#define PSP_ADHOC_TX_WAKE_BIT          1u
#define PSP_ADHOC_HOT_ACK_GRACE_US   500u
#define PSP_ADHOC_HOT_WINDOW_US     50000u
#define PSP_ADHOC_HOT_YIELD_US         50u
#define PSP_ADHOC_HOT_DRAIN_BUDGET_US 1000u
#define PSP_ADHOC_HOT_DRAIN_MAX         8u
#define PSP_ADHOC_SHUTDOWN_TIMEOUT 4000000u
#define PSP_ADHOC_EXIT_TIMEOUT      250000u
#define PSP_ADHOC_CONTROL_RETRY    750000u
#define PSP_ADHOC_HEALTH_GRACE     1500000u
#define PSP_ADHOC_ACK_PROGRESS_TIMEOUT 2000000u
#define PSP_ADHOC_ACK_HARD_TIMEOUT 6000000u
#define PSP_ADHOC_RECOVERY_IDLE             0u
#define PSP_ADHOC_PROBE_TIMEOUT       6000000u
#define PSP_ADHOC_TRANSPORT_RECOVERY  8000000u
#define PSP_ADHOC_GROUP_RECOVERY     15000000u
#define PSP_ADHOC_STACK_RECOVERY     12000000u
#define PSP_ADHOC_PLATFORM_RETRY       250000u
#define PSP_ADHOC_RECOVERY_SETTLE      750000u
#define PSP_ADHOC_RECOVERY_REQUEST_RETRY 100000u
#define PSP_ADHOC_RECOVERY_REQUEST_TIMEOUT 20000000u
#define PSP_ADHOC_GROUP_OPERATION    12000000u
#define PSP_ADHOC_DISCONNECT_WAIT     2000000u

#define PSP_ADHOC_ERROR_DOMAIN_RECORDING UINT32_MAX

#define SCE_NET_ERROR_NO_SPACE             ((int)0x80410001u)
#define SCE_ADHOC_ERROR_SOCKET_DELETED     ((int)0x80410707u)
#define SCE_ADHOC_ERROR_WOULD_BLOCK        ((int)0x80410709u)
#define SCE_ADHOC_ERROR_DISCONNECTED       ((int)0x8041070cu)
#define SCE_ADHOC_ERROR_TIMEOUT            ((int)0x80410715u)
#define SCE_ADHOCCTL_ERROR_NO_SCAN_INFO     ((int)0x80410b0au)
#define SCE_ADHOCCTL_ERROR_BUSY             ((int)0x80410b10u)

static const char psp_adhoc_group[8] =
 { 'G', 'B', 'A', 'R', 'U', 'S', 'H', '\0' };

static const AdhocRecoveryConfig psp_adhoc_recovery_config =
{
  PSP_ADHOC_RECOVERY_IDLE,
  PSP_ADHOC_PROBE_TIMEOUT,
  PSP_ADHOC_TRANSPORT_RECOVERY,
  PSP_ADHOC_GROUP_RECOVERY,
  PSP_ADHOC_STACK_RECOVERY,
  PSP_ADHOC_PLATFORM_RETRY
};

typedef enum
{
  PSP_ADHOC_COMMAND_HOST,
  PSP_ADHOC_COMMAND_JOIN,
  PSP_ADHOC_COMMAND_DISCONNECT,
  PSP_ADHOC_COMMAND_STOP
} PspAdhocCommandType;

typedef enum
{
  PSP_ADHOC_IO_OK,
  PSP_ADHOC_IO_ERROR,
  PSP_ADHOC_IO_PEER_CLOSED
} PspAdhocIoResult;

typedef enum
{
  PSP_ADHOC_SEND_IDLE,
  PSP_ADHOC_SEND_SENT,
  PSP_ADHOC_SEND_SENT_CRITICAL,
  PSP_ADHOC_SEND_BLOCKED
} PspAdhocSendProgress;

typedef enum
{
  PSP_ADHOC_SERVICE_ERROR,
  PSP_ADHOC_SERVICE_CLOSED,
  PSP_ADHOC_SERVICE_PEER_CLOSED
} PspAdhocServiceResult;

typedef struct
{
  PspAdhocCommandType type;
} PspAdhocCommand;

typedef struct
{
  PspAdhocCommand commands[PSP_ADHOC_COMMAND_CAPACITY];
  LinkTransportEvent events[PSP_ADHOC_EVENT_CAPACITY];
  volatile uint32_t command_read;
  volatile uint32_t command_write;
  volatile uint32_t event_read;
  volatile uint32_t event_write;
  volatile int close_requested;
  volatile int fallback_error;
  volatile int fallback_pending;
  volatile uint32_t fallback_generation;
  volatile PspAdhocState state;
  volatile int last_error;
  volatile int failure_error;
  volatile int failure_valid;
  volatile int api_error;
  volatile uint32_t api_error_valid;
  volatile PspAdhocPhase api_error_phase;
  volatile uint32_t error_domain;
  volatile int internal_error_code;
  volatile PspAdhocPhase internal_error_phase;
  volatile LinkTimeoutSource timeout_source;
  volatile PspAdhocPhase phase;
  volatile PspAdhocPhase failure_phase;
  volatile unsigned int failure_free_memory;
  volatile unsigned int backpressure_events;
  volatile unsigned int backpressure_episode_count;
  volatile unsigned int backpressure_max_age_ms;
  volatile unsigned int backpressure_streak_high_water;
  volatile unsigned int last_blocked_kind;
  volatile unsigned int last_blocked_state_class;
  volatile unsigned int clock_backoff_raises;
  volatile unsigned int clock_deferred;
  volatile unsigned int health_causes;
  volatile unsigned int health_cause_history;
  volatile unsigned int session_recovery_requests;
  volatile unsigned int session_recovery_last_reason;
  volatile unsigned int tx_datagrams;
  volatile unsigned int rx_datagrams;
  volatile unsigned int api_retries;
  volatile unsigned int sent_bytes;
  volatile unsigned int tx_queue_high_water;
  volatile unsigned int hot_wake_count;
  volatile unsigned int hot_wake_latency_max_us;
  volatile unsigned int hot_drain_count;
  volatile unsigned int hot_drain_packets;
  volatile unsigned int hot_drain_high_water;
  volatile unsigned int hot_yields;
  volatile uint32_t hot_wake_generation;
  volatile uint32_t hot_wake_consumed;
  volatile uint32_t hot_wake_published_us;
  volatile uint32_t activity_generation;
  volatile uint32_t activity_consumed;
  volatile uint32_t activity_epoch;
  volatile uint32_t health_generation;
  volatile uint32_t health_consumed;
  volatile uint32_t health_epoch;
  volatile uint32_t transport_degraded;
  volatile uint32_t session_recovery_generation;
  volatile uint32_t session_recovery_consumed;
  volatile int session_recovery_reason;
  volatile LinkTimeoutSource session_recovery_timeout_source;
  volatile unsigned int resync_state;
  uint32_t resync_requests_accepted;
  uint32_t resync_requests_rejected;
  uint32_t recovery_id;
  uint32_t last_completed_recovery_id;
  uint32_t recovery_request_id;
  uint64_t recovery_request_started_us;
  uint64_t recovery_request_next_us;
  uint64_t ack_stall_started_us;
  AdhocResyncRequestGate resync_request_gate;
  volatile int recovery_trigger_valid;
  volatile int recovery_trigger_error;
  volatile PspAdhocPhase recovery_trigger_phase;
  int recovery_notified;
  LifecycleTeardown teardown;
  PspAdhocFailureSnapshot failure_snapshot;
  SceUID thread_id;
  SceUID tx_wake_id;
  int module_common;
  int module_adhoc;
  int net_initialized;
  int adhoc_initialized;
  int control_initialized;
  volatile int control_connected;
  volatile int socket_id;
  unsigned char local_mac[ADHOC_PEER_MAC_SIZE];
  unsigned char peer_mac[ADHOC_PEER_MAC_SIZE];
  unsigned short local_port;
  unsigned short peer_port;
  uint32_t connection_id;
  AdhocDatagram datagram;
  int peer_valid;
  int is_host;
  PspAdhocCommandType deferred_role;
  int deferred_role_valid;
  AdhocRecovery recovery;
  LinkTransportScheduler transport;
  uint64_t hot_until_us;
  uint64_t worker_accounted_us;
  uint64_t hot_time_us;
  uint64_t cold_time_us;
} PspAdhocContext;

static PspAdhocContext psp_adhoc =
{
  .state = PSP_ADHOC_STOPPED,
  .thread_id = -1,
  .tx_wake_id = -1,
  .socket_id = -1
};

static volatile PspAdhocTransitionPhase psp_adhoc_transition_phase =
 PSP_ADHOC_TRANSITION_NONE;

static uint64_t psp_adhoc_now(void);
static void psp_adhoc_increment(volatile unsigned int *value);
static void psp_adhoc_tx_wake_signal(PspAdhocContext *context);

static unsigned int psp_adhoc_counter_add(unsigned int left,
 unsigned int right)
{
  return UINT_MAX - left < right ? UINT_MAX : left + right;
}

static unsigned int psp_adhoc_us_to_ms(uint64_t value)
{
  return value / 1000u > UINT_MAX ? UINT_MAX :
   (unsigned int)(value / 1000u);
}

static void psp_adhoc_memory_barrier(void)
{
  __builtin_allegrex_sync();
}

static uint64_t psp_adhoc_saturating_time_add(uint64_t left,
 uint64_t right)
{
  return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static int psp_adhoc_tx_wake_create(PspAdhocContext *context)
{
  SceUID wake_id;

  if(context == NULL)
    return 0;
  wake_id = sceKernelCreateEventFlag("GBA link tx wake",
   PSP_EVENT_WAITSINGLE, 0, NULL);
  if(wake_id < 0)
  {
    context->last_error = wake_id;
    return 0;
  }
  context->tx_wake_id = wake_id;
  return 1;
}

static void psp_adhoc_tx_wake_signal(PspAdhocContext *context)
{
  if(context != NULL && context->tx_wake_id >= 0)
    sceKernelSetEventFlag(context->tx_wake_id, PSP_ADHOC_TX_WAKE_BIT);
}

static void psp_adhoc_tx_wake_reset(PspAdhocContext *context)
{
  uint32_t matched = 0;

  if(context == NULL)
    return;
  if(context->tx_wake_id >= 0)
  {
    sceKernelPollEventFlag(context->tx_wake_id, PSP_ADHOC_TX_WAKE_BIT,
     PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &matched);
  }
  __atomic_store_n(&context->hot_wake_consumed,
   __atomic_load_n(&context->hot_wake_generation, __ATOMIC_ACQUIRE),
   __ATOMIC_RELEASE);
  context->hot_until_us = 0;
}

static void psp_adhoc_tx_wake_cancel(PspAdhocContext *context)
{
  /* Event flags have no cancellation API; setting the bit releases the wait. */
  psp_adhoc_tx_wake_signal(context);
}

static void psp_adhoc_tx_wake_delete(PspAdhocContext *context)
{
  SceUID wake_id;

  if(context == NULL || context->tx_wake_id < 0)
    return;
  wake_id = context->tx_wake_id;
  context->tx_wake_id = -1;
  sceKernelDeleteEventFlag(wake_id);
}

static void psp_adhoc_tx_wake_wait(PspAdhocContext *context,
 unsigned int timeout_us)
{
  uint32_t matched = 0;
  SceUInt timeout = timeout_us;

  if(context->tx_wake_id < 0)
  {
    sceKernelDelayThread(timeout_us);
    return;
  }
  sceKernelWaitEventFlag(context->tx_wake_id, PSP_ADHOC_TX_WAKE_BIT,
   PSP_EVENT_WAITOR | PSP_EVENT_WAITCLEAR, &matched, &timeout);
}

static void psp_adhoc_publish_hot_wake(PspAdhocContext *context,
 uint64_t enqueued_us)
{
  uint32_t generation = __atomic_load_n(&context->hot_wake_generation,
   __ATOMIC_ACQUIRE);
  uint32_t consumed = __atomic_load_n(&context->hot_wake_consumed,
   __ATOMIC_ACQUIRE);

  if(generation == consumed)
  {
    generation++;
    if(generation == 0)
      generation = 1u;
    __atomic_store_n(&context->hot_wake_published_us,
     (uint32_t)enqueued_us, __ATOMIC_RELAXED);
    __atomic_store_n(&context->hot_wake_generation, generation,
     __ATOMIC_RELEASE);
  }
  psp_adhoc_tx_wake_signal(context);
}

static void psp_adhoc_hot_touch(PspAdhocContext *context, uint64_t now_us)
{
  context->hot_until_us = psp_adhoc_saturating_time_add(now_us,
   PSP_ADHOC_HOT_WINDOW_US);
  if(context->transport.lanes.profile != LINK_TRANSPORT_PROFILE_HOT)
  {
    link_transport_scheduler_set_profile(&context->transport,
     LINK_TRANSPORT_PROFILE_HOT, PSP_ADHOC_HOT_ACK_GRACE_US);
  }
}

static void psp_adhoc_consume_hot_wake(PspAdhocContext *context,
 uint64_t now_us)
{
  uint32_t generation = __atomic_load_n(&context->hot_wake_generation,
   __ATOMIC_ACQUIRE);
  uint32_t consumed = __atomic_load_n(&context->hot_wake_consumed,
   __ATOMIC_ACQUIRE);
  uint32_t published;
  unsigned int latency;

  if(generation == consumed)
    return;
  published = __atomic_load_n(&context->hot_wake_published_us,
   __ATOMIC_RELAXED);
  latency = (uint32_t)now_us - published;
  if(latency > context->hot_wake_latency_max_us)
    context->hot_wake_latency_max_us = latency;
  psp_adhoc_increment(&context->hot_wake_count);
  __atomic_store_n(&context->hot_wake_consumed, generation,
   __ATOMIC_RELEASE);
  psp_adhoc_hot_touch(context, now_us);
}

static int psp_adhoc_hot_active(PspAdhocContext *context, uint64_t now_us)
{
  const LinkTransportLanes *lanes = &context->transport.lanes;

  if(lanes->profile != LINK_TRANSPORT_PROFILE_HOT)
    return 0;
  if(now_us < context->hot_until_us ||
     link_transport_lanes_critical_outstanding(lanes) ||
     context->transport.critical_pending_count != 0 ||
     link_spsc_ring_depth(&context->transport.handoff) != 0 ||
     lanes->ack_pending)
    return 1;
  link_transport_scheduler_set_profile(&context->transport,
   LINK_TRANSPORT_PROFILE_COLD, 0);
  return 0;
}

static void psp_adhoc_account_worker_time(PspAdhocContext *context,
 uint64_t now_us, int hot)
{
  uint64_t elapsed;

  if(context->worker_accounted_us == 0 ||
     now_us < context->worker_accounted_us)
  {
    context->worker_accounted_us = now_us;
    return;
  }
  elapsed = now_us - context->worker_accounted_us;
  if(hot)
    context->hot_time_us = psp_adhoc_saturating_time_add(
     context->hot_time_us, elapsed);
  else
    context->cold_time_us = psp_adhoc_saturating_time_add(
     context->cold_time_us, elapsed);
  context->worker_accounted_us = now_us;
}

static LinkTransportLane psp_adhoc_tx_lane(uint8_t message_type)
{
  if(message_type == LINK_MESSAGE_SIO_STATE ||
     message_type == LINK_MESSAGE_HEARTBEAT ||
     message_type == LINK_MESSAGE_CLOCK_SYNC)
    return LINK_LANE_REPLACEABLE;
  return LINK_LANE_CRITICAL;
}

static LinkReplaceableClass psp_adhoc_tx_state_class(uint8_t message_type)
{
  if(message_type == LINK_MESSAGE_SIO_STATE)
    return LINK_STATE_SIO;
  if(message_type == LINK_MESSAGE_HEARTBEAT)
    return LINK_STATE_HEARTBEAT;
  if(message_type == LINK_MESSAGE_CLOCK_SYNC)
    return LINK_STATE_CLOCK;
  return LINK_STATE_NONE;
}

static int psp_adhoc_tx_handoff_push(PspAdhocContext *context,
 const LinkPacket *packet, const uint8_t *data, size_t size)
{
  LinkTransportLane lane = psp_adhoc_tx_lane(packet->type);
  LinkReplaceableClass state_class =
   psp_adhoc_tx_state_class(packet->type);
  uint64_t now = psp_adhoc_now();
  unsigned int depth;
  int pushed;

  pushed = link_transport_scheduler_queue(&context->transport, lane,
   state_class, now, data, size);
  depth = link_transport_scheduler_depth(&context->transport);
  if(depth > context->tx_queue_high_water)
    context->tx_queue_high_water = depth;
  if(pushed && lane == LINK_LANE_CRITICAL)
    psp_adhoc_publish_hot_wake(context, now);
  return pushed;
}

static void psp_adhoc_fold_transport_diagnostics(PspAdhocContext *context)
{
  const LinkTransportLanes *lanes = &context->transport.lanes;
  uint64_t max_age_us = lanes->backpressure_max_age_us;
  uint64_t active_age_us = link_transport_lanes_backpressure_age(lanes,
   psp_adhoc_now());
  unsigned int max_age_ms;

  if(active_age_us > max_age_us)
    max_age_us = active_age_us;
  max_age_ms = psp_adhoc_us_to_ms(max_age_us);
  context->backpressure_episode_count = psp_adhoc_counter_add(
   context->backpressure_episode_count, lanes->backpressure_episodes);
  if(max_age_ms > context->backpressure_max_age_ms)
    context->backpressure_max_age_ms = max_age_ms;
  if(lanes->backpressure_streak_high_water >
     context->backpressure_streak_high_water)
  {
    context->backpressure_streak_high_water =
     lanes->backpressure_streak_high_water;
  }
  if(lanes->last_blocked_kind != LINK_LANES_TX_NONE)
  {
    context->last_blocked_kind = (unsigned int)lanes->last_blocked_kind;
    context->last_blocked_state_class =
     (unsigned int)lanes->last_blocked_state_class;
  }
  context->clock_backoff_raises = psp_adhoc_counter_add(
   context->clock_backoff_raises, lanes->clock_backoff_raises);
  context->clock_deferred = psp_adhoc_counter_add(
   context->clock_deferred, lanes->clock_deferred);
}

static void psp_adhoc_transport_reset(PspAdhocContext *context,
 uint32_t connection_id, uint32_t epoch)
{
  psp_adhoc_fold_transport_diagnostics(context);
  link_transport_scheduler_reset(&context->transport, connection_id, epoch);
  psp_adhoc_tx_wake_reset(context);
}

static uint64_t psp_adhoc_now(void)
{
  return (uint64_t)sceKernelGetSystemTimeWide();
}

static void psp_adhoc_set_phase(PspAdhocContext *context,
 PspAdhocPhase phase)
{
  context->phase = phase;
  psp_adhoc_memory_barrier();
}

static int psp_adhoc_cancelled(const PspAdhocContext *context)
{
  return lifecycle_teardown_cancelled(&context->teardown);
}

static void psp_adhoc_snapshot_tx_base(PspAdhocContext *context,
 PspAdhocFailureSnapshot *snapshot, uint64_t now_us)
{
  const LinkLanesCritical *critical = &context->transport.lanes.critical;
  uint64_t age_us;

  if(!critical->valid)
    return;
  snapshot->tx_base_sequence = critical->logical_id.message;
  snapshot->tx_base_attempts = critical->attempts;
  if(critical->size > 6u)
    snapshot->tx_base_message = critical->data[6];
  if(critical->attempts != 0 && now_us >= critical->first_sent_us)
  {
    age_us = now_us - critical->first_sent_us;
    snapshot->tx_base_age_ms = age_us / 1000u > UINT_MAX ? UINT_MAX :
     (unsigned int)(age_us / 1000u);
  }
}

static void psp_adhoc_snapshot_datagram(PspAdhocContext *context,
 PspAdhocFailureSnapshot *snapshot, uint64_t now_us)
{
  uint64_t age_us;

  snapshot->next_tx_sequence = context->transport.lanes.next_wire_packet;
  snapshot->next_rx_sequence = context->transport.lanes.last_rx_critical;
  snapshot->buffered_packets =
   link_transport_lanes_depth(&context->transport.lanes);
  snapshot->received_data_packets = context->rx_datagrams;
  snapshot->received_ack_packets =
   context->transport.lanes.received_acks;
  snapshot->ack_advances = context->transport.lanes.ack_advances;
  if(context->transport.lanes.last_ack_progress_us != 0 &&
     now_us >= context->transport.lanes.last_ack_progress_us)
  {
    age_us = now_us - context->transport.lanes.last_ack_progress_us;
    snapshot->ack_progress_age_ms = age_us / 1000u > UINT_MAX ? UINT_MAX :
     (unsigned int)(age_us / 1000u);
  }
}

static void psp_adhoc_snapshot_backpressure(PspAdhocContext *context,
 PspAdhocFailureSnapshot *snapshot, uint64_t now_us)
{
  const LinkTransportLanes *lanes = &context->transport.lanes;
  uint64_t age_us = link_transport_lanes_backpressure_age(lanes, now_us);
  uint64_t max_age_us = lanes->backpressure_max_age_us;
  uint64_t next_delay_us = 0;
  unsigned int lane_max_age_ms;

  if(age_us > max_age_us)
    max_age_us = age_us;
  lane_max_age_ms = psp_adhoc_us_to_ms(max_age_us);
  snapshot->backpressure_age_ms = psp_adhoc_us_to_ms(age_us);
  snapshot->backpressure_streak = lanes->backpressure_streak;
  snapshot->backpressure_episode_count = psp_adhoc_counter_add(
   context->backpressure_episode_count, lanes->backpressure_episodes);
  snapshot->backpressure_max_age_ms = context->backpressure_max_age_ms;
  if(lane_max_age_ms > snapshot->backpressure_max_age_ms)
    snapshot->backpressure_max_age_ms = lane_max_age_ms;
  snapshot->backpressure_streak_high_water =
   context->backpressure_streak_high_water;
  if(lanes->backpressure_streak_high_water >
     snapshot->backpressure_streak_high_water)
  {
    snapshot->backpressure_streak_high_water =
     lanes->backpressure_streak_high_water;
  }
  snapshot->last_blocked_kind = context->last_blocked_kind;
  snapshot->last_blocked_state_class = context->last_blocked_state_class;
  if(lanes->last_blocked_kind != LINK_LANES_TX_NONE)
  {
    snapshot->last_blocked_kind = (unsigned int)lanes->last_blocked_kind;
    snapshot->last_blocked_state_class =
     (unsigned int)lanes->last_blocked_state_class;
  }
  snapshot->clock_backoff_us = lanes->clock_backoff_us;
  if(lanes->clock_not_before_us != 0 &&
     now_us < lanes->clock_not_before_us)
  {
    next_delay_us = lanes->clock_not_before_us - now_us;
  }
  snapshot->clock_next_delay_us = next_delay_us > UINT_MAX ? UINT_MAX :
   (unsigned int)next_delay_us;
  snapshot->clock_success_streak = lanes->clock_success_streak;
  snapshot->clock_backoff_raises = psp_adhoc_counter_add(
   context->clock_backoff_raises, lanes->clock_backoff_raises);
  snapshot->clock_deferred = psp_adhoc_counter_add(
   context->clock_deferred, lanes->clock_deferred);
  snapshot->health_causes = context->health_causes |
   context->health_cause_history;
  snapshot->session_recovery_requests =
   context->session_recovery_requests;
  snapshot->session_recovery_reason =
   context->session_recovery_last_reason;
}

static void psp_adhoc_snapshot_scheduler(PspAdhocContext *context,
 PspAdhocFailureSnapshot *snapshot)
{
  unsigned int mailbox_coalesced =
   link_transport_scheduler_replaceable_coalesced(&context->transport);

  snapshot->tx_queue_depth = link_transport_scheduler_depth(
   &context->transport);
  snapshot->tx_queue_high_water = context->tx_queue_high_water;
  snapshot->tx_queue_coalesced = psp_adhoc_counter_add(
   context->transport.lanes.coalesced, mailbox_coalesced);
  snapshot->tx_queue_rejected = psp_adhoc_counter_add(
   context->transport.handoff.rejected,
   context->transport.lanes.rejected);
  snapshot->tx_critical_handoff_depth = link_spsc_ring_depth(
   &context->transport.handoff);
  snapshot->tx_critical_handoff_high_water =
   context->transport.handoff.high_water;
  snapshot->tx_critical_pending_depth =
   context->transport.critical_pending_count;
  snapshot->tx_critical_pending_high_water =
   context->transport.critical_pending_high_water;
  snapshot->tx_critical_retry_suppressed =
   link_transport_scheduler_critical_retry_suppressed(
    &context->transport);

#define PSP_ADHOC_SNAPSHOT_MAILBOX(name, state_class) \
  snapshot->tx_##name##_mailbox_depth = \
   link_transport_scheduler_replaceable_class_depth(&context->transport, \
    state_class); \
  snapshot->tx_##name##_published = \
   link_transport_scheduler_replaceable_published(&context->transport, \
    state_class); \
  snapshot->tx_##name##_coalesced = \
   link_transport_scheduler_replaceable_class_coalesced( \
    &context->transport, state_class); \
  snapshot->tx_##name##_high_water = \
   link_transport_scheduler_replaceable_high_water(&context->transport, \
    state_class)

  PSP_ADHOC_SNAPSHOT_MAILBOX(sio, LINK_STATE_SIO);
  PSP_ADHOC_SNAPSHOT_MAILBOX(heartbeat, LINK_STATE_HEARTBEAT);
  PSP_ADHOC_SNAPSHOT_MAILBOX(clock, LINK_STATE_CLOCK);
#undef PSP_ADHOC_SNAPSHOT_MAILBOX
}

static void psp_adhoc_snapshot_worker(PspAdhocContext *context,
 PspAdhocFailureSnapshot *snapshot)
{
  snapshot->hot_wake_count = context->hot_wake_count;
  snapshot->hot_wake_latency_max_us = context->hot_wake_latency_max_us;
  snapshot->hot_drain_count = context->hot_drain_count;
  snapshot->hot_drain_packets = context->hot_drain_packets;
  snapshot->hot_drain_high_water = context->hot_drain_high_water;
  snapshot->hot_yields = context->hot_yields;
  snapshot->hot_time_ms = psp_adhoc_us_to_ms(context->hot_time_us);
  snapshot->cold_time_ms = psp_adhoc_us_to_ms(context->cold_time_us);
}

static int psp_adhoc_internal_error(int error)
{
  return error == PSP_ADHOC_ERROR_WLAN_OFF ||
   error == PSP_ADHOC_ERROR_THREAD ||
   error == PSP_ADHOC_ERROR_COMMAND_QUEUE ||
   error == PSP_ADHOC_ERROR_EVENT_QUEUE ||
   error == PSP_ADHOC_ERROR_PROTOCOL ||
   error == PSP_ADHOC_ERROR_SHUTDOWN ||
   error == PSP_ADHOC_ERROR_SESSION_RECOVERY ||
   error == PSP_ADHOC_ERROR_TIMEOUT ||
   error == PSP_ADHOC_ERROR_PEER_RECOVERY ||
   error == PSP_ADHOC_ERROR_TRANSPORT_RECOVERY;
}

static PspAdhocErrorDomain psp_adhoc_internal_domain(int error)
{
  if(error == PSP_ADHOC_ERROR_SESSION_RECOVERY)
    return PSP_ADHOC_ERROR_DOMAIN_SESSION;
  if(error == PSP_ADHOC_ERROR_SHUTDOWN)
    return PSP_ADHOC_ERROR_DOMAIN_LIFECYCLE;
  if(error == PSP_ADHOC_ERROR_PROTOCOL ||
     error == PSP_ADHOC_ERROR_TIMEOUT ||
     error == PSP_ADHOC_ERROR_PEER_RECOVERY ||
     error == PSP_ADHOC_ERROR_TRANSPORT_RECOVERY)
    return PSP_ADHOC_ERROR_DOMAIN_TRANSPORT;
  return PSP_ADHOC_ERROR_DOMAIN_INTERNAL;
}

static void psp_adhoc_note_api_failure(PspAdhocContext *context, int error,
 PspAdhocPhase phase)
{
  uint32_t expected = 0;

  if(context == NULL || error >= 0 || psp_adhoc_internal_error(error) ||
     !__atomic_compare_exchange_n(&context->api_error_valid, &expected,
      PSP_ADHOC_ERROR_DOMAIN_RECORDING, 0, __ATOMIC_ACQ_REL,
      __ATOMIC_ACQUIRE))
    return;
  context->api_error = error;
  context->api_error_phase = phase;
  __atomic_store_n(&context->api_error_valid, 1u, __ATOMIC_RELEASE);
}

static void psp_adhoc_record_internal_error(PspAdhocContext *context,
 PspAdhocErrorDomain domain, int code, LinkTimeoutSource timeout_source)
{
  uint32_t expected = PSP_ADHOC_ERROR_DOMAIN_NONE;
  PspAdhocPhase phase;

  if(context == NULL || domain == PSP_ADHOC_ERROR_DOMAIN_NONE)
    return;
  phase = domain == PSP_ADHOC_ERROR_DOMAIN_SESSION ?
   PSP_ADHOC_PHASE_NONE : context->phase;
  if(__atomic_compare_exchange_n(&context->error_domain, &expected,
     PSP_ADHOC_ERROR_DOMAIN_RECORDING, 0, __ATOMIC_ACQ_REL,
     __ATOMIC_ACQUIRE))
  {
    context->internal_error_code = code;
    context->internal_error_phase = phase;
    context->timeout_source = timeout_source;
    __atomic_store_n(&context->error_domain, (uint32_t)domain,
     __ATOMIC_RELEASE);
  }
  else if(timeout_source != LINK_TIMEOUT_NONE &&
          __atomic_load_n(&context->timeout_source, __ATOMIC_ACQUIRE) ==
           LINK_TIMEOUT_NONE)
  {
    __atomic_store_n(&context->timeout_source, timeout_source,
     __ATOMIC_RELEASE);
  }
}

static void psp_adhoc_record_error(PspAdhocContext *context, int error,
 LinkTimeoutSource timeout_source)
{
  if(psp_adhoc_internal_error(error))
  {
    psp_adhoc_record_internal_error(context,
     psp_adhoc_internal_domain(error), error, timeout_source);
    return;
  }
  psp_adhoc_note_api_failure(context, error, context->phase);
  psp_adhoc_record_internal_error(context, PSP_ADHOC_ERROR_DOMAIN_PSP_API,
   error, timeout_source);
}

static void psp_adhoc_set_timeout(PspAdhocContext *context,
 LinkTimeoutSource timeout_source)
{
  context->last_error = PSP_ADHOC_ERROR_TIMEOUT;
  psp_adhoc_record_internal_error(context,
   PSP_ADHOC_ERROR_DOMAIN_TRANSPORT, PSP_ADHOC_ERROR_TIMEOUT,
   timeout_source);
}

static void psp_adhoc_snapshot_attribution(PspAdhocContext *context,
 PspAdhocFailureSnapshot *snapshot)
{
  uint32_t api_valid = __atomic_load_n(&context->api_error_valid,
   __ATOMIC_ACQUIRE);
  uint32_t domain = __atomic_load_n(&context->error_domain,
   __ATOMIC_ACQUIRE);

  snapshot->api_error = api_valid == 1u ? context->api_error : 0;
  snapshot->api_phase = api_valid == 1u ?
   (unsigned int)context->api_error_phase : PSP_ADHOC_PHASE_NONE;
  snapshot->error_domain = domain == PSP_ADHOC_ERROR_DOMAIN_RECORDING ?
   PSP_ADHOC_ERROR_DOMAIN_NONE : domain;
  snapshot->internal_error_code = context->internal_error_code;
  snapshot->internal_error_phase =
   (unsigned int)context->internal_error_phase;
  snapshot->timeout_source = (unsigned int)__atomic_load_n(
   &context->timeout_source, __ATOMIC_ACQUIRE);
}

static void psp_adhoc_capture_snapshot(PspAdhocContext *context)
{
  pdpStatStruct stats[PSP_ADHOC_PEER_CAPACITY];
  PspAdhocFailureSnapshot *snapshot = &context->failure_snapshot;
  int size = (int)sizeof(stats);
  int count;
  int index;

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->control_state = -1;
  snapshot->socket_present = -1;
  snapshot->cpu_clock = (unsigned int)scePowerGetCpuClockFrequencyInt();
  snapshot->bus_clock = (unsigned int)scePowerGetBusClockFrequencyInt();
  snapshot->backpressure_events = context->backpressure_events;
  snapshot->tx_datagrams = context->tx_datagrams;
  snapshot->rx_datagrams = context->rx_datagrams;
  snapshot->retransmissions = context->transport.lanes.retransmissions;
  snapshot->duplicates = context->transport.lanes.duplicates;
  snapshot->out_of_window = context->transport.lanes.stale;
  snapshot->invalid_packets = context->transport.lanes.invalid_packets;
  snapshot->ack_packets = context->transport.lanes.received_acks;
  snapshot->probes = context->transport.lanes.probes;
  snapshot->rto_us = (unsigned int)context->transport.lanes.rto_us;
  snapshot->rtt_estimate_us =
   context->transport.lanes.smoothed_rtt_us > UINT_MAX ? UINT_MAX :
   (unsigned int)context->transport.lanes.smoothed_rtt_us;
  snapshot->queue_sojourn_high_water_us =
   context->transport.lanes.sojourn_high_water_us > UINT_MAX ? UINT_MAX :
   (unsigned int)context->transport.lanes.sojourn_high_water_us;
  snapshot->api_retries = context->api_retries;
  snapshot->sent_bytes = context->sent_bytes;
  psp_adhoc_snapshot_backpressure(context, snapshot, psp_adhoc_now());
  psp_adhoc_snapshot_tx_base(context, snapshot, psp_adhoc_now());
  psp_adhoc_snapshot_datagram(context, snapshot, psp_adhoc_now());
  snapshot->outstanding_packets =
   link_transport_lanes_critical_outstanding(&context->transport.lanes);
  psp_adhoc_snapshot_scheduler(context, snapshot);
  psp_adhoc_snapshot_worker(context, snapshot);
  psp_adhoc_snapshot_attribution(context, snapshot);
  snapshot->transport_resyncs =
   context->recovery.total_transport_resyncs;
  snapshot->group_recoveries = context->recovery.total_group_rebuilds;
  snapshot->stack_recoveries = context->recovery.total_stack_restarts;
  snapshot->recovery_state = (unsigned int)context->recovery.state;
  snapshot->resync_state = context->resync_state;
  snapshot->transport_epoch = context->transport.lanes.epoch;
  snapshot->last_received_sequence =
   context->transport.lanes.last_rx_critical;
  snapshot->peer_activity = context->activity_generation;
  snapshot->consumed_activity = context->activity_consumed;
  snapshot->transition_phase = psp_adhoc_transition_phase;
  snapshot->shutdown_phase = lifecycle_teardown_phase(&context->teardown);
  snapshot->cancellation_token = context->teardown.cancellation_token;

  if(snapshot->shutdown_phase != LIFECYCLE_TEARDOWN_IDLE)
    return;

  if(context->control_initialized)
    snapshot->control_result = sceNetAdhocctlGetState(
     &snapshot->control_state);

  if(!context->adhoc_initialized || context->socket_id < 0)
    return;

  memset(stats, 0, sizeof(stats));
  snapshot->socket_result = sceNetAdhocGetPdpStat(&size, stats);
  if(snapshot->socket_result < 0)
    return;

  snapshot->socket_present = 0;
  count = size / (int)sizeof(stats[0]);
  if(count > (int)PSP_ADHOC_PEER_CAPACITY)
    count = (int)PSP_ADHOC_PEER_CAPACITY;
  for(index = 0; index < count; index++)
  {
    if(stats[index].pdpId != context->socket_id)
      continue;
    snapshot->socket_present = 1;
    snapshot->received_bytes = stats[index].rcvdData;
    break;
  }
}

static void psp_adhoc_capture_failure(PspAdhocContext *context, int error)
{
  uint32_t api_valid;

  context->last_error = error;
  if(context->failure_valid)
  {
    psp_adhoc_memory_barrier();
    return;
  }
  psp_adhoc_record_error(context, error, LINK_TIMEOUT_NONE);
  psp_adhoc_capture_snapshot(context);
  context->failure_snapshot.recovery_terminal_error = error;
  context->failure_snapshot.recovery_terminal_phase =
   (unsigned int)context->phase;
  api_valid = __atomic_load_n(&context->api_error_valid, __ATOMIC_ACQUIRE);
  context->failure_error = api_valid == 1u ? context->api_error :
   context->internal_error_code;
  context->failure_phase = api_valid == 1u ? context->api_error_phase :
   context->internal_error_phase;
  context->failure_free_memory = sceKernelTotalFreeMemSize();
  psp_adhoc_memory_barrier();
  context->failure_valid = 1;
}

static uint32_t psp_adhoc_event_generation(const PspAdhocContext *context)
{
  return context->datagram.epoch == 0u ? 1u : context->datagram.epoch;
}

static int psp_adhoc_emit(PspAdhocContext *context,
 LinkTransportEventType type, int error, const uint8_t *data, size_t size);

static uint32_t psp_adhoc_new_recovery_id(PspAdhocContext *context,
 uint64_t now)
{
  uint32_t recovery_id = (uint32_t)now ^ (uint32_t)(now >> 32) ^
   context->connection_id ^ (context->datagram.epoch * 0x9E3779B9u);

  if(recovery_id == 0 ||
     recovery_id == context->last_completed_recovery_id)
    recovery_id ^= 0xA341316Cu;
  return recovery_id == 0 ? 1u : recovery_id;
}

static void psp_adhoc_request_recovery(PspAdhocContext *context,
 int error, uint32_t recovery_id, LinkTimeoutSource timeout_source)
{
  uint64_t now = psp_adhoc_now();

  if(psp_adhoc_internal_error(error))
  {
    psp_adhoc_record_internal_error(context,
     psp_adhoc_internal_domain(error), error, timeout_source);
  }
  else
  {
    psp_adhoc_note_api_failure(context, error, context->phase);
    psp_adhoc_record_internal_error(context,
     PSP_ADHOC_ERROR_DOMAIN_TRANSPORT,
     PSP_ADHOC_ERROR_TRANSPORT_RECOVERY, timeout_source);
  }

  if(recovery_id == context->last_completed_recovery_id &&
     recovery_id != 0)
    return;

  if(!context->is_host && recovery_id == 0 &&
     context->recovery_id == 0)
  {
    if(context->recovery_request_id == 0)
    {
      context->recovery_request_id = psp_adhoc_new_recovery_id(context,
       now);
      context->recovery_request_started_us = now;
      context->recovery_request_next_us = now;
      context->state = PSP_ADHOC_RECOVERING;
      if(!context->recovery_notified &&
         psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_RECOVERING,
          0, NULL, 0))
        context->recovery_notified = 1;
    }
    if(!context->recovery_trigger_valid)
    {
      context->recovery_trigger_error = error;
      context->recovery_trigger_phase = context->phase;
      psp_adhoc_memory_barrier();
      context->recovery_trigger_valid = 1;
      runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 4,
       (uint32_t)error, (uint32_t)context->phase,
       context->recovery_request_id);
    }
    context->last_error = error;
    return;
  }

  context->recovery_request_id = 0;
  context->recovery_request_started_us = 0;
  context->recovery_request_next_us = 0;
  if(context->recovery_id == 0)
  {
    context->recovery_id = recovery_id != 0 ? recovery_id :
     psp_adhoc_new_recovery_id(context, now);
  }
  if(!context->recovery_trigger_valid)
  {
    context->recovery_trigger_error = error;
    context->recovery_trigger_phase = context->phase;
    psp_adhoc_memory_barrier();
    context->recovery_trigger_valid = 1;
    runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 0,
     (uint32_t)error, (uint32_t)context->phase,
     (uint32_t)context->recovery.state);
  }
  context->last_error = error;
  adhoc_recovery_request(&context->recovery);
}

static void psp_adhoc_publish_health(PspAdhocContext *context,
 int degraded)
{
  uint64_t now;
  uint64_t age_us = 0;
  uint64_t backpressure_age_us;
  uint32_t age_ms = 0;
  uint32_t value = degraded != 0;
  uint32_t generation;

  if(__atomic_load_n(&context->transport_degraded, __ATOMIC_ACQUIRE) == value)
    return;
  __atomic_store_n(&context->transport_degraded, value, __ATOMIC_RELEASE);
  __atomic_store_n(&context->health_epoch,
   psp_adhoc_event_generation(context), __ATOMIC_RELEASE);
  generation = __atomic_add_fetch(&context->health_generation, 1u,
   __ATOMIC_ACQ_REL);
  if(generation == 0)
  {
    __atomic_store_n(&context->health_generation, 1u, __ATOMIC_RELEASE);
    generation = 1u;
  }
  now = psp_adhoc_now();
  backpressure_age_us = link_transport_lanes_backpressure_age(
   &context->transport.lanes, now);
  if(backpressure_age_us > age_us)
    age_us = backpressure_age_us;
  if(context->ack_stall_started_us != 0 &&
     now >= context->ack_stall_started_us)
  {
    uint64_t ack_age_us = now - context->ack_stall_started_us;

    if(ack_age_us > age_us)
      age_us = ack_age_us;
  }
  age_ms = psp_adhoc_us_to_ms(age_us);
  runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_HEALTH,
   degraded ? (uint8_t)(1u | (context->health_causes << 1)) : 0u,
   generation, age_ms,
   context->transport.lanes.epoch);
}

static void psp_adhoc_consume_session_recovery(PspAdhocContext *context)
{
  uint32_t generation = __atomic_load_n(
   &context->session_recovery_generation, __ATOMIC_ACQUIRE);
  int reason;
  LinkTimeoutSource timeout_source;

  if(generation == __atomic_load_n(&context->session_recovery_consumed,
     __ATOMIC_ACQUIRE))
    return;
  reason = __atomic_load_n(&context->session_recovery_reason,
   __ATOMIC_ACQUIRE);
  timeout_source = __atomic_load_n(
   &context->session_recovery_timeout_source, __ATOMIC_ACQUIRE);
  __atomic_store_n(&context->session_recovery_consumed, generation,
   __ATOMIC_RELEASE);
  psp_adhoc_increment(&context->session_recovery_requests);
  context->session_recovery_last_reason = (unsigned int)reason;
  runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 5,
   (uint32_t)reason, generation, context->datagram.epoch);
  psp_adhoc_request_recovery(context, PSP_ADHOC_ERROR_SESSION_RECOVERY, 0,
   timeout_source);
}

static int psp_adhoc_command_push(PspAdhocContext *context,
 const PspAdhocCommand *command)
{
  uint32_t write = context->command_write;
  uint32_t next = (write + 1u) % PSP_ADHOC_COMMAND_CAPACITY;

  if(next == context->command_read)
    return 0;

  psp_adhoc_memory_barrier();
  context->commands[write] = *command;
  psp_adhoc_memory_barrier();
  context->command_write = next;
  psp_adhoc_tx_wake_signal(context);
  return 1;
}

static int psp_adhoc_command_pop(PspAdhocContext *context,
 PspAdhocCommand *command)
{
  uint32_t read = context->command_read;

  if(read == context->command_write)
    return 0;

  psp_adhoc_memory_barrier();
  *command = context->commands[read];
  psp_adhoc_memory_barrier();
  context->command_read = (read + 1u) % PSP_ADHOC_COMMAND_CAPACITY;
  return 1;
}

static int psp_adhoc_event_push(PspAdhocContext *context,
 const LinkTransportEvent *event)
{
  uint32_t write = context->event_write;
  uint32_t next = (write + 1u) % PSP_ADHOC_EVENT_CAPACITY;

  if(next == context->event_read)
  {
    psp_adhoc_capture_failure(context, PSP_ADHOC_ERROR_EVENT_QUEUE);
    context->fallback_error = PSP_ADHOC_ERROR_EVENT_QUEUE;
    context->fallback_generation = psp_adhoc_event_generation(context);
    psp_adhoc_memory_barrier();
    context->fallback_pending = 1;
    return 0;
  }

  psp_adhoc_memory_barrier();
  context->events[write] = *event;
  psp_adhoc_memory_barrier();
  context->event_write = next;
  return 1;
}

static int psp_adhoc_event_pop(PspAdhocContext *context,
 LinkTransportEvent *event)
{
  uint32_t read = context->event_read;

  if(read != context->event_write)
  {
    psp_adhoc_memory_barrier();
    *event = context->events[read];
    psp_adhoc_memory_barrier();
    context->event_read = (read + 1u) % PSP_ADHOC_EVENT_CAPACITY;
    return 1;
  }

  if(context->fallback_pending)
  {
    psp_adhoc_memory_barrier();
    memset(event, 0, sizeof(*event));
    event->type = LINK_TRANSPORT_EVENT_ERROR;
    event->generation = context->fallback_generation;
    event->error = context->fallback_error;
    psp_adhoc_memory_barrier();
    context->fallback_pending = 0;
    return 1;
  }

  return 0;
}

static int psp_adhoc_emit(PspAdhocContext *context,
 LinkTransportEventType type, int error, const uint8_t *data, size_t size)
{
  LinkTransportEvent event;

  if(data != NULL && size > sizeof(event.data))
  {
    psp_adhoc_capture_failure(context, PSP_ADHOC_ERROR_PROTOCOL);
    return 0;
  }

  memset(&event, 0, sizeof(event));
  event.type = type;
  event.generation = psp_adhoc_event_generation(context);
  event.error = error;
  if(data != NULL)
  {
    event.size = (uint16_t)size;
    memcpy(event.data, data, size);
  }
  return psp_adhoc_event_push(context, &event);
}

static void psp_adhoc_set_error(PspAdhocContext *context, int error)
{
  psp_adhoc_tx_wake_cancel(context);
  runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_ERROR,
   (uint8_t)context->state, (uint32_t)error,
   (uint32_t)context->phase, context->datagram.epoch);
  psp_adhoc_capture_failure(context, error);
  context->state = PSP_ADHOC_FAILED;
  psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_ERROR, error, NULL, 0);
}

static int psp_adhoc_is_control_temporary_error(int error)
{
  return error == SCE_ADHOCCTL_ERROR_BUSY ||
   error == SCE_ADHOCCTL_ERROR_NO_SCAN_INFO;
}

static int psp_adhoc_is_temporary_error(int error)
{
  return error == SCE_NET_ERROR_NO_SPACE ||
   error == SCE_ADHOC_ERROR_WOULD_BLOCK ||
   error == SCE_ADHOC_ERROR_TIMEOUT;
}

static int psp_adhoc_is_recoverable_socket_error(int error)
{
  return error == SCE_ADHOC_ERROR_SOCKET_DELETED ||
   error == SCE_ADHOC_ERROR_DISCONNECTED;
}


static void psp_adhoc_close_connection(PspAdhocContext *context)
{
  int socket_id = context->socket_id;
  int control_connected = context->control_connected;

  context->socket_id = -1;
  context->control_connected = 0;
  context->peer_valid = 0;
  context->recovery_notified = 0;
  context->recovery_id = 0;
  context->recovery_request_id = 0;
  context->recovery_request_started_us = 0;
  context->recovery_request_next_us = 0;
  context->local_port = 0;
  adhoc_datagram_init(&context->datagram, 0, 0, 0);
  psp_adhoc_transport_reset(context, 0, 0);
  psp_adhoc_memory_barrier();

  if(socket_id >= 0)
    sceNetAdhocPdpDelete(socket_id, 0);

  if(control_connected)
    sceNetAdhocctlDisconnect();

}

static void psp_adhoc_prepare_connection(PspAdhocContext *context)
{
  psp_adhoc_transport_reset(context, 0, 0);
  context->api_error = 0;
  context->api_error_phase = PSP_ADHOC_PHASE_NONE;
  __atomic_store_n(&context->api_error_valid, 0u, __ATOMIC_RELEASE);
  context->internal_error_code = PSP_ADHOC_ERROR_NONE;
  context->internal_error_phase = PSP_ADHOC_PHASE_NONE;
  __atomic_store_n(&context->timeout_source, LINK_TIMEOUT_NONE,
   __ATOMIC_RELEASE);
  __atomic_store_n(&context->error_domain, PSP_ADHOC_ERROR_DOMAIN_NONE,
   __ATOMIC_RELEASE);
  context->close_requested = 0;
  context->backpressure_events = 0;
  context->backpressure_episode_count = 0;
  context->backpressure_max_age_ms = 0;
  context->backpressure_streak_high_water = 0;
  context->last_blocked_kind = LINK_LANES_TX_NONE;
  context->last_blocked_state_class = LINK_STATE_NONE;
  context->clock_backoff_raises = 0;
  context->clock_deferred = 0;
  context->health_causes = 0;
  context->health_cause_history = 0;
  context->session_recovery_requests = 0;
  context->session_recovery_last_reason = 0;
  context->tx_datagrams = 0;
  context->rx_datagrams = 0;
  context->api_retries = 0;
  context->sent_bytes = 0;
  context->tx_queue_high_water = 0;
  context->hot_wake_count = 0;
  context->hot_wake_latency_max_us = 0;
  context->hot_drain_count = 0;
  context->hot_drain_packets = 0;
  context->hot_drain_high_water = 0;
  context->hot_yields = 0;
  context->worker_accounted_us = 0;
  context->hot_time_us = 0;
  context->cold_time_us = 0;
  context->resync_state = 0;
  context->resync_requests_accepted = 0;
  context->resync_requests_rejected = 0;
  context->recovery_id = 0;
  context->last_completed_recovery_id = 0;
  context->recovery_request_id = 0;
  context->recovery_request_started_us = 0;
  context->recovery_request_next_us = 0;
  context->ack_stall_started_us = 0;
  adhoc_resync_request_gate_init(&context->resync_request_gate);
  context->recovery_trigger_valid = 0;
  context->recovery_trigger_error = PSP_ADHOC_ERROR_NONE;
  context->recovery_trigger_phase = PSP_ADHOC_PHASE_NONE;
  context->peer_valid = 0;
  context->connection_id = 0;
  context->activity_consumed = context->activity_generation;
  context->activity_epoch = 1u;
  context->health_consumed = context->health_generation;
  context->health_epoch = 1u;
  context->transport_degraded = 0;
  context->session_recovery_consumed = context->session_recovery_generation;
  context->session_recovery_reason = 0;
  context->session_recovery_timeout_source = LINK_TIMEOUT_NONE;
  context->recovery_notified = 0;
  context->fallback_generation = 1u;
  context->local_port = 0;
  adhoc_datagram_init(&context->datagram, 0, 0, 0);
  memset(context->local_mac, 0, sizeof(context->local_mac));
  memset(context->peer_mac, 0, sizeof(context->peer_mac));
  context->peer_port = 0;
  adhoc_recovery_init(&context->recovery, 0,
   &psp_adhoc_recovery_config);
  psp_adhoc_memory_barrier();
}

static void psp_adhoc_network_deinit(PspAdhocContext *context)
{
  if(context->control_initialized)
  {
    sceNetAdhocctlTerm();
    context->control_initialized = 0;
  }
  if(context->adhoc_initialized)
  {
    sceNetAdhocTerm();
    context->adhoc_initialized = 0;
  }
  if(context->net_initialized)
  {
    sceNetTerm();
    context->net_initialized = 0;
  }
  if(psp_adhoc_cancelled(context))
  {
    lifecycle_teardown_advance(&context->teardown,
     LIFECYCLE_TEARDOWN_WORKER_QUIESCED);
    lifecycle_teardown_advance(&context->teardown,
     LIFECYCLE_TEARDOWN_RESOURCES_CLOSED);
  }
  if(context->module_adhoc)
  {
    sceUtilityUnloadNetModule(PSP_NET_MODULE_ADHOC);
    context->module_adhoc = 0;
  }
  if(context->module_common)
  {
    sceUtilityUnloadNetModule(PSP_NET_MODULE_COMMON);
    context->module_common = 0;
  }
  if(psp_adhoc_cancelled(context))
    lifecycle_teardown_advance(&context->teardown,
     LIFECYCLE_TEARDOWN_MODULES_UNLOADED);
}

static void psp_adhoc_network_term(PspAdhocContext *context)
{
  psp_adhoc_close_connection(context);
  psp_adhoc_network_deinit(context);
}

static int psp_adhoc_network_init(PspAdhocContext *context)
{
  struct productStruct product;
  int result;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_LOAD_COMMON);
  result = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
  if(result < 0)
    return result;
  context->module_common = 1;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_LOAD_ADHOC);
  result = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
  if(result < 0)
    return result;
  context->module_adhoc = 1;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_NET_INIT);
  result = sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024);
  if(result < 0)
    return result;
  context->net_initialized = 1;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_ADHOC_INIT);
  result = sceNetAdhocInit();
  if(result < 0)
    return result;
  context->adhoc_initialized = 1;

  memset(&product, 0, sizeof(product));
  memcpy(product.product, "GBARUSH01", sizeof(product.product));
  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_CONTROL_INIT);
  result = sceNetAdhocctlInit(32 * 1024, 0x20, &product);
  if(result < 0)
    return result;
  context->control_initialized = 1;
  return 0;
}

static int psp_adhoc_control_command(PspAdhocContext *context)
{
  PspAdhocCommand command;

  while(psp_adhoc_command_pop(context, &command))
  {
    switch(command.type)
    {
      case PSP_ADHOC_COMMAND_STOP:
        return 1;

      case PSP_ADHOC_COMMAND_DISCONNECT:
        return 1;

      case PSP_ADHOC_COMMAND_HOST:
      case PSP_ADHOC_COMMAND_JOIN:
        context->deferred_role = command.type;
        context->deferred_role_valid = 1;
        return 1;

    }
  }

  return psp_adhoc_cancelled(context);
}

static int psp_adhoc_wait_control_state(PspAdhocContext *context,
 unsigned int timeout)
{
  uint64_t started = sceKernelGetSystemTimeWide();
  int state;
  int result;

  while(sceKernelGetSystemTimeWide() - started < timeout)
  {
    if(psp_adhoc_control_command(context))
      return 0;

    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_WLAN);
    if(!sceWlanGetSwitchState())
    {
      context->last_error = PSP_ADHOC_ERROR_WLAN_OFF;
      return -1;
    }

    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_CONTROL_WAIT);
    state = 0;
    result = sceNetAdhocctlGetState(&state);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }
    if(state == 1)
      return 1;

    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  psp_adhoc_set_timeout(context, LINK_TIMEOUT_CONTROL);
  return -1;
}

static void psp_adhoc_increment(volatile unsigned int *value)
{
  if(*value != UINT_MAX)
    (*value)++;
}

static void psp_adhoc_note_activity(PspAdhocContext *context)
{
  context->activity_epoch = psp_adhoc_event_generation(context);
  psp_adhoc_memory_barrier();
  context->activity_generation++;
  psp_adhoc_memory_barrier();
}

static uint32_t psp_adhoc_make_connection_id(PspAdhocContext *context)
{
  uint64_t now = sceKernelGetSystemTimeWide();
  uint32_t connection_id = (uint32_t)now ^ (uint32_t)(now >> 32);

  connection_id ^= ((uint32_t)context->local_mac[0] << 24) |
   ((uint32_t)context->local_mac[1] << 16) |
   ((uint32_t)context->local_mac[4] << 8) |
   context->local_mac[5];
  return connection_id == 0 ? 1u : connection_id;
}

static int psp_adhoc_create_pdp(PspAdhocContext *context,
 unsigned short port, PspAdhocPhase phase)
{
  psp_adhoc_set_phase(context, phase);
  context->socket_id = sceNetAdhocPdpCreate(context->local_mac, port,
   PSP_ADHOC_SOCKET_BUFFER, 0);
  if(context->socket_id < 0)
  {
    context->last_error = context->socket_id;
    context->socket_id = -1;
    return 0;
  }
  context->local_port = port;
  return 1;
}

static int psp_adhoc_open_pdp(PspAdhocContext *context,
 unsigned short port)
{
  return psp_adhoc_create_pdp(context, port,
   PSP_ADHOC_PHASE_PDP_CREATE);
}


static int psp_adhoc_send_raw(PspAdhocContext *context,
 const unsigned char *peer_mac, unsigned short peer_port,
 const uint8_t *data, size_t size)
{
  int result;

  if(context->socket_id < 0)
    return 0;
  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PDP_SEND);
  result = sceNetAdhocPdpSend(context->socket_id,
   (unsigned char *)peer_mac, peer_port, (void *)data, (unsigned int)size,
   0, 1);
  if(result == 0 || result == (int)size)
  {
    psp_adhoc_increment(&context->tx_datagrams);
    if(UINT_MAX - context->sent_bytes < size)
      context->sent_bytes = UINT_MAX;
    else
      context->sent_bytes += (unsigned int)size;
    return 1;
  }
  if(result < 0 && psp_adhoc_is_temporary_error(result))
  {
    psp_adhoc_increment(&context->api_retries);
    if(result == SCE_NET_ERROR_NO_SPACE)
    {
      psp_adhoc_increment(&context->backpressure_events);
      psp_adhoc_note_api_failure(context, result,
       PSP_ADHOC_PHASE_PDP_SEND);
    }
    return 0;
  }
  if(result < 0 && psp_adhoc_is_recoverable_socket_error(result))
  {
    psp_adhoc_request_recovery(context, result, 0, LINK_TIMEOUT_NONE);
    return 0;
  }

  context->last_error = result < 0 ? result : PSP_ADHOC_ERROR_PROTOCOL;
  return -1;
}

static int psp_adhoc_receive_raw(PspAdhocContext *context, uint8_t *data,
 size_t capacity, size_t *data_size, unsigned char *peer_mac,
 unsigned short *peer_port)
{
  unsigned int length = (unsigned int)capacity;
  int result;

  if(context->socket_id < 0)
    return 0;
  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PDP_RECEIVE);
  result = sceNetAdhocPdpRecv(context->socket_id, peer_mac, peer_port,
   data, &length, 0, 1);
  if(result < 0)
  {
    if(psp_adhoc_is_temporary_error(result))
      return 0;
    if(psp_adhoc_is_recoverable_socket_error(result))
    {
      psp_adhoc_request_recovery(context, result, 0, LINK_TIMEOUT_NONE);
      return 0;
    }
    context->last_error = result;
    return -1;
  }

  if(result > 0 && (length == capacity || length == 0))
    length = (unsigned int)result;
  if(length == 0 || length > capacity)
  {
    context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
    return -1;
  }
  *data_size = length;
  psp_adhoc_increment(&context->rx_datagrams);
  return 1;
}

static int psp_adhoc_receive_rendezvous(PspAdhocContext *context,
 AdhocRendezvousMessage *message, unsigned char *peer_mac,
 unsigned short *peer_port)
{
  uint8_t encoded[ADHOC_DATAGRAM_MAX_PACKET];
  size_t encoded_size = 0;
  int result;

  result = psp_adhoc_receive_raw(context, encoded, sizeof(encoded),
   &encoded_size, peer_mac, peer_port);
  if(result <= 0)
    return result;
  if(!adhoc_rendezvous_decode(encoded, encoded_size, message))
    return 0;
  return 1;
}

static int psp_adhoc_send_rendezvous(PspAdhocContext *context,
 AdhocRendezvousKind kind, uint32_t epoch, uint32_t recovery_id)
{
  AdhocRendezvousMessage message;
  uint8_t encoded[ADHOC_RENDEZVOUS_PACKET_SIZE];
  size_t encoded_size = 0;

  message.kind = kind;
  message.connection_id = context->connection_id;
  message.epoch = epoch;
  message.recovery_id = recovery_id;
  if(!adhoc_rendezvous_encode(&message, encoded, sizeof(encoded),
     &encoded_size))
  {
    context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
    return -1;
  }
  return psp_adhoc_send_raw(context, context->peer_mac,
   context->peer_port, encoded, encoded_size);
}

static int psp_adhoc_host_connection(PspAdhocContext *context)
{
  AdhocRendezvousMessage message;
  unsigned char source_mac[ADHOC_PEER_MAC_SIZE];
  unsigned short source_port;
  int acknowledged;
  int attempt;
  int result;

  psp_adhoc_close_connection(context);
  context->is_host = 1;
  psp_adhoc_prepare_connection(context);
  context->state = PSP_ADHOC_HOSTING;
  context->last_error = PSP_ADHOC_ERROR_NONE;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_HOST_CREATE);
  result = sceNetAdhocctlCreate(psp_adhoc_group);
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }
  context->control_connected = 1;

  result = psp_adhoc_wait_control_state(context, PSP_ADHOC_CONTROL_TIMEOUT);
  if(result <= 0)
    return result;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_MAC_ADDRESS);
  result = sceWlanGetEtherAddr(context->local_mac);
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }

  if(!psp_adhoc_open_pdp(context, PSP_ADHOC_PORT))
    return -1;

  while(!psp_adhoc_cancelled(context))
  {
    if(psp_adhoc_control_command(context))
      return 0;
    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_WLAN);
    if(!sceWlanGetSwitchState())
    {
      context->last_error = PSP_ADHOC_ERROR_WLAN_OFF;
      return -1;
    }

    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PDP_HANDSHAKE);
    source_port = 0;
    result = psp_adhoc_receive_rendezvous(context, &message, source_mac,
     &source_port);
    if(result < 0)
      return -1;
    if(result > 0 && message.kind == ADHOC_RENDEZVOUS_CONNECT &&
       message.epoch == 1)
    {
      memcpy(context->peer_mac, source_mac, sizeof(context->peer_mac));
      context->peer_port = source_port;
      context->peer_valid = 1;
      context->connection_id = message.connection_id;
      acknowledged = 0;
      for(attempt = 0; attempt < PSP_ADHOC_HANDSHAKE_ACKS; attempt++)
      {
        result = psp_adhoc_send_rendezvous(context,
         ADHOC_RENDEZVOUS_CONNECT_ACK, 1, 0);
        if(result < 0)
          return -1;
        if(result > 0)
          acknowledged = 1;
        if(attempt + 1 < PSP_ADHOC_HANDSHAKE_ACKS)
          sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
      }
      if(acknowledged)
      {
        adhoc_datagram_init(&context->datagram,
         context->connection_id, 1, psp_adhoc_now());
        adhoc_datagram_set_role(&context->datagram, context->is_host);
        psp_adhoc_transport_reset(context, context->connection_id, 1u);
        return 1;
      }
    }

    sceKernelDelayThread(PSP_ADHOC_POLL_DELAY);
  }

  return 0;
}

static int psp_adhoc_scan(PspAdhocContext *context,
 struct SceNetAdhocctlScanInfo *selected)
{
  struct SceNetAdhocctlScanInfo scans[PSP_ADHOC_SCAN_CAPACITY];
  uint64_t started;
  int length;
  int count;
  int state;
  int result;
  int index;

  started = sceKernelGetSystemTimeWide();
  do
  {
    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_SCAN_START);
    result = sceNetAdhocctlScan();
    if(result != SCE_ADHOCCTL_ERROR_BUSY)
      break;
    if(psp_adhoc_control_command(context))
      return 0;
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }
  while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_CONTROL_RETRY);
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }

  started = sceKernelGetSystemTimeWide();
  while(sceKernelGetSystemTimeWide() - started < 1500000u)
  {
    if(psp_adhoc_control_command(context))
      return 0;

    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_SCAN_WAIT);
    state = 0;
    result = sceNetAdhocctlGetState(&state);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }
    if(state != 2 && sceKernelGetSystemTimeWide() - started >= 250000u)
      break;
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  memset(scans, 0, sizeof(scans));
  length = sizeof(scans);
  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_SCAN_RESULTS);
  result = sceNetAdhocctlGetScanInfo(&length, scans);
  if(psp_adhoc_is_control_temporary_error(result))
    return 2;
  if(result < 0)
  {
    context->last_error = result;
    return -1;
  }

  count = length / (int)sizeof(scans[0]);
  if(count > (int)PSP_ADHOC_SCAN_CAPACITY)
    count = PSP_ADHOC_SCAN_CAPACITY;
  for(index = 0; index < count; index++)
  {
    if(memcmp(scans[index].name, psp_adhoc_group,
       sizeof(scans[index].name)) == 0)
    {
      *selected = scans[index];
      selected->next = NULL;
      return 1;
    }
  }

  return 2;
}

static int psp_adhoc_resolve_peer(PspAdhocContext *context,
 const unsigned char *local_mac, const unsigned char *preferred_mac,
 unsigned char *peer_mac)
{
  struct SceNetAdhocctlPeerInfo peers[PSP_ADHOC_PEER_CAPACITY];
  uint8_t peer_macs[PSP_ADHOC_PEER_CAPACITY][ADHOC_PEER_MAC_SIZE];
  uint64_t started = sceKernelGetSystemTimeWide();
  int length;
  int count;
  int result;
  int index;

  while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_PEER_TIMEOUT)
  {
    if(psp_adhoc_control_command(context))
      return 0;

    memset(peers, 0, sizeof(peers));
    length = sizeof(peers);
    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PEER_LIST);
    result = sceNetAdhocctlGetPeerList(&length, peers);
    if(result < 0)
    {
      if(psp_adhoc_is_control_temporary_error(result))
      {
        sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
        continue;
      }
      context->last_error = result;
      return -1;
    }

    count = length / (int)sizeof(peers[0]);
    if(count > (int)PSP_ADHOC_PEER_CAPACITY)
      count = PSP_ADHOC_PEER_CAPACITY;
    for(index = 0; index < count; index++)
      memcpy(peer_macs[index], peers[index].mac, ADHOC_PEER_MAC_SIZE);

    if(adhoc_peer_select(local_mac, preferred_mac, &peer_macs[0][0],
       (size_t)count, peer_mac))
      return 1;

    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  psp_adhoc_set_timeout(context, LINK_TIMEOUT_PEER_DISCOVERY);
  return -1;
}

static int psp_adhoc_join_connection(PspAdhocContext *context)
{
  struct SceNetAdhocctlScanInfo selected;
  AdhocRendezvousMessage message;
  unsigned char source_mac[ADHOC_PEER_MAC_SIZE];
  unsigned char peer_mac[ADHOC_PEER_MAC_SIZE];
  unsigned short source_port;
  uint64_t started;
  uint64_t next_connect_us;
  uint64_t now;
  int result;

  psp_adhoc_close_connection(context);
  context->is_host = 0;
  psp_adhoc_prepare_connection(context);
  context->state = PSP_ADHOC_SEARCHING;
  context->last_error = PSP_ADHOC_ERROR_NONE;

  while(!psp_adhoc_cancelled(context))
  {
    result = psp_adhoc_scan(context, &selected);
    if(result <= 0)
      return result;
    if(result == 2)
    {
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
      continue;
    }

    started = sceKernelGetSystemTimeWide();
    do
    {
      psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_JOIN_GROUP);
      result = sceNetAdhocctlJoin(&selected);
      if(result != SCE_ADHOCCTL_ERROR_BUSY)
        break;
      if(psp_adhoc_control_command(context))
        return 0;
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
    }
    while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_CONTROL_RETRY);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }
    context->control_connected = 1;
    context->state = PSP_ADHOC_CONNECTING;

    result = psp_adhoc_wait_control_state(context,
     PSP_ADHOC_CONTROL_TIMEOUT);
    if(result <= 0)
      return result;

    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_MAC_ADDRESS);
    result = sceWlanGetEtherAddr(context->local_mac);
    if(result < 0)
    {
      context->last_error = result;
      return -1;
    }

    result = psp_adhoc_resolve_peer(context, context->local_mac,
     selected.bssid,
     peer_mac);
    if(result <= 0)
      return result;

    memcpy(context->peer_mac, peer_mac, sizeof(context->peer_mac));
    context->peer_port = PSP_ADHOC_PORT;
    context->peer_valid = 1;
    context->connection_id = psp_adhoc_make_connection_id(context);
    if(!psp_adhoc_open_pdp(context, PSP_ADHOC_CLIENT_PORT))
      return -1;

    started = sceKernelGetSystemTimeWide();
    next_connect_us = started;
    while(sceKernelGetSystemTimeWide() - started < PSP_ADHOC_CONNECT_TIMEOUT)
    {
      if(psp_adhoc_control_command(context))
        return 0;

      now = sceKernelGetSystemTimeWide();
      if(now >= next_connect_us)
      {
        psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PDP_HANDSHAKE);
        result = psp_adhoc_send_rendezvous(context,
         ADHOC_RENDEZVOUS_CONNECT, 1, 0);
        if(result < 0)
          return -1;
        next_connect_us = now + PSP_ADHOC_HANDSHAKE_RETRY;
      }

      source_port = 0;
      result = psp_adhoc_receive_rendezvous(context, &message, source_mac,
       &source_port);
      if(result < 0)
        return -1;
      if(result > 0 &&
         memcmp(source_mac, context->peer_mac, sizeof(source_mac)) == 0 &&
         source_port == context->peer_port &&
         message.kind == ADHOC_RENDEZVOUS_CONNECT_ACK &&
         message.connection_id == context->connection_id &&
         message.epoch == 1)
      {
        adhoc_datagram_init(&context->datagram,
         context->connection_id, 1, psp_adhoc_now());
        adhoc_datagram_set_role(&context->datagram, context->is_host);
        psp_adhoc_transport_reset(context, context->connection_id, 1u);
        return 1;
      }
      sceKernelDelayThread(PSP_ADHOC_POLL_DELAY);
    }

    psp_adhoc_set_timeout(context, LINK_TIMEOUT_CONNECT_HANDSHAKE);
    return -1;
  }

  return 0;
}

static int psp_adhoc_recovery_cancelled(const PspAdhocContext *context)
{
  return psp_adhoc_cancelled(context) || context->close_requested;
}

static void psp_adhoc_drop_pdp(PspAdhocContext *context)
{
  int socket_id = context->socket_id;

  context->socket_id = -1;
  psp_adhoc_memory_barrier();
  if(socket_id >= 0)
    sceNetAdhocPdpDelete(socket_id, 0);
}

static int psp_adhoc_rendezvous_from_peer(PspAdhocContext *context,
 const AdhocRendezvousMessage *message, const unsigned char *source_mac,
 unsigned short source_port)
{
  return message->connection_id == context->connection_id &&
   memcmp(source_mac, context->peer_mac, ADHOC_PEER_MAC_SIZE) == 0 &&
   source_port == context->peer_port;
}

static int psp_adhoc_send_resync_repeated(PspAdhocContext *context,
 AdhocRendezvousKind kind, uint32_t epoch, uint32_t recovery_id)
{
  int acknowledged = 0;
  int attempt;
  int result;

  for(attempt = 0; attempt < PSP_ADHOC_HANDSHAKE_ACKS; attempt++)
  {
    result = psp_adhoc_send_rendezvous(context,
     kind, epoch, recovery_id);
    if(result < 0)
      return 0;
    if(result > 0)
      acknowledged = 1;
    if(attempt + 1 < PSP_ADHOC_HANDSHAKE_ACKS)
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }
  return acknowledged;
}

static int psp_adhoc_log_sample(uint32_t count)
{
  return count != 0u && (count & (count - 1u)) == 0u;
}

static void psp_adhoc_log_resync_request(PspAdhocContext *context,
 int accepted, int pending, uint32_t requested_epoch,
 uint32_t recovery_id)
{
  uint32_t count;

  if(accepted)
    count = ++context->resync_requests_accepted;
  else
    count = ++context->resync_requests_rejected;
  if(!psp_adhoc_log_sample(count))
    return;
  runtime_log_psp_network(accepted ?
   RUNTIME_LOG_EVENT_LINK_REQUEST_ACCEPTED :
   RUNTIME_LOG_EVENT_LINK_REQUEST_REJECTED,
   (uint8_t)((context->is_host != 0) | (pending ? 2u : 0u)),
   count, requested_epoch, recovery_id);
}

static int psp_adhoc_resync_send_data(PspAdhocContext *context,
 uint64_t now)
{
  AdhocDatagramPacket packet;
  int result;

  if(!adhoc_datagram_prepare(&context->datagram, now, &packet))
    return 1;
  result = psp_adhoc_send_raw(context, context->peer_mac,
   context->peer_port, packet.data, packet.size);
  if(result < 0)
    return 0;
  if(result == 0)
    return adhoc_datagram_note_blocked(&context->datagram, &packet,
     psp_adhoc_now());
  return adhoc_datagram_note_sent(&context->datagram, &packet,
   psp_adhoc_now());
}

static int psp_adhoc_resync_receive_data(PspAdhocContext *context,
 const AdhocResync *resync, const uint8_t *input, size_t input_size,
 uint64_t now, int *peer_probe_received)
{
  uint8_t payload[ADHOC_DATAGRAM_MAX_PAYLOAD];
  size_t payload_size;
  AdhocDatagramReceiveResult received;

  received = adhoc_datagram_receive(&context->datagram, input,
   input_size, now);
  if(received == ADHOC_DATAGRAM_INVALID ||
     received == ADHOC_DATAGRAM_FOREIGN)
    return 1;
  psp_adhoc_note_activity(context);
  while(adhoc_datagram_pop(&context->datagram, payload,
        sizeof(payload), &payload_size))
  {
    if(adhoc_resync_accept_probe(resync, payload, payload_size) &&
       !*peer_probe_received)
    {
      *peer_probe_received = 1;
      runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RESYNC, 0x10u,
       (uint32_t)resync->state, resync->current_epoch,
       (uint32_t)(resync->role == ADHOC_RESYNC_HOST));
    }
  }
  return 1;
}

static int psp_adhoc_resync_transport(PspAdhocContext *context,
 uint64_t deadline)
{
  AdhocResync resync;
  AdhocRendezvousMessage message;
  uint8_t input[ADHOC_DATAGRAM_MAX_PACKET];
  uint8_t probe[ADHOC_RESYNC_PROBE_SIZE];
  unsigned char source_mac[ADHOC_PEER_MAC_SIZE];
  unsigned short source_port;
  uint64_t next_send_us = 0;
  uint64_t now;
  size_t input_size;
  unsigned int previous_state;
  int probe_queued = 0;
  int peer_probe_received = 0;
  int result;

  if(context->recovery_id == 0)
    psp_adhoc_request_recovery(context, PSP_ADHOC_ERROR_TIMEOUT, 0,
     LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
  adhoc_resync_init(&resync, context->is_host ? ADHOC_RESYNC_HOST :
   ADHOC_RESYNC_JOINER, context->datagram.epoch,
   context->recovery_id);
  next_send_us = psp_adhoc_now() + adhoc_resync_send_delay(resync.role, 1);
  context->resync_state = (unsigned int)resync.state + 1u;
  psp_adhoc_memory_barrier();
  runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RESYNC, 0,
   context->resync_state, context->datagram.epoch,
   (uint32_t)(context->is_host != 0));
  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_TRANSPORT_RECOVER);
  while(psp_adhoc_now() < deadline)
  {
    unsigned int actions;

    if(psp_adhoc_recovery_cancelled(context))
      return 0;
    now = psp_adhoc_now();

    if(adhoc_resync_data_phase(&resync))
    {
      if(!probe_queued)
      {
        if(!adhoc_resync_encode_probe(&resync, probe, sizeof(probe)) ||
           !adhoc_datagram_queue(&context->datagram, probe,
            sizeof(probe)))
        {
          context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
          return 0;
        }
        probe_queued = 1;
      }
      if(!psp_adhoc_resync_send_data(context, now))
        return 0;
      if(!resync.local_data_ready && peer_probe_received &&
         adhoc_datagram_outstanding(&context->datagram) == 0u)
      {
        actions = adhoc_resync_mark_data_ready(&resync);
        runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RESYNC, 0x20u,
         (uint32_t)resync.state, resync.current_epoch,
         (uint32_t)(resync.role == ADHOC_RESYNC_HOST));
        if((actions & ADHOC_RESYNC_ACTION_SEND_NOW) != 0)
          next_send_us = 0;
      }
    }

    if(now >= next_send_us)
    {
      AdhocRendezvousKind kind;
      uint32_t epoch;
      uint32_t recovery_id;

      if(adhoc_resync_outbound(&resync, &kind, &epoch, &recovery_id))
      {
        result = psp_adhoc_send_rendezvous(context, kind, epoch,
         recovery_id);
        if(result < 0)
          return 0;
      }
      next_send_us = now + adhoc_resync_send_delay(resync.role, 0);
    }

    input_size = 0;
    source_port = 0;
    result = psp_adhoc_receive_raw(context, input, sizeof(input),
     &input_size, source_mac, &source_port);
    if(result < 0)
      return 0;
    if(result == 0)
    {
      sceKernelDelayThread(PSP_ADHOC_POLL_DELAY);
      continue;
    }
    if(memcmp(source_mac, context->peer_mac, ADHOC_PEER_MAC_SIZE) != 0 ||
       source_port != context->peer_port)
      continue;

    if(!adhoc_rendezvous_decode(input, input_size, &message))
    {
      if(adhoc_resync_data_phase(&resync) &&
         !psp_adhoc_resync_receive_data(context, &resync, input,
          input_size, now, &peer_probe_received))
        return 0;
      continue;
    }
    if(!psp_adhoc_rendezvous_from_peer(context, &message,
       source_mac, source_port))
      continue;

    {
      actions = adhoc_resync_receive(&resync,
       message.kind, message.epoch, message.recovery_id);

      context->recovery_id = resync.recovery_id;

      previous_state = context->resync_state;
      context->resync_state = (unsigned int)resync.state + 1u;
      psp_adhoc_memory_barrier();
      if(context->resync_state != previous_state)
        runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RESYNC,
         (uint8_t)message.kind, previous_state, context->resync_state,
         resync.current_epoch);
      if((actions & ADHOC_RESYNC_ACTION_RESET_EPOCH) != 0)
      {
        adhoc_datagram_init(&context->datagram,
         context->connection_id, resync.current_epoch, now);
        adhoc_datagram_set_role(&context->datagram, context->is_host);
        psp_adhoc_transport_reset(context, context->connection_id,
         resync.current_epoch);
        probe_queued = 0;
        peer_probe_received = 0;
      }
      if((actions & ADHOC_RESYNC_ACTION_FINAL_ACK) != 0 &&
         !psp_adhoc_send_resync_repeated(context,
          ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK,
          resync.current_epoch, resync.recovery_id))
        return 0;
      if((actions & ADHOC_RESYNC_ACTION_COMPLETE) != 0)
      {
        runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RESYNC, 0x80u,
         context->resync_state, resync.current_epoch, actions);
        return 1;
      }
      if((actions & ADHOC_RESYNC_ACTION_SEND_NOW) != 0)
        next_send_us = 0;
    }
  }

  psp_adhoc_set_timeout(context, LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
  runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RESYNC, 0x40u,
   context->resync_state, context->datagram.epoch,
   (uint32_t)context->last_error);
  return 0;
}

static int psp_adhoc_wait_control_value(PspAdhocContext *context,
 int expected, uint64_t deadline)
{
  int state;
  int result;

  while(psp_adhoc_now() < deadline)
  {
    if(psp_adhoc_recovery_cancelled(context))
      return 0;
    if(!sceWlanGetSwitchState())
    {
      context->last_error = PSP_ADHOC_ERROR_WLAN_OFF;
      return 0;
    }

    state = -1;
    result = sceNetAdhocctlGetState(&state);
    if(result < 0)
    {
      context->last_error = result;
      return 0;
    }
    if(state == expected)
      return 1;
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  psp_adhoc_set_timeout(context, LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
  return 0;
}

static int psp_adhoc_recovery_disconnect_group(PspAdhocContext *context,
 uint64_t deadline)
{
  int state = -1;
  int result;

  psp_adhoc_drop_pdp(context);
  if(!context->control_initialized)
    return 0;

  result = sceNetAdhocctlGetState(&state);
  if(result < 0)
  {
    context->last_error = result;
    return 0;
  }
  if(state != 0 || context->control_connected)
  {
    do
    {
      result = sceNetAdhocctlDisconnect();
      if(result == 0)
        break;
      if(result != SCE_ADHOCCTL_ERROR_BUSY)
      {
        context->last_error = result;
        return 0;
      }
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
    }
    while(psp_adhoc_now() < deadline);
    if(result != 0)
    {
      context->last_error = result;
      return 0;
    }
  }
  context->control_connected = 0;
  psp_adhoc_memory_barrier();
  return psp_adhoc_wait_control_value(context, 0, deadline);
}

static int psp_adhoc_recovery_find_group(PspAdhocContext *context,
 struct SceNetAdhocctlScanInfo *selected, uint64_t deadline)
{
  struct SceNetAdhocctlScanInfo scans[PSP_ADHOC_SCAN_CAPACITY];
  uint64_t scan_started;
  int length;
  int count;
  int state;
  int result;
  int index;

  while(psp_adhoc_now() < deadline)
  {
    if(psp_adhoc_recovery_cancelled(context))
      return 0;

    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_SCAN_START);
    result = sceNetAdhocctlScan();
    if(result == SCE_ADHOCCTL_ERROR_BUSY)
    {
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
      continue;
    }
    if(result < 0)
    {
      context->last_error = result;
      return 0;
    }

    scan_started = psp_adhoc_now();
    while(psp_adhoc_now() < deadline &&
          psp_adhoc_now() - scan_started < 1500000u)
    {
      if(psp_adhoc_recovery_cancelled(context))
        return 0;
      psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_SCAN_WAIT);
      state = -1;
      result = sceNetAdhocctlGetState(&state);
      if(result < 0)
      {
        context->last_error = result;
        return 0;
      }
      if(state != 2 &&
         psp_adhoc_now() - scan_started >= 250000u)
        break;
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
    }

    memset(scans, 0, sizeof(scans));
    length = sizeof(scans);
    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_SCAN_RESULTS);
    result = sceNetAdhocctlGetScanInfo(&length, scans);
    if(psp_adhoc_is_control_temporary_error(result))
    {
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
      continue;
    }
    if(result < 0)
    {
      context->last_error = result;
      return 0;
    }

    count = length / (int)sizeof(scans[0]);
    if(count > (int)PSP_ADHOC_SCAN_CAPACITY)
      count = PSP_ADHOC_SCAN_CAPACITY;
    for(index = 0; index < count; index++)
    {
      if(memcmp(scans[index].name, psp_adhoc_group,
         sizeof(scans[index].name)) == 0)
      {
        *selected = scans[index];
        selected->next = NULL;
        return 1;
      }
    }
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  psp_adhoc_set_timeout(context, LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
  return 0;
}

static int psp_adhoc_recovery_resolve_peer(PspAdhocContext *context,
 const unsigned char *preferred_mac, unsigned char *peer_mac,
 uint64_t deadline)
{
  struct SceNetAdhocctlPeerInfo peers[PSP_ADHOC_PEER_CAPACITY];
  uint8_t peer_macs[PSP_ADHOC_PEER_CAPACITY][ADHOC_PEER_MAC_SIZE];
  int length;
  int count;
  int result;
  int index;

  while(psp_adhoc_now() < deadline)
  {
    if(psp_adhoc_recovery_cancelled(context))
      return 0;
    memset(peers, 0, sizeof(peers));
    length = sizeof(peers);
    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PEER_LIST);
    result = sceNetAdhocctlGetPeerList(&length, peers);
    if(result < 0)
    {
      if(psp_adhoc_is_control_temporary_error(result))
      {
        sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
        continue;
      }
      context->last_error = result;
      return 0;
    }

    count = length / (int)sizeof(peers[0]);
    if(count > (int)PSP_ADHOC_PEER_CAPACITY)
      count = PSP_ADHOC_PEER_CAPACITY;
    for(index = 0; index < count; index++)
      memcpy(peer_macs[index], peers[index].mac, ADHOC_PEER_MAC_SIZE);
    if(adhoc_peer_select(context->local_mac, preferred_mac,
       &peer_macs[0][0], (size_t)count, peer_mac))
      return 1;
    sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
  }

  psp_adhoc_set_timeout(context, LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
  return 0;
}

static int psp_adhoc_rebuild_group(PspAdhocContext *context,
 uint64_t now)
{
  struct SceNetAdhocctlScanInfo selected;
  unsigned char peer_mac[ADHOC_PEER_MAC_SIZE];
  uint64_t deadline = now + PSP_ADHOC_GROUP_OPERATION;
  uint64_t disconnect_deadline = now + PSP_ADHOC_DISCONNECT_WAIT;
  int result;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_GROUP_RECOVER);
  if(!psp_adhoc_recovery_disconnect_group(context,
     disconnect_deadline < deadline ? disconnect_deadline : deadline))
    return 0;

  if(context->is_host)
  {
    do
    {
      if(psp_adhoc_recovery_cancelled(context))
        return 0;
      psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_HOST_CREATE);
      result = sceNetAdhocctlCreate(psp_adhoc_group);
      if(result != SCE_ADHOCCTL_ERROR_BUSY)
        break;
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
    }
    while(psp_adhoc_now() < deadline);
    if(result < 0)
    {
      context->last_error = result;
      return 0;
    }
    context->control_connected = 1;
    if(!psp_adhoc_wait_control_value(context, 1, deadline))
      return 0;
    if(!psp_adhoc_open_pdp(context, PSP_ADHOC_PORT) ||
       !psp_adhoc_resync_transport(context, deadline))
      return 0;
  }
  else
  {
    memset(&selected, 0, sizeof(selected));
    if(!psp_adhoc_recovery_find_group(context, &selected, deadline))
      return 0;
    do
    {
      if(psp_adhoc_recovery_cancelled(context))
        return 0;
      psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_JOIN_GROUP);
      result = sceNetAdhocctlJoin(&selected);
      if(result != SCE_ADHOCCTL_ERROR_BUSY)
        break;
      sceKernelDelayThread(PSP_ADHOC_CONTROL_DELAY);
    }
    while(psp_adhoc_now() < deadline);
    if(result < 0)
    {
      context->last_error = result;
      return 0;
    }
    context->control_connected = 1;
    if(!psp_adhoc_wait_control_value(context, 1, deadline))
      return 0;
    if(!psp_adhoc_recovery_resolve_peer(context, selected.bssid,
       peer_mac, deadline))
      return 0;
    memcpy(context->peer_mac, peer_mac, sizeof(context->peer_mac));
    context->peer_port = PSP_ADHOC_PORT;
    context->peer_valid = 1;
    if(!psp_adhoc_open_pdp(context, PSP_ADHOC_CLIENT_PORT) ||
       !psp_adhoc_resync_transport(context, deadline))
      return 0;
  }

  context->last_error = PSP_ADHOC_ERROR_NONE;
  psp_adhoc_memory_barrier();
  return 1;
}

static int psp_adhoc_restart_stack(PspAdhocContext *context)
{
  int result;

  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_STACK_RECOVER);
  psp_adhoc_drop_pdp(context);
  if(context->control_initialized && context->control_connected)
    sceNetAdhocctlDisconnect();
  context->control_connected = 0;
  psp_adhoc_network_deinit(context);
  if(psp_adhoc_recovery_cancelled(context))
    return 0;

  result = psp_adhoc_network_init(context);
  if(result < 0)
  {
    context->last_error = result;
    return 0;
  }
  result = sceWlanGetEtherAddr(context->local_mac);
  if(result < 0)
  {
    context->last_error = result;
    return 0;
  }
  return psp_adhoc_rebuild_group(context, sceKernelGetSystemTimeWide());
}

static PspAdhocIoResult psp_adhoc_finish_recovery(
 PspAdhocContext *context)
{
  context->last_completed_recovery_id = context->recovery_id;
  context->recovery_id = 0;
  context->recovery_request_id = 0;
  context->recovery_request_started_us = 0;
  context->recovery_request_next_us = 0;
  context->ack_stall_started_us = 0;
  context->health_causes = 0;
  adhoc_resync_request_gate_settle(&context->resync_request_gate,
   psp_adhoc_now(), PSP_ADHOC_RECOVERY_SETTLE);
  context->last_error = PSP_ADHOC_ERROR_NONE;
  context->state = PSP_ADHOC_CONNECTED;
  psp_adhoc_publish_health(context, 0);
  if(context->recovery_notified &&
     !psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_RECOVERED,
      0, NULL, 0))
  {
    context->last_error = PSP_ADHOC_ERROR_EVENT_QUEUE;
    return PSP_ADHOC_IO_ERROR;
  }
  runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 0x80u,
   context->recovery.total_transport_resyncs,
   context->recovery.total_group_rebuilds,
   context->recovery.total_stack_restarts);
  context->recovery_notified = 0;
  context->recovery_trigger_valid = 0;
  return PSP_ADHOC_IO_OK;
}

static PspAdhocIoResult psp_adhoc_recovery_step(PspAdhocContext *context,
 uint64_t now)
{
  AdhocRecoveryState previous_state = context->recovery.state;
  unsigned int actions = adhoc_recovery_poll(&context->recovery, now);
  uint64_t action_now = now;
  int result = 0;

  if(context->recovery.state != previous_state)
    runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 1,
     (uint32_t)previous_state, (uint32_t)context->recovery.state,
     actions);

  if(previous_state < ADHOC_RECOVERY_TRANSPORT_RESYNC &&
     context->recovery.state >= ADHOC_RECOVERY_TRANSPORT_RESYNC &&
     context->recovery.state <= ADHOC_RECOVERY_STACK_RESTART)
  {
    context->state = PSP_ADHOC_RECOVERING;
    if(!context->recovery_notified &&
       !psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_RECOVERING,
        0, NULL, 0))
    {
      context->last_error = PSP_ADHOC_ERROR_EVENT_QUEUE;
      return PSP_ADHOC_IO_ERROR;
    }
    context->recovery_notified = 1;
  }

  if(actions & ADHOC_RECOVERY_ACTION_FAIL)
  {
    psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_RECOVERY_FAILED);
    if(context->last_error == PSP_ADHOC_ERROR_NONE)
      psp_adhoc_set_timeout(context, LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
    runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 0x40u,
     (uint32_t)context->recovery.state, (uint32_t)context->last_error,
     (uint32_t)context->phase);
    return PSP_ADHOC_IO_ERROR;
  }

  if(actions & ADHOC_RECOVERY_ACTION_PROBE)
  {
    adhoc_datagram_request_probe(&context->datagram);
    adhoc_recovery_probe_sent(&context->recovery, now);
  }
  else if(actions & ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT)
  {
    result = psp_adhoc_resync_transport(context,
     now + PSP_ADHOC_TRANSPORT_RECOVERY);
    action_now = sceKernelGetSystemTimeWide();
    adhoc_recovery_platform_result(&context->recovery, action_now,
     result > 0);
  }
  else if(actions & ADHOC_RECOVERY_ACTION_REBUILD_GROUP)
  {
    result = psp_adhoc_rebuild_group(context, now);
    action_now = sceKernelGetSystemTimeWide();
    adhoc_recovery_platform_result(&context->recovery, action_now,
     result > 0);
  }
  else if(actions & ADHOC_RECOVERY_ACTION_RESTART_STACK)
  {
    result = psp_adhoc_restart_stack(context);
    action_now = sceKernelGetSystemTimeWide();
    adhoc_recovery_platform_result(&context->recovery, action_now,
     result > 0);
  }

  if((actions & (ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT |
                 ADHOC_RECOVERY_ACTION_REBUILD_GROUP |
                 ADHOC_RECOVERY_ACTION_RESTART_STACK)) != 0)
    runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 2,
     actions, (uint32_t)(result > 0),
     (uint32_t)context->recovery.state);

  if(result > 0 &&
     (actions & (ADHOC_RECOVERY_ACTION_RESYNC_TRANSPORT |
                 ADHOC_RECOVERY_ACTION_REBUILD_GROUP |
                 ADHOC_RECOVERY_ACTION_RESTART_STACK)))
    return psp_adhoc_finish_recovery(context);
  return PSP_ADHOC_IO_OK;
}


static PspAdhocIoResult psp_adhoc_note_datagram_activity(
 PspAdhocContext *context, uint64_t now)
{
  int was_probing = context->recovery.state == ADHOC_RECOVERY_PROBING;

  psp_adhoc_note_activity(context);
  adhoc_recovery_note_peer(&context->recovery, now);
  if(was_probing && context->recovery.state == ADHOC_RECOVERY_HEALTHY)
  {
    context->last_error = PSP_ADHOC_ERROR_NONE;
    context->state = PSP_ADHOC_CONNECTED;
    runtime_log_psp_network(RUNTIME_LOG_EVENT_LINK_RECOVERY, 3,
     ADHOC_RECOVERY_PROBING, ADHOC_RECOVERY_HEALTHY,
     context->datagram.epoch);
  }
  return PSP_ADHOC_IO_OK;
}

static int psp_adhoc_handle_resync_control(PspAdhocContext *context,
 const AdhocRendezvousMessage *message)
{
  uint32_t current_epoch = context->datagram.epoch;

  if(message->connection_id != context->connection_id ||
     message->kind == ADHOC_RENDEZVOUS_CONNECT ||
     message->kind == ADHOC_RENDEZVOUS_CONNECT_ACK)
    return 0;

  if(context->is_host)
  {
    if(message->kind == ADHOC_RENDEZVOUS_RESYNC_REQUEST)
    {
      AdhocResyncRequestResult request_result;

      if(message->recovery_id == context->last_completed_recovery_id)
      {
        psp_adhoc_log_resync_request(context, 0, 0, message->epoch,
         message->recovery_id);
        return 1;
      }
      request_result = adhoc_resync_request_gate_note(
       &context->resync_request_gate, current_epoch, message->epoch,
       message->recovery_id, psp_adhoc_now());
      if(request_result == ADHOC_RESYNC_REQUEST_ACCEPTED)
      {
        psp_adhoc_log_resync_request(context, 1, 0, message->epoch,
         message->recovery_id);
        psp_adhoc_request_recovery(context, PSP_ADHOC_ERROR_PEER_RECOVERY, 0,
         LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
      }
      else
      {
        psp_adhoc_log_resync_request(context, 0,
         request_result == ADHOC_RESYNC_REQUEST_PENDING,
         message->epoch, message->recovery_id);
      }
      return 1;
    }
  }
  else
  {
    if(message->kind == ADHOC_RENDEZVOUS_RESYNC_BEGIN)
    {
      if(message->recovery_id == context->last_completed_recovery_id ||
         message->epoch != adhoc_datagram_next_sequence(current_epoch))
      {
        psp_adhoc_log_resync_request(context, 0, 0, message->epoch,
         message->recovery_id);
      }
      else
      {
        psp_adhoc_request_recovery(context, PSP_ADHOC_ERROR_PEER_RECOVERY,
         message->recovery_id, LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
      }
      return 1;
    }
    if(message->kind == ADHOC_RENDEZVOUS_RESYNC_COMMIT &&
       message->epoch == current_epoch &&
       message->recovery_id == context->last_completed_recovery_id)
    {
      psp_adhoc_send_rendezvous(context,
       ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK, current_epoch,
       message->recovery_id);
      return 1;
    }
    if(message->kind == ADHOC_RENDEZVOUS_RESYNC_COMPLETE &&
       message->epoch == current_epoch &&
       message->recovery_id == context->last_completed_recovery_id)
    {
      psp_adhoc_send_resync_repeated(context,
       ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK, current_epoch,
       message->recovery_id);
      return 1;
    }
  }
  return 1;
}

static PspAdhocIoResult psp_adhoc_recovery_request_step(
 PspAdhocContext *context, uint64_t now)
{
  int result;

  if(context->is_host || context->recovery_request_id == 0)
    return PSP_ADHOC_IO_OK;
  if(now < context->recovery_request_started_us ||
     now - context->recovery_request_started_us >=
      PSP_ADHOC_RECOVERY_REQUEST_TIMEOUT)
  {
    psp_adhoc_set_timeout(context, LINK_TIMEOUT_RECOVERY_RENDEZVOUS);
    return PSP_ADHOC_IO_ERROR;
  }
  if(now < context->recovery_request_next_us)
    return PSP_ADHOC_IO_OK;
  result = psp_adhoc_send_rendezvous(context,
   ADHOC_RENDEZVOUS_RESYNC_REQUEST, context->datagram.epoch,
   context->recovery_request_id);
  if(result < 0)
    return PSP_ADHOC_IO_ERROR;
  context->recovery_request_next_us = now +
   PSP_ADHOC_RECOVERY_REQUEST_RETRY;
  return PSP_ADHOC_IO_OK;
}

static PspAdhocIoResult psp_adhoc_send_step(PspAdhocContext *context,
 PspAdhocSendProgress *progress)
{
  LinkLanesPacket packet;
  uint64_t now;
  int prepared;
  int result;

  if(progress != NULL)
    *progress = PSP_ADHOC_SEND_IDLE;

  if(context->socket_id < 0 ||
     context->recovery.state >= ADHOC_RECOVERY_TRANSPORT_RESYNC ||
     context->recovery_request_id != 0)
    return PSP_ADHOC_IO_OK;

  now = psp_adhoc_now();
  prepared = link_transport_scheduler_prepare(&context->transport, now,
   &packet);
  if(prepared < 0)
  {
    context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
    return PSP_ADHOC_IO_ERROR;
  }
  if(prepared == 0)
    return PSP_ADHOC_IO_OK;
  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PDP_SEND);
  result = psp_adhoc_send_raw(context, context->peer_mac,
   context->peer_port, packet.data, packet.size);
  if(result < 0)
    return PSP_ADHOC_IO_ERROR;
  if(result == 0)
  {
    if(!link_transport_scheduler_note_blocked(&context->transport,
       &packet, psp_adhoc_now()))
    {
      context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
      return PSP_ADHOC_IO_ERROR;
    }
    if(progress != NULL)
      *progress = PSP_ADHOC_SEND_BLOCKED;
    return PSP_ADHOC_IO_OK;
  }
  if(!link_transport_scheduler_note_sent(&context->transport, &packet,
     psp_adhoc_now()))
  {
    context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
    return PSP_ADHOC_IO_ERROR;
  }
  if(progress != NULL)
  {
    *progress = packet.kind == LINK_LANES_TX_CRITICAL ?
     PSP_ADHOC_SEND_SENT_CRITICAL : PSP_ADHOC_SEND_SENT;
  }
  return PSP_ADHOC_IO_OK;
}

static PspAdhocIoResult psp_adhoc_send_drain(PspAdhocContext *context,
 int await_hot_handoff)
{
  PspAdhocSendProgress progress;
  PspAdhocIoResult result = PSP_ADHOC_IO_OK;
  uint64_t started_us = psp_adhoc_now();
  uint64_t now_us;
  unsigned int packets = 0;
  unsigned int maximum = context->transport.lanes.profile ==
   LINK_TRANSPORT_PROFILE_HOT ? PSP_ADHOC_HOT_DRAIN_MAX : 1u;
  int hot = maximum != 1u;

  if(hot)
    psp_adhoc_increment(&context->hot_drain_count);
  for(;;)
  {
    result = psp_adhoc_send_step(context, &progress);
    if(result != PSP_ADHOC_IO_OK || progress == PSP_ADHOC_SEND_BLOCKED)
      break;
    if(progress == PSP_ADHOC_SEND_SENT ||
       progress == PSP_ADHOC_SEND_SENT_CRITICAL)
    {
      packets++;
      if(progress == PSP_ADHOC_SEND_SENT_CRITICAL)
        await_hot_handoff = 0;
    }
    else if(!await_hot_handoff)
    {
      break;
    }
    now_us = psp_adhoc_now();
    if(packets >= maximum || now_us < started_us ||
       now_us - started_us >= PSP_ADHOC_HOT_DRAIN_BUDGET_US)
      break;
    if(progress == PSP_ADHOC_SEND_IDLE)
    {
      psp_adhoc_increment(&context->hot_yields);
      sceKernelDelayThread(PSP_ADHOC_HOT_YIELD_US);
      psp_adhoc_consume_hot_wake(context, psp_adhoc_now());
    }
  }

  if(hot)
  {
    context->hot_drain_packets = psp_adhoc_counter_add(
     context->hot_drain_packets, packets);
    if(packets > context->hot_drain_high_water)
      context->hot_drain_high_water = packets;
  }
  return result;
}

static PspAdhocIoResult psp_adhoc_receive(PspAdhocContext *context,
 int *hot_delivery)
{
  uint8_t input[PSP_ADHOC_RECEIVE_SCRATCH];
  AdhocRendezvousMessage control;
  LinkSpscItem delivery;
  unsigned char source_mac[ADHOC_PEER_MAC_SIZE];
  unsigned short source_port;
  LinkLanesReceiveResult received;
  size_t input_size;
  uint64_t now;
  int reads;
  int result;

  if(hot_delivery != NULL)
    *hot_delivery = 0;

  if(context->socket_id < 0 ||
     context->recovery.state >= ADHOC_RECOVERY_TRANSPORT_RESYNC)
    return PSP_ADHOC_IO_OK;

  for(reads = 0; reads < (int)PSP_ADHOC_RECEIVE_BATCH; reads++)
  {
    input_size = 0;
    source_port = 0;
    result = psp_adhoc_receive_raw(context, input, sizeof(input),
     &input_size, source_mac, &source_port);
    if(result < 0)
      return PSP_ADHOC_IO_ERROR;
    if(result == 0)
      break;
    if(memcmp(source_mac, context->peer_mac, sizeof(source_mac)) != 0 ||
       source_port != context->peer_port)
      continue;

    now = psp_adhoc_now();
    if(adhoc_rendezvous_decode(input, input_size, &control))
    {
      if(psp_adhoc_handle_resync_control(context, &control))
        psp_adhoc_note_datagram_activity(context, now);
      continue;
    }

    if(context->recovery_request_id != 0)
      continue;

    received = link_transport_scheduler_receive(&context->transport,
     input, input_size, now, &delivery);
    if(received == LINK_LANES_INVALID ||
       received == LINK_LANES_FOREIGN || received == LINK_LANES_STALE ||
       received == LINK_LANES_DUPLICATE)
      continue;
    if(received == LINK_LANES_CONFLICT)
    {
      context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
      return PSP_ADHOC_IO_ERROR;
    }
    psp_adhoc_note_datagram_activity(context, now);
    if(received == LINK_LANES_ACKED)
    {
      continue;
    }
    if(received == LINK_LANES_DELIVERED)
    {
      LinkPacket decoded;

      if(link_protocol_decode(delivery.data, delivery.size, &decoded) !=
         LINK_PROTOCOL_OK)
      {
        context->last_error = PSP_ADHOC_ERROR_PROTOCOL;
        return PSP_ADHOC_IO_ERROR;
      }
      if(psp_adhoc_tx_lane(decoded.type) == LINK_LANE_CRITICAL)
      {
        psp_adhoc_hot_touch(context, now);
        if(hot_delivery != NULL)
          *hot_delivery = 1;
      }
      if(!psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_PACKET, 0,
         delivery.data, delivery.size))
      {
        context->last_error = PSP_ADHOC_ERROR_EVENT_QUEUE;
        return PSP_ADHOC_IO_ERROR;
      }
    }
  }
  return PSP_ADHOC_IO_OK;
}


static PspAdhocServiceResult psp_adhoc_connected_service(
 PspAdhocContext *context)
{
  PspAdhocCommand command;
  PspAdhocIoResult io_result;
  uint64_t health_check = sceKernelGetSystemTimeWide();
  uint64_t health_unhealthy_since = 0;
  uint64_t now;
  uint32_t health_causes;
  int state;
  int result;
  int ack_stalled;
  int backpressure_stalled;
  int hot_delivery;
  int hot_active;
  int health_error = 0;

  adhoc_recovery_init(&context->recovery, health_check,
   &psp_adhoc_recovery_config);
  context->worker_accounted_us = health_check;

  while(!psp_adhoc_cancelled(context))
  {
    if(context->close_requested)
      return PSP_ADHOC_SERVICE_CLOSED;

    now = sceKernelGetSystemTimeWide();
    psp_adhoc_consume_hot_wake(context, now);
    psp_adhoc_consume_session_recovery(context);
    io_result = psp_adhoc_recovery_step(context, now);
    if(io_result == PSP_ADHOC_IO_ERROR)
      return PSP_ADHOC_SERVICE_ERROR;

    io_result = psp_adhoc_recovery_request_step(context,
     sceKernelGetSystemTimeWide());
    if(io_result == PSP_ADHOC_IO_ERROR)
      return PSP_ADHOC_SERVICE_ERROR;

    io_result = psp_adhoc_receive(context, &hot_delivery);
    if(io_result == PSP_ADHOC_IO_PEER_CLOSED)
      return PSP_ADHOC_SERVICE_PEER_CLOSED;
    if(io_result == PSP_ADHOC_IO_ERROR)
      return PSP_ADHOC_SERVICE_ERROR;

    if(context->recovery.state < ADHOC_RECOVERY_TRANSPORT_RESYNC &&
       psp_adhoc_command_pop(context, &command))
    {
      switch(command.type)
      {
        case PSP_ADHOC_COMMAND_DISCONNECT:
          return PSP_ADHOC_SERVICE_CLOSED;

        case PSP_ADHOC_COMMAND_STOP:
          return PSP_ADHOC_SERVICE_CLOSED;

        case PSP_ADHOC_COMMAND_HOST:
        case PSP_ADHOC_COMMAND_JOIN:
          context->deferred_role = command.type;
          context->deferred_role_valid = 1;
          context->close_requested = 1;
          return PSP_ADHOC_SERVICE_CLOSED;
      }
    }

    io_result = psp_adhoc_send_drain(context, hot_delivery);
    if(io_result == PSP_ADHOC_IO_ERROR)
      return PSP_ADHOC_SERVICE_ERROR;

    now = sceKernelGetSystemTimeWide();
    ack_stalled = link_transport_lanes_progress_stalled(
     &context->transport.lanes, now, PSP_ADHOC_ACK_PROGRESS_TIMEOUT);
    backpressure_stalled = link_transport_lanes_backpressure_stalled(
     &context->transport.lanes, now, PSP_ADHOC_ACK_PROGRESS_TIMEOUT);
    health_causes = (ack_stalled ? PSP_ADHOC_HEALTH_CAUSE_ACK_STALL : 0u) |
     (backpressure_stalled ?
       PSP_ADHOC_HEALTH_CAUSE_BACKPRESSURE : 0u);
    context->health_causes = health_causes;
    context->health_cause_history |= health_causes;

    if(!ack_stalled)
    {
      context->ack_stall_started_us = 0;
    }
    else if(context->ack_stall_started_us == 0)
    {
      context->ack_stall_started_us = now;
      link_transport_lanes_request_probe(&context->transport.lanes);
      if(context->is_host)
        adhoc_recovery_probe(&context->recovery, now);
    }
    else if(now >= context->ack_stall_started_us &&
            now - context->ack_stall_started_us >=
             PSP_ADHOC_ACK_HARD_TIMEOUT &&
            context->recovery.state < ADHOC_RECOVERY_TRANSPORT_RESYNC)
    {
      psp_adhoc_request_recovery(context, PSP_ADHOC_ERROR_TIMEOUT, 0,
       LINK_TIMEOUT_RECEIVE_SILENCE);
    }
    if(link_transport_lanes_backpressure_stalled(&context->transport.lanes,
       now, PSP_ADHOC_ACK_HARD_TIMEOUT) &&
       context->recovery.state < ADHOC_RECOVERY_TRANSPORT_RESYNC)
    {
      psp_adhoc_request_recovery(context, SCE_NET_ERROR_NO_SPACE, 0,
       LINK_TIMEOUT_PRESSURE);
    }
    if(health_causes != 0)
      psp_adhoc_publish_health(context, 1);
    else if(!adhoc_recovery_active(&context->recovery) &&
            context->recovery_request_id == 0)
      psp_adhoc_publish_health(context, 0);

    if(adhoc_recovery_active(&context->recovery) ||
       context->recovery_request_id != 0)
    {
      health_unhealthy_since = 0;
      health_error = 0;
      health_check = now;
    }
    else if(now - health_check >= 250000u)
    {
      psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_WLAN);
      if(!sceWlanGetSwitchState())
      {
        context->last_error = PSP_ADHOC_ERROR_WLAN_OFF;
        return PSP_ADHOC_SERVICE_ERROR;
      }
      psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_HEALTH_CHECK);
      state = 0;
      result = sceNetAdhocctlGetState(&state);
      if(result < 0 || state != 1)
      {
        health_error = result < 0 ? result : SCE_ADHOC_ERROR_DISCONNECTED;
        if(health_unhealthy_since == 0)
          health_unhealthy_since = now;
        else if(now - health_unhealthy_since >= PSP_ADHOC_HEALTH_GRACE)
        {
          psp_adhoc_request_recovery(context, health_error, 0,
           LINK_TIMEOUT_RECEIVE_SILENCE);
          health_unhealthy_since = 0;
          health_error = 0;
        }
      }
      else
      {
        health_unhealthy_since = 0;
        health_error = 0;
      }
      health_check = now;
    }

    now = psp_adhoc_now();
    hot_active = psp_adhoc_hot_active(context, now);
    psp_adhoc_account_worker_time(context, now, hot_active);
    if(hot_active)
    {
      psp_adhoc_increment(&context->hot_yields);
      sceKernelDelayThread(PSP_ADHOC_HOT_YIELD_US);
    }
    else
    {
      psp_adhoc_tx_wake_wait(context, PSP_ADHOC_POLL_DELAY);
    }
  }

  return PSP_ADHOC_SERVICE_CLOSED;
}

static int psp_adhoc_thread(SceSize args, void *argp)
{
  PspAdhocContext *context = &psp_adhoc;
  PspAdhocCommand command;
  PspAdhocServiceResult service_result;
  int result;

  (void)args;
  (void)argp;

  result = psp_adhoc_network_init(context);
  if(result < 0)
  {
    psp_adhoc_set_error(context, result);
    psp_adhoc_network_term(context);
    psp_adhoc_tx_wake_cancel(context);
    return 0;
  }

  context->state = PSP_ADHOC_READY;
  psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_NONE);
  while(!psp_adhoc_cancelled(context))
  {
    if(context->deferred_role_valid)
    {
      memset(&command, 0, sizeof(command));
      command.type = context->deferred_role;
      context->deferred_role_valid = 0;
    }
    else if(!psp_adhoc_command_pop(context, &command))
    {
      psp_adhoc_tx_wake_wait(context, PSP_ADHOC_CONTROL_DELAY);
      continue;
    }

    if(command.type == PSP_ADHOC_COMMAND_STOP)
    {
      break;
    }
    if(command.type == PSP_ADHOC_COMMAND_DISCONNECT)
      continue;

    if(command.type == PSP_ADHOC_COMMAND_HOST)
      result = psp_adhoc_host_connection(context);
    else
      result = psp_adhoc_join_connection(context);

    if(result == 1)
    {
      context->state = PSP_ADHOC_CONNECTED;
      psp_adhoc_set_phase(context, PSP_ADHOC_PHASE_PDP_RECEIVE);
      psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_CONNECTED, 0, NULL, 0);
      service_result = psp_adhoc_connected_service(context);
      switch(service_result)
      {
        case PSP_ADHOC_SERVICE_PEER_CLOSED:
          psp_adhoc_capture_failure(context, context->last_error);
          psp_adhoc_emit(context, LINK_TRANSPORT_EVENT_CLOSED,
           context->last_error, NULL, 0);
          break;

        case PSP_ADHOC_SERVICE_ERROR:
          if(!context->close_requested)
            psp_adhoc_set_error(context, context->last_error);
          break;

        case PSP_ADHOC_SERVICE_CLOSED:
          break;
      }
    }
    else if(result < 0)
    {
      psp_adhoc_set_error(context, context->last_error);
    }

    if(psp_adhoc_cancelled(context))
      lifecycle_teardown_advance(&context->teardown,
       LIFECYCLE_TEARDOWN_WORKER_QUIESCED);
    psp_adhoc_close_connection(context);
    if(!psp_adhoc_cancelled(context) && !context->deferred_role_valid &&
       context->state != PSP_ADHOC_FAILED)
      context->state = PSP_ADHOC_READY;
  }

  if(psp_adhoc_cancelled(context))
    lifecycle_teardown_advance(&context->teardown,
     LIFECYCLE_TEARDOWN_WORKER_QUIESCED);
  psp_adhoc_network_term(context);
  psp_adhoc_tx_wake_cancel(context);
  if(psp_adhoc_cancelled(context))
    lifecycle_teardown_advance(&context->teardown,
     LIFECYCLE_TEARDOWN_WORKER_STOPPED);
  context->state = PSP_ADHOC_STOPPED;
  return 0;
}

static int psp_adhoc_transport_host(void *opaque)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocCommand command;

  if(psp_adhoc_cancelled(context))
    return 0;
  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_HOST;
  if(!psp_adhoc_command_push(context, &command))
  {
    psp_adhoc_capture_failure(context, PSP_ADHOC_ERROR_COMMAND_QUEUE);
    return 0;
  }
  return 1;
}

static int psp_adhoc_transport_join(void *opaque)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocCommand command;

  if(psp_adhoc_cancelled(context))
    return 0;
  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_JOIN;
  if(!psp_adhoc_command_push(context, &command))
  {
    psp_adhoc_capture_failure(context, PSP_ADHOC_ERROR_COMMAND_QUEUE);
    return 0;
  }
  return 1;
}

static int psp_adhoc_transport_send(void *opaque, const uint8_t *data,
 size_t size)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  LinkPacket packet;

  if(psp_adhoc_cancelled(context) || data == NULL || size == 0 ||
     size > LINK_PROTOCOL_MAX_PACKET ||
     link_protocol_decode(data, size, &packet) != LINK_PROTOCOL_OK)
    return 0;
  if(!psp_adhoc_tx_handoff_push(context, &packet, data, size))
  {
    psp_adhoc_capture_failure(context, PSP_ADHOC_ERROR_COMMAND_QUEUE);
    return 0;
  }
  return 1;
}

static int psp_adhoc_transport_poll(void *opaque, LinkTransportEvent *event)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  uint32_t activity_generation;
  uint32_t health_generation;

  if(event == NULL)
    return 0;
  health_generation = __atomic_load_n(&context->health_generation,
   __ATOMIC_ACQUIRE);
  if(health_generation != __atomic_load_n(&context->health_consumed,
     __ATOMIC_ACQUIRE))
  {
    memset(event, 0, sizeof(*event));
    event->type = __atomic_load_n(&context->transport_degraded,
     __ATOMIC_ACQUIRE) ? LINK_TRANSPORT_EVENT_DEGRADED :
     LINK_TRANSPORT_EVENT_HEALTHY;
    event->generation = __atomic_load_n(&context->health_epoch,
     __ATOMIC_ACQUIRE);
    __atomic_store_n(&context->health_consumed, health_generation,
     __ATOMIC_RELEASE);
    return 1;
  }
  if(psp_adhoc_event_pop(context, event))
    return 1;

  psp_adhoc_memory_barrier();
  activity_generation = context->activity_generation;
  if(activity_generation == context->activity_consumed)
    return 0;

  memset(event, 0, sizeof(*event));
  event->type = LINK_TRANSPORT_EVENT_ACTIVITY;
  event->generation = context->activity_epoch;
  context->activity_consumed = activity_generation;
  psp_adhoc_memory_barrier();
  return 1;
}

static int psp_adhoc_transport_request_recovery(void *opaque, int reason,
 LinkTimeoutSource timeout_source)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocState state = __atomic_load_n(&context->state, __ATOMIC_ACQUIRE);
  uint32_t generation;

  if(psp_adhoc_cancelled(context))
    return 0;
  if(state == PSP_ADHOC_RECOVERING)
    return 1;
  if(state != PSP_ADHOC_CONNECTED)
    return 0;
  __atomic_store_n(&context->session_recovery_reason, reason,
   __ATOMIC_RELEASE);
  __atomic_store_n(&context->session_recovery_timeout_source,
   timeout_source, __ATOMIC_RELEASE);
  generation = __atomic_add_fetch(&context->session_recovery_generation,
   1u, __ATOMIC_ACQ_REL);
  if(generation == 0)
    __atomic_store_n(&context->session_recovery_generation, 1u,
     __ATOMIC_RELEASE);
  psp_adhoc_tx_wake_signal(context);
  return 1;
}

static void psp_adhoc_transport_disconnect(void *opaque)
{
  PspAdhocContext *context = (PspAdhocContext *)opaque;
  PspAdhocCommand command;

  if(psp_adhoc_cancelled(context))
    return;
  context->close_requested = 1;
  psp_adhoc_memory_barrier();
  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_DISCONNECT;
  if(!psp_adhoc_command_push(context, &command))
    psp_adhoc_capture_failure(context, PSP_ADHOC_ERROR_COMMAND_QUEUE);
}

static uint64_t psp_adhoc_transport_now(void *opaque)
{
  (void)opaque;
  return sceKernelGetSystemTimeWide();
}

static const LinkTransportOps psp_adhoc_transport_ops =
{
  .host = psp_adhoc_transport_host,
  .join = psp_adhoc_transport_join,
  .send = psp_adhoc_transport_send,
  .poll = psp_adhoc_transport_poll,
  .request_recovery = psp_adhoc_transport_request_recovery,
  .disconnect = psp_adhoc_transport_disconnect,
  .now_us = psp_adhoc_transport_now
};

int psp_adhoc_start(void)
{
  SceUID thread;

  if(psp_adhoc.thread_id >= 0)
  {
    if(!psp_adhoc_cancelled(&psp_adhoc))
      return 1;
    psp_adhoc_stop();
    psp_adhoc_service_shutdown();
    if(psp_adhoc.thread_id >= 0)
      return 0;
  }
  psp_adhoc_tx_wake_delete(&psp_adhoc);
  memset(&psp_adhoc, 0, sizeof(psp_adhoc));
  psp_adhoc.thread_id = -1;
  psp_adhoc.tx_wake_id = -1;
  psp_adhoc.socket_id = -1;
  psp_adhoc.state = PSP_ADHOC_STARTING;
  lifecycle_teardown_init(&psp_adhoc.teardown);
  link_transport_scheduler_init(&psp_adhoc.transport);

  psp_adhoc_set_phase(&psp_adhoc, PSP_ADHOC_PHASE_WLAN);
  if(!sceWlanGetSwitchState())
  {
    psp_adhoc_capture_failure(&psp_adhoc, PSP_ADHOC_ERROR_WLAN_OFF);
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return 0;
  }

  psp_adhoc_set_phase(&psp_adhoc, PSP_ADHOC_PHASE_THREAD);
  if(!psp_adhoc_tx_wake_create(&psp_adhoc))
  {
    psp_adhoc_capture_failure(&psp_adhoc, psp_adhoc.last_error);
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return 0;
  }
  thread = sceKernelCreateThread("GBA link network", psp_adhoc_thread,
   0x1E, 0x10000, PSP_THREAD_ATTR_USER, NULL);
  if(thread < 0)
  {
    psp_adhoc_tx_wake_delete(&psp_adhoc);
    psp_adhoc_capture_failure(&psp_adhoc, thread);
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return 0;
  }

  psp_adhoc.thread_id = thread;
  if(sceKernelStartThread(thread, 0, NULL) < 0)
  {
    sceKernelDeleteThread(thread);
    psp_adhoc.thread_id = -1;
    psp_adhoc_tx_wake_delete(&psp_adhoc);
    psp_adhoc_capture_failure(&psp_adhoc, PSP_ADHOC_ERROR_THREAD);
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return 0;
  }
  return 1;
}

static void psp_adhoc_stop_internal(int exiting)
{
  PspAdhocCommand command;
  SceUInt timeout;
  uint64_t now;
  int result;

  if(psp_adhoc.thread_id < 0)
  {
    psp_adhoc_tx_wake_delete(&psp_adhoc);
    return;
  }

  now = psp_adhoc_now();
  lifecycle_teardown_begin(&psp_adhoc.teardown, now,
   exiting ? PSP_ADHOC_EXIT_TIMEOUT : PSP_ADHOC_SHUTDOWN_TIMEOUT);
  psp_adhoc.close_requested = 1;
  psp_adhoc_memory_barrier();

  memset(&command, 0, sizeof(command));
  command.type = PSP_ADHOC_COMMAND_STOP;
  psp_adhoc_command_push(&psp_adhoc, &command);
  psp_adhoc_tx_wake_cancel(&psp_adhoc);
  lifecycle_teardown_advance(&psp_adhoc.teardown,
   LIFECYCLE_TEARDOWN_WORKER_SIGNALED);
  psp_adhoc_memory_barrier();

  lifecycle_teardown_advance(&psp_adhoc.teardown,
   LIFECYCLE_TEARDOWN_WAITING);
  if(!exiting)
    return;

  timeout = lifecycle_teardown_remaining_us(&psp_adhoc.teardown,
   psp_adhoc_now());
  result = timeout == 0 ? -1 :
   sceKernelWaitThreadEnd(psp_adhoc.thread_id, &timeout);

  if(result < 0)
  {
    lifecycle_teardown_expire(&psp_adhoc.teardown);
    psp_adhoc_set_phase(&psp_adhoc, PSP_ADHOC_PHASE_SHUTDOWN);
    psp_adhoc_record_internal_error(&psp_adhoc,
     PSP_ADHOC_ERROR_DOMAIN_LIFECYCLE, PSP_ADHOC_ERROR_SHUTDOWN,
     LINK_TIMEOUT_SHUTDOWN);
    psp_adhoc_capture_failure(&psp_adhoc, PSP_ADHOC_ERROR_SHUTDOWN);
    psp_adhoc.state = PSP_ADHOC_FAILED;
    if(exiting && sceKernelTerminateDeleteThread(psp_adhoc.thread_id) >= 0)
    {
      psp_adhoc.thread_id = -1;
      psp_adhoc_tx_wake_delete(&psp_adhoc);
    }
    return;
  }

  result = sceKernelDeleteThread(psp_adhoc.thread_id);
  if(result < 0)
  {
    psp_adhoc_set_phase(&psp_adhoc, PSP_ADHOC_PHASE_SHUTDOWN);
    psp_adhoc_capture_failure(&psp_adhoc, PSP_ADHOC_ERROR_SHUTDOWN);
    psp_adhoc.state = PSP_ADHOC_FAILED;
    return;
  }

  psp_adhoc.thread_id = -1;
  psp_adhoc_tx_wake_delete(&psp_adhoc);
  psp_adhoc.state = PSP_ADHOC_STOPPED;
  lifecycle_teardown_advance(&psp_adhoc.teardown,
   LIFECYCLE_TEARDOWN_COMPLETE);
}

void psp_adhoc_stop(void)
{
  psp_adhoc_stop_internal(0);
}

void psp_adhoc_stop_for_exit(void)
{
  psp_adhoc_stop_internal(1);
}

int psp_adhoc_service_shutdown(void)
{
  SceKernelThreadInfo info;
  LifecycleTeardownPoll poll_result;
  int result;

  if(psp_adhoc.thread_id < 0)
  {
    psp_adhoc_tx_wake_delete(&psp_adhoc);
    return 1;
  }
  if(!psp_adhoc_cancelled(&psp_adhoc))
    return 1;

  memset(&info, 0, sizeof(info));
  info.size = sizeof(info);
  result = sceKernelReferThreadStatus(psp_adhoc.thread_id, &info);
  poll_result = lifecycle_teardown_poll(&psp_adhoc.teardown,
   psp_adhoc_now(), result >= 0 &&
    (info.status & (PSP_THREAD_STOPPED | PSP_THREAD_KILLED)) != 0);
  if(poll_result == LIFECYCLE_TEARDOWN_POLL_WORKER_STOPPED)
  {
    result = sceKernelDeleteThread(psp_adhoc.thread_id);
    if(result >= 0)
    {
      psp_adhoc.thread_id = -1;
      psp_adhoc_tx_wake_delete(&psp_adhoc);
      if(lifecycle_teardown_phase(&psp_adhoc.teardown) !=
         LIFECYCLE_TEARDOWN_DEADLINE_EXPIRED)
      {
        psp_adhoc.state = PSP_ADHOC_STOPPED;
        lifecycle_teardown_advance(&psp_adhoc.teardown,
         LIFECYCLE_TEARDOWN_COMPLETE);
      }
      return 1;
    }
  }

  if(poll_result == LIFECYCLE_TEARDOWN_POLL_PENDING)
    return 0;

  psp_adhoc_set_phase(&psp_adhoc, PSP_ADHOC_PHASE_SHUTDOWN);
  psp_adhoc_record_internal_error(&psp_adhoc,
   PSP_ADHOC_ERROR_DOMAIN_LIFECYCLE, PSP_ADHOC_ERROR_SHUTDOWN,
   LINK_TIMEOUT_SHUTDOWN);
  psp_adhoc_capture_failure(&psp_adhoc, PSP_ADHOC_ERROR_SHUTDOWN);
  psp_adhoc.state = PSP_ADHOC_FAILED;
  return -1;
}

int psp_adhoc_shutdown_pending(void)
{
  return psp_adhoc.thread_id >= 0 && psp_adhoc_cancelled(&psp_adhoc);
}

LinkTransport psp_adhoc_transport(void)
{
  LinkTransport transport;

  transport.ops = &psp_adhoc_transport_ops;
  transport.context = &psp_adhoc;
  return transport;
}

int psp_adhoc_wlan_enabled(void)
{
  return sceWlanGetSwitchState() != 0;
}

PspAdhocState psp_adhoc_state(void)
{
  return psp_adhoc.state;
}

int psp_adhoc_last_error(void)
{
  if(!psp_adhoc.failure_valid)
    return 0;
  psp_adhoc_memory_barrier();
  return psp_adhoc.failure_error;
}

PspAdhocPhase psp_adhoc_failure_phase(void)
{
  psp_adhoc_memory_barrier();
  return psp_adhoc.failure_phase;
}

const char *psp_adhoc_phase_code(PspAdhocPhase phase)
{
  switch(phase)
  {
    case PSP_ADHOC_PHASE_WLAN:          return "WLAN";
    case PSP_ADHOC_PHASE_THREAD:        return "THREAD";
    case PSP_ADHOC_PHASE_LOAD_COMMON:   return "LOAD COMMON";
    case PSP_ADHOC_PHASE_LOAD_ADHOC:    return "LOAD ADHOC";
    case PSP_ADHOC_PHASE_NET_INIT:      return "NET INIT";
    case PSP_ADHOC_PHASE_ADHOC_INIT:    return "ADHOC INIT";
    case PSP_ADHOC_PHASE_CONTROL_INIT:  return "CTL INIT";
    case PSP_ADHOC_PHASE_HOST_CREATE:   return "HOST CREATE";
    case PSP_ADHOC_PHASE_CONTROL_WAIT:  return "CTL WAIT";
    case PSP_ADHOC_PHASE_MAC_ADDRESS:   return "MAC";
    case PSP_ADHOC_PHASE_SCAN_START:    return "SCAN";
    case PSP_ADHOC_PHASE_SCAN_WAIT:     return "SCAN WAIT";
    case PSP_ADHOC_PHASE_SCAN_RESULTS:  return "SCAN RESULTS";
    case PSP_ADHOC_PHASE_JOIN_GROUP:    return "JOIN";
    case PSP_ADHOC_PHASE_PEER_LIST:     return "PEER LIST";
    case PSP_ADHOC_PHASE_PDP_CREATE:    return "PDP CREATE";
    case PSP_ADHOC_PHASE_PDP_HANDSHAKE: return "PDP CONNECT";
    case PSP_ADHOC_PHASE_PDP_SEND:      return "PDP SEND";
    case PSP_ADHOC_PHASE_PDP_RECEIVE:   return "PDP RECEIVE";
    case PSP_ADHOC_PHASE_TRANSPORT_RECOVER:return "PDP RESYNC";
    case PSP_ADHOC_PHASE_GROUP_RECOVER: return "GROUP RECOVER";
    case PSP_ADHOC_PHASE_STACK_RECOVER: return "STACK RECOVER";
    case PSP_ADHOC_PHASE_RECOVERY_FAILED:return "RECOVERY FAILED";
    case PSP_ADHOC_PHASE_HEALTH_CHECK:  return "HEALTH";
    case PSP_ADHOC_PHASE_SHUTDOWN:      return "SHUTDOWN";
    case PSP_ADHOC_PHASE_NONE:
    default:
      return "NETWORK";
  }
}

unsigned int psp_adhoc_failure_free_memory(void)
{
  psp_adhoc_memory_barrier();
  return psp_adhoc.failure_free_memory;
}

void psp_adhoc_set_transition_phase(PspAdhocTransitionPhase phase)
{
  psp_adhoc_transition_phase = phase;
  psp_adhoc_memory_barrier();
}

void psp_adhoc_get_failure_snapshot(PspAdhocFailureSnapshot *snapshot)
{
  if(snapshot == NULL)
    return;

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->control_state = -1;
  snapshot->socket_present = -1;
  if(!psp_adhoc.failure_valid)
  {
    psp_adhoc_memory_barrier();
    snapshot->cpu_clock = (unsigned int)scePowerGetCpuClockFrequencyInt();
    snapshot->bus_clock = (unsigned int)scePowerGetBusClockFrequencyInt();
    snapshot->backpressure_events = psp_adhoc.backpressure_events;
    snapshot->tx_datagrams = psp_adhoc.tx_datagrams;
    snapshot->rx_datagrams = psp_adhoc.rx_datagrams;
    snapshot->retransmissions =
     psp_adhoc.transport.lanes.retransmissions;
    snapshot->duplicates = psp_adhoc.transport.lanes.duplicates;
    snapshot->out_of_window = psp_adhoc.transport.lanes.stale;
    snapshot->invalid_packets =
     psp_adhoc.transport.lanes.invalid_packets;
    snapshot->ack_packets = psp_adhoc.transport.lanes.received_acks;
    snapshot->probes = psp_adhoc.transport.lanes.probes;
    snapshot->rto_us = (unsigned int)psp_adhoc.transport.lanes.rto_us;
    snapshot->rtt_estimate_us =
     psp_adhoc.transport.lanes.smoothed_rtt_us > UINT_MAX ? UINT_MAX :
     (unsigned int)psp_adhoc.transport.lanes.smoothed_rtt_us;
    snapshot->queue_sojourn_high_water_us =
     psp_adhoc.transport.lanes.sojourn_high_water_us > UINT_MAX ? UINT_MAX :
     (unsigned int)psp_adhoc.transport.lanes.sojourn_high_water_us;
    snapshot->api_retries = psp_adhoc.api_retries;
    snapshot->sent_bytes = psp_adhoc.sent_bytes;
    psp_adhoc_snapshot_backpressure(&psp_adhoc, snapshot,
     psp_adhoc_now());
    psp_adhoc_snapshot_tx_base(&psp_adhoc, snapshot, psp_adhoc_now());
    psp_adhoc_snapshot_datagram(&psp_adhoc, snapshot, psp_adhoc_now());
    snapshot->outstanding_packets = link_transport_lanes_critical_outstanding(
     &psp_adhoc.transport.lanes);
    psp_adhoc_snapshot_scheduler(&psp_adhoc, snapshot);
    psp_adhoc_snapshot_worker(&psp_adhoc, snapshot);
    psp_adhoc_snapshot_attribution(&psp_adhoc, snapshot);
    snapshot->transport_resyncs =
     psp_adhoc.recovery.total_transport_resyncs;
    snapshot->group_recoveries =
     psp_adhoc.recovery.total_group_rebuilds;
    snapshot->stack_recoveries =
     psp_adhoc.recovery.total_stack_restarts;
    snapshot->recovery_state =
     (unsigned int)psp_adhoc.recovery.state;
    snapshot->resync_state = psp_adhoc.resync_state;
    snapshot->transport_epoch = psp_adhoc.transport.lanes.epoch;
    snapshot->last_received_sequence =
     psp_adhoc.transport.lanes.last_rx_critical;
    snapshot->peer_activity = psp_adhoc.activity_generation;
    snapshot->consumed_activity = psp_adhoc.activity_consumed;
    snapshot->transition_phase = psp_adhoc_transition_phase;
    snapshot->shutdown_phase = lifecycle_teardown_phase(&psp_adhoc.teardown);
    snapshot->cancellation_token = psp_adhoc.teardown.cancellation_token;
    return;
  }
  psp_adhoc_memory_barrier();
  *snapshot = psp_adhoc.failure_snapshot;
  snapshot->shutdown_phase = lifecycle_teardown_phase(&psp_adhoc.teardown);
  snapshot->cancellation_token = psp_adhoc.teardown.cancellation_token;
}
