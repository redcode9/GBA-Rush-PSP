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
 * The .sav load and its write transaction; see save_store.h. The write
 * follows config_store.c: a checked temporary file, the old file kept as
 * the backup, then a rename into place. Here the temporary file is
 * checked by its CRC32, every rename is checked on both names, and the
 * backup always holds the previous verified image.
 */

#include <stdio.h>

#include "save_store.h"
#include "zlib/zlib.h"

/* dir, name and suffix joined; 0 if that does not fit, since a cut path
   would name another file. */
static int join_path(char *path, const char *dir, const char *name,
 const char *suffix)
{
  int length = snprintf(path, SAVE_STORE_PATH_MAX, "%s%s%s", dir, name,
   suffix);

  return length >= 0 && length < SAVE_STORE_PATH_MAX;
}

static int set_paths(SaveStoreWrite *transaction, const char *dir,
 const char *name)
{
  return join_path(transaction->primary, dir, name, "") &&
         join_path(transaction->temporary, dir, name, ".tmp") &&
         join_path(transaction->backup, dir, name, ".bak");
}

/* A rename both names confirm: a device can report one that did not
   happen, or fail one that did. */
static int moved(const SaveStoreIo *io, const char *source,
 const char *target)
{
  return io->move(io->context, source, target) &&
         io->exists(io->context, target) && !io->exists(io->context, source);
}

/* See save_store_recover(). */
static int recover(const SaveStoreIo *io, const SaveStoreWrite *paths)
{
  if(io->exists(io->context, paths->primary))
    return io->remove(io->context, paths->temporary);
  if(io->exists(io->context, paths->backup))
  {
    if(io->exists(io->context, paths->temporary))
      return moved(io, paths->temporary, paths->primary);
    return moved(io, paths->backup, paths->primary);
  }
  return io->remove(io->context, paths->temporary);
}

SaveStoreResult save_store_load(const SaveStoreIo *io, const char *dir,
 const char *name, uint8_t *data, size_t capacity, size_t *file_size)
{
  char path[SAVE_STORE_PATH_MAX];
  size_t size = 0;
  size_t length = 0;

  *file_size = 0;
  if(!join_path(path, dir, name, ""))
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

SaveStoreResult save_store_recover(const SaveStoreIo *io, const char *dir,
 const char *name)
{
  static SaveStoreWrite paths;

  if(!set_paths(&paths, dir, name) || !recover(io, &paths))
    return SAVE_STORE_FAILED;
  return SAVE_STORE_OK;
}

SaveStoreResult save_store_begin(SaveStoreWrite *transaction,
 const SaveStoreIo *io, const char *dir, const char *name)
{
  transaction->io = io;
  transaction->file = -1;
  transaction->crc = crc32(0L, Z_NULL, 0);
  transaction->size = 0;
  transaction->result = SAVE_STORE_FAILED;
  if(!set_paths(transaction, dir, name) || !recover(io, transaction))
    return SAVE_STORE_FAILED;

  transaction->file = io->open_write(io->context, transaction->temporary);
  transaction->result = (transaction->file < 0) ? SAVE_STORE_WRITE_FAILED :
   SAVE_STORE_OK;
  return transaction->result;
}

SaveStoreResult save_store_append(SaveStoreWrite *transaction,
 const uint8_t *data, size_t size)
{
  const SaveStoreIo *io = transaction->io;
  size_t done = 0;
  int count;

  while(transaction->result == SAVE_STORE_OK && done < size)
  {
    count = io->write(io->context, transaction->file, data + done,
     size - done);
    if(count <= 0 || (size_t)count > size - done)
      transaction->result = SAVE_STORE_WRITE_FAILED;
    else
    {
      transaction->crc = crc32(transaction->crc, data + done, (uInt)count);
      transaction->size += (size_t)count;
      done += (size_t)count;
    }
  }
  return transaction->result;
}

/* Puts the .bak, the old file, back under the file's name, deleting what
   a failed promotion left there. */
static SaveStoreResult restore_backup(const SaveStoreWrite *transaction,
 SaveStoreResult result)
{
  const SaveStoreIo *io = transaction->io;

  if(io->remove(io->context, transaction->primary) &&
     moved(io, transaction->backup, transaction->primary))
    return result;
  return SAVE_STORE_RESTORE_FAILED;
}

/* Once the .tmp checks out: the old file becomes the .bak, replacing the
   one before it, and the .tmp takes the file's name. */
static SaveStoreResult promote(const SaveStoreWrite *transaction)
{
  const SaveStoreIo *io = transaction->io;
  int had_primary = io->exists(io->context, transaction->primary);

  if(had_primary)
  {
    if(!io->remove(io->context, transaction->backup))
      return SAVE_STORE_BACKUP_FAILED;
    if(!moved(io, transaction->primary, transaction->backup))
    {
      if(io->exists(io->context, transaction->primary))
        return SAVE_STORE_BACKUP_FAILED;
      return restore_backup(transaction, SAVE_STORE_BACKUP_FAILED);
    }
  }
  if(moved(io, transaction->temporary, transaction->primary) &&
     io->flush(io->context, transaction->primary))
    return SAVE_STORE_OK;
  if(had_primary)
    return restore_backup(transaction, SAVE_STORE_PROMOTE_FAILED);
  return SAVE_STORE_PROMOTE_FAILED;
}

SaveStoreResult save_store_commit(SaveStoreWrite *transaction)
{
  const SaveStoreIo *io = transaction->io;
  uint32_t crc = 0;
  size_t size = 0;

  if(transaction->file >= 0)
  {
    if(!io->close(io->context, transaction->file) &&
       transaction->result == SAVE_STORE_OK)
      transaction->result = SAVE_STORE_WRITE_FAILED;
    transaction->file = -1;
  }
  if(transaction->result == SAVE_STORE_OK &&
     !io->flush(io->context, transaction->temporary))
    transaction->result = SAVE_STORE_WRITE_FAILED;
  if(transaction->result != SAVE_STORE_OK)
    return transaction->result;

  /* A close that lost data shows here. */
  if(!io->crc32(io->context, transaction->temporary, &crc, &size) ||
     crc != transaction->crc || size != transaction->size)
    transaction->result = SAVE_STORE_VERIFY_FAILED;
  else
    transaction->result = promote(transaction);
  return transaction->result;
}

SaveStoreResult save_store_write(const SaveStoreIo *io, const char *dir,
 const char *name, const uint8_t *data, size_t size)
{
  static SaveStoreWrite transaction;

  save_store_begin(&transaction, io, dir, name);
  save_store_append(&transaction, data, size);
  return save_store_commit(&transaction);
}

SaveStoreResult save_store_ring_push(const SaveStoreIo *io, const char *dir,
 const char *name, const uint8_t *data, size_t size)
{
  static char links[3][SAVE_STORE_PATH_MAX];
  int i;

  /* The write's .link1.tmp and .link1.bak are the longest names: a push
     that could not write must not rotate the generations away. */
  if(!join_path(links[0], dir, name, ".link1.tmp") ||
     !join_path(links[0], dir, name, ".link1") ||
     !join_path(links[1], dir, name, ".link2") ||
     !join_path(links[2], dir, name, ".link3") ||
     !io->remove(io->context, links[2]))
    return SAVE_STORE_FAILED;
  /* .link2 first, so each rename's target is free. */
  for(i = 1; i >= 0; i--)
  {
    if(io->exists(io->context, links[i]) &&
       !moved(io, links[i], links[i + 1]))
      return SAVE_STORE_FAILED;
  }
  /* .link1 is gone, so the write keeps no .link1.bak. */
  return save_store_write(io, "", links[0], data, size);
}
