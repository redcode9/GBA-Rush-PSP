/*
 * Host tests for save_store.c, the .sav load and write: a missing or empty
 * file is a blank cartridge, a file that exists but cannot be read is a
 * failure (never mistaken for a blank cartridge), and a file larger than
 * the buffer loads its first part only. Each fault switch of the fake
 * file system (fake_store_io.c) is shown to take effect.
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

static FakeStore store;
static SaveStoreIo io;
/* The load buffer plus a tail that must stay untouched. */
static uint8_t buffer[CAPACITY + 16];

static void reset(void)
{
  fake_store_init(&store, &io, "ms0:/SAVE/game.sav");
  memset(buffer, CANARY, sizeof(buffer));
}

static SaveStoreResult load(size_t *file_size)
{
  return save_store_load(&io, "ms0:/SAVE/", "game.sav", buffer, CAPACITY,
   file_size);
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

/* A path that does not fit is refused before any I/O, not cut short. */
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
  assert(save_store_write(&io, "ms0:/", name, data, sizeof(data)) ==
         SAVE_STORE_FAILED);
  assert(store.exists_calls == 0 && store.read_calls == 0 &&
         store.write_calls == 0);
}

/* Loading reads only; writing replaces the file or reports the failure. */
static void test_write_and_load_never_writes(void)
{
  uint8_t data[512];
  size_t file_size;
  size_t i;

  reset();
  fake_store_seed(&store, 32 * KIB, 5);
  load(&file_size);
  store.fail_open = 1;
  load(&file_size);
  assert(store.write_calls == 0);

  for(i = 0; i < sizeof(data); i++)
    data[i] = (uint8_t)i;
  assert(save_store_write(&io, "ms0:/SAVE/", "game.sav", data,
   sizeof(data)) == SAVE_STORE_OK);
  assert(store.size == sizeof(data) && memcmp(store.data, data, 512) == 0);

  store.fail_write = 1;
  assert(save_store_write(&io, "ms0:/SAVE/", "game.sav", data, 16) ==
         SAVE_STORE_FAILED);
  assert(store.size == sizeof(data));
}

int main(void)
{
  test_missing_and_empty_files_are_blank();
  test_whole_file_loads();
  test_unreadable_file_fails();
  test_oversized_file_loads_a_prefix();
  test_long_path_fails_without_io();
  test_write_and_load_never_writes();
  puts("save store tests passed");
  return 0;
}
