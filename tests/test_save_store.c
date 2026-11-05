/*
 * Host tests for save_store.c, the .sav load and write: a missing or empty
 * file is a blank cartridge, a file that exists but cannot be read is a
 * failure (never mistaken for a blank cartridge), and a file larger than
 * the buffer loads its first part only. A write goes through a verified
 * .tmp and keeps the old file as the .bak: a power cut after any step,
 * then a restart (recovery and load), gives the old image or the new one,
 * and each fault gives its own result with the old or new image left.
 * Each fault switch of the fake file system (fake_store_io.c) is shown to
 * take effect. The pre-link copies keep three generations, the newest in
 * .link1; a power cut or a failed rename loses at most the oldest, and
 * never leaves a partial .link1.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fake_store_io.h"
#include "save_store.h"

#define KIB 1024u
#define CAPACITY (128 * KIB)
#define CANARY 0xA5

#define SAVE_DIR "ms0:/SAVE/"
#define NAME "game.sav"
#define PRIMARY SAVE_DIR NAME
#define TMP PRIMARY ".tmp"
#define BAK PRIMARY ".bak"

/* The images the write tests use: the file before the write, the one
   written, and an older .bak. */
#define OLD 1
#define OLD_SIZE 1000u
#define NEW 2
#define NEW_SIZE 1200u
#define ANCIENT 9
#define ANCIENT_SIZE 900u
#define CHUNK 400u

/* The pre-link generations. A seeded ring of n holds seeds n (in .link1)
   down to 1; a push adds PUSHED. */
#define LINK1 PRIMARY ".link1"
#define LINK2 PRIMARY ".link2"
#define LINK3 PRIMARY ".link3"
#define LINK_SIZE 300u
#define PUSHED 20

static const char *const links[] = { LINK1, LINK2, LINK3 };

/* What is on the Memory Stick before a write. */
enum
{
  NO_FILE,
  ONLY_FILE,
  FILE_AND_BACKUP
};

static FakeStore store;
static SaveStoreIo io;
static SaveStoreWrite transaction;
/* The load buffer plus a tail that must stay untouched. */
static uint8_t buffer[CAPACITY + 16];
static uint8_t source[512 * KIB];

static void reset(void)
{
  fake_store_init(&store, &io, PRIMARY);
  memset(buffer, CANARY, sizeof(buffer));
}

static SaveStoreResult load(size_t *file_size)
{
  return save_store_load(&io, SAVE_DIR, NAME, buffer, CAPACITY, file_size);
}

static int tail_untouched(void)
{
  size_t i;

  for(i = CAPACITY; i < sizeof(buffer); i++)
  {
    if(buffer[i] != CANARY)
      return 0;
  }
  return 1;
}

static void fill_source(size_t size, uint8_t seed)
{
  size_t i;

  for(i = 0; i < size; i++)
    source[i] = fake_store_pattern(i, seed);
}

static void seed_state(int state)
{
  reset();
  if(state != NO_FILE)
    fake_store_seed(&store, OLD_SIZE, OLD);
  if(state == FILE_AND_BACKUP)
    fake_store_seed_file(&store, BAK, ANCIENT_SIZE, ANCIENT);
}

/* The new image in CHUNK-byte appends, so a cut can fall between them. */
static SaveStoreResult write_new_in_chunks(void)
{
  size_t offset;

  fill_source(NEW_SIZE, NEW);
  save_store_begin(&transaction, &io, SAVE_DIR, NAME);
  for(offset = 0; offset < NEW_SIZE; offset += CHUNK)
    save_store_append(&transaction, source + offset, CHUNK);
  return save_store_commit(&transaction);
}

/* The load buffer holds size bytes of seed's pattern. */
static int loaded(size_t file_size, size_t size, uint8_t seed)
{
  size_t i;

  if(file_size != size)
    return 0;
  for(i = 0; i < size; i++)
  {
    if(buffer[i] != fake_store_pattern(i, seed))
      return 0;
  }
  return 1;
}

/* The next start of the emulator: recovery, then the load. Returns NEW or
   OLD for the image it finds, 0 for a blank cartridge. No .tmp is left,
   and the next write works. */
