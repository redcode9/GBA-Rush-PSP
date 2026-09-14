#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "adhoc_datagram.h"

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

static int send_next(AdhocDatagram *from, AdhocDatagram *to,
 uint64_t now_us, int deliver, AdhocDatagramPacket *copy)
{
  AdhocDatagramPacket packet = { 0 };

  if(!adhoc_datagram_prepare(from, now_us, &packet))
    return 0;
  CHECK(adhoc_datagram_note_sent(from, &packet, now_us));
  if(copy != NULL)
    *copy = packet;
  if(deliver)
  {
    CHECK(adhoc_datagram_receive(to, packet.data, packet.size, now_us) >
     ADHOC_DATAGRAM_FOREIGN);
  }
  return 1;
}

static void acknowledge(AdhocDatagram *from, AdhocDatagram *to,
 uint64_t now_us)
{
  AdhocDatagramPacket packet;

  CHECK(adhoc_datagram_prepare(from, now_us, &packet));
  CHECK(packet.kind == ADHOC_DATAGRAM_TX_ACK ||
   packet.kind == ADHOC_DATAGRAM_TX_PROBE);
  CHECK(adhoc_datagram_note_sent(from, &packet, now_us));
  CHECK(adhoc_datagram_receive(to, packet.data, packet.size, now_us) ==
   ADHOC_DATAGRAM_ACCEPTED);
}

static void test_ordered_round_trip(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  uint8_t output[ADHOC_DATAGRAM_MAX_PAYLOAD];
  size_t size = 0;

  adhoc_datagram_init(&sender, 0x12345678u, 1, 0);
  adhoc_datagram_init(&receiver, 0x12345678u, 1, 0);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"one", 3));
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"two", 3));
  CHECK(send_next(&sender, &receiver, 1000, 1, NULL));
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(size == 3 && memcmp(output, "one", 3) == 0);
  acknowledge(&receiver, &sender, 6000);
  CHECK(send_next(&sender, &receiver, 9000, 1, NULL));
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(size == 3 && memcmp(output, "two", 3) == 0);
  CHECK(!adhoc_datagram_pop(&receiver, output, sizeof(output), &size));

  acknowledge(&receiver, &sender, 12000);
  CHECK(adhoc_datagram_outstanding(&sender) == 0);
  CHECK(sender.smoothed_rtt_us != 0);
}

static void test_retransmits_loss(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket packet = { 0 };
  uint8_t output[8];
  size_t size = 0;
  uint64_t retry_at;

  adhoc_datagram_init(&sender, 8, 2, 0);
  adhoc_datagram_init(&receiver, 8, 2, 0);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"lost", 4));
  CHECK(send_next(&sender, &receiver, 0, 0, &packet));
  CHECK(packet.sequence == 1);
  retry_at = sender.tx[0].deadline_us;
  CHECK(retry_at > 0u);
  CHECK(!adhoc_datagram_prepare(&sender, retry_at - 1u, &packet));
  CHECK(send_next(&sender, &receiver, retry_at, 1, &packet));
  CHECK(packet.sequence == 1);
  CHECK(sender.retransmissions == 1);
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(size == 4 && memcmp(output, "lost", 4) == 0);
  acknowledge(&receiver, &sender, retry_at + 5000u);
  CHECK(adhoc_datagram_outstanding(&sender) == 0);
}

static void test_window_sends_before_ack(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket first = { 0 };
  uint8_t output[4];
  size_t size = 0;

  adhoc_datagram_init(&sender, 9, 3, 0);
  adhoc_datagram_init(&receiver, 9, 3, 0);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"1", 1));
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"2", 1));
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"3", 1));
  CHECK(send_next(&sender, &receiver, 0, 1, &first));
  CHECK(first.sequence == 1u);
  CHECK(send_next(&sender, &receiver, 1000u, 1, &first));
  CHECK(first.sequence == 2u);
  CHECK(send_next(&sender, &receiver, 2000u, 1, &first));
  CHECK(first.sequence == 3u);
  CHECK(!adhoc_datagram_prepare(&sender, 3000u, &first));
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(size == 1 && output[0] == '1');
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(size == 1 && output[0] == '2');
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(size == 1 && output[0] == '3');
  acknowledge(&receiver, &sender, 5000u);
  CHECK(adhoc_datagram_outstanding(&sender) == 0u);
}

