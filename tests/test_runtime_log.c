/*
 * Host tests for the diagnostic runtime log: the fixed-size event rings of
 * runtime_log.c (main, network and the smaller critical ring), the
 * big-endian file they are encoded into (read by
 * tools/decode_runtime_log.py), and runtime_log_store.c, which replaces a
 * log file safely and rotates the logs of the last four sessions.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime_log.h"
#include "runtime_log_store.h"
#include "zlib/zlib.h"

typedef struct
{
  char path[24];
  uint8_t data[64];
  size_t size;
  int valid;
} FakeLogFile;

/* In-memory file system; fail_promote makes renaming current.tmp fail. */
typedef struct
{
  FakeLogFile files[8];
  int fail_promote;
} FakeLogStore;

/* Finds path, or with create claims a free slot for it; NULL when it is
   absent or all 8 slots are taken. */
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

/* Creates or replaces path; fails over 64 bytes or with all slots taken. */
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

/* Moves source to target, replacing any existing target. */
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

/* The store's I/O callbacks, wired to the in-memory file system. */
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

/* A one-byte file whose value shows where rotation moved it. */
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

static void write_u32(uint8_t *output, uint32_t value)
{
  output[0] = (uint8_t)(value >> 24);
  output[1] = (uint8_t)(value >> 16);
  output[2] = (uint8_t)(value >> 8);
  output[3] = (uint8_t)value;
}

/* The payload CRC and then the header CRC, in the last 8 header bytes. */
static void write_crcs(uint8_t *data, size_t header_size, size_t size)
{
  uLong crc;

  crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, data + header_size, (uInt)(size - header_size));
  write_u32(data + header_size - 8u, (uint32_t)crc);
  crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, data, (uInt)(header_size - 4u));
  write_u32(data + header_size - 4u, (uint32_t)crc);
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

/* The critical ring is already full when the link benchmark summary and
   then the teardown checkpoints arrive: the summary must still be kept. */
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

/* A ring forged so that its record ordinals end at UINT32_MAX: the
   snapshot must stop there instead of wrapping around. */
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

/* Encodes one record per ring with the metadata of a session; the build id
   is the longest the header holds. */
static size_t encode_sample(uint8_t *encoded, size_t capacity)
{
  RuntimeLogMetadata metadata;
  RuntimeLogRing main_ring;
  RuntimeLogRing network_ring;
  RuntimeLogRing critical_ring;
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
  strcpy(metadata.build_id, "0123456789ab-dirty-tar.");

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
   &metadata, encoded, capacity, &written));
  return written;
}

/* One record per ring and the build id survive encode and validate, and a
   flipped payload byte fails the CRC. The version and header layout are
   pinned so that a format change also updates
   tools/decode_runtime_log.py. */
static void test_encode_and_validate(void)
{
  uint8_t encoded[RUNTIME_LOG_MAX_FILE_SIZE];
  RuntimeLogMetadata decoded;
  uint16_t main_count = 0;
  uint16_t network_count = 0;
  uint16_t critical_count = 0;
  size_t written;
  size_t index;

  written = encode_sample(encoded, sizeof(encoded));
  assert(written == RUNTIME_LOG_HEADER_SIZE +
   (3u * RUNTIME_LOG_RECORD_SIZE));
  assert(runtime_log_validate(encoded, written, &decoded, &main_count,
   &network_count, &critical_count));
  assert(read_u16(encoded + 4) == RUNTIME_LOG_VERSION);
  assert(RUNTIME_LOG_VERSION == 6u);
  assert(read_u16(encoded + 6) == 96u);
  assert(main_count == 1);
  assert(network_count == 1);
  assert(critical_count == 1);
  assert(decoded.session_id == 0x12345678u);
  assert(decoded.rom_identity == 0x42505249u);
  assert(decoded.flushed_us == 900u);
  assert(strcmp(decoded.build_id, "0123456789ab-dirty-tar.") == 0);
  /* The id at 56, NUL-padded to 80, then zero up to the CRCs at 88. */
  assert(memcmp(encoded + 56, "0123456789ab-dirty-tar.", 23) == 0);
  for(index = 79; index < 88; index++)
    assert(encoded[index] == 0);
  /* value[0] of the first record, 20 bytes into it. */
  assert(read_u32(encoded + RUNTIME_LOG_HEADER_SIZE + 20) == 6u);

  encoded[written - 1] ^= 1u;
  assert(!runtime_log_validate(encoded, written, NULL, NULL, NULL, NULL));
}

