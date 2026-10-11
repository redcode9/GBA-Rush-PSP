#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "link_transport_lanes.h"

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

static LinkLogicalId logical_id(uint32_t epoch, uint32_t message)
{
  LinkLogicalId id;

  id.epoch = epoch;
  id.message = message;
  return id;
}

static void note_sent(LinkTransportLanes *lanes,
 const LinkLanesPacket *packet, uint64_t now_us)
{
  CHECK(link_transport_lanes_note_sent(lanes, packet, now_us));
}

static LinkLanesReceiveResult deliver(LinkTransportLanes *receiver,
 const LinkLanesPacket *packet, uint64_t now_us, LinkSpscItem *item)
{
  return link_transport_lanes_receive(receiver, packet->data, packet->size,
   now_us, item);
}

static void test_one_critical_record_and_stable_logical_id(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket first;
  LinkLanesPacket retry;
  LinkLanesPacket ack;
  LinkSpscItem item;
  const uint8_t payload[] = { 1u, 2u, 3u };
  const uint8_t other[] = { 4u };
  LinkLogicalId id = logical_id(7u, 41u);

  link_transport_lanes_init(&sender, 9u, 7u);
  link_transport_lanes_init(&receiver, 9u, 7u);
  CHECK(link_transport_lanes_queue_critical(&sender, id, 100u,
   payload, sizeof(payload)));
  CHECK(link_transport_lanes_queue_critical(&sender, id, 100u,
   payload, sizeof(payload)));
  CHECK(sender.coalesced == 1u);
  CHECK(!link_transport_lanes_queue_critical(&sender,
   logical_id(7u, 42u), 101u, other, sizeof(other)));
  CHECK(sender.rejected == 1u);

  CHECK(link_transport_lanes_prepare(&sender, 100u, &first));
  CHECK(first.kind == LINK_LANES_TX_CRITICAL);
  CHECK(first.logical_id.message == id.message);
  note_sent(&sender, &first, 100u);

  CHECK(link_transport_lanes_prepare(&sender,
   100u + LINK_LANES_RTO_INITIAL_US, &retry));
  CHECK(retry.kind == LINK_LANES_TX_CRITICAL);
  CHECK(retry.logical_id.message == first.logical_id.message);
  CHECK(retry.wire_packet != first.wire_packet);
  note_sent(&sender, &retry, 100u + LINK_LANES_RTO_INITIAL_US);
  CHECK(sender.retransmissions == 1u);
  CHECK(sender.probes == 1u);

  CHECK(deliver(&receiver, &retry, 300100u, &item) ==
   LINK_LANES_DELIVERED);
  CHECK(item.logical_id.message == id.message);
  CHECK(item.size == sizeof(payload));
  CHECK(memcmp(item.data, payload, sizeof(payload)) == 0);
  CHECK(link_transport_lanes_prepare(&receiver, 300100u, &ack));
  CHECK(ack.kind == LINK_LANES_TX_ACK);
  CHECK(ack.acknowledged_message == id.message);
  note_sent(&receiver, &ack, 300100u);
  CHECK(deliver(&sender, &ack, 300200u, &item) == LINK_LANES_ACKED);
  CHECK(!link_transport_lanes_critical_outstanding(&sender));
  CHECK(sender.smoothed_rtt_us == 0u);
}

static void test_lost_ack_replays_without_duplicate_delivery(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket data;
  LinkLanesPacket lost_ack;
  LinkLanesPacket retry;
  LinkLanesPacket ack;
  LinkSpscItem item;
  const uint8_t payload[] = { 8u, 9u };

  link_transport_lanes_init(&sender, 12u, 3u);
  link_transport_lanes_init(&receiver, 12u, 3u);
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(3u, 1u), 0u, payload, sizeof(payload)));
  CHECK(link_transport_lanes_prepare(&sender, 10u, &data));
  note_sent(&sender, &data, 10u);
  CHECK(deliver(&receiver, &data, 20u, &item) == LINK_LANES_DELIVERED);
  CHECK(link_transport_lanes_prepare(&receiver, 20u, &lost_ack));
  note_sent(&receiver, &lost_ack, 20u);

  CHECK(link_transport_lanes_prepare(&sender,
   10u + LINK_LANES_RTO_INITIAL_US, &retry));
  note_sent(&sender, &retry, 10u + LINK_LANES_RTO_INITIAL_US);
  CHECK(deliver(&receiver, &retry, 210000u, &item) ==
   LINK_LANES_DUPLICATE);
  CHECK(receiver.duplicates == 1u);
  CHECK(link_transport_lanes_prepare(&receiver, 210000u, &ack));
  note_sent(&receiver, &ack, 210000u);
  CHECK(deliver(&sender, &ack, 210100u, &item) == LINK_LANES_ACKED);
  CHECK(sender.ack_advances == 1u);
  CHECK(deliver(&sender, &lost_ack, 210200u, &item) == LINK_LANES_STALE);
  CHECK(sender.ack_advances == 1u);
}

