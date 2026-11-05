/*
 * Host test: the real memory.c save path, from load_gamepak() to the bytes
 * of the .sav. memory.c is compiled against the real PSP SDK headers (see
 * the Makefile target) and reads and writes the .sav through backup_io,
 * here the fake file system of tests/fake_store_io.c; host_dma_stubs.c
 * answers the yes/no prompts and gives the ROM file its size.
 *
 * Regressions it guards: a 64 KiB .sav of an SRAM game was written back as
 * one byte; a .sav that failed to read was replaced by a blank image on the
 * first flush; a .sav over 128 KiB overran the save buffer.
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
int host_stub_yesno_asked(void);
int host_stub_yesno_saw_rom(void);
int host_stub_save_load(uint8_t *flags, uint32_t values[3]);

#define KIB 1024u
#define ROM_SIZE 0x8000
#define YES 0
#define NO 1

/* Frames update_backup() waits after the last game write; one more than
   memory.c's WRITE_BACKUP_DELAY so the periodic flush is due. */
#define FLUSH_FRAMES 11

static FakeStore store;
static SaveStoreIo io;

/* A one-page ROM whose save library string is tag (none if NULL). */
static void make_rom(const char *tag)
{
  memset(gamepak_rom, 0, ROM_SIZE);
  if(tag != NULL)
    memcpy(gamepak_rom + 0x4000, tag, strlen(tag));
}

/* Loads game.gba with the given prompt answers; returns load_gamepak()'s
   result. The .sav is "game.sav" in the empty dir_save. */
static s32 load(const char *tag, const unsigned *answers, int count)
{
  make_rom(tag);
  host_stub_yesno(answers, count);
  return load_gamepak("game.gba");
}

static void fresh_store(void)
{
  fake_store_init(&store, &io, "game.sav");
  backup_io = &io;
}

/* The game writes one SRAM byte, as through region 0xE. */
static void sram_write(u32 address, u8 value)
{
  write_backup(address, value);
}

/* The Flash byte-program sequence: 0xAA, 0x55, 0xA0, then the byte. */
static void flash_write(u32 address, u8 value)
{
  write_backup(0x5555, 0xAA);
  write_backup(0x2AAA, 0x55);
  write_backup(0x5555, 0xA0);
  write_backup(address, value);
}

/* The bug: SRAM tag and a 64 KiB file. The game writes below 0x8000 and
   the .sav must come back 65536 bytes long, with the upper half intact. */
static void test_sram_64k_file_keeps_its_size(void)
{
  fresh_store();
  fake_store_seed(&store, 64 * KIB, 1);

  assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
  assert(host_stub_yesno_asked() == 0);
  sram_write(0x10, 0x42);
  update_backup_immediately();

  assert(store.write_calls == 1);
  assert(store.size == 65536);
  assert(store.data[0x10] == 0x42);
  assert(store.data[0x9000] == fake_store_pattern(0x9000, 1));
}

/* No tag either: the 64 KiB file still keeps its size once the game picks
   SRAM, here through the periodic flush. */
static void test_untagged_64k_file_keeps_its_size(void)
{
  int frame;

  fresh_store();
  fake_store_seed(&store, 64 * KIB, 2);

  assert(load(NULL, NULL, 0) == ROM_SIZE);
  sram_write(0x20, 0x24);
  for(frame = 0; frame < FLUSH_FRAMES; frame++)
    update_backup();

  assert(store.write_calls == 1);
  assert(store.size == 65536);
  assert(store.data[0x20] == 0x24);
}

/* A missing .sav is a blank cartridge: no prompt, and saving works. */
static void test_missing_save_is_a_blank_cartridge(void)
{
  fresh_store();

  assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
  assert(host_stub_yesno_asked() == 0);
  sram_write(0x00, 0x99);
  update_backup_immediately();

  assert(store.write_calls == 1 && store.size == 32 * KIB);
  assert(store.data[0] == 0x99 && store.data[1] == 0xFF);
}

/* Unreadable .sav, no retry, play without saving: the game runs, and no
   periodic flush, ROM change, menu suspend or quit (all
   update_backup_immediately()) writes the file. While the prompts are up
   no ROM counts as loaded, so a suspend there saves no AUTO state for a
   machine not yet reset. */
static void test_unreadable_save_blocks_every_write(void)
{
  static const unsigned answers[] = { NO, YES };
  int frame;

  fresh_store();
  fake_store_seed(&store, 32 * KIB, 3);
  store.fail_open = 1;

  assert(load("SRAM_V113", answers, 2) == ROM_SIZE);
  assert(host_stub_yesno_asked() == 2);
  assert(!host_stub_yesno_saw_rom());
  assert(strcmp(gamepak_filename, "game.gba") == 0);
  sram_write(0x10, 0x42);
  for(frame = 0; frame < FLUSH_FRAMES; frame++)
    update_backup();
  sram_write(0x11, 0x43);
  update_backup_immediately();

  assert(store.write_calls == 0);
  assert(store.size == 32 * KIB);
  assert(store.data[0x10] == fake_store_pattern(0x10, 3));
}

/* Retry reads the file again; once it reads, saving works again. The
   runtime log has both loads. */