static void test_window_recovers_missing_base(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket packet = { 0 };
  uint8_t output;
  size_t size;
  uint64_t retry_at;

  adhoc_datagram_init(&sender, 18u, 1u, 0u);
  adhoc_datagram_init(&receiver, 18u, 1u, 0u);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"1", 1u));
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"2", 1u));
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"3", 1u));

  CHECK(send_next(&sender, &receiver, 0u, 0, &packet));
  CHECK(packet.sequence == 1u);
  retry_at = sender.tx[0].deadline_us;
  CHECK(send_next(&sender, &receiver, 1000u, 1, &packet));
  CHECK(packet.sequence == 2u);
  CHECK(send_next(&sender, &receiver, 2000u, 1, &packet));
  CHECK(packet.sequence == 3u);
  CHECK(!adhoc_datagram_pop(&receiver, &output, sizeof(output), &size));

  acknowledge(&receiver, &sender, 5000u);
  CHECK(adhoc_datagram_outstanding(&sender) == 1u);
  CHECK(sender.tx_base_sequence == 1u);
  CHECK(send_next(&sender, &receiver, retry_at, 1, &packet));
  CHECK(packet.sequence == 1u);
  CHECK(adhoc_datagram_pop(&receiver, &output, sizeof(output), &size));
  CHECK(output == '1');
  CHECK(adhoc_datagram_pop(&receiver, &output, sizeof(output), &size));
  CHECK(output == '2');
  CHECK(adhoc_datagram_pop(&receiver, &output, sizeof(output), &size));
  CHECK(output == '3');
  acknowledge(&receiver, &sender, retry_at + 5000u);
  CHECK(adhoc_datagram_outstanding(&sender) == 0u);
  CHECK(sender.tx_base_sequence == 4u);
}

static void test_backpressure_defers_retry(void)
{
  AdhocDatagram sender;
  AdhocDatagramPacket packet = { 0 };

  adhoc_datagram_init(&sender, 10u, 1u, 0u);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"x", 1u));
  CHECK(adhoc_datagram_prepare(&sender, 1000u, &packet));
  CHECK(adhoc_datagram_note_blocked(&sender, &packet, 1000u));
  CHECK(!adhoc_datagram_prepare(&sender,
   1000u + ADHOC_DATAGRAM_BACKPRESSURE_RETRY_US - 1u, &packet));
  CHECK(adhoc_datagram_prepare(&sender,
   1000u + ADHOC_DATAGRAM_BACKPRESSURE_RETRY_US, &packet));
  CHECK(adhoc_datagram_note_sent(&sender, &packet,
   1000u + ADHOC_DATAGRAM_BACKPRESSURE_RETRY_US));
}

static void test_ack_does_not_block_async_response(void)
{
  AdhocDatagram host;
  AdhocDatagram joiner;
  AdhocDatagramPacket packet = { 0 };
  uint8_t output;
  size_t size = 0;

  adhoc_datagram_init(&host, 0x10203040u, 1u, 0u);
  adhoc_datagram_init(&joiner, 0x10203040u, 1u, 0u);
  adhoc_datagram_set_role(&host, 1);
  adhoc_datagram_set_role(&joiner, 0);
  CHECK(adhoc_datagram_queue(&host, (const uint8_t *)"h", 1u));

  CHECK(send_next(&host, &joiner, 0u, 1, NULL));
  CHECK(adhoc_datagram_pop(&joiner, &output, sizeof(output), &size));
  CHECK(output == 'h');
  acknowledge(&joiner, &host, 5000u);
  CHECK(adhoc_datagram_outstanding(&host) == 0u);

  CHECK(adhoc_datagram_queue(&joiner, (const uint8_t *)"j", 1u));
  CHECK(send_next(&joiner, &host, 6000u, 1, &packet));
  CHECK(packet.kind == ADHOC_DATAGRAM_TX_DATA);
  CHECK(adhoc_datagram_pop(&host, &output, sizeof(output), &size));
  CHECK(output == 'j');

  acknowledge(&host, &joiner, 11000u);
  CHECK(adhoc_datagram_outstanding(&joiner) == 0u);
}

