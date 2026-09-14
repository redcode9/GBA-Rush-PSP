#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_transport_scheduler.h"

#define REPLACEABLE_STRESS_ITEMS 100000u

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

typedef struct
{
  LinkTransportScheduler scheduler;
  volatile uint32_t producer_done;
  volatile uint32_t producer_failed;
  volatile uint32_t consumer_failed;
  volatile uint32_t final_value;
} ReplaceableStress;

static void retry_packet(uint8_t *packet, size_t size, uint8_t type,
 uint8_t marker)
{
  memset(packet, marker, size);
  packet[6] = type;
}

static void test_deferred_critical_does_not_block_state(void)
{
  LinkTransportScheduler sender;
  LinkTransportScheduler receiver;
  LinkLanesPacket first;
  LinkLanesPacket state;
  LinkLanesPacket ack;
  LinkLanesPacket second;
  LinkSpscItem delivery;
  const uint8_t first_data[] = { 1u };
  const uint8_t second_data[] = { 2u };
  const uint8_t state_data[] = { 3u };

  link_transport_scheduler_init(&sender);
  link_transport_scheduler_init(&receiver);
  link_transport_scheduler_reset(&sender, 17u, 2u);
  link_transport_scheduler_reset(&receiver, 17u, 2u);
  CHECK(link_transport_scheduler_queue(&sender, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 10u, first_data, sizeof(first_data)));
  CHECK(link_transport_scheduler_queue(&sender, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 11u, second_data, sizeof(second_data)));
  CHECK(link_transport_scheduler_queue(&sender, LINK_LANE_REPLACEABLE,
   LINK_STATE_SIO, 12u, state_data, sizeof(state_data)));

  CHECK(link_transport_scheduler_prepare(&sender, 20u, &first) == 1);
  CHECK(first.kind == LINK_LANES_TX_CRITICAL);
  CHECK(link_transport_scheduler_note_sent(&sender, &first, 20u));
  CHECK(sender.critical_pending_count == 1u);

  CHECK(link_transport_scheduler_prepare(&sender, 21u, &state) == 1);
  CHECK(state.kind == LINK_LANES_TX_STATE);
  CHECK(state.data[LINK_LANES_HEADER_SIZE] == state_data[0]);
  CHECK(link_transport_scheduler_note_sent(&sender, &state, 21u));
  CHECK(link_transport_scheduler_receive(&receiver, first.data, first.size,
   30u, &delivery) == LINK_LANES_DELIVERED);
  CHECK(link_transport_scheduler_prepare(&receiver, 30u, &ack) == 1);
  CHECK(ack.kind == LINK_LANES_TX_ACK);
  CHECK(link_transport_scheduler_note_sent(&receiver, &ack, 30u));
  CHECK(link_transport_scheduler_receive(&sender, ack.data, ack.size,
   31u, &delivery) == LINK_LANES_ACKED);

  CHECK(link_transport_scheduler_prepare(&sender, 32u, &second) == 1);
  CHECK(second.kind == LINK_LANES_TX_CRITICAL);
  CHECK(second.logical_id.message != first.logical_id.message);
  CHECK(second.data[LINK_LANES_HEADER_SIZE] == second_data[0]);
}

static void test_reset_discards_old_epoch_work(void)
{
  LinkTransportScheduler scheduler;
  LinkLanesPacket packet;
  const uint8_t payload[] = { 9u };

  link_transport_scheduler_init(&scheduler);
  link_transport_scheduler_reset(&scheduler, 1u, 1u);
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 0u, payload, sizeof(payload)));
  link_transport_scheduler_reset(&scheduler, 1u, 2u);
  CHECK(link_transport_scheduler_depth(&scheduler) == 0u);
  CHECK(link_transport_scheduler_prepare(&scheduler, 1u, &packet) == 0);
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 2u, payload, sizeof(payload)));
  CHECK(link_transport_scheduler_prepare(&scheduler, 2u, &packet) == 1);
  CHECK(packet.logical_id.epoch == 2u);
}

