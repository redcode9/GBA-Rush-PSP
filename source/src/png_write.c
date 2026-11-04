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

#include <string.h>

#include "png_write.h"

/* Deflate's 8 KiB window and smaller hash tables need 64 KiB plus its
   state (zlib.h: 2^(windowBits+2) + 2^(memLevel+9)), which keeps a shot
   under 96 KiB with the scratch rows. */
#define PNG_WINDOW_BITS 13
#define PNG_MEM_LEVEL 6
/* IDAT chunks are cut at this size, so the stream can later be written
   out block by block. */
#define PNG_IDAT_BLOCK 8192u
/* One bit per 15-bit colour. */
#define PNG_SEEN_BYTES (32768u / 8)
#define PNG_PALETTE_BYTES (256u * 2)

typedef struct
{
  z_stream z;
  uint8_t *out;
  size_t size;
  size_t pos; /* end of the last complete chunk */
} PngWriter;

static void png_put32(uint8_t *p, uint32_t value)
{
  p[0] = (uint8_t)(value >> 24);
  p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8);
  p[3] = (uint8_t)value;
}

/* Starts a chunk of the given type at w->pos and returns where its data
   goes, or NULL when room data bytes and the chunk's 12 bytes of length,
   type and CRC do not fit. */
static uint8_t *png_chunk_open(PngWriter *w, const char *type, size_t room)
{
  if(w->size - w->pos < room + 12)
    return NULL;

  memcpy(w->out + w->pos + 4, type, 4);
  return w->out + w->pos + 8;
}

/* Ends the chunk png_chunk_open started, after length data bytes. */
static void png_chunk_close(PngWriter *w, uint32_t length)
{
  uint8_t *chunk = w->out + w->pos;

  png_put32(chunk, length);
  png_put32(chunk + 8 + length, crc32(0, chunk + 4, length + 4));
  w->pos += (size_t)length + 12;
}

/* Points deflate's output at a new IDAT chunk of at most PNG_IDAT_BLOCK
   bytes, keeping room for IEND; 0 when not even one byte fits. */
static int png_idat_open(PngWriter *w)
{
  size_t room = w->size - w->pos;

  if(room <= 24)
    return 0;
  room -= 24;
  if(room > PNG_IDAT_BLOCK)
    room = PNG_IDAT_BLOCK;

  w->z.next_out = png_chunk_open(w, "IDAT", room);
  w->z.avail_out = (uInt)room;
  return 1;
}

static void png_idat_close(PngWriter *w)
{
  png_chunk_close(w, (uint32_t)(w->z.next_out - (w->out + w->pos + 8)));
}

/* Feeds length bytes to deflate, or finishes the stream with Z_FINISH,
   moving on to a new IDAT chunk whenever one fills up. */
static int png_deflate(PngWriter *w, uint8_t *data, uInt length, int flush)
{
  w->z.next_in = data;
  w->z.avail_in = length;

  for(;;)
  {
    int status = deflate(&w->z, flush);

    if(status == Z_STREAM_END)
    {
      png_idat_close(w);
      return PNG_WRITE_OK;
    }
    if(status != Z_OK)
      return PNG_WRITE_ERR_ZLIB;

    if(w->z.avail_out == 0)
    {
      png_idat_close(w);
      if(!png_idat_open(w))
        return PNG_WRITE_ERR_SPACE;
    }
    /* With output room left, deflate has taken all of the input. */
    if(flush == Z_NO_FLUSH && w->z.avail_in == 0)
      return PNG_WRITE_OK;
  }
}

static uint8_t png_expand5(uint32_t value)
{
  return (uint8_t)((value << 3) | (value >> 2));
}

/* Fills palette with the frame's 15-bit colours in ascending order and
   returns how many there are, or 257 as soon as there are more than 256.
   seen holds PNG_SEEN_BYTES bytes. */