/* A version 5 file, built from a version 6 one the way runtime_log.c wrote
   it then (64-byte header, CRCs at 56 and 60), still validates and has no
   build id; a version 6 header of the old size does not. */
static void test_version_five_still_validates(void)
{
  uint8_t encoded[RUNTIME_LOG_MAX_FILE_SIZE];
  uint8_t old[RUNTIME_LOG_MAX_FILE_SIZE];
  RuntimeLogMetadata decoded;
  uint16_t main_count = 0;
  size_t written;
  size_t old_size;

  written = encode_sample(encoded, sizeof(encoded));
  old_size = written - (RUNTIME_LOG_HEADER_SIZE - RUNTIME_LOG_V5_HEADER_SIZE);
  memcpy(old, encoded, 56);
  memcpy(old + RUNTIME_LOG_V5_HEADER_SIZE, encoded + RUNTIME_LOG_HEADER_SIZE,
   written - RUNTIME_LOG_HEADER_SIZE);
  old[4] = 0;
  old[5] = 5;
  old[6] = 0;
  old[7] = RUNTIME_LOG_V5_HEADER_SIZE;
  write_crcs(old, RUNTIME_LOG_V5_HEADER_SIZE, old_size);

  memset(&decoded, 0xFF, sizeof(decoded));
  assert(runtime_log_validate(old, old_size, &decoded, &main_count, NULL,
   NULL));
  assert(main_count == 1);
  assert(decoded.session_id == 0x12345678u);
  assert(decoded.flushed_us == 900u);
  assert(decoded.build_id[0] == '\0');

  old[5] = 6;
  write_crcs(old, RUNTIME_LOG_V5_HEADER_SIZE, old_size);
  assert(!runtime_log_validate(old, old_size, NULL, NULL, NULL, NULL));
}

/* NULL arguments are rejected. The encode call fails on its NULL critical
   ring before the one-byte-short buffer is checked, so that limit is not
   covered here. */
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

/* Full rings encode to exactly RUNTIME_LOG_MAX_FILE_SIZE. */
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

/* The 12 bytes a name-probe chunk carries, value0's top byte first. */
static void probe_chunk_bytes(const RuntimeLogRecord *record, uint8_t *bytes)
{
  unsigned int index;

  for(index = 0; index < 12u; index++)
    bytes[index] = (uint8_t)(record->value[index / 4u] >>
     (24u - 8u * (index % 4u)));
}

/* Logs one probe entry into a fresh main ring and checks every record:
   header, d_name chunks, FAT chunks with zero padding, open results.
   Returns the FAT length the header reports. */
