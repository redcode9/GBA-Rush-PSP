#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config_store.h"

#define FAKE_FILE_COUNT 3

typedef struct
{
  const char *path;
  uint8_t data[CONFIG_MODEL_ENCODED_SIZE];
  size_t size;
  int present;
} FakeFile;

typedef struct
{
  FakeFile files[FAKE_FILE_COUNT];
  int fail_write;
  int fail_flush;
  int fail_promote;
  int fail_restore;
  int corrupt_temp;
  int corrupt_primary;
} FakeStore;

static FakeFile *find_file(FakeStore *store, const char *path)
{
  size_t i;

  for(i = 0; i < FAKE_FILE_COUNT; i++)
  {
    if(strcmp(store->files[i].path, path) == 0)
      return &store->files[i];
  }
  return NULL;
}

static int fake_read(void *context, const char *path, uint8_t *data,
 size_t capacity, size_t *size)
{
  FakeFile *file = find_file((FakeStore *)context, path);

  if(file == NULL || !file->present || file->size > capacity)
    return 0;
  memcpy(data, file->data, file->size);
  *size = file->size;
  return 1;
}

static int fake_write(void *context, const char *path, const uint8_t *data,
 size_t size)
{
  FakeStore *store = (FakeStore *)context;
  FakeFile *file = find_file(store, path);

  if(store->fail_write || file == NULL || size > sizeof(file->data))
    return 0;
  memcpy(file->data, data, size);
  file->size = size;
  file->present = 1;
  if(store->corrupt_temp && strcmp(path, "config.tmp") == 0)
    file->data[size - 1] ^= 1;
  return 1;
}

static int fake_flush(void *context)
{
  return !((FakeStore *)context)->fail_flush;
}

static int fake_move(void *context, const char *source, const char *target)
{
  FakeStore *store = (FakeStore *)context;
  FakeFile *from = find_file(store, source);
  FakeFile *to = find_file(store, target);

  if(from == NULL || to == NULL || !from->present)
    return 0;
  if(store->fail_promote && strcmp(source, "config.tmp") == 0)
    return 0;
  if(store->fail_restore && strcmp(source, "config.bak") == 0)
    return 0;
  memcpy(to->data, from->data, from->size);
  to->size = from->size;
  to->present = 1;
  from->present = 0;
  if(store->corrupt_primary && strcmp(source, "config.tmp") == 0 &&
     strcmp(target, "config.bin") == 0)
    to->data[to->size - 1] ^= 1;
  return 1;
}

static int fake_remove(void *context, const char *path)
{
  FakeFile *file = find_file((FakeStore *)context, path);

  if(file == NULL)
    return 0;
  file->present = 0;
  file->size = 0;
  return 1;
}

static int fake_exists(void *context, const char *path)
{
  FakeFile *file = find_file((FakeStore *)context, path);

  return file != NULL && file->present;
}

static void init_store(FakeStore *store, ConfigStoreIo *io)
{
  memset(store, 0, sizeof(*store));
  store->files[0].path = "config.bin";
  store->files[1].path = "config.tmp";
  store->files[2].path = "config.bak";
  io->context = store;
  io->read = fake_read;
  io->write = fake_write;
  io->flush = fake_flush;
  io->move = fake_move;
  io->remove = fake_remove;
  io->exists = fake_exists;
}

static void seed_file(FakeStore *store, const char *path,
 const ConfigModel *model)
{
  FakeFile *file = find_file(store, path);
  size_t written;

  assert(file != NULL);
  assert(config_model_encode(model, file->data, sizeof(file->data), &written));
  file->size = written;
  file->present = 1;
}

static ConfigModel decode_file(FakeStore *store, const char *path)
{
  FakeFile *file = find_file(store, path);
  ConfigModel model;

  assert(file != NULL && file->present);
  assert(config_model_decode(file->data, file->size, &model) !=
         CONFIG_MODEL_DECODE_INVALID);
  return model;
}

static void test_atomic_save_and_recovery_copy(void)
{
  FakeStore store;
  ConfigStoreIo io;
  ConfigModel old_model;
  ConfigModel new_model;
  ConfigModel decoded;

  init_store(&store, &io);
  config_model_defaults(&old_model);
  old_model.language = 1;
  seed_file(&store, "config.bin", &old_model);

  new_model = old_model;
  new_model.language = 4;
  assert(config_store_save(&io, "config.bin", "config.tmp", "config.bak",
                           &new_model) == CONFIG_STORE_OK);
  decoded = decode_file(&store, "config.bin");
  assert(decoded.language == 4);
  decoded = decode_file(&store, "config.bak");
  assert(decoded.language == 1);
  assert(!fake_exists(&store, "config.tmp"));
}