static int restart(void)
{
  size_t file_size = 0;
  SaveStoreResult result;
  int image;

  fake_store_restart(&store);
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_OK);
  assert(fake_store_file_size(&store, TMP) < 0);
  result = load(&file_size);
  if(result == SAVE_STORE_NOT_FOUND)
    image = 0;
  else
  {
    assert(result == SAVE_STORE_OK);
    if(loaded(file_size, NEW_SIZE, NEW))
      image = NEW;
    else
    {
      assert(loaded(file_size, OLD_SIZE, OLD));
      image = OLD;
    }
  }

  fill_source(NEW_SIZE, NEW);
  assert(save_store_write(&io, SAVE_DIR, NAME, source, NEW_SIZE) ==
         SAVE_STORE_OK);
  assert(fake_store_file_is(&store, PRIMARY, NEW_SIZE, NEW));
  return image;
}

static void test_missing_and_empty_files_are_blank(void)
{
  size_t file_size = 1;

  reset();
  assert(load(&file_size) == SAVE_STORE_NOT_FOUND);
  assert(file_size == 0 && store.read_calls == 0);

  reset();
  store.present = 1;
  assert(load(&file_size) == SAVE_STORE_NOT_FOUND);
  assert(file_size == 0 && store.read_calls == 1);
}

static void test_whole_file_loads(void)
{
  size_t file_size = 0;
  size_t i;

  reset();
  fake_store_seed(&store, 32 * KIB, 3);
  assert(load(&file_size) == SAVE_STORE_OK);
  assert(file_size == 32 * KIB);
  for(i = 0; i < 32 * KIB; i++)
    assert(buffer[i] == fake_store_pattern(i, 3));
  assert(buffer[32 * KIB] == CANARY);
}

/* A file that is there but does not read is never a blank cartridge. */
static void test_unreadable_file_fails(void)
{
  size_t file_size = 1;

  reset();
  fake_store_seed(&store, 64 * KIB, 1);
  store.fail_open = 1;
  assert(load(&file_size) == SAVE_STORE_FAILED);
  assert(file_size == 0);

  reset();
  fake_store_seed(&store, 64 * KIB, 1);
  store.fail_reads = 1;
  assert(load(&file_size) == SAVE_STORE_FAILED);
  assert(load(&file_size) == SAVE_STORE_OK);
  assert(file_size == 64 * KIB && store.read_calls == 2);

  reset();
  fake_store_seed(&store, 64 * KIB, 1);
  store.short_read = 100;
  assert(load(&file_size) == SAVE_STORE_FAILED);
  assert(file_size == 0);
}

/* 200 KiB loads its first 128 KiB and reports the real size. */
static void test_oversized_file_loads_a_prefix(void)
{
  size_t file_size = 0;

  reset();
  fake_store_seed(&store, 200 * KIB, 9);
  assert(load(&file_size) == SAVE_STORE_OK);
  assert(file_size == 200 * KIB);
  assert(store.last_capacity == CAPACITY);
  assert(buffer[CAPACITY - 1] == fake_store_pattern(CAPACITY - 1, 9));
  assert(tail_untouched());
}

/* A path that does not fit is refused before any I/O, not cut short;
   for a write, the .tmp and .bak names must fit too. */
