/*
 * GBA Rush PSP
 *
 * Copyright (C) 2026 Piero Carrieri
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

/*
 * Reads and writes the cartridge save file (.sav) through SaveStoreIo, so
 * the host tests can swap the file system for a fake that fails on demand.
 * Loading tells a missing file (a blank cartridge) from one that exists
 * but cannot be read, which must never be written over.
 *
 * Every write is a transaction: the data goes to <name>.tmp, which is
 * synced and read back against the CRC32 of what was written; only then
 * does the old file become <name>.bak and the .tmp take its name. A power
 * cut at any step leaves the old image or the new one, which
 * save_store_recover() puts back under the file's own name.
 *
 * A save keeps its size from write to write, so an old file of another
 * size than the image being written is the file from before a load of the
 * wrong size. While there is no <name>.orig, the write renames it to
 * .orig instead of .bak: the first file of another size is kept once, and
 * no write ever deletes or replaces it, so it is always the file or the
 * .orig. An empty file holds nothing to keep. A size the game itself
 * changes (SRAM growing to 64 KiB, a Flash bank switch to 128 KiB, the
 * EEPROM's 8 KiB promotion) keeps the smaller file the same way.
 *
 * Limits. The rule sees sizes only: a file of the wrong size that the next
 * write matches (a savestate brought that chip size back before any keep)
 * is not kept. A device that answers exists() with yes because it cannot
 * tell makes the old file the .bak instead. A failed promotion whose
 * restore also fails, or is cut, after deleting the new file leaves only
 * the .orig: the next start is a blank cartridge, the old file still in
 * .orig.
 */

#ifndef SAVE_STORE_H
#define SAVE_STORE_H

#include <stddef.h>
#include <stdint.h>

/* Longest path with its terminator, as MAX_PATH in common.h. */
#define SAVE_STORE_PATH_MAX 512

/* Each callback but write and open_write returns nonzero on success. */
typedef struct
{
  void *context;
  /* Nonzero if path names anything, even something that will not open.
     When the device cannot tell (it is gone), nonzero too: the load then
     fails instead of starting a blank cartridge. */
  int (*exists)(void *context, const char *path);
  /* Reads at most capacity bytes from the start of path. *size gets the
     bytes read, which may stop short on an error, and *file_size the
     file's whole length. 0 if the file will not open or measure. */
  int (*read)(void *context, const char *path, uint8_t *data,
              size_t capacity, size_t *size, size_t *file_size);
  /* Creates or truncates path for writing; a negative handle on
     failure. */
  int32_t (*open_write)(void *context, const char *path);
  /* Writes up to size bytes; the count written, which may be short, or
     0 or less on failure (a full device). */
  int (*write)(void *context, int32_t file, const uint8_t *data,
               size_t size);
  /* Closes a handle from open_write; 0 if the data may not all be in the
     file. */
  int (*close)(void *context, int32_t file);
  /* Syncs the device that holds path, e.g. "ms0:" or "ef0:". */
  int (*flush)(void *context, const char *path);
  /* Renames source to target, which must not exist. */
  int (*move)(void *context, const char *source, const char *target);
  /* Deletes path; a missing file counts as success. */
  int (*remove)(void *context, const char *path);
  /* Reads path back to its end: zlib's CRC32 of its bytes and its
     length. */
  int (*crc32)(void *context, const char *path, uint32_t *crc,
               size_t *size);
} SaveStoreIo;

typedef enum
{
  SAVE_STORE_OK = 0,      /* loaded, recovered, or written */
  SAVE_STORE_NOT_FOUND,   /* no file, or an empty one: a blank cartridge */
  SAVE_STORE_FAILED,      /* unreadable, or a .tmp or .bak left by a cut
                             write could not be put back; also a path too
                             long for SAVE_STORE_PATH_MAX, or a generation
                             that would not rotate */
  /* Write results. Unless noted, the old file was not touched. */
  SAVE_STORE_WRITE_FAILED,  /* the .tmp would not open, take every byte,
                               close or sync */
  SAVE_STORE_VERIFY_FAILED, /* the .tmp read back wrong */
  SAVE_STORE_BACKUP_FAILED, /* the old file's size would not read, or it
                               would not become the .bak or the .orig; if
                               it moved, it is back */
  SAVE_STORE_PROMOTE_FAILED, /* the .tmp would not take the file's name;
                                the old file is back */
  SAVE_STORE_RESTORE_FAILED  /* a step failed after the old file became
                                the .bak or the .orig, and it would not go
                                back either; save_store_recover() retries */
} SaveStoreResult;