static void test_failures_leave_previous_config_readable(void)
{
  FakeStore store;
  ConfigStoreIo io;
  ConfigModel old_model;
  ConfigModel new_model;
  ConfigModel decoded;

  init_store(&store, &io);
  config_model_defaults(&old_model);
  new_model = old_model;
  new_model.theme = 8;
  seed_file(&store, "config.bin", &old_model);

  store.fail_write = 1;
  assert(config_store_save(&io, "config.bin", "config.tmp", "config.bak",
                           &new_model) == CONFIG_STORE_WRITE_FAILED);
  decoded = decode_file(&store, "config.bin");
  assert(decoded.theme == 0);

  store.fail_write = 0;
  store.fail_flush = 1;
  assert(config_store_save(&io, "config.bin", "config.tmp", "config.bak",
                           &new_model) == CONFIG_STORE_FLUSH_FAILED);
  decoded = decode_file(&store, "config.bin");
  assert(decoded.theme == 0);

  store.fail_flush = 0;
  store.corrupt_temp = 1;
  assert(config_store_save(&io, "config.bin", "config.tmp", "config.bak",
                           &new_model) == CONFIG_STORE_VALIDATION_FAILED);
  decoded = decode_file(&store, "config.bin");
  assert(decoded.theme == 0);

  store.corrupt_temp = 0;
  store.fail_promote = 1;
  assert(config_store_save(&io, "config.bin", "config.tmp", "config.bak",
                           &new_model) == CONFIG_STORE_PROMOTE_FAILED);
  decoded = decode_file(&store, "config.bin");
  assert(decoded.theme == 0);
}

static void test_restore_failure_keeps_recovery_readable(void)
{
  FakeStore store;
  ConfigStoreIo io;
  ConfigModel old_model;
  ConfigModel new_model;
  ConfigModel loaded;
  int used_recovery = 0;

  init_store(&store, &io);
  config_model_defaults(&old_model);
  new_model = old_model;
  new_model.theme = 8;
  seed_file(&store, "config.bin", &old_model);
  store.corrupt_primary = 1;
  store.fail_restore = 1;

  assert(config_store_save(&io, "config.bin", "config.tmp", "config.bak",
                           &new_model) == CONFIG_STORE_RESTORE_FAILED);
  assert(config_store_load(&io, "config.bin", "config.bak", &loaded,
                           &used_recovery) == CONFIG_STORE_OK);
  assert(used_recovery == 1);
  assert(loaded.theme == 0);
}

static void test_failed_post_promote_validation_restores_backup(void)
{
  FakeStore store;
  ConfigStoreIo io;
  ConfigModel old_model;
  ConfigModel new_model;
  ConfigModel decoded;

  init_store(&store, &io);
  config_model_defaults(&old_model);
  new_model = old_model;
  new_model.swap_confirm_buttons = 1;
  seed_file(&store, "config.bin", &old_model);
  store.corrupt_primary = 1;

  assert(config_store_save(&io, "config.bin", "config.tmp", "config.bak",
                           &new_model) == CONFIG_STORE_VALIDATION_FAILED);
  store.corrupt_primary = 0;
  decoded = decode_file(&store, "config.bin");
  assert(decoded.swap_confirm_buttons == 0);
}

static void test_load_uses_recovery_only_when_needed(void)
{
  FakeStore store;
  ConfigStoreIo io;
  ConfigModel primary;
  ConfigModel recovery;
  ConfigModel loaded;
  int used_recovery = 0;

  init_store(&store, &io);
  config_model_defaults(&primary);
  recovery = primary;
  primary.language = 4;
  recovery.language = 2;
  seed_file(&store, "config.bin", &primary);
  seed_file(&store, "config.bak", &recovery);

  assert(config_store_load(&io, "config.bin", "config.bak", &loaded,
                           &used_recovery) == CONFIG_STORE_OK);
  assert(loaded.language == 4);
  assert(used_recovery == 0);

  find_file(&store, "config.bin")->data[0] ^= 1;
  assert(config_store_load(&io, "config.bin", "config.bak", &loaded,
                           &used_recovery) == CONFIG_STORE_OK);
  assert(loaded.language == 2);
  assert(used_recovery == 1);
}

int main(void)
{
  test_atomic_save_and_recovery_copy();
  test_failures_leave_previous_config_readable();
  test_failed_post_promote_validation_restores_backup();
  test_restore_failure_keeps_recovery_readable();
  test_load_uses_recovery_only_when_needed();
  puts("config store tests passed");
  return 0;
}
