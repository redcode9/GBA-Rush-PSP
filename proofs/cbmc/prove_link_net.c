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

/* The 40-byte header plus 12 bytes: room for up to 6 records. The unwind
   bound in scripts/check-cbmc.sh follows from it: if the datagram grows and
   the bound does not, the proof fails on an unwinding assertion. */
#define PROOF_DATAGRAM 52u

/* Replaces CBMC's built-in memcpy, whose temporary array of symbolic size
   sends the solver into array theory and out of memory. The preconditions
   are CBMC 6.11's own, word for word; the copy goes byte by byte, so every
   byte is checked as well. scripts/check-cbmc.sh explains its unwind bound
   and why the proof still checks everything it did. The preconditions come
   first so that CBMC checks them at each call, as it does its own. */
void *memcpy(void *dst, const void *src, size_t n)
{
  __CPROVER_precondition(
    __CPROVER_POINTER_OBJECT(dst) != __CPROVER_POINTER_OBJECT(src) ||
      ((const unsigned char *)src >= (const unsigned char *)dst + n) ||
      ((const unsigned char *)dst >= (const unsigned char *)src + n),
    "memcpy src/dst overlap");
  __CPROVER_precondition(
    __CPROVER_r_ok(src, n), "memcpy source region readable");
  __CPROVER_precondition(
    __CPROVER_w_ok(dst, n), "memcpy destination region writeable");

  for(size_t i = 0; i < n; i++)
    ((unsigned char *)dst)[i] = ((const unsigned char *)src)[i];
  return dst;
}

/* Left undefined on purpose: CBMC lets each call return any value. */
uint32_t nondet_u32(void);
uint64_t nondet_u64(void);
size_t nondet_size(void);

/* The proof itself: CBMC fails it if any assert or safety check can fail
   for some nondet datagram, channel state or reply capacity. */
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
  /* The unread ones look like what link_net_receive delivers: it rejects
     type 0. */
  for(i = net.in_read; i != net.in_write; i++)
  {
    net.in[i % LINK_NET_QUEUE].type = (uint8_t)(1u + i % 4u);
    net.in[i % LINK_NET_QUEUE].size = 0u;
  }

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
