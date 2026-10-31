/*
 * Host tests for link_net.c, the reliable message channel the link cable
 * runs over unreliable ad-hoc datagrams: pairing by console id, rejection
 * of foreign and malformed datagrams, exactly-once in-order delivery over a
 * lossy network, flow control and the heartbeat.
 *
 * Datagram layout: a 40-byte header ("GR", version, message count, sender
 * id, receiver id, first sequence, ack, consumed, clock, timestamp, echo,
 * reserved), then one (type, size, data) record per message.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link_net.h"

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

/* Small deterministic generator so failures reproduce. */
static uint32_t rng_state;

static uint32_t rng(void)
{
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state >> 8;
}

/* Sends whatever `from` has to say straight to `to` over a perfect link. */
static size_t exchange(LinkNet *from, LinkNet *to, uint64_t now)
{
  uint8_t datagram[LINK_NET_MAX_DATAGRAM];
  size_t size = link_net_build(from, datagram, sizeof(datagram), 0, now);

  if(size != 0)
    link_net_receive(to, datagram, size, now);
  return size;
}

static void test_first_contact_and_delivery(void)
{
  LinkNet a;
  LinkNet b;
  const LinkNetMessage *message;
  uint8_t payload[3] = { 1, 2, 3 };

  link_net_init(&a, 11, 0);
  link_net_init(&b, 22, 0);
  CHECK(link_net_queue(&a, 5, payload, sizeof(payload)));

  CHECK(exchange(&a, &b, 100) != 0);
  CHECK(b.peer_id == 11);
  message = link_net_peek(&b);
  CHECK(message != NULL);
  CHECK(message != NULL && message->type == 5 && message->size == 3 &&
   memcmp(message->data, payload, 3) == 0);
  link_net_pop(&b);
  CHECK(link_net_peek(&b) == NULL);

  /* The acknowledgement waits for the ack delay, then frees the message. */
  CHECK(link_net_unacked(&a) == 1);
  CHECK(exchange(&b, &a, 5000) != 0);
  CHECK(link_net_unacked(&a) == 0);
  CHECK(link_net_backlog(&a) == 0);
}

static void test_rejects_foreign_and_stale_packets(void)
{
  LinkNet a;
  LinkNet b;
  LinkNet stranger;
  LinkNet old_a;
  uint8_t datagram[LINK_NET_MAX_DATAGRAM];
  size_t size;

  link_net_init(&a, 11, 0);
  link_net_init(&b, 22, 0);
  link_net_init(&stranger, 33, 0);
  link_net_init(&old_a, 44, 0);

  exchange(&a, &b, 10);
  exchange(&b, &a, 20);

  /* A third console, or a restarted peer, is ignored once paired. */
  link_net_queue(&stranger, 1, NULL, 0);
  size = link_net_build(&stranger, datagram, sizeof(datagram), 0, 30);
  CHECK(!link_net_receive(&b, datagram, size, 30));
  link_net_queue(&old_a, 1, NULL, 0);
  size = link_net_build(&old_a, datagram, sizeof(datagram), 0, 30);
  CHECK(!link_net_receive(&b, datagram, size, 30));
  CHECK(link_net_peek(&b) == NULL);

  /* Our own packets echoed back are ignored too. */
  link_net_queue(&b, 1, NULL, 0);
  size = link_net_build(&b, datagram, sizeof(datagram), 0, 40);
  CHECK(!link_net_receive(&b, datagram, size, 40));
}

