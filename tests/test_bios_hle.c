/*
 * Host tests for bios_hle.c, the official BIOS's Div results, and for the
 * built-in BIOS image (open_bios.h) against them: its SWI 0x06 and 0x07
 * run from ARM and Thumb code on the ARM core's interpreter must return
 * the same r0, r1 and r3, and keep r2, r12 and the flags.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arm_core.h"
#include "bios_hle.h"

typedef uint8_t u8;
#include "open_bios.h"

static int failures;

#define CHECK(condition) check((condition), #condition, __LINE__)

static void check(int ok, const char *text, int line)
{
  if(!ok)
  {
    printf("test_bios_hle:%d: %s\n", line, text);
    failures++;
  }
}

/* ---- The reference --------------------------------------------------- */

/* Results of the official BIOS (SWI 0x06 run on an ARM emulator), the
   cases it returns from included. */
static const struct
{
  int32_t numerator, denominator;
  uint32_t r0, r1, r3;
} official[] =
{
  {-1234, 10, 0xFFFFFF85, 0xFFFFFFFC, 0x7B},
  {100, 7, 14, 2, 14},
  {-100, 7, 0xFFFFFFF2, 0xFFFFFFFE, 14},
  {100, -7, 0xFFFFFFF2, 2, 14},
  {-100, -7, 14, 0xFFFFFFFE, 14},
  {0, 0, 1, 0, 1},
  {1, 0, 1, 1, 1},
  {-1, 0, 0xFFFFFFFF, 0xFFFFFFFF, 1},
  {INT32_MIN, -1, 0x80000000, 0, 0x80000000},
  {INT32_MIN, 1, 0x80000000, 0, 0x80000000},
  {INT32_MIN, 2, 0xC0000000, 0, 0x40000000},
  {INT32_MIN, 3, 0xD5555556, 0xFFFFFFFE, 0x2AAAAAAA},
  {INT32_MIN, INT32_MIN, 1, 0, 1},
  {5, INT32_MIN, 0, 5, 0},
  {INT32_MAX, 1, 0x7FFFFFFF, 0, 0x7FFFFFFF},
};

static void test_reference(void)
{
  unsigned i;
  GbaBiosDiv result;

  for(i = 0; i < sizeof(official) / sizeof(official[0]); i++)
  {
    result = gba_bios_div(official[i].numerator, official[i].denominator);
    CHECK(result.r0 == official[i].r0);
    CHECK(result.r1 == official[i].r1);
    CHECK(result.r3 == official[i].r3);
  }
  /* Where the official BIOS hangs: the rule of 1 and -1 divided by 0. */
  result = gba_bios_div(7, 0);
  CHECK(result.r0 == 1 && result.r1 == 7 && result.r3 == 1);
  result = gba_bios_div(-7, 0);
  CHECK(result.r0 == 0xFFFFFFFF && result.r1 == 0xFFFFFFF9 && result.r3 == 1);
  result = gba_bios_div(INT32_MIN, 0);
  CHECK(result.r0 == 0xFFFFFFFF && result.r1 == 0x80000000 && result.r3 == 1);
}

/* ---- The built-in BIOS on the ARM core --------------------------------- */

static ArmCore core;
static uint8_t bios[0x4000];
static uint8_t iwram[0x8000];
/* At 0x08000000: callers of each SWI, "swi n" then "b .", in ARM from 0
   and in Thumb from 0x100 (one per 0x10 bytes, by SWI number). */
static uint8_t rom[0x200];

static uint32_t load(const uint8_t *memory, uint32_t offset, unsigned size)
{
  uint32_t value = 0;

  memcpy(&value, memory + offset, size);
  return value;
}

static uint32_t read_any(uint32_t address, unsigned size)
{
  switch(address >> 24)
  {
    case 0: return address < sizeof(bios) ? load(bios, address, size) : 0;
    case 3: return load(iwram, address & 0x7FFF, size);
    case 8: return load(rom, address & (sizeof(rom) - 1), size);
    default: return 0;
  }
}

static uint32_t read8(void *context, uint32_t address)
{
  (void)context;
  return read_any(address, 1);
}

static uint32_t read16(void *context, uint32_t address)
{
  (void)context;
  return read_any(address, 2);
}

static uint32_t read32(void *context, uint32_t address)
{
  (void)context;
  return read_any(address, 4);
}

/* Writes outside work RAM do nothing here. */
static void ignore(void *context, uint32_t address, uint32_t value)
{
  (void)context;
  (void)address;
  (void)value;
}

static const ArmBus bus =
{
  NULL, read8, read16, read32, ignore, ignore, ignore, read32
};