static void test_replaceable_state_bypasses_waiting_critical(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket critical;
  LinkLanesPacket state;
  LinkSpscItem item;
  const uint8_t critical_payload[] = { 1u };
  const uint8_t old_clock[] = { 2u };
  const uint8_t new_clock[] = { 3u };

  link_transport_lanes_init(&sender, 4u, 2u);
  link_transport_lanes_init(&receiver, 4u, 2u);
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(2u, 10u), 0u, critical_payload,
   sizeof(critical_payload)));
  CHECK(link_transport_lanes_prepare(&sender, 0u, &critical));
  note_sent(&sender, &critical, 0u);

  CHECK(link_transport_lanes_queue_state(&sender, LINK_STATE_CLOCK,
   logical_id(2u, 11u), 1u, old_clock, sizeof(old_clock)));
  CHECK(link_transport_lanes_queue_state(&sender, LINK_STATE_CLOCK,
   logical_id(2u, 12u), 2u, new_clock, sizeof(new_clock)));
  CHECK(sender.coalesced == 1u);
  CHECK(link_transport_lanes_prepare(&sender, 2u, &state));
  CHECK(state.kind == LINK_LANES_TX_STATE);
  CHECK(state.state_class == LINK_STATE_CLOCK);
  CHECK(state.logical_id.message == 12u);
  note_sent(&sender, &state, 3u);
  CHECK(link_transport_lanes_critical_outstanding(&sender));
  CHECK(deliver(&receiver, &state, 4u, &item) == LINK_LANES_DELIVERED);
  CHECK(item.lane == LINK_LANE_REPLACEABLE);
  CHECK(item.state_class == LINK_STATE_CLOCK);
  CHECK(item.data[0] == 3u);
}

static void test_state_reorder_is_latest_wins(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket old_packet;
  LinkLanesPacket new_packet;
  LinkSpscItem item;
  const uint8_t old_value[] = { 1u };
  const uint8_t new_value[] = { 2u };

  link_transport_lanes_init(&sender, 5u, 1u);
  link_transport_lanes_init(&receiver, 5u, 1u);
  CHECK(link_transport_lanes_queue_state(&sender, LINK_STATE_SIO,
   logical_id(1u, 1u), 0u, old_value, sizeof(old_value)));
  CHECK(link_transport_lanes_prepare(&sender, 0u, &old_packet));
  note_sent(&sender, &old_packet, 0u);
  CHECK(link_transport_lanes_queue_state(&sender, LINK_STATE_SIO,
   logical_id(1u, 2u), 1u, new_value, sizeof(new_value)));
  CHECK(link_transport_lanes_prepare(&sender, 1u, &new_packet));
  note_sent(&sender, &new_packet, 1u);

  CHECK(deliver(&receiver, &new_packet, 2u, &item) ==
   LINK_LANES_DELIVERED);
  CHECK(item.data[0] == 2u);
  CHECK(deliver(&receiver, &old_packet, 3u, &item) == LINK_LANES_STALE);
  CHECK(receiver.stale == 1u);
}

