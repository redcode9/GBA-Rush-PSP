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

#include "runtime_log_store.h"

static int runtime_log_store_valid(const RuntimeLogStoreIo *io)
{
  return io != NULL && io->exists != NULL && io->write != NULL &&
   io->remove != NULL && io->rename != NULL;
}

static int runtime_log_store_remove_if_present(const RuntimeLogStoreIo *io,
 const char *path)
{
  return !io->exists(io->context, path) || io->remove(io->context, path);
}

/* Cleans up after an interrupted save: removes a leftover temporary file,
   then either drops a stale backup or, when the primary is missing, puts
   the backup back in its place. */
int runtime_log_store_recover(const RuntimeLogStoreIo *io,
 const char *primary, const char *temporary, const char *backup)
{
  if(!runtime_log_store_valid(io) || primary == NULL || temporary == NULL ||
     backup == NULL)
    return 0;

  if(!runtime_log_store_remove_if_present(io, temporary))
    return 0;
  if(io->exists(io->context, primary))
    return runtime_log_store_remove_if_present(io, backup);
  if(io->exists(io->context, backup))
    return io->rename(io->context, backup, primary);
  return 1;
}

/* Replaces primary with data: writes temporary, moves primary to backup,
   promotes temporary, then deletes the backup. On failure the old primary
   is put back where possible; recover() handles a crash midway. */
int runtime_log_store_save(const RuntimeLogStoreIo *io,
 const char *primary, const char *temporary, const char *backup,
 const uint8_t *data, size_t size)
{
  int had_primary;

  if(!runtime_log_store_valid(io) || primary == NULL || temporary == NULL ||
     backup == NULL || data == NULL || size == 0 ||
     !runtime_log_store_recover(io, primary, temporary, backup))
    return 0;

  if(!io->write(io->context, temporary, data, size))
  {
    runtime_log_store_remove_if_present(io, temporary);
    return 0;
  }

  had_primary = io->exists(io->context, primary);
  if(had_primary && (!runtime_log_store_remove_if_present(io, backup) ||
     !io->rename(io->context, primary, backup)))
  {
    runtime_log_store_remove_if_present(io, temporary);
    return 0;
  }
  if(!io->rename(io->context, temporary, primary))
  {
    if(had_primary && !io->exists(io->context, primary))
      io->rename(io->context, backup, primary);
    runtime_log_store_remove_if_present(io, temporary);
    return 0;
  }
  return runtime_log_store_remove_if_present(io, backup);
}

/* Shifts each slot up by one (slots[count - 1] is deleted), leaving
   slots[0] free for the next session. */
int runtime_log_store_rotate(const RuntimeLogStoreIo *io,
 const char *const *slots, size_t count)
{
  size_t index;

  if(!runtime_log_store_valid(io) || slots == NULL || count < 2)
    return 0;
  for(index = count - 1u; index != 0; index--)
  {
    if(slots[index] == NULL || slots[index - 1u] == NULL ||
       !runtime_log_store_remove_if_present(io, slots[index]))
      return 0;
    if(io->exists(io->context, slots[index - 1u]) &&
       !io->rename(io->context, slots[index - 1u], slots[index]))
      return 0;
  }
  return 1;
}
