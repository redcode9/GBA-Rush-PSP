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
 * Host tests for ui_batch.c, the interface's command list, drawn with the
 * reference rasterizer in ui_raster.c.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_batch.h"
#include "ui_raster.h"

#define W UI_SCREEN_WIDTH
#define H UI_SCREEN_HEIGHT
#define WHITE 0xFFFF
#define RED   0x801F
#define BLUE  0xFC00
#define IMAGE_SIDE   16
#define IMAGE_STRIDE 20
/* The hash of compose_screen() in ui_raster. */
#define COMPOSED_HASH 0xF6B8FC41u

static UiBatch batch;
static UiRaster raster;
static UiCommand logged[2 * UI_BATCH_CAPACITY];
static uint32_t logged_count;
static uint32_t flush_calls;
static uint16_t image[IMAGE_SIDE * IMAGE_STRIDE];
static uint8_t glyph_page[8 * 8 / 2];

static void raster_flush(void *context, const UiCommand *commands,
 uint32_t count, const UiTexture *textures)
{
  ui_raster_execute((UiRaster *)context, commands, count, textures);
}

static void log_flush(void *context, const UiCommand *commands,
 uint32_t count, const UiTexture *textures)
{
  uint32_t i;

  (void)context;
  (void)textures;
  flush_calls++;
  for(i = 0; i < count; i++)
  {
    assert(logged_count < 2 * UI_BATCH_CAPACITY);
    logged[logged_count++] = commands[i];
  }
}

static void start_logging(void)
{
  logged_count = 0;
  flush_calls = 0;
  ui_batch_init(&batch, log_flush, NULL);
}

static void start_raster(uint16_t background)
{
  ui_batch_init(&batch, raster_flush, &raster);
  assert(ui_batch_fill(&batch, 0, 0, W, H, background));
}

/* Texel (u, v) of the image is u | v << 5, so a pixel tells where it was
   sampled; the columns past its width hold a colour no test expects. */
static uint16_t image_texel(int u, int v)
{
  return image[v * IMAGE_STRIDE + u];
}

static void make_textures(void)
{
  int u, v;

  for(v = 0; v < IMAGE_SIDE; v++)
    for(u = 0; u < IMAGE_STRIDE; u++)
      image[v * IMAGE_STRIDE + u] = (uint16_t)(u < IMAGE_SIDE ?
       0x8000 | u | v << 5 : 0x7C1F);

  /* Coverage of texel i: i % 16, two to a byte, low nibble first. */
  for(u = 0; u < (int)sizeof(glyph_page); u++)
    glyph_page[u] = (uint8_t)(((2 * u) & 15) | ((2 * u + 1) & 15) << 4);
}

static void check_rect(const UiCommand *c, int x0, int y0, int x1, int y1)
{
  assert(c->x0 == x0 && c->y0 == y0 && c->x1 == x1 && c->y1 == y1);
}

static void check_texels(const UiCommand *c, int u0, int v0, int u1, int v1)
{
  assert(c->u0 == u0 && c->v0 == v0 && c->u1 == u1 && c->v1 == v1);
}

static void test_later_command_wins(void)
{
  start_raster(WHITE);
  assert(ui_batch_fill(&batch, 0, 0, 10, 10, RED));
  assert(ui_batch_fill(&batch, 5, 5, 15, 15, BLUE));
  ui_batch_flush(&batch);

  assert(raster.pixels[2][2] == RED);
  assert(raster.pixels[5][5] == BLUE);
  assert(raster.pixels[9][9] == BLUE);
  assert(raster.pixels[14][14] == BLUE);
  /* Half-open: the far edges stay out. */
  assert(raster.pixels[15][15] == WHITE);
  assert(raster.pixels[10][2] == WHITE);
  assert(raster.pixels[2][10] == WHITE);
}

