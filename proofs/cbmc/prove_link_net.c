/*
 * Bounded proof for the link channel's packet handling.
 *
 * Datagrams come straight from the radio, so any byte sequence must be
 * safe to parse. CBMC checks every datagram of up to PROOF_DATAGRAM bytes
 * against a channel in an arbitrary reachable state: no out-of-bounds
 * access or overflow, delivered messages fit their buffers, and the
 * queues never exceed their capacity. Building the reply is checked too.
 * The queues are shrunk to LINK_NET_QUEUE=4 so the state space stays small;
 * the code indexes them the same way at any size.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "link_net.h"

/* The 40-byte header plus 12 bytes: room for several short records. */
#define PROOF_DATAGRAM 52u

/* Left undefined on purpose: CBMC lets each call return any value. */
uint32_t nondet_u32(void);
uint64_t nondet_u64(void);
size_t nondet_size(void);

int main(void)
{
  static LinkNet net;
  uint8_t datagram[PROOF_DATAGRAM];   /* left unset: CBMC tries every value */
  uint8_t reply[PROOF_DATAGRAM + 32u]; /* fits one full-size record */
  size_t size = nondet_size();
  size_t capacity = nondet_size();
  uint32_t queued = nondet_u32();
  uint32_t delivered = nondet_u32();
  uint32_t unacked = nondet_u32();
  uint32_t unread = nondet_u32();
  unsigned int i;

  __CPROVER_assume(size <= PROOF_DATAGRAM);
  __CPROVER_assume(capacity <= sizeof(reply));

  link_net_init(&net, nondet_u32(), nondet_u64());
  net.peer_id = nondet_u32();

  /* Reach an arbitrary state: some messages queued, some delivered. */
  __CPROVER_assume(queued <= 4u && delivered <= 4u);
  __CPROVER_assume(unacked <= queued && unread <= delivered);
  for(i = 0; i < queued; i++)
    link_net_queue(&net, (uint8_t)(1u + i), datagram, (uint8_t)(i * 3u));
  net.out_acked = net.out_next - unacked;
  net.in_write = delivered;
  net.in_read = delivered - unread;

  link_net_receive(&net, datagram, size, nondet_u64());

  assert(net.in_write - net.in_read <= LINK_NET_QUEUE);
  assert(net.out_next - net.out_acked <= LINK_NET_QUEUE);
  for(i = net.in_read; i != net.in_write; i++)
  {
    const LinkNetMessage *message = &net.in[i % LINK_NET_QUEUE];

    assert(message->type != 0);
    assert(message->size <= LINK_NET_MAX_MESSAGE);
  }

  size = link_net_build(&net, reply, capacity, nondet_u32(), nondet_u64());
  assert(size == 0 || (size >= LINK_NET_HEADER_SIZE && size <= capacity));
  return 0;
}
