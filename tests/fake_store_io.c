/*
 * In-memory .sav file system for the save tests; see fake_store_io.h.
 */

#include <string.h>

#include "fake_store_io.h"

static int is_file(const FakeStore *store, const char *path)
{
  return strcmp(store->path, path) == 0;
}

static int fake_exists(void *context, const char *path)
{
  FakeStore *store = (FakeStore *)context;

  store->exists_calls++;
  return is_file(store, path) && store->present;
}

/* Like the sceIo binding: 0 only if the file will not open; a read that
   breaks off still returns 1 with the bytes it got. */
static int fake_read(void *context, const char *path, uint8_t *data,
 size_t capacity, size_t *size, size_t *file_size)
{
  FakeStore *store = (FakeStore *)context;
  size_t count;

  store->read_calls++;
  store->last_capacity = capacity;
  if(!is_file(store, path) || !store->present || store->fail_open)
    return 0;
  count = store->size < capacity ? store->size : capacity;
  if(store->fail_reads > 0)
  {
    store->fail_reads--;
    count = 0;
  }
  else if(store->short_read != 0 && store->short_read < count)
    count = store->short_read;
  memcpy(data, store->data, count);
  *size = count;
  *file_size = store->size;
  if(store->hide_after_reads != 0 &&
     store->read_calls >= store->hide_after_reads)
    store->present = 0;
  return 1;
}

static int fake_write(void *context, const char *path, const uint8_t *data,
 size_t size)
{
  FakeStore *store = (FakeStore *)context;

  store->write_calls++;
  if(store->fail_write || !is_file(store, path) ||
     size > sizeof(store->data))
    return 0;
  memcpy(store->data, data, size);
  store->size = size;
  store->present = 1;
  return 1;
}

void fake_store_init(FakeStore *store, SaveStoreIo *io, const char *path)
{
  memset(store, 0, sizeof(*store));
  strncpy(store->path, path, sizeof(store->path) - 1);
  store->path[sizeof(store->path) - 1] = '\0';
  io->context = store;
  io->exists = fake_exists;
  io->read = fake_read;
  io->write = fake_write;
}

uint8_t fake_store_pattern(size_t offset, uint8_t seed)
{
  return (uint8_t)((offset * 7 + seed) & 0xFF);
}

void fake_store_seed(FakeStore *store, size_t size, uint8_t seed)
{
  size_t i;

  for(i = 0; i < size; i++)
    store->data[i] = fake_store_pattern(i, seed);
  store->size = size;
  store->present = 1;
}
