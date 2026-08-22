#include <assert.h>
#include <stdio.h>

#include "video_mosaic.h"

static void test_width_one_samples_every_pixel(void)
{
  VideoMosaicCounter counter = { 0 };
  int i;

  for(i = 0; i < 16; i++)
    assert(video_mosaic_counter_take(&counter, 1));
}

static void test_sampling_sequence(void)
{
  VideoMosaicCounter counter = { 0 };
  const int expected[] = { 1, 0, 0, 1, 0, 0, 1, 0 };
  size_t i;

  for(i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
    assert(video_mosaic_counter_take(&counter, 3) == expected[i]);
}

static void test_independent_scanlines_restart_at_first_pixel(void)
{
  VideoMosaicCounter first = { 0 };
  VideoMosaicCounter second = { 0 };

  assert(video_mosaic_counter_take(&first, 4));
  assert(!video_mosaic_counter_take(&first, 4));
  assert(video_mosaic_counter_take(&second, 4));
}

int main(void)
{
  test_width_one_samples_every_pixel();
  test_sampling_sequence();
  test_independent_scanlines_restart_at_first_pixel();
  puts("Video mosaic tests passed");
  return 0;
}
