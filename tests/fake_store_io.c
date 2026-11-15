/*
 * In-memory .sav file system for the save tests; see fake_store_io.h.
 */

#include <string.h>

#include "fake_store_io.h"
#include "zlib/zlib.h"

/* The primary file (slot 0) or a sibling (slot 1 on), seen the same
   way. */
typedef struct
{
  char *path;
  uint8_t *data;
  size_t *size;
  int *present;
} FileRef;

static FileRef slot_ref(FakeStore *store, int slot)
{
  FileRef ref;
  FakeFile *file;

  if(slot == 0)
  {
    ref.path = store->path;
    ref.data = store->data;
    ref.size = &store->size;
    ref.present = &store->present;
    return ref;
  }
  file = &store->siblings[slot - 1];
  ref.path = file->path;
  ref.data = file->data;
  ref.size = &file->size;
  ref.present = &file->present;
  return ref;
}

/* The slot of the file at path, if it is there; else -1. */
static int present_slot(FakeStore *store, const char *path)
{
  int slot;

  for(slot = 0; slot <= FAKE_STORE_SIBLINGS; slot++)
  {
    FileRef ref = slot_ref(store, slot);

    if(*ref.present && strcmp(ref.path, path) == 0)
      return slot;
  }
  return -1;
}

/* The slot for path: its own, or a free sibling slot named path. -1 if
   the table is full. */
static int claim_slot(FakeStore *store, const char *path)
{
  int slot;

  if(strcmp(store->path, path) == 0)
    return 0;
  for(slot = 1; slot <= FAKE_STORE_SIBLINGS; slot++)
  {
    if(strcmp(store->siblings[slot - 1].path, path) == 0)
      return slot;
  }
  for(slot = 1; slot <= FAKE_STORE_SIBLINGS; slot++)
  {
    FakeFile *file = &store->siblings[slot - 1];

    if(!file->present && strlen(path) < sizeof(file->path))
    {
      strcpy(file->path, path);
      return slot;
    }
  }
  return -1;
}

/* Counts a call of one operation; 0 once the power is off. The call that
   reaches cut_after still happens. */
static int step(FakeStore *store, int *counter)
{
  (*counter)++;
  if(store->power_off)
    return 0;
  store->calls++;
  if(store->cut_after != 0 && store->calls == store->cut_after)
    store->power_off = 1;
  return 1;
}

/* A device that is gone cannot tell, and says yes, as SaveStoreIo asks. */
static int fake_exists(void *context, const char *path)
{
  FakeStore *store = (FakeStore *)context;

  if(!step(store, &store->exists_calls))
    return 1;
  return present_slot(store, path) >= 0;
}

/* Like the sceIo binding: 0 only if the file will not open; a read that
   breaks off still returns 1 with the bytes it got. */
static int fake_read(void *context, const char *path, uint8_t *data,
 size_t capacity, size_t *size, size_t *file_size)
{
  FakeStore *store = (FakeStore *)context;
  int slot;
  FileRef ref;
  size_t count;

  if(!step(store, &store->read_calls))
    return 0;
  store->last_capacity = capacity;
  slot = present_slot(store, path);
  if(slot < 0 || store->fail_open)
    return 0;
  ref = slot_ref(store, slot);
  count = *ref.size < capacity ? *ref.size : capacity;
  if(store->fail_reads > 0)
  {
    store->fail_reads--;
    count = 0;
  }
  else if(store->short_read != 0 && store->short_read < count)
    count = store->short_read;
  memcpy(data, ref.data, count);
  *size = count;
  *file_size = *ref.size;
  if(store->hide_after_reads != 0 &&
     store->read_calls >= store->hide_after_reads)
    *ref.present = 0;
  return 1;
}

static int32_t fake_open_write(void *context, const char *path)
{
  FakeStore *store = (FakeStore *)context;
  int slot;
  FileRef ref;

  if(!step(store, &store->open_write_calls) ||
     store->fail_open_write == store->open_write_calls ||
     store->open_file >= 0)
    return -1;
  slot = claim_slot(store, path);
  if(slot < 0)
    return -1;
  ref = slot_ref(store, slot);
  *ref.size = 0;
  *ref.present = 1;
  store->open_file = slot;
  return slot;
}

/* Stores what fits before the device or the file is full: a short count,
   then -1 as ENOSPC. short_write cuts every count short. */
static int fake_write(void *context, int32_t file, const uint8_t *data,
 size_t size)
{
  FakeStore *store = (FakeStore *)context;
  FileRef ref;
  size_t room;

  if(!step(store, &store->write_calls) || file < 0 ||
     file != store->open_file)
    return -1;
  ref = slot_ref(store, file);
  room = FAKE_STORE_CAPACITY - *ref.size;
  if(store->space != 0 && store->space - store->written < room)
    room = store->space - store->written;
  if(store->short_write != 0 && store->short_write < room)
    room = store->short_write;
  if(size > room)
    size = room;
  if(size == 0)
    return -1;
  memcpy(ref.data + *ref.size, data, size);
  *ref.size += size;
  store->written += size;
  return (int)size;
}

