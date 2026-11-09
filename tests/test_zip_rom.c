/*
 * Host tests for zip_rom.c, the .zip ROM loader. Each archive is built here
 * as a single local entry (PKWARE APPNOTE.TXT 4.3.7) around a raw deflate
 * stream from zlib, so no binary fixture lives in Git. A fake reader serves
 * the archive in chunks, counts its calls and can fail halfway.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zip_rom.h"
#include "zlib/zlib.h"

#define ROM_SIZE (200u * 1024u + 77u)
#define OUT_CAP (ROM_SIZE + 256u)
#define CANARY_SIZE 64u
#define CANARY 0xA5u
#define SMALL_CHUNK 4096u
#define LARGE_CHUNK (128u * 1024u)
#define EXTRA_SIZE 9u
#define NO_FAILURE UINT32_MAX

enum { DESC_NONE, DESC_PLAIN, DESC_SIGNED };

static uint8_t rom[ROM_SIZE];
static uint32_t rom_crc;
static uint8_t packed[ROM_SIZE + ROM_SIZE / 4u];
static uint32_t packed_size;
static uint8_t archive[sizeof(packed) + 512u];
static uint8_t out[OUT_CAP + CANARY_SIZE];
static uint8_t in_buf[LARGE_CHUNK];
static int with_progress = 1;

typedef struct
{
  const uint8_t *data;
  uint32_t len;
  uint32_t pos;
  uint32_t chunk;
  uint32_t fail_at; /* a read that would reach this byte fails */
  uint32_t calls;
  uint32_t last_done;
} FakeArchive;

static int32_t fake_read(void *ctx, uint8_t *buf, uint32_t len)
{
  FakeArchive *fake = ctx;
  uint32_t n = fake->len - fake->pos;

  assert(len > 0 && len <= fake->chunk);
  fake->calls++;
  if(n > len)
    n = len;
  if(fake->pos + n > fake->fail_at)
    return -1;
  memcpy(buf, fake->data + fake->pos, n);
  fake->pos += n;
  return (int32_t)n;
}

static void fake_progress(void *ctx, uint32_t done, uint32_t total)
{
  FakeArchive *fake = ctx;

  assert(total == fake->len);
  assert(done > fake->last_done && done <= fake->pos);
  fake->last_done = done;
}

/* Loads the first len bytes of archive through chunk-sized reads; checks
   the read bound, the progress and the canary after out_cap. */
static int load(uint32_t len, uint32_t chunk, uint32_t out_cap,
 uint32_t fail_at, uint32_t *size)
{
  FakeArchive fake;
  ZipRomIo io;
  uint32_t i;
  int result;

  memset(&fake, 0, sizeof(fake));
  fake.data = archive;
  fake.len = len;
  fake.chunk = chunk;
  fake.fail_at = fail_at;
  io.ctx = &fake;
  io.read = fake_read;
  io.progress = with_progress ? fake_progress : NULL;
  io.in_buf = in_buf;
  io.in_cap = chunk;
  io.total = len;
  memset(out, CANARY, sizeof(out));
  result = zip_rom_load(&io, out, out_cap, size);
  for(i = out_cap; i < sizeof(out); i++)
    assert(out[i] == CANARY);
  assert(fake.calls <= (len + chunk - 1) / chunk + 2);
  /* Bytes read after the stream are not reported. */
  assert(with_progress ? (fake.last_done <= fake.pos &&
   fake.pos - fake.last_done <= chunk) : fake.last_done == 0);
  return result;
}

static int load_all(uint32_t len, uint32_t chunk)
{
  uint32_t size;

  return load(len, chunk, OUT_CAP, NO_FAILURE, &size);
}

static void expect_rom(uint32_t len, uint32_t chunk, uint32_t out_cap)
{
  uint32_t size = 0;

  assert(load(len, chunk, out_cap, NO_FAILURE, &size) == ZIP_ROM_OK);
  assert(size == ROM_SIZE);
  assert(memcmp(out, rom, ROM_SIZE) == 0);
}