static void test_long_path_fails_without_io(void)
{
  char name[SAVE_STORE_PATH_MAX];
  size_t file_size = 1;
  uint8_t data[4] = { 1, 2, 3, 4 };

  reset();
  memset(name, 'a', sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  assert(save_store_load(&io, "ms0:/", name, buffer, CAPACITY, &file_size) ==
         SAVE_STORE_FAILED);
  assert(file_size == 0);
  assert(save_store_recover(&io, "ms0:/", name) == SAVE_STORE_FAILED);
  assert(save_store_write(&io, "ms0:/", name, data, sizeof(data)) ==
         SAVE_STORE_FAILED);
  assert(store.calls == 0);

  /* "ms0:/" plus 506 characters fits, with ".tmp" it does not. */
  name[SAVE_STORE_PATH_MAX - 1 - 5] = '\0';
  assert(save_store_load(&io, "ms0:/", name, buffer, CAPACITY, &file_size) ==
         SAVE_STORE_NOT_FOUND);
  assert(store.calls == 1);
  assert(save_store_write(&io, "ms0:/", name, data, sizeof(data)) ==
         SAVE_STORE_FAILED);
  assert(store.calls == 1);

  /* ".link1" fits after 500 characters, ".link1.tmp" does not: a push
     that could not write .link1 rotates nothing. */
  name[500] = '\0';
  assert(save_store_ring_push(&io, "ms0:/", name, data, sizeof(data)) ==
         SAVE_STORE_FAILED);
  assert(store.calls == 1);
}

/* Loading reads only: no write, rename or delete. */
static void test_load_never_writes(void)
{
  size_t file_size;

  reset();
  fake_store_seed(&store, 32 * KIB, 5);
  fake_store_seed_file(&store, TMP, 32 * KIB, 6);
  load(&file_size);
  store.fail_open = 1;
  load(&file_size);
  assert(store.read_calls == 2);
  assert(store.open_write_calls == 0 && store.move_calls == 0 &&
         store.remove_calls == 0);
  assert(fake_store_file_is(&store, TMP, 32 * KIB, 6));
}

/* Each write leaves the new image under the file's name and the one it
   replaced as the .bak, with no .tmp. */
static void test_write_keeps_the_old_image_as_backup(void)
{
  reset();
  fill_source(OLD_SIZE, OLD);
  assert(save_store_write(&io, SAVE_DIR, NAME, source, OLD_SIZE) ==
         SAVE_STORE_OK);
  assert(fake_store_file_is(&store, PRIMARY, OLD_SIZE, OLD));
  assert(fake_store_file_size(&store, BAK) < 0);
  assert(fake_store_file_size(&store, TMP) < 0);
  assert(store.flush_calls == 2 && store.crc32_calls == 1);

  assert(write_new_in_chunks() == SAVE_STORE_OK);
  assert(fake_store_file_is(&store, PRIMARY, NEW_SIZE, NEW));
  assert(fake_store_file_is(&store, BAK, OLD_SIZE, OLD));
  assert(fake_store_file_size(&store, TMP) < 0);

  fill_source(ANCIENT_SIZE, ANCIENT);
  assert(save_store_write(&io, SAVE_DIR, NAME, source, ANCIENT_SIZE) ==
         SAVE_STORE_OK);
  assert(fake_store_file_is(&store, PRIMARY, ANCIENT_SIZE, ANCIENT));
  assert(fake_store_file_is(&store, BAK, NEW_SIZE, NEW));
  assert(store.open_file == -1);
}

/* The recovery table, one row per state a cut write can leave. */
static void test_recovery(void)
{
  /* File and .tmp: the .tmp goes. */
  seed_state(ONLY_FILE);
  fake_store_seed_file(&store, TMP, NEW_SIZE, NEW);
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_OK);
  assert(fake_store_file_is(&store, PRIMARY, OLD_SIZE, OLD));
  assert(fake_store_file_size(&store, TMP) < 0);

  /* .bak and .tmp: the .tmp, verified before the file moved, is
     promoted. */
  reset();
  fake_store_seed_file(&store, BAK, OLD_SIZE, OLD);
  fake_store_seed_file(&store, TMP, NEW_SIZE, NEW);
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_OK);
  assert(fake_store_file_is(&store, PRIMARY, NEW_SIZE, NEW));
  assert(fake_store_file_is(&store, BAK, OLD_SIZE, OLD));
  assert(fake_store_file_size(&store, TMP) < 0);

  /* .bak alone comes back. */
  reset();
  fake_store_seed_file(&store, BAK, OLD_SIZE, OLD);
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_OK);
  assert(fake_store_file_is(&store, PRIMARY, OLD_SIZE, OLD));
  assert(fake_store_file_size(&store, BAK) < 0);

  /* .tmp alone was never verified: it goes, a blank cartridge. */
  reset();
  fake_store_seed_file(&store, TMP, NEW_SIZE, NEW);
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_OK);
  assert(store.calls == 3 && fake_store_file_size(&store, TMP) < 0);
  assert(restart() == 0);

  /* Nothing at all: nothing to do. */
  reset();
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_OK);
  assert(store.move_calls == 0);

  /* A rename or delete that fails, or one the names do not confirm, is
     a failed recovery, and the files stay for the next try. */
  reset();
  fake_store_seed_file(&store, BAK, OLD_SIZE, OLD);
  fake_store_seed_file(&store, TMP, NEW_SIZE, NEW);
  store.fail_move = 1;
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_FAILED);
  store.move_lies_ok = 2;
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_FAILED);
  assert(store.move_calls == 2);
  assert(fake_store_file_is(&store, TMP, NEW_SIZE, NEW));
  assert(restart() == NEW);

  seed_state(ONLY_FILE);
  fake_store_seed_file(&store, TMP, NEW_SIZE, NEW);
  store.fail_remove = 1;
  assert(save_store_recover(&io, SAVE_DIR, NAME) == SAVE_STORE_FAILED);
  assert(store.remove_calls == 1);
  assert(restart() == OLD);
}

