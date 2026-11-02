/* Crash-safe load and save of a ConfigModel file. File access goes through
   ConfigStoreIo so the host tests can inject failures. Save writes and
   checks the temporary file, keeps the last good primary as the recovery
   copy, then renames the new file into place; load falls back to the
   recovery copy when the primary is missing or invalid. */

#include "config_store.h"

static int io_valid(const ConfigStoreIo *io)
{
  return io != NULL && io->read != NULL && io->write != NULL &&
         io->flush != NULL && io->move != NULL && io->remove != NULL &&
         io->exists != NULL;
}

/* True if path reads and decodes; model may be NULL to only validate. */
static int read_valid_model(const ConfigStoreIo *io, const char *path,
 ConfigModel *model)
{
  uint8_t data[CONFIG_MODEL_ENCODED_SIZE];
  ConfigModel decoded;
  size_t size;

  if(!io->read(io->context, path, data, sizeof(data), &size) ||
     config_model_decode(data, size, &decoded) == CONFIG_MODEL_DECODE_INVALID)
    return 0;
  if(model != NULL)
    *model = decoded;
  return 1;
}

ConfigStoreResult config_store_load(const ConfigStoreIo *io,
 const char *primary_path, const char *recovery_path, ConfigModel *model,
 int *used_recovery)
{
  if(!io_valid(io) || primary_path == NULL || recovery_path == NULL ||
     model == NULL)
    return CONFIG_STORE_INVALID_ARGUMENT;

  if(read_valid_model(io, primary_path, model))
  {
    if(used_recovery != NULL)
      *used_recovery = 0;
    return CONFIG_STORE_OK;
  }

  if(read_valid_model(io, recovery_path, model))
  {
    if(used_recovery != NULL)
      *used_recovery = 1;
    return CONFIG_STORE_OK;
  }

  return CONFIG_STORE_NOT_FOUND;
}

/* Put the recovery copy back as the primary after a failed save. Returns
   original_result if that worked, CONFIG_STORE_RESTORE_FAILED if not. */
static ConfigStoreResult restore_recovery(const ConfigStoreIo *io,
 const char *primary_path, const char *recovery_path,
 ConfigStoreResult original_result)
{
  if(io->exists(io->context, primary_path))
    io->remove(io->context, primary_path);
  if(!io->exists(io->context, recovery_path) ||
     !io->move(io->context, recovery_path, primary_path) ||
     !io->flush(io->context) ||
     !read_valid_model(io, primary_path, NULL))
    return CONFIG_STORE_RESTORE_FAILED;
  return original_result;
}

ConfigStoreResult config_store_save(const ConfigStoreIo *io,
 const char *primary_path, const char *temporary_path,
 const char *recovery_path, const ConfigModel *model)
{
  uint8_t encoded[CONFIG_MODEL_ENCODED_SIZE];
  size_t encoded_size;
  int primary_was_valid;
  ConfigStoreResult result;

  if(!io_valid(io) || primary_path == NULL || temporary_path == NULL ||
     recovery_path == NULL || model == NULL ||
     !config_model_encode(model, encoded, sizeof(encoded), &encoded_size))
    return CONFIG_STORE_INVALID_ARGUMENT;

  if(io->exists(io->context, temporary_path))
    io->remove(io->context, temporary_path);
  if(!io->write(io->context, temporary_path, encoded, encoded_size))
    return CONFIG_STORE_WRITE_FAILED;
  if(!io->flush(io->context))
  {
    io->remove(io->context, temporary_path);
    return CONFIG_STORE_FLUSH_FAILED;
  }
  if(!read_valid_model(io, temporary_path, NULL))
  {
    io->remove(io->context, temporary_path);
    return CONFIG_STORE_VALIDATION_FAILED;
  }

  primary_was_valid = read_valid_model(io, primary_path, NULL);
  if(primary_was_valid)
  {
    if(io->exists(io->context, recovery_path) &&
       !io->remove(io->context, recovery_path))
    {
      io->remove(io->context, temporary_path);
      return CONFIG_STORE_BACKUP_FAILED;
    }
    if(!io->move(io->context, primary_path, recovery_path))
    {
      io->remove(io->context, temporary_path);
      return CONFIG_STORE_BACKUP_FAILED;
    }
  }
  else if(io->exists(io->context, primary_path) &&
          !io->remove(io->context, primary_path))
  {
    io->remove(io->context, temporary_path);
    return CONFIG_STORE_BACKUP_FAILED;
  }

  if(!io->move(io->context, temporary_path, primary_path))
  {
    if(primary_was_valid)
      return restore_recovery(io, primary_path, recovery_path,
                              CONFIG_STORE_PROMOTE_FAILED);
    return CONFIG_STORE_PROMOTE_FAILED;
  }

  if(!io->flush(io->context))
    result = CONFIG_STORE_FLUSH_FAILED;
  else if(!read_valid_model(io, primary_path, NULL))
    result = CONFIG_STORE_VALIDATION_FAILED;
  else
    return CONFIG_STORE_OK;

  if(primary_was_valid)
    return restore_recovery(io, primary_path, recovery_path, result);
  io->remove(io->context, primary_path);
  return result;
}
