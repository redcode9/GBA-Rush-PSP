#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime_log.h"
#include "runtime_log_store.h"

typedef struct
{
  char path[24];
  uint8_t data[64];
  size_t size;
  int valid;
} FakeLogFile;

typedef struct
{
  FakeLogFile files[8];
  int fail_promote;
} FakeLogStore;

static FakeLogFile *fake_file(FakeLogStore *store, const char *path,
 int create)
{
  size_t index;
  FakeLogFile *empty = NULL;

  for(index = 0; index < 8; index++)
  {
    if(store->files[index].valid &&
       strcmp(store->files[index].path, path) == 0)
      return &store->files[index];
    if(!store->files[index].valid && empty == NULL)
      empty = &store->files[index];
  }
  if(!create || empty == NULL)
    return NULL;
  memset(empty, 0, sizeof(*empty));
  strncpy(empty->path, path, sizeof(empty->path) - 1u);
  empty->valid = 1;
  return empty;
}

static int fake_exists(void *context, const char *path)
{
  return fake_file((FakeLogStore *)context, path, 0) != NULL;
}

static int fake_write(void *context, const char *path,
 const uint8_t *data, size_t size)
{
  FakeLogFile *file;

  if(size > sizeof(((FakeLogFile *)0)->data))
    return 0;
  file = fake_file((FakeLogStore *)context, path, 1);
  if(file == NULL)
    return 0;
  memcpy(file->data, data, size);
  file->size = size;
  return 1;
}

static int fake_remove(void *context, const char *path)
{
  FakeLogFile *file = fake_file((FakeLogStore *)context, path, 0);

  if(file != NULL)
    memset(file, 0, sizeof(*file));
  return 1;
}

static int fake_rename(void *context, const char *source,
 const char *target)
{
  FakeLogStore *store = (FakeLogStore *)context;
  FakeLogFile *source_file = fake_file(store, source, 0);
  FakeLogFile copy;

  if(source_file == NULL ||
     (store->fail_promote && strcmp(source, "current.tmp") == 0))
    return 0;
  copy = *source_file;
  fake_remove(store, target);
  memset(source_file, 0, sizeof(*source_file));
  source_file = fake_file(store, target, 1);
  if(source_file == NULL)
    return 0;
  memcpy(source_file->data, copy.data, copy.size);
  source_file->size = copy.size;
  return 1;
}

static RuntimeLogStoreIo fake_io(FakeLogStore *store)
{
  RuntimeLogStoreIo io;

  io.context = store;
  io.exists = fake_exists;
  io.write = fake_write;
  io.remove = fake_remove;
  io.rename = fake_rename;
  return io;
}

static void seed_file(FakeLogStore *store, const char *path, uint8_t value)
{
  FakeLogFile *file = fake_file(store, path, 1);

  assert(file != NULL);
  file->data[0] = value;
  file->size = 1;
}

static uint32_t read_u32(const uint8_t *input)
{
  return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
   ((uint32_t)input[2] << 8) | input[3];
}

