/*
 * Host tests for sound_math.c, the integer math of the GBA sound code:
 * per-output-sample phase steps in 8.24 fixed point for the PSG channels,
 * the timers and the 256 Hz sequencer, plus full-scale mixing helpers.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "sound_math.h"

#define SAMPLE_RATE 48000U
#define FP08_24_SCALE 16777216.0   /* 1.0 in 8.24 fixed point */

/* The floating-point step: events per second / output rate, in 8.24. */
static uint32_t reference_step(double numerator, uint32_t denominator)
{
  double result = (numerator / SAMPLE_RATE / denominator) * FP08_24_SCALE;

  return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

/* Every valid input must give exactly the step the former floating-point
   code computed (reference_step()). */
static void test_integer_steps_match_previous_timing(void)
{
  uint32_t rate;
  uint32_t ratio;
  uint32_t shift;
  uint32_t reload;

  /* Event rates per second: square duty steps 1048576 / (2048 - rate),
     wave samples 2097152 / (2048 - rate), noise 524288 / r / 2^(s + 1)
     with r = 0 counting as 0.5, timer overflows 2^24 / reload. */
  for(rate = 0; rate < 2048; rate++)
  {
    assert(sound_math_tone_step(rate, SAMPLE_RATE) ==
     reference_step(1048576.0, 2048 - rate));
    assert(sound_math_wave_step(rate, SAMPLE_RATE) ==
     reference_step(2097152.0, 2048 - rate));
  }

  for(ratio = 0; ratio < 8; ratio++)
  {
    for(shift = 0; shift < 16; shift++)
    {
      uint32_t denominator = ratio == 0 ? 2U << shift
       : ratio << (shift + 1);
      double numerator = ratio == 0 ? 1048576.0 : 524288.0;

      assert(sound_math_noise_step(ratio, shift, SAMPLE_RATE) ==
       reference_step(numerator, denominator));
    }
  }

  for(reload = 1; reload <= 65536; reload++)
  {
    uint32_t actual = sound_math_timer_step(reload, SAMPLE_RATE);
    uint32_t expected = reference_step(16777216.0, reload);
    assert(actual == expected);
  }

  assert(sound_math_tick_step(SAMPLE_RATE) ==
   reference_step(256.0, 1));
  assert(sound_math_retrigger_offset() ==
   (uint32_t)((1.0 / 12.0) * FP08_24_SCALE));
}

static void test_invalid_steps_are_rejected(void)
{
  assert(sound_math_tone_step(2048, SAMPLE_RATE) == 0);
  assert(sound_math_wave_step(2048, SAMPLE_RATE) == 0);
  assert(sound_math_noise_step(8, 0, SAMPLE_RATE) == 0);
  assert(sound_math_noise_step(1, 16, SAMPLE_RATE) == 0);
  assert(sound_math_timer_step(0, SAMPLE_RATE) == 0);
  assert(sound_math_tick_step(0) == 0);
}

static void test_full_scale_mixing(void)
{
  assert(sound_math_interpolate(-32768, 32512, 0) == -32768);
  assert(sound_math_interpolate(-32768, 32512, 0x00FFFFFF) == 32512);
  assert(sound_math_interpolate(0, 256, 0x00800000) == 128);

  assert(sound_math_scale_psg(-8, 1 << 28) == -8192);
  assert(sound_math_scale_psg(7, 1 << 28) == 7168);
  assert(sound_math_scale_psg(1, 1 << 17) == 1);
}

int main(void)
{
  test_integer_steps_match_previous_timing();
  test_invalid_steps_are_rejected();
  test_full_scale_mixing();
  puts("Sound math tests passed");
  return 0;
}
