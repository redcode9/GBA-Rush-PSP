/*
 * Host test: the real memory.c save path, from load_gamepak() to the bytes
 * of the .sav. memory.c is compiled against the real PSP SDK headers (see
 * the Makefile target) and reads and writes the .sav through backup_io,
 * here the fake file system of tests/fake_store_io.c; host_dma_stubs.c
 * answers the yes/no prompts and gives the ROM file its size.
 *
 * Regressions it guards: a 64 KiB .sav of an SRAM game was written back as
 * one byte; a .sav that failed to read was replaced by a blank image on the
 * first flush; a .sav over 128 KiB overran the save buffer; a write that
 * failed was dropped as if it had been written.
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
int host_stub_save_write(uint8_t *flags, uint32_t values[3]);
void host_stub_set_time(uint64_t t0, uint64_t t1);

#define KIB 1024u
#define ROM_SIZE 0x8000
#define YES 0
#define NO 1

/* Frames update_backup() waits after the last game write; one more than
   memory.c's WRITE_BACKUP_DELAY so the periodic flush is due. */
#define FLUSH_FRAMES 11
/* Frames between a failed periodic write and the next try: memory.c's
   WRITE_BACKUP_RETRY_DELAY. */
#define RETRY_FRAMES 300

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

/* Calls update_backup() once a frame until a write of the .sav starts;
   returns the frames that took, or -1 if none started within limit. */
static int frames_to_next_write(int limit)
{
  int before = store.open_write_calls;
  int frame;

  for(frame = 1; frame <= limit; frame++)
  {
    update_backup();
    if(store.open_write_calls != before)
      return frame;
  }
  return -1;
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
  /* A write the load blocked counts as done: quit and the menu go on. */
  assert(update_backup_immediately());

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
   yet) is not a blank cartridge: it asks again, the runtime log shows it
   failed, and playing on without saving still leaves the file alone. */
