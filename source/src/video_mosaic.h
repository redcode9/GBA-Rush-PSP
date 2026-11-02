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

#ifndef VIDEO_MOSAIC_H
#define VIDEO_MOSAIC_H

/* Horizontal mosaic sampler for video_cc.cc. take() returns 1 on the first
 * pixel of every `width`-pixel block, meaning "fetch a new source pixel";
 * otherwise the renderer repeats the last one. A NULL counter always takes.
 * Host tested in tests/test_video_mosaic.c.
 */

#include <stdint.h>

typedef struct
{
  uint32_t remaining;
} VideoMosaicCounter;

/* A zeroed counter takes on its first call; width 0 counts as 1. */
static inline int video_mosaic_counter_take(VideoMosaicCounter *counter,
 uint32_t width)
{
  int take;

  if(counter == 0)
    return 1;
  if(width == 0)
    width = 1;

  take = counter->remaining == 0;
  if(take)
    counter->remaining = width;
  counter->remaining--;
  return take;
}

#endif /* VIDEO_MOSAIC_H */
