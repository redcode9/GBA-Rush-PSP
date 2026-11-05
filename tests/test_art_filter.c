/*
 * Host tests for art_filter_box_shrink(), which shrinks a 240x160 savestate
 * preview to a 60x40 slot thumbnail (and box art to its panel size): known
 * patterns give exact averages, each 5-bit channel and the alpha bit stay
 * apart, and guard words around exact-size buffers show that it reads only
 * inside the source and writes only inside the destination.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "art_filter.h"

#define SRC_W 240
#define SRC_H 160
#define DST_W 60
#define DST_H 40
/* More than one source row, so a box one row too low lands in the guard. */
#define GUARD 256

#define RGB5(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

static uint16_t src_buffer[GUARD + SRC_W * SRC_H + GUARD];
static uint16_t dst_buffer[GUARD + 141 * 141 + GUARD];

static uint16_t *const src = src_buffer + GUARD;
static uint16_t *const dst = dst_buffer + GUARD;

static void fill(uint16_t *buffer, uint32_t count, uint16_t value)
{
  uint32_t i;

  for(i = 0; i < count; i++)
    buffer[i] = value;
}

static void test_uniform_color_survives(void)
{
  const uint16_t color = (uint16_t)(0x8000u | RGB5(25, 19, 7));
  uint32_t i;

  fill(src, SRC_W * SRC_H, color);
  fill(dst, DST_W * DST_H, 0);
  art_filter_box_shrink(src, SRC_W, SRC_H, SRC_W, dst, DST_W, DST_H, DST_W);
  for(i = 0; i < DST_W * DST_H; i++)
    assert(dst[i] == color);
}

/* Each 4x4 source block has its own color, so each thumbnail pixel must
   come from exactly one block; odd blocks also carry the alpha bit. */
static uint16_t block_color(uint32_t block)
{
  return (uint16_t)(block | ((block & 1) << 15));
}

static void test_blocks_map_one_to_one(void)
{
  uint32_t x, y;

  for(y = 0; y < SRC_H; y++)
    for(x = 0; x < SRC_W; x++)
      src[y * SRC_W + x] = block_color((y / 4) * DST_W + x / 4);
  fill(dst, DST_W * DST_H, 0);
  art_filter_box_shrink(src, SRC_W, SRC_H, SRC_W, dst, DST_W, DST_H, DST_W);
  for(y = 0; y < DST_H; y++)
    for(x = 0; x < DST_W; x++)
      assert(dst[y * DST_W + x] == block_color(y * DST_W + x));
}

/* 8 white pixels in each 16-pixel box: 31 * 8 / 16 = 15.5 rounds to 16. */
static void test_checkerboard_gives_mid_gray(void)
{
  uint32_t x, y, i;

  for(y = 0; y < SRC_H; y++)
    for(x = 0; x < SRC_W; x++)
      src[y * SRC_W + x] = ((x + y) & 1) ? RGB5(31, 31, 31) : 0;
  fill(dst, DST_W * DST_H, 0xFFFF);
  art_filter_box_shrink(src, SRC_W, SRC_H, SRC_W, dst, DST_W, DST_H, DST_W);
  for(i = 0; i < DST_W * DST_H; i++)
    assert(dst[i] == RGB5(16, 16, 16));
}

static void test_channels_never_carry(void)
{
  const uint16_t one_each[4] = { RGB5(31, 0, 0), RGB5(0, 31, 0),
   RGB5(0, 0, 31), 0x8000 };
  const uint16_t full[4] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
  const uint16_t white[4] = { 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF };
  uint16_t out = 0;

  /* (31 + 2) / 4 = 8 in every channel; one alpha bit in four is dropped. */
  art_filter_box_shrink(one_each, 2, 2, 2, &out, 1, 1, 1);
  assert(out == RGB5(8, 8, 8));
  art_filter_box_shrink(full, 2, 2, 2, &out, 1, 1, 1);
  assert(out == 0xFFFF);
  art_filter_box_shrink(white, 2, 2, 2, &out, 1, 1, 1);
  assert(out == 0x7FFF);
}