static void test_rejects_malformed_packets(void)
{
  LinkNet a;
  LinkNet b;
  uint8_t datagram[LINK_NET_MAX_DATAGRAM];
  uint8_t mutated[LINK_NET_MAX_DATAGRAM];
  size_t size;
  unsigned int i;

  link_net_init(&a, 11, 0);
  link_net_init(&b, 22, 0);
  link_net_queue(&a, 7, "abcdef", 6);
  link_net_queue(&a, 8, "xyz", 3);
  size = link_net_build(&a, datagram, sizeof(datagram), 0, 0);

  CHECK(!link_net_receive(&b, datagram, LINK_NET_HEADER_SIZE - 1, 0));
  CHECK(!link_net_receive(&b, datagram, size - 1, 0));
  memcpy(mutated, datagram, size);
  mutated[0] = 'X';
  CHECK(!link_net_receive(&b, mutated, size, 0));
  memcpy(mutated, datagram, size);
  mutated[3] = 3;   /* claims a third message that is not there */
  CHECK(!link_net_receive(&b, mutated, size, 0));
  memcpy(mutated, datagram, size);
  mutated[LINK_NET_HEADER_SIZE + 1] = 200;   /* message runs past the end */
  CHECK(!link_net_receive(&b, mutated, size, 0));
  memcpy(mutated, datagram, size);
  mutated[16] = 9;   /* acknowledges messages never sent */
  CHECK(!link_net_receive(&b, mutated, size, 0));
  CHECK(link_net_peek(&b) == NULL);
  CHECK(b.peer_id == 0);

  /* Random corruption never crashes and never delivers a partial packet. */
  rng_state = 12345;
  for(i = 0; i < 20000; i++)
  {
    size_t length = rng() % (size + 8);
    unsigned int flips = 1 + rng() % 4;

    memcpy(mutated, datagram, size);
    while(flips-- != 0)
      mutated[rng() % size] ^= (uint8_t)(1u << (rng() % 8));
    link_net_init(&b, 22, 0);
    if(link_net_receive(&b, mutated, length, 0))
    {
      const LinkNetMessage *message = link_net_peek(&b);

      CHECK(message == NULL || message->size <= LINK_NET_MAX_MESSAGE);
    }
  }
  link_net_init(&b, 22, 0);
  CHECK(link_net_receive(&b, datagram, size, 0));
}

/* A datagram in transit. */
typedef struct
{
  uint8_t data[LINK_NET_MAX_DATAGRAM];
  size_t size;
  uint64_t arrival;
  int to_b;
} Flight;

/* Delivers 5000 messages each way over a lossy, duplicating, reordering
   link and checks every message arrives exactly once and in order. */
static void test_lossy_network_in_order_once(unsigned int loss_percent,
 unsigned int duplicate_percent, unsigned int jitter_us)
{
  enum { TOTAL = 5000, FLIGHTS = 512 };
  static Flight flights[FLIGHTS];
  LinkNet a;
  LinkNet b;
  uint32_t sent_a = 0;
  uint32_t sent_b = 0;
  uint32_t got_a = 0;
  uint32_t got_b = 0;
  uint64_t now = 0;
  unsigned int count = 0;
  unsigned int i;

  rng_state = loss_percent * 100u + duplicate_percent + jitter_us;
  link_net_init(&a, 1, 0);
  link_net_init(&b, 2, 0);

  while((got_a < TOTAL || got_b < TOTAL) && now < 600000000u)
  {
    LinkNet *sides[2] = { &a, &b };
    unsigned int side;

    now += 250;
    while(sent_a < TOTAL && link_net_backlog(&a) < 40)
    {
      uint8_t value[4] = { (uint8_t)sent_a, (uint8_t)(sent_a >> 8), 0, 0 };

      if(!link_net_queue(&a, 1, value, sizeof(value)))
        break;
      sent_a++;
    }
    while(sent_b < TOTAL && link_net_backlog(&b) < 40)
    {
      uint8_t value[4] = { (uint8_t)sent_b, (uint8_t)(sent_b >> 8), 1, 0 };

      if(!link_net_queue(&b, 2, value, sizeof(value)))
        break;
      sent_b++;
    }

    for(side = 0; side < 2; side++)
    {
      uint8_t datagram[LINK_NET_MAX_DATAGRAM];
      size_t size = link_net_build(sides[side], datagram, sizeof(datagram),
       0, now);
      unsigned int copies;

      if(size == 0)
        continue;
      copies = rng() % 100 < loss_percent ? 0u : 1u;
      if(copies != 0 && rng() % 100 < duplicate_percent)
        copies = 2;
      while(copies-- != 0 && count < FLIGHTS)
      {
        memcpy(flights[count].data, datagram, size);
        flights[count].size = size;
        flights[count].arrival = now + 1000u + rng() % (jitter_us + 1u);
        flights[count].to_b = side == 0;
        count++;
      }
    }

    for(i = 0; i < count;)
    {
      if(flights[i].arrival <= now)
      {
        link_net_receive(flights[i].to_b ? &b : &a, flights[i].data,
         flights[i].size, now);
        flights[i] = flights[--count];
      }
      else
        i++;
    }

    while(link_net_peek(&b) != NULL)
    {
      const LinkNetMessage *message = link_net_peek(&b);

      CHECK(message->type == 1);
      CHECK((uint32_t)(message->data[0] | (message->data[1] << 8)) ==
       (got_b & 0xFFFFu));
      got_b++;
      link_net_pop(&b);
    }
    while(link_net_peek(&a) != NULL)
    {
      const LinkNetMessage *message = link_net_peek(&a);

      CHECK(message->type == 2);
      CHECK((uint32_t)(message->data[0] | (message->data[1] << 8)) ==
       (got_a & 0xFFFFu));
      got_a++;
      link_net_pop(&a);
    }
  }

  CHECK(got_a == TOTAL);
  CHECK(got_b == TOTAL);
  /* The round-trip estimate stays near the 1 ms base delay plus jitter. */
  CHECK(a.rtt_us >= 1000u && a.rtt_us < 1000u + 2u * jitter_us + 20000u);
  printf("link_net loss=%u%% dup=%u%% jitter=%uus: %u messages each way in "
   "%.2fs, rtt %uus, %u resends\n", loss_percent, duplicate_percent,
   jitter_us, TOTAL, (double)now / 1e6, a.rtt_us, a.resends + b.resends);
}

