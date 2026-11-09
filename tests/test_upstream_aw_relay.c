/*
 * Host check behind the Advance Wars rows of docs/UPSTREAM.md (ENG-19.2).
 *
 * libretro/gpsp at 5819380 relays the Advance Wars link protocol in
 * serial_proto.c. Its sender, serialaw_senddata (:418-427), stores two
 * header words and the 16-bit payload, packed two per word, in a local
 * u32 pkt[2 + 128]: a payload over 256 halfwords writes past it. The
 * question is whether a caller gets there.
 *
 * The caller is serialaw_master_send (:486-568). serial.c:163-181 calls it
 * on every multiplayer transfer the game starts as the master (netplay
 * client 0, also the value with no netplay session) in the AW1 or AW2
 * mode, chosen by the core option or by the game's gba_over.h flags
 * (gba_memory.c:1917-1920). Every value it acts on is a word the game
 * wrote to SIOMLT_SEND.
 *
 * The relay is not in our tree (LNK-11.2 ports it behind the LNK-11 gate),
 * so this test runs our own model of that master side. The model keeps
 * only what decides the payload size, and counts where the relay would
 * write instead of writing there, so the test itself stays defined C.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PKT_WORDS (2 + 128)    /* u32 pkt[2 + 128], serial_proto.c:420 */
#define QUEUE_ENTRIES 512      /* u16 data[MAX_FPACK], serial_proto.c:37, :82 */
#define CMD_NONE 0x7FFF
#define CMD_NOP 0x5FFF

enum relay_state { PACKETS, SYNC, INTERSYNC };   /* PACKETS after a reset */
enum parse_state { COMMANDS, HEADER, BODY };

struct relay
{
  unsigned tail;         /* words after the packet body: 1 in AW1, 2 in AW2 */
  uint16_t sync;         /* sync command: 0x5678 in AW1, 0x9ABC in AW2 */
  enum relay_state state;
  enum parse_state parse;
  unsigned size;         /* low byte of the packet's header word */
  unsigned queued;       /* halfwords in the master's queue */
  unsigned top;          /* most halfwords the queue ever held */
  unsigned sent;         /* payload halfwords of the last send */
  unsigned sends;
};

static void relay_init(struct relay *r, int aw2)
{
  memset(r, 0, sizeof(*r));
  r->tail = aw2 ? 2 : 1;
  r->sync = aw2 ? 0x9ABC : 0x5678;
}

static void queue_word(struct relay *r)
{
  r->queued++;
  if(r->queued > r->top)
    r->top = r->queued;
}

static void send_payload(struct relay *r, unsigned halfwords)
{
  r->sent = halfwords;
  r->sends++;
}

/* One transfer the game starts as the master, with `word` in SIOMLT_SEND.
   No peer is connected, so no peer has queued data. */
static void relay_master(struct relay *r, uint16_t word)
{
  switch(r->state)
  {
    case SYNC:
      if(word == CMD_NONE)
        r->state = PACKETS;
      else
      {
        if((word != r->sync) && (word != CMD_NOP))
        {
          r->state = INTERSYNC;
          r->queued = 0;
        }
        send_payload(r, 0);
      }
      break;

    case INTERSYNC:
      /* Key words have nonzero low 10 bits and are queued; a word with
         zero low bits sends the whole run, whatever its length. */
      if((word >= 0x8000) && (word <= 0x9F00))
      {
        if((word & 0x3FF) != 0)
          queue_word(r);
        else
        {
          send_payload(r, r->queued);
          r->queued = 0;
        }
      }
      else if(word != CMD_NOP)
        r->state = PACKETS;
      break;

    case PACKETS:
      if(r->parse == COMMANDS)
      {
        if((word >> 8) == 0x4F)
          r->parse = HEADER;
        else if(word == r->sync)
          r->state = SYNC;
        else
          send_payload(r, 0);
      }
      else if(r->parse == HEADER)
      {
        /* The queue keeps the header too, then size + tail more words. */
        r->size = word & 0xFF;
        r->queued = 0;
        queue_word(r);
        r->parse = BODY;
      }
      else
      {
        queue_word(r);
        if(r->queued == r->size + r->tail + 1)
        {
          r->parse = COMMANDS;
          send_payload(r, r->queued);
        }
      }
      break;
  }
}

/* 32-bit words the sender stores in pkt for a payload of n halfwords. */
static unsigned pkt_words(unsigned n)
{
  return 2 + (n + 1) / 2;
}

/* A packet's payload is its header plus size + tail words, so the largest
   size bytes overflow pkt by one word: 255 in AW1, 254 and 255 in AW2. From
   one size byte lower the payload no longer fits the 8-bit count of the
   flags word, and its upper bit lands in the state byte. */
static void test_packet_size_byte_overflows_pkt(void)
{
  int aw2;
  unsigned size;

  for(aw2 = 0; aw2 <= 1; aw2++)
  {
    for(size = 0; size <= 0xFF; size++)
    {
      struct relay r;
      unsigned body;
      int overflow = aw2 ? (size >= 254) : (size == 255);
      int truncated = aw2 ? (size >= 253) : (size >= 254);

      relay_init(&r, aw2);
      relay_master(&r, 0x4FFF);
      relay_master(&r, (uint16_t)size);
      for(body = 0; (r.sends == 0) && (body < 1000); body++)
        relay_master(&r, 0x1234);

      assert(r.sends == 1);
      assert(body == size + r.tail);
      assert(r.sent == size + r.tail + 1);
      assert(r.top <= QUEUE_ENTRIES);
      assert((pkt_words(r.sent) > PKT_WORDS) == overflow);
      assert((r.sent > 0xFF) == truncated);
    }
  }
}

/* In the intersync phase a run of 256 key words fits pkt, 257 overflow it,
   and a run of 513 overflows the master's 512-entry queue before any send. */
static void test_intersync_run_overflows_pkt_and_queue(void)
{
  struct relay r;
  unsigned i;

  relay_init(&r, 0);
  relay_master(&r, r.sync);
  assert(r.state == SYNC);
  relay_master(&r, 0x8001);
  assert(r.state == INTERSYNC);

  for(i = 0; i < 256; i++)
    relay_master(&r, 0x8001);
  relay_master(&r, 0x8000);
  assert(r.sent == 256);
  assert(pkt_words(r.sent) == PKT_WORDS);

  for(i = 0; i < 257; i++)
    relay_master(&r, 0x8001);
  relay_master(&r, 0x8000);
  assert(r.sent == 257);
  assert(pkt_words(r.sent) > PKT_WORDS);

  for(i = 0; i < QUEUE_ENTRIES + 1; i++)
    relay_master(&r, 0x8001);
  assert(r.state == INTERSYNC);
  assert(r.top > QUEUE_ENTRIES);
}

int main(void)
{
  test_packet_size_byte_overflows_pkt();
  test_intersync_run_overflows_pkt_and_queue();
  puts("Upstream AW relay tests passed");
  return 0;
}