static void test_retry_without_the_file_stays_blocked(void)
{
  static const unsigned answers[] = { YES, NO, YES };
  uint8_t flags;
  uint32_t values[3];
  int loads = host_stub_save_load(&flags, values);

  fresh_store();
  fake_store_seed(&store, 32 * KIB, 8);
  store.fail_reads = 1;
  store.hide_after_reads = 1;

  assert(load("SRAM_V113", answers, 3) == ROM_SIZE);
  assert(host_stub_save_load(&flags, values) == loads + 2);
  assert(flags == SAVE_STORE_FAILED && values[0] == 0);
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

/* A full Memory Stick: the periodic write fails, the old file stays and
   the write stays pending. The next try comes RETRY_FRAMES later however
   often the game writes meanwhile, and once there is room it writes
   everything the game wrote. Each write logs its result, duration, size
   and try number. */
static void test_failed_write_stays_pending(void)
{
  int frame;
  int writes;
  uint8_t flags;
  uint32_t values[3];

  fresh_store();
  fake_store_seed(&store, 32 * KIB, 10);
  assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
  writes = host_stub_save_write(&flags, values);

  store.space = 1;
  sram_write(0x10, 0x42);
  host_stub_set_time(1000, 1250);
  assert(frames_to_next_write(FLUSH_FRAMES) > 0);
  assert(fake_store_file_is(&store, "game.sav", 32 * KIB, 10));
  assert(host_stub_save_write(&flags, values) == writes + 1);
  assert(flags == SAVE_STORE_WRITE_FAILED && values[0] == 250);
  assert(values[1] == 32 * KIB && values[2] == 1);

  for(frame = 1; frame < RETRY_FRAMES; frame++)
  {
    if((frame % 50) == 0)
      sram_write(0x11, (u8)frame);
    update_backup();
  }
  assert(store.open_write_calls == 1);
  update_backup();
  assert(store.open_write_calls == 2);
  assert(fake_store_file_is(&store, "game.sav", 32 * KIB, 10));
  assert(host_stub_save_write(&flags, values) == writes + 2);
  assert(values[2] == 2);

  store.space = 0;
  assert(frames_to_next_write(RETRY_FRAMES) == RETRY_FRAMES);
  assert(store.size == 32 * KIB);
  assert(store.data[0x10] == 0x42 && store.data[0x11] == 250);
  assert(store.data[0x12] == fake_store_pattern(0x12, 10));
  assert(host_stub_save_write(&flags, values) == writes + 3);
  assert(flags == SAVE_STORE_OK && values[2] == 3);
  /* Written: nothing is pending any more. */
  assert(frames_to_next_write(2 * RETRY_FRAMES) < 0);
  assert(host_stub_save_write(&flags, values) == writes + 3);
  /* The next write is a first try again. */
  sram_write(0x12, 0x43);
  assert(frames_to_next_write(FLUSH_FRAMES) > 0);
  assert(host_stub_save_write(&flags, values) == writes + 4);
  assert(flags == SAVE_STORE_OK && values[2] == 1);
}

/* Only a game's first failed periodic write raises the warning. A game
   loaded again may warn again, and the write still pending from before
   is dropped with the old buffer; its next write is a first try. */
static void test_warning_once_per_game(void)
{
  uint8_t flags;
  uint32_t values[3];

  fresh_store();
  fake_store_seed(&store, 32 * KIB, 11);
  assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
  assert(!backup_write_warning);

  store.space = 1;
  sram_write(0x10, 0x42);
  assert(frames_to_next_write(FLUSH_FRAMES) > 0);
  assert(backup_write_warning);
  backup_write_warning = 0; /* the menu showed it */
  assert(frames_to_next_write(RETRY_FRAMES) == RETRY_FRAMES);
  assert(!backup_write_warning);

  assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
  assert(frames_to_next_write(2 * RETRY_FRAMES) < 0);
  assert(store.open_write_calls == 2);
  sram_write(0x10, 0x43);
  assert(frames_to_next_write(FLUSH_FRAMES) > 0);
  assert(backup_write_warning);
  host_stub_save_write(&flags, values);
  assert(flags != SAVE_STORE_OK && values[2] == 1);
}

/* The last tries before the game goes away (quit, menu suspend, ROM
   change) report a failed write and leave it pending, with no warning:
   the menu asks about it instead. */
static void test_final_write_reports_failure(void)
{
  fresh_store();
  fake_store_seed(&store, 32 * KIB, 12);
  assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
  assert(update_backup_immediately());
  assert(store.open_write_calls == 0);

  store.fail_move = 1;
  sram_write(0x10, 0x42);
  assert(!update_backup_immediately());
  assert(fake_store_file_is(&store, "game.sav", 32 * KIB, 12));
  assert(!backup_write_warning);

  assert(update_backup_immediately());
  assert(store.size == 32 * KIB && store.data[0x10] == 0x42);
  assert(update_backup_immediately() && store.open_write_calls == 2);

  /* A failed periodic write, then a menu write that succeeds: the next
     game write goes out after the usual delay, not the retry wait. */
  store.fail_move = store.move_calls + 1;
  sram_write(0x10, 0x43);
  assert(frames_to_next_write(FLUSH_FRAMES) > 0);
  assert(store.data[0x10] == 0x42);
  assert(update_backup_immediately() && store.data[0x10] == 0x43);
  sram_write(0x10, 0x44);
  assert(frames_to_next_write(FLUSH_FRAMES) > 0);
  assert(store.data[0x10] == 0x44);
}

/* The power goes off at each step of a periodic write in turn: after the
   next start the .sav is the old image or the new one, never another. */
static void test_cut_write_leaves_old_or_new(void)
{
  int cut;
  int olds = 0;
  int news = 0;
  int finished = 0;

  for(cut = 1; !finished; cut++)
  {
    int frame;
    size_t i;

    assert(cut < 100);
    fresh_store();
    fake_store_seed(&store, 32 * KIB, 13);
    assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
    store.cut_after = store.calls + cut;
    sram_write(0x10, 0x42);
    for(frame = 0; frame < FLUSH_FRAMES; frame++)
      update_backup();
    finished = !store.power_off;

    fake_store_restart(&store);
    assert(load("SRAM_V113", NULL, 0) == ROM_SIZE);
    assert(store.present && store.size == 32 * KIB);
    for(i = 0; i < 32 * KIB; i++)
      assert(i == 0x10 || store.data[i] == fake_store_pattern(i, 13));
    if(store.data[0x10] == 0x42)
      news++;
    else
    {
      assert(store.data[0x10] == fake_store_pattern(0x10, 13));
      olds++;
    }
  }
  assert(olds > 0 && news > 0);
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
  test_failed_write_stays_pending();
  test_warning_once_per_game();
  test_final_write_reports_failure();
  test_cut_write_leaves_old_or_new();
  puts("memory backup tests passed");
  return 0;
}