/* At most LINK_NET_QUEUE messages may be unacknowledged at once. */
static void test_full_window_and_inbox(void)
{
  LinkNet a;
  LinkNet b;
  unsigned int i;
  unsigned int delivered = 0;

  link_net_init(&a, 1, 0);
  link_net_init(&b, 2, 0);
  for(i = 0; i < LINK_NET_QUEUE; i++)
    CHECK(link_net_queue(&a, 1, &i, 1));
  CHECK(!link_net_queue(&a, 1, &i, 1));

  /* The receiver never pops: its inbox fills, and the sender keeps the
     rest until there is room again. */
  for(i = 0; i < 50; i++)
  {
    exchange(&a, &b, 20000u * (i + 1));
    exchange(&b, &a, 20000u * (i + 1) + 10000u);
  }
  CHECK(link_net_unacked(&a) == 0);
  while(link_net_peek(&b) != NULL)
  {
    CHECK(link_net_peek(&b)->data[0] == (uint8_t)delivered);
    delivered++;
    link_net_pop(&b);
  }
  CHECK(delivered == LINK_NET_QUEUE);
}

/* With nothing to send, a datagram goes out only when the heartbeat is due;
   a peer heard from 8 s ago or more counts as silent with an 8 s timeout. */
static void test_silence_and_heartbeat(void)
{
  LinkNet a;
  uint8_t datagram[LINK_NET_MAX_DATAGRAM];

  link_net_init(&a, 1, 0);
  CHECK(link_net_build(&a, datagram, sizeof(datagram), 0, 0) != 0);
  CHECK(link_net_build(&a, datagram, sizeof(datagram), 0, 1000) == 0);
  link_net_set_heartbeat(&a, 16000);
  CHECK(link_net_build(&a, datagram, sizeof(datagram), 0, 16000) != 0);
  CHECK(!link_net_peer_silent(&a, 1000000, 8000000));
  CHECK(link_net_peer_silent(&a, 8000000, 8000000));
}

/* Runs every case, the lossy one from a perfect link up to 50% loss; failed
   CHECKs are printed and counted, and any failure makes the exit status 1. */
int main(void)
{
  test_first_contact_and_delivery();
  test_rejects_foreign_and_stale_packets();
  test_rejects_malformed_packets();
  test_full_window_and_inbox();
  test_silence_and_heartbeat();
  test_lossy_network_in_order_once(0, 0, 0);
  test_lossy_network_in_order_once(5, 2, 4000);
  test_lossy_network_in_order_once(25, 10, 20000);
  test_lossy_network_in_order_once(50, 25, 50000);

  if(failures != 0)
  {
    fprintf(stderr, "%d link_net check(s) failed\n", failures);
    return 1;
  }
  printf("link_net tests passed\n");
  return 0;
}