static uint32_t caller(unsigned swi, int thumb)
{
  return 0x08000000u + (thumb ? 0x100u : 0) + swi * 0x10u;
}

static void write_callers(void)
{
  unsigned swi;

  for(swi = 0; swi < 16; swi++)
  {
    uint32_t arm[2] = { 0xEF000000u | (swi << 16), 0xEAFFFFFEu };
    uint16_t thumb[2] = { (uint16_t)(0xDF00u | swi), 0xE7FEu };

    memcpy(rom + swi * 0x10, arm, sizeof(arm));
    memcpy(rom + 0x100 + swi * 0x10, thumb, sizeof(thumb));
  }
}

/* Calls a SWI from system mode as the BIOS's own boot leaves the stacks;
   returns 1 if it came back to its caller within the cycles. */
static int call_swi(unsigned swi, int thumb, uint32_t cpsr_flags,
 int32_t cycles)
{
  uint32_t r0 = core.v[0];
  uint32_t r1 = core.v[1];

  arm_core_reset(&core);
  arm_core_set_waitcnt(&core, 0x4317);
  core.map[3].read = iwram;
  core.map[3].write = iwram;
  core.map[3].mask = 0x7FFF;
  core.v[13] = 0x03007FE0;
  arm_core_set_cpsr(&core, cpsr_flags | 0x1F | (thumb ? ARM_CPSR_T : 0));
  core.v[13] = 0x03007F00;
  core.v[0] = r0;
  core.v[1] = r1;
  core.v[2] = 0x22222222;
  core.v[12] = 0xCCCCCCCC;
  core.v[15] = caller(swi, thumb);
  arm_core_run(&core, cycles);
  return core.v[15] == caller(swi, thumb) + (thumb ? 2 : 4);
}

static void check_div(int32_t numerator, int32_t denominator)
{
  GbaBiosDiv expected = gba_bios_div(numerator, denominator);
  static const uint32_t flags[2] = { 0x60000000u, 0x90000000u };
  unsigned swi;
  int thumb;

  for(swi = 6; swi <= 7; swi++)
    for(thumb = 0; thumb <= 1; thumb++)
    {
      uint32_t cpsr_flags = flags[(swi + (unsigned)thumb) & 1];
      int returned;

      /* DivArm takes the denominator in r0. */
      core.v[0] = (uint32_t)(swi == 6 ? numerator : denominator);
      core.v[1] = (uint32_t)(swi == 6 ? denominator : numerator);
      returned = call_swi(swi, thumb, cpsr_flags, 20000);
      CHECK(returned);
      if(!returned || core.v[0] != expected.r0 || core.v[1] != expected.r1 ||
         core.v[3] != expected.r3)
      {
        printf("test_bios_hle: SWI %u %s %d / %d: r0=%08x r1=%08x r3=%08x\n",
         swi, thumb ? "Thumb" : "ARM", (int)numerator, (int)denominator,
         (unsigned)core.v[0], (unsigned)core.v[1], (unsigned)core.v[3]);
        failures++;
      }
      CHECK(core.v[2] == 0x22222222 && core.v[12] == 0xCCCCCCCC);
      CHECK((arm_core_cpsr(&core) & 0xF00000FFu) ==
       (cpsr_flags | 0x1F | (thumb ? ARM_CPSR_T : 0)));
    }
}

static void test_image_div(void)
{
  static const int32_t values[] =
  {
    0, 1, -1, 2, -2, 3, -3, 7, -7, 10, -10, 100, -1234, 0xFFFF, 0x10000,
    0x12345678, -0x12345678, 0x40000000, -0x40000000, INT32_MAX,
    -INT32_MAX, INT32_MIN
  };
  const unsigned count = sizeof(values) / sizeof(values[0]);
  uint32_t seed = 12345;
  unsigned i;
  unsigned j;

  for(i = 0; i < count; i++)
    for(j = 0; j < count; j++)
      check_div(values[i], values[j]);
  for(i = 0; i < 2000; i++)
  {
    int32_t numerator;
    int32_t denominator;

    seed = seed * 1664525u + 1013904223u;
    numerator = (int32_t)seed;
    seed = seed * 1664525u + 1013904223u;
    /* Small to large denominators: a random bit length. */
    denominator = (int32_t)seed >> (seed & 31);
    check_div(numerator, denominator);
  }
}

int main(void)
{
  test_reference();
  memcpy(bios, open_bios_rom, sizeof(bios));
  write_callers();
  arm_core_init(&core, &bus);
  test_image_div();
  if(failures)
    return 1;
  printf("bios_hle tests passed\n");
  return 0;
}
