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
 * PNG encoder for screenshots, kept free of PSP headers so
 * tests/test_png_write.c can decode its output on the host. It writes into
 * the caller's buffer and takes every byte of working memory (zlib's and
 * its own) from the caller's allocator, so nothing stays reserved between
 * shots.
 */

#ifndef PNG_WRITE_H
#define PNG_WRITE_H

#include <stddef.h>
#include <stdint.h>

#include "zlib/zlib.h"

/* Widest and tallest image accepted. */
#define PNG_WRITE_MAX_SIDE 4096

enum
{
  PNG_WRITE_OK = 0,
  PNG_WRITE_ERR_ARGUMENT = -1, /* NULL pointer, zero or oversized side */
  PNG_WRITE_ERR_MEMORY = -2,   /* the allocator returned NULL */
  PNG_WRITE_ERR_SPACE = -3,    /* the PNG does not fit in out_size bytes */
  PNG_WRITE_ERR_ZLIB = -4
};

/* Encodes width x height pixels in GU_PSM_5551 order (red in bits 0-4,
 * green in 5-9, blue in 10-14; bit 15 is ignored), rows stride pixels
 * apart, as a PNG in out. Frames with at most 256 colours become 8-bit
 * indexed PNGs, others 24-bit RGB with a filter chosen per row. Returns
 * PNG_WRITE_OK and the PNG's size in *written, or a PNG_WRITE_ERR_* code;
 * nothing is written at or past out + out_size either way. zalloc and
 * zfree have zlib's signature and serve every allocation, under 80 KiB
 * at once for a 240x160 frame. */
int png_write_rgba5551(const uint16_t *pixels, uint32_t width,
 uint32_t height, uint32_t stride, uint8_t *out, size_t out_size,
 size_t *written, alloc_func zalloc, free_func zfree, void *opaque);

#endif /* PNG_WRITE_H */