static void test_role_jitter_breaks_symmetric_retries(void)
{
  AdhocDatagram host;
  AdhocDatagram joiner;
  AdhocDatagramPacket packet;

  adhoc_datagram_init(&host, 0x55667788u, 1u, 0u);
  adhoc_datagram_init(&joiner, 0x55667788u, 1u, 0u);
  adhoc_datagram_set_role(&host, 1);
  adhoc_datagram_set_role(&joiner, 0);
  CHECK(adhoc_datagram_queue(&host, (const uint8_t *)"h", 1u));
  CHECK(adhoc_datagram_queue(&joiner, (const uint8_t *)"j", 1u));
  CHECK(adhoc_datagram_prepare(&host, 0u, &packet));
  CHECK(adhoc_datagram_note_sent(&host, &packet, 0u));
  CHECK(adhoc_datagram_prepare(&joiner, 0u, &packet));
  CHECK(adhoc_datagram_note_sent(&joiner, &packet, 0u));
  CHECK(host.tx[0].deadline_us != joiner.tx[0].deadline_us);
}

static void test_symmetric_ack_collision_recovers_before_data_retry(void)
{
  AdhocDatagram endpoint[2];
  AdhocDatagramPacket outbound[2];
  uint8_t output;
  size_t size;
  uint64_t now_us;
  unsigned int side;

  adhoc_datagram_init(&endpoint[0], 0x41434B53u, 1u, 0u);
  adhoc_datagram_init(&endpoint[1], 0x41434B53u, 1u, 0u);
  adhoc_datagram_set_role(&endpoint[0], 1);
  adhoc_datagram_set_role(&endpoint[1], 0);
  CHECK(adhoc_datagram_queue(&endpoint[0],
   (const uint8_t *)"h", 1u));
  CHECK(adhoc_datagram_queue(&endpoint[1],
   (const uint8_t *)"j", 1u));

  for(side = 0; side < 2u; side++)
  {
    CHECK(adhoc_datagram_prepare(&endpoint[side], 0u, &outbound[side]));
    CHECK(outbound[side].kind == ADHOC_DATAGRAM_TX_DATA);
    CHECK(adhoc_datagram_note_sent(&endpoint[side],
     &outbound[side], 0u));
  }
  CHECK(adhoc_datagram_receive(&endpoint[0], outbound[1].data,
   outbound[1].size, 0u) == ADHOC_DATAGRAM_ACCEPTED);
  CHECK(adhoc_datagram_receive(&endpoint[1], outbound[0].data,
   outbound[0].size, 0u) == ADHOC_DATAGRAM_ACCEPTED);
  for(side = 0; side < 2u; side++)
  {
    CHECK(adhoc_datagram_pop(&endpoint[side], &output,
     sizeof(output), &size));
  }

  for(now_us = 0u; now_us <= 20000u; now_us += 1000u)
  {
    int prepared[2];

    prepared[0] = adhoc_datagram_prepare(&endpoint[0], now_us,
     &outbound[0]);
    prepared[1] = adhoc_datagram_prepare(&endpoint[1], now_us,
     &outbound[1]);
    for(side = 0; side < 2u; side++)
    {
      if(prepared[side])
        CHECK(adhoc_datagram_note_sent(&endpoint[side],
         &outbound[side], now_us));
    }
    if(prepared[0] && !prepared[1])
    {
      CHECK(adhoc_datagram_receive(&endpoint[1], outbound[0].data,
       outbound[0].size, now_us) == ADHOC_DATAGRAM_ACCEPTED);
    }
    else if(prepared[1] && !prepared[0])
    {
      CHECK(adhoc_datagram_receive(&endpoint[0], outbound[1].data,
       outbound[1].size, now_us) == ADHOC_DATAGRAM_ACCEPTED);
    }
  }

  CHECK(adhoc_datagram_outstanding(&endpoint[0]) == 0u);
  CHECK(adhoc_datagram_outstanding(&endpoint[1]) == 0u);
  CHECK(endpoint[0].retransmissions == 0u);
  CHECK(endpoint[1].retransmissions == 0u);
}

static void test_duplicates_corruption_and_foreign_epoch(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket packet;

  adhoc_datagram_init(&sender, 11, 4, 0);
  adhoc_datagram_init(&receiver, 11, 4, 0);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"x", 1));
  CHECK(send_next(&sender, &receiver, 0, 1, &packet));
  CHECK(adhoc_datagram_receive(&receiver, packet.data, packet.size, 1) ==
   ADHOC_DATAGRAM_DUPLICATE);
  CHECK(receiver.duplicates == 1);

  packet.data[packet.size - 1u] ^= 1;
  CHECK(adhoc_datagram_receive(&receiver, packet.data, packet.size, 2) ==
   ADHOC_DATAGRAM_INVALID);
  CHECK(receiver.invalid_packets == 1);

  packet.data[packet.size - 1u] ^= 1;
  adhoc_datagram_init(&receiver, 11, 5, 0);
  CHECK(adhoc_datagram_receive(&receiver, packet.data, packet.size, 3) ==
   ADHOC_DATAGRAM_FOREIGN);
}

