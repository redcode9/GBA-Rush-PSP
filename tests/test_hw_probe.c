/*
 * Host tests for hw_probe.c, the memory system probe: the pointer chain
 * visits every slot once per lap, the emitted MIPS code branches slot to
 * slot and returns, the knee finders read line, capacity and ways off
 * synthetic curves, and the report fits its capacity or says it does not.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw_probe.h"

#define BUFFER_WORDS (1u << 16)

static uintptr_t buffer[BUFFER_WORDS];
static uint32_t code[BUFFER_WORDS];

/* The number after "name=" in the report, or -1. */
static long field(const char *report, const char *name)
{
  char key[64];
  const char *at;

  snprintf(key, sizeof(key), "\n%s=", name);
  at = strstr(report, key);
  return at ? strtol(at + strlen(key), NULL, 10) : -1;
}

static void check_chain(uint32_t count, uint32_t stride)
{
  static unsigned char seen[BUFFER_WORDS];
  unsigned char *base = (unsigned char *)buffer;
  uintptr_t *start = hw_probe_chain(buffer, count, stride);
  uintptr_t *slot = start;
  uint32_t i;

  assert(start == buffer);
  memset(seen, 0, sizeof(seen));
  for(i = 0; i < count; i++)
  {
    size_t offset = (size_t)((unsigned char *)slot - base);

    assert(offset % stride == 0 && offset / stride < count);
    assert(!seen[offset / stride]);
    seen[offset / stride] = 1;
    slot = (uintptr_t *)*slot;
  }
  assert(slot == start);
}

static void test_chain_visits_every_slot_once(void)
{
  check_chain(1, 8);
  assert(buffer[0] == (uintptr_t)buffer);
  check_chain(2, 8);
  check_chain(64, 64);
  check_chain(1000, 256);
  check_chain(16, 16384);
  assert(hw_probe_chain(buffer, 0, 64) == NULL);
  assert(hw_probe_chain(buffer, 4, 0) == NULL);
  assert(hw_probe_chain(buffer, 4, sizeof(uintptr_t) + 1) == NULL);
}

static void check_code(uint32_t count, uint32_t stride)
{
  uint32_t words = stride / 4;
  uint32_t i;

  memset(code, 0xFF, sizeof(code));
  assert(hw_probe_emit(code, count, stride) == (count - 1) * stride + 8);
  for(i = 0; i + 1 < count; i++)
  {
    uint32_t pc = i * stride;
    uint32_t op = code[i * words];
    int32_t offset = (int16_t)(op & 0xFFFFu);

    assert((op & 0xFFFF0000u) == 0x10000000u);
    assert(pc + 4 + (uint32_t)(offset * 4) == (i + 1) * stride);
    assert(code[i * words + 1] == 0);
  }
  assert(code[i * words] == 0x03E00008u);
  assert(code[i * words + 1] == 0);
}

static void test_emit_branches_to_next_slot(void)
{
  check_code(1, 8);
  check_code(2, 8);
  check_code(100, 8);
  check_code(64, 64);
  check_code(16, 256);
  check_code(3, HW_PROBE_MAX_STRIDE);
  hw_probe_emit(code, 2, 16384);
  assert((code[0] & 0xFFFFu) == 4095);
  assert(hw_probe_emit(code, 0, 64) == 0);
  assert(hw_probe_emit(code, 2, 4) == 0);
  assert(hw_probe_emit(code, 2, 66) == 0);
  /* Branches reach 128 KiB, the scans stop at 64 KiB. */
  assert(hw_probe_emit(code, 2, HW_PROBE_MAX_STRIDE + 4) == 0);
}

/* Cycles x100 for hit + miss * min(1, stride / line). */
static void rising_curve(uint32_t *curve, uint32_t line, uint32_t hit,
 uint32_t miss)
{
  uint32_t i;

  for(i = 0; i < HW_PROBE_STRIDES; i++)
    curve[i] = hw_probe_stride[i] >= line ? hit + miss :
     hit + miss * hw_probe_stride[i] / line;
}

static void test_line_from_rising_curve(void)
{
  uint32_t curve[HW_PROBE_STRIDES];
  uint32_t i;

  rising_curve(curve, 64, 100, 800);
  assert(hw_probe_line(curve) == 64);
  rising_curve(curve, 32, 100, 800);
  assert(hw_probe_line(curve) == 32);
  rising_curve(curve, 256, 300, 6000);
  assert(hw_probe_line(curve) == 256);
  /* Exactly 90% of the last point is the line. */
  curve[0] = 100;
  curve[1] = 200;
  curve[2] = 300;
  curve[3] = 810;
  curve[4] = 850;
  curve[5] = 900;
  assert(hw_probe_line(curve) == 64);
  for(i = 0; i < HW_PROBE_STRIDES; i++)
    curve[i] = 500;
  assert(hw_probe_line(curve) == 0);
  /* A 25% rise is noise, not a line. */
  curve[HW_PROBE_STRIDES - 1] = 625;
  assert(hw_probe_line(curve) == 0);
  curve[HW_PROBE_STRIDES - 1] = 626;
  assert(hw_probe_line(curve) == 256);
}

