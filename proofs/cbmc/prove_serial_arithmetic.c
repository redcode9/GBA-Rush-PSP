#include <stdint.h>

#include "adhoc_datagram.h"
#include "link_journal.h"
#include "link_protocol.h"
#include "link_transport_lanes.h"

extern uint32_t nondet_uint32(void);

int main(void)
{
  uint32_t value = nondet_uint32();
  uint32_t reference = nondet_uint32();
  int protocol_after = link_protocol_sequence_after(value, reference);
  int journal_after = link_journal_serial_after(value, reference);
  int transport_after = link_transport_serial_after(value, reference);

  __CPROVER_assert(protocol_after == journal_after &&
   journal_after == transport_after,
   "all layers use identical serial-number ordering");
  __CPROVER_assert(!link_protocol_sequence_after(value, value),
   "serial ordering is irreflexive");
  if(protocol_after)
    __CPROVER_assert(!link_protocol_sequence_after(reference, value),
     "bounded serial ordering is asymmetric");
  __CPROVER_assert(adhoc_datagram_next_sequence(value) != 0,
   "wire sequence zero remains reserved across wrap");
  __CPROVER_assert(link_transport_next_number(value) != 0,
   "transport packet zero remains reserved across wrap");
  return 0;
}
