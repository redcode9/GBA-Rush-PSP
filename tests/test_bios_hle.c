/*
 * Host tests for bios_hle.c, the official BIOS's Div results.
 */

#include <stdint.h>
#include <stdio.h>

#include "bios_hle.h"

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

int main(void)
{
  test_reference();
  if(failures)
    return 1;
  printf("bios_hle tests passed\n");
  return 0;
}
