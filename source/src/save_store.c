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

#include <stdio.h>

#include "save_store.h"

/* dir followed by name; 0 if that does not fit, since a cut path would
   name another file. */
static int join_path(char *path, const char *dir, const char *name)
{
  int length = snprintf(path, SAVE_STORE_PATH_MAX, "%s%s", dir, name);

  return length >= 0 && length < SAVE_STORE_PATH_MAX;
}

SaveStoreResult save_store_load(const SaveStoreIo *io, const char *dir,
 const char *name, uint8_t *data, size_t capacity, size_t *file_size)
{
  char path[SAVE_STORE_PATH_MAX];
  size_t size = 0;
  size_t length = 0;

  *file_size = 0;
  if(!join_path(path, dir, name))
    return SAVE_STORE_FAILED;
  if(!io->exists(io->context, path))
    return SAVE_STORE_NOT_FOUND;
  if(!io->read(io->context, path, data, capacity, &size, &length))
    return SAVE_STORE_FAILED;
  if(length == 0)
    return SAVE_STORE_NOT_FOUND;
  /* Stopping before the end of the file, or of capacity, is a failed
     read. */
  if(size != (length < capacity ? length : capacity))
    return SAVE_STORE_FAILED;
  *file_size = length;
  return SAVE_STORE_OK;
}

SaveStoreResult save_store_write(const SaveStoreIo *io, const char *dir,
 const char *name, const uint8_t *data, size_t size)
{
  char path[SAVE_STORE_PATH_MAX];

  if(!join_path(path, dir, name) || !io->write(io->context, path, data, size))
    return SAVE_STORE_FAILED;
  return SAVE_STORE_OK;
}