static void test_backpressure_preserves_work_and_measures_sojourn(void)
{
  LinkTransportLanes sender;
  LinkLanesPacket blocked;
  LinkLanesPacket retry;
  const uint8_t payload[] = { 6u };

  link_transport_lanes_init(&sender, 6u, 4u);
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(4u, 9u), 100u, payload, sizeof(payload)));
  CHECK(link_transport_lanes_prepare(&sender, 150u, &blocked));
  CHECK(link_transport_lanes_note_blocked(&sender, &blocked, 150u));
  CHECK(sender.no_space == 1u);
  CHECK(sender.backpressure_active == 1u);
  CHECK(sender.backpressure_started_us == 150u);
  CHECK(sender.backpressure_streak == 1u);
  CHECK(sender.backpressure_streak_high_water == 1u);
  CHECK(sender.backpressure_episodes == 1u);
  CHECK(sender.last_blocked_kind == LINK_LANES_TX_CRITICAL);
  CHECK(sender.last_blocked_state_class == LINK_STATE_NONE);
  CHECK(sender.clock_backoff_us == LINK_LANES_CLOCK_BACKOFF_MIN_US);
  CHECK(sender.clock_success_streak == 0u);
  CHECK(link_transport_lanes_backpressure_age(&sender, 149u) == 0u);
  CHECK(link_transport_lanes_backpressure_age(&sender, 250u) == 100u);
  CHECK(!link_transport_lanes_backpressure_stalled(&sender,
   150u + 1999999u, 2000000u));
  CHECK(link_transport_lanes_backpressure_stalled(&sender,
   150u + 2000000u, 2000000u));
  CHECK(sender.critical.attempts == 0u);
  CHECK(sender.sojourn_high_water_us == 50u);
  CHECK(!link_transport_lanes_prepare(&sender,
   150u + LINK_LANES_BACKPRESSURE_RETRY_US - 1u, &retry));
  CHECK(link_transport_lanes_prepare(&sender,
   150u + LINK_LANES_BACKPRESSURE_RETRY_US, &retry));
  CHECK(retry.logical_id.message == blocked.logical_id.message);
  CHECK(retry.wire_packet != blocked.wire_packet);
  note_sent(&sender, &retry,
   150u + LINK_LANES_BACKPRESSURE_RETRY_US);
  CHECK(sender.critical.attempts == 1u);
  CHECK(sender.backpressure_active == 0u);
  CHECK(sender.backpressure_streak == 0u);
  CHECK(sender.backpressure_max_age_us ==
   LINK_LANES_BACKPRESSURE_RETRY_US);
  CHECK(sender.backpressure_streak_high_water == 1u);
  CHECK(sender.backpressure_episodes == 1u);
}

static void test_clock_backoff_is_bounded_and_recovers_gradually(void)
{
  LinkTransportLanes lanes;
  LinkLanesPacket packet;
  const uint8_t clock[] = { 1u };
  uint64_t now = 100u;
  unsigned int expected;
  unsigned int index;

  link_transport_lanes_init(&lanes, 31u, 4u);
  CHECK(link_transport_lanes_queue_state(&lanes, LINK_STATE_CLOCK,
   logical_id(4u, 1u), now, clock, sizeof(clock)));
  CHECK(link_transport_lanes_prepare(&lanes, now, &packet));
  CHECK(packet.state_class == LINK_STATE_CLOCK);

  expected = LINK_LANES_CLOCK_BACKOFF_MIN_US;
  for(index = 0; index < 6u; index++)
  {
    CHECK(link_transport_lanes_note_blocked(&lanes, &packet, now));
    CHECK(lanes.clock_backoff_us == expected);
    CHECK(lanes.clock_success_streak == 0u);
    CHECK(!link_transport_lanes_prepare(&lanes,
     lanes.clock_not_before_us - 1u, &packet));
    now = lanes.clock_not_before_us;
    CHECK(link_transport_lanes_prepare(&lanes, now, &packet));
    CHECK(packet.state_class == LINK_STATE_CLOCK);
    if(expected < LINK_LANES_CLOCK_BACKOFF_MAX_US)
      expected *= 2u;
    if(expected > LINK_LANES_CLOCK_BACKOFF_MAX_US)
      expected = LINK_LANES_CLOCK_BACKOFF_MAX_US;
  }
  CHECK(lanes.clock_backoff_us == LINK_LANES_CLOCK_BACKOFF_MAX_US);
  CHECK(lanes.clock_backoff_raises == 4u);
  CHECK(lanes.clock_deferred >= 5u);

  note_sent(&lanes, &packet, now);
  for(index = 1u; index < LINK_LANES_CLOCK_SUCCESS_THRESHOLD; index++)
  {
    uint8_t value = (uint8_t)(index + 1u);

    now = lanes.clock_not_before_us;
    CHECK(link_transport_lanes_queue_state(&lanes, LINK_STATE_CLOCK,
     logical_id(4u, index + 2u), now, &value, sizeof(value)));
    CHECK(link_transport_lanes_prepare(&lanes, now, &packet));
    note_sent(&lanes, &packet, now);
  }
  CHECK(lanes.clock_backoff_us == LINK_LANES_CLOCK_BACKOFF_MAX_US / 2u);
  CHECK(lanes.clock_success_streak == 0u);

  now = lanes.clock_not_before_us;
  CHECK(link_transport_lanes_queue_state(&lanes, LINK_STATE_CLOCK,
   logical_id(4u, 100u), now, clock, sizeof(clock)));
  CHECK(link_transport_lanes_prepare(&lanes, now, &packet));
  note_sent(&lanes, &packet, now);
  CHECK(lanes.clock_success_streak == 1u);
  now = lanes.clock_not_before_us;
  CHECK(link_transport_lanes_queue_state(&lanes, LINK_STATE_CLOCK,
   logical_id(4u, 101u), now, clock, sizeof(clock)));
  CHECK(link_transport_lanes_prepare(&lanes, now, &packet));
  CHECK(link_transport_lanes_note_blocked(&lanes, &packet, now));
  CHECK(lanes.clock_backoff_us == LINK_LANES_CLOCK_BACKOFF_MAX_US);
  CHECK(lanes.clock_success_streak == 0u);

  link_transport_lanes_init(&lanes, 31u, 5u);
  CHECK(lanes.clock_backoff_us == 0u);
  CHECK(lanes.clock_not_before_us == 0u);
  CHECK(lanes.backpressure_active == 0u);
  CHECK(lanes.backpressure_episodes == 0u);
}

