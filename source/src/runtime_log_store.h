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

int runtime_log_store_recover(const RuntimeLogStoreIo *io,
 const char *primary, const char *temporary, const char *backup);
int runtime_log_store_save(const RuntimeLogStoreIo *io,
 const char *primary, const char *temporary, const char *backup,
 const uint8_t *data, size_t size);
int runtime_log_store_rotate(const RuntimeLogStoreIo *io,
 const char *const *slots, size_t count);

#endif /* RUNTIME_LOG_STORE_H */