static void test_clip_edges(void)
{
  UiTexture registry;
  int texture;

  start_logging();
  texture = ui_batch_add_texture(&batch, image, UI_TEXTURE_5551, IMAGE_SIDE,
   IMAGE_SIDE, IMAGE_STRIDE);
  assert(texture == 0);

  /* Left and top screen edges, from negative coordinates. */
  assert(ui_batch_image(&batch, texture, -3, -4, 13, 12, 0, 0, 16, 16));
  /* Right and bottom screen edges. */
  assert(ui_batch_image(&batch, texture, 470, 262, 486, 278, 0, 0, 16, 16));
  /* All four edges of a clip rectangle. */
  ui_batch_set_clip(&batch, 100, 50, 110, 60);
  assert(ui_batch_image(&batch, texture, 95, 45, 111, 61, 0, 0, 16, 16));
  /* Stretched twice: the texels move by half the cut. */
  ui_batch_set_clip(&batch, -50, -50, 1000, 1000);
  assert(ui_batch_image(&batch, texture, -4, 200, 12, 216, 0, 0, 8, 8));
  assert(ui_batch_fill(&batch, -10, -10, 5, 5, RED));
  ui_batch_flush(&batch);

  assert(flush_calls == 1 && logged_count == 5);
  check_rect(&logged[0], 0, 0, 13, 12);
  check_texels(&logged[0], 3, 4, 16, 16);
  check_rect(&logged[1], 470, 262, 480, 272);
  check_texels(&logged[1], 0, 0, 10, 10);
  check_rect(&logged[2], 100, 50, 110, 60);
  check_texels(&logged[2], 5, 5, 15, 15);
  check_rect(&logged[3], 0, 200, 12, 216);
  check_texels(&logged[3], 2, 0, 8, 8);
  check_rect(&logged[4], 0, 0, 5, 5);

  registry = batch.textures[texture];
  start_raster(0);
  ui_batch_flush(&batch);
  ui_raster_execute(&raster, logged, 4, &registry);
  assert(raster.pixels[0][0] == image_texel(3, 4));
  assert(raster.pixels[11][12] == image_texel(15, 15));
  assert(raster.pixels[271][479] == image_texel(9, 9));
  assert(raster.pixels[50][100] == image_texel(5, 5));
  assert(raster.pixels[59][109] == image_texel(14, 14));
  assert(raster.pixels[49][100] == 0 && raster.pixels[50][99] == 0);
  assert(raster.pixels[60][109] == 0 && raster.pixels[59][110] == 0);
  /* Clipped or not, screen column x samples texel (x + 4) / 2. */
  assert(raster.pixels[200][0] == image_texel(2, 0));
  assert(raster.pixels[200][1] == image_texel(2, 0));
  assert(raster.pixels[200][2] == image_texel(3, 0));
  assert(raster.pixels[215][11] == image_texel(7, 7));
}

static void test_glyph_clip_and_coverage(void)
{
  int page;

  start_logging();
  page = ui_batch_add_texture(&batch, glyph_page, UI_TEXTURE_T4, 8, 8, 8);
  assert(page == 0);
  assert(ui_batch_glyph(&batch, page, -2, 270, 0, 0, 4, 4, RED));
  ui_batch_flush(&batch);
  assert(logged_count == 1);
  check_rect(&logged[0], 0, 270, 2, 272);
  check_texels(&logged[0], 2, 0, 4, 2);

  /* Coverage 15 is the tint, 0 the background, between them a blend. */
  start_raster(WHITE);
  page = ui_batch_add_texture(&batch, glyph_page, UI_TEXTURE_T4, 8, 8, 8);
  assert(ui_batch_glyph(&batch, page, 0, 0, 0, 0, 8, 8, 0x0C63));
  ui_batch_flush(&batch);
  assert(raster.pixels[0][0] == WHITE);           /* coverage 0 */
  assert(raster.pixels[1][7] == (0x8000 | 0x0C63)); /* coverage 15 */
  /* Coverage 8 is alpha 136: (24 * 136 + 255 * 119) / 255 == 131, 16 in
     five bits. */
  assert(raster.pixels[1][0] == 0xC210);
}

