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

/* DMA0 started at VBlank copies nothing until the VBlank, then raises
   its IRQ (IF bit 8) if asked to and turns itself off. */
static void test_dma0_vblank_irq(void)
{
  store(iwram, 0x100, 4, 0x55667788u);
  mem_write(0x040000B0, 4, 0x03000100);
  mem_write(0x040000B4, 4, 0x03000200);
  mem_write(0x040000B8, 4, 0xD4000001u);
  assert(load(iwram, 0x200, 4) == 0 && !(io16(0x202) & 0x0100));
  advance(next_vblank() - now);
  assert(load(iwram, 0x200, 4) == 0x55667788u);
  assert((io16(0x202) & 0x0100) && !(io16(0x0BA) & 0x8000));
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

int main(void)
{
  arm_core_init(&core, &bus);
  test_dma3_word_alignment();
  test_dma0_vblank_irq();
  test_postflg_byte_does_not_halt();
  puts("rom_run tests passed");
  return 0;
}
