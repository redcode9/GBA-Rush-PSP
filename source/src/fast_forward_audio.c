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

#include "fast_forward_audio.h"

#include <stddef.h>

/* Factors outside 1..3 fall back to normal speed. */
static uint32_t valid_factor(uint32_t factor)
{
  return factor >= 1 && factor <= 3 ? factor : 1;
}

uint32_t fast_forward_audio_required_samples(uint32_t output_frames,
 uint32_t factor)
{
  return output_frames * 2U * valid_factor(factor);
}

/* Average of factor summed samples, rounded half away from zero and
 * clamped to s16. */
static int16_t output_sample(int64_t sum, uint32_t factor, int audible)
{
  int64_t sample;

  if(!audible)
    return 0;

  if(sum >= 0)
    sample = (sum + factor / 2U) / (int64_t)factor;
  else
    sample = (sum - factor / 2U) / (int64_t)factor;
  if(sample > 32767)
    sample = 32767;
  else if(sample < -32768)
    sample = -32768;
  return (int16_t)sample;
}

void fast_forward_audio_compress_ring(int32_t *ring, uint32_t ring_size,
 uint32_t *base, int16_t *output, uint32_t output_frames, uint32_t factor,
 int audible)
{
  uint32_t position;
  uint32_t frame;

  if(ring == NULL || base == NULL || output == NULL || ring_size < 2)
    return;

  factor = valid_factor(factor);
  position = *base % ring_size;
  for(frame = 0; frame < output_frames; frame++)
  {
    int64_t left = 0;
    int64_t right = 0;
    uint32_t input_frame;

    for(input_frame = 0; input_frame < factor; input_frame++)
    {
      left += ring[position];
      ring[position] = 0;
      position = (position + 1) % ring_size;
      right += ring[position];
      ring[position] = 0;
      position = (position + 1) % ring_size;
    }

    output[frame * 2] = output_sample(left, factor, audible);
    output[frame * 2 + 1] = output_sample(right, factor, audible);
  }
  *base = position;
}
