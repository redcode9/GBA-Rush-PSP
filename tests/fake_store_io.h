/*
 * In-memory stand-in for the .sav file system behind SaveStoreIo, for the
 * host tests of save_store.c and of memory.c's save path. The primary
 * file, at the path given to fake_store_init(), keeps its fields at the
 * top of FakeStore; a few sibling files (the .tmp and .bak) live in a
 * small table. Fault switches make each operation fail the ways a Memory
 * Stick can, and counters record what was called.
 */

#ifndef FAKE_STORE_IO_H
#define FAKE_STORE_IO_H

#include <stddef.h>
#include <stdint.h>

#include "save_store.h"

/* Room for a 512 KiB savestate plus a margin, and for .sav files larger
   than the 128 KiB save buffer. */
#define FAKE_STORE_CAPACITY (576 * 1024)
#define FAKE_STORE_SIBLINGS 3

typedef struct
{
  char path[SAVE_STORE_PATH_MAX];
  uint8_t data[FAKE_STORE_CAPACITY];
  size_t size;
  int present;
} FakeFile;

/* About 2.3 MiB: keep instances static, not on the stack. */
typedef struct
{
  /* The primary file. */
  char path[SAVE_STORE_PATH_MAX];
  uint8_t data[FAKE_STORE_CAPACITY];
  size_t size;
  int present;
  FakeFile siblings[FAKE_STORE_SIBLINGS];

  /* Read faults, on any file. */
  int fail_open;     /* the file is there but will not open */
  int fail_reads;    /* this many reads stop before the first byte */
  size_t short_read; /* if nonzero, reads stop after this many bytes */
  int hide_after_reads; /* if nonzero, the file read is gone after this
                           many reads, as when the Memory Stick goes away */
  /* Write faults. Each names the call it hits, 1 for the first call of
     that operation since fake_store_init(); 0 for none. */
  int fail_open_write;
  int fail_close;       /* reports a failure; the data stays */
  int lose_on_close;    /* the file loses its last byte, reported or not */
  int corrupt_on_close; /* the file's first byte flips, at the same size */
  int fail_flush;
  int fail_move;        /* fails without moving */
  int move_lies_ok;     /* reports success without moving */
  int move_lies_fail;   /* moves, then reports a failure */
  int fail_remove;
  int fail_crc32;
  size_t short_write;   /* if nonzero, a write takes at most this many
                           bytes */
  size_t space;         /* if nonzero, the device is full once this many
                           bytes are written: ENOSPC */
  int cut_after;        /* if nonzero, the power goes off after this many
                           calls of any operation: later calls fail and
                           change nothing, what was done stays */
  int power_off;

  /* What was called. */
  int calls;            /* every call while the power was on */
  int exists_calls;
  int read_calls;
  int open_write_calls;
  int write_calls;
  int close_calls;
  int flush_calls;
  int move_calls;
  int remove_calls;
  int crc32_calls;
  size_t written;       /* bytes the writes accepted */
  size_t last_capacity; /* capacity passed to the last read */
  int32_t open_file;    /* the handle open_write gave, or -1 */
} FakeStore;

/* Empties the store, clears the switches and counters, and points io at
   the store with path as its primary file. */
void fake_store_init(FakeStore *store, SaveStoreIo *io, const char *path);
/* Power back on after a cut: no file is open and cut_after is cleared;
   the files stay as the cut left them, the other switches and the
   counters as they were. */
void fake_store_restart(FakeStore *store);
/* Makes the primary file size bytes long, byte i holding
   (i * 7 + seed) & 0xFF. */
void fake_store_seed(FakeStore *store, size_t size, uint8_t seed);
/* The same for a sibling file at path, created if needed. */
void fake_store_seed_file(FakeStore *store, const char *path, size_t size,
 uint8_t seed);
/* The byte fake_store_seed() put at offset. */
uint8_t fake_store_pattern(size_t offset, uint8_t seed);
/* Size of the file at path, or -1 if there is none. */
long fake_store_file_size(FakeStore *store, const char *path);
/* Nonzero if the file at path holds size bytes of seed's pattern. */
int fake_store_file_is(FakeStore *store, const char *path,
 size_t size, uint8_t seed);

#endif /* FAKE_STORE_IO_H */