static void test_ack_bypasses_data_backpressure_timer(void)
{
  LinkTransportLanes local;
  LinkTransportLanes peer;
  LinkLanesPacket blocked;
  LinkLanesPacket incoming;
  LinkLanesPacket ack;
  LinkSpscItem item;
  const uint8_t local_data[] = { 1u };
  const uint8_t peer_data[] = { 2u };

  link_transport_lanes_init(&local, 14u, 2u);
  link_transport_lanes_init(&peer, 14u, 2u);
  CHECK(link_transport_lanes_queue_critical(&local,
   logical_id(2u, 1u), 0u, local_data, sizeof(local_data)));
  CHECK(link_transport_lanes_prepare(&local, 100u, &blocked));
  CHECK(link_transport_lanes_note_blocked(&local, &blocked, 100u));

  CHECK(link_transport_lanes_queue_critical(&peer,
   logical_id(2u, 1u), 0u, peer_data, sizeof(peer_data)));
  CHECK(link_transport_lanes_prepare(&peer, 101u, &incoming));
  note_sent(&peer, &incoming, 101u);
  CHECK(deliver(&local, &incoming, 102u, &item) == LINK_LANES_DELIVERED);
  CHECK(link_transport_lanes_prepare(&local, 102u, &ack));
  CHECK(ack.kind == LINK_LANES_TX_ACK);
  CHECK(ack.acknowledged_message == incoming.logical_id.message);
  note_sent(&local, &ack, 102u);
  CHECK(local.backpressure_active == 0u);
  CHECK(local.backpressure_max_age_us == 2u);
}

static void test_clean_rtt_sample_and_packet_wrap(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket data;
  LinkLanesPacket ack;
  LinkLanesPacket state;
  LinkSpscItem item;
  const uint8_t payload[] = { 7u };

  link_transport_lanes_init(&sender, 8u, 5u);
  link_transport_lanes_init(&receiver, 8u, 5u);
  sender.next_wire_packet = UINT32_MAX;
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(5u, UINT32_MAX), 1000u, payload, sizeof(payload)));
  CHECK(link_transport_lanes_prepare(&sender, 1000u, &data));
  CHECK(data.wire_packet == UINT32_MAX);
  note_sent(&sender, &data, 1000u);
  CHECK(deliver(&receiver, &data, 1100u, &item) == LINK_LANES_DELIVERED);
  CHECK(link_transport_lanes_prepare(&receiver, 1100u, &ack));
  note_sent(&receiver, &ack, 1100u);
  CHECK(deliver(&sender, &ack, 1200u, &item) == LINK_LANES_ACKED);
  CHECK(sender.smoothed_rtt_us == 200u);
  CHECK(sender.rto_us >= LINK_LANES_RTO_MIN_US);

  CHECK(link_transport_lanes_queue_state(&sender, LINK_STATE_HEARTBEAT,
   logical_id(5u, 1u), 1201u, payload, sizeof(payload)));
  CHECK(link_transport_lanes_prepare(&sender, 1201u, &state));
  CHECK(state.wire_packet == 1u);
}

