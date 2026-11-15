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

#include "zip_rom.h"

#include <string.h>

#include "zlib/zlib.h"

/* Record layouts from PKWARE APPNOTE.TXT, all little-endian. The local file
 * header (4.3.7) is 30 bytes:
 *   0 signature  6 flags  8 method  14 CRC-32  18 compressed size
 *   22 uncompressed size  26 name length  28 extra field length
 * then the name, the extra field and the entry's data. */
#define LOCAL_HEADER_SIZE 30u
#define LOCAL_SIGNATURE 0x04034B50u
#define DESCRIPTOR_SIGNATURE 0x08074B50u
#define FLAG_ENCRYPTED 0x0001u
#define FLAG_DESCRIPTOR 0x0008u
#define METHOD_STORED 0u
#define METHOD_DEFLATED 8u
/* The old loader's limits: a name that fits a 256-byte path, and an extra
   field length that is positive as a signed 16-bit value. */
#define NAME_MAX_LENGTH 255u
#define EXTRA_MAX_LENGTH 0x7FFFu

/* Buffered reader over io->in_buf. */
typedef struct
{
  const ZipRomIo *io;
  uint32_t pos;   /* next unread byte of in_buf */
  uint32_t avail; /* bytes really read into in_buf */
  uint32_t done;  /* archive bytes read so far */
  uint32_t reported; /* done of the last progress call */
} ZipReader;

static uint16_t get16(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
   ((uint32_t)p[3] << 24);
}

/* Calls progress only when done grew: the popup takes a smaller or equal
   done for a new load. */
static void report(ZipReader *reader, uint32_t done)
{
  const ZipRomIo *io = reader->io;

  if(io->progress != NULL && done > reader->reported)
  {
    reader->reported = done;
    io->progress(io->ctx, done, io->total);
  }
}

/* One read of up to len bytes into buf; *got is never 0 on ZIP_ROM_OK. */
static int reader_read(ZipReader *reader, uint8_t *buf, uint32_t len,
 uint32_t *got)
{
  const ZipRomIo *io = reader->io;
  int32_t count = io->read(io->ctx, buf, len);

  if(count < 0 || (uint32_t)count > len)
    return ZIP_ROM_IO_ERROR;
  if(count == 0)
    return ZIP_ROM_TRUNCATED;
  *got = (uint32_t)count;
  reader->done += *got;
  return ZIP_ROM_OK;
}

static int reader_fill(ZipReader *reader)
{
  int result = reader_read(reader, reader->io->in_buf, reader->io->in_cap,
   &reader->avail);

  if(result == ZIP_ROM_OK)
    reader->pos = 0;
  return result;
}

/* Copies the next n archive bytes to dst, or skips them when dst is NULL. */
static int reader_take(ZipReader *reader, uint8_t *dst, uint32_t n)
{
  while(n > 0)
  {
    uint32_t chunk;

    if(reader->pos == reader->avail)
    {
      int result = reader_fill(reader);

      if(result != ZIP_ROM_OK)
        return result;
    }
    chunk = reader->avail - reader->pos;
    if(chunk > n)
      chunk = n;
    if(dst != NULL)
    {
      memcpy(dst, reader->io->in_buf + reader->pos, chunk);
      dst += chunk;
    }
    reader->pos += chunk;
    n -= chunk;
  }
  return ZIP_ROM_OK;
}

/* ASCII case-insensitive equality with a lowercase word; strcasecmp is not
   C99. */
static int same_word(const char *text, const char *lower)
{
  for(; *text != '\0' && *lower != '\0'; text++, lower++)
  {
    char c = *text;

    if(c >= 'A' && c <= 'Z')
      c = (char)(c - 'A' + 'a');
    if(c != *lower)
      return 0;
  }
  return *text == *lower;
}

static int is_rom_name(const char *name)
{
  const char *ext = strrchr(name, '.');

  if(ext == NULL)
    return 0;
  ext++;
  return same_word(ext, "gba") || same_word(ext, "agb") ||
   same_word(ext, "bin");
}

/* Copies a stored entry straight into out, size bytes. */
static int load_stored(ZipReader *reader, uint8_t *out, uint32_t size,
 uint32_t *crc_out)
{
  uint32_t done = reader->avail - reader->pos;
  uLong crc;

  if(done > size)
    done = size;
  memcpy(out, reader->io->in_buf + reader->pos, done);
  reader->pos += done;
  crc = crc32(0L, out, done);
  while(done < size)
  {
    uint32_t want = size - done;
    uint32_t got;
    int result;

    if(want > reader->io->in_cap)
      want = reader->io->in_cap;
    result = reader_read(reader, out + done, want, &got);
    if(result != ZIP_ROM_OK)
      return result;
    crc = crc32(crc, out + done, got);
    done += got;
    report(reader, reader->done);
  }
  *crc_out = (uint32_t)crc;
  return ZIP_ROM_OK;
}

/* Inflates a raw deflate stream into out, at most out_cap bytes, one
   ZIP_ROM_INFLATE_STEP at a time, reporting the archive bytes used after
   each inflate call. On return the input zlib did not use is the reader's
   again. */