static void test_replaceable_burst_bypasses_full_critical_handoff(void)
{
  LinkTransportScheduler scheduler;
  LinkLanesPacket packet;
  uint32_t state_value;
  uint8_t critical;
  unsigned int index;

  link_transport_scheduler_init(&scheduler);
  link_transport_scheduler_reset(&scheduler, 41u, 3u);
  for(index = 0; index < LINK_SPSC_CAPACITY; index++)
  {
    critical = (uint8_t)index;
    CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
     LINK_STATE_NONE, index, &critical, sizeof(critical)));
  }
  for(index = 1; index <= 1000u; index++)
  {
    state_value = index;
    CHECK(link_transport_scheduler_queue(&scheduler,
     LINK_LANE_REPLACEABLE, LINK_STATE_SIO, index,
     (const uint8_t *)&state_value, sizeof(state_value)));
  }

  CHECK(scheduler.handoff.rejected == 0u);
  CHECK(link_transport_scheduler_replaceable_depth(&scheduler) == 1u);
  CHECK(link_transport_scheduler_replaceable_class_depth(&scheduler,
   LINK_STATE_SIO) == 1u);
  CHECK(link_transport_scheduler_replaceable_published(&scheduler,
   LINK_STATE_SIO) == 1000u);
  CHECK(link_transport_scheduler_replaceable_class_coalesced(&scheduler,
   LINK_STATE_SIO) == 999u);
  CHECK(link_transport_scheduler_replaceable_high_water(&scheduler,
   LINK_STATE_SIO) == 1u);
  CHECK(link_transport_scheduler_replaceable_coalesced(&scheduler) == 999u);
  CHECK(link_transport_scheduler_depth(&scheduler) ==
   LINK_SPSC_CAPACITY + 1u);

  CHECK(link_transport_scheduler_prepare(&scheduler, 2000u, &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_CRITICAL);
  CHECK(link_transport_scheduler_note_sent(&scheduler, &packet, 2000u));
  CHECK(link_transport_scheduler_prepare(&scheduler, 2001u, &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_STATE);
  memcpy(&state_value, packet.data + LINK_LANES_HEADER_SIZE,
   sizeof(state_value));
  CHECK(state_value == 1000u);
}

static void test_recovery_retry_is_suppressed_until_transport_ack(void)
{
  LinkTransportScheduler sender;
  LinkTransportScheduler receiver;
  LinkLanesPacket packet;
  LinkLanesPacket ack;
  LinkSpscItem delivery;
  uint8_t resume[LINK_PROTOCOL_HEADER_SIZE +
   LINK_PROTOCOL_RESUME_PAYLOAD_SIZE];
  uint32_t first_logical;
  unsigned int index;

  retry_packet(resume, sizeof(resume), LINK_MESSAGE_RESUME, 0x5au);
  link_transport_scheduler_init(&sender);
  link_transport_scheduler_init(&receiver);
  link_transport_scheduler_reset(&sender, 55u, 7u);
  link_transport_scheduler_reset(&receiver, 55u, 7u);

  for(index = 0; index < 20u; index++)
  {
    CHECK(link_transport_scheduler_queue(&sender, LINK_LANE_CRITICAL,
     LINK_STATE_NONE, index, resume, sizeof(resume)));
  }
  CHECK(link_transport_scheduler_depth(&sender) == 1u);
  CHECK(link_transport_scheduler_critical_retry_suppressed(&sender) == 19u);

  CHECK(link_transport_scheduler_prepare(&sender, 100u, &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_CRITICAL);
  first_logical = packet.logical_id.message;
  CHECK(link_transport_scheduler_note_sent(&sender, &packet, 100u));
  CHECK(link_transport_scheduler_receive(&receiver, packet.data, packet.size,
   101u, &delivery) == LINK_LANES_DELIVERED);
  CHECK(link_transport_scheduler_prepare(&receiver, 102u, &ack) == 1);
  CHECK(ack.kind == LINK_LANES_TX_ACK);
  CHECK(link_transport_scheduler_note_sent(&receiver, &ack, 102u));
  CHECK(link_transport_scheduler_receive(&sender, ack.data, ack.size, 103u,
   &delivery) == LINK_LANES_ACKED);

  CHECK(link_transport_scheduler_queue(&sender, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 104u, resume, sizeof(resume)));
  CHECK(link_transport_scheduler_prepare(&sender, 104u, &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_CRITICAL);
  CHECK(packet.logical_id.message != first_logical);
}

static void test_changed_recovery_control_is_distinct(void)
{
  LinkTransportScheduler scheduler;
  uint8_t first[LINK_PROTOCOL_HEADER_SIZE +
   LINK_PROTOCOL_RESUME_PAYLOAD_SIZE];
  uint8_t second[LINK_PROTOCOL_HEADER_SIZE +
   LINK_PROTOCOL_RESUME_PAYLOAD_SIZE];

  retry_packet(first, sizeof(first), LINK_MESSAGE_RESUME_ACK, 0x31u);
  memcpy(second, first, sizeof(second));
  second[sizeof(second) - 1u] ^= 1u;
  link_transport_scheduler_init(&scheduler);
  link_transport_scheduler_reset(&scheduler, 70u, 4u);

  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 1u, first, sizeof(first)));
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 2u, second, sizeof(second)));
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 3u, second, sizeof(second)));
  CHECK(link_transport_scheduler_depth(&scheduler) == 2u);
  CHECK(link_transport_scheduler_critical_retry_suppressed(&scheduler) == 1u);
}

