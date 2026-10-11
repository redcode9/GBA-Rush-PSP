#include <stdint.h>

#include "link_spsc_ring.h"
#include "link_tx_queue.h"

extern unsigned int nondet_uint(void);
static int tx_queue_valid(const LinkTxQueue *queue)
{
  unsigned int index;
  unsigned int depth = queue->critical_count;

  if(queue->critical_read >= LINK_TX_CRITICAL_CAPACITY ||
     queue->critical_write >= LINK_TX_CRITICAL_CAPACITY ||
     queue->critical_count > LINK_TX_CRITICAL_CAPACITY ||
     queue->critical_write !=
      (queue->critical_read + queue->critical_count) %
       LINK_TX_CRITICAL_CAPACITY ||
     queue->replaceable_next >= LINK_TX_REPLACEABLE_COUNT)
    return 0;
  for(index = 0; index < LINK_TX_REPLACEABLE_COUNT; index++)
  {
    if(queue->replaceable_valid[index] > 1)
      return 0;
    depth += queue->replaceable_valid[index];
  }
  return depth <= LINK_TX_CRITICAL_CAPACITY +
   LINK_TX_REPLACEABLE_COUNT;
}

static int spsc_valid(const LinkSpscRing *ring)
{
  if(ring->write_index - ring->read_index > LINK_SPSC_CAPACITY ||
     ring->high_water > LINK_SPSC_CAPACITY)
    return 0;
  return 1;
}

int main(void)
{
  uint8_t data[1];
  LinkLogicalId logical_id;
  LinkSpscItem spsc_item;
  LinkSpscRing spsc;
  LinkTxItem tx_item;
  LinkTxPriority priority;
  LinkTxQueue tx_queue;
  LinkTransportLane lane;
  LinkReplaceableClass state_class;
  unsigned int action;
  unsigned int raw_lane;
  unsigned int raw_priority;
  unsigned int raw_state_class;
  const size_t size = sizeof(data);

  __CPROVER_assume(tx_queue_valid(&tx_queue));
  raw_priority = nondet_uint();
  __CPROVER_assume(raw_priority <= (unsigned int)LINK_TX_CLOCK);
  priority = (LinkTxPriority)raw_priority;
  action = nondet_uint();
  __CPROVER_assume(action <= 2u);
  if(action == 0u)
    link_tx_queue_push(&tx_queue, priority, data, size);
  else if(action == 1u)
    link_tx_queue_pop(&tx_queue, &tx_item);
  else
    link_tx_queue_pop_ready(&tx_queue, nondet_uint(), &tx_item);
  __CPROVER_assert(tx_queue_valid(&tx_queue),
   "transmit queue invariant is inductive");
  __CPROVER_assert(link_tx_queue_depth(&tx_queue) <=
   LINK_TX_CRITICAL_CAPACITY + LINK_TX_REPLACEABLE_COUNT,
   "transmit queue remains bounded");

  __CPROVER_assume(spsc_valid(&spsc));
  raw_lane = nondet_uint();
  raw_state_class = nondet_uint();
  __CPROVER_assume(raw_lane <= (unsigned int)LINK_LANE_REPLACEABLE);
  __CPROVER_assume(raw_state_class < (unsigned int)LINK_STATE_COUNT);
  lane = (LinkTransportLane)raw_lane;
  state_class = (LinkReplaceableClass)raw_state_class;
  action = nondet_uint();
  __CPROVER_assume(action <= 2u);
  if(action == 0u)
    link_spsc_ring_push(&spsc, lane, state_class, logical_id,
     nondet_uint(), data, size);
  else if(action == 1u)
    link_spsc_ring_pop(&spsc, &spsc_item);
  else
    link_spsc_ring_discard(&spsc);
  __CPROVER_assert(spsc_valid(&spsc),
   "SPSC queue invariant is inductive across index wrap");
  __CPROVER_assert(link_spsc_ring_depth(&spsc) <= LINK_SPSC_CAPACITY,
   "SPSC queue remains bounded");
  return 0;
}
