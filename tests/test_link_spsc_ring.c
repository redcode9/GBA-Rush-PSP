#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_spsc_ring.h"

#define STRESS_ITEMS 200000u

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
  LinkSpscRing ring;
  volatile int producer_done;
  volatile int stress_failed;
} StressContext;

static void test_input_and_lane_validation(void)
{
  LinkSpscRing ring;
  LinkLogicalId id = { 1u, 2u };
  const uint8_t value = 7u;

  link_spsc_ring_init(&ring);
  CHECK(!link_spsc_ring_push(NULL, LINK_LANE_CRITICAL, LINK_STATE_NONE,
   id, 0, &value, 1u));
  CHECK(!link_spsc_ring_push(&ring, LINK_LANE_REPLACEABLE,
   LINK_STATE_NONE, id, 0, &value, 1u));
  CHECK(!link_spsc_ring_push(&ring, LINK_LANE_CRITICAL,
   LINK_STATE_CLOCK, id, 0, &value, 1u));
  CHECK(link_spsc_ring_depth(&ring) == 0u);
}

static void test_fifo_wrap_and_full_boundary(void)
{
  LinkSpscRing ring;
  LinkSpscItem item;
  LinkLogicalId id = { 3u, 0u };
  uint8_t value;
  unsigned int index;

  link_spsc_ring_init(&ring);
  ring.read_index = UINT32_MAX - 3u;
  ring.write_index = UINT32_MAX - 3u;
  for(index = 0; index < LINK_SPSC_CAPACITY; index++)
  {
    value = (uint8_t)index;
    id.message = index + 1u;
    CHECK(link_spsc_ring_push(&ring, LINK_LANE_CRITICAL,
     LINK_STATE_NONE, id, 100u + index, &value, 1u));
  }
  value = 0xffu;
  CHECK(!link_spsc_ring_push(&ring, LINK_LANE_CRITICAL,
   LINK_STATE_NONE, id, 200u, &value, 1u));
  CHECK(ring.rejected == 1u);
  CHECK(ring.high_water == LINK_SPSC_CAPACITY);

  for(index = 0; index < LINK_SPSC_CAPACITY; index++)
  {
    CHECK(link_spsc_ring_pop(&ring, &item));
    CHECK(item.data[0] == (uint8_t)index);
    CHECK(item.logical_id.epoch == 3u);
    CHECK(item.logical_id.message == index + 1u);
    CHECK(item.enqueued_us == 100u + index);
  }
  CHECK(!link_spsc_ring_pop(&ring, &item));
}

static void test_replaceable_metadata_is_preserved(void)
{
  LinkSpscRing ring;
  LinkSpscItem item;
  LinkLogicalId id = { 9u, 17u };
  const uint8_t payload[] = { 1u, 3u, 5u, 7u };

  link_spsc_ring_init(&ring);
  CHECK(link_spsc_ring_push(&ring, LINK_LANE_REPLACEABLE,
   LINK_STATE_CLOCK, id, UINT64_C(123456789), payload, sizeof(payload)));
  CHECK(link_spsc_ring_pop(&ring, &item));
  CHECK(item.lane == LINK_LANE_REPLACEABLE);
  CHECK(item.state_class == LINK_STATE_CLOCK);
  CHECK(item.logical_id.epoch == id.epoch);
  CHECK(item.logical_id.message == id.message);
  CHECK(item.enqueued_us == UINT64_C(123456789));
  CHECK(item.size == sizeof(payload));
  CHECK(memcmp(item.data, payload, sizeof(payload)) == 0);
}

static void *stress_producer(void *argument)
{
  StressContext *context = argument;
  LinkLogicalId id = { 11u, 0u };
  uint32_t payload[2];
  uint32_t sequence;

  for(sequence = 1; sequence <= STRESS_ITEMS; sequence++)
  {
    payload[0] = sequence;
    payload[1] = ~sequence;
    id.message = sequence;
    while(!link_spsc_ring_push(&context->ring, LINK_LANE_CRITICAL,
          LINK_STATE_NONE, id, sequence, (const uint8_t *)payload,
          sizeof(payload)))
    {
    }
  }
  __atomic_store_n(&context->producer_done, 1, __ATOMIC_RELEASE);
  return NULL;
}

static void *stress_consumer(void *argument)
{
  StressContext *context = argument;
  LinkSpscItem item;
  uint32_t payload[2];
  uint32_t expected = 1u;

  while(expected <= STRESS_ITEMS)
  {
    if(!link_spsc_ring_pop(&context->ring, &item))
      continue;
    memcpy(payload, item.data, sizeof(payload));
    if(item.logical_id.epoch != 11u ||
       item.logical_id.message != expected ||
       item.enqueued_us != expected || item.size != sizeof(payload) ||
       payload[0] != expected || payload[1] != ~expected)
    {
      __atomic_store_n(&context->stress_failed, 1, __ATOMIC_RELEASE);
      return NULL;
    }
    expected++;
  }
  return NULL;
}

static void test_concurrent_acquire_release_handoff(void)
{
  StressContext context;
  pthread_t producer;
  pthread_t consumer;

  memset(&context, 0, sizeof(context));
  link_spsc_ring_init(&context.ring);
  CHECK(pthread_create(&producer, NULL, stress_producer, &context) == 0);
  CHECK(pthread_create(&consumer, NULL, stress_consumer, &context) == 0);
  CHECK(pthread_join(producer, NULL) == 0);
  CHECK(pthread_join(consumer, NULL) == 0);
  CHECK(__atomic_load_n(&context.producer_done, __ATOMIC_ACQUIRE) == 1);
  CHECK(__atomic_load_n(&context.stress_failed, __ATOMIC_ACQUIRE) == 0);
  CHECK(link_spsc_ring_depth(&context.ring) == 0u);
}

int main(void)
{
  test_input_and_lane_validation();
  test_fifo_wrap_and_full_boundary();
  test_replaceable_metadata_is_preserved();
  test_concurrent_acquire_release_handoff();

  if(failures != 0)
  {
    fprintf(stderr, "%d link SPSC ring test(s) failed\n", failures);
    return 1;
  }
  puts("link SPSC ring tests passed");
  return 0;
}
