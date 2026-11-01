/*
 * Host test: production dma_transfer() performance probe correctness.
 *
 * Compiles source/src/memory.c on the host
 * (real PSP SDK headers + tiny shims, see Makefile) and drives the actual
 * dma_transfer() path, so perf_monitor_record_dma() is exercised with the
 * values the production loop computes:
 *   - element count must be the transfer length, not the exhausted
 *     while(length--) counter;
 *   - the gamepak-pair class must cover GBA ROM regions 0x08-0x0D on BOTH
 *     source and destination, and nothing else.
 * It also checks paged ROM loading (test_paged_rom_cache_and_io).
 *
 * The system clock is stubbed (host_stub_set_time) so each transfer
 * observes a deterministic measured duration.
 */
#include <stdint.h>
#include <stdio.h>

#include "host_shim/gba_types.h"
#include "cpu.h"
#include "memory.h"
#include "perf_monitor.h"

void host_stub_set_time(uint64_t t0, uint64_t t1);
void host_stub_rom_io(int read_limit, int seek_fails);



static int failures = 0;

#define CHECK_EQ(expr, want, label) \
  do { \
    uint64_t check_got = (uint64_t)(expr); \
    uint64_t check_want = (uint64_t)(want); \
    if (check_got != check_want) { \
      printf("FAIL %s: want %llu got %llu\n", (label), \
       (unsigned long long)check_want, (unsigned long long)check_got); \
      failures++; \
    } \
  } while (0)

/* An immediate, non-repeating channel 0 DMA with both addresses
   incrementing and no IRQ. */
static void setup_dma(DmaTransferType *d, u32 src, u32 dst, u32 len,
 DMA_LENGTH_TYPE lt)
{
  d->dma_channel = 0;
  d->source_address = src;
  d->dest_address = dst;
  d->length = len;
  d->repeat_type = DMA_NO_REPEAT;
  d->direct_sound_channel = DMA_NO_DIRECT_SOUND;
  d->source_direction = DMA_INCREMENT;
  d->dest_direction = DMA_INCREMENT;
  d->length_type = lt;
  d->start_type = DMA_START_IMMEDIATELY;
  d->irq = DMA_NO_IRQ;
}

/*
 * A ROM file larger than its one-page (32 KiB) buffer, read through the
 * stubbed file I/O: bytes survive page eviction in every ROM mirror (0x08,
 * 0x0A, 0x0C); a failed or short read pads with 0xFF, is not cached and
 * counts as an I/O error; a short last page of the file is valid.
 */
static void test_paged_rom_cache_and_io(void)
{
  u32 base;
  u8 *page;
  PerfMetricsSnapshot snap;

  gamepak_ram_buffer_size = 0x8000;
  gamepak_ram_pages = 1;
  gamepak_size = gamepak_file_size = 0x10000;
  gamepak_file_large = 1;
  init_memory();
  host_stub_rom_io(0x8000, 0);

  for(base = 0x08000000; base <= 0x0C000000; base += 0x02000000)
  {
    CHECK_EQ(read_memory8(base + 16), 1, "ROM initial byte");
    load_gamepak_page(1);
    CHECK_EQ(read_memory8(base + 16), 1, "ROM byte after cache eviction");
    CHECK_EQ(read_memory16(base + 16), 0x0101, "ROM halfword after eviction");
    load_gamepak_page(1);
    CHECK_EQ(read_memory32(base + 16), 0x01010101, "ROM word after eviction");
  }

  perf_monitor_init();
  perf_monitor_start(0);
  host_stub_rom_io(-1, 0);
  page = load_gamepak_page(0);
  CHECK_EQ(page[0], 0xFF, "failed ROM read clears old data");
  CHECK_EQ(page[0x7FFF], 0xFF, "failed ROM read clears page tail");
  CHECK_EQ(memory_map_read[0x08000000 >> 15] == NULL, 1,
   "failed ROM page is not cached");
  CHECK_EQ(read_memory8(0x08000010), 0xFF, "failed ROM byte");
  host_stub_rom_io(0x8000, 0);
  CHECK_EQ(read_memory8(0x08000010), 1, "same ROM address retries after failure");

  host_stub_rom_io(8, 0);
  page = load_gamepak_page(1);
  CHECK_EQ(page[7], 2, "short ROM read keeps received bytes");
  CHECK_EQ(page[8], 0xFF, "short ROM read clears unread bytes");
  CHECK_EQ(memory_map_read[0x0A008000 >> 15] == NULL, 1,
   "short ROM read is not cached in mirrors");

  host_stub_rom_io(0x8000, 1);
  page = load_gamepak_page(0);
  CHECK_EQ(page[0], 0xFF, "failed seek does not read another ROM page");
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.gamepak_read_errors, 4, "ROM I/O failures recorded");

  gamepak_file_size = 0x8005;
  host_stub_rom_io(0x8000, 0);
  page = load_gamepak_page(1);
  CHECK_EQ(page[4], 2, "partial final ROM page data");
  CHECK_EQ(page[5], 0xFF, "partial final ROM page padding");
  CHECK_EQ(memory_map_read[0x08008000 >> 15] == page, 1,
   "valid partial final page is cached");
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.gamepak_read_errors, 4, "final page padding is not an I/O error");
}

/* Transfers A-G share one monitor, so each checks running totals; then the
   paged ROM test. Mismatches print FAIL and make the exit status 1. */