static uint32_t png_collect_colours(const uint16_t *pixels, uint32_t width,
 uint32_t height, uint32_t stride, uint8_t *seen, uint16_t *palette)
{
  uint32_t count = 0;
  uint32_t x, y, key;

  memset(seen, 0, PNG_SEEN_BYTES);
  for(y = 0; y < height; y++)
  {
    const uint16_t *row = pixels + (size_t)y * stride;

    for(x = 0; x < width; x++)
    {
      key = row[x] & 0x7FFFu;
      if(!(seen[key >> 3] & (1u << (key & 7))))
      {
        if(++count > 256)
          return count;
        seen[key >> 3] |= (uint8_t)(1u << (key & 7));
      }
    }
  }

  count = 0;
  for(key = 0; key < PNG_SEEN_BYTES * 8; key++)
  {
    if(seen[key >> 3] & (1u << (key & 7)))
      palette[count++] = (uint16_t)key;
  }
  return count;
}

/* The position of key, which is present, in the ascending palette. */
static uint8_t png_palette_index(const uint16_t *palette, uint32_t count,
 uint32_t key)
{
  uint32_t low = 0;
  uint32_t high = count - 1;

  while(low < high)
  {
    uint32_t middle = (low + high) / 2;

    if((uint32_t)palette[middle] < key)
      low = middle + 1;
    else
      high = middle;
  }
  return (uint8_t)low;
}

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c)
{
  int pa = b - c;
  int pb = a - c;
  int pc = pa + pb;

  if(pa < 0)
    pa = -pa;
  if(pb < 0)
    pb = -pb;
  if(pc < 0)
    pc = -pc;

  if(pa <= pb && pa <= pc)
    return a;
  if(pb <= pc)
    return b;
  return c;
}

/* Byte x under PNG filter type (0 None, 1 Sub, 2 Up, 3 Average, 4 Paeth),
   with a the byte one pixel to its left, b the byte above and c the byte
   above a. */
static uint8_t png_filter(uint32_t type, uint8_t x, uint8_t a, uint8_t b,
 uint8_t c)
{
  switch(type)
  {
    case 1:
      return (uint8_t)(x - a);
    case 2:
      return (uint8_t)(x - b);
    case 3:
      return (uint8_t)(x - ((a + b) >> 1));
    case 4:
      return (uint8_t)(x - png_paeth(a, b, c));
  }
  return x;
}

/* Writes the type byte and the filtered bytes of an RGB row into line,
   choosing the filter whose output has the smallest sum of magnitudes
   (as signed bytes), the usual heuristic for photos and gradients. */
static void png_filter_row(const uint8_t *row, const uint8_t *above,
 uint32_t length, uint8_t *line)
{
  uint32_t best = 0;
  uint32_t best_sum = UINT32_MAX;
  uint32_t type, i;

  for(type = 0; type < 5; type++)
  {
    uint32_t sum = 0;

    for(i = 0; i < length; i++)
    {
      uint8_t a = i >= 3 ? row[i - 3] : 0;
      uint8_t c = i >= 3 ? above[i - 3] : 0;
      uint8_t v = png_filter(type, row[i], a, above[i], c);

      sum += v < 128 ? (uint32_t)v : 256u - v;
    }
    if(sum < best_sum)
    {
      best_sum = sum;
      best = type;
    }
  }

  line[0] = (uint8_t)best;
  for(i = 0; i < length; i++)
  {
    uint8_t a = i >= 3 ? row[i - 3] : 0;
    uint8_t c = i >= 3 ? above[i - 3] : 0;

    line[i + 1] = png_filter(best, row[i], a, above[i], c);
  }
}

/* Everything after the allocations: the chunks and the image data.
   scratch holds the palette, the colour bitmap, two RGB rows and the line
   handed to deflate. */
