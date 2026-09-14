#include <stdio.h>
#include <string.h>

#include "adhoc_rendezvous.h"

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

static void test_round_trip(void)
{
  AdhocRendezvousMessage input;
  AdhocRendezvousMessage output;
  uint8_t packet[ADHOC_RENDEZVOUS_PACKET_SIZE];
  size_t packet_size = 0;

  input.kind = ADHOC_RENDEZVOUS_CONNECT;
  input.connection_id = 0x12345678u;
  input.epoch = 7;
  input.recovery_id = 0;
  CHECK(adhoc_rendezvous_encode(&input, packet, sizeof(packet),
   &packet_size));
  CHECK(packet_size == sizeof(packet));
  CHECK(adhoc_rendezvous_decode(packet, packet_size, &output));
  CHECK(output.kind == input.kind);
  CHECK(output.connection_id == input.connection_id);
  CHECK(output.epoch == input.epoch);

  input.kind = ADHOC_RENDEZVOUS_CONNECT_ACK;
  CHECK(adhoc_rendezvous_encode(&input, packet, sizeof(packet),
   &packet_size));
  CHECK(adhoc_rendezvous_decode(packet, packet_size, &output));
  CHECK(output.kind == input.kind);

  input.kind = ADHOC_RENDEZVOUS_RESYNC_COMMIT_ACK;
  input.epoch = 8;
  input.recovery_id = 0xABCDEF01u;
  CHECK(adhoc_rendezvous_encode(&input, packet, sizeof(packet),
   &packet_size));
  CHECK(adhoc_rendezvous_decode(packet, packet_size, &output));
  CHECK(output.kind == input.kind && output.epoch == input.epoch);
  CHECK(output.recovery_id == input.recovery_id);

  input.kind = ADHOC_RENDEZVOUS_RESYNC_COMPLETE;
  CHECK(adhoc_rendezvous_encode(&input, packet, sizeof(packet),
   &packet_size));
  CHECK(adhoc_rendezvous_decode(packet, packet_size, &output));
  CHECK(output.kind == input.kind);

  input.kind = ADHOC_RENDEZVOUS_RESYNC_COMPLETE_ACK;
  CHECK(adhoc_rendezvous_encode(&input, packet, sizeof(packet),
   &packet_size));
  CHECK(adhoc_rendezvous_decode(packet, packet_size, &output));
  CHECK(output.kind == input.kind);

  input.kind = ADHOC_RENDEZVOUS_RESYNC_DATA_READY;
  CHECK(adhoc_rendezvous_encode(&input, packet, sizeof(packet),
   &packet_size));
  CHECK(adhoc_rendezvous_decode(packet, packet_size, &output));
  CHECK(output.kind == input.kind);
}

static void test_rejects_invalid_packets(void)
{
  AdhocRendezvousMessage message;
  uint8_t packet[ADHOC_RENDEZVOUS_PACKET_SIZE];
  size_t packet_size = 0;

  message.kind = ADHOC_RENDEZVOUS_CONNECT;
  message.connection_id = 7;
  message.epoch = 1;
  message.recovery_id = 0;
  CHECK(adhoc_rendezvous_encode(&message, packet, sizeof(packet),
   &packet_size));
  packet[8] ^= 1;
  CHECK(!adhoc_rendezvous_decode(packet, packet_size, &message));
  CHECK(!adhoc_rendezvous_decode(packet, packet_size - 1u, &message));

  message.kind = ADHOC_RENDEZVOUS_CONNECT;
  message.connection_id = 7;
  message.epoch = 1;
  message.recovery_id = 1;
  CHECK(!adhoc_rendezvous_encode(&message, packet, sizeof(packet),
   &packet_size));
  message.kind = ADHOC_RENDEZVOUS_RESYNC_REQUEST;
  message.recovery_id = 0;
  CHECK(!adhoc_rendezvous_encode(&message, packet, sizeof(packet),
   &packet_size));

  memset(&message, 0, sizeof(message));
  CHECK(!adhoc_rendezvous_encode(&message, packet, sizeof(packet),
   &packet_size));
}

int main(void)
{
  test_round_trip();
  test_rejects_invalid_packets();
  if(failures != 0)
  {
    fprintf(stderr, "%d rendezvous test(s) failed\n", failures);
    return 1;
  }
  puts("ad-hoc rendezvous tests passed");
  return 0;
}