/* Cuts the power after call cut of the write over state, and if
   recovery_cut is nonzero after that many calls of the first recovery
   too, then restarts. Returns 0 if the write ended before its cut, -1 if
   the recovery did, else 1. */
static int cut_and_restart(int state, int cut, int recovery_cut)
{
  SaveStoreResult result;
  int image;

  seed_state(state);
  store.cut_after = cut;
  result = write_new_in_chunks();
  if(!store.power_off)
  {
    assert(result == SAVE_STORE_OK);
    assert(fake_store_file_is(&store, PRIMARY, NEW_SIZE, NEW));
    return 0;
  }
  if(recovery_cut != 0)
  {
    fake_store_restart(&store);
    store.cut_after = store.calls + recovery_cut;
    save_store_recover(&io, SAVE_DIR, NAME);
    if(!store.power_off)
      return -1;
  }
  image = restart();
  assert(image == NEW || image == (state == NO_FILE ? 0 : OLD));
  return 1;
}

/* For every call k of a write, from each starting state: the power goes
   off after call k (and again during the recovery that follows), and the
   next start finds the old image or the new one, never a blank cartridge
   where there was a save. */
static void test_power_cut_at_every_step(void)
{
  int state;
  int cut;
  int recovery_cut;
  int recoveries_cut = 0;

  for(state = NO_FILE; state <= FILE_AND_BACKUP; state++)
  {
    for(cut = 1; cut_and_restart(state, cut, 0) != 0; cut++)
    {
      for(recovery_cut = 1; cut_and_restart(state, cut, recovery_cut) > 0;
       recovery_cut++)
        recoveries_cut++;
    }
    /* Recovery, open, 3 appends, close, flush, CRC, rename(s), checks. */
    assert(cut > (state == NO_FILE ? 12 : 18));
  }
  assert(recoveries_cut > 0);
}

typedef struct
{
  int had_file;        /* a save was there before the write */
  int *fault;          /* the fake's switch */
  int call;            /* the call it hits */
  int *also;           /* a second switch, or NULL */
  int also_call;
  int *calls;          /* the counter that shows fault was reached */
  SaveStoreResult result;
  int image;           /* what the next start finds: OLD, NEW or 0 */
} Fault;

/* Every write fault, with the result it gives and the image left. With a
   save there, remove call 1 is the recovery's, 2 the old .bak's, 3 the
   restore's; move call 1 makes the .bak, 2 promotes the .tmp, 3
   restores; flush call 1 syncs the .tmp, 2 the promoted file. */