static void test_sequence_wrap_and_window_bound(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket first;
  AdhocDatagramPacket second;
  uint8_t output[2];
  size_t size = 0;
  unsigned int index;

  adhoc_datagram_init(&sender, 12, 6, 0);
  adhoc_datagram_init(&receiver, 12, 6, 0);
  sender.tx_base_sequence = UINT32_MAX;
  sender.next_tx_sequence = UINT32_MAX;
  receiver.next_rx_sequence = UINT32_MAX;
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"a", 1));
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"b", 1));
  CHECK(send_next(&sender, &receiver, 0, 1, &first));
  CHECK(first.sequence == UINT32_MAX);
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(output[0] == 'a');
  acknowledge(&receiver, &sender, 5000);
  CHECK(send_next(&sender, &receiver, 8000, 1, &second));
  CHECK(second.sequence == 1);
  CHECK(adhoc_datagram_pop(&receiver, output, sizeof(output), &size));
  CHECK(output[0] == 'b');

  adhoc_datagram_init(&sender, 13, 1, 0);
  for(index = 0; index < ADHOC_DATAGRAM_TX_WINDOW; index++)
    CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"q", 1));
  CHECK(!adhoc_datagram_queue(&sender, (const uint8_t *)"q", 1));
}

static void test_probe_gets_prompt_ack(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket probe;
  AdhocDatagramPacket ack;

  adhoc_datagram_init(&sender, 14, 1, 0);
  adhoc_datagram_init(&receiver, 14, 1, 0);
  adhoc_datagram_request_probe(&sender);
  CHECK(adhoc_datagram_prepare(&sender, 0, &probe));
  CHECK(probe.kind == ADHOC_DATAGRAM_TX_PROBE);
  CHECK(adhoc_datagram_note_sent(&sender, &probe, 0));
  CHECK(adhoc_datagram_receive(&receiver, probe.data, probe.size, 1) ==
   ADHOC_DATAGRAM_ACCEPTED);
  CHECK(!adhoc_datagram_prepare(&receiver, 1000, &ack));
  CHECK(adhoc_datagram_prepare(&receiver, 1001, &ack));
  CHECK(ack.kind == ADHOC_DATAGRAM_TX_ACK);
  CHECK(adhoc_datagram_note_sent(&receiver, &ack, 1001));
  CHECK(adhoc_datagram_receive(&sender, ack.data, ack.size, 1002) ==
   ADHOC_DATAGRAM_ACCEPTED);
  CHECK(!adhoc_datagram_prepare(&sender, 1003, &ack));
}

static void test_due_ack_precedes_retransmission(void)
{
  AdhocDatagram first;
  AdhocDatagram second;
  AdhocDatagramPacket packet;
  AdhocDatagramPacket delayed;
  uint8_t output;
  size_t size = 0;

  adhoc_datagram_init(&first, 15u, 1u, 0u);
  adhoc_datagram_init(&second, 15u, 1u, 0u);
  CHECK(adhoc_datagram_queue(&first, (const uint8_t *)"a", 1u));
  CHECK(adhoc_datagram_queue(&second, (const uint8_t *)"b", 1u));
  CHECK(send_next(&first, &second, 0u, 0, &delayed));
  CHECK(send_next(&second, &first, 1000u, 1, NULL));
  CHECK(adhoc_datagram_pop(&first, &output, sizeof(output), &size));
  CHECK(adhoc_datagram_receive(&second, delayed.data, delayed.size,
   2000u) == ADHOC_DATAGRAM_ACCEPTED);
  CHECK(adhoc_datagram_pop(&second, &output, sizeof(output), &size));

  CHECK(adhoc_datagram_prepare(&first, 100000u, &packet));
  CHECK(packet.kind == ADHOC_DATAGRAM_TX_ACK);
  CHECK(adhoc_datagram_note_sent(&first, &packet, 100000u));
  CHECK(adhoc_datagram_receive(&second, packet.data, packet.size,
   100001u) == ADHOC_DATAGRAM_ACCEPTED);
  CHECK(adhoc_datagram_outstanding(&second) == 0u);
  CHECK(first.retransmissions == 0u);
}

