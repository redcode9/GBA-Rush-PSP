/*
 * Host tests for mem_stats.c: the largest block search on a fake
 * allocator that gives blocks up to a threshold, the packing of a
 * measurement into the two MEMORY_STATS records of the runtime log, and
 * the allocation trace ring.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "mem_stats.h"

#define MIB (1024u * 1024u)

/* Gives a dummy block for any size up to threshold. */
typedef struct
{
  size_t threshold;
  size_t largest_request;
  unsigned calls;
  unsigned allocs;
  unsigned frees;
  unsigned outstanding;
  unsigned max_outstanding;
} FakeHeap;

static char dummy_block;

static void *fake_alloc(size_t size, void *context)
{
  FakeHeap *heap = context;

  heap->calls++;
  if(size > heap->largest_request)
    heap->largest_request = size;
  if(size == 0 || size > heap->threshold)
    return NULL;
  heap->allocs++;
  heap->outstanding++;
  if(heap->outstanding > heap->max_outstanding)
    heap->max_outstanding = heap->outstanding;
  return &dummy_block;
}

static void fake_release(void *block, void *context)
{
  FakeHeap *heap = context;

  assert(block == &dummy_block);
  assert(heap->outstanding == 1);
  heap->outstanding--;
  heap->frees++;
}

static uint32_t probe(FakeHeap *heap, size_t threshold, uint32_t limit)
{
  memset(heap, 0, sizeof(*heap));
  heap->threshold = threshold;
  return mem_stats_largest_block(fake_alloc, fake_release, heap, limit);
}

static void test_largest_block_bisection_fake_allocator(void)
{
  FakeHeap heap;

  /* Not a whole number of steps: the answer is the step below. */
  assert(probe(&heap, 5 * MIB + 1234, 64 * MIB) == 5 * MIB);
  assert(heap.allocs == heap.frees);
  assert(heap.allocs > 0);
  assert(heap.max_outstanding == 1);
  assert(heap.outstanding == 0);
  assert(heap.largest_request <= 64 * MIB);
  /* Bisection over 16384 steps. */
  assert(heap.calls <= 15);

  assert(probe(&heap, 5 * MIB + MEM_STATS_STEP - 1, 64 * MIB) == 5 * MIB);
  assert(probe(&heap, 5 * MIB + MEM_STATS_STEP, 64 * MIB) ==
   5 * MIB + MEM_STATS_STEP);
  assert(heap.allocs == heap.frees);

  /* More memory than the limit: the limit, never a request above it. */
  assert(probe(&heap, 200 * MIB, 64 * MIB) == 64 * MIB);
  assert(heap.largest_request == 64 * MIB);
  assert(heap.allocs == heap.frees && heap.max_outstanding == 1);
  assert(probe(&heap, 200 * MIB, 64 * MIB + 100) == 64 * MIB);
  assert(heap.largest_request == 64 * MIB);
}

static void test_zero_memory(void)
{
  FakeHeap heap;

  assert(probe(&heap, 0, 64 * MIB) == 0);
  assert(heap.allocs == 0 && heap.frees == 0);
  assert(probe(&heap, MEM_STATS_STEP - 1, 64 * MIB) == 0);
  assert(heap.allocs == 0 && heap.frees == 0);
  assert(probe(&heap, MEM_STATS_STEP, 64 * MIB) == MEM_STATS_STEP);
  assert(heap.allocs == heap.frees);

  /* A limit under one step asks for nothing. */
  assert(probe(&heap, 64 * MIB, MEM_STATS_STEP - 1) == 0);
  assert(heap.calls == 0);
}

/* A capture's report keeps the lower of its start and stop values. */
static void test_keep_lowest_per_field(void)
{
  const MemStats start = { 10, 20, 30, 40, 50, 60 };
  const MemStats lower = { 9, 18, 27, 36, 45, 54 };
  const MemStats higher = { 11, 21, 31, 41, 51, 61 };
  const MemStats mixed = { 9, 21, 27, 41, 45, 61 };
  const MemStats mixed_lowest = { 9, 20, 27, 40, 45, 60 };
  MemStats lowest = start;

  mem_stats_keep_lowest(&lowest, &higher);
  assert(memcmp(&lowest, &start, sizeof(lowest)) == 0);
  mem_stats_keep_lowest(&lowest, &lower);
  assert(memcmp(&lowest, &lower, sizeof(lowest)) == 0);

  /* Each field on its own: a sample lower in some fields only. */
  lowest = start;
  mem_stats_keep_lowest(&lowest, &mixed);
  assert(memcmp(&lowest, &mixed_lowest, sizeof(lowest)) == 0);
}

static void roundtrip(const MemStats *stats, MemStatsPoint point)
{
  MemStats decoded;
  MemStatsPoint decoded_point;
  uint8_t flags[2];
  uint32_t values[2][3];

  mem_stats_encode(stats, point, flags, values);
  assert(flags[0] == (uint8_t)(point << 1));
  assert(flags[1] == (uint8_t)(point << 1 | 1));
  assert(values[0][0] == stats->image >> 10);
  assert(values[0][1] == stats->heap_free >> 10);
  assert(values[0][2] == stats->heap_largest >> 10);
  assert(values[1][0] == stats->kernel_free >> 10);
  assert(values[1][1] == stats->kernel_largest >> 10);
  assert(values[1][2] == stats->rom_buffer >> 10);

  memset(&decoded, 0xFF, sizeof(decoded));
  assert(mem_stats_decode(flags, (const uint32_t (*)[3])values, &decoded,
   &decoded_point));
  assert(decoded_point == point);
  assert(decoded.image == (stats->image & ~1023u));
  assert(decoded.heap_free == (stats->heap_free & ~1023u));
  assert(decoded.heap_largest == (stats->heap_largest & ~1023u));
  assert(decoded.kernel_free == (stats->kernel_free & ~1023u));
  assert(decoded.kernel_largest == (stats->kernel_largest & ~1023u));
  assert(decoded.rom_buffer == (stats->rom_buffer & ~1023u));
}

