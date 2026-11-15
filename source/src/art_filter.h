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

#ifndef ART_FILTER_H
#define ART_FILTER_H

#include <stdint.h>

/* Shrinks a 5551 image (red in the low bits, bit 15 the alpha bit) to
   dst_w x dst_h: each destination pixel averages, with rounding, each 5-bit
   channel over its source box [x * src_w / dst_w, (x + 1) * src_w / dst_w)
   by the same rows, and keeps bit 15 when at least half the box has it.
   Pitches are in pixels. Does nothing for a NULL image, a zero size, a
   pitch below its width, a source side above 8192 or a destination larger
   than the source in either direction. */
void art_filter_box_shrink(const uint16_t *src, uint32_t src_w,
 uint32_t src_h, uint32_t src_pitch, uint16_t *dst, uint32_t dst_w,
 uint32_t dst_h, uint32_t dst_pitch);

#endif /* ART_FILTER_H */