static uint32_t check_probe_entry(const uint8_t *name, size_t name_size,
 size_t name_length, const uint8_t *fat, size_t fat_capacity)
{
  static const int32_t opened[RUNTIME_LOG_NAME_PROBE_OPENS] = {
   3, (int32_t)0x80010002u, RUNTIME_LOG_NAME_PROBE_NOT_TRIED, 4,
   (int32_t)0x80020001u };
  RuntimeLogRecord records[RUNTIME_LOG_RING_CAPACITY];
  RuntimeLogRing ring;
  uint8_t bytes[12];
  size_t name_chunks = (name_length + 11u) / 12u;
  size_t fat_chunks;
  size_t count;
  size_t chunk;
  size_t index;
  size_t offset;
  uint32_t fat_length;

  runtime_log_ring_init(&ring, RUNTIME_LOG_SOURCE_MAIN);
  count = runtime_log_name_probe_entry(&ring, 500, 7, name, name_size, fat,
   fat_capacity, opened, RUNTIME_LOG_RING_CAPACITY);
  assert(count == ring.next_ordinal);
  assert(runtime_log_ring_snapshot(&ring, records,
   RUNTIME_LOG_RING_CAPACITY) == count);

  assert(records[0].type == RUNTIME_LOG_EVENT_NAME_PROBE);
  assert(records[0].flags == 0x00u);
  assert(records[0].emulated_cycle == 0u);
  assert(records[0].timestamp_us == 500u);
  assert(records[0].value[0] == 7u);
  assert(records[0].value[1] == name_length);
  fat_length = records[0].value[2];
  fat_chunks = (fat_length + 11u) / 12u;
  assert(count == 3u + name_chunks + fat_chunks);

  for(chunk = 0; chunk < name_chunks; chunk++)
  {
    assert(records[1 + chunk].flags == (0x40u | chunk));
    probe_chunk_bytes(&records[1 + chunk], bytes);
    for(index = 0; index < 12u; index++)
    {
      offset = chunk * 12u + index;
      assert(bytes[index] == (offset < name_length ? name[offset] : 0u));
    }
  }
  for(chunk = 0; chunk < fat_chunks; chunk++)
  {
    assert(records[1 + name_chunks + chunk].flags == (0x80u | chunk));
    probe_chunk_bytes(&records[1 + name_chunks + chunk], bytes);
    for(index = 0; index < 12u; index++)
    {
      offset = chunk * 12u + index;
      assert(bytes[index] == (offset < fat_length ? fat[offset] : 0u));
    }
  }
  assert(records[count - 2u].flags == 0xC0u);
  assert(records[count - 2u].value[0] == 3u);
  assert(records[count - 2u].value[1] == 0x80010002u);
  assert(records[count - 2u].value[2] == 0xFFFFFFFFu);
  assert(records[count - 1u].flags == 0xC1u);
  assert(records[count - 1u].value[0] == 4u);
  assert(records[count - 1u].value[1] == 0x80020001u);
  assert(records[count - 1u].value[2] == 0u);
  return fat_length;
}

/* d_name lengths around the 12-byte chunk edges, up to the longest name;
   bytes above 0x7F (UTF-8, Shift-JIS, GBK) are logged as they are. */
static void test_name_probe_chunks_d_name(void)
{
  static const size_t lengths[] = { 0u, 11u, 12u, 13u, 255u };
  uint8_t name[256];
  uint8_t fat[1044];
  size_t test;
  size_t index;

  memset(fat, 0, sizeof(fat));
  for(test = 0; test < sizeof(lengths) / sizeof(lengths[0]); test++)
  {
    memset(name, 0, sizeof(name));
    for(index = 0; index < lengths[test]; index++)
      name[index] = (uint8_t)(0x81u + index % 120u);
    /* An empty record still covers layout B's alias field. */
    assert(check_probe_entry(name, sizeof(name), lengths[test], fat,
     sizeof(fat)) == 20u);
  }
  /* A d_name with no NUL stops at the longest name logged. */
  memset(name, 'n', sizeof(name));
  assert(check_probe_entry(name, sizeof(name), 255u, fat,
   sizeof(fat)) == 20u);
}

/* The FAT bytes logged reach the end of both layouts' long names. */
static void test_name_probe_fat_layouts(void)
{
  static const uint8_t name[] = "game.gba";
  uint8_t fat[1044];

  /* Layout A: alias at 0, a 30-byte long name at 13. */
  memset(fat, 0, sizeof(fat));
  memcpy(fat, "ABCDEF~1.GBA", 12);
  memset(fat + 13, 0xE3, 30);
  assert(check_probe_entry(name, sizeof(name), 8u, fat, sizeof(fat)) ==
   43u);

  /* Layout B: size word, alias at 4, a 40-byte long name at 20. */
  memset(fat, 0, sizeof(fat));
  fat[0] = 0x14;
  fat[1] = 0x04;
  memcpy(fat + 4, "ABCDEF~1.GBA", 12);
  memset(fat + 20, 0xC3, 40);
  assert(check_probe_entry(name, sizeof(name), 8u, fat, sizeof(fat)) ==
   60u);

  /* No NUL anywhere: each long name stops at 255 bytes, layout B's ends
     later; a short buffer stops at its end. */
  memset(fat, 'L', sizeof(fat));
  assert(check_probe_entry(name, sizeof(name), 8u, fat, sizeof(fat)) ==
   20u + 255u);
  assert(check_probe_entry(name, sizeof(name), 8u, fat, 100u) == 100u);
  assert(check_probe_entry(name, sizeof(name), 8u, fat, 10u) == 10u);
  assert(check_probe_entry(name, sizeof(name), 8u, NULL, 0u) == 0u);
}