static void put16(uint8_t *p, uint32_t value)
{
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
  put16(p, value);
  put16(p + 2, value >> 16);
}

/* Offset of the entry's data for a name of name_length bytes. */
static uint32_t data_offset(uint32_t name_length)
{
  return 30u + name_length + EXTRA_SIZE;
}

/* Writes one local entry holding rom, deflated for method 8 and raw
   otherwise, with size as its uncompressed size; returns its length. With
   flag bit 3 the header's CRC and sizes are 0 and the descriptor has them. */
static uint32_t build(uint32_t flags, uint32_t method, const char *name,
 uint32_t size, int descriptor)
{
  const uint8_t *data = method == 8u ? packed : rom;
  uint32_t data_size = method == 8u ? packed_size : ROM_SIZE;
  uint32_t name_length = (uint32_t)strlen(name);
  uint32_t at = data_offset(name_length);

  memset(archive, 0, at);
  put32(archive, 0x04034B50u);
  put16(archive + 4, 20u);
  put16(archive + 6, flags);
  put16(archive + 8, method);
  if((flags & 8u) == 0)
  {
    put32(archive + 14, rom_crc);
    put32(archive + 18, data_size);
    put32(archive + 22, size);
  }
  put16(archive + 26, name_length);
  put16(archive + 28, EXTRA_SIZE);
  memcpy(archive + 30, name, name_length);
  memset(archive + 30 + name_length, 0xEE, EXTRA_SIZE);
  memcpy(archive + at, data, data_size);
  at += data_size;
  if(descriptor == DESC_SIGNED)
  {
    put32(archive + at, 0x08074B50u);
    at += 4;
  }
  if(descriptor != DESC_NONE)
  {
    put32(archive + at, rom_crc);
    put32(archive + at + 4, data_size);
    put32(archive + at + 8, size);
    at += 12;
  }
  return at;
}

/* About 200 KiB that alternate 4 KiB of a compressible ramp and 4 KiB of
   noise, so the stream mixes Huffman-coded and stored blocks. */
static void make_fixture(void)
{
  uint32_t seed = 0x12345678u;
  uint32_t i;
  uLong crc;
  z_stream stream;

  for(i = 0; i < ROM_SIZE; i++)
  {
    seed = seed * 1103515245u + 12345u;
    rom[i] = (i & 0x1000u) != 0 ? (uint8_t)(seed >> 24) :
     (uint8_t)(i * 7u / 13u);
  }
  crc = crc32(0L, rom, ROM_SIZE);
  rom_crc = (uint32_t)crc;

  memset(&stream, 0, sizeof(stream));
  assert(deflateInit2(&stream, 9, Z_DEFLATED, -MAX_WBITS, 8,
   Z_DEFAULT_STRATEGY) == Z_OK);
  stream.next_in = rom;
  stream.avail_in = ROM_SIZE;
  stream.next_out = packed;
  stream.avail_out = sizeof(packed);
  assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
  packed_size = (uint32_t)stream.total_out;
  assert(deflateEnd(&stream) == Z_OK);
  assert(packed_size < ROM_SIZE);
}

static void test_valid_entries_load_byte_for_byte(void)
{
  uint32_t len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);

  expect_rom(len, SMALL_CHUNK, OUT_CAP);
  expect_rom(len, LARGE_CHUNK, OUT_CAP);
  expect_rom(len, SMALL_CHUNK, ROM_SIZE);
  with_progress = 0;
  expect_rom(len, SMALL_CHUNK, OUT_CAP);
  with_progress = 1;

  len = build(0, 0, "Game.GBA", ROM_SIZE, DESC_NONE);
  expect_rom(len, SMALL_CHUNK, OUT_CAP);
  expect_rom(len, LARGE_CHUNK, ROM_SIZE);

  len = build(0, 8, "Game v1.1.gba", ROM_SIZE, DESC_NONE);
  expect_rom(len, SMALL_CHUNK, OUT_CAP);
}