static void test_data_ack_is_prompt(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket packet;
  uint8_t output;
  size_t size = 0;

  adhoc_datagram_init(&sender, 16u, 1u, 0u);
  adhoc_datagram_init(&receiver, 16u, 1u, 0u);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"a", 1u));
  CHECK(send_next(&sender, &receiver, 0u, 1, NULL));
  CHECK(adhoc_datagram_pop(&receiver, &output, sizeof(output), &size));

  CHECK(!adhoc_datagram_prepare(&receiver, 999u, &packet));
  CHECK(adhoc_datagram_prepare(&receiver, 1000u, &packet));
  CHECK(packet.kind == ADHOC_DATAGRAM_TX_ACK);
  CHECK(adhoc_datagram_note_sent(&receiver, &packet, 1000u));
  CHECK(adhoc_datagram_receive(&sender, packet.data, packet.size, 1001u) ==
   ADHOC_DATAGRAM_ACCEPTED);
  CHECK(!adhoc_datagram_prepare(&receiver, 4999u, &packet));
  CHECK(adhoc_datagram_prepare(&receiver, 5000u, &packet));
  CHECK(adhoc_datagram_outstanding(&sender) == 0u);
}

static void test_response_data_piggybacks_ack(void)
{
  AdhocDatagram host;
  AdhocDatagram joiner;
  AdhocDatagramPacket packet;
  uint8_t output;
  size_t size = 0;

  adhoc_datagram_init(&host, 0x50494759u, 1u, 0u);
  adhoc_datagram_init(&joiner, 0x50494759u, 1u, 0u);
  adhoc_datagram_set_role(&host, 1);
  adhoc_datagram_set_role(&joiner, 0);
  CHECK(adhoc_datagram_queue(&host, (const uint8_t *)"h", 1u));
  CHECK(send_next(&host, &joiner, 0u, 1, NULL));
  CHECK(adhoc_datagram_pop(&joiner, &output, sizeof(output), &size));
  CHECK(adhoc_datagram_queue(&joiner, (const uint8_t *)"j", 1u));
  CHECK(adhoc_datagram_prepare(&joiner, 1000u, &packet));
  CHECK(packet.kind == ADHOC_DATAGRAM_TX_DATA);
  CHECK(adhoc_datagram_note_sent(&joiner, &packet, 1000u));
  CHECK(joiner.ack_pending);
  CHECK(adhoc_datagram_receive(&host, packet.data, packet.size, 2000u) ==
   ADHOC_DATAGRAM_ACCEPTED);
  CHECK(adhoc_datagram_outstanding(&host) == 0u);
  CHECK(adhoc_datagram_pop(&host, &output, sizeof(output), &size));
  CHECK(output == 'j');
}

static void test_progress_stall_ignores_duplicate_activity(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket packet;
  uint8_t output;
  size_t size;
  uint64_t started_us = 100000u;
  uint64_t timeout_us = 2000000u;

  adhoc_datagram_init(&sender, 0x5354414Cu, 1u, started_us);
  adhoc_datagram_init(&receiver, 0x5354414Cu, 1u, started_us);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"x", 1u));
  CHECK(send_next(&sender, &receiver, started_us, 1, &packet));
  CHECK(adhoc_datagram_pop(&receiver, &output, sizeof(output), &size));
  CHECK(!adhoc_datagram_progress_stalled(&sender,
   started_us + timeout_us - 1u, timeout_us));

  CHECK(adhoc_datagram_receive(&receiver, packet.data, packet.size,
   started_us + timeout_us - 1000u) == ADHOC_DATAGRAM_DUPLICATE);
  CHECK(adhoc_datagram_progress_stalled(&sender,
   started_us + timeout_us, timeout_us));
}