static void test_deferred_clock_does_not_block_other_state(void)
{
  LinkTransportScheduler scheduler;
  LinkLanesPacket packet;
  uint8_t old_clock = 1u;
  uint8_t new_clock = 2u;
  uint8_t sio = 3u;
  uint8_t heartbeat = 4u;

  link_transport_scheduler_init(&scheduler);
  link_transport_scheduler_reset(&scheduler, 81u, 6u);
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_REPLACEABLE,
   LINK_STATE_CLOCK, 0u, &old_clock, sizeof(old_clock)));
  CHECK(link_transport_scheduler_prepare(&scheduler, 0u, &packet) == 1);
  CHECK(packet.state_class == LINK_STATE_CLOCK);
  CHECK(link_transport_scheduler_note_blocked(&scheduler, &packet, 0u));
  CHECK(link_transport_scheduler_prepare(&scheduler,
   LINK_LANES_BACKPRESSURE_RETRY_US, &packet) == 1);
  CHECK(packet.state_class == LINK_STATE_CLOCK);
  CHECK(link_transport_scheduler_note_blocked(&scheduler, &packet,
   LINK_LANES_BACKPRESSURE_RETRY_US));

  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_REPLACEABLE,
   LINK_STATE_CLOCK, 1u, &new_clock, sizeof(new_clock)));
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_REPLACEABLE,
   LINK_STATE_SIO, 1u, &sio, sizeof(sio)));
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_REPLACEABLE,
   LINK_STATE_HEARTBEAT, 1u, &heartbeat, sizeof(heartbeat)));
  CHECK(link_transport_scheduler_prepare(&scheduler,
   2u * LINK_LANES_BACKPRESSURE_RETRY_US - 1u, &packet) == 0);

  CHECK(link_transport_scheduler_prepare(&scheduler,
   2u * LINK_LANES_BACKPRESSURE_RETRY_US, &packet) == 1);
  CHECK(packet.state_class == LINK_STATE_SIO);
  CHECK(packet.data[LINK_LANES_HEADER_SIZE] == sio);
  CHECK(link_transport_scheduler_note_sent(&scheduler, &packet,
   2u * LINK_LANES_BACKPRESSURE_RETRY_US));

  CHECK(link_transport_scheduler_prepare(&scheduler,
   2u * LINK_LANES_BACKPRESSURE_RETRY_US + 1u, &packet) == 1);
  CHECK(packet.state_class == LINK_STATE_HEARTBEAT);
  CHECK(packet.data[LINK_LANES_HEADER_SIZE] == heartbeat);
  CHECK(link_transport_scheduler_note_sent(&scheduler, &packet,
   2u * LINK_LANES_BACKPRESSURE_RETRY_US + 1u));

  CHECK(link_transport_scheduler_prepare(&scheduler,
   2u * LINK_LANES_BACKPRESSURE_RETRY_US + 2u, &packet) == 0);
  CHECK(scheduler.lanes.clock_deferred >= 1u);
  CHECK(link_transport_scheduler_prepare(&scheduler,
   3u * LINK_LANES_BACKPRESSURE_RETRY_US, &packet) == 1);
  CHECK(packet.state_class == LINK_STATE_CLOCK);
  CHECK(packet.data[LINK_LANES_HEADER_SIZE] == new_clock);
}

