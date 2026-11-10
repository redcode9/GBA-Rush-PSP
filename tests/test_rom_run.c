/*
 * Host tests for the GBA around the New core in tests/core/rom_run.c, the
 * runner of make test-roms: its bus driven directly, without a ROM.
 * make test-sanitize runs them under ASan, which catches an access past
 * the end of a memory array.
 */

#include <assert.h>

#define main rom_run_main
#include "core/rom_run.c"
#undef main

/* A 32-bit DMA3 ignores address bits 0-1: given the last word of EWRAM
   and of VRAM at addresses ending in 2, it copies the whole word and
   stays inside both arrays. */
static void test_dma3_word_alignment(void)
{
  store(ewram, 0x3FFFC, 4, 0x11223344u);
  mem_write(0x040000D4, 4, 0x0203FFFE);
  mem_write(0x040000D8, 4, 0x06017FFE);
  mem_write(0x040000DC, 4, 0x84000001u);
  assert(load(vram, 0x17FFC, 4) == 0x11223344u);
}

/* A byte written to POSTFLG is stored and does not halt; one written to
   HALTCNT halts. */
static void test_postflg_byte_does_not_halt(void)
{
  mem_write(0x04000300, 1, 1);
  assert(!halted && io[0x300] == 1);
  mem_write(0x04000301, 1, 0);
  assert(halted);
}

/* Past 40003FFh nothing answers, though the 3FFh mirror would map
   4FFF600h (where the mGBA suite prints) and 4FFF608h onto IE and IME,
   and 4000400h, the first address past them, onto DISPCNT. 4800200h has
   200h in its low 23 bits, so a guard on fewer than 24 bits would let it
   reach IE. */
static void test_io_past_registers(void)
{
  mem_write(0x04000000, 2, 0x0403);
  mem_write(0x04000200, 2, 0x2001);
  mem_write(0x04000208, 2, 1);
  mem_write(0x04000400, 2, 0);
  mem_write(0x04800200, 2, 0);
  mem_write(0x04FFF600, 1, 0);
  mem_write(0x04FFF600, 2, 0);
  mem_write(0x04FFF608, 4, 0);
  assert(io16(0x000) == 0x0403);
  assert(io16(0x200) == 0x2001 && io16(0x208) == 1);
  assert(mem_read(0x04000400, 2) == 0 && mem_read(0x04FFF600, 1) == 0);
  assert(mem_read(0x04800200, 2) == 0);
  assert(mem_read(0x04FFF600, 2) == 0 && mem_read(0x04FFF600, 4) == 0);
  mem_write(0x04000000, 2, 0);
  mem_write(0x04000200, 2, 0);
  mem_write(0x04000208, 2, 0);
}

int main(void)
{
  arm_core_init(&core, &bus);
  test_dma3_word_alignment();
  test_postflg_byte_does_not_halt();
  test_io_past_registers();
  puts("rom_run tests passed");
  return 0;
}