/* One write in progress, from save_store_begin() to save_store_commit().
   It holds four paths: keep it static, not on a thread's stack. */
typedef struct
{
  const SaveStoreIo *io;
  char primary[SAVE_STORE_PATH_MAX];
  char temporary[SAVE_STORE_PATH_MAX]; /* primary plus ".tmp" */
  char backup[SAVE_STORE_PATH_MAX];    /* primary plus ".bak" */
  char original[SAVE_STORE_PATH_MAX];  /* primary plus ".orig" */
  int32_t file;     /* the open .tmp, or negative */
  uint32_t crc;     /* CRC32 of the bytes appended so far */
  size_t size;
  SaveStoreResult result; /* the first failure, kept to the commit */
} SaveStoreWrite;

/* Loads dir followed by name into data. A file larger than capacity loads
   its first capacity bytes; *file_size gets its whole length on
   SAVE_STORE_OK and 0 otherwise. Never writes or renames anything: call
   save_store_recover() first. */
SaveStoreResult save_store_load(const SaveStoreIo *io, const char *dir,
 const char *name, uint8_t *data, size_t capacity, size_t *file_size);
/* Finishes a write a power cut stopped. With the file there, a .tmp is
   deleted. Without it, a .tmp is promoted if there is a .bak (the file
   moves to .bak only once the .tmp is verified), else the .bak comes back.
   With neither, a .orig beside a .tmp comes back and the .tmp is deleted:
   the cut fell after the old file became the .orig, or in a first write.
   A .tmp alone is an unverified first write, and is deleted. OK, or
   FAILED if a step fails. */
SaveStoreResult save_store_recover(const SaveStoreIo *io, const char *dir,
 const char *name);
/* Recovers, then opens dir followed by name plus ".tmp" for writing. Call
   save_store_commit() even when a step fails: it closes the .tmp. */
SaveStoreResult save_store_begin(SaveStoreWrite *transaction,
 const SaveStoreIo *io, const char *dir, const char *name);
/* Appends size bytes to the .tmp. After a failure, does nothing and
   returns it again. */
SaveStoreResult save_store_append(SaveStoreWrite *transaction,
 const uint8_t *data, size_t size);
/* Closes, syncs and checks the .tmp, then makes it the file, the old file
   becoming the .bak, or the .orig if it is to be kept (see the top of this
   file). Returns the first failure of the whole write. */
SaveStoreResult save_store_commit(SaveStoreWrite *transaction);
/* save_store_begin(), one save_store_append() of size bytes of data, and
   save_store_commit(). */
SaveStoreResult save_store_write(const SaveStoreIo *io, const char *dir,
 const char *name, const uint8_t *data, size_t size);
/* Keeps size bytes of data as the newest of three generations, dir
   followed by name plus ".link1" (newest) to ".link3" (oldest). The
   generations move up one name as far as the first free one: with all
   three there, .link3 is deleted, .link2 becomes .link3 and .link1
   becomes .link2, each rename checked on both names. Then
   save_store_write() writes .link1. A failed rename stops the push there,
   so only the oldest generation is lost, and the gap a failed push leaves
   is filled by the next one without losing another. FAILED if a name
   does not fit, .link3 will not delete or a rename fails, before .link1
   is written; else save_store_write()'s result. */
SaveStoreResult save_store_ring_push(const SaveStoreIo *io, const char *dir,
 const char *name, const uint8_t *data, size_t size);

#endif /* SAVE_STORE_H */