static uint16_t read_u16(const uint8_t *input)
{
  return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static void test_ring_retains_latest_records(void)
{
  RuntimeLogRecord records[RUNTIME_LOG_RING_CAPACITY];
  RuntimeLogRing ring;
  size_t count;
  uint32_t event;

  runtime_log_ring_init(&ring, RUNTIME_LOG_SOURCE_MAIN);
  for(event = 1; event <= RUNTIME_LOG_RING_CAPACITY + 20u; event++)
  {
    runtime_log_ring_emit(&ring, event * 10u, event,
     RUNTIME_LOG_EVENT_LINK_STATE, 0, event, 0, 0);
  }

  count = runtime_log_ring_snapshot(&ring, records,
   RUNTIME_LOG_RING_CAPACITY);
  assert(count == RUNTIME_LOG_RING_CAPACITY);
  assert(records[0].ordinal == 21u);
  assert(records[0].value[0] == 21u);
  assert(records[count - 1].ordinal == RUNTIME_LOG_RING_CAPACITY + 20u);
  assert(records[count - 1].source == RUNTIME_LOG_SOURCE_MAIN);
  assert(runtime_log_ring_dropped(&ring) == 20u);
}

static void test_critical_ring_tracks_its_smaller_retention_window(void)
{
  RuntimeLogRecord records[RUNTIME_LOG_CRITICAL_CAPACITY];
  RuntimeLogRing ring;
  size_t count;
  uint32_t event;

  runtime_log_ring_init(&ring, RUNTIME_LOG_SOURCE_CRITICAL);
  for(event = 1; event <= RUNTIME_LOG_CRITICAL_CAPACITY + 7u; event++)
    runtime_log_ring_emit(&ring, event, event,
     RUNTIME_LOG_EVENT_LINK_CHECKPOINT, 0, event, 0, 0);

  count = runtime_log_ring_snapshot(&ring, records,
   RUNTIME_LOG_CRITICAL_CAPACITY);
  assert(count == RUNTIME_LOG_CRITICAL_CAPACITY);
  assert(records[0].ordinal == 8u);
  assert(records[count - 1].ordinal ==
   RUNTIME_LOG_CRITICAL_CAPACITY + 7u);
  assert(runtime_log_ring_dropped(&ring) == 7u);
}

static void test_benchmark_survives_shutdown_checkpoint(void)
{
  RuntimeLogRing ring;
  RuntimeLogRecord records[RUNTIME_LOG_CRITICAL_CAPACITY];
  size_t count;
  unsigned int index;
  const unsigned int summary_records = 14u;
  const unsigned int checkpoint_records = 32u;

  runtime_log_ring_init(&ring, RUNTIME_LOG_SOURCE_CRITICAL);
  for(index = 0; index < RUNTIME_LOG_CRITICAL_CAPACITY; index++)
    runtime_log_ring_emit(&ring, index, 0, RUNTIME_LOG_EVENT_LINK_STATE,
     0, 0, 0, 0);
  for(index = 0; index < summary_records; index++)
    runtime_log_ring_emit(&ring, 100u + index, 0,
     RUNTIME_LOG_EVENT_LINK_BENCHMARK, (uint8_t)index, 7u, index, 0);
  /* main_link_handle_closed logs the result before the teardown checkpoint. */
  for(index = 0; index < checkpoint_records; index++)
    runtime_log_ring_emit(&ring, 200u + index, 0,
     RUNTIME_LOG_EVENT_LINK_CHECKPOINT, (uint8_t)index, 0, 0, 0);

  count = runtime_log_ring_snapshot(&ring, records,
   RUNTIME_LOG_CRITICAL_CAPACITY);
  assert(count >= summary_records + checkpoint_records);
  for(index = 0; index < summary_records; index++)
  {
    const RuntimeLogRecord *record = &records[
     count - summary_records - checkpoint_records + index];
    assert(record->type == RUNTIME_LOG_EVENT_LINK_BENCHMARK);
    assert(record->flags == index && record->value[0] == 7u);
  }
}

static void test_snapshot_at_ordinal_limit_is_bounded(void)
{
  RuntimeLogRecord records[2];
  RuntimeLogRing ring;
  uint32_t ordinal;
  uint32_t index;

  runtime_log_ring_init(&ring, RUNTIME_LOG_SOURCE_MAIN);
  for(ordinal = UINT32_MAX - 1u; ordinal != 0; ordinal++)
  {
    index = (ordinal - 1u) & (RUNTIME_LOG_RING_CAPACITY - 1u);
    ring.records[index].ordinal = ordinal;
    ring.records[index].source = RUNTIME_LOG_SOURCE_MAIN;
    ring.published[index] = ordinal;
  }
  ring.next_ordinal = UINT32_MAX;

  assert(runtime_log_ring_snapshot(&ring, records, 2) == 2u);
  assert(records[0].ordinal == UINT32_MAX - 1u);
  assert(records[1].ordinal == UINT32_MAX);
}

static void test_encode_and_validate(void)
{
  uint8_t encoded[RUNTIME_LOG_MAX_FILE_SIZE];
  RuntimeLogMetadata decoded;
  RuntimeLogMetadata metadata;
  RuntimeLogRing main_ring;
  RuntimeLogRing network_ring;
  RuntimeLogRing critical_ring;
  uint16_t main_count = 0;
  uint16_t network_count = 0;
  uint16_t critical_count = 0;
  size_t written = 0;

  memset(&metadata, 0, sizeof(metadata));
  metadata.session_id = 0x12345678u;
  metadata.flush_reason = RUNTIME_LOG_FLUSH_LINK_FAILURE;
  metadata.psp_model = 1;
  metadata.firmware = 0x06060110u;
  metadata.cpu_clock_mhz = 333;
  metadata.rom_identity = 0x42505249u;
  metadata.started_us = 100;
  metadata.flushed_us = 900;

  runtime_log_ring_init(&main_ring, RUNTIME_LOG_SOURCE_MAIN);
  runtime_log_ring_init(&network_ring, RUNTIME_LOG_SOURCE_NETWORK);
  runtime_log_ring_init(&critical_ring, RUNTIME_LOG_SOURCE_CRITICAL);
  runtime_log_ring_emit(&main_ring, 110, 7, RUNTIME_LOG_EVENT_LINK_STATE,
   0, 6, 0, 0);
  runtime_log_ring_emit(&network_ring, 120, 0,
   RUNTIME_LOG_EVENT_LINK_RECOVERY, 1, 2, 3, 4);
  runtime_log_ring_emit(&critical_ring, 130, 8,
   RUNTIME_LOG_EVENT_LINK_CHECKPOINT, 0, 6, 0, 0);

  assert(runtime_log_encode(&main_ring, &network_ring, &critical_ring,
   &metadata, encoded, sizeof(encoded), &written));
  assert(written == RUNTIME_LOG_HEADER_SIZE +
   (3u * RUNTIME_LOG_RECORD_SIZE));
  assert(runtime_log_validate(encoded, written, &decoded, &main_count,
   &network_count, &critical_count));
  assert(read_u16(encoded + 4) == RUNTIME_LOG_VERSION);
  assert(RUNTIME_LOG_VERSION == 5u);
  assert(main_count == 1);
  assert(network_count == 1);
  assert(critical_count == 1);
  assert(decoded.session_id == metadata.session_id);
  assert(decoded.rom_identity == metadata.rom_identity);
  assert(read_u32(encoded + RUNTIME_LOG_HEADER_SIZE + 20) == 6u);

  encoded[written - 1] ^= 1u;
  assert(!runtime_log_validate(encoded, written, NULL, NULL, NULL, NULL));
}

static void test_invalid_arguments(void)
{
  RuntimeLogRing ring;
  RuntimeLogMetadata metadata;
  uint8_t encoded[RUNTIME_LOG_HEADER_SIZE - 1u];
  size_t written = 0;

  memset(&metadata, 0, sizeof(metadata));
  runtime_log_ring_init(&ring, RUNTIME_LOG_SOURCE_MAIN);
  assert(runtime_log_ring_snapshot(NULL, NULL, 0) == 0);
  assert(runtime_log_ring_dropped(NULL) == 0);
  assert(!runtime_log_encode(&ring, &ring, NULL, &metadata, encoded,
   sizeof(encoded), &written));
  assert(!runtime_log_validate(NULL, 0, NULL, NULL, NULL, NULL));
}

static void test_maximum_current_snapshot_size(void)
{
  uint8_t encoded[RUNTIME_LOG_MAX_FILE_SIZE];
  RuntimeLogMetadata metadata;
  RuntimeLogRing main_ring;
  RuntimeLogRing network_ring;
  RuntimeLogRing critical_ring;
  uint16_t main_count = 0;
  uint16_t network_count = 0;
  uint16_t critical_count = 0;
  size_t written = 0;
  uint32_t index;

  memset(&metadata, 0, sizeof(metadata));
  runtime_log_ring_init(&main_ring, RUNTIME_LOG_SOURCE_MAIN);
  runtime_log_ring_init(&network_ring, RUNTIME_LOG_SOURCE_NETWORK);
  runtime_log_ring_init(&critical_ring, RUNTIME_LOG_SOURCE_CRITICAL);
  for(index = 0; index < RUNTIME_LOG_RING_CAPACITY; index++)
  {
    runtime_log_ring_emit(&main_ring, index, index,
     RUNTIME_LOG_EVENT_LINK_STATE, 0, index, 0, 0);
    runtime_log_ring_emit(&network_ring, index, 0,
     RUNTIME_LOG_EVENT_LINK_RECOVERY, 0, index, 0, 0);
  }
  for(index = 0; index < RUNTIME_LOG_CRITICAL_CAPACITY + 4u; index++)
    runtime_log_ring_emit(&critical_ring, index, index,
     RUNTIME_LOG_EVENT_LINK_CHECKPOINT, 0, index, 0, 0);

  assert(runtime_log_encode(&main_ring, &network_ring, &critical_ring,
   &metadata, encoded, sizeof(encoded), &written));
  assert(written == RUNTIME_LOG_MAX_FILE_SIZE);
  assert(runtime_log_validate(encoded, written, NULL, &main_count,
   &network_count, &critical_count));
  assert(main_count == RUNTIME_LOG_RING_CAPACITY);
  assert(network_count == RUNTIME_LOG_RING_CAPACITY);
  assert(critical_count == RUNTIME_LOG_CRITICAL_CAPACITY);
}

static void test_store_preserves_previous_log_on_failed_promote(void)
{
  static const uint8_t updated[] = { 4, 5, 6 };
  FakeLogStore store;
  RuntimeLogStoreIo io;
  FakeLogFile *primary;

  memset(&store, 0, sizeof(store));
  io = fake_io(&store);
  seed_file(&store, "current.bin", 1);
  store.fail_promote = 1;
  assert(!runtime_log_store_save(&io, "current.bin", "current.tmp",
   "current.bak", updated, sizeof(updated)));
  primary = fake_file(&store, "current.bin", 0);
  assert(primary != NULL && primary->size == 1 && primary->data[0] == 1);

  store.fail_promote = 0;
  assert(runtime_log_store_save(&io, "current.bin", "current.tmp",
   "current.bak", updated, sizeof(updated)));
  primary = fake_file(&store, "current.bin", 0);
  assert(primary != NULL && primary->size == sizeof(updated));
  assert(memcmp(primary->data, updated, sizeof(updated)) == 0);
  assert(!fake_exists(&store, "current.tmp"));
  assert(!fake_exists(&store, "current.bak"));
}

static void test_store_rotates_four_sessions(void)
{
  const char *slots[] = { "log1", "log2", "log3", "log4" };
  FakeLogStore store;
  RuntimeLogStoreIo io;

  memset(&store, 0, sizeof(store));
  io = fake_io(&store);
  seed_file(&store, "log1", 1);
  seed_file(&store, "log2", 2);
  seed_file(&store, "log3", 3);
  seed_file(&store, "log4", 4);
  assert(runtime_log_store_rotate(&io, slots, 4));
  assert(!fake_exists(&store, "log1"));
  assert(fake_file(&store, "log2", 0)->data[0] == 1);
  assert(fake_file(&store, "log3", 0)->data[0] == 2);
  assert(fake_file(&store, "log4", 0)->data[0] == 3);
}

int main(void)
{
  test_ring_retains_latest_records();
  test_critical_ring_tracks_its_smaller_retention_window();
  test_benchmark_survives_shutdown_checkpoint();
  test_snapshot_at_ordinal_limit_is_bounded();
  test_encode_and_validate();
  test_invalid_arguments();
  test_maximum_current_snapshot_size();
  test_store_preserves_previous_log_on_failed_promote();
  test_store_rotates_four_sessions();
  puts("Runtime log tests passed");
  return 0;
}
