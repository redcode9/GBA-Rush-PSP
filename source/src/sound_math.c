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

#include "sound_math.h"

#define FP08_24_SCALE (1ULL << 24)

/* numerator / (denominator * sample_rate) in 8.24, saturating at UINT32_MAX.
 * The numerators below are per-second step counts derived from the GBA
 * clock, so a channel stays in tune at any output rate. */
static uint32_t fixed_step(uint32_t numerator, uint32_t denominator,
 uint32_t sample_rate)
{
  uint64_t divisor;
  uint64_t result;

  if(denominator == 0 || sample_rate == 0)
    return 0;

  divisor = (uint64_t)denominator * sample_rate;
  result = ((uint64_t)numerator * FP08_24_SCALE) / divisor;
  return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

/* Square channels play 131072 / (2048 - rate) Hz through an 8-step duty
 * pattern: 131072 * 8 = 1048576 steps per second. */
uint32_t sound_math_tone_step(uint32_t rate, uint32_t sample_rate)
{
  if(rate >= 2048)
    return 0;
  return fixed_step(1048576, 2048 - rate, sample_rate);
}

/* The wave channel reads a 4-bit sample at 2097152 / (2048 - rate) Hz. */
uint32_t sound_math_wave_step(uint32_t rate, uint32_t sample_rate)
{
  if(rate >= 2048)
    return 0;
  return fixed_step(2097152, 2048 - rate, sample_rate);
}

/* SOUND4CNT_H: the LFSR clocks at 524288 / ratio / 2^(shift + 1) Hz, with
 * ratio 0 counting as 0.5. */
uint32_t sound_math_noise_step(uint32_t ratio, uint32_t shift,
 uint32_t sample_rate)
{
  if(ratio > 7 || shift > 15)
    return 0;
  if(ratio == 0)
    return fixed_step(1048576, 2U << shift, sample_rate);
  return fixed_step(524288, ratio << (shift + 1), sample_rate);
}

/* A Direct Sound FIFO pops one sample per timer overflow: the 16.78 MHz
 * CPU clock divided by the timer period in cycles. */
uint32_t sound_math_timer_step(uint32_t reload, uint32_t sample_rate)
{
  return fixed_step(16777216, reload, sample_rate);
}

/* 256 Hz sequencer that clocks PSG length, envelope and sweep. */
uint32_t sound_math_tick_step(uint32_t sample_rate)
{
  return fixed_step(256, 1, sample_rate);
}

/* Phase rewind of 1/12 sample applied when a square channel restarts,
 * carried over from the earlier floating-point code. */
uint32_t sound_math_retrigger_offset(void)
{
  return (uint32_t)(FP08_24_SCALE / 12);
}
