#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fast_forward_audio.h"

static void test_required_samples(void)
{
  assert(fast_forward_audio_required_samples(800, 1) == 1600);
  assert(fast_forward_audio_required_samples(800, 2) == 3200);
  assert(fast_forward_audio_required_samples(800, 3) == 4800);
  assert(fast_forward_audio_required_samples(800, 0) == 1600);
}

static void test_stereo_groups_are_averaged(void)
{
  int16_t ring[] = { 10, 100, 30, 300, -20, -200, -40, -400 };
  int16_t output[4] = { 0 };
  uint32_t base = 0;

  fast_forward_audio_compress_ring(ring, 8, &base, output, 2, 2, 1);
  assert(output[0] == 20 * 16);
  assert(output[1] == 200 * 16);
  assert(output[2] == -30 * 16);
  assert(output[3] == -300 * 16);
  assert(base == 0);
  assert(memcmp(ring, (int16_t[8]){ 0 }, sizeof(ring)) == 0);
}

static void test_ring_wrap(void)
{
  int16_t ring[] = { 30, 300, 0, 0, 10, 100, 20, 200 };
  int16_t output[2] = { 0 };
  uint32_t base = 4;

  fast_forward_audio_compress_ring(ring, 8, &base, output, 1, 3, 1);
  assert(output[0] == 20 * 16);
  assert(output[1] == 200 * 16);
  assert(base == 2);
}

static void test_clipping_and_muting(void)
{
  int16_t ring[] = { 3000, -3000, 3000, -3000 };
  int16_t output[2] = { 0 };
  uint32_t base = 0;

  fast_forward_audio_compress_ring(ring, 4, &base, output, 1, 2, 1);
  assert(output[0] == 32752);
  assert(output[1] == -32768);

  ring[0] = 100;
  ring[1] = -100;
  base = 0;
  fast_forward_audio_compress_ring(ring, 4, &base, output, 1, 1, 0);
  assert(output[0] == 0 && output[1] == 0);
  assert(ring[0] == 0 && ring[1] == 0);
}

int main(void)
{
  test_required_samples();
  test_stereo_groups_are_averaged();
  test_ring_wrap();
  test_clipping_and_muting();
  puts("Fast-forward audio tests passed");
  return 0;
}
