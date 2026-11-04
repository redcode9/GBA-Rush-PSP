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
 * png_write.c on 20 frames drawn here (10 GBA-like scenes of tiles, text
 * and sprites, 10 synthetic ones): every PNG is checked chunk by chunk with
 * its CRCs, inflated by zlib and by the small inflater below (written from
 * RFC 1951), unfiltered and compared with the input pixel by pixel. All
 * memory comes from a counting arena, so the peak per shot is measured.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "png_write.h"

#define FRAME_WIDTH 240u
#define FRAME_HEIGHT 160u
/* Rows padded as in a PSP texture; the padding must never be read. */
#define FRAME_STRIDE 256u
#define PADDING_COLOUR 0x4321u
#define OUT_SIZE (192u * 1024u)
#define GUARD_BYTES 64u
#define GUARD_VALUE 0xCDu
#define RAW_SIZE (FRAME_HEIGHT * (1u + 3u * FRAME_WIDTH))
#define PEAK_LIMIT (96u * 1024u)
#define MEDIAN_LIMIT 4096u

static uint16_t frame[FRAME_STRIDE * FRAME_HEIGHT];
static uint8_t png[OUT_SIZE + GUARD_BYTES];
static uint8_t idat[OUT_SIZE];
static uint8_t raw_zlib[RAW_SIZE + 1];
static uint8_t raw_own[RAW_SIZE + 1];
static uint8_t colour_seen[32768];
static unsigned filters_used[5];
static unsigned blocks_seen[3];

/* Allocation arena: a size header before each block and a red zone after
   it, checked when the block is freed. */
#define ARENA_SIZE (256u * 1024u)
#define ARENA_HEADER 16u
#define ARENA_RED_ZONE 16u
#define ARENA_RED_VALUE 0xFDu
#define ARENA_FREED ((size_t)-1)
#define NO_FAILURE 0xFFFFFFFFu

static union
{
  uint64_t align;
  uint8_t bytes[ARENA_SIZE];
} arena;
static size_t arena_top;
static size_t arena_live;
static size_t arena_peak;
static unsigned arena_attempts;
static unsigned arena_allocs;
static unsigned arena_frees;
static unsigned arena_fail_at = NO_FAILURE;

static size_t arena_padded(size_t bytes)
{
  return (bytes + 15u) & ~(size_t)15u;
}

static voidpf test_zalloc(voidpf opaque, uInt items, uInt size)
{
  size_t bytes = (size_t)items * size;
  size_t padded = arena_padded(bytes);
  uint8_t *block;

  (void)opaque;
  if(arena_attempts++ == arena_fail_at)
    return Z_NULL;
  assert(arena_top + ARENA_HEADER + padded + ARENA_RED_ZONE <= ARENA_SIZE);

  block = arena.bytes + arena_top;
  memcpy(block, &bytes, sizeof(bytes));
  /* Never hand out zeroes, so nothing relies on them. */
  memset(block + ARENA_HEADER, 0xA5, bytes);
  memset(block + ARENA_HEADER + bytes, ARENA_RED_VALUE,
   padded - bytes + ARENA_RED_ZONE);
  arena_top += ARENA_HEADER + padded + ARENA_RED_ZONE;
  arena_live += bytes;
  if(arena_live > arena_peak)
    arena_peak = arena_live;
  arena_allocs++;
  return block + ARENA_HEADER;
}

static void test_zfree(voidpf opaque, voidpf address)
{
  uint8_t *data = address;
  size_t bytes, i;

  (void)opaque;
  assert(data != NULL);
  memcpy(&bytes, data - ARENA_HEADER, sizeof(bytes));
  assert(bytes != ARENA_FREED);
  for(i = bytes; i < arena_padded(bytes) + ARENA_RED_ZONE; i++)
    assert(data[i] == ARENA_RED_VALUE);

  arena_live -= bytes;
  arena_frees++;
  bytes = ARENA_FREED;
  memcpy(data - ARENA_HEADER, &bytes, sizeof(bytes));
}

/* Every block was given back; starts the next count. */
static void arena_reset(void)
{
  assert(arena_live == 0);
  assert(arena_allocs == arena_frees);
  arena_top = 0;
  arena_peak = 0;
  arena_attempts = 0;
  arena_allocs = 0;
  arena_frees = 0;
  arena_fail_at = NO_FAILURE;
}

static uint32_t read32(const uint8_t *p)
{
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
   ((uint32_t)p[2] << 8) | p[3];
}

