/* Crash-safe load and save of a settings file (ConfigModel). All file
   access goes through ConfigStoreIo so tests can inject failures. */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "config_model.h"

typedef struct
{
  void *context;
  int (*read)(void *context, const char *path, uint8_t *data,
              size_t capacity, size_t *size);
  int (*write)(void *context, const char *path, const uint8_t *data,
               size_t size);
  int (*flush)(void *context);
  int (*move)(void *context, const char *source, const char *target);
  int (*remove)(void *context, const char *path);
  int (*exists)(void *context, const char *path);
} ConfigStoreIo;

typedef enum
{
  CONFIG_STORE_OK = 0,
  CONFIG_STORE_INVALID_ARGUMENT,
  CONFIG_STORE_NOT_FOUND,
  CONFIG_STORE_WRITE_FAILED,
  CONFIG_STORE_FLUSH_FAILED,
  CONFIG_STORE_VALIDATION_FAILED,
  CONFIG_STORE_BACKUP_FAILED,
  CONFIG_STORE_PROMOTE_FAILED,
  CONFIG_STORE_RESTORE_FAILED
} ConfigStoreResult;

/* Reads primary, or the recovery copy if primary is missing or invalid;
   *used_recovery tells which. */
ConfigStoreResult config_store_load(const ConfigStoreIo *io,
 const char *primary_path, const char *recovery_path, ConfigModel *model,
 int *used_recovery);
/* Writes and checks temporary, moves the old primary to recovery only if it
   is valid (an invalid one is deleted, keeping the older recovery), then
   moves temporary into place; if a later step fails, that old primary is
   moved back. */
ConfigStoreResult config_store_save(const ConfigStoreIo *io,
 const char *primary_path, const char *temporary_path,
 const char *recovery_path, const ConfigModel *model);

#endif
