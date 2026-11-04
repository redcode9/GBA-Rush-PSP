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
 */

#ifndef SAVE_STORE_H
#define SAVE_STORE_H

#include <stddef.h>
#include <stdint.h>

/* Longest path with its terminator, as MAX_PATH in common.h. */
#define SAVE_STORE_PATH_MAX 512

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
  /* Creates or truncates path with size bytes of data; 0 on failure. */
  int (*write)(void *context, const char *path, const uint8_t *data,
               size_t size);
} SaveStoreIo;

typedef enum
{
  SAVE_STORE_OK = 0,      /* loaded, or written */
  SAVE_STORE_NOT_FOUND,   /* no file, or an empty one: a blank cartridge */
  SAVE_STORE_FAILED       /* unreadable or not written; also a path too
                             long for SAVE_STORE_PATH_MAX */
} SaveStoreResult;

/* Loads dir followed by name into data. A file larger than capacity loads
   its first capacity bytes; *file_size gets its whole length on
   SAVE_STORE_OK and 0 otherwise. Never writes or renames anything. */
SaveStoreResult save_store_load(const SaveStoreIo *io, const char *dir,
 const char *name, uint8_t *data, size_t capacity, size_t *file_size);
/* Replaces dir followed by name with size bytes of data. */
SaveStoreResult save_store_write(const SaveStoreIo *io, const char *dir,
 const char *name, const uint8_t *data, size_t size);

#endif /* SAVE_STORE_H */