/* Bitwise CRC-32 (ISO 3309, as PNG uses it), independent of zlib's. */
static uint32_t test_crc32(const uint8_t *data, size_t length)
{
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  unsigned bit;

  for(i = 0; i < length; i++)
  {
    crc ^= data[i];
    for(bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

static uint32_t test_adler32(const uint8_t *data, size_t length)
{
  uint32_t a = 1;
  uint32_t b = 0;
  size_t i;

  for(i = 0; i < length; i++)
  {
    a = (a + data[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

/* A second inflater, from RFC 1950 and RFC 1951, that asserts on anything
   malformed. */
typedef struct
{
  const uint8_t *in;
  size_t in_size;
  size_t in_pos;
  uint32_t bit_buffer;
  unsigned bit_count;
  uint8_t *out;
  size_t out_size;
  size_t out_pos;
} Inflater;

typedef struct
{
  uint16_t count[16];     /* number of codes of each length */
  uint16_t symbols[288];  /* symbols in code order */
} HuffmanTable;

static uint32_t inflater_bits(Inflater *s, unsigned count)
{
  uint32_t value;

  while(s->bit_count < count)
  {
    assert(s->in_pos < s->in_size);
    s->bit_buffer |= (uint32_t)s->in[s->in_pos++] << s->bit_count;
    s->bit_count += 8;
  }
  value = s->bit_buffer & ((1u << count) - 1u);
  s->bit_buffer >>= count;
  s->bit_count -= count;
  return value;
}

/* Canonical code of RFC 1951 3.2.2: codes of one length are consecutive
   and follow symbol order. */
static void huffman_build(HuffmanTable *t, const uint8_t *lengths,
 unsigned n)
{
  uint16_t offset[16] = { 0 };
  unsigned i, length;
  int left = 1;

  memset(t->count, 0, sizeof(t->count));
  for(i = 0; i < n; i++)
    t->count[lengths[i]]++;
  t->count[0] = 0;
  for(length = 1; length < 16; length++)
  {
    left = left * 2 - t->count[length];
    assert(left >= 0);
  }

  offset[1] = 0;
  for(length = 1; length < 15; length++)
    offset[length + 1] = (uint16_t)(offset[length] + t->count[length]);
  for(i = 0; i < n; i++)
  {
    if(lengths[i] != 0)
      t->symbols[offset[lengths[i]]++] = (uint16_t)i;
  }
}

static unsigned huffman_decode(Inflater *s, const HuffmanTable *t)
{
  uint32_t code = 0;
  uint32_t first = 0;
  uint32_t index = 0;
  unsigned length;

  for(length = 1; length < 16; length++)
  {
    /* Huffman codes are packed starting from their most significant bit. */
    code |= inflater_bits(s, 1);
    if(code - first < t->count[length])
      return t->symbols[index + code - first];
    index += t->count[length];
    first = (first + t->count[length]) << 1;
    code <<= 1;
  }
  assert(0);
  return 0;
}

/* RFC 1951 3.2.5: match lengths 3-258 for codes 257-285. */
static unsigned inflater_length(Inflater *s, unsigned code)
{
  unsigned base = 3;
  unsigned i;

  assert(code < 29);
  if(code == 28)
    return 258;
  for(i = 0; i < code; i++)
    base += 1u << (i < 8 ? 0 : (i - 4) / 4);
  return base + inflater_bits(s, code < 8 ? 0 : (code - 4) / 4);
}

/* RFC 1951 3.2.5: distances 1-32768 for codes 0-29. */
static unsigned inflater_distance(Inflater *s, unsigned code)
{
  unsigned base = 1;
  unsigned i;

  assert(code < 30);
  for(i = 0; i < code; i++)
    base += 1u << (i < 4 ? 0 : (i - 2) / 2);
  return base + inflater_bits(s, code < 4 ? 0 : (code - 2) / 2);
}

static void inflater_codes(Inflater *s, const HuffmanTable *literals,
 const HuffmanTable *distances)
{
  for(;;)
  {
    unsigned symbol = huffman_decode(s, literals);
    unsigned length, distance;

    if(symbol < 256)
    {
      assert(s->out_pos < s->out_size);
      s->out[s->out_pos++] = (uint8_t)symbol;
      continue;
    }
    if(symbol == 256)
      return;

    length = inflater_length(s, symbol - 257);
    distance = inflater_distance(s, huffman_decode(s, distances));
    assert(distance <= s->out_pos);
    assert(length <= s->out_size - s->out_pos);
    while(length--)
    {
      s->out[s->out_pos] = s->out[s->out_pos - distance];
      s->out_pos++;
    }
  }
}

static void inflater_fixed(Inflater *s)
{
  uint8_t lengths[288];
  HuffmanTable literals, distances;
  unsigned i;

  for(i = 0; i < 288; i++)
    lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
  huffman_build(&literals, lengths, 288);
  for(i = 0; i < 30; i++)
    lengths[i] = 5;
  huffman_build(&distances, lengths, 30);
  inflater_codes(s, &literals, &distances);
}

static void inflater_dynamic(Inflater *s)
{
  static const uint8_t order[19] =
   { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
  uint8_t lengths[286 + 30];
  HuffmanTable code_lengths, literals, distances;
  unsigned literal_count = inflater_bits(s, 5) + 257;
  unsigned distance_count = inflater_bits(s, 5) + 1;
  unsigned code_count = inflater_bits(s, 4) + 4;
  unsigned i;

  assert(literal_count <= 286 && distance_count <= 30);
  memset(lengths, 0, 19);
  for(i = 0; i < code_count; i++)
    lengths[order[i]] = (uint8_t)inflater_bits(s, 3);
  huffman_build(&code_lengths, lengths, 19);

  i = 0;
  while(i < literal_count + distance_count)
  {
    unsigned symbol = huffman_decode(s, &code_lengths);
    unsigned repeat;
    uint8_t value = 0;

    if(symbol < 16)
    {
      lengths[i++] = (uint8_t)symbol;
      continue;
    }
    if(symbol == 16)
    {
      assert(i > 0);
      value = lengths[i - 1];
      repeat = 3 + inflater_bits(s, 2);
    }
    else if(symbol == 17)
      repeat = 3 + inflater_bits(s, 3);
    else
      repeat = 11 + inflater_bits(s, 7);
    assert(i + repeat <= literal_count + distance_count);
    while(repeat--)
      lengths[i++] = value;
  }

  assert(lengths[256] != 0);
  huffman_build(&literals, lengths, literal_count);
  huffman_build(&distances, lengths + literal_count, distance_count);
  inflater_codes(s, &literals, &distances);
}

/* Inflates a zlib stream and checks its header and Adler-32; returns the
   inflated size. */
static size_t own_inflate(const uint8_t *in, size_t in_size, uint8_t *out,
 size_t out_size)
{
  Inflater s;
  unsigned last;

  assert(in_size >= 6);
  assert((in[0] & 15) == 8 && (in[0] >> 4) <= 7);
  assert((((unsigned)in[0] << 8) | in[1]) % 31 == 0);
  assert((in[1] & 0x20) == 0);

  memset(&s, 0, sizeof(s));
  s.in = in;
  s.in_size = in_size;
  s.in_pos = 2;
  s.out = out;
  s.out_size = out_size;

  do
  {
    unsigned type;

    last = inflater_bits(&s, 1);
    type = inflater_bits(&s, 2);
    assert(type < 3);
    blocks_seen[type]++;
    if(type == 0)
    {
      unsigned length;

      /* Stored data starts on the next byte boundary. */
      s.bit_buffer = 0;
      s.bit_count = 0;
      assert(s.in_size - s.in_pos >= 4);
      length = in[s.in_pos] | ((unsigned)in[s.in_pos + 1] << 8);
      assert((in[s.in_pos + 2] | ((unsigned)in[s.in_pos + 3] << 8)) ==
       (~length & 0xFFFFu));
      s.in_pos += 4;
      assert(length <= s.in_size - s.in_pos);
      assert(length <= s.out_size - s.out_pos);
      memcpy(s.out + s.out_pos, in + s.in_pos, length);
      s.in_pos += length;
      s.out_pos += length;
    }
    else if(type == 1)
      inflater_fixed(&s);
    else
      inflater_dynamic(&s);
  }
  while(!last);

  assert(s.in_pos + 4 == s.in_size);
  assert(read32(in + s.in_pos) == test_adler32(out, s.out_pos));
  return s.out_pos;
}

/* The decoder above on stored blocks, which PNGs of 15-bit pixels never
   contain, and on a fixed-code stream. */
static void test_own_inflater(void)
{
  static const uint8_t fixed_abc[] =
   { 0x78, 0x9c, 0x4b, 0x4c, 0x4a, 0x06, 0x00, 0x02, 0x4d, 0x01, 0x27 };
  uint8_t text[600];
  z_stream z;
  unsigned i;

  assert(own_inflate(fixed_abc, sizeof(fixed_abc), raw_own, 4) == 3);
  assert(memcmp(raw_own, "abc", 3) == 0);

  for(i = 0; i < sizeof(text); i++)
    text[i] = (uint8_t)(i * 7);
  memset(&z, 0, sizeof(z));
  z.zalloc = test_zalloc;
  z.zfree = test_zfree;
  /* Level 0 stores; a small window keeps it inside the arena. */
  assert(deflateInit2(&z, 0, Z_DEFLATED, 9, 1, Z_DEFAULT_STRATEGY) == Z_OK);
  z.next_in = text;
  z.avail_in = sizeof(text);
  z.next_out = idat;
  z.avail_out = OUT_SIZE;
  assert(deflate(&z, Z_FINISH) == Z_STREAM_END);
  assert(deflateEnd(&z) == Z_OK);
  arena_reset();

  assert(own_inflate(idat, z.total_out, raw_own, sizeof(text)) ==
   sizeof(text));
  assert(memcmp(raw_own, text, sizeof(text)) == 0);
  assert(blocks_seen[0] > 0 && blocks_seen[1] > 0);
}

typedef struct
{
  uint32_t width;
  uint32_t height;
  unsigned colour_type;
  uint8_t palette[768];
  unsigned palette_size;
} PngImage;

/* Checks the signature, the chunk order and every CRC, and gathers the
   IDAT data into idat; returns its length. */
static size_t parse_png(const uint8_t *data, size_t size, PngImage *image)
{
  size_t pos = 8;
  size_t idat_size = 0;
  unsigned index = 0;
  int in_idat = 0;
  int idat_done = 0;
  int ended = 0;

  memset(image, 0, sizeof(*image));
  assert(size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0);
  while(pos < size)
  {
    const uint8_t *type = data + pos + 4;
    const uint8_t *body = data + pos + 8;
    uint32_t length;

    assert(!ended);
    assert(size - pos >= 12);
    length = read32(data + pos);
    assert(length <= size - pos - 12);
    assert(read32(body + length) == test_crc32(type, length + 4));

    if(index == 0)
    {
      assert(memcmp(type, "IHDR", 4) == 0 && length == 13);
      image->width = read32(body);
      image->height = read32(body + 4);
      image->colour_type = body[9];
      assert(body[8] == 8);
      assert(image->colour_type == 2 || image->colour_type == 3);
      assert(body[10] == 0 && body[11] == 0 && body[12] == 0);
    }
    else if(memcmp(type, "PLTE", 4) == 0)
    {
      assert(image->colour_type == 3 && image->palette_size == 0);
      assert(!in_idat && length >= 3 && length <= 768 && length % 3 == 0);
      memcpy(image->palette, body, length);
      image->palette_size = length / 3;
    }
    else if(memcmp(type, "IDAT", 4) == 0)
    {
      assert(!idat_done && length <= 8192);
      assert(image->colour_type == 2 || image->palette_size > 0);
      memcpy(idat + idat_size, body, length);
      idat_size += length;
      in_idat = 1;
    }
    else
    {
      assert(memcmp(type, "IEND", 4) == 0 && length == 0);
      ended = 1;
    }
    if(in_idat && memcmp(type, "IDAT", 4) != 0)
      idat_done = 1;
    pos += (size_t)length + 12;
    index++;
  }
  assert(ended && in_idat && pos == size);
  return idat_size;
}

static int spec_paeth(int a, int b, int c)
{
  int p = a + b - c;
  int pa = p > a ? p - a : a - p;
  int pb = p > b ? p - b : b - p;
  int pc = p > c ? p - c : c - p;

  if(pa <= pb && pa <= pc)
    return a;
  return pb <= pc ? b : c;
}

/* Undoes the PNG filters in place (PNG specification, section 9). */
static void unfilter(uint8_t *raw, uint32_t width, uint32_t height,
 unsigned bpp)
{
  size_t line = 1 + (size_t)width * bpp;
  size_t i;
  uint32_t y;

  for(y = 0; y < height; y++)
  {
    uint8_t *row = raw + y * line + 1;
    const uint8_t *above = y > 0 ? row - line : NULL;
    unsigned type = row[-1];

    assert(type <= 4);
    if(bpp == 3)
      filters_used[type]++;
    for(i = 0; i < line - 1; i++)
    {
      int a = i >= bpp ? row[i - bpp] : 0;
      int b = above != NULL ? above[i] : 0;
      int c = above != NULL && i >= bpp ? above[i - bpp] : 0;
      int predictor = 0;

      if(type == 1)
        predictor = a;
      else if(type == 2)
        predictor = b;
      else if(type == 3)
        predictor = (a + b) / 2;
      else if(type == 4)
        predictor = spec_paeth(a, b, c);
      row[i] = (uint8_t)(row[i] + predictor);
    }
  }
}

static uint8_t expand5(unsigned value)
{
  return (uint8_t)((value << 3) | (value >> 2));
}

static unsigned count_colours(const uint16_t *pixels, uint32_t width,
 uint32_t height, uint32_t stride)
{
  unsigned count = 0;
  uint32_t x, y;

  memset(colour_seen, 0, sizeof(colour_seen));
  for(y = 0; y < height; y++)
  {
    for(x = 0; x < width; x++)
    {
      unsigned key = pixels[y * stride + x] & 0x7FFFu;

      if(!colour_seen[key])
      {
        colour_seen[key] = 1;
        count++;
      }
    }
  }
  return count;
}

/* Decodes a PNG both ways and compares it with the pixels it came from. */
static void check_png(const uint16_t *pixels, uint32_t width,
 uint32_t height, uint32_t stride, const uint8_t *data, size_t size)
{
  PngImage image;
  unsigned colours = count_colours(pixels, width, height, stride);
  size_t idat_size = parse_png(data, size, &image);
  unsigned bpp = image.colour_type == 3 ? 1 : 3;
  size_t raw_size = (size_t)height * (1 + (size_t)width * bpp);
  size_t line = 1 + (size_t)width * bpp;
  z_stream z;
  uint32_t x, y;

  assert(image.width == width && image.height == height);
  assert((image.colour_type == 3) == (colours <= 256));
  assert(image.colour_type == 2 || image.palette_size == colours);
  assert(raw_size <= RAW_SIZE);

  memset(&z, 0, sizeof(z));
  z.zalloc = test_zalloc;
  z.zfree = test_zfree;
  assert(inflateInit(&z) == Z_OK);
  z.next_in = idat;
  z.avail_in = (uInt)idat_size;
  z.next_out = raw_zlib;
  z.avail_out = (uInt)raw_size + 1;
  assert(inflate(&z, Z_FINISH) == Z_STREAM_END);
  assert(z.total_out == raw_size && z.avail_in == 0);
  assert(inflateEnd(&z) == Z_OK);
  arena_reset();

  assert(own_inflate(idat, idat_size, raw_own, raw_size + 1) == raw_size);
  assert(memcmp(raw_zlib, raw_own, raw_size) == 0);

  unfilter(raw_own, width, height, bpp);
  for(y = 0; y < height; y++)
  {
    for(x = 0; x < width; x++)
    {
      unsigned pixel = pixels[y * stride + x];
      const uint8_t *sample = raw_own + y * line + 1 + x * bpp;
      const uint8_t *rgb = sample;

      if(bpp == 1)
      {
        assert(*sample < image.palette_size);
        rgb = image.palette + 3 * *sample;
      }
      assert(rgb[0] == expand5(pixel & 31u));
      assert(rgb[1] == expand5((pixel >> 5) & 31u));
      assert(rgb[2] == expand5((pixel >> 10) & 31u));
    }
  }
}

/* Encodes with png_write_rgba5551, checks the result and returns its
   size; *peak gets the most memory held at once. */
static size_t encode_and_check(const uint16_t *pixels, uint32_t width,
 uint32_t height, uint32_t stride, size_t *peak)
{
  size_t written = 0;
  unsigned i;

  memset(png, GUARD_VALUE, sizeof(png));
  assert(png_write_rgba5551(pixels, width, height, stride, png, OUT_SIZE,
   &written, test_zalloc, test_zfree, NULL) == PNG_WRITE_OK);
  assert(written > 0 && written <= OUT_SIZE);
  for(i = 0; i < GUARD_BYTES; i++)
    assert(png[OUT_SIZE + i] == GUARD_VALUE);
  *peak = arena_peak;
  arena_reset();

  check_png(pixels, width, height, stride, png, written);
  return written;
}

/* Deterministic frame drawing. */
static uint32_t rng_state;

static uint32_t rng(void)
{
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

static unsigned rng_below(unsigned n)
{
  return rng() % n;
}

static void put_pixel(int x, int y, unsigned colour)
{
  if(x >= 0 && y >= 0 && x < (int)FRAME_WIDTH && y < (int)FRAME_HEIGHT)
    frame[(unsigned)y * FRAME_STRIDE + (unsigned)x] = (uint16_t)colour;
}

static void fill_rect(int x, int y, int width, int height, unsigned colour)
{
  int i, j;

  for(j = 0; j < height; j++)
    for(i = 0; i < width; i++)
      put_pixel(x + i, y + j, colour);
}

static void clear_frame(void)
{
  unsigned i;

  for(i = 0; i < FRAME_STRIDE * FRAME_HEIGHT; i++)
    frame[i] = PADDING_COLOUR;
}

/* GBA-like scenes: 16 palettes of 16 colours, 8x8 tiles, an 8x8 font,
   mirrored sprites. */
static uint16_t scene_palette[256];
static uint8_t tiles[16][64];
static uint8_t font[40][64];

enum { TILE_FLAT, TILE_SPECKLE, TILE_BRICK, TILE_DETAIL };

static void make_tile(uint8_t *tile, unsigned style)
{
  unsigned base = 1 + rng_below(15);
  unsigned i;

  for(i = 0; i < 64; i++)
  {
    unsigned x = i % 8;
    unsigned y = i / 8;

    if(style == TILE_FLAT)
      tile[i] = (uint8_t)base;
    else if(style == TILE_SPECKLE)
      tile[i] = (uint8_t)(rng_below(6) == 0 ? 1 + rng_below(15) : base);
    else if(style == TILE_BRICK)
      tile[i] = (uint8_t)(y % 4 == 3 || x == (y / 4) * 4 ? 1 : base);
    else
      tile[i] = (uint8_t)rng_below(16);
  }
}

static void make_font(void)
{
  unsigned glyph, i;

  for(glyph = 0; glyph < 40; glyph++)
  {
    for(i = 0; i < 64; i++)
    {
      unsigned x = i % 8;
      unsigned y = i / 8;

      font[glyph][i] = (uint8_t)(x >= 1 && x <= 5 && y <= 6 &&
       rng_below(5) < 2);
    }
  }
}

static void draw_tile(int x, int y, const uint8_t *tile, unsigned palette,
 int transparent)
{
  unsigned i;

  for(i = 0; i < 64; i++)
  {
    if(transparent && tile[i] == 0)
      continue;
    put_pixel(x + (int)(i % 8), y + (int)(i / 8),
     scene_palette[palette * 16 + tile[i]]);
  }
}

static void draw_text(int x, int y, unsigned characters, unsigned colour)
{
  unsigned i, p;

  for(i = 0; i < characters; i++)
  {
    unsigned glyph = rng_below(48);

    if(glyph >= 40)
      continue; /* a space */
    for(p = 0; p < 64; p++)
    {
      if(font[glyph][p])
        put_pixel(x + (int)(i * 8 + p % 8), y + (int)(p / 8), colour);
    }
  }
}

/* A left-right symmetric sprite: an outlined shape filled with runs of a
   few colours. */
static void draw_sprite(int x, int y, unsigned size, unsigned palette)
{
  unsigned half = size / 2;
  unsigned extent = half / 2;
  unsigned row, column;

  for(row = 0; row < size; row++)
  {
    unsigned colour = 2 + rng_below(4);

    if(rng_below(2))
      extent = extent + 1 < half ? extent + 1 : extent;
    else if(extent > 1)
      extent--;
    for(column = 0; column < extent; column++)
    {
      unsigned index = column == 0 || row == 0 || row == size - 1 ? 1 : colour;
      unsigned value = scene_palette[palette * 16 + index];

      if(rng_below(5) == 0)
        colour = 2 + rng_below(4);
      put_pixel(x + (int)(half - 1 - (extent - 1 - column)), y + (int)row,
       value);
      put_pixel(x + (int)(half + (extent - 1 - column)), y + (int)row, value);
    }
  }
}

typedef struct
{
  unsigned sky_bands;    /* colour bands at the top; 0 fills it with tiles */
  unsigned ground_rows;  /* rows of repeated ground tiles at the bottom */
  unsigned objects;      /* 2x2 blocks of detailed tiles on the map */
  unsigned sprites;      /* 16x16 sprites */
  unsigned big_sprites;  /* 32x32 or 64x64 sprites */
  unsigned hud;          /* a line of text at the top */
  unsigned text_box;     /* lines of text in a box at the bottom */
} SceneRecipe;

static const SceneRecipe scenes[10] =
{
  { 8, 4, 3, 4, 0, 1, 0 },   /* platformer */
  { 0, 0, 6, 5, 0, 0, 2 },   /* overworld with a dialogue */
  { 1, 0, 0, 1, 0, 1, 8 },   /* menu */
  { 4, 0, 0, 0, 2, 1, 2 },   /* battle */
  { 10, 2, 0, 0, 1, 0, 1 },  /* title screen */
  { 1, 0, 8, 0, 0, 1, 0 },   /* puzzle */
  { 0, 3, 4, 6, 0, 1, 0 },   /* top-down action */
  { 0, 0, 10, 3, 1, 1, 0 },  /* dungeon */
  { 2, 0, 0, 0, 2, 0, 3 },   /* cutscene with portraits */
  { 1, 0, 0, 10, 1, 1, 0 }   /* shooter */
};

static void draw_scene(unsigned seed)
{
  const SceneRecipe *recipe = &scenes[seed];
  unsigned i, x, y, palette;
  unsigned ground_top = FRAME_HEIGHT - recipe->ground_rows * 8;

  rng_state = 0x9E3779B9u * (seed + 1);
  clear_frame();
  for(i = 0; i < 256; i++)
    scene_palette[i] = (uint16_t)(rng() & 0x7FFFu);
  make_tile(tiles[0], TILE_SPECKLE);
  make_tile(tiles[1], TILE_BRICK);
  make_tile(tiles[2], TILE_FLAT);
  for(i = 3; i < 16; i++)
    make_tile(tiles[i], i < 6 ? TILE_SPECKLE : TILE_DETAIL);
  make_font();

  /* Backdrop: colour bands like a sky, or a floor of two tiles. */
  if(recipe->sky_bands > 0)
  {
    unsigned band = (ground_top + recipe->sky_bands - 1) / recipe->sky_bands;

    for(y = 0; y < ground_top; y++)
      fill_rect(0, (int)y, FRAME_WIDTH, 1,
       scene_palette[16 + y / band]);
  }
  else
  {
    for(y = 0; y < ground_top; y += 8)
      for(x = 0; x < FRAME_WIDTH; x += 8)
        draw_tile((int)x, (int)y, tiles[rng_below(8) == 0 ? 3 : 0], 1, 0);
  }

  for(y = ground_top; y < FRAME_HEIGHT; y += 8)
    for(x = 0; x < FRAME_WIDTH; x += 8)
      draw_tile((int)x, (int)y, tiles[y == ground_top ? 1 : 2], 2, 0);

  for(i = 0; i < recipe->objects; i++)
  {
    int ox = (int)(rng_below(FRAME_WIDTH / 16) * 16);
    int oy = (int)(rng_below(FRAME_HEIGHT / 16) * 16);

    palette = 3 + rng_below(3);
    draw_tile(ox, oy, tiles[6 + rng_below(10)], palette, 0);
    draw_tile(ox + 8, oy, tiles[6 + rng_below(10)], palette, 0);
    draw_tile(ox, oy + 8, tiles[6 + rng_below(10)], palette, 0);
    draw_tile(ox + 8, oy + 8, tiles[6 + rng_below(10)], palette, 0);
  }

  for(i = 0; i < recipe->big_sprites; i++)
  {
    unsigned size = rng_below(2) ? 64 : 32;

    draw_sprite((int)rng_below(FRAME_WIDTH - size),
     (int)rng_below(FRAME_HEIGHT - size), size, 8 + i % 4);
  }
  for(i = 0; i < recipe->sprites; i++)
    draw_sprite((int)rng_below(FRAME_WIDTH - 16),
     (int)rng_below(FRAME_HEIGHT - 16), 16, 12 + i % 4);

  if(recipe->hud)
    draw_text(8, 4, 26, scene_palette[255]);

  if(recipe->text_box > 0)
  {
    int top = (int)(FRAME_HEIGHT - 12 * recipe->text_box - 12);

    fill_rect(2, top, FRAME_WIDTH - 4, (int)FRAME_HEIGHT - top - 2,
     scene_palette[6 * 16 + 1]);
    fill_rect(4, top + 2, FRAME_WIDTH - 8, (int)FRAME_HEIGHT - top - 6,
     scene_palette[6 * 16 + 2]);
    for(i = 0; i < recipe->text_box; i++)
      draw_text(10, top + 6 + (int)i * 12, 27, scene_palette[6 * 16 + 3]);
  }
}

/* The 256 colours of index k < 256 are distinct (odd multiplier). */
static unsigned distinct_colour(unsigned k)
{
  return (k * 0x9E37u) & 0x7FFFu;
}

static void draw_synthetic(unsigned which)
{
  unsigned x, y;

  rng_state = 0x2545F491u * (which + 1);
  clear_frame();
  for(y = 0; y < FRAME_HEIGHT; y++)
  {
    for(x = 0; x < FRAME_WIDTH; x++)
    {
      unsigned r = 0, g = 0, b = 0, colour;

      switch(which)
      {
        case 0: /* exactly 256 colours, in 15x10 blocks */
        case 1: /* the same with one more colour */
          colour = distinct_colour((y / 10) * 16 + x / 15);
          break;
        case 2: /* horizontal and vertical ramps */
          r = x * 31 / (FRAME_WIDTH - 1);
          g = y * 31 / (FRAME_HEIGHT - 1);
          b = 15;
          colour = r | (g << 5) | (b << 10);
          break;
        case 3: /* diagonal ramps on three channels */
          r = (x + y) * 31 / (FRAME_WIDTH + FRAME_HEIGHT - 2);
          g = (x * 2) % 32;
          b = (y * 3) % 32;
          colour = r | (g << 5) | (b << 10);
          break;
        case 4: /* noise over every colour, alpha bit included */
          colour = rng() & 0xFFFFu;
          break;
        case 5: /* noise over 256 colours */
          colour = distinct_colour(rng_below(256));
          break;
        case 6: /* one colour, alpha bit random */
          colour = 0x1234u | (rng_below(2) << 15);
          break;
        case 7: /* two-colour checkerboard */
          colour = (x + y) % 2 ? 0x7FFFu : 0;
          break;
        case 8: /* ramps over noise in the lower half */
          r = x * 31 / (FRAME_WIDTH - 1);
          g = y < FRAME_HEIGHT / 2 ? 8 : rng_below(32);
          colour = r | (g << 5) | (20u << 10);
          break;
        default: /* rings */
          r = ((x * x + y * y) >> 6) % 32;
          g = ((x * y) >> 7) % 32;
          b = (r + g) / 2;
          colour = r | (g << 5) | (b << 10);
          break;
      }
      frame[y * FRAME_STRIDE + x] = (uint16_t)colour;
    }
  }
  if(which == 1)
    frame[(FRAME_HEIGHT - 1) * FRAME_STRIDE + FRAME_WIDTH - 1] =
     (uint16_t)distinct_colour(256);
}

static size_t gba_median;

static void test_reference_frames(void)
{
  size_t sizes[10];
  unsigned i, j;

  for(i = 0; i < 20; i++)
  {
    size_t peak = 0;
    size_t size;

    if(i < 10)
      draw_scene(i);
    else
      draw_synthetic(i - 10);
    if(i < 10)
      assert(count_colours(frame, FRAME_WIDTH, FRAME_HEIGHT,
       FRAME_STRIDE) <= 256);
    if(i == 10)
      assert(count_colours(frame, FRAME_WIDTH, FRAME_HEIGHT,
       FRAME_STRIDE) == 256);
    if(i == 11)
      assert(count_colours(frame, FRAME_WIDTH, FRAME_HEIGHT,
       FRAME_STRIDE) == 257);

    size = encode_and_check(frame, FRAME_WIDTH, FRAME_HEIGHT, FRAME_STRIDE,
     &peak);
    assert(peak <= PEAK_LIMIT);
    if(i < 10)
      sizes[i] = size;
  }

  /* Median of the GBA-like frames. */
  for(i = 1; i < 10; i++)
  {
    size_t value = sizes[i];

    for(j = i; j > 0 && sizes[j - 1] > value; j--)
      sizes[j] = sizes[j - 1];
    sizes[j] = value;
  }
  gba_median = (sizes[4] + sizes[5]) / 2;
  assert(gba_median <= MEDIAN_LIMIT);

  /* The RGB frames picked more than one filter. */
  for(i = 0, j = 0; i < 5; i++)
    j += filters_used[i] > 0;
  assert(j >= 3);
  assert(blocks_seen[2] > 0);
}

/* Small and narrow images, rows further apart than they are wide. */
static void test_small_images(void)
{
  size_t peak;
  unsigned i;

  frame[0] = 0x7C00u;
  assert(encode_and_check(frame, 1, 1, 1, &peak) > 0);

  for(i = 0; i < 15; i++)
    frame[i] = (uint16_t)distinct_colour(i % 4);
  assert(encode_and_check(frame, 3, 2, 7, &peak) > 0);

  /* 272 colours, 17 wide: RGB. */
  for(i = 0; i < 17 * 16; i++)
    frame[i] = (uint16_t)distinct_colour(i);
  assert(encode_and_check(frame, 17, 16, 17, &peak) > 0);

  /* 257 colours in one column. */
  for(i = 0; i < 257; i++)
    frame[i * 3] = (uint16_t)distinct_colour(i);
  assert(encode_and_check(frame, 1, 257, 3, &peak) > 0);
}

static void test_invalid_arguments(void)
{
  size_t written = 7;

  assert(png_write_rgba5551(NULL, 1, 1, 1, png, OUT_SIZE, &written,
   test_zalloc, test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 1, 1, 1, NULL, OUT_SIZE, &written,
   test_zalloc, test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 1, 1, 1, png, OUT_SIZE, NULL,
   test_zalloc, test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 1, 1, 1, png, OUT_SIZE, &written,
   NULL, test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 1, 1, 1, png, OUT_SIZE, &written,
   test_zalloc, NULL, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 0, 1, 1, png, OUT_SIZE, &written,
   test_zalloc, test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 1, 0, 1, png, OUT_SIZE, &written,
   test_zalloc, test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 2, 1, 1, png, OUT_SIZE, &written,
   test_zalloc, test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, PNG_WRITE_MAX_SIDE + 1, 1,
   PNG_WRITE_MAX_SIDE + 1, png, OUT_SIZE, &written, test_zalloc,
   test_zfree, NULL) == PNG_WRITE_ERR_ARGUMENT);
  assert(png_write_rgba5551(frame, 1, PNG_WRITE_MAX_SIDE + 1, 1, png,
   OUT_SIZE, &written, test_zalloc, test_zfree, NULL) ==
   PNG_WRITE_ERR_ARGUMENT);
  assert(written == 7);
  assert(arena_attempts == 0);
}

/* Every buffer short of the PNG's size fails without writing past it, and
   the exact size succeeds. */
static void check_short_buffers(void)
{
  size_t full = 0;
  size_t written;
  size_t sizes[12];
  unsigned i, k;

  assert(png_write_rgba5551(frame, FRAME_WIDTH, FRAME_HEIGHT, FRAME_STRIDE,
   png, OUT_SIZE, &full, test_zalloc, test_zfree, NULL) == PNG_WRITE_OK);
  arena_reset();

  sizes[0] = 0;
  sizes[1] = 1;
  sizes[2] = 8;
  sizes[3] = 32;
  sizes[4] = 33;
  sizes[5] = 60;
  sizes[6] = full / 2;
  sizes[7] = full - 13;
  sizes[8] = full - 12;
  sizes[9] = full - 1;
  sizes[10] = full > 8300 ? 8300 : full - 2;
  sizes[11] = full;
  for(k = 0; k < 12; k++)
  {
    int result;

    memset(png, GUARD_VALUE, sizes[k] + GUARD_BYTES);
    written = 0;
    result = png_write_rgba5551(frame, FRAME_WIDTH, FRAME_HEIGHT,
     FRAME_STRIDE, png, sizes[k], &written, test_zalloc, test_zfree, NULL);
    arena_reset();
    for(i = 0; i < GUARD_BYTES; i++)
      assert(png[sizes[k] + i] == GUARD_VALUE);
    if(k < 11)
    {
      assert(result == PNG_WRITE_ERR_SPACE);
      assert(written == 0);
    }
    else
    {
      assert(result == PNG_WRITE_OK && written == full);
      check_png(frame, FRAME_WIDTH, FRAME_HEIGHT, FRAME_STRIDE, png, full);
    }
  }
}

static void test_short_buffers(void)
{
  draw_scene(0);
  check_short_buffers();
  draw_synthetic(4);
  check_short_buffers();
}

/* Each allocation in turn fails: an error, and every block given back. */
static void test_failed_allocations(void)
{
  unsigned fail_at;
  int result = PNG_WRITE_ERR_MEMORY;

  draw_synthetic(3);
  for(fail_at = 0; result != PNG_WRITE_OK; fail_at++)
  {
    size_t written = 0;

    assert(fail_at < 32);
    arena_fail_at = fail_at;
    result = png_write_rgba5551(frame, FRAME_WIDTH, FRAME_HEIGHT,
     FRAME_STRIDE, png, OUT_SIZE, &written, test_zalloc, test_zfree, NULL);
    assert(result == PNG_WRITE_OK || result == PNG_WRITE_ERR_MEMORY);
    assert((result == PNG_WRITE_OK) == (written > 0));
    arena_reset();
  }
  assert(fail_at > 2);
}

int main(void)
{
  test_own_inflater();
  test_invalid_arguments();
  test_reference_frames();
  test_small_images();
  test_short_buffers();
  test_failed_allocations();
  puts("PNG writer tests passed");
  return 0;
}
