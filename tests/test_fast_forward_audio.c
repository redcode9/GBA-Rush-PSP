/*
 * Host tests for fast_forward_audio.c, which squeezes 1-3 emulated stereo
 * frames into each output frame while fast-forwarding. The input is the
 * sound ring of interleaved left/right int32 samples; consumed slots are
 * cleared and the read position (base) advances with wrap-around.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fast_forward_audio.h"

/* Samples = frames x 2 channels x factor; an invalid factor counts as 1. */
static void test_required_samples(void)
{
  assert(fast_forward_audio_required_samples(800, 1) == 1600);
  assert(fast_forward_audio_required_samples(800, 2) == 3200);
  assert(fast_forward_audio_required_samples(800, 3) == 4800);
  assert(fast_forward_audio_required_samples(800, 0) == 1600);
}

static void test_stereo_groups_are_averaged(void)
{
  int32_t ring[] = { 160, 1600, 480, 4800, -320, -3200, -640, -6400 };
  int16_t output[4] = { 0 };
  uint32_t base = 0;

  fast_forward_audio_compress_ring(ring, 8, &base, output, 2, 2, 1);
  assert(output[0] == 320);
  assert(output[1] == 3200);
  assert(output[2] == -480);
  assert(output[3] == -4800);
  assert(base == 0);
  assert(memcmp(ring, (int32_t[8]){ 0 }, sizeof(ring)) == 0);
}

/* Reading three frames from slot 4 of an 8-slot ring wraps to slot 0. */
static void test_ring_wrap(void)
{
  int32_t ring[] = { 480, 4800, 0, 0, 160, 1600, 320, 3200 };
  int16_t output[2] = { 0 };
  uint32_t base = 4;

  fast_forward_audio_compress_ring(ring, 8, &base, output, 1, 3, 1);
  assert(output[0] == 320);
  assert(output[1] == 3200);
  assert(base == 2);
}

static void test_clipping_and_muting(void)
{
  int32_t ring[] = { 40000, -40000, 40000, -40000 };
  int16_t output[2] = { 0 };
  uint32_t base = 0;

  fast_forward_audio_compress_ring(ring, 4, &base, output, 1, 2, 1);
  assert(output[0] == 32767);
  assert(output[1] == -32768);

  /* Not audible: output is silence, but the ring is still consumed. */
  ring[0] = 100;
  ring[1] = -100;
  base = 0;
  fast_forward_audio_compress_ring(ring, 4, &base, output, 1, 1, 0);
  assert(output[0] == 0 && output[1] == 0);
  assert(ring[0] == 0 && ring[1] == 0);
}

/* Averages round half away from zero, so left and right mirror exactly. */
static void test_average_rounds_without_sign_bias(void)
{
  int32_t ring[] = { 1, -1, 2, -2, 2, -2, 3, -3, 3, -3, 3, -3 };
  int16_t output[4] = { 0 };
  uint32_t base = 0;

  fast_forward_audio_compress_ring(ring, 12, &base, output, 2, 3, 1);
  assert(output[0] == 2);
  assert(output[1] == -2);
  assert(output[2] == 3);
  assert(output[3] == -3);
}

int main(void)
{
  test_required_samples();
  test_stereo_groups_are_averaged();
  test_ring_wrap();
  test_clipping_and_muting();
  test_average_rounds_without_sign_bias();
  puts("Fast-forward audio tests passed");
  return 0;
}