static void test_retransmit_backoff_is_retained(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket packet;
  uint8_t value;
  size_t size;
  uint64_t first_deadline;
  uint64_t backed_off_rto;

  adhoc_datagram_init(&sender, 17u, 1u, 0u);
  adhoc_datagram_init(&receiver, 17u, 1u, 0u);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"a", 1u));
  CHECK(adhoc_datagram_prepare(&sender, 1u, &packet));
  CHECK(adhoc_datagram_note_sent(&sender, &packet, 1u));
  first_deadline = sender.tx[0].deadline_us;
  CHECK(adhoc_datagram_prepare(&sender, first_deadline, &packet));
  CHECK(adhoc_datagram_note_sent(&sender, &packet, first_deadline));
  backed_off_rto = sender.rto_us;
  CHECK(backed_off_rto > 200000u);
  CHECK(sender.tx[0].deadline_us >= first_deadline + backed_off_rto);

  CHECK(adhoc_datagram_receive(&receiver, packet.data, packet.size,
   first_deadline + 1u) == ADHOC_DATAGRAM_ACCEPTED);
  CHECK(adhoc_datagram_pop(&receiver, &value, sizeof(value), &size));
  acknowledge(&receiver, &sender, first_deadline + 5000u);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"b", 1u));
  CHECK(adhoc_datagram_prepare(&sender, first_deadline + 8000u, &packet));
  CHECK(adhoc_datagram_note_sent(&sender, &packet,
   first_deadline + 8000u));
  CHECK(sender.tx[0].deadline_us >=
   first_deadline + 8000u + backed_off_rto);
}

static void test_stale_ack_does_not_slide_window(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  AdhocDatagramPacket packet;
  AdhocDatagramPacket stale_ack;
  uint8_t value;
  size_t size;

  adhoc_datagram_init(&sender, 15, 1, 0);
  adhoc_datagram_init(&receiver, 15, 1, 0);
  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"a", 1));
  CHECK(send_next(&sender, &receiver, 0, 1, NULL));
  CHECK(adhoc_datagram_pop(&receiver, &value, sizeof(value), &size));
  CHECK(adhoc_datagram_prepare(&receiver, 5000, &stale_ack));
  CHECK(adhoc_datagram_note_sent(&receiver, &stale_ack, 5000));
  CHECK(adhoc_datagram_receive(&sender, stale_ack.data, stale_ack.size,
   6000) == ADHOC_DATAGRAM_ACCEPTED);
  CHECK(sender.tx_base_sequence == 2);

  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"b", 1));
  CHECK(send_next(&sender, &receiver, 9000, 1, NULL));
  CHECK(adhoc_datagram_pop(&receiver, &value, sizeof(value), &size));
  acknowledge(&receiver, &sender, 12000);
  CHECK(sender.tx_base_sequence == 3);

  CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)"c", 1));
  CHECK(adhoc_datagram_prepare(&sender, 15000, &packet));
  CHECK(adhoc_datagram_note_sent(&sender, &packet, 15000));
  CHECK(adhoc_datagram_receive(&sender, stale_ack.data, stale_ack.size,
   16000) == ADHOC_DATAGRAM_ACCEPTED);
  CHECK(sender.tx_base_sequence == 3);
  CHECK(adhoc_datagram_outstanding(&sender) == 1);
}

#define FAULT_MESSAGE_COUNT 100u
#define FAULT_PACKET_CAPACITY 48u

typedef struct
{
  AdhocDatagramPacket packet;
  uint64_t deliver_at_us;
  uint8_t destination;
  uint8_t valid;
} FaultPacket;

static void fault_enqueue(FaultPacket *network,
 const AdhocDatagramPacket *packet, uint64_t deliver_at_us,
 uint8_t destination)
{
  unsigned int index;

  for(index = 0; index < FAULT_PACKET_CAPACITY; index++)
  {
    if(network[index].valid)
      continue;
    network[index].packet = *packet;
    network[index].deliver_at_us = deliver_at_us;
    network[index].destination = destination;
    network[index].valid = 1;
    return;
  }
  CHECK(0 && "fault network capacity exhausted");
}