static const Fault faults[] =
{
  { 1, &store.fail_open_write, 1, NULL, 0, &store.open_write_calls,
    SAVE_STORE_WRITE_FAILED, OLD },
  { 1, &store.fail_close, 1, NULL, 0, &store.close_calls,
    SAVE_STORE_WRITE_FAILED, OLD },
  { 1, &store.fail_close, 1, &store.lose_on_close, 1, &store.close_calls,
    SAVE_STORE_WRITE_FAILED, OLD },
  { 1, &store.fail_flush, 1, NULL, 0, &store.flush_calls,
    SAVE_STORE_WRITE_FAILED, OLD },
  { 1, &store.lose_on_close, 1, NULL, 0, &store.close_calls,
    SAVE_STORE_VERIFY_FAILED, OLD },
  { 1, &store.corrupt_on_close, 1, NULL, 0, &store.close_calls,
    SAVE_STORE_VERIFY_FAILED, OLD },
  { 1, &store.fail_crc32, 1, NULL, 0, &store.crc32_calls,
    SAVE_STORE_VERIFY_FAILED, OLD },
  { 1, &store.fail_remove, 1, NULL, 0, &store.remove_calls,
    SAVE_STORE_FAILED, OLD },
  { 1, &store.fail_remove, 2, NULL, 0, &store.remove_calls,
    SAVE_STORE_BACKUP_FAILED, OLD },
  { 1, &store.fail_move, 1, NULL, 0, &store.move_calls,
    SAVE_STORE_BACKUP_FAILED, OLD },
  { 1, &store.move_lies_ok, 1, NULL, 0, &store.move_calls,
    SAVE_STORE_BACKUP_FAILED, OLD },
  { 1, &store.move_lies_fail, 1, NULL, 0, &store.move_calls,
    SAVE_STORE_BACKUP_FAILED, OLD },
  { 1, &store.fail_move, 2, NULL, 0, &store.move_calls,
    SAVE_STORE_PROMOTE_FAILED, OLD },
  { 1, &store.move_lies_ok, 2, NULL, 0, &store.move_calls,
    SAVE_STORE_PROMOTE_FAILED, OLD },
  { 1, &store.move_lies_fail, 2, NULL, 0, &store.move_calls,
    SAVE_STORE_PROMOTE_FAILED, OLD },
  { 1, &store.fail_flush, 2, NULL, 0, &store.flush_calls,
    SAVE_STORE_PROMOTE_FAILED, OLD },
  /* The old file would not go back: the verified .tmp or the promoted
     file is there for the next start. */
  { 1, &store.fail_move, 2, &store.fail_remove, 3, &store.remove_calls,
    SAVE_STORE_RESTORE_FAILED, NEW },
  { 1, &store.move_lies_fail, 2, &store.fail_remove, 3, &store.remove_calls,
    SAVE_STORE_RESTORE_FAILED, NEW },
  { 1, &store.move_lies_fail, 1, &store.fail_move, 2, &store.move_calls,
    SAVE_STORE_RESTORE_FAILED, NEW },
  { 1, &store.fail_flush, 2, &store.fail_move, 3, &store.move_calls,
    SAVE_STORE_RESTORE_FAILED, OLD },
  /* A first write has no old file to keep or restore. */
  { 0, &store.fail_move, 1, NULL, 0, &store.move_calls,
    SAVE_STORE_PROMOTE_FAILED, 0 },
  { 0, &store.move_lies_fail, 1, NULL, 0, &store.move_calls,
    SAVE_STORE_PROMOTE_FAILED, NEW },
  { 0, &store.lose_on_close, 1, NULL, 0, &store.close_calls,
    SAVE_STORE_VERIFY_FAILED, 0 }
};

static void test_each_fault_gives_its_result(void)
{
  size_t i;

  for(i = 0; i < sizeof(faults) / sizeof(faults[0]); i++)
  {
    const Fault *fault = &faults[i];

    seed_state(fault->had_file ? ONLY_FILE : NO_FILE);
    *fault->fault = fault->call;
    if(fault->also != NULL)
      *fault->also = fault->also_call;
    assert(write_new_in_chunks() == fault->result);
    assert(*fault->calls >= (fault->also != NULL ? fault->also_call :
     fault->call));
    assert(store.open_file == -1);
    if(fault->result != SAVE_STORE_RESTORE_FAILED)
    {
      if(fault->image == OLD)
        assert(fake_store_file_is(&store, PRIMARY, OLD_SIZE, OLD));
      else if(fault->image == NEW)
        assert(fake_store_file_is(&store, PRIMARY, NEW_SIZE, NEW));
      else
        assert(fake_store_file_size(&store, PRIMARY) < 0);
    }
    *fault->fault = 0;
    if(fault->also != NULL)
      *fault->also = 0;
    assert(restart() == fault->image);
  }
}

/* A full device stops the write at what fit, and later appends do
   nothing; a device that takes a few bytes per call still gets them
   all. */
