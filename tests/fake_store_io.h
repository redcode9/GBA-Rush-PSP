/*
 * In-memory stand-in for the .sav file system behind SaveStoreIo, for the
 * host tests of save_store.c and of memory.c's save path. It holds one
 * file, at the path given to fake_store_init(); every other path is
 * absent and cannot be written. Fault switches make each operation fail
 * the ways a Memory Stick can, and counters record what was called.
 */

#ifndef FAKE_STORE_IO_H
#define FAKE_STORE_IO_H

#include <stddef.h>
#include <stdint.h>

#include "save_store.h"

/* Larger than the 128 KiB save buffer, for oversized files. */
#define FAKE_STORE_CAPACITY (256 * 1024)

/* About 256 KiB: keep instances static, not on the stack. */
typedef struct
{
  char path[SAVE_STORE_PATH_MAX];
  uint8_t data[FAKE_STORE_CAPACITY];
  size_t size;
  int present;
  /* Fault switches. */
  int fail_open;     /* the file is there but will not open */
  int fail_reads;    /* this many reads stop before the first byte */
  size_t short_read; /* if nonzero, reads stop after this many bytes */
  int hide_after_reads; /* if nonzero, the file is gone after this many
                           reads, as when the Memory Stick goes away */
  int fail_write;
  /* What was called. */
  int exists_calls;
  int read_calls;
  int write_calls;
  size_t last_capacity; /* capacity passed to the last read */
} FakeStore;

/* Empties the store, clears the switches and counters, and points io at
   the store with path as its one file. */
void fake_store_init(FakeStore *store, SaveStoreIo *io, const char *path);
/* Makes the file size bytes long, byte i holding (i * 7 + seed) & 0xFF. */
void fake_store_seed(FakeStore *store, size_t size, uint8_t seed);
/* The byte fake_store_seed() put at offset. */
uint8_t fake_store_pattern(size_t offset, uint8_t seed);

#endif /* FAKE_STORE_IO_H */
