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

#include <stddef.h>

#include "art_filter.h"

void art_filter_box_shrink(const uint16_t *src, uint32_t src_w,
 uint32_t src_h, uint32_t src_pitch, uint16_t *dst, uint32_t dst_w,
 uint32_t dst_h, uint32_t dst_pitch)
{
  uint32_t x, y;

  /* 8192 keeps every product and channel sum below 2^32. */
  if(src == NULL || dst == NULL || dst_w == 0 || dst_h == 0 ||
     dst_w > src_w || dst_h > src_h || src_w > 8192 || src_h > 8192 ||
     src_pitch < src_w || dst_pitch < dst_w)
    return;

  for(y = 0; y < dst_h; y++)
  {
    uint32_t y0 = y * src_h / dst_h;
    uint32_t y1 = (y + 1) * src_h / dst_h;

    for(x = 0; x < dst_w; x++)
    {
      uint32_t x0 = x * src_w / dst_w;
      uint32_t x1 = (x + 1) * src_w / dst_w;
      uint32_t count = (x1 - x0) * (y1 - y0);
      uint32_t red = 0, green = 0, blue = 0, alpha = 0;
      uint32_t sx, sy;

      for(sy = y0; sy < y1; sy++)
      {
        const uint16_t *row = src + (size_t)sy * src_pitch;

        for(sx = x0; sx < x1; sx++)
        {
          uint32_t pixel = row[sx];

          red += pixel & 0x1F;
          green += (pixel >> 5) & 0x1F;
          blue += (pixel >> 10) & 0x1F;
          alpha += pixel >> 15;
        }
      }

      dst[(size_t)y * dst_pitch + x] = (uint16_t)(
       ((red + count / 2) / count) |
       (((green + count / 2) / count) << 5) |
       (((blue + count / 2) / count) << 10) |
       ((alpha * 2 >= count) ? 0x8000u : 0u));
    }
  }
}
