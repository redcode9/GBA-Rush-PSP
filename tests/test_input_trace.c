/*
 * Host tests for input_trace.c, the diagnostic input recorder (built with
 * DIAGNOSTIC_INPUT_TRACE): key states are stored per frame as change
 * events, encoded as a "GBIT" file tied to the game code and ROM revision,
 * and replayed in place of the physical keys from a chosen start frame.
 * Key masks use the 10 GBA button bits (0x3FF = all of them).
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "input_trace.h"

static const uint8_t game_code[INPUT_TRACE_GAME_CODE_SIZE] =
 { 'B', 'P', 'R', 'E' };

/* Encodes a five-frame recording (frames 100-104, revision 1) of four
   events: the initial state plus three changes. Returns its size. */
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

  /* Replayed from frame 500: recorded keys replace the physical ones
     (0x3FF) until the trace ends; before and after it they pass through. */
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

  /* The initial state fills one of the two slots, an unchanged state adds
     nothing, and the second change overflows. A frame before the start is
     out of order. */
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

static void test_paged_header_identity(void)
{
  /*
   * Fixture of a paged ROM: the first 32KiB page (which holds the header)
   * has been evicted and its physical slot reused for a distant file
   * region, so gamepak_rom no longer carries the identity. Only the
   * stable gamepak_header copy (taken at load time) does.
   */
  uint8_t rom_page[32 * 1024];
  uint8_t header[INPUT_TRACE_IDENTITY_SIZE];
  uint8_t encoded[INPUT_TRACE_HEADER_SIZE + 8 * INPUT_TRACE_EVENT_SIZE];
  InputTraceEvent events[8];
  InputTrace trace;
  InputTrace replay;
  size_t written = 0;
  size_t size;
  size_t i;

  for(i = 0; i < sizeof(rom_page); i++)
    rom_page[i] = (uint8_t)(i ^ 0xA5);

  /* Cartridge header: title at 0xA0, game code at 0xAC, revision at 0xBC.
     0x1000000 is the 16 MiB ROM file size. */
  memset(header, 0, sizeof(header));
  memcpy(header + 0xA0, "BPREREPLAY", 10);
  memcpy(header + 0xAC, game_code, sizeof(game_code));
  header[0xBC] = 7;

  /* The record wrapper must take the identity from the stable header,
   * not from the paged ROM buffer. */
  input_trace_init(&trace, events, 8);
  assert(input_trace_begin_record_from_header(&trace, 0, header,
   0x1000000U, 0));
  assert(trace.state == INPUT_TRACE_RECORDING);
  assert(memcmp(trace.game_code, game_code, sizeof(game_code)) == 0);
  assert(trace.rom_revision == 7);
  assert(input_trace_apply(&trace, 0, 0) == 0);
  assert(input_trace_apply(&trace, 1, 5) == 5);
  assert(input_trace_finish_record(&trace, 2));
  assert(input_trace_encode(&trace, encoded, sizeof(encoded), &written));
  size = written;

  /* The replay wrapper must validate against the stable header even
   * though the ROM header page is gone. */
  input_trace_init(&replay, events, 8);
  assert(input_trace_begin_replay_from_header(&replay, encoded, size,
   header, 0x1000000U, 1000));
  assert(replay.state == INPUT_TRACE_REPLAYING);
  assert(replay.rom_revision == 7);
  assert(input_trace_apply(&replay, 1001, 0x3FF) == 5);
  input_trace_cancel(&replay);
  assert(replay.state == INPUT_TRACE_IDLE);

  /* A header carrying a different ROM revision must be rejected. */
  header[0xBC] = 8;
  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay_from_header(&replay, encoded, size,
   header, 0x1000000U, 1000));
  assert(replay.error == INPUT_TRACE_ERROR_IDENTITY);
  header[0xBC] = 7;

  /* The paged ROM buffer itself must not be a source of identity. */
  memset(rom_page + 0xAC, 0, sizeof(game_code));
  assert(memcmp(rom_page + 0xAC, game_code, sizeof(game_code)) != 0);
}

/* A ROM file too small to contain the 0xC0-byte header has no identity. */
static void test_header_file_size_gate(void)
{
  uint8_t header[INPUT_TRACE_IDENTITY_SIZE];
  uint8_t encoded[INPUT_TRACE_HEADER_SIZE + 8 * INPUT_TRACE_EVENT_SIZE];
  InputTraceEvent events[8];
  InputTrace trace;
  InputTrace replay;
  size_t size;

  memset(header, 0, sizeof(header));
  memcpy(header + 0xAC, game_code, sizeof(game_code));
  header[0xBC] = 1;
  size = make_trace(encoded, sizeof(encoded));

  input_trace_init(&trace, events, 8);
  assert(!input_trace_begin_record_from_header(&trace, 0, header,
   INPUT_TRACE_IDENTITY_SIZE - 1U, 0));
  assert(trace.state == INPUT_TRACE_IDLE);
  assert(input_trace_begin_record_from_header(&trace, 0, header,
   INPUT_TRACE_IDENTITY_SIZE, 0));
  assert(trace.state == INPUT_TRACE_RECORDING);
  assert(input_trace_finish_record(&trace, 1));
  input_trace_cancel(&trace);

  input_trace_init(&replay, events, 8);
  assert(!input_trace_begin_replay_from_header(&replay, encoded, size,
   header, INPUT_TRACE_IDENTITY_SIZE - 1U, 0));
  assert(replay.state == INPUT_TRACE_IDLE);
  assert(!input_trace_begin_replay_from_header(&replay, encoded, size,
   NULL, 0x1000000U, 0));
  assert(replay.state == INPUT_TRACE_IDLE);

  input_trace_init(&replay, events, 8);
  assert(input_trace_begin_replay_from_header(&replay, encoded, size,
   header, 0x1000000U, 2000));
  assert(replay.state == INPUT_TRACE_REPLAYING);
  assert(input_trace_apply(&replay, 2000 + replay.duration_frames + 1,
   0x3FF) == 0x3FF);
  assert(replay.state == INPUT_TRACE_FINISHED);
}

/* Runs every recording, file and replay case; a failing assert aborts the
   run. */
int main(void)
{
  test_record_and_replay();
  test_duplicate_states_and_capacity();
  test_identity_and_checksum_rejection();
  test_malformed_traces();
  test_cancel_and_invalid_arguments();
  test_paged_header_identity();
  test_header_file_size_gate();
  puts("Input trace tests passed");
  return 0;
}