/* Flag bit 3: CRC and sizes come from the descriptor after the stream. */
static void test_streamed_entries_use_the_descriptor(void)
{
  uint32_t size;
  uint32_t len = build(8, 8, "game.agb", ROM_SIZE, DESC_PLAIN);

  expect_rom(len, SMALL_CHUNK, OUT_CAP);
  expect_rom(len, LARGE_CHUNK, OUT_CAP);
  assert(load(len, SMALL_CHUNK, ROM_SIZE - 1, NO_FAILURE, &size) ==
   ZIP_ROM_TOO_BIG);

  len = build(8, 8, "game.bin", ROM_SIZE, DESC_SIGNED);
  expect_rom(len, SMALL_CHUNK, OUT_CAP);
  expect_rom(len, LARGE_CHUNK, OUT_CAP);
  assert(load_all(len - 6, SMALL_CHUNK) == ZIP_ROM_TRUNCATED);

  len = build(8, 8, "game.gba", ROM_SIZE, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_TRUNCATED);

  len = build(8, 8, "game.gba", ROM_SIZE + 1, DESC_PLAIN);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);

  len = build(8, 0, "game.gba", ROM_SIZE, DESC_PLAIN);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_UNSUPPORTED);
}

static void test_damaged_data_is_corrupt(void)
{
  static const uint32_t percent[] = { 10, 50, 90 };
  uint32_t offset = data_offset(8);
  uint32_t len;
  uint32_t i;
  int result;

  /* Block type 3 does not exist: inflate stops with Z_DATA_ERROR. */
  len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);
  archive[offset] |= 0x06u;
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);

  /* Deeper in the stream the outcome depends on what the byte decodes to,
     but the ROM never loads. */
  for(i = 0; i < sizeof(percent) / sizeof(percent[0]); i++)
  {
    len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);
    archive[offset + packed_size * percent[i] / 100u] ^= 0xFFu;
    result = load_all(len, SMALL_CHUNK);
    assert(result == ZIP_ROM_CORRUPT || result == ZIP_ROM_TRUNCATED ||
     result == ZIP_ROM_TOO_BIG);
  }

  len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);
  archive[14] ^= 0x01u;
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);

  len = build(0, 0, "game.gba", ROM_SIZE, DESC_NONE);
  archive[offset + ROM_SIZE / 2] ^= 0xFFu;
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);
  assert(load_all(len, LARGE_CHUNK) == ZIP_ROM_CORRUPT);

  len = build(0, 8, "game.gba", ROM_SIZE - 1, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);
  len = build(0, 8, "game.gba", ROM_SIZE + 1, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);
}

static void test_bad_headers_are_corrupt(void)
{
  uint32_t len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);

  archive[2] = 0x05;
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);
  build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);
  put16(archive + 26, 256u);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);
  build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);
  put16(archive + 28, 0x8000u);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_CORRUPT);
}

static void test_truncated_archives(void)
{
  static const uint32_t percent[] = { 10, 50, 99 };
  uint32_t len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);
  uint32_t i;

  for(i = 0; i < sizeof(percent) / sizeof(percent[0]); i++)
  {
    assert(load_all(len * percent[i] / 100u, SMALL_CHUNK) ==
     ZIP_ROM_TRUNCATED);
    assert(load_all(len * percent[i] / 100u, LARGE_CHUNK) ==
     ZIP_ROM_TRUNCATED);
  }
  assert(load_all(20, SMALL_CHUNK) == ZIP_ROM_TRUNCATED);
  assert(load_all(34, SMALL_CHUNK) == ZIP_ROM_TRUNCATED);
  assert(load_all(0, SMALL_CHUNK) == ZIP_ROM_TRUNCATED);

  len = build(0, 0, "game.gba", ROM_SIZE, DESC_NONE);
  assert(load_all(len / 2, SMALL_CHUNK) == ZIP_ROM_TRUNCATED);
  assert(load_all(len - 1, LARGE_CHUNK) == ZIP_ROM_TRUNCATED);
}

