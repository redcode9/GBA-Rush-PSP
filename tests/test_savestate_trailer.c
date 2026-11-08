/*
 * Host tests for savestate_trailer.c, the 48-byte trailer after a v2
 * savestate's 512 KiB payload: round trip, a golden vector, the outcome of
 * every altered field and file size, the ROM id, and a deterministic fuzz.
 * Short tails and fuzz inputs are also decoded from the very end of a
 * buffer, so AddressSanitizer (make test-sanitize) reports any read past
 * the given tail; without it, only a read that changes the outcome fails.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "savestate_trailer.h"
#include "zlib/zlib.h"

#define PAYLOAD SAVESTATE_TRAILER_PAYLOAD_SIZE
#define FULL_SIZE (SAVESTATE_TRAILER_PAYLOAD_SIZE + SAVESTATE_TRAILER_SIZE)
#define FUZZ_ITERATIONS 100000u
#define FUZZ_BUFFER 64u

static int failures;

#define CHECK(condition) \
  do \
  { \
    if(!(condition)) \
    { \
      fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
       #condition); \
      failures++; \
    } \
  } while(0)

/* Small deterministic generator so failures reproduce. */
static uint32_t rng_state;

static uint32_t rng(void)
{
  rng_state = rng_state * 1664525u + 1013904223u;
  return rng_state >> 8;
}

static uint32_t rng32(void)
{
  uint32_t high = rng();

  return (high << 8) ^ rng();
}

/* struct.pack('<4sHHII4sB3xIIIII', b'GRST', 2, 48, 0x80000, 0x12345678,
   b'BPRE', 1, 0xA1B2C3D4, 0x01000000, 0x81977335, 0x00010500, 0) followed
   by its zlib.crc32 packed '<I', computed in Python. A plain literal, so a
   decoder in another language can share it. */
static const uint8_t golden[SAVESTATE_TRAILER_SIZE] =
{
  0x47, 0x52, 0x53, 0x54, 0x02, 0x00, 0x30, 0x00,
  0x00, 0x00, 0x08, 0x00, 0x78, 0x56, 0x34, 0x12,
  0x42, 0x50, 0x52, 0x45, 0x01, 0x00, 0x00, 0x00,
  0xD4, 0xC3, 0xB2, 0xA1, 0x00, 0x00, 0x00, 0x01,
  0x35, 0x73, 0x97, 0x81, 0x00, 0x05, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x15, 0x90, 0x39, 0xB3
};

/* The trailer the golden vector holds. */
static SavestateTrailer sample_trailer(void)
{
  SavestateTrailer t;

  memset(&t, 0, sizeof(t));
  t.version = SAVESTATE_TRAILER_VERSION;
  t.payload_size = PAYLOAD;
  t.payload_crc = 0x12345678u;
  memcpy(t.rom.game_code, "BPRE", 4);
  t.rom.software_version = 1;
  t.rom.header_crc = 0xA1B2C3D4u;
  t.rom.rom_size = 0x01000000u;
  t.bios_crc = 0x81977335u;
  t.emulator_version = 0x00010500u;
  return t;
}

static int same_trailer(const SavestateTrailer *a, const SavestateTrailer *b)
{
  return a->version == b->version && a->payload_size == b->payload_size &&
   a->payload_crc == b->payload_crc &&
   memcmp(a->rom.game_code, b->rom.game_code, 4) == 0 &&
   a->rom.software_version == b->rom.software_version &&
   a->rom.header_crc == b->rom.header_crc &&
   a->rom.rom_size == b->rom.rom_size && a->bios_crc == b->bios_crc &&
   a->emulator_version == b->emulator_version;
}

/* Rewrites bytes 44-47 after a test edits the fields, so the edit itself
   is what the decoder sees. */
static void refresh_crc(uint8_t *trailer)
{
  uLong crc = crc32(0L, Z_NULL, 0);
  uint32_t value = (uint32_t)crc32(crc, trailer, 44);

  trailer[44] = (uint8_t)(value & 0xFFu);
  trailer[45] = (uint8_t)((value >> 8) & 0xFFu);
  trailer[46] = (uint8_t)((value >> 16) & 0xFFu);
  trailer[47] = (uint8_t)(value >> 24);
}

/* A whole v2 file: 512 KiB plus the 48 bytes of `trailer`. */
static SavestateTrailerResult decode_full(const uint8_t *trailer,
 uint32_t payload_crc, const SavestateRomId *current, SavestateTrailer *out)
{
  return savestate_trailer_decode(FULL_SIZE, trailer, SAVESTATE_TRAILER_SIZE,
   payload_crc, current, out);
}

