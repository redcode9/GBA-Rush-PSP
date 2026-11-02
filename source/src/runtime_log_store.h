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
 * Crash-safe file replacement and rotation for the runtime log, written
 * against a small I/O interface so the host tests can inject failures.
 */

#ifndef RUNTIME_LOG_STORE_H
#define RUNTIME_LOG_STORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
  void *context;
  int (*exists)(void *context, const char *path);
  int (*write)(void *context, const char *path,
   const uint8_t *data, size_t size);
  int (*remove)(void *context, const char *path);
  int (*rename)(void *context, const char *source, const char *target);
} RuntimeLogStoreIo;

/* Cleans up after an interrupted save: drops the temporary file and puts
   the backup back if the primary is missing. */
int runtime_log_store_recover(const RuntimeLogStoreIo *io,
 const char *primary, const char *temporary, const char *backup);
/* Writes temporary, moves primary to backup, renames temporary to primary,
   then drops the backup. Some complete copy exists at every step. */
int runtime_log_store_save(const RuntimeLogStoreIo *io,
 const char *primary, const char *temporary, const char *backup,
 const uint8_t *data, size_t size);
/* Shifts slots[i] to slots[i + 1], dropping the last one. */
int runtime_log_store_rotate(const RuntimeLogStoreIo *io,
 const char *const *slots, size_t count);

#endif /* RUNTIME_LOG_STORE_H */
