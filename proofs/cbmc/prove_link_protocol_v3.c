#include <stdint.h>
#include <string.h>

#include "link_protocol.h"

static int start_equal(const LinkStreamStart *left,
 const LinkStreamStart *right)
{
  return left->generation == right->generation &&
   left->start_cycle == right->start_cycle &&
   left->finish_cycle == right->finish_cycle &&
   left->clock_cycle == right->clock_cycle &&
   left->acknowledged_sequence == right->acknowledged_sequence &&
   left->outgoing == right->outgoing &&
   left->identity == right->identity && left->mode == right->mode &&
   left->setting == right->setting &&
   left->player_count == right->player_count &&
   left->flags == right->flags;
}

static int reply_equal(const LinkStreamReply *left,
 const LinkStreamReply *right)
{
  return left->generation == right->generation &&
   left->clock_cycle == right->clock_cycle &&
   left->acknowledged_sequence == right->acknowledged_sequence &&
   left->outgoing == right->outgoing &&
   left->start_identity == right->start_identity &&
   left->start_cycle == right->start_cycle && left->mode == right->mode &&
   left->setting == right->setting &&
   left->player_count == right->player_count &&
   left->flags == right->flags;
}

static int benchmark_equal(const LinkBenchmarkControl *left,
 const LinkBenchmarkControl *right)
{
  return left->run_id == right->run_id &&
   left->duration_ms == right->duration_ms &&
   left->target_words_per_second == right->target_words_per_second &&
   left->maximum_burst_us == right->maximum_burst_us &&
   left->seed == right->seed && left->burst_words == right->burst_words &&
   left->action == right->action && left->flags == right->flags;
}

int main(void)
{
  LinkPacket packet;
  LinkStreamStart start;
  LinkStreamStart decoded_start;
  LinkStreamReply reply;
  LinkStreamReply decoded_reply;
  LinkBenchmarkControl benchmark;
  LinkBenchmarkControl decoded_benchmark;

  memset(&start, 0, sizeof(start));
  start.generation = 3u;
  start.start_cycle = 100u;
  start.finish_cycle = 1252u;
  start.clock_cycle = 100u;
  start.acknowledged_sequence = 6u;
  start.outgoing = 0x1234u;
  start.identity = 0xABCDEF01u;
  start.mode = 2u;
  start.setting = 3u;
  start.player_count = 2u;
  start.flags = LINK_STREAM_FLAG_CLOCK_OWNER;
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_START, 9u, 7u);
  __CPROVER_assert(link_protocol_write_stream_start(&packet, &start) ==
    LINK_PROTOCOL_OK, "valid v3 start is encodable");
  __CPROVER_assert(packet.payload_size == 32u,
   "v3 start payload remains bounded");
  __CPROVER_assert(link_protocol_read_stream_start(&packet,
    &decoded_start) == LINK_PROTOCOL_OK &&
    start_equal(&start, &decoded_start),
   "v3 start payload round trips exactly");

  memset(&reply, 0, sizeof(reply));
  reply.generation = 3u;
  reply.clock_cycle = 700u;
  reply.acknowledged_sequence = 7u;
  reply.outgoing = 0x5678u;
  reply.start_identity = start.identity;
  reply.start_cycle = start.start_cycle;
  reply.mode = 2u;
  reply.setting = 3u;
  reply.player_count = 2u;
  link_protocol_packet_init(&packet, LINK_MESSAGE_TRANSFER_REPLY, 9u, 7u);
  __CPROVER_assert(link_protocol_write_stream_reply(&packet, &reply) ==
    LINK_PROTOCOL_OK, "valid v3 reply is encodable");
  __CPROVER_assert(link_protocol_read_stream_reply(&packet,
    &decoded_reply) == LINK_PROTOCOL_OK &&
    reply_equal(&reply, &decoded_reply),
   "v3 reply payload round trips exactly");

  memset(&benchmark, 0, sizeof(benchmark));
  benchmark.run_id = 1u;
  benchmark.duration_ms = 600000u;
  benchmark.target_words_per_second = 1000u;
  benchmark.maximum_burst_us = 20000u;
  benchmark.seed = 0x13579BDFu;
  benchmark.burst_words = 9u;
  benchmark.action = LINK_BENCHMARK_START;
  link_protocol_packet_init(&packet, LINK_MESSAGE_BENCHMARK_CONTROL,
   9u, 0u);
  __CPROVER_assert(link_protocol_write_benchmark_control(&packet,
    &benchmark) == LINK_PROTOCOL_OK &&
    packet.flags == LINK_PROTOCOL_FLAG_OPTIONAL,
   "benchmark control is bounded and explicitly optional");
  __CPROVER_assert(link_protocol_read_benchmark_control(&packet,
    &decoded_benchmark) == LINK_PROTOCOL_OK &&
    benchmark_equal(&benchmark, &decoded_benchmark),
   "benchmark control round trips exactly");
  return 0;
}
