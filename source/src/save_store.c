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
 * backup always holds the previous verified image, unless that image is
 * kept as the .orig.
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
         join_path(transaction->backup, dir, name, ".bak") &&
         join_path(transaction->original, dir, name, ".orig");
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
  /* A cut after the old file became the .orig; the .tmp may also be a
     first write's, never verified, so the .orig, the old file, comes
     back. */
  if(io->exists(io->context, paths->original) &&
     io->exists(io->context, paths->temporary))
    return moved(io, paths->original, paths->primary) &&
           io->remove(io->context, paths->temporary);
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

SaveStoreResult save_store_remove(const SaveStoreIo *io, const char *dir,
 const char *name)
{
  static SaveStoreWrite paths;

  if(!set_paths(&paths, dir, name) ||
     !io->remove(io->context, paths.original) ||
     !io->remove(io->context, paths.temporary) ||
     !io->remove(io->context, paths.backup) ||
     !io->remove(io->context, paths.primary))
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

/* Puts the old file back under the file's name from old, its .bak or its
   .orig, deleting what a failed promotion left there. */
static SaveStoreResult restore_backup(const SaveStoreWrite *transaction,
 const char *old, SaveStoreResult result)
{
  const SaveStoreIo *io = transaction->io;

  if(io->remove(io->context, transaction->primary) &&
     moved(io, old, transaction->primary))
    return result;
  return SAVE_STORE_RESTORE_FAILED;
}

/* 1 if the old file is to be kept as the .orig: not empty, of another
   size than the image being written, and no .orig yet (see save_store.h).
   -1 if its size will not read. The size goes first, so a routine write
   asks nothing more. */
static int keeps_original(const SaveStoreWrite *transaction)
{
  const SaveStoreIo *io = transaction->io;
  uint8_t none;
  size_t size = 0;
  size_t length = 0;

  /* Capacity 0 reads the length only. */
  if(!io->read(io->context, transaction->primary, &none, 0, &size, &length))
    return -1;
  return length != 0 && length != transaction->size &&
         !io->exists(io->context, transaction->original);
}

/* Once the .tmp checks out: the old file becomes the .bak, replacing the
   one before it, or the .orig if it is to be kept, and the .tmp takes the
   file's name. */
static SaveStoreResult promote(const SaveStoreWrite *transaction)
{
  const SaveStoreIo *io = transaction->io;
  const char *old = NULL;
  int keep;

  if(io->exists(io->context, transaction->primary))
  {
    keep = keeps_original(transaction);
    if(keep < 0 || !io->remove(io->context, transaction->backup))
      return SAVE_STORE_BACKUP_FAILED;
    old = keep ? transaction->original : transaction->backup;
    if(!moved(io, transaction->primary, old))
    {
      if(io->exists(io->context, transaction->primary))
        return SAVE_STORE_BACKUP_FAILED;
      return restore_backup(transaction, old, SAVE_STORE_BACKUP_FAILED);
    }
  }
  if(moved(io, transaction->temporary, transaction->primary) &&
     io->flush(io->context, transaction->primary))
    return SAVE_STORE_OK;
  if(old != NULL)
    return restore_backup(transaction, old, SAVE_STORE_PROMOTE_FAILED);
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
  int free_slot = 0;
  int i;

  /* The write's .link1.orig is the longest name: a push that could not
     write must not rotate the generations away. */
  if(!join_path(links[0], dir, name, ".link1.orig") ||
     !join_path(links[0], dir, name, ".link1") ||
     !join_path(links[1], dir, name, ".link2") ||
     !join_path(links[2], dir, name, ".link3"))
    return SAVE_STORE_FAILED;
  /* Shift only up to the first free name: a push that stopped before
     writing .link1 left one, and filling it costs no generation. */
  while(free_slot < 2 && io->exists(io->context, links[free_slot]))
    free_slot++;
  if(free_slot == 2 && !io->remove(io->context, links[2]))
    return SAVE_STORE_FAILED;
  /* The highest first, so each rename's target is free. */
  for(i = free_slot - 1; i >= 0; i--)
  {
    if(!moved(io, links[i], links[i + 1]))
      return SAVE_STORE_FAILED;
  }
  /* .link1 is gone, so the write keeps no .link1.bak. */
  return save_store_write(io, "", links[0], data, size);
}
