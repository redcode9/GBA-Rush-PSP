#include <stdio.h>
#include <string.h>

#include "link_tx_queue.h"

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

static void test_critical_fifo_precedes_replaceable_state(void)
{
  LinkTxQueue queue;
  LinkTxItem item;
  const uint8_t state[] = { 1u };
  const uint8_t first[] = { 2u };
  const uint8_t second[] = { 3u };

  link_tx_queue_init(&queue);
  CHECK(link_tx_queue_push(&queue, LINK_TX_SIO_STATE, state,
   sizeof(state)));
  CHECK(link_tx_queue_push(&queue, LINK_TX_CRITICAL, first,
   sizeof(first)));
  CHECK(link_tx_queue_push(&queue, LINK_TX_CRITICAL, second,
   sizeof(second)));
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.data[0] == 2u);
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.data[0] == 3u);
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.data[0] == 1u);
  CHECK(!link_tx_queue_pop(&queue, &item));
}

static void test_replaceable_messages_keep_latest_value(void)
{
  LinkTxQueue queue;
  LinkTxItem item;
  const uint8_t old_state[] = { 1u, 2u };
  const uint8_t new_state[] = { 3u, 4u };
  const uint8_t heartbeat[] = { 5u };

  link_tx_queue_init(&queue);
  CHECK(link_tx_queue_push(&queue, LINK_TX_SIO_STATE, old_state,
   sizeof(old_state)));
  CHECK(link_tx_queue_push(&queue, LINK_TX_SIO_STATE, new_state,
   sizeof(new_state)));
  CHECK(link_tx_queue_push(&queue, LINK_TX_HEARTBEAT, heartbeat,
   sizeof(heartbeat)));
  CHECK(queue.coalesced == 1u);
  CHECK(link_tx_queue_depth(&queue) == 2u);
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.size == sizeof(new_state));
  CHECK(memcmp(item.data, new_state, sizeof(new_state)) == 0);
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.data[0] == 5u);
}

static void test_full_critical_queue_does_not_drop_existing_work(void)
{
  LinkTxQueue queue;
  LinkTxItem item;
  uint8_t value;
  unsigned int index;

  link_tx_queue_init(&queue);
  for(index = 0; index < LINK_TX_CRITICAL_CAPACITY; index++)
  {
    value = (uint8_t)index;
    CHECK(link_tx_queue_push(&queue, LINK_TX_CRITICAL, &value, 1u));
  }
  value = 0xffu;
  CHECK(!link_tx_queue_push(&queue, LINK_TX_CRITICAL, &value, 1u));
  CHECK(queue.rejected == 1u);
  CHECK(link_tx_queue_push(&queue, LINK_TX_SIO_STATE, &value, 1u));
  CHECK(link_tx_queue_depth(&queue) == LINK_TX_CRITICAL_CAPACITY + 1u);
  for(index = 0; index < LINK_TX_CRITICAL_CAPACITY; index++)
  {
    CHECK(link_tx_queue_pop(&queue, &item));
    CHECK(item.data[0] == (uint8_t)index);
  }
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.data[0] == 0xffu);
}

static void test_identical_critical_replay_is_coalesced(void)
{
  LinkTxQueue queue;
  LinkTxItem item;
  const uint8_t replay[] = { 1u, 2u, 3u, 4u };

  link_tx_queue_init(&queue);
  CHECK(link_tx_queue_push(&queue, LINK_TX_CRITICAL, replay,
   sizeof(replay)));
  CHECK(link_tx_queue_push(&queue, LINK_TX_CRITICAL, replay,
   sizeof(replay)));
  CHECK(queue.coalesced == 1u);
  CHECK(queue.rejected == 0u);
  CHECK(link_tx_queue_depth(&queue) == 1u);
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.size == sizeof(replay));
  CHECK(memcmp(item.data, replay, sizeof(replay)) == 0);
  CHECK(!link_tx_queue_pop(&queue, &item));
}

static void test_replaceable_round_robin_prevents_starvation(void)
{
  LinkTxQueue queue;
  LinkTxItem item;
  const uint8_t state[] = { 1u };
  const uint8_t heartbeat[] = { 2u };

  link_tx_queue_init(&queue);
  CHECK(link_tx_queue_push(&queue, LINK_TX_SIO_STATE, state, 1u));
  CHECK(link_tx_queue_push(&queue, LINK_TX_HEARTBEAT, heartbeat, 1u));
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.data[0] == 1u);
  CHECK(link_tx_queue_push(&queue, LINK_TX_SIO_STATE, state, 1u));
  CHECK(link_tx_queue_pop(&queue, &item));
  CHECK(item.data[0] == 2u);
}

static void test_queue_waits_for_reliable_ack(void)
{
  LinkTxQueue queue;
  LinkTxItem item;
  const uint8_t clock[] = { 1u };
  const uint8_t critical[] = { 2u };

  link_tx_queue_init(&queue);
  CHECK(link_tx_queue_push(&queue, LINK_TX_CLOCK, clock, sizeof(clock)));
  CHECK(!link_tx_queue_pop_ready(&queue, 1u, &item));
  CHECK(link_tx_queue_depth(&queue) == 1u);

  CHECK(link_tx_queue_push(&queue, LINK_TX_CRITICAL, critical,
   sizeof(critical)));
  CHECK(!link_tx_queue_pop_ready(&queue, 1u, &item));
  CHECK(link_tx_queue_pop_ready(&queue, 0u, &item));
  CHECK(item.data[0] == 2u);
  CHECK(link_tx_queue_pop_ready(&queue, 0u, &item));
  CHECK(item.data[0] == 1u);
}

int main(void)
{
  test_critical_fifo_precedes_replaceable_state();
  test_replaceable_messages_keep_latest_value();
  test_full_critical_queue_does_not_drop_existing_work();
  test_identical_critical_replay_is_coalesced();
  test_replaceable_round_robin_prevents_starvation();
  test_queue_waits_for_reliable_ack();

  if(failures != 0)
  {
    fprintf(stderr, "%d link transmit queue test(s) failed\n", failures);
    return 1;
  }
  puts("link transmit queue tests passed");
  return 0;
}