/* The alpha bit is kept when at least half the box has it. */
static void test_alpha_majority(void)
{
  uint16_t box[16];
  uint16_t out = 0xFFFF;

  fill(box, 16, 0);
  fill(box, 8, 0x8000);
  art_filter_box_shrink(box, 4, 4, 4, &out, 1, 1, 1);
  assert(out == 0x8000);
  box[7] = 0;
  art_filter_box_shrink(box, 4, 4, 4, &out, 1, 1, 1);
  assert(out == 0);
}

/* 240 -> 141 and 160 -> 141 give boxes of one or two pixels, so a single
   white guard word read into any box makes that pixel non-black. */
static void test_odd_ratio_stays_inside_buffers(void)
{
  uint32_t i;

  fill(src_buffer, GUARD + SRC_W * SRC_H + GUARD, 0xFFFF);
  fill(src, SRC_W * SRC_H, 0);
  fill(dst_buffer, GUARD + 141 * 141 + GUARD, 0x1234);
  art_filter_box_shrink(src, SRC_W, SRC_H, SRC_W, dst, 141, 141, 141);
  for(i = 0; i < 141 * 141; i++)
    assert(dst[i] == 0);
  for(i = 0; i < GUARD; i++)
  {
    assert(dst_buffer[i] == 0x1234);
    assert(dst[141 * 141 + i] == 0x1234);
  }
}

/* Pixels past the width in either pitch are neither read nor written. */
static void test_pitch_padding_untouched(void)
{
  uint16_t image[4 * 10];
  uint16_t out[2 * 6];
  uint32_t x, y;

  for(y = 0; y < 4; y++)
    for(x = 0; x < 10; x++)
      image[y * 10 + x] = (x < 8) ? RGB5(3, 5, 7) : 0xFFFF;
  fill(out, 2 * 6, 0x1234);
  art_filter_box_shrink(image, 8, 4, 10, out, 4, 2, 6);
  for(y = 0; y < 2; y++)
    for(x = 0; x < 6; x++)
      assert(out[y * 6 + x] == ((x < 4) ? RGB5(3, 5, 7) : 0x1234));
}

static void test_invalid_sizes_do_nothing(void)
{
  uint16_t image[4] = { 0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF };
  uint16_t out[9];
  uint32_t i;

  fill(out, 9, 0x1234);
  art_filter_box_shrink(image, 2, 2, 2, out, 0, 1, 1);
  art_filter_box_shrink(image, 2, 2, 2, out, 1, 0, 1);
  art_filter_box_shrink(image, 0, 2, 2, out, 1, 1, 1);
  art_filter_box_shrink(image, 2, 0, 2, out, 1, 1, 1);
  art_filter_box_shrink(image, 2, 2, 2, out, 3, 1, 3);
  art_filter_box_shrink(image, 2, 2, 2, out, 1, 3, 1);
  art_filter_box_shrink(image, 2, 2, 1, out, 1, 1, 1);
  art_filter_box_shrink(image, 2, 2, 2, out, 2, 1, 1);
  art_filter_box_shrink(NULL, 2, 2, 2, out, 1, 1, 1);
  art_filter_box_shrink(image, 2, 2, 2, NULL, 1, 1, 1);
  art_filter_box_shrink(image, 8193, 1, 8193, out, 1, 1, 1);
  for(i = 0; i < 9; i++)
    assert(out[i] == 0x1234);
}

int main(void)
{
  test_uniform_color_survives();
  test_blocks_map_one_to_one();
  test_checkerboard_gives_mid_gray();
  test_channels_never_carry();
  test_alpha_majority();
  test_odd_ratio_stays_inside_buffers();
  test_pitch_padding_untouched();
  test_invalid_sizes_do_nothing();
  puts("Art filter tests passed");
  return 0;
}