static void test_unsupported_entries(void)
{
  uint32_t len = build(1, 8, "game.gba", ROM_SIZE, DESC_NONE);

  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_UNSUPPORTED);
  len = build(0, 12, "game.gba", ROM_SIZE, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_UNSUPPORTED);
  len = build(0, 8, "readme.txt", ROM_SIZE, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_UNSUPPORTED);
  len = build(0, 8, "game", ROM_SIZE, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_UNSUPPORTED);
  len = build(0, 8, "game.gb", ROM_SIZE, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_UNSUPPORTED);
  len = build(0, 8, "game.gbax", ROM_SIZE, DESC_NONE);
  assert(load_all(len, SMALL_CHUNK) == ZIP_ROM_UNSUPPORTED);
}

static void test_too_big_for_the_buffer(void)
{
  uint32_t size;
  uint32_t len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);

  assert(load(len, SMALL_CHUNK, ROM_SIZE - 1, NO_FAILURE, &size) ==
   ZIP_ROM_TOO_BIG);
  len = build(0, 0, "game.gba", ROM_SIZE, DESC_NONE);
  assert(load(len, SMALL_CHUNK, ROM_SIZE - 1, NO_FAILURE, &size) ==
   ZIP_ROM_TOO_BIG);
}

static void test_read_errors_stop_the_load(void)
{
  uint32_t size;
  uint32_t len = build(0, 8, "game.gba", ROM_SIZE, DESC_NONE);

  assert(load(len, SMALL_CHUNK, OUT_CAP, len / 2, &size) ==
   ZIP_ROM_IO_ERROR);
  assert(load(len, LARGE_CHUNK, OUT_CAP, len / 2, &size) ==
   ZIP_ROM_IO_ERROR);
  len = build(0, 0, "game.gba", ROM_SIZE, DESC_NONE);
  assert(load(len, SMALL_CHUNK, OUT_CAP, len / 2, &size) ==
   ZIP_ROM_IO_ERROR);
  assert(load(len, SMALL_CHUNK, OUT_CAP, 0, &size) == ZIP_ROM_IO_ERROR);
}

/* A 1.5 MiB ROM: game bytes, then 0xFF to the end. The padding packs into a
   small part of one read, and must still move the bar once per inflate
   step. */
#define PAD_ROM_SIZE (6u * ZIP_ROM_INFLATE_STEP)
#define PAD_CHUNK (64u * 1024u) /* zip.c's read size */
#define PAD_DATA_OFFSET 38u     /* the header and "game.gba", no extra */

static uint8_t pad_rom[PAD_ROM_SIZE];
static uint8_t pad_archive[PAD_DATA_OFFSET + PAD_ROM_SIZE / 2u];
static uint8_t pad_out[PAD_ROM_SIZE];
static uint32_t high_water; /* pad_out matches pad_rom below this */

/* Moves high_water over the bytes written since the last call; returns how
   far it moved. */
static uint32_t scan_high_water(void)
{
  uint32_t start = high_water;

  while(high_water < PAD_ROM_SIZE &&
   pad_out[high_water] == pad_rom[high_water])
    high_water++;
  return high_water - start;
}

static void padding_progress(void *ctx, uint32_t done, uint32_t total)
{
  fake_progress(ctx, done, total);
  assert(scan_high_water() <= ZIP_ROM_INFLATE_STEP);
}

/* Fills pad_rom with game bytes of the fixture's pattern and 0xFF after
   them, and pad_archive with its entry: method 8, CRC and sizes in the
   header, nothing after the stream. Returns the archive's length. */