static void test_retry_reads_the_save_again(void)
{
  static const unsigned answers[] = { YES };
  uint8_t flags;
  uint32_t values[3];
  int loads = host_stub_save_load(&flags, values);

  fresh_store();
  fake_store_seed(&store, 32 * KIB, 4);
  store.fail_reads = 1;

  assert(load("SRAM_V113", answers, 1) == ROM_SIZE);
  assert(host_stub_save_load(&flags, values) == loads + 2);
  assert(flags == SAVE_STORE_OK && values[0] == 32 * KIB &&
         values[1] == 32 * KIB &&
         values[2] == (uint32_t)(SAVE_TYPE_SRAM | (SAVE_TAG_SRAM << 8)));
  assert(host_stub_yesno_asked() == 1);
  assert(!host_stub_yesno_saw_rom());
  assert(strcmp(gamepak_filename, "game.gba") == 0);
  assert(store.read_calls == 2);
  assert(read_backup(0x30) == (u32)fake_store_pattern(0x30, 4));

  sram_write(0x30, 0x77);
  update_backup_immediately();
  assert(store.write_calls == 1 && store.data[0x30] == 0x77);
}

/* A retry that no longer finds the file (the Memory Stick is not back
   yet) is not a blank cartridge: it asks again, and playing on without
   saving still leaves the file alone. */
static void test_retry_without_the_file_stays_blocked(void)
{
  static const unsigned answers[] = { YES, NO, YES };

  fresh_store();
  fake_store_seed(&store, 32 * KIB, 8);
  store.fail_reads = 1;
  store.hide_after_reads = 1;

  assert(load("SRAM_V113", answers, 3) == ROM_SIZE);
  assert(host_stub_yesno_asked() == 3);
  assert(!host_stub_yesno_saw_rom());
  assert(strcmp(gamepak_filename, "game.gba") == 0);
  sram_write(0x10, 0x42);
  update_backup_immediately();
  assert(store.write_calls == 0);
}

/* A write cut between its two renames left only the .bak and the verified
   .tmp. The load recovers them before reading; a recovery that fails is
   an unreadable save, and retrying finishes it and loads the new image. */
static void test_cut_write_is_recovered_at_load(void)
{
  static const unsigned answers[] = { YES };

  fresh_store();
  fake_store_seed_file(&store, "game.sav.bak", 32 * KIB, 1);
  fake_store_seed_file(&store, "game.sav.tmp", 32 * KIB, 9);
  store.fail_move = 1;

  assert(load("SRAM_V113", answers, 1) == ROM_SIZE);
  assert(host_stub_yesno_asked() == 1);
  assert(store.move_calls == 2);
  assert(read_backup(0x30) == (u32)fake_store_pattern(0x30, 9));
  assert(fake_store_file_size(&store, "game.sav.tmp") < 0);
  assert(fake_store_file_is(&store, "game.sav.bak", 32 * KIB, 1));
}

/* Refusing both prompts cancels the load: no ROM stays loaded, and nothing
   writes the file. */
static void test_refusing_both_prompts_cancels(void)
{
  static const unsigned answers[] = { NO, NO };

  fresh_store();
  fake_store_seed(&store, 32 * KIB, 5);
  store.fail_open = 1;

  assert(load("SRAM_V113", answers, 2) == LOAD_GAMEPAK_CANCELLED);
  assert(gamepak_filename[0] == '\0' && gamepak_file_size == 0);
  sram_write(0x10, 0x42);
  update_backup_immediately();
  assert(store.write_calls == 0);
}

/* 200 KiB for a 128 KiB Flash game: the first 128 KiB load (ASan catches
   any write past the buffer) and the file is written back at 128 KiB. */
static void test_oversized_save_loads_a_prefix(void)
{
  fresh_store();
  fake_store_seed(&store, 200 * KIB, 6);

  assert(load("FLASH1M_V103", NULL, 0) == ROM_SIZE);
  assert(store.last_capacity == 128 * KIB);
  flash_write(0x10, 0x42);
  update_backup_immediately();

  assert(store.write_calls == 1);
  assert(store.size == 128 * KIB);
  assert(store.data[0x10] == 0x42);
  assert(store.data[0x1FFFF] == fake_store_pattern(0x1FFFF, 6));
}

/* The tag beats the file size: a 128 KiB image of a 64 KiB Flash game
   goes back at the chip's 64 KiB, the runtime log shows the mismatch, and
   the 128 KiB file is kept as the .orig. */
static void test_tag_beats_file_size(void)
{
  uint8_t flags;
  uint32_t values[3];

  fresh_store();
  fake_store_seed(&store, 128 * KIB, 7);

  assert(load("FLASH512_V131", NULL, 0) == ROM_SIZE);
  host_stub_save_load(&flags, values);
  assert(flags == SAVE_STORE_OK && values[0] == 128 * KIB &&
         values[1] == 64 * KIB &&
         values[2] == (SAVE_TYPE_FLASH | (SAVE_TAG_FLASH64 << 8) | (1u << 16)));
  flash_write(0x10, 0x42);
  update_backup_immediately();

  assert(store.write_calls == 1);
  assert(store.size == 64 * KIB);
  assert(store.data[0x10] == 0x42);
  assert(store.data[0x8000] == fake_store_pattern(0x8000, 7));
  assert(fake_store_file_is(&store, "game.sav.orig", 128 * KIB, 7));
}

int main(void)
{
  /* Callers tell a cancel from a load error, which is -1 or -2. */
  assert(LOAD_GAMEPAK_CANCELLED != -1 && LOAD_GAMEPAK_CANCELLED != -2);

  init_gamepak_buffer();
  host_stub_file_length(ROM_SIZE);

  test_sram_64k_file_keeps_its_size();
  test_untagged_64k_file_keeps_its_size();
  test_missing_save_is_a_blank_cartridge();
  test_unreadable_save_blocks_every_write();
  test_retry_reads_the_save_again();
  test_retry_without_the_file_stays_blocked();
  test_cut_write_is_recovered_at_load();
  test_refusing_both_prompts_cancels();
  test_oversized_save_loads_a_prefix();
  test_tag_beats_file_size();
  puts("memory backup tests passed");
  return 0;
}