static void test_same_id_with_changed_payload_fails_closed(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket packet;
  LinkSpscItem item;
  const uint8_t first[] = { 1u };
  const uint8_t changed[] = { 2u };

  link_transport_lanes_init(&sender, 10u, 6u);
  link_transport_lanes_init(&receiver, 10u, 6u);
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(6u, 2u), 0u, first, sizeof(first)));
  CHECK(link_transport_lanes_prepare(&sender, 0u, &packet));
  note_sent(&sender, &packet, 0u);
  CHECK(deliver(&receiver, &packet, 1u, &item) == LINK_LANES_DELIVERED);

  link_transport_lanes_init(&sender, 10u, 6u);
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(6u, 2u), 2u, changed, sizeof(changed)));
  CHECK(link_transport_lanes_prepare(&sender, 2u, &packet));
  note_sent(&sender, &packet, 2u);
  CHECK(deliver(&receiver, &packet, 3u, &item) == LINK_LANES_CONFLICT);
  CHECK(receiver.conflicts == 1u);
}

static void test_hot_ping_pong_piggybacks_every_ready_ack(void)
{
  LinkTransportLanes host;
  LinkTransportLanes joiner;
  LinkLanesPacket start;
  LinkLanesPacket reply;
  LinkSpscItem item;
  uint8_t start_payload;
  uint8_t reply_payload;
  uint64_t now = 100u;
  unsigned int sequence;
  unsigned int datagrams = 0u;

  link_transport_lanes_init(&host, 44u, 3u);
  link_transport_lanes_init(&joiner, 44u, 3u);
  CHECK(link_transport_lanes_set_profile(&host,
   LINK_TRANSPORT_PROFILE_HOT, 500u));
  CHECK(link_transport_lanes_set_profile(&joiner,
   LINK_TRANSPORT_PROFILE_HOT, 500u));

  for(sequence = 1u; sequence <= 9u; sequence++)
  {
    start_payload = (uint8_t)sequence;
    reply_payload = (uint8_t)(0x80u | sequence);
    CHECK(link_transport_lanes_queue_critical(&host,
     logical_id(3u, sequence), now, &start_payload,
     sizeof(start_payload)));
    CHECK(link_transport_lanes_prepare(&host, now, &start));
    CHECK(start.kind == LINK_LANES_TX_CRITICAL);
    CHECK(start.acknowledged_message ==
     (sequence == 1u ? 0u : sequence - 1u));
    note_sent(&host, &start, now);
    datagrams++;
    CHECK(deliver(&joiner, &start, now + 100u, &item) ==
     LINK_LANES_DELIVERED);

    CHECK(link_transport_lanes_queue_critical(&joiner,
     logical_id(3u, sequence), now + 101u, &reply_payload,
     sizeof(reply_payload)));
    CHECK(link_transport_lanes_prepare(&joiner, now + 101u, &reply));
    CHECK(reply.kind == LINK_LANES_TX_CRITICAL);
    CHECK(reply.acknowledged_message == sequence);
    note_sent(&joiner, &reply, now + 101u);
    datagrams++;
    CHECK(deliver(&host, &reply, now + 200u, &item) ==
     LINK_LANES_DELIVERED);
    now += 1000u;
  }

  CHECK(datagrams == 18u);
  CHECK(host.ack_advances == 9u);
  CHECK(joiner.ack_advances == 8u);
  CHECK(deliver(&host, &reply, now, &item) == LINK_LANES_DUPLICATE);
  CHECK(host.ack_advances == 9u);
  CHECK(host.ack_pending != 0);
  CHECK(joiner.ack_pending == 0);
}

static void test_hot_ack_grace_is_bounded(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket data;
  LinkLanesPacket ack;
  LinkSpscItem item;
  const uint8_t payload[] = { 0x5au };

  link_transport_lanes_init(&sender, 45u, 2u);
  link_transport_lanes_init(&receiver, 45u, 2u);
  CHECK(link_transport_lanes_set_profile(&receiver,
   LINK_TRANSPORT_PROFILE_HOT, 500u));
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(2u, 1u), 0u, payload, sizeof(payload)));
  CHECK(link_transport_lanes_prepare(&sender, 0u, &data));
  note_sent(&sender, &data, 0u);
  CHECK(deliver(&receiver, &data, 100u, &item) ==
   LINK_LANES_DELIVERED);
  CHECK(!link_transport_lanes_prepare(&receiver, 599u, &ack));
  CHECK(link_transport_lanes_prepare(&receiver, 600u, &ack));
  CHECK(ack.kind == LINK_LANES_TX_ACK);
  CHECK(ack.acknowledged_message == 1u);
  note_sent(&receiver, &ack, 600u);
  CHECK(receiver.ack_pending == 0);
}