static uint32_t build_padded(uint32_t game)
{
  uint32_t seed = 1;
  uint32_t i, len;
  z_stream stream;

  for(i = 0; i < PAD_ROM_SIZE; i++)
  {
    seed = seed * 1103515245u + 12345u;
    if(i >= game)
      pad_rom[i] = 0xFFu;
    else
      pad_rom[i] = (i & 0x1000u) != 0 ? (uint8_t)(seed >> 24) :
       (uint8_t)(i * 7u / 13u);
  }

  memset(&stream, 0, sizeof(stream));
  assert(deflateInit2(&stream, 6, Z_DEFLATED, -MAX_WBITS, 8,
   Z_DEFAULT_STRATEGY) == Z_OK);
  stream.next_in = pad_rom;
  stream.avail_in = PAD_ROM_SIZE;
  stream.next_out = pad_archive + PAD_DATA_OFFSET;
  stream.avail_out = sizeof(pad_archive) - PAD_DATA_OFFSET;
  assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
  len = PAD_DATA_OFFSET + (uint32_t)stream.total_out;
  assert(deflateEnd(&stream) == Z_OK);
  memset(pad_archive, 0, PAD_DATA_OFFSET);
  put32(pad_archive, 0x04034B50u);
  put16(pad_archive + 4, 20u);
  put16(pad_archive + 8, 8u);
  put32(pad_archive + 14, (uint32_t)crc32(0L, pad_rom, PAD_ROM_SIZE));
  put32(pad_archive + 18, len - PAD_DATA_OFFSET);
  put32(pad_archive + 22, PAD_ROM_SIZE);
  put16(pad_archive + 26, 8u);
  memcpy(pad_archive + 30, "game.gba", 8);
  return len;
}

/* Loads pad_archive through PAD_CHUNK reads into a pad_out where no byte
   matches pad_rom until written. */
static int load_padded(uint32_t len, uint32_t out_cap, uint32_t *size)
{
  FakeArchive fake;
  ZipRomIo io;
  uint32_t i;
  int result;

  for(i = 0; i < PAD_ROM_SIZE; i++)
    pad_out[i] = (uint8_t)(pad_rom[i] ^ 0x5Au);
  memset(&fake, 0, sizeof(fake));
  fake.data = pad_archive;
  fake.len = len;
  fake.chunk = PAD_CHUNK;
  fake.fail_at = NO_FAILURE;
  io.ctx = &fake;
  io.read = fake_read;
  io.progress = padding_progress;
  io.in_buf = in_buf;
  io.in_cap = PAD_CHUNK;
  io.total = len;
  high_water = 0;
  result = zip_rom_load(&io, pad_out, out_cap, size);
  assert(fake.calls <= (len + PAD_CHUNK - 1) / PAD_CHUNK + 2);
  return result;
}

static void expect_padded(uint32_t len)
{
  uint32_t size = 0;

  /* out_cap is the ROM size, so the stream ends with no room left. */
  assert(load_padded(len, PAD_ROM_SIZE, &size) == ZIP_ROM_OK);
  assert(size == PAD_ROM_SIZE);
  assert(memcmp(pad_out, pad_rom, PAD_ROM_SIZE) == 0);
  /* zip.c's closing call to progress draws the rest. */
  assert(scan_high_water() <= ZIP_ROM_INFLATE_STEP);
  assert(high_water == PAD_ROM_SIZE);
}

static void test_padding_reports_every_step(void)
{
  /* With 512 KiB of game the padding starts a few reads in; with 32 KiB
     the whole archive comes in the first read. */
  static const uint32_t game[] = { 2u * ZIP_ROM_INFLATE_STEP, 32u * 1024u };
  uint32_t i, len = 0;

  for(i = 0; i < sizeof(game) / sizeof(game[0]); i++)
  {
    len = build_padded(game[i]);
    expect_padded(len);
  }
  assert(len <= PAD_CHUNK);
}

int main(void)
{
  make_fixture();
  test_valid_entries_load_byte_for_byte();
  test_streamed_entries_use_the_descriptor();
  test_damaged_data_is_corrupt();
  test_bad_headers_are_corrupt();
  test_truncated_archives();
  test_unsupported_entries();
  test_too_big_for_the_buffer();
  test_read_errors_stop_the_load();
  test_padding_reports_every_step();
  puts("Zip ROM tests passed");
  return 0;
}