static void test_hot_profile_never_lets_state_overtake_ready_critical(void)
{
  LinkTransportScheduler scheduler;
  LinkLanesPacket packet;
  const uint8_t critical[] = { 0x11u };
  const uint8_t sio[] = { 0x22u };
  const uint8_t heartbeat[] = { 0x33u };
  const uint8_t clock[] = { 0x44u };

  link_transport_scheduler_init(&scheduler);
  link_transport_scheduler_reset(&scheduler, 82u, 7u);
  CHECK(link_transport_scheduler_set_profile(&scheduler,
   LINK_TRANSPORT_PROFILE_HOT, UINT32_MAX));
  CHECK(scheduler.lanes.profile == LINK_TRANSPORT_PROFILE_HOT);
  CHECK(scheduler.lanes.ack_grace_us ==
   LINK_LANES_HOT_ACK_GRACE_MAX_US);
  CHECK(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_SIO, 1u, sio, sizeof(sio)));
  CHECK(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_HEARTBEAT, 1u, heartbeat,
   sizeof(heartbeat)));
  CHECK(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_CLOCK, 1u, clock, sizeof(clock)));
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, 2u, critical, sizeof(critical)));

  CHECK(link_transport_scheduler_prepare(&scheduler, 2u, &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_CRITICAL);
  CHECK(packet.data[LINK_LANES_HEADER_SIZE] == critical[0]);
  CHECK(link_transport_scheduler_note_sent(&scheduler, &packet, 2u));
  CHECK(link_transport_scheduler_prepare(&scheduler, 3u, &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_STATE);
}

static void test_hot_no_space_retains_critical_and_yields_between_retries(void)
{
  LinkTransportScheduler scheduler;
  LinkLanesPacket packet;
  const uint8_t critical[] = { 0x55u };
  const uint8_t state[] = { 0x66u };
  uint64_t now = 0u;
  unsigned int attempt;

  link_transport_scheduler_init(&scheduler);
  link_transport_scheduler_reset(&scheduler, 83u, 8u);
  CHECK(link_transport_scheduler_set_profile(&scheduler,
   LINK_TRANSPORT_PROFILE_HOT, 500u));
  CHECK(link_transport_scheduler_queue(&scheduler, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, now, critical, sizeof(critical)));
  CHECK(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_HEARTBEAT, now, state,
   sizeof(state)));

  for(attempt = 0u; attempt < 4u; attempt++)
  {
    CHECK(link_transport_scheduler_prepare(&scheduler, now, &packet) == 1);
    CHECK(packet.kind == LINK_LANES_TX_CRITICAL);
    CHECK(link_transport_scheduler_note_blocked(&scheduler, &packet, now));
    CHECK(scheduler.lanes.critical.valid != 0);
    CHECK(link_transport_scheduler_prepare(&scheduler,
     now + LINK_LANES_BACKPRESSURE_RETRY_US - 1u, &packet) == 0);
    now += LINK_LANES_BACKPRESSURE_RETRY_US;
  }
  CHECK(scheduler.lanes.no_space == 4u);
  CHECK(scheduler.lanes.critical.attempts == 0u);
  CHECK(link_transport_scheduler_prepare(&scheduler, now, &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_CRITICAL);
  CHECK(link_transport_scheduler_note_sent(&scheduler, &packet, now));
  CHECK(link_transport_scheduler_prepare(&scheduler, now + 1u,
   &packet) == 1);
  CHECK(packet.kind == LINK_LANES_TX_STATE);
}

static void *replaceable_stress_producer(void *argument)
{
  ReplaceableStress *stress = argument;
  uint32_t payload[2];
  uint32_t sequence;

  for(sequence = 1; sequence <= REPLACEABLE_STRESS_ITEMS; sequence++)
  {
    payload[0] = sequence;
    payload[1] = ~sequence;
    if(!link_transport_scheduler_queue(&stress->scheduler,
       LINK_LANE_REPLACEABLE, LINK_STATE_SIO, sequence,
       (const uint8_t *)payload, sizeof(payload)))
    {
      __atomic_store_n(&stress->producer_failed, 1u, __ATOMIC_RELEASE);
      break;
    }
  }
  __atomic_store_n(&stress->producer_done, 1u, __ATOMIC_RELEASE);
  return NULL;
}

static void *replaceable_stress_consumer(void *argument)
{
  ReplaceableStress *stress = argument;
  LinkLanesPacket packet;
  uint32_t payload[2];
  uint32_t previous = 0;

  for(;;)
  {
    int prepared = link_transport_scheduler_prepare(&stress->scheduler,
     previous + 1u, &packet);

    if(prepared < 0)
    {
      __atomic_store_n(&stress->consumer_failed, 1u, __ATOMIC_RELEASE);
      return NULL;
    }
    if(prepared == 1)
    {
      if(packet.kind != LINK_LANES_TX_STATE ||
         packet.size != LINK_LANES_HEADER_SIZE + sizeof(payload))
      {
        __atomic_store_n(&stress->consumer_failed, 1u, __ATOMIC_RELEASE);
        return NULL;
      }
      memcpy(payload, packet.data + LINK_LANES_HEADER_SIZE,
       sizeof(payload));
      if(payload[0] <= previous || payload[1] != ~payload[0] ||
         !link_transport_scheduler_note_sent(&stress->scheduler, &packet,
          payload[0]))
      {
        __atomic_store_n(&stress->consumer_failed, 1u, __ATOMIC_RELEASE);
        return NULL;
      }
      previous = payload[0];
      __atomic_store_n(&stress->final_value, previous, __ATOMIC_RELEASE);
      continue;
    }
    if(__atomic_load_n(&stress->producer_done, __ATOMIC_ACQUIRE) != 0 &&
       link_transport_scheduler_depth(&stress->scheduler) == 0u)
      return NULL;
  }
}

static void test_replaceable_concurrent_latest_wins(void)
{
  ReplaceableStress stress;
  pthread_t producer;
  pthread_t consumer;

  memset(&stress, 0, sizeof(stress));
  link_transport_scheduler_init(&stress.scheduler);
  link_transport_scheduler_reset(&stress.scheduler, 91u, 8u);
  CHECK(pthread_create(&producer, NULL, replaceable_stress_producer,
   &stress) == 0);
  CHECK(pthread_create(&consumer, NULL, replaceable_stress_consumer,
   &stress) == 0);
  CHECK(pthread_join(producer, NULL) == 0);
  CHECK(pthread_join(consumer, NULL) == 0);
  CHECK(__atomic_load_n(&stress.producer_failed, __ATOMIC_ACQUIRE) == 0u);
  CHECK(__atomic_load_n(&stress.consumer_failed, __ATOMIC_ACQUIRE) == 0u);
  CHECK(__atomic_load_n(&stress.final_value, __ATOMIC_ACQUIRE) ==
   REPLACEABLE_STRESS_ITEMS);
}

int main(void)
{
  test_deferred_critical_does_not_block_state();
  test_reset_discards_old_epoch_work();
  test_replaceable_burst_bypasses_full_critical_handoff();
  test_recovery_retry_is_suppressed_until_transport_ack();
  test_changed_recovery_control_is_distinct();
  test_deferred_clock_does_not_block_other_state();
  test_hot_profile_never_lets_state_overtake_ready_critical();
  test_hot_no_space_retains_critical_and_yields_between_retries();
  test_replaceable_concurrent_latest_wins();

  if(failures != 0)
  {
    fprintf(stderr, "%d transport scheduler test(s) failed\n", failures);
    return 1;
  }
  puts("link transport scheduler tests passed");
  return 0;
}