static void test_seeded_fault_network(void)
{
  AdhocDatagram sender;
  AdhocDatagram receiver;
  FaultPacket network[FAULT_PACKET_CAPACITY];
  unsigned int attempts[FAULT_MESSAGE_COUNT + 1u];
  unsigned int queued = 1;
  unsigned int expected = 1;
  unsigned int ack_number = 0;
  uint64_t now_us;

  memset(network, 0, sizeof(network));
  memset(attempts, 0, sizeof(attempts));
  adhoc_datagram_init(&sender, 0x46554c54u, 9, 0);
  adhoc_datagram_init(&receiver, 0x46554c54u, 9, 0);

  for(now_us = 0; now_us < 20000000u; now_us += 1000u)
  {
    AdhocDatagramPacket packet;
    unsigned int index;

    while(queued <= FAULT_MESSAGE_COUNT &&
          adhoc_datagram_can_queue(&sender))
    {
      uint32_t value = queued;

      CHECK(adhoc_datagram_queue(&sender, (const uint8_t *)&value,
       sizeof(value)));
      queued++;
    }

    if(adhoc_datagram_prepare(&sender, now_us, &packet))
    {
      unsigned int attempt;
      uint64_t delay;

      CHECK(packet.kind == ADHOC_DATAGRAM_TX_DATA);
      CHECK(packet.sequence <= FAULT_MESSAGE_COUNT);
      CHECK(adhoc_datagram_note_sent(&sender, &packet, now_us));
      attempt = ++attempts[packet.sequence];
      delay = ((packet.sequence * 5u + attempt * 3u) % 9u) * 1000u;
      if(attempt != 1u ||
         (packet.sequence % 7u != 0u && packet.sequence % 11u != 0u))
      {
        fault_enqueue(network, &packet, now_us + delay, 1);
        if(attempt == 1u && packet.sequence % 13u == 0u)
          fault_enqueue(network, &packet, now_us + delay + 2000u, 1);
      }
    }

    if(adhoc_datagram_prepare(&receiver, now_us, &packet))
    {
      CHECK(packet.kind == ADHOC_DATAGRAM_TX_ACK);
      CHECK(adhoc_datagram_note_sent(&receiver, &packet, now_us));
      ack_number++;
      if(ack_number % 5u != 1u)
      {
        fault_enqueue(network, &packet,
         now_us + (ack_number % 4u) * 1000u, 0);
      }
    }

    for(index = FAULT_PACKET_CAPACITY; index-- != 0;)
    {
      AdhocDatagramReceiveResult result;

      if(!network[index].valid ||
         network[index].deliver_at_us > now_us)
        continue;
      if(network[index].destination != 0)
      {
        result = adhoc_datagram_receive(&receiver,
         network[index].packet.data, network[index].packet.size, now_us);
      }
      else
      {
        result = adhoc_datagram_receive(&sender,
         network[index].packet.data, network[index].packet.size, now_us);
      }
      CHECK(result == ADHOC_DATAGRAM_ACCEPTED ||
       result == ADHOC_DATAGRAM_DUPLICATE);
      network[index].valid = 0;
    }

    while(expected <= FAULT_MESSAGE_COUNT)
    {
      uint32_t value = 0;
      size_t size = 0;

      if(!adhoc_datagram_pop(&receiver, (uint8_t *)&value,
         sizeof(value), &size))
        break;
      CHECK(size == sizeof(value));
      CHECK(value == expected);
      expected++;
    }

    if(expected > FAULT_MESSAGE_COUNT &&
       adhoc_datagram_outstanding(&sender) == 0)
      break;
  }

  CHECK(queued == FAULT_MESSAGE_COUNT + 1u);
  CHECK(expected == FAULT_MESSAGE_COUNT + 1u);
  CHECK(adhoc_datagram_outstanding(&sender) == 0);
  CHECK(sender.retransmissions != 0);
  CHECK(receiver.duplicates != 0);
  CHECK(receiver.out_of_window == 0);
  CHECK(receiver.invalid_packets == 0);
}

#define CONGESTED_MESSAGE_COUNT 24u
#define CONGESTED_PACKET_CAPACITY 12u

typedef struct
{
  AdhocDatagramPacket packet;
  uint64_t deliver_at_us;
  uint8_t destination;
  uint8_t valid;
} CongestedPacket;

static int congested_enqueue(CongestedPacket *network,
 uint64_t *next_delivery_us, const AdhocDatagramPacket *packet,
 uint64_t now_us, uint8_t destination)
{
  uint64_t deliver_at = now_us + 200000u;
  unsigned int index;

  if(deliver_at <= next_delivery_us[destination])
    deliver_at = next_delivery_us[destination] + 40000u;
  for(index = 0; index < CONGESTED_PACKET_CAPACITY; index++)
  {
    if(network[index].valid)
      continue;
    network[index].packet = *packet;
    network[index].deliver_at_us = deliver_at;
    network[index].destination = destination;
    network[index].valid = 1;
    next_delivery_us[destination] = deliver_at;
    return 1;
  }
  return 0;
}