static int png_encode(PngWriter *w, const uint16_t *pixels, uint32_t width,
 uint32_t height, uint32_t stride, void *scratch)
{
  uint16_t *palette = scratch;
  uint8_t *seen = (uint8_t *)scratch + PNG_PALETTE_BYTES;
  uint8_t *above = seen + PNG_SEEN_BYTES;
  uint8_t *row = above + 3 * width;
  uint8_t *line = row + 3 * width;
  uint32_t colours =
   png_collect_colours(pixels, width, height, stride, seen, palette);
  uint32_t i, x, y;
  uint8_t *data;
  int result;

  if(w->size < 8)
    return PNG_WRITE_ERR_SPACE;
  memcpy(w->out, "\x89PNG\r\n\x1a\n", 8);
  w->pos = 8;

  data = png_chunk_open(w, "IHDR", 13);
  if(data == NULL)
    return PNG_WRITE_ERR_SPACE;
  png_put32(data, width);
  png_put32(data + 4, height);
  data[8] = 8;                      /* bits per sample or index */
  data[9] = colours <= 256 ? 3 : 2; /* indexed or RGB */
  data[10] = 0;                     /* deflate */
  data[11] = 0;                     /* adaptive filtering */
  data[12] = 0;                     /* not interlaced */
  png_chunk_close(w, 13);

  if(colours <= 256)
  {
    data = png_chunk_open(w, "PLTE", 3 * colours);
    if(data == NULL)
      return PNG_WRITE_ERR_SPACE;
    for(i = 0; i < colours; i++)
    {
      data[3 * i] = png_expand5(palette[i] & 31u);
      data[3 * i + 1] = png_expand5((palette[i] >> 5) & 31u);
      data[3 * i + 2] = png_expand5((palette[i] >> 10) & 31u);
    }
    png_chunk_close(w, 3 * colours);
  }

  if(!png_idat_open(w))
    return PNG_WRITE_ERR_SPACE;

  memset(above, 0, 3 * width);
  for(y = 0; y < height; y++)
  {
    const uint16_t *source = pixels + (size_t)y * stride;
    uInt length;

    if(colours <= 256)
    {
      /* Filters rarely help indexed rows: they stay unfiltered. */
      line[0] = 0;
      for(x = 0; x < width; x++)
        line[x + 1] = png_palette_index(palette, colours, source[x] & 0x7FFFu);
      length = width + 1;
    }
    else
    {
      uint8_t *swap;

      for(x = 0; x < width; x++)
      {
        row[3 * x] = png_expand5(source[x] & 31u);
        row[3 * x + 1] = png_expand5((source[x] >> 5) & 31u);
        row[3 * x + 2] = png_expand5((source[x] >> 10) & 31u);
      }
      png_filter_row(row, above, 3 * width, line);
      swap = above;
      above = row;
      row = swap;
      length = 3 * width + 1;
    }

    result = png_deflate(w, line, length, Z_NO_FLUSH);
    if(result != PNG_WRITE_OK)
      return result;
  }

  result = png_deflate(w, NULL, 0, Z_FINISH);
  if(result != PNG_WRITE_OK)
    return result;

  /* png_idat_open kept these 12 bytes free. */
  png_chunk_open(w, "IEND", 0);
  png_chunk_close(w, 0);
  return PNG_WRITE_OK;
}

int png_write_rgba5551(const uint16_t *pixels, uint32_t width,
 uint32_t height, uint32_t stride, uint8_t *out, size_t out_size,
 size_t *written, alloc_func zalloc, free_func zfree, void *opaque)
{
  PngWriter w;
  void *scratch;
  int result;

  if(pixels == NULL || out == NULL || written == NULL || zalloc == NULL ||
   zfree == NULL || width == 0 || height == 0 ||
   width > PNG_WRITE_MAX_SIDE || height > PNG_WRITE_MAX_SIDE ||
   stride < width)
    return PNG_WRITE_ERR_ARGUMENT;

  /* The palette, the colour bitmap, two RGB rows and the filtered line. */
  scratch = zalloc(opaque,
   PNG_PALETTE_BYTES + PNG_SEEN_BYTES + 2 * (3 * width) + 3 * width + 1, 1);
  if(scratch == NULL)
    return PNG_WRITE_ERR_MEMORY;

  memset(&w, 0, sizeof(w));
  w.z.zalloc = zalloc;
  w.z.zfree = zfree;
  w.z.opaque = opaque;
  w.out = out;
  w.size = out_size;

  result = deflateInit2(&w.z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
   PNG_WINDOW_BITS, PNG_MEM_LEVEL, Z_DEFAULT_STRATEGY);
  if(result == Z_OK)
  {
    result = png_encode(&w, pixels, width, height, stride, scratch);
    deflateEnd(&w.z);
  }
  else
    result = result == Z_MEM_ERROR ? PNG_WRITE_ERR_MEMORY : PNG_WRITE_ERR_ZLIB;

  zfree(opaque, scratch);
  if(result == PNG_WRITE_OK)
    *written = w.pos;
  return result;
}
