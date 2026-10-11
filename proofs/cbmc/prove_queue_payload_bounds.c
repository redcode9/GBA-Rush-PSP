#include <stdint.h>

#include "link_spsc_ring.h"
#include "link_tx_queue.h"

extern size_t nondet_size(void);

int main(void)
{
  uint8_t data[LINK_PROTOCOL_MAX_PACKET];
  LinkLogicalId logical_id = { 1u, 1u };
  LinkSpscRing spsc;
  LinkTxQueue tx_queue;
  int expected;
  int spsc_result;
  int tx_result;
  size_t size = nondet_size();

  __CPROVER_assume(size <= LINK_PROTOCOL_MAX_PACKET + 1u);
  expected = size >= 1u && size <= LINK_PROTOCOL_MAX_PACKET;

  link_tx_queue_init(&tx_queue);
  tx_result = link_tx_queue_push(&tx_queue, LINK_TX_CRITICAL, data, size);
  __CPROVER_assert(tx_result == expected,
   "transmit queue accepts exactly the declared payload bound");
  __CPROVER_assert(tx_queue.critical_count == (unsigned int)expected,
   "transmit queue publishes only a fully bounded payload");

  link_spsc_ring_init(&spsc);
  spsc_result = link_spsc_ring_push(&spsc, LINK_LANE_IMMEDIATE,
   LINK_STATE_NONE, logical_id, 0u, data, size);
  __CPROVER_assert(spsc_result == expected,
   "SPSC queue accepts exactly the declared payload bound");
  __CPROVER_assert(link_spsc_ring_depth(&spsc) == (unsigned int)expected,
   "SPSC queue publishes only a fully bounded payload");
  return 0;
}