static void test_full_batch_flushes_in_order(void)
{
  uint32_t i;

  start_logging();
  for(i = 0; i < UI_BATCH_CAPACITY; i++)
    assert(ui_batch_fill(&batch, 0, 0, 1, 1, (uint16_t)i));
  assert(flush_calls == 0);
  for(; i < UI_BATCH_CAPACITY + 5; i++)
    assert(ui_batch_fill(&batch, 0, 0, 1, 1, (uint16_t)i));
  assert(flush_calls == 1 && logged_count == UI_BATCH_CAPACITY);

  ui_batch_flush(&batch);
  assert(flush_calls == 2 && logged_count == UI_BATCH_CAPACITY + 5);
  for(i = 0; i < logged_count; i++)
    assert(logged[i].color == i);

  ui_batch_flush(&batch);
  assert(flush_calls == 2);
}

static void test_alpha(void)
{
  start_raster(WHITE);
  assert(ui_batch_fill_alpha(&batch, 0, 0, 1, 1, 0x000000FF));
  assert(ui_batch_fill_alpha(&batch, 1, 0, 2, 1, 0xFF0000FF));
  assert(ui_batch_fill_alpha(&batch, 2, 0, 3, 1, 0x80000000));
  assert(ui_batch_fill_alpha(&batch, 3, 0, 4, 1, 0xFF7F7F7F));
  ui_batch_flush(&batch);

  assert(raster.pixels[0][0] == WHITE);
  assert(raster.pixels[0][1] == RED);
  /* 255 * 127 / 255 == 127, 15 in five bits. */
  assert(raster.pixels[0][2] == 0xBDEF);
  assert(raster.pixels[0][3] == 0xBDEF);
}

static void test_refusals(void)
{
  int i;

  start_logging();
  assert(!ui_batch_fill(&batch, W, 0, W + 10, 10, RED));
  assert(!ui_batch_fill(&batch, -10, 0, 0, 10, RED));
  assert(!ui_batch_fill(&batch, 0, H, 10, H + 1, RED));
  assert(!ui_batch_fill(&batch, 10, 10, 10, 20, RED));
  assert(!ui_batch_fill(&batch, 0, 0, 40000, 10, RED));
  ui_batch_set_clip(&batch, 10, 10, 20, 20);
  assert(!ui_batch_fill(&batch, 0, 0, 10, 10, RED));
  assert(!ui_batch_fill(&batch, 20, 20, 30, 30, RED));
  ui_batch_set_clip(&batch, 500, 0, 600, 10);
  assert(!ui_batch_fill(&batch, 0, 0, W, H, RED));
  ui_batch_flush(&batch);
  assert(flush_calls == 0 && logged_count == 0);

  ui_batch_set_clip(&batch, 0, 0, W, H);
  for(i = 0; i < UI_BATCH_TEXTURES; i++)
    assert(ui_batch_add_texture(&batch, image, UI_TEXTURE_5551, IMAGE_SIDE,
     IMAGE_SIDE, IMAGE_STRIDE) == i);
  assert(ui_batch_add_texture(&batch, image, UI_TEXTURE_5551, IMAGE_SIDE,
   IMAGE_SIDE, IMAGE_STRIDE) == -1);

  /* Unknown slots, the wrong format, texels outside the texture. */
  assert(!ui_batch_image(&batch, -1, 0, 0, 16, 16, 0, 0, 16, 16));
  assert(!ui_batch_image(&batch, UI_BATCH_TEXTURES, 0, 0, 16, 16, 0, 0, 16,
   16));
  assert(!ui_batch_glyph(&batch, 0, 0, 0, 0, 0, 8, 8, RED));
  assert(!ui_batch_image(&batch, 0, 0, 0, 17, 16, 0, 0, 17, 16));
  assert(!ui_batch_image(&batch, 0, 0, 0, 16, 16, 4, 0, 4, 16));

  /* Clearing the registry first draws what still names its slots. */
  assert(ui_batch_image(&batch, 0, 0, 0, 16, 16, 0, 0, 16, 16));
  ui_batch_clear_textures(&batch);
  assert(flush_calls == 1 && logged_count == 1);
  assert(!ui_batch_image(&batch, 0, 0, 0, 16, 16, 0, 0, 16, 16));
  assert(ui_batch_add_texture(&batch, image, UI_TEXTURE_5551, IMAGE_SIDE,
   IMAGE_SIDE, IMAGE_STRIDE) == 0);
  assert(ui_batch_add_texture(&batch, NULL, UI_TEXTURE_5551, 1, 1, 1) == -1);
  assert(ui_batch_add_texture(&batch, image, UI_TEXTURE_5551, 513, 1,
   513) == -1);
  assert(ui_batch_add_texture(&batch, image, UI_TEXTURE_5551, 16, 16,
   15) == -1);
}

