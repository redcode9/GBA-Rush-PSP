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
 * 8.24 fixed-point phase steps and mixing helpers for sound.c. Kept free of
 * PSP headers so tests/test_sound_math.c can check them on the host.
 */

#ifndef SOUND_MATH_H
#define SOUND_MATH_H

#include <stdint.h>

/* Each *_step returns how far a channel's 8.24 phase advances per output
 * sample at sample_rate, or 0 for an out-of-range register value. */
uint32_t sound_math_tone_step(uint32_t rate, uint32_t sample_rate);
uint32_t sound_math_wave_step(uint32_t rate, uint32_t sample_rate);
uint32_t sound_math_noise_step(uint32_t ratio, uint32_t shift,
 uint32_t sample_rate);
uint32_t sound_math_timer_step(uint32_t reload, uint32_t sample_rate);
uint32_t sound_math_tick_step(uint32_t sample_rate);
uint32_t sound_math_retrigger_offset(void);

/* Linear interpolation between two FIFO samples by the 24-bit fraction of
 * an 8.24 phase, rounded to nearest. */
static inline int32_t sound_math_interpolate(int32_t current,
 int32_t next, uint32_t fraction)
{
  int64_t scaled = (int64_t)(next - current) * (fraction & 0x00FFFFFFU);

  return current + (int32_t)((scaled + (1 << 23)) >> 24);
}

/* Scales a 4-bit PSG sample (-8..7) by a combined envelope * channel *
 * master volume (at most 1 << 28) onto the 16-bit mix bus, rounded. */
static inline int32_t sound_math_scale_psg(int32_t sample, int32_t volume)
{
  return (sample * volume + (1 << 17)) >> 18;
}

#endif /* SOUND_MATH_H */