static void test_bidirectional_congestion_recovers(void)
{
  AdhocDatagram endpoint[2];
  CongestedPacket network[CONGESTED_PACKET_CAPACITY];
  uint64_t next_delivery_us[2] = { 0, 0 };
  unsigned int next_value[2] = { 1, 1 };
  unsigned int expected[2] = { 1, 1 };
  uint64_t now_us;
  unsigned int side;

  memset(network, 0, sizeof(network));
  adhoc_datagram_init(&endpoint[0], 0x434F4E47u, 1u, 0u);
  adhoc_datagram_init(&endpoint[1], 0x434F4E47u, 1u, 0u);
  adhoc_datagram_set_role(&endpoint[0], 1);
  adhoc_datagram_set_role(&endpoint[1], 0);

  for(now_us = 0; now_us < 20000000u; now_us += 1000u)
  {
    unsigned int index;

    for(side = 0; side < 2u; side++)
    {
      AdhocDatagramPacket packet;

      while(next_value[side] <= CONGESTED_MESSAGE_COUNT &&
            adhoc_datagram_can_queue(&endpoint[side]))
      {
        uint32_t value = next_value[side]++;

        CHECK(adhoc_datagram_queue(&endpoint[side],
         (const uint8_t *)&value, sizeof(value)));
      }
      if(!adhoc_datagram_prepare(&endpoint[side], now_us, &packet))
        continue;
      if(congested_enqueue(network, next_delivery_us, &packet, now_us,
          (uint8_t)(side ^ 1u)))
      {
        CHECK(adhoc_datagram_note_sent(&endpoint[side], &packet, now_us));
      }
      else
      {
        CHECK(adhoc_datagram_note_blocked(&endpoint[side], &packet,
         now_us));
      }
    }

    for(index = 0; index < CONGESTED_PACKET_CAPACITY; index++)
    {
      CongestedPacket *queued = &network[index];
      AdhocDatagramReceiveResult result;

      if(!queued->valid || queued->deliver_at_us > now_us)
        continue;
      result = adhoc_datagram_receive(&endpoint[queued->destination],
       queued->packet.data, queued->packet.size, now_us);
      CHECK(result == ADHOC_DATAGRAM_ACCEPTED ||
       result == ADHOC_DATAGRAM_DUPLICATE);
      queued->valid = 0;
    }

    for(side = 0; side < 2u; side++)
    {
      uint32_t value;
      size_t size;

      while(adhoc_datagram_pop(&endpoint[side], (uint8_t *)&value,
            sizeof(value), &size))
      {
        CHECK(size == sizeof(value));
        CHECK(value == expected[side]);
        expected[side]++;
      }
    }

    if(expected[0] > CONGESTED_MESSAGE_COUNT &&
       expected[1] > CONGESTED_MESSAGE_COUNT &&
       adhoc_datagram_outstanding(&endpoint[0]) == 0u &&
       adhoc_datagram_outstanding(&endpoint[1]) == 0u)
      break;
  }

  CHECK(expected[0] == CONGESTED_MESSAGE_COUNT + 1u);
  CHECK(expected[1] == CONGESTED_MESSAGE_COUNT + 1u);
  CHECK(adhoc_datagram_outstanding(&endpoint[0]) == 0u);
  CHECK(adhoc_datagram_outstanding(&endpoint[1]) == 0u);
  CHECK(endpoint[0].retransmissions != 0u);
  CHECK(endpoint[1].retransmissions != 0u);
  CHECK(endpoint[0].ack_packets < CONGESTED_MESSAGE_COUNT * 2u);
  CHECK(endpoint[1].ack_packets < CONGESTED_MESSAGE_COUNT * 2u);
}

int main(void)
{
  test_ordered_round_trip();
  test_retransmits_loss();
  test_window_sends_before_ack();
  test_window_recovers_missing_base();
  test_backpressure_defers_retry();
  test_ack_does_not_block_async_response();
  test_role_jitter_breaks_symmetric_retries();
  test_symmetric_ack_collision_recovers_before_data_retry();
  test_duplicates_corruption_and_foreign_epoch();
  test_sequence_wrap_and_window_bound();
  test_probe_gets_prompt_ack();
  test_due_ack_precedes_retransmission();
  test_data_ack_is_prompt();
  test_response_data_piggybacks_ack();
  test_progress_stall_ignores_duplicate_activity();
  test_retransmit_backoff_is_retained();
  test_stale_ack_does_not_slide_window();
  test_seeded_fault_network();
  test_bidirectional_congestion_recovers();

  if(failures != 0)
  {
    fprintf(stderr, "%d ad-hoc datagram test(s) failed\n", failures);
    return 1;
  }
  puts("ad-hoc datagram tests passed");
  return 0;
}