static void test_hot_and_cold_share_rtt_with_bounded_rto(void)
{
  LinkTransportLanes sender;
  LinkTransportLanes receiver;
  LinkLanesPacket data;
  LinkLanesPacket ack;
  LinkSpscItem item;
  const uint8_t payload[] = { 1u };
  uint64_t estimate;

  link_transport_lanes_init(&sender, 46u, 5u);
  link_transport_lanes_init(&receiver, 46u, 5u);
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(5u, 1u), 0u, payload, sizeof(payload)));
  CHECK(link_transport_lanes_prepare(&sender, 0u, &data));
  note_sent(&sender, &data, 0u);
  CHECK(deliver(&receiver, &data, 10000u, &item) ==
   LINK_LANES_DELIVERED);
  CHECK(link_transport_lanes_prepare(&receiver, 10000u, &ack));
  note_sent(&receiver, &ack, 10000u);
  CHECK(deliver(&sender, &ack, 20000u, &item) == LINK_LANES_ACKED);
  estimate = sender.smoothed_rtt_us;
  CHECK(estimate == 20000u);
  CHECK(sender.rto_us >= LINK_LANES_RTO_MIN_US);

  CHECK(link_transport_lanes_set_profile(&sender,
   LINK_TRANSPORT_PROFILE_HOT, 1u));
  CHECK(sender.smoothed_rtt_us == estimate);
  CHECK(sender.ack_grace_us == LINK_LANES_HOT_ACK_GRACE_MIN_US);
  CHECK(sender.rto_us >= LINK_LANES_HOT_RTO_MIN_US);
  CHECK(sender.rto_us <= LINK_LANES_HOT_RTO_MAX_US);

  CHECK(link_transport_lanes_set_profile(&sender,
   LINK_TRANSPORT_PROFILE_COLD, UINT32_MAX));
  CHECK(sender.smoothed_rtt_us == estimate);
  CHECK(sender.ack_grace_us == 0u);
  CHECK(sender.rto_us >= LINK_LANES_RTO_MIN_US);
  CHECK(sender.rto_us <= LINK_LANES_RTO_MAX_US);
}

static void test_lost_hot_critical_retries_inside_burst_budget(void)
{
  LinkTransportLanes sender;
  LinkLanesPacket first;
  LinkLanesPacket retry;
  const uint8_t payload[] = { 7u };

  link_transport_lanes_init(&sender, 47u, 6u);
  CHECK(link_transport_lanes_set_profile(&sender,
   LINK_TRANSPORT_PROFILE_HOT, 500u));
  CHECK(link_transport_lanes_queue_critical(&sender,
   logical_id(6u, 1u), 0u, payload, sizeof(payload)));
  CHECK(link_transport_lanes_prepare(&sender, 0u, &first));
  note_sent(&sender, &first, 0u);
  CHECK(sender.critical.deadline_us == LINK_LANES_HOT_RTO_INITIAL_US);
  CHECK(sender.critical.deadline_us <= LINK_LANES_HOT_RTO_MAX_US);
  CHECK(!link_transport_lanes_prepare(&sender,
   LINK_LANES_HOT_RTO_INITIAL_US - 1u, &retry));
  CHECK(link_transport_lanes_prepare(&sender,
   LINK_LANES_HOT_RTO_INITIAL_US, &retry));
  CHECK(retry.kind == LINK_LANES_TX_CRITICAL);
  CHECK(retry.logical_id.message == first.logical_id.message);
}

int main(void)
{
  test_one_critical_record_and_stable_logical_id();
  test_lost_ack_replays_without_duplicate_delivery();
  test_replaceable_state_bypasses_waiting_critical();
  test_state_reorder_is_latest_wins();
  test_backpressure_preserves_work_and_measures_sojourn();
  test_clock_backoff_is_bounded_and_recovers_gradually();
  test_ack_bypasses_data_backpressure_timer();
  test_clean_rtt_sample_and_packet_wrap();
  test_same_id_with_changed_payload_fails_closed();
  test_hot_ping_pong_piggybacks_every_ready_ack();
  test_hot_ack_grace_is_bounded();
  test_hot_and_cold_share_rtt_with_bounded_rto();
  test_lost_hot_critical_retries_inside_burst_budget();

  if(failures != 0)
  {
    fprintf(stderr, "%d transport lane test(s) failed\n", failures);
    return 1;
  }
  puts("link transport lane tests passed");
  return 0;
}