static int fake_close(void *context, int32_t file)
{
  FakeStore *store = (FakeStore *)context;
  FileRef ref;

  if(!step(store, &store->close_calls) || file < 0 ||
     file != store->open_file)
    return 0;
  store->open_file = -1;
  ref = slot_ref(store, file);
  if(store->lose_on_close == store->close_calls && *ref.size > 0)
    (*ref.size)--;
  if(store->corrupt_on_close == store->close_calls && *ref.size > 0)
    ref.data[0] ^= 0xFF;
  return store->fail_close != store->close_calls;
}

static int fake_flush(void *context, const char *path)
{
  FakeStore *store = (FakeStore *)context;

  (void)path;
  return step(store, &store->flush_calls) &&
         store->fail_flush != store->flush_calls;
}

/* Like FAT, never renames over an existing file. */
static int fake_move(void *context, const char *source, const char *target)
{
  FakeStore *store = (FakeStore *)context;
  int from;
  int to;
  FileRef old;
  FileRef new_file;

  if(!step(store, &store->move_calls) ||
     store->fail_move == store->move_calls)
    return 0;
  if(store->move_lies_ok == store->move_calls)
    return 1;
  from = present_slot(store, source);
  if(from < 0 || present_slot(store, target) >= 0)
    return 0;
  to = claim_slot(store, target);
  if(to < 0)
    return 0;
  old = slot_ref(store, from);
  new_file = slot_ref(store, to);
  memcpy(new_file.data, old.data, *old.size);
  *new_file.size = *old.size;
  *new_file.present = 1;
  *old.present = 0;
  return store->move_lies_fail != store->move_calls;
}

static int fake_remove(void *context, const char *path)
{
  FakeStore *store = (FakeStore *)context;
  int slot;

  if(!step(store, &store->remove_calls) ||
     store->fail_remove == store->remove_calls)
    return 0;
  slot = present_slot(store, path);
  if(slot >= 0)
    *slot_ref(store, slot).present = 0;
  return 1;
}

static int fake_crc32(void *context, const char *path, uint32_t *crc,
 size_t *size)
{
  FakeStore *store = (FakeStore *)context;
  int slot;
  FileRef ref;

  if(!step(store, &store->crc32_calls) ||
     store->fail_crc32 == store->crc32_calls)
    return 0;
  slot = present_slot(store, path);
  if(slot < 0)
    return 0;
  ref = slot_ref(store, slot);
  *crc = crc32(crc32(0L, Z_NULL, 0), ref.data, (uInt)*ref.size);
  *size = *ref.size;
  return 1;
}

void fake_store_init(FakeStore *store, SaveStoreIo *io, const char *path)
{
  memset(store, 0, sizeof(*store));
  strncpy(store->path, path, sizeof(store->path) - 1);
  store->path[sizeof(store->path) - 1] = '\0';
  store->open_file = -1;
  io->context = store;
  io->exists = fake_exists;
  io->read = fake_read;
  io->open_write = fake_open_write;
  io->write = fake_write;
  io->close = fake_close;
  io->flush = fake_flush;
  io->move = fake_move;
  io->remove = fake_remove;
  io->crc32 = fake_crc32;
}

void fake_store_restart(FakeStore *store)
{
  store->power_off = 0;
  store->cut_after = 0;
  store->open_file = -1;
}

uint8_t fake_store_pattern(size_t offset, uint8_t seed)
{
  return (uint8_t)((offset * 7 + seed) & 0xFF);
}

void fake_store_seed_file(FakeStore *store, const char *path, size_t size,
 uint8_t seed)
{
  int slot = claim_slot(store, path);
  FileRef ref;
  size_t i;

  if(slot < 0)
    return;
  ref = slot_ref(store, slot);
  for(i = 0; i < size; i++)
    ref.data[i] = fake_store_pattern(i, seed);
  *ref.size = size;
  *ref.present = 1;
}

void fake_store_seed(FakeStore *store, size_t size, uint8_t seed)
{
  fake_store_seed_file(store, store->path, size, seed);
}

long fake_store_file_size(FakeStore *store, const char *path)
{
  int slot = present_slot(store, path);

  return slot < 0 ? -1 : (long)*slot_ref(store, slot).size;
}

int fake_store_file_is(FakeStore *store, const char *path, size_t size,
 uint8_t seed)
{
  int slot = present_slot(store, path);
  FileRef ref;
  size_t i;

  if(slot < 0)
    return 0;
  ref = slot_ref(store, slot);
  if(*ref.size != size)
    return 0;
  for(i = 0; i < size; i++)
  {
    if(ref.data[i] != fake_store_pattern(i, seed))
      return 0;
  }
  return 1;
}