/* An entry that does not fit the budget emits nothing; one that fits
   exactly is logged whole. */
static void test_name_probe_budget(void)
{
  static const int32_t opened[RUNTIME_LOG_NAME_PROBE_OPENS] = { 0 };
  RuntimeLogRing ring;
  uint8_t name[256];
  uint8_t fat[1044];

  memset(name, 0, sizeof(name));
  memcpy(name, "13-byte-name!", 13);
  memset(fat, 0, sizeof(fat));
  runtime_log_ring_init(&ring, RUNTIME_LOG_SOURCE_MAIN);
  runtime_log_ring_emit(&ring, 1, 0, RUNTIME_LOG_EVENT_LOG_FLUSH, 0, 0, 0, 0);
  /* Header, 2 name chunks, 2 FAT chunks, 2 open records. */
  assert(runtime_log_name_probe_entry(&ring, 2, 0, name, sizeof(name), fat,
   sizeof(fat), opened, 6) == 0u);
  assert(ring.next_ordinal == 1u);
  assert(runtime_log_name_probe_entry(&ring, 2, 0, name, sizeof(name), fat,
   sizeof(fat), opened, 7) == 7u);
  assert(ring.next_ordinal == 8u);
  assert(runtime_log_name_probe_entry(NULL, 2, 0, name, sizeof(name), fat,
   sizeof(fat), opened, 7) == 0u);
}

/* The snapshot file holds a chunk's bytes in their original order. */
static void test_name_probe_bytes_in_file_order(void)
{
  static const int32_t opened[RUNTIME_LOG_NAME_PROBE_OPENS] = { 0 };
  /* 12 bytes: one chunk. */
  static const uint8_t name[] = "\xE6\x97\xA5\xE6\x9C\xAC-ABCDE";
  uint8_t encoded[RUNTIME_LOG_MAX_FILE_SIZE];
  RuntimeLogMetadata metadata;
  RuntimeLogRing main_ring;
  RuntimeLogRing network_ring;
  RuntimeLogRing critical_ring;
  size_t written = 0;

  memset(&metadata, 0, sizeof(metadata));
  runtime_log_ring_init(&main_ring, RUNTIME_LOG_SOURCE_MAIN);
  runtime_log_ring_init(&network_ring, RUNTIME_LOG_SOURCE_NETWORK);
  runtime_log_ring_init(&critical_ring, RUNTIME_LOG_SOURCE_CRITICAL);
  assert(runtime_log_name_probe_entry(&main_ring, 2, 0, name, sizeof(name),
   NULL, 0, opened, 8) == 4u);
  assert(runtime_log_encode(&main_ring, &network_ring, &critical_ring,
   &metadata, encoded, sizeof(encoded), &written));
  /* The second record is the d_name chunk; its values start at 20. */
  assert(memcmp(encoded + RUNTIME_LOG_HEADER_SIZE +
   RUNTIME_LOG_RECORD_SIZE + 20u, name, 12) == 0);
  assert(encoded[RUNTIME_LOG_HEADER_SIZE + RUNTIME_LOG_RECORD_SIZE + 19u] ==
   0x40u);
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

/* Rotation shifts each log one slot later and drops the oldest. */
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

/* Runs every ring, encoding and store case; a failing assert aborts the
   run. */
int main(void)
{
  test_ring_retains_latest_records();
  test_critical_ring_tracks_its_smaller_retention_window();
  test_benchmark_survives_shutdown_checkpoint();
  test_snapshot_at_ordinal_limit_is_bounded();
  test_encode_and_validate();
  test_version_five_still_validates();
  test_invalid_arguments();
  test_maximum_current_snapshot_size();
  test_name_probe_chunks_d_name();
  test_name_probe_fat_layouts();
  test_name_probe_budget();
  test_name_probe_bytes_in_file_order();
  test_store_preserves_previous_log_on_failed_promote();
  test_store_rotates_four_sessions();
  puts("Runtime log tests passed");
  return 0;
}
