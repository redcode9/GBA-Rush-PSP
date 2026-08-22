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

#ifndef FAST_FORWARD_AUDIO_H
#define FAST_FORWARD_AUDIO_H

#include <stdint.h>

uint32_t fast_forward_audio_required_samples(uint32_t output_frames,
 uint32_t factor);
void fast_forward_audio_compress_ring(int16_t *ring, uint32_t ring_size,
 uint32_t *base, int16_t *output, uint32_t output_frames, uint32_t factor,
 int audible);

#endif /* FAST_FORWARD_AUDIO_H */
