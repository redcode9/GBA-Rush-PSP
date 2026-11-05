/*
 * Host test: the EEPROM serial protocol through the real memory.c, driven
 * the way a game drives it. The bits sit in EWRAM, one per halfword, and
 * DMA3, programmed through its registers, carries them to region 0xD
 * (write_eeprom) and back (read_eeprom). memory.c is built as for
 * test_memory_backup (see the Makefile target): the cartridge is an
 * EEPROM-tagged ROM, its .sav lives in tests/fake_store_io.c and each
 * case flushes it there.
 *
 * Regression it guards: an 8 KiB chip whose game wrote before it ever read
 * stayed at 512 B, so the 81-unit write took a 6-bit block number, landed
 * on the wrong block and left the protocol out of step.
 *
 * memory.c keeps its state from one case to the next (eeprom_size is a
 * global only a load resets), so the cases run in a fixed order.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "host_shim/gba_types.h"
#include "cpu.h"
#include "memory.h"
#include "fake_store_io.h"

void host_stub_file_length(int length);
void host_stub_yesno(const unsigned *answers, int count);

#define KIB 1024u
#define ROM_SIZE 0x8000
#define BITS_OUT 0x02000000u /* the bits a DMA sends to the chip */
#define BITS_IN  0x02001000u /* the bits a read brings back */
#define EEPROM   0x0D000000u
#define SEED     0x5A

static FakeStore store;
static SaveStoreIo io;

static const u8 data[8] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };

/* Loads an EEPROM-tagged ROM over a sav_size-byte .sav seeded with SEED
   (none if 0); either way a 512 B chip until a request says otherwise.
   Then resets memory and DMA3 as a GBA reset does. */
static void insert_cartridge(u32 sav_size)
{
  fake_store_init(&store, &io, "game.sav");
  if(sav_size != 0)
    fake_store_seed(&store, sav_size, SEED);
  backup_io = &io;
  memset(gamepak_rom, 0, ROM_SIZE);
  memcpy(gamepak_rom + 0x4000, "EEPROM_V124", 11);
  host_stub_yesno(NULL, 0);
  assert(load_gamepak("game.gba") == ROM_SIZE);
  init_memory();
  dma[3].start_type = DMA_INACTIVE;
}

/* An immediate 16-bit DMA3, both addresses incrementing. */
static void dma3(u32 source, u32 dest, u32 units)
{
  write_memory32(0x040000D4, source);
  write_memory32(0x040000D8, dest);
  write_memory16(0x040000DC, (u16)units);
  write_memory16(0x040000DE, 0x8000);
}

/* Puts count (at most 32) bits of value, most significant first, at bit
   at of BITS_OUT; returns the bit after them. */
static u32 put_bits(u32 at, u32 value, u32 count)
{
  while(count-- > 0)
    write_memory16(BITS_OUT + at++ * 2, (value >> count) & 1);
  return at;
}

/* Writes data to block: command 10, the block number, 64 data bits and
   the stop bit, 73 units at 512 B and 81 at 8 KiB. */
static void eeprom_write(u32 block, u32 address_bits)
{
  u32 units = put_bits(put_bits(0, 2, 2), block, address_bits);
  int i;

  for(i = 0; i < 8; i++)
    units = put_bits(units, data[i], 8);
  dma3(BITS_OUT, EEPROM, put_bits(units, 0, 1));
}

/* Sends a units-long read request for block (command 11, the block
   number, then zeros: the stop bit and any padding), then reads 68 units
   back: 4 dummy bits and the 64 data bits, returned in out. */
static void eeprom_read(u32 block, u32 address_bits, u32 units, u8 *out)
{
  u32 sent = put_bits(put_bits(0, 3, 2), block, address_bits);
  int i;

  while(sent < units)
    sent = put_bits(sent, 0, 1);
  dma3(BITS_OUT, EEPROM, units);
  dma3(EEPROM, BITS_IN, 68);
  memset(out, 0, 8);
  for(i = 0; i < 64; i++)
    out[i / 8] |= (read_memory16(BITS_IN + (4 + i) * 2) & 1) << (7 - i % 8);
}

/* Flushes the save: the .sav must be size bytes, with data at block. */
static void assert_saved(u32 size, u32 block)
{
  update_backup_immediately();
  assert(store.write_calls == 1);
  assert(store.size == size);
  assert(memcmp(store.data + block * 8, data, 8) == 0);
}

/* (a) 512 B chip: a 73-unit write and a 9-unit read of block 0x2A. The
   data comes back and the chip stays 512 B. */
static void test_512_write_then_read(void)
{
  u8 got[8];

  insert_cartridge(0);
  eeprom_write(0x2A, 6);
  eeprom_read(0x2A, 6, 9, got);
  assert(memcmp(got, data, 8) == 0);
  assert_saved(512, 0x2A);
}

/* (b) 8 KiB chip on a new save: an 81-unit write of block 0x2AB before
   any read, then a 17-unit read. Fails on 1.4.2, where only a 17-unit DMA
   made the chip 8 KiB: the write took a 6-bit block number. */
static void test_8k_write_before_read(void)
{
  u8 got[8];

  insert_cartridge(0);
  eeprom_write(0x2AB, 14);
  eeprom_read(0x2AB, 14, 17, got);
  assert(memcmp(got, data, 8) == 0);
  assert_saved(8 * KIB, 0x2AB);
}

/* (c) 8 KiB chip read first, over a 4 KiB .sav (which loads as 512 B):
   the 17-unit read of block 0x155 returns that block's bytes, not those
   of the 6-bit block its first bits would name at 512 B. Then an 81-unit
   write and a read back. */
static void test_8k_read_then_write(void)
{
  u8 got[8];
  int i;

  insert_cartridge(4 * KIB);
  eeprom_read(0x155, 14, 17, got);
  for(i = 0; i < 8; i++)
    assert(got[i] == fake_store_pattern(0x155 * 8 + i, SEED));
  eeprom_write(0x155, 14);
  eeprom_read(0x155, 14, 17, got);
  assert(memcmp(got, data, 8) == 0);
  assert_saved(8 * KIB, 0x155);
}

/* (d) 512 B again (the load puts eeprom_size back): a 49-unit DMA, here
   the 9-unit read request of block 0x15 padded with 40 zeros that the
   chip ignores until the read, keeps the chip 512 B. A rule on the low 5
   bits of the count (49 & 0x1F is 17) would make it 8 KiB, and the read
   would take a 14-bit block number and miss the block. */
static void test_49_units_do_not_promote(void)
{
  u8 got[8];

  insert_cartridge(0);
  eeprom_write(0x15, 6);
  eeprom_read(0x15, 6, 49, got);
  assert(memcmp(got, data, 8) == 0);
  assert_saved(512, 0x15);
}

int main(void)
{
  init_gamepak_buffer();
  host_stub_file_length(ROM_SIZE);

  test_512_write_then_read();
  test_8k_write_before_read();
  test_8k_read_then_write();
  test_49_units_do_not_promote();
  puts("EEPROM tests passed");
  return 0;
}