int main(void)
{
  PerfMetricsSnapshot snap;
  DmaTransferType d;
  u32 i;

  /* Production gamepak + memory setup (small-ROM direct mapping). */
  init_gamepak_buffer();
  gamepak_size = 0x20000;
  for (i = 0; i < gamepak_size; i++)
    gamepak_rom[i] = (u8)(i * 7 + 1);
  init_memory();

  perf_monitor_init();
  perf_monitor_start(1000);

  /* A: 16-bit ROM -> IWRAM. No gamepak pair expected. */
  setup_dma(&d, 0x08000000, 0x03000000, 10, DMA_16BIT);
  host_stub_set_time(100, 107);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma16_transfers, 1, "A dma16_transfers");
  CHECK_EQ(snap.dma16_elements, 10, "A dma16_elements");
  CHECK_EQ(snap.dma16_bytes, 20, "A dma16_bytes");
  CHECK_EQ(snap.dma16_total_us, 7, "A dma16_total_us");
  CHECK_EQ(snap.dma_gamepak_pairs, 0, "A dma_gamepak_pairs");
  for (i = 0; i < 10; i++)
  {
    /* IWRAM must hold the ROM bytes unchanged and in the same order. */
    u16 expect = (u16)((gamepak_rom[2 * i] << 8) | gamepak_rom[2 * i + 1]);
    u16 got = (u16)((iwram[2 * i] << 8) | iwram[2 * i + 1]);
    CHECK_EQ(got, expect, "A iwram data");
  }

  /* B: 16-bit ROM -> ROM mirror 0x0C. Must count as gamepak pair. */
  setup_dma(&d, 0x08000010, 0x0C000010, 5, DMA_16BIT);
  host_stub_set_time(100, 111);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma16_transfers, 2, "B dma16_transfers");
  CHECK_EQ(snap.dma16_elements, 15, "B dma16_elements");
  CHECK_EQ(snap.dma16_bytes, 30, "B dma16_bytes");
  CHECK_EQ(snap.dma16_total_us, 18, "B dma16_total_us");
  CHECK_EQ(snap.dma_gamepak_pairs, 1, "B dma_gamepak_pairs");

  /* C: 32-bit ROM -> EWRAM. Not a gamepak pair. */
  setup_dma(&d, 0x08000020, 0x02000020, 8, DMA_32BIT);
  host_stub_set_time(200, 205);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma32_transfers, 1, "C dma32_transfers");
  CHECK_EQ(snap.dma32_elements, 8, "C dma32_elements");
  CHECK_EQ(snap.dma32_bytes, 32, "C dma32_bytes");
  CHECK_EQ(snap.dma32_total_us, 5, "C dma32_total_us");
  CHECK_EQ(snap.dma_gamepak_pairs, 1, "C dma_gamepak_pairs");

  /* D: 32-bit ROM -> ROM 0x0A. Must count as gamepak pair. */
  setup_dma(&d, 0x08000040, 0x0A000040, 4, DMA_32BIT);
  host_stub_set_time(300, 309);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma32_transfers, 2, "D dma32_transfers");
  CHECK_EQ(snap.dma32_elements, 12, "D dma32_elements");
  CHECK_EQ(snap.dma32_bytes, 48, "D dma32_bytes");
  CHECK_EQ(snap.dma32_total_us, 14, "D dma32_total_us");
  CHECK_EQ(snap.dma_gamepak_pairs, 2, "D dma_gamepak_pairs");

  /* E: 32-bit EWRAM -> IWRAM. No gamepak side, no pair. */
  setup_dma(&d, 0x02000100, 0x03000100, 4, DMA_32BIT);
  host_stub_set_time(400, 403);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma32_transfers, 3, "E dma32_transfers");
  CHECK_EQ(snap.dma32_elements, 16, "E dma32_elements");
  CHECK_EQ(snap.dma32_bytes, 64, "E dma32_bytes");
  CHECK_EQ(snap.dma_gamepak_pairs, 2, "E dma_gamepak_pairs");

  /* F: 32-bit VRAM -> ROM. Only one gamepak side, no pair. */
  setup_dma(&d, 0x06000100, 0x08000100, 4, DMA_32BIT);
  host_stub_set_time(500, 504);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma32_transfers, 4, "F dma32_transfers");
  CHECK_EQ(snap.dma32_elements, 20, "F dma32_elements");
  CHECK_EQ(snap.dma_gamepak_pairs, 2, "F dma_gamepak_pairs");

  /* 0x0E is SRAM, outside the ROM regions. */
  setup_dma(&d, 0x0E000020, 0x08000020, 1, DMA_16BIT);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma_gamepak_pairs, 2, "SRAM is not a ROM mirror");

  /* G: monitor stopped -> no further records. */
  perf_monitor_stop(2000);
  CHECK_EQ(perf_monitor_active(), 0, "G monitor inactive");
  setup_dma(&d, 0x02000200, 0x03000200, 2, DMA_16BIT);
  host_stub_set_time(600, 612);
  dma_transfer(&d);
  perf_monitor_snapshot(&snap);
  CHECK_EQ(snap.dma16_transfers, 3, "G dma16_transfers unchanged");
  CHECK_EQ(snap.dma32_transfers, 4, "G dma32_transfers unchanged");
  CHECK_EQ(snap.dma_gamepak_pairs, 2, "G dma_gamepak_pairs unchanged");

  test_paged_rom_cache_and_io();

  if (failures != 0)
  {
    printf("test_perf_dma_probe: %d failure(s)\n", failures);
    return 1;
  }
  printf("test_perf_dma_probe: PASS\n");
  return 0;
}
