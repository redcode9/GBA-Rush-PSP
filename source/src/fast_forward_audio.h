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
 * Fast-forward audio: sound.c drains its stereo s32 mix ring through here,
 * averaging 1-3 emulated frames into each output frame so fast-forward
 * plays at normal pitch. Host-tested by tests/test_fast_forward_audio.c.
 */

#ifndef FAST_FORWARD_AUDIO_H
#define FAST_FORWARD_AUDIO_H

#include <stdint.h>

/* Ring entries (interleaved L/R) consumed to produce output_frames. */
uint32_t fast_forward_audio_required_samples(uint32_t output_frames,
 uint32_t factor);
/* Writes output_frames stereo s16 frames from the ring starting at *base,
 * zeroes the consumed entries and advances *base. With audible == 0 the
 * ring is still consumed but silence is written. */
void fast_forward_audio_compress_ring(int32_t *ring, uint32_t ring_size,
 uint32_t *base, int16_t *output, uint32_t output_frames, uint32_t factor,
 int audible);

#endif /* FAST_FORWARD_AUDIO_H */