static void test_short_writes(void)
{
  seed_state(ONLY_FILE);
  store.space = 500;
  assert(write_new_in_chunks() == SAVE_STORE_WRITE_FAILED);
  assert(store.written == 500 && store.write_calls == 3);
  assert(fake_store_file_is(&store, PRIMARY, OLD_SIZE, OLD));
  store.space = 0;
  assert(restart() == OLD);

  seed_state(ONLY_FILE);
  store.short_write = 7;
  assert(write_new_in_chunks() == SAVE_STORE_OK);
  assert(store.write_calls == 3 * ((CHUNK + 6) / 7));
  assert(fake_store_file_is(&store, PRIMARY, NEW_SIZE, NEW));
}

/* 512 KiB, a savestate's size, streamed in 1-byte, 16 KiB and one
   512 KiB append. */
static void test_streaming(void)
{
  static const size_t chunks[] = { 1, 16 * KIB, 512 * KIB };
  size_t i;
  size_t offset;

  fill_source(sizeof(source), 4);
  for(i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++)
  {
    seed_state(ONLY_FILE);
    assert(save_store_begin(&transaction, &io, SAVE_DIR, NAME) == SAVE_STORE_OK);
    for(offset = 0; offset < sizeof(source); offset += chunks[i])
      assert(save_store_append(&transaction, source + offset, chunks[i]) ==
             SAVE_STORE_OK);
    assert(save_store_commit(&transaction) == SAVE_STORE_OK);
    assert(store.write_calls == (int)(sizeof(source) / chunks[i]));
    assert(fake_store_file_is(&store, PRIMARY, sizeof(source), 4));
    assert(fake_store_file_is(&store, BAK, OLD_SIZE, OLD));
  }
}

static SaveStoreResult push(int seed)
{
  fill_source(LINK_SIZE, (uint8_t)seed);
  return save_store_ring_push(&io, SAVE_DIR, NAME, source, LINK_SIZE);
}

/* The seed of the image in links[index]: 0 for no file, -1 for a file
   that holds no whole image. */
static int generation(int index)
{
  int seed;

  if(fake_store_file_size(&store, links[index]) < 0)
    return 0;
  for(seed = 1; seed <= PUSHED + 1; seed++)
  {
    if(fake_store_file_is(&store, links[index], LINK_SIZE, (uint8_t)seed))
      return seed;
  }
  return -1;
}

/* The .sav and count generations. */
static void seed_ring(int count)
{
  int i;

  seed_state(ONLY_FILE);
  for(i = 0; i < count; i++)
    fake_store_seed_file(&store, links[i], LINK_SIZE, (uint8_t)(count - i));
}

/* After a push of PUSHED over seed_ring(count), whole or cut: each link
   holds a whole image, newer ones first; every generation is still there
   but the oldest of three; the .sav is untouched and no .link1.bak was
   made. */
static void check_ring(int count)
{
  unsigned seen = 0;
  int newer = PUSHED + 1;
  int seed;
  int i;

  for(i = 0; i < 3; i++)
  {
    seed = generation(i);
    assert(seed >= 0);
    if(seed == 0)
      continue;
    assert(seed < newer && (seed == PUSHED || seed <= count));
    newer = seed;
    seen |= 1u << seed;
  }
  for(seed = (count == 3) ? 2 : 1; seed <= count; seed++)
    assert(seen & (1u << seed));
  assert(fake_store_file_is(&store, PRIMARY, OLD_SIZE, OLD));
  assert(fake_store_file_size(&store, LINK1 ".bak") < 0);
}

/* A push of PUSHED + 1 after one that failed or was cut works, and keeps
   every generation there before but the oldest of three, newer ones
   first. */
static void check_next_push(void)
{
  unsigned before = 0;
  unsigned after = 0;
  int present = 0;
  int oldest = 0;
  int newer = PUSHED + 2;
  int seed;
  int i;

  for(i = 0; i < 3; i++)
  {
    seed = generation(i);
    if(seed > 0)
    {
      before |= 1u << seed;
      present++;
      oldest = seed;
    }
  }
  if(present == 3)
    before &= ~(1u << oldest);
  assert(push(PUSHED + 1) == SAVE_STORE_OK);
  assert(generation(0) == PUSHED + 1);
  for(i = 0; i < 3; i++)
  {
    seed = generation(i);
    assert(seed >= 0);
    if(seed == 0)
      continue;
    assert(seed < newer);
    newer = seed;
    after |= 1u << seed;
  }
  assert(after == (before | 1u << (PUSHED + 1)));
}

