#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "input_trace.h"

static const uint8_t game_code[INPUT_TRACE_GAME_CODE_SIZE] =
 { 'B', 'P', 'R', 'E' };

static size_t make_trace(uint8_t *encoded, size_t encoded_size)
{
  InputTraceEvent events[8];
  InputTrace trace;
  size_t written = 0;

  input_trace_init(&trace, events, 8);
  assert(input_trace_begin_record(&trace, 100, game_code, 1, 0));
  assert(input_trace_apply(&trace, 100, 0) == 0);
  assert(input_trace_apply(&trace, 101, 1) == 1);
  assert(input_trace_apply(&trace, 102, 1) == 1);
  assert(input_trace_apply(&trace, 103, 3) == 3);
  assert(input_trace_apply(&trace, 104, 0) == 0);
  assert(input_trace_finish_record(&trace, 105));
  assert(input_trace_encode(&trace, encoded, encoded_size, &written));
  return written;
}

static void test_record_and_replay(void)
{
  uint8_t encoded[INPUT_TRACE_HEADER_SIZE + 8 * INPUT_TRACE_EVENT_SIZE];
  InputTraceEvent events[8];
  InputTrace replay;
  size_t size = make_trace(encoded, sizeof(encoded));

  assert(size == INPUT_TRACE_HEADER_SIZE + 4 * INPUT_TRACE_EVENT_SIZE);
  assert(memcmp(encoded, INPUT_TRACE_MAGIC, 4) == 0);

  input_trace_init(&replay, events, 8);
  assert(input_trace_begin_replay(&replay, encoded, size, game_code, 1, 500));
  assert(replay.state == INPUT_TRACE_REPLAYING);
  assert(replay.event_count == 4);
  assert(replay.duration_frames == 5);
  assert(input_trace_apply(&replay, 499, 0x3FF) == 0x3FF);
  assert(input_trace_apply(&replay, 500, 0x3FF) == 0);
  assert(input_trace_apply(&replay, 501, 0x3FF) == 1);
  assert(input_trace_apply(&replay, 502, 0x3FF) == 1);
  assert(input_trace_apply(&replay, 503, 0x3FF) == 3);
  assert(input_trace_apply(&replay, 504, 0x3FF) == 0);
  assert(input_trace_apply(&replay, 505, 0x3FF) == 0);
  assert(input_trace_apply(&replay, 506, 0x155) == 0x155);
  assert(replay.state == INPUT_TRACE_FINISHED);
}

static void test_duplicate_states_and_capacity(void)
{
  InputTraceEvent events[2];
  InputTrace trace;

  input_trace_init(&trace, events, 2);
  assert(input_trace_begin_record(&trace, 10, game_code, 0, 0));
  assert(input_trace_record(&trace, 11, 0));
  assert(trace.event_count == 1);
  assert(input_trace_record(&trace, 12, 1));
  assert(trace.event_count == 2);
  assert(!input_trace_record(&trace, 13, 2));
  assert(trace.state == INPUT_TRACE_ERROR);
  assert(trace.error == INPUT_TRACE_ERROR_CAPACITY);

  input_trace_init(&trace, events, 2);
  assert(input_trace_begin_record(&trace, 20, game_code, 0, 0));
  assert(!input_trace_record(&trace, 19, 1));
  assert(trace.error == INPUT_TRACE_ERROR_ORDER);
}

static void test_identity_and_checksum_rejection(void)
{
  uint8_t encoded[INPUT_TRACE_HEADER_SIZE + 8 * INPUT_TRACE_EVENT_SIZE];
  uint8_t wrong_code[INPUT_TRACE_GAME_CODE_SIZE] = { 'A', 'X', 'V', 'E' };
  InputTraceEvent events[8];
  InputTrace replay;
  size_t size = make_trace(encoded, sizeof(encoded));

  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay(&replay, encoded, size, wrong_code, 1, 0));
  assert(replay.error == INPUT_TRACE_ERROR_IDENTITY);

  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay(&replay, encoded, size, game_code, 2, 0));
  assert(replay.error == INPUT_TRACE_ERROR_IDENTITY);

  encoded[size - 1] ^= 0x80;
  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay(&replay, encoded, size, game_code, 1, 0));
  assert(replay.error == INPUT_TRACE_ERROR_CHECKSUM);
}

static void test_malformed_traces(void)
{
  uint8_t encoded[INPUT_TRACE_HEADER_SIZE + 8 * INPUT_TRACE_EVENT_SIZE];
  uint8_t copy[sizeof(encoded)];
  InputTraceEvent events[8];
  InputTrace replay;
  size_t size = make_trace(encoded, sizeof(encoded));

  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay(&replay, encoded,
   INPUT_TRACE_HEADER_SIZE - 1, game_code, 1, 0));
  assert(replay.error == INPUT_TRACE_ERROR_FORMAT);

  memcpy(copy, encoded, size);
  copy[0] = 'X';
  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay(&replay, copy, size, game_code, 1, 0));
  assert(replay.error == INPUT_TRACE_ERROR_FORMAT);

  memcpy(copy, encoded, size);
  copy[4]++;
  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay(&replay, copy, size, game_code, 1, 0));
  assert(replay.error == INPUT_TRACE_ERROR_FORMAT);

  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay(&replay, encoded, size - 1,
   game_code, 1, 0));
  assert(replay.error == INPUT_TRACE_ERROR_FORMAT);

  input_trace_init(&replay, events, 2);
  assert(!input_trace_begin_replay(&replay, encoded, size, game_code, 1, 0));
  assert(replay.error == INPUT_TRACE_ERROR_CAPACITY);
}

static void test_cancel_and_invalid_arguments(void)
{
  InputTraceEvent events[2];
  InputTrace trace;
  uint8_t encoded[64];
  size_t written = 99;

  input_trace_init(&trace, events, 2);
  assert(trace.state == INPUT_TRACE_IDLE);
  assert(!input_trace_begin_record(NULL, 0, game_code, 0, 0));
  assert(!input_trace_begin_record(&trace, 0, NULL, 0, 0));
  assert(!input_trace_encode(&trace, encoded, sizeof(encoded), &written));
  assert(written == 0);

  assert(input_trace_begin_record(&trace, 0, game_code, 0, 0));
  input_trace_cancel(&trace);
  assert(trace.state == INPUT_TRACE_IDLE);
  assert(trace.event_count == 0);
}

int main(void)
{
  test_record_and_replay();
  test_duplicate_states_and_capacity();
  test_identity_and_checksum_rejection();
  test_malformed_traces();
  test_cancel_and_invalid_arguments();
  puts("Input trace tests passed");
  return 0;
}