/* 1 when records with these flags and value as the image decode. */
static int decodes(uint8_t first, uint8_t second, uint32_t value)
{
  const uint8_t flags[2] = { first, second };
  const uint32_t values[2][3] = { { value, 0, 0 }, { 0, 0, 0 } };
  MemStats stats;
  MemStatsPoint point;

  return mem_stats_decode(flags, values, &stats, &point);
}

static void test_encode_decode_roundtrip(void)
{
  const MemStats zero = { 0, 0, 0, 0, 0, 0 };
  const MemStats full =
  {
    64 * MIB, 64 * MIB, 64 * MIB, 64 * MIB, 64 * MIB, 64 * MIB
  };
  /* Each field distinct; the sub-KiB parts round down. */
  const MemStats mixed =
  {
    3 * MIB + 1023, 1 * MIB + 1, 8 * MIB, 20 * MIB + 2047, 12 * MIB + 5,
    32 * MIB
  };
  MemStats largest;
  int point;

  memset(&largest, 0xFF, sizeof(largest));
  for(point = MEM_STATS_BOOT; point <= MEM_STATS_CAPTURE_STOP; point++)
  {
    roundtrip(&zero, (MemStatsPoint)point);
    roundtrip(&full, (MemStatsPoint)point);
    roundtrip(&mixed, (MemStatsPoint)point);
    roundtrip(&largest, (MemStatsPoint)point);
  }

  assert(decodes(2, 3, 0));
  assert(decodes(10, 11, UINT32_MAX >> 10));
  /* Point 0 and 6 do not exist. */
  assert(!decodes(0, 1, 0));
  assert(!decodes(12, 13, 0));
  /* Parts swapped or of two points. */
  assert(!decodes(3, 2, 0));
  assert(!decodes(3, 3, 0));
  assert(!decodes(2, 2, 0));
  assert(!decodes(2, 5, 0));
  /* Over 4 GiB in bytes. */
  assert(!decodes(2, 3, (UINT32_MAX >> 10) + 1));
}

/* Push n has pointer n, size n, caller n + 1; even pushes are frees. */
static void push_numbered(MemStatsTraceRing *ring, uint32_t n)
{
  mem_stats_trace_push(ring, n, n % 2 ? n : MEM_STATS_TRACE_FREE, n + 1);
}

static void assert_numbered(const MemStatsTraceEntry *entry, uint32_t n)
{
  assert(entry->pointer == n);
  assert(entry->size == (n % 2 ? n : MEM_STATS_TRACE_FREE));
  assert(entry->caller == n + 1);
}

static void test_trace_ring_overwrites_oldest(void)
{
  static MemStatsTraceRing ring;
  static MemStatsTraceEntry out[MEM_STATS_TRACE_CAPACITY + 1];
  uint32_t n;
  size_t i;

  assert(mem_stats_trace_snapshot(&ring, out, MEM_STATS_TRACE_CAPACITY) == 0);

  /* Partial: in push order. */
  for(n = 1; n <= 3; n++)
    push_numbered(&ring, n);
  assert(mem_stats_trace_snapshot(&ring, out, MEM_STATS_TRACE_CAPACITY) == 3);
  for(i = 0; i < 3; i++)
    assert_numbered(&out[i], (uint32_t)i + 1);

  /* Full and wrapped by 3: pushes 1-3 are gone. */
  for(; n <= MEM_STATS_TRACE_CAPACITY + 3; n++)
    push_numbered(&ring, n);
  assert(mem_stats_trace_snapshot(&ring, out, MEM_STATS_TRACE_CAPACITY + 1) ==
   MEM_STATS_TRACE_CAPACITY);
  for(i = 0; i < MEM_STATS_TRACE_CAPACITY; i++)
    assert_numbered(&out[i], (uint32_t)i + 4);

  /* A smaller output keeps the newest. */
  assert(mem_stats_trace_snapshot(&ring, out, 5) == 5);
  for(i = 0; i < 5; i++)
    assert_numbered(&out[i], MEM_STATS_TRACE_CAPACITY - 1 + (uint32_t)i);
  assert(mem_stats_trace_snapshot(&ring, out, 0) == 0);

  /* The push count wrapping past 4G leaves the ring full. */
  ring.next = UINT32_MAX;
  push_numbered(&ring, 7);
  push_numbered(&ring, 8);
  assert(mem_stats_trace_snapshot(&ring, out, MEM_STATS_TRACE_CAPACITY) ==
   MEM_STATS_TRACE_CAPACITY);
  assert_numbered(&out[MEM_STATS_TRACE_CAPACITY - 2], 7);
  assert_numbered(&out[MEM_STATS_TRACE_CAPACITY - 1], 8);
}

int main(void)
{
  test_largest_block_bisection_fake_allocator();
  test_zero_memory();
  test_keep_lowest_per_field();
  test_encode_decode_roundtrip();
  test_trace_ring_overwrites_oldest();
  puts("Memory stats tests passed");
  return 0;
}