/* From no generation to three and past: after n pushes there are
   min(n, 3), the newest in .link1, and no .tmp. */
static void test_ring_keeps_three_generations(void)
{
  int n;
  int i;

  seed_ring(0);
  for(n = 1; n <= 5; n++)
  {
    assert(push(n) == SAVE_STORE_OK);
    for(i = 0; i < 3; i++)
      assert(generation(i) == (i < n ? n - i : 0));
    assert(fake_store_file_size(&store, LINK1 ".tmp") < 0);
    assert(fake_store_file_size(&store, LINK1 ".bak") < 0);
    assert(fake_store_file_is(&store, PRIMARY, OLD_SIZE, OLD));
  }
}

/* For 0 to 3 generations there and every call k of a push: the power goes
   off after call k, the next start recovers .link1 (a leftover .link1.tmp
   goes), and the ring has lost nothing but the oldest of three, with no
   partial .link1. The next push loses no more than that. */
static void test_ring_power_cut_at_every_step(void)
{
  int count;
  int cut;
  int leftovers = 0;
  SaveStoreResult result = SAVE_STORE_FAILED;

  for(count = 0; count <= 3; count++)
  {
    for(cut = 1; ; cut++)
    {
      seed_ring(count);
      store.cut_after = cut;
      result = push(PUSHED);
      if(!store.power_off)
        break;
      fake_store_restart(&store);
      check_ring(count);
      leftovers += fake_store_file_size(&store, LINK1 ".tmp") >= 0;
      assert(save_store_recover(&io, SAVE_DIR, NAME ".link1") ==
             SAVE_STORE_OK);
      assert(fake_store_file_size(&store, LINK1 ".tmp") < 0);
      check_ring(count);
      check_next_push();
    }
    assert(result == SAVE_STORE_OK && generation(0) == PUSHED);
    check_ring(count);
    /* One or two checks, any removal and renames, then the write's 13
       calls. */
    assert(cut > 14);
  }
  assert(leftovers > 0);
}

/* A rename that fails, or that the names do not confirm either way, stops
   the push before .link1 is written, the generations as check_ring()
   wants them; the next push loses no more. A failed write of .link1 gives
   the store's result, and push after push still loses only the oldest
   generation. */
static void test_ring_stops_at_a_failed_rename(void)
{
  static int *const renames[] =
  {
    &store.fail_move, &store.move_lies_ok, &store.move_lies_fail
  };
  size_t i;
  int call;

  for(i = 0; i < sizeof(renames) / sizeof(renames[0]); i++)
  {
    for(call = 1; call <= 2; call++)
    {
      seed_ring(3);
      *renames[i] = call;
      assert(push(PUSHED) == SAVE_STORE_FAILED);
      assert(store.move_calls == call && store.open_write_calls == 0);
      check_ring(3);
      *renames[i] = 0;
      check_next_push();
    }
  }

  seed_ring(3);
  for(call = 1; call <= 3; call++)
  {
    store.fail_open_write = call;
    assert(push(PUSHED) == SAVE_STORE_WRITE_FAILED);
    assert(generation(0) == 0);
    check_ring(3);
  }
  check_next_push();
}

int main(void)
{
  test_missing_and_empty_files_are_blank();
  test_whole_file_loads();
  test_unreadable_file_fails();
  test_oversized_file_loads_a_prefix();
  test_long_path_fails_without_io();
  test_load_never_writes();
  test_write_keeps_the_old_image_as_backup();
  test_recovery();
  test_power_cut_at_every_step();
  test_each_fault_gives_its_result();
  test_short_writes();
  test_streaming();
  test_ring_keeps_three_generations();
  test_ring_power_cut_at_every_step();
  test_ring_stops_at_a_failed_rename();
  puts("save store tests passed");
  return 0;
}