/* Decodes the first `tail_size` (at most FUZZ_BUFFER) bytes of `source`
   copied to the very end of a buffer, so AddressSanitizer reports a read
   past them. */
static SavestateTrailerResult decode_at_end(uint32_t file_size,
 const uint8_t *source, size_t tail_size, uint32_t payload_crc,
 const SavestateRomId *current, SavestateTrailer *out)
{
  uint8_t edge[FUZZ_BUFFER];
  uint8_t *tail = edge + FUZZ_BUFFER - tail_size;

  memcpy(tail, source, tail_size);
  return savestate_trailer_decode(file_size, tail, tail_size, payload_crc,
   current, out);
}

static void test_round_trip(void)
{
  SavestateTrailer t = sample_trailer();
  SavestateTrailer out;
  uint8_t trailer[SAVESTATE_TRAILER_SIZE];
  uint8_t again[SAVESTATE_TRAILER_SIZE];

  savestate_trailer_encode(&t, trailer);
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_V2);
  CHECK(same_trailer(&t, &out));

  /* The encoder writes its own version whatever the caller passes. */
  t.version = 9;
  savestate_trailer_encode(&t, again);
  CHECK(memcmp(trailer, again, sizeof(trailer)) == 0);
}

static void test_golden_vector(void)
{
  SavestateTrailer t = sample_trailer();
  SavestateTrailer out;
  uint8_t trailer[SAVESTATE_TRAILER_SIZE];

  savestate_trailer_encode(&t, trailer);
  CHECK(memcmp(trailer, golden, sizeof(golden)) == 0);
  CHECK(decode_full(golden, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_V2);
  CHECK(same_trailer(&t, &out));
}

static void test_altered_fields(void)
{
  SavestateTrailer t = sample_trailer();
  SavestateTrailer other;
  SavestateTrailer out;
  SavestateRomId current;
  uint8_t good[SAVESTATE_TRAILER_SIZE];
  uint8_t trailer[SAVESTATE_TRAILER_SIZE];
  unsigned int i;
  unsigned int bit;

  savestate_trailer_encode(&t, good);

  memcpy(trailer, good, sizeof(trailer));
  trailer[0] = 'g';
  refresh_crc(trailer);
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);

  /* A later version is reported before the CRC, which it may have moved,
     and on a tail of any length from 8 bytes. */
  memcpy(trailer, good, sizeof(trailer));
  trailer[4] = 3;
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_NEWER);
  CHECK(out.version == 3);
  trailer[4] = 0xFF;
  trailer[5] = 0xFF;
  CHECK(savestate_trailer_decode(PAYLOAD + 8u, trailer, 8, t.payload_crc,
   &t.rom, &out) == SAVESTATE_TRAILER_NEWER);
  CHECK(out.version == 0xFFFFu);

  /* A 6- or 7-byte tail is corrupt, whatever version it holds. */
  for(i = 6; i < 8; i++)
  {
    trailer[4] = 3;
    trailer[5] = 0;
    CHECK(decode_at_end(PAYLOAD + i, trailer, i, t.payload_crc, &t.rom,
     &out) == SAVESTATE_TRAILER_CORRUPT);
    trailer[4] = 2;
    CHECK(decode_at_end(PAYLOAD + i, trailer, i, t.payload_crc, &t.rom,
     &out) == SAVESTATE_TRAILER_CORRUPT);
  }

  memcpy(trailer, good, sizeof(trailer));
  trailer[4] = 1;
  refresh_crc(trailer);
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);
  memcpy(trailer, good, sizeof(trailer));
  trailer[6] = 40;
  refresh_crc(trailer);
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);

  /* Any single flipped bit, the trailer CRC's own included, without a CRC
     fix; one in the version field may also read as a later version. */
  for(i = 0; i < SAVESTATE_TRAILER_SIZE; i++)
  {
    for(bit = 0; bit < 8; bit++)
    {
      SavestateTrailerResult expected;

      memcpy(trailer, good, sizeof(trailer));
      trailer[i] ^= (uint8_t)(1u << bit);
      expected = ((uint32_t)trailer[4] | ((uint32_t)trailer[5] << 8)) >
       SAVESTATE_TRAILER_VERSION ? SAVESTATE_TRAILER_NEWER :
       SAVESTATE_TRAILER_CORRUPT;
      CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) == expected);
    }
  }

  other = t;
  other.payload_size = PAYLOAD - 1u;
  savestate_trailer_encode(&other, trailer);
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);
  other.payload_size = PAYLOAD + 1u;
  savestate_trailer_encode(&other, trailer);
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);

  CHECK(decode_full(good, t.payload_crc ^ 1u, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);

  /* BIOS CRC and emulator version are never checked, only returned. */
  other = t;
  other.bios_crc ^= 0x01010101u;
  other.emulator_version = 0x00020000u;
  savestate_trailer_encode(&other, trailer);
  CHECK(decode_full(trailer, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_V2);
  CHECK(same_trailer(&other, &out));

  current = t.rom;
  current.game_code[3] ^= 0x20u;
  CHECK(decode_full(good, t.payload_crc, &current, &out) ==
   SAVESTATE_TRAILER_OTHER_ROM);
  CHECK(same_trailer(&t, &out));
  current = t.rom;
  current.software_version++;
  CHECK(decode_full(good, t.payload_crc, &current, &out) ==
   SAVESTATE_TRAILER_OTHER_ROM);
  current = t.rom;
  current.header_crc ^= 0x100u;
  CHECK(decode_full(good, t.payload_crc, &current, &out) ==
   SAVESTATE_TRAILER_OTHER_ROM);
  current.rom_size = 0x00800000u;
  CHECK(decode_full(good, t.payload_crc, &current, &out) ==
   SAVESTATE_TRAILER_OTHER_ROM);

  /* Same game, other ROM size: a warning, so the fields come back. */
  current = t.rom;
  current.rom_size = 0x00800000u;
  CHECK(decode_full(good, t.payload_crc, &current, &out) ==
   SAVESTATE_TRAILER_ROM_SIZE_DIFFERS);
  CHECK(same_trailer(&t, &out));
}

static void test_file_sizes(void)
{
  SavestateTrailer t = sample_trailer();
  SavestateTrailer out;
  uint8_t trailer[SAVESTATE_TRAILER_SIZE + 1];

  savestate_trailer_encode(&t, trailer);
  trailer[SAVESTATE_TRAILER_SIZE] = 0;

  CHECK(decode_at_end(PAYLOAD, trailer, 0, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_LEGACY);
  CHECK(decode_at_end(PAYLOAD - 1u, trailer, 0, t.payload_crc, &t.rom,
   &out) == SAVESTATE_TRAILER_CORRUPT);
  CHECK(decode_at_end(0, trailer, 0, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);
  CHECK(decode_at_end(PAYLOAD + 4u, trailer, 4, t.payload_crc, &t.rom,
   &out) == SAVESTATE_TRAILER_CORRUPT);
  CHECK(decode_at_end(FULL_SIZE - 1u, trailer, SAVESTATE_TRAILER_SIZE - 1u,
   t.payload_crc, &t.rom, &out) == SAVESTATE_TRAILER_CORRUPT);
  CHECK(savestate_trailer_decode(FULL_SIZE + 1u, trailer,
   SAVESTATE_TRAILER_SIZE + 1u, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);
  CHECK(decode_at_end(0xFFFFFFFFu, trailer, SAVESTATE_TRAILER_SIZE,
   t.payload_crc, &t.rom, &out) == SAVESTATE_TRAILER_CORRUPT);

  /* A short read of a full-size file. */
  CHECK(decode_at_end(FULL_SIZE, trailer, SAVESTATE_TRAILER_SIZE - 1u,
   t.payload_crc, &t.rom, &out) == SAVESTATE_TRAILER_CORRUPT);
  CHECK(decode_at_end(FULL_SIZE, trailer, 0, t.payload_crc, &t.rom, &out) ==
   SAVESTATE_TRAILER_CORRUPT);
}

static void test_rom_id(void)
{
  static const uint8_t game_code[4] = { 0xB7, 0xBE, 0xC5, 0xCC };
  uint8_t header[0xC0];
  SavestateRomId id;
  SavestateRomId again;
  unsigned int i;

  for(i = 0; i < sizeof(header); i++)
    header[i] = (uint8_t)((i * 7u + 3u) & 0xFFu);
  savestate_trailer_rom_id(header, 0x00400000u, &id);
  CHECK(memcmp(id.game_code, game_code, 4) == 0);
  CHECK(id.software_version == 0x27u);
  /* zlib.crc32(header[0xA0:0xC0]) in Python. */
  CHECK(id.header_crc == 0x28BAE2A6u);
  CHECK(id.rom_size == 0x00400000u);

  /* Only 0xA0-0xBF count. */
  header[0x9F] ^= 0xFFu;
  savestate_trailer_rom_id(header, 0x00400000u, &again);
  CHECK(again.header_crc == id.header_crc);
  header[0xA0] ^= 0xFFu;
  savestate_trailer_rom_id(header, 0x00400000u, &again);
  CHECK(again.header_crc != id.header_crc);
}

/* Each input is decoded four times: with zeros, ones or the source's own
   bytes after the `tail_size` given ones, where a read past them that
   changes the outcome gives different answers, and from the very end of a
   buffer, where AddressSanitizer reports any read past them. Inputs are
   random bytes or a valid trailer with a few changes, so the deep paths
   run too. */
static void test_fuzz(void)
{
  unsigned int seen[SAVESTATE_TRAILER_ROM_SIZE_DIFFERS + 1];
  uint32_t iteration;
  unsigned int i;

  memset(seen, 0, sizeof(seen));
  rng_state = 20261104u;
  for(iteration = 0; iteration < FUZZ_ITERATIONS; iteration++)
  {
    SavestateTrailer t = sample_trailer();
    SavestateTrailer out[4];
    SavestateTrailerResult result[4];
    SavestateRomId current;
    uint8_t source[FUZZ_BUFFER];
    uint8_t given[3][FUZZ_BUFFER];
    uint32_t file_size;
    uint32_t payload_crc;
    size_t tail_size;
    int ok;

    t.payload_crc = rng32();
    t.rom.header_crc = rng32();
    payload_crc = t.payload_crc;
    current = t.rom;
    for(i = 0; i < FUZZ_BUFFER; i++)
      source[i] = (uint8_t)rng();

    if(rng() % 4 != 0)
    {
      unsigned int changes = rng() % 4;

      savestate_trailer_encode(&t, source);
      for(i = 0; i < changes; i++)
      {
        unsigned int at = rng() % SAVESTATE_TRAILER_SIZE;

        source[at] = (uint8_t)rng();
      }
      if(rng() % 2 != 0)
        refresh_crc(source);
      tail_size = rng() % 4 != 0 ? SAVESTATE_TRAILER_SIZE :
       rng() % (FUZZ_BUFFER + 1);
    }
    else
      tail_size = rng() % (FUZZ_BUFFER + 1);

    switch(rng() % 8)
    {
      case 0: payload_crc ^= 1u << (rng() % 32); break;
      case 1: current.game_code[rng() % 4] ^= 0x20u; break;
      case 2: current.header_crc ^= 1u << (rng() % 32); break;
      case 3: current.rom_size += 0x100000u; break;
      default: break;
    }

    file_size = PAYLOAD + (uint32_t)tail_size;
    switch(rng() % 16)
    {
      case 0: file_size++; break;
      case 1: file_size--; break;
      case 2: file_size = rng32(); break;
      case 3: file_size = PAYLOAD; break;
      default: break;
    }

    memset(given[0], 0x00, FUZZ_BUFFER);
    memset(given[1], 0xFF, FUZZ_BUFFER);
    memcpy(given[0], source, tail_size);
    memcpy(given[1], source, tail_size);
    memcpy(given[2], source, FUZZ_BUFFER);
    for(i = 0; i < 3; i++)
      result[i] = savestate_trailer_decode(file_size, given[i], tail_size,
       payload_crc, &current, &out[i]);
    result[3] = decode_at_end(file_size, source, tail_size, payload_crc,
     &current, &out[3]);

    ok = 1;
    for(i = 1; i < 4; i++)
      ok = ok && result[i] == result[0] && same_trailer(&out[i], &out[0]);
    ok = ok && (unsigned int)result[0] <=
      (unsigned int)SAVESTATE_TRAILER_ROM_SIZE_DIFFERS &&
     (result[0] == SAVESTATE_TRAILER_LEGACY) == (file_size == PAYLOAD) &&
     ((result[0] != SAVESTATE_TRAILER_V2 &&
       result[0] != SAVESTATE_TRAILER_ROM_SIZE_DIFFERS) ||
      (tail_size == SAVESTATE_TRAILER_SIZE && file_size == FULL_SIZE));
    CHECK(ok);
    if(!ok)
    {
      fprintf(stderr, "fuzz input %u: file_size %u tail_size %u\n",
       (unsigned int)iteration, (unsigned int)file_size,
       (unsigned int)tail_size);
      return;
    }
    seen[result[0]]++;
  }

  /* Every outcome came up, so the fuzz reached past the early returns. */
  for(i = 0; i <= SAVESTATE_TRAILER_ROM_SIZE_DIFFERS; i++)
    CHECK(seen[i] != 0);
}

/* Runs every case; failed CHECKs are printed and counted, and any failure
   makes the exit status 1. */
int main(void)
{
  test_round_trip();
  test_golden_vector();
  test_altered_fields();
  test_file_sizes();
  test_rom_id();
  test_fuzz();

  if(failures != 0)
  {
    fprintf(stderr, "%d savestate_trailer check(s) failed\n", failures);
    return 1;
  }
  printf("savestate_trailer tests passed\n");
  return 0;
}
