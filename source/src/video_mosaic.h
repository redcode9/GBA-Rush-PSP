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

#include <stdint.h>

typedef struct
{
  uint32_t remaining;
} VideoMosaicCounter;

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