/* draw_box_line, draw_hline and draw_vline take inclusive ends. */
static void test_line_adapters(void)
{
  int x, y;

  start_raster(WHITE);
  assert(ui_batch_box_line(&batch, 10, 20, 15, 24, RED) == 4);
  assert(ui_batch_hline(&batch, 0, 4, 30, BLUE));
  assert(ui_batch_vline(&batch, 40, 0, 3, BLUE));
  ui_batch_flush(&batch);

  for(y = 18; y <= 26; y++)
  {
    for(x = 8; x <= 17; x++)
    {
      int edge = (x == 10 || x == 15) && y >= 20 && y <= 24;

      edge |= (y == 20 || y == 24) && x >= 10 && x <= 15;
      assert(raster.pixels[y][x] == (edge ? RED : WHITE));
    }
  }
  assert(raster.pixels[30][0] == BLUE && raster.pixels[30][4] == BLUE);
  assert(raster.pixels[30][5] == WHITE && raster.pixels[29][0] == WHITE);
  assert(raster.pixels[0][40] == BLUE && raster.pixels[3][40] == BLUE);
  assert(raster.pixels[4][40] == WHITE && raster.pixels[0][41] == WHITE);

  /* A one-pixel box is one line, a one-row box too, a one-column box has
     no second side. */
  assert(ui_batch_box_line(&batch, 50, 50, 50, 50, RED) == 1);
  assert(ui_batch_box_line(&batch, 50, 60, 55, 60, RED) == 1);
  assert(ui_batch_box_line(&batch, 60, 60, 60, 65, RED) == 3);
  assert(ui_batch_box_line(&batch, 60, 60, 59, 65, RED) == 0);
}

/* A menu-like screen: background, an image, a popup as draw_popup_frame
   draws it at (100, 60) 200 x 100, an image and a glyph on the popup. */
static uint32_t compose_screen(void)
{
  int texture, page;

  ui_batch_init(&batch, raster_flush, &raster);
  texture = ui_batch_add_texture(&batch, image, UI_TEXTURE_5551, IMAGE_SIDE,
   IMAGE_SIDE, IMAGE_STRIDE);
  page = ui_batch_add_texture(&batch, glyph_page, UI_TEXTURE_T4, 8, 8, 8);

  assert(ui_batch_fill(&batch, 0, 0, W, H, 0x8C63));
  assert(ui_batch_image(&batch, texture, 20, 20, 52, 52, 0, 0, 16, 16));
  assert(ui_batch_fill_alpha(&batch, 0, 0, W, H, 0x8F000000));
  assert(ui_batch_fill(&batch, 103, 63, 303, 163, 0x8421));
  assert(ui_batch_box_line(&batch, 99, 59, 300, 160, 0xFFE0) == 4);
  assert(ui_batch_fill(&batch, 100, 60, 300, 160, 0x9CE7));
  assert(ui_batch_hline(&batch, 101, 298, 61, 0xA94A));
  assert(ui_batch_image(&batch, texture, 110, 70, 126, 86, 0, 0, 16, 16));
  assert(ui_batch_glyph(&batch, page, 130, 70, 0, 0, 8, 8, 0xFFFF));
  ui_batch_flush(&batch);
  return ui_raster_hash(&raster);
}

static void test_composed_screen_hash(void)
{
  uint32_t hash = compose_screen();

  assert(hash == COMPOSED_HASH);
  assert(compose_screen() == hash);
  raster.pixels[271][479] ^= 1;
  assert(ui_raster_hash(&raster) != hash);
}

int main(void)
{
  make_textures();
  test_later_command_wins();
  test_clip_edges();
  test_glyph_clip_and_coverage();
  test_full_batch_flushes_in_order();
  test_alpha();
  test_refusals();
  test_line_adapters();
  test_composed_screen_hash();
  puts("UI batch tests passed");
  return 0;
}