static void test_capacity_from_jump(void)
{
  uint32_t curve[HW_PROBE_SIZES] = { 100, 100, 101, 99, 100, 900, 950 };
  uint32_t flat[HW_PROBE_SIZES] = { 100, 100, 100, 100, 100, 125, 125 };
  uint32_t no_baseline[HW_PROBE_SIZES] = { 0, 900, 900, 900, 900, 900, 900 };

  assert(hw_probe_capacity(curve) == 16384);
  curve[1] = 126;
  assert(hw_probe_capacity(curve) == 1024);
  assert(hw_probe_capacity(flat) == 0);
  /* No point can jump against itself: without a baseline there is none. */
  assert(hw_probe_capacity(no_baseline) == 0);
}

static void test_ways_from_jump(void)
{
  uint32_t curve[HW_PROBE_WAYS];
  uint32_t i;

  for(i = 0; i < HW_PROBE_WAYS; i++)
    curve[i] = i < 2 ? 300 - 50 * i : 4000;
  assert(hw_probe_ways(curve) == 2);
  for(i = 0; i < HW_PROBE_WAYS; i++)
    curve[i] = 300;
  assert(hw_probe_ways(curve) == 0);
  curve[HW_PROBE_WAYS - 1] = 4000;
  assert(hw_probe_ways(curve) == 15);
}

static void test_cycles_and_bandwidth_math(void)
{
  assert(hw_probe_cycles_x100(1000, 333, 333000) == 100);
  assert(hw_probe_cycles_x100(197, 333, 1u << 16) == 100);
  assert(hw_probe_cycles_x100(1000, 333, 0) == 0);
  assert(hw_probe_cycles_x100(UINT64_MAX / 1000000u, 333, 1) == UINT32_MAX);
  assert(hw_probe_mib_per_s(4u << 20, 1000) == 4000);
  assert(hw_probe_mib_per_s(1u << 20, 1000000) == 1);
  assert(hw_probe_mib_per_s(1u << 20, 0) == 0);
}

static void test_format_fits_and_overflows(void)
{
  static char out[HW_PROBE_CAPACITY + 1];
  HwProbe probe;
  int length;
  uint32_t i;

  /* Every number at its widest still fits. */
  memset(&probe, 0xFF, sizeof(probe));
  assert(hw_probe_format(out, HW_PROBE_CAPACITY, &probe) > 0);

  probe.cpu_mhz = 333;
  probe.bus_mhz = 166;
  probe.latency_x100[HW_PROBE_RAM] = 4200;
  probe.read_mib_s[HW_PROBE_SCRATCHPAD] = 1200;
  probe.write_mib_s[HW_PROBE_DCACHE] = 900;
  rising_curve(probe.dline, 64, 100, 800);
  rising_curve(probe.iline, 64, 200, 900);
  for(i = 0; i < HW_PROBE_SIZES; i++)
    probe.dsize[i] = probe.isize[i] = i < 5 ? 100 : 900;
  for(i = 0; i < HW_PROBE_WAYS; i++)
    probe.dways[i] = probe.iways[i] = i < 2 ? 100 : 900;
  probe.iways[HW_PROBE_WAYS - 1] = 2000000000u;

  length = hw_probe_format(out, HW_PROBE_CAPACITY, &probe);
  assert(length > 0 && (size_t)length == strlen(out));
  assert(!strncmp(out, "hw-probe=1\n", 11));
  assert(field(out, "cpu-mhz") == 333);
  assert(field(out, "bus-mhz") == 166);
  assert(field(out, "ram-latency-cycles-x100") == 4200);
  assert(field(out, "scratchpad-read-mib-s") == 1200);
  assert(field(out, "dcache-write-mib-s") == 900);
  assert(field(out, "dcache-line-bytes") == 64);
  assert(field(out, "dcache-capacity-bytes") == 16384);
  assert(field(out, "dcache-ways") == 2);
  assert(field(out, "icache-line-bytes") == 64);
  assert(field(out, "icache-capacity-bytes") == 16384);
  assert(field(out, "icache-ways") == 2);
  assert(field(out, "dcache-stride-8") == 200);
  assert(field(out, "dcache-size-65536") == 900);
  assert(field(out, "dcache-ways-1") == 100);
  assert(field(out, "icache-stride-256") == 1100);
  assert(field(out, "icache-ways-16") == 2000000000L);
  assert(out[length - 1] == '\n');

  for(i = 0; i < (uint32_t)length; i += 97)
  {
    memset(out, 'x', sizeof(out));
    assert(hw_probe_format(out, i, &probe) == -1);
    assert(out[i] == 'x');
  }
  memset(out, 'x', sizeof(out));
  assert(hw_probe_format(out, (size_t)length, &probe) == -1);
  assert(out[length] == 'x');
  assert(hw_probe_format(out, (size_t)length + 1, &probe) == length);
}

int main(void)
{
  test_chain_visits_every_slot_once();
  test_emit_branches_to_next_slot();
  test_line_from_rising_curve();
  test_capacity_from_jump();
  test_ways_from_jump();
  test_cycles_and_bandwidth_math();
  test_format_fits_and_overflows();
  puts("Hardware probe tests passed");
  return 0;
}
