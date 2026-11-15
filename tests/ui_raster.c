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
 * Reference rasterizer for ui_batch commands, host only. It follows what
 * the menu asks of the GE: replaced 5551 fills, lines and images, and
 * blending set up as init_video does it, sceGuBlendFunc(GU_ADD,
 * GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA). Textures are drawn 1:1, one texel
 * per pixel. Dithering is not modelled.
 */

#include "ui_raster.h"

static uint32_t ui_raster_expand(uint32_t c5)
{
  return (c5 << 3) | (c5 >> 2);
}

/* Each 5-bit channel widened to 8 bits, blended with alpha and truncated
   back to 5; rgb8 is 0x00BBGGRR. The pixel keeps its own alpha bit. */
static uint16_t ui_raster_blend(uint16_t dst, uint32_t rgb8, uint32_t alpha)
{
  uint32_t out = dst & 0x8000u;
  uint32_t channel;

  for(channel = 0; channel < 3; channel++)
  {
    uint32_t s = (rgb8 >> (channel * 8)) & 0xFFu;
    uint32_t d = ui_raster_expand((dst >> (channel * 5)) & 0x1Fu);

    out |= (((s * alpha + d * (255 - alpha)) / 255) >> 3) << (channel * 5);
  }
  return (uint16_t)out;
}

static uint32_t ui_raster_rgb8(uint32_t color5551)
{
  return ui_raster_expand(color5551 & 0x1Fu) |
   ui_raster_expand((color5551 >> 5) & 0x1Fu) << 8 |
   ui_raster_expand((color5551 >> 10) & 0x1Fu) << 16;
}

static uint32_t ui_raster_texel(const UiTexture *texture, uint32_t u,
 uint32_t v)
{
  uint32_t i = v * texture->stride + u;

  if(texture->format == UI_TEXTURE_T4)
    return (((const uint8_t *)texture->pixels)[i >> 1] >> ((i & 1) * 4)) &
     0xFu;
  return ((const uint16_t *)texture->pixels)[i];
}

void ui_raster_execute(UiRaster *raster, const UiCommand *commands,
 uint32_t count, const UiTexture *textures)
{
  uint32_t i;

  for(i = 0; i < count; i++)
  {
    const UiCommand *c = &commands[i];
    const UiTexture *texture = &textures[c->texture];
    int x, y;

    for(y = c->y0; y < c->y1; y++)
    {
      uint16_t *row = raster->pixels[y];
      uint32_t v = c->v0 + (uint32_t)(y - c->y0);

      for(x = c->x0; x < c->x1; x++)
      {
        uint32_t u = c->u0 + (uint32_t)(x - c->x0);

        switch(c->kind)
        {
          case UI_COMMAND_FILL:
          case UI_COMMAND_LINE:
            row[x] = (uint16_t)c->color;
            break;

          case UI_COMMAND_FILL_ALPHA:
            row[x] = ui_raster_blend(row[x], c->color & 0xFFFFFFu,
             c->color >> 24);
            break;

          case UI_COMMAND_IMAGE:
            row[x] = (uint16_t)ui_raster_texel(texture, u, v);
            break;

          case UI_COMMAND_GLYPH:
            /* Coverage 0..15 is alpha 0..255. */
            row[x] = ui_raster_blend(row[x], ui_raster_rgb8(c->color),
             ui_raster_texel(texture, u, v) * 17);
            break;
        }
      }
    }
  }
}

uint32_t ui_raster_hash(const UiRaster *raster)
{
  uint32_t hash = 2166136261u;
  int x, y;

  for(y = 0; y < UI_SCREEN_HEIGHT; y++)
  {
    for(x = 0; x < UI_SCREEN_WIDTH; x++)
    {
      hash = (hash ^ (raster->pixels[y][x] & 0xFFu)) * 16777619u;
      hash = (hash ^ (raster->pixels[y][x] >> 8)) * 16777619u;
    }
  }
  return hash;
}
