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

#ifndef SOUND_MATH_H
#define SOUND_MATH_H

#include <stdint.h>

uint32_t sound_math_tone_step(uint32_t rate, uint32_t sample_rate);
uint32_t sound_math_wave_step(uint32_t rate, uint32_t sample_rate);
uint32_t sound_math_noise_step(uint32_t ratio, uint32_t shift,
 uint32_t sample_rate);
uint32_t sound_math_timer_step(uint32_t reload, uint32_t sample_rate);
uint32_t sound_math_tick_step(uint32_t sample_rate);
uint32_t sound_math_retrigger_offset(void);

static inline int32_t sound_math_interpolate(int32_t current,
 int32_t next, uint32_t fraction)
{
  int64_t scaled = (int64_t)(next - current) * (fraction & 0x00FFFFFFU);

  return current + (int32_t)((scaled + (1 << 23)) >> 24);
}

static inline int32_t sound_math_scale_psg(int32_t sample, int32_t volume)
{
  return (sample * volume + (1 << 17)) >> 18;
}

#endif /* SOUND_MATH_H */
