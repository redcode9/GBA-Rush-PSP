#include <stdint.h>
#include <string.h>

#include "link_transport_scheduler.h"

static unsigned int ready_slots(const LinkReplaceableMailbox *mailbox)
{
  unsigned int index;
  unsigned int ready = 0;

  for(index = 0; index < LINK_REPLACEABLE_MAILBOX_SLOTS; index++)
  {
    uint32_t state = __atomic_load_n(&mailbox->slots[index].state,
     __ATOMIC_ACQUIRE);

    __CPROVER_assert(state <= LINK_MAILBOX_SLOT_READING,
     "mailbox ownership state stays in range");
    ready += state == LINK_MAILBOX_SLOT_READY;
  }
  return ready;
}

int main(void)
{
  LinkTransportScheduler scheduler;
  LinkTransportScheduler peer;
  LinkLanesPacket packet;
  LinkLanesPacket inbound;
  LinkSpscItem item;
  uint8_t latest[2] = { 0x22u, 0x44u };
  uint8_t older[2] = { 0x11u, 0x33u };
  uint8_t resume[LINK_PROTOCOL_HEADER_SIZE];
  uint8_t changed[LINK_PROTOCOL_HEADER_SIZE];
  uint8_t other_type[LINK_PROTOCOL_HEADER_SIZE];
  uint8_t critical;
  unsigned int index;

  link_transport_scheduler_init(&scheduler);
  link_transport_scheduler_reset(&scheduler, 9u, 1u);
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_SIO, 1u, older, sizeof(older)),
   "first replaceable publication is admitted");
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_SIO, 2u, latest, sizeof(latest)),
   "newest replaceable publication replaces unread state");
  __CPROVER_assert(ready_slots(
   &scheduler.replaceable[LINK_STATE_SIO]) == 1u,
   "a replaceable class exposes at most one ready payload");
  __CPROVER_assert(link_transport_scheduler_replaceable_published(
   &scheduler, LINK_STATE_SIO) == 2u,
   "replaceable publication accounting is exact");
  __CPROVER_assert(link_transport_scheduler_replaceable_class_coalesced(
   &scheduler, LINK_STATE_SIO) == 1u,
   "unread replacement is counted once");
  __CPROVER_assert(link_transport_scheduler_prepare(&scheduler, 3u,
   &packet) == 1, "newest replaceable payload reaches the lane");
  __CPROVER_assert(packet.kind == LINK_LANES_TX_STATE &&
   packet.logical_id.epoch == 1u &&
   memcmp(packet.data + LINK_LANES_HEADER_SIZE, latest,
    sizeof(latest)) == 0,
   "latest-wins publication preserves the final payload and epoch");

  link_transport_scheduler_reset(&scheduler, 9u, 2u);
  scheduler.replaceable[LINK_STATE_SIO].slots[0].item.logical_id.epoch = 1u;
  scheduler.replaceable[LINK_STATE_SIO].slots[0].item.logical_id.message = 7u;
  scheduler.replaceable[LINK_STATE_SIO].slots[0].item.lane =
   LINK_LANE_REPLACEABLE;
  scheduler.replaceable[LINK_STATE_SIO].slots[0].item.state_class =
   LINK_STATE_SIO;
  scheduler.replaceable[LINK_STATE_SIO].slots[0].item.size = 1u;
  scheduler.replaceable[LINK_STATE_SIO].slots[0].item.data[0] = 0x55u;
  __atomic_store_n(
   &scheduler.replaceable[LINK_STATE_SIO].slots[0].state,
   LINK_MAILBOX_SLOT_READY, __ATOMIC_RELEASE);
  __CPROVER_assert(link_transport_scheduler_prepare(&scheduler, 4u,
   &packet) == 0, "a late writer from the old epoch is discarded");
  __CPROVER_assert(link_transport_scheduler_depth(&scheduler) == 0u,
   "stale epoch work cannot remain queued");

  memset(resume, 0, sizeof(resume));
  resume[6] = LINK_MESSAGE_RESUME;
  resume[sizeof(resume) - 1u] = 0x31u;
  memcpy(changed, resume, sizeof(changed));
  changed[sizeof(changed) - 1u] = 0x32u;
  memcpy(other_type, resume, sizeof(other_type));
  other_type[6] = LINK_MESSAGE_RESUME_ACK;
  link_transport_scheduler_reset(&scheduler, 9u, 3u);
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_CRITICAL, LINK_STATE_NONE, 5u, resume, sizeof(resume)),
   "first recovery control is admitted");
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_CRITICAL, LINK_STATE_NONE, 6u, resume, sizeof(resume)),
   "an identical pending recovery retry is a successful no-op");
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_CRITICAL, LINK_STATE_NONE, 7u, changed, sizeof(changed)),
   "a changed recovery payload remains distinct");
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_CRITICAL, LINK_STATE_NONE, 8u, other_type,
   sizeof(other_type)), "a changed recovery type remains distinct");
  __CPROVER_assert(link_spsc_ring_depth(&scheduler.handoff) == 3u &&
   link_transport_scheduler_critical_retry_suppressed(&scheduler) == 1u,
   "retry suppression allocates no extra critical entry");
  __CPROVER_assert(link_spsc_ring_pop(&scheduler.handoff, &item) &&
   memcmp(item.data, resume, sizeof(resume)) == 0,
   "the first distinct critical item stays first");
  __CPROVER_assert(link_spsc_ring_pop(&scheduler.handoff, &item) &&
   memcmp(item.data, changed, sizeof(changed)) == 0,
   "changed critical payload retains FIFO order");
  __CPROVER_assert(link_spsc_ring_pop(&scheduler.handoff, &item) &&
   memcmp(item.data, other_type, sizeof(other_type)) == 0,
   "changed critical type retains FIFO order");
  __CPROVER_assert(link_spsc_ring_depth(&scheduler.handoff) == 0u,
   "a finite critical FIFO drains completely in order");

  link_transport_scheduler_reset(&scheduler, 9u, 4u);
  for(index = 0; index < LINK_SPSC_CAPACITY; index++)
  {
    critical = (uint8_t)index;
    __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
     LINK_LANE_CRITICAL, LINK_STATE_NONE, index, &critical,
     sizeof(critical)), "distinct critical capacity remains available");
  }
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_CLOCK, 20u, latest, sizeof(latest)),
   "replaceable traffic bypasses a full critical handoff");
  __CPROVER_assert(link_spsc_ring_depth(&scheduler.handoff) ==
   LINK_SPSC_CAPACITY &&
   link_transport_scheduler_replaceable_class_depth(&scheduler,
    LINK_STATE_CLOCK) == 1u,
   "critical and replaceable ingress stay independently bounded");

  link_transport_scheduler_reset(&scheduler, 9u, 5u);
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_CLOCK, 30u, latest, sizeof(latest)),
   "clock state is admitted before transport pressure");
  __CPROVER_assert(link_transport_scheduler_prepare(&scheduler, 30u,
   &packet) == 1 && packet.state_class == LINK_STATE_CLOCK,
   "clock state is initially eligible");
  __CPROVER_assert(link_transport_scheduler_note_blocked(&scheduler,
   &packet, 30u), "blocked clock remains pending");
  __CPROVER_assert(scheduler.lanes.backpressure_active &&
   scheduler.lanes.backpressure_started_us == 30u &&
   scheduler.lanes.backpressure_streak == 1u,
   "first blocked send starts one bounded episode");
  __CPROVER_assert(scheduler.lanes.clock_backoff_us ==
   LINK_LANES_CLOCK_BACKOFF_MIN_US &&
   scheduler.lanes.clock_not_before_us ==
    30u + LINK_LANES_CLOCK_BACKOFF_MIN_US,
   "first pressure result establishes the minimum clock backoff");
  __CPROVER_assert(!link_transport_lanes_backpressure_stalled(
   &scheduler.lanes, 30u + 1999999u, 2000000u) &&
   link_transport_lanes_backpressure_stalled(&scheduler.lanes,
    30u + 2000000u, 2000000u),
   "backpressure stall threshold has an exact boundary");
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_REPLACEABLE, LINK_STATE_SIO, 31u, older, sizeof(older)),
   "SIO remains admissible while clock is deferred");
  __CPROVER_assert(link_transport_scheduler_prepare(&scheduler,
   30u + LINK_LANES_BACKPRESSURE_RETRY_US, &packet) == 1 &&
   packet.state_class == LINK_STATE_SIO,
   "deferred clock cannot block an eligible SIO state");
  __CPROVER_assert(link_transport_scheduler_note_sent(&scheduler, &packet,
   30u + LINK_LANES_BACKPRESSURE_RETRY_US),
   "successful SIO send completes normally");
  __CPROVER_assert(!scheduler.lanes.backpressure_active &&
   scheduler.lanes.backpressure_streak == 0u &&
   scheduler.lanes.backpressure_max_age_us ==
    LINK_LANES_BACKPRESSURE_RETRY_US,
   "successful send ends the episode but preserves its maximum age");

  link_transport_scheduler_init(&peer);
  link_transport_scheduler_reset(&scheduler, 9u, 6u);
  link_transport_scheduler_reset(&peer, 9u, 6u);
  __CPROVER_assert(link_transport_scheduler_set_profile(&scheduler,
    LINK_TRANSPORT_PROFILE_HOT, 500u) &&
   link_transport_scheduler_set_profile(&peer,
    LINK_TRANSPORT_PROFILE_HOT, 500u),
   "both directions enter the bounded hot profile");
  critical = 0xa1u;
  __CPROVER_assert(link_transport_scheduler_queue(&peer,
   LINK_LANE_CRITICAL, LINK_STATE_NONE, 100u, &critical,
   sizeof(critical)), "peer hot critical is admitted");
  __CPROVER_assert(link_transport_scheduler_prepare(&peer, 100u,
   &inbound) == 1 &&
   link_transport_scheduler_note_sent(&peer, &inbound, 100u),
   "peer hot critical is sent");
  __CPROVER_assert(link_transport_scheduler_receive(&scheduler,
   inbound.data, inbound.size, 101u, &item) == LINK_LANES_DELIVERED,
   "hot critical schedules a bounded delayed ack");
  critical = 0xb2u;
  __CPROVER_assert(link_transport_scheduler_queue(&scheduler,
   LINK_LANE_CRITICAL, LINK_STATE_NONE, 102u, &critical,
   sizeof(critical)), "ready hot reply is admitted");
  __CPROVER_assert(link_transport_scheduler_prepare(&scheduler, 102u,
   &packet) == 1 && packet.kind == LINK_LANES_TX_CRITICAL &&
   packet.acknowledged_message == inbound.logical_id.message,
   "ready hot reply overtakes and piggybacks the pure ack");
  __CPROVER_assert(link_transport_scheduler_note_sent(&scheduler,
   &packet, 102u) && !scheduler.lanes.ack_pending,
   "piggybacking retires the pending pure ack");
  __CPROVER_assert(scheduler.lanes.rto_us >=
    LINK_LANES_HOT_RTO_MIN_US && scheduler.lanes.rto_us <=
    LINK_LANES_HOT_RTO_MAX_US,
   "hot retransmission timeout remains bounded");

  return 0;
}