static int load_deflated(ZipReader *reader, uint8_t *out, uint32_t out_cap,
 uint32_t *crc_out, uint32_t *size_out)
{
  z_stream stream;
  uLong crc = crc32(0L, Z_NULL, 0);
  int result = ZIP_ROM_OK;

  memset(&stream, 0, sizeof(stream)); /* Z_NULL: zlib's own allocators */
  if(inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    return ZIP_ROM_CORRUPT;
  stream.next_in = reader->io->in_buf + reader->pos;
  stream.avail_in = reader->avail - reader->pos;
  stream.next_out = out;
  stream.avail_out = out_cap < ZIP_ROM_INFLATE_STEP ? out_cap :
   ZIP_ROM_INFLATE_STEP;
  for(;;)
  {
    int step_full = stream.avail_out == 0 && stream.total_out < out_cap;
    uInt before;
    uInt produced;
    int err;

    if(step_full)
    {
      uint32_t left = out_cap - (uint32_t)stream.total_out;

      stream.avail_out = left < ZIP_ROM_INFLATE_STEP ? left :
       ZIP_ROM_INFLATE_STEP;
    }
    /* After a full step zlib may still hold the rest of a match, or the end
       of the stream, in its bit buffer: inflate once before reading more. */
    else if(stream.avail_in == 0)
    {
      result = reader_fill(reader);
      if(result != ZIP_ROM_OK)
        break;
      stream.next_in = reader->io->in_buf;
      stream.avail_in = reader->avail;
    }
    before = stream.avail_out;
    err = inflate(&stream, Z_NO_FLUSH);
    produced = before - stream.avail_out;
    crc = crc32(crc, stream.next_out - produced, produced);
    report(reader, reader->done - stream.avail_in);
    if(err == Z_STREAM_END)
      break;
    /* Z_BUF_ERROR with room left means all input is used: refill. */
    if(err == Z_BUF_ERROR && stream.avail_out == 0)
      result = ZIP_ROM_TOO_BIG;
    else if(err != Z_OK && err != Z_BUF_ERROR)
      result = ZIP_ROM_CORRUPT;
    if(result != ZIP_ROM_OK)
      break;
  }
  reader->pos = reader->avail - stream.avail_in;
  *crc_out = (uint32_t)crc;
  *size_out = (uint32_t)stream.total_out;
  inflateEnd(&stream);
  return result;
}

/* Data descriptor after the data (APPNOTE 4.3.9): an optional signature,
   then CRC-32, compressed size and uncompressed size.
   shortcut: an unsigned descriptor whose CRC equals the signature, or a
   ZIP64 one with 8-byte sizes, ends as CORRUPT; handle them if a real ROM
   zip turns out to use them. */
static int read_descriptor(ZipReader *reader, uint32_t *crc, uint32_t *size)
{
  uint8_t field[12];
  int result = reader_take(reader, field, 4);

  if(result == ZIP_ROM_OK && get32(field) == DESCRIPTOR_SIGNATURE)
    result = reader_take(reader, field, 4);
  if(result == ZIP_ROM_OK)
    result = reader_take(reader, field + 4, 8);
  if(result == ZIP_ROM_OK)
  {
    *crc = get32(field);
    *size = get32(field + 8);
  }
  return result;
}

int zip_rom_load(const ZipRomIo *io, uint8_t *out, uint32_t out_cap,
 uint32_t *size)
{
  ZipReader reader = { io, 0, 0, 0, 0 };
  uint8_t header[LOCAL_HEADER_SIZE];
  char name[NAME_MAX_LENGTH + 1];
  uint32_t flags, method, name_length, extra_length;
  uint32_t expected_crc, expected_size, crc, written;
  int result = reader_take(&reader, header, LOCAL_HEADER_SIZE);

  if(result != ZIP_ROM_OK)
    return result;
  if(get32(header) != LOCAL_SIGNATURE)
    return ZIP_ROM_CORRUPT;
  flags = get16(header + 6);
  method = get16(header + 8);
  expected_crc = get32(header + 14);
  expected_size = get32(header + 22);
  name_length = get16(header + 26);
  extra_length = get16(header + 28);
  if(name_length > NAME_MAX_LENGTH || extra_length > EXTRA_MAX_LENGTH)
    return ZIP_ROM_CORRUPT;
  if((flags & FLAG_ENCRYPTED) != 0 ||
   (method != METHOD_STORED && method != METHOD_DEFLATED))
    return ZIP_ROM_UNSUPPORTED;
  if((flags & FLAG_DESCRIPTOR) == 0 && expected_size > out_cap)
    return ZIP_ROM_TOO_BIG;

  result = reader_take(&reader, (uint8_t *)name, name_length);
  if(result == ZIP_ROM_OK)
    result = reader_take(&reader, NULL, extra_length);
  if(result != ZIP_ROM_OK)
    return result;
  name[name_length] = '\0';
  if(!is_rom_name(name))
    return ZIP_ROM_UNSUPPORTED;

  if(method == METHOD_STORED)
  {
    /* Without a size in the header a stored entry's end cannot be found:
       its descriptor follows data that may contain anything. */
    if((flags & FLAG_DESCRIPTOR) != 0)
      return ZIP_ROM_UNSUPPORTED;
    written = expected_size;
    result = load_stored(&reader, out, expected_size, &crc);
  }
  else
  {
    result = load_deflated(&reader, out, out_cap, &crc, &written);
    if(result == ZIP_ROM_OK && (flags & FLAG_DESCRIPTOR) != 0)
      result = read_descriptor(&reader, &expected_crc, &expected_size);
  }
  if(result != ZIP_ROM_OK)
    return result;
  if(crc != expected_crc || written != expected_size)
    return ZIP_ROM_CORRUPT;
  *size = written;
  return ZIP_ROM_OK;
}
