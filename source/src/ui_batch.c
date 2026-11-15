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
 * The interface's command list: clipping, the texture registry and the
 * flush that keeps every command in order. See ui_batch.h.
 */

#include "ui_batch.h"

#include <stddef.h>

static int ui_coord_ok(int v)
{
  return v >= INT16_MIN && v <= INT16_MAX;
}

/* Cuts [x0, x1) x [y0, y1) to the clip rectangle, moves a textured
   command's texels with the cut edges and queues it; 0 when nothing is
   left. */
static int ui_batch_queue(UiBatch *batch, UiCommand *command, int x0, int y0,
 int x1, int y1)
{
  int cx0, cy0, cx1, cy1;

  if(!ui_coord_ok(x0) || !ui_coord_ok(y0) || !ui_coord_ok(x1) ||
     !ui_coord_ok(y1))
    return 0;

  cx0 = x0 > batch->clip_x0 ? x0 : batch->clip_x0;
  cy0 = y0 > batch->clip_y0 ? y0 : batch->clip_y0;
  cx1 = x1 < batch->clip_x1 ? x1 : batch->clip_x1;
  cy1 = y1 < batch->clip_y1 ? y1 : batch->clip_y1;
  if(cx0 >= cx1 || cy0 >= cy1)
    return 0;

  if(command->kind == UI_COMMAND_IMAGE || command->kind == UI_COMMAND_GLYPH)
  {
    /* Drawn 1:1, so each texel edge moves as far as its screen edge. */
    command->u0 = (uint16_t)(command->u0 + (cx0 - x0));
    command->u1 = (uint16_t)(command->u1 - (x1 - cx1));
    command->v0 = (uint16_t)(command->v0 + (cy0 - y0));
    command->v1 = (uint16_t)(command->v1 - (y1 - cy1));
  }

  command->x0 = (int16_t)cx0;
  command->y0 = (int16_t)cy0;
  command->x1 = (int16_t)cx1;
  command->y1 = (int16_t)cy1;

  if(batch->count == UI_BATCH_CAPACITY)
    ui_batch_flush(batch);
  batch->commands[batch->count++] = *command;
  return 1;
}

static int ui_batch_solid(UiBatch *batch, UiCommandKind kind, int x0, int y0,
 int x1, int y1, uint32_t color)
{
  UiCommand command = { 0 };

  command.kind = (uint8_t)kind;
  command.color = color;
  return ui_batch_queue(batch, &command, x0, y0, x1, y1);
}

/* Queues the w x h texels at (u, v) of slot texture, which must hold the
   given format, 1:1 at (x, y). */
static int ui_batch_textured(UiBatch *batch, UiCommandKind kind,
 UiTextureFormat format, int texture, int x, int y, int u, int v, int w,
 int h, uint32_t color)
{
  const UiTexture *registered;
  UiCommand command = { 0 };

  if(texture < 0 || (uint32_t)texture >= batch->texture_count)
    return 0;
  registered = &batch->textures[texture];
  /* The texels must lie inside the texture; each check comes before a sum
     that could overflow an int. */
  if(registered->format != format || !ui_coord_ok(x) || !ui_coord_ok(y) ||
     w < 1 || h < 1 || u < 0 || v < 0 || u > registered->width - w ||
     v > registered->height - h)
    return 0;

  command.kind = (uint8_t)kind;
  command.texture = (uint8_t)texture;
  command.u0 = (uint16_t)u;
  command.v0 = (uint16_t)v;
  command.u1 = (uint16_t)(u + w);
  command.v1 = (uint16_t)(v + h);
  command.color = color;
  return ui_batch_queue(batch, &command, x, y, x + w, y + h);
}

void ui_batch_init(UiBatch *batch, UiBatchFlush flush, void *context)
{
  batch->count = 0;
  batch->texture_count = 0;
  batch->flush = flush;
  batch->context = context;
  ui_batch_set_clip(batch, 0, 0, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
}

void ui_batch_set_clip(UiBatch *batch, int x0, int y0, int x1, int y1)
{
  x0 = x0 < 0 ? 0 : x0 > UI_SCREEN_WIDTH ? UI_SCREEN_WIDTH : x0;
  y0 = y0 < 0 ? 0 : y0 > UI_SCREEN_HEIGHT ? UI_SCREEN_HEIGHT : y0;
  x1 = x1 < x0 ? x0 : x1 > UI_SCREEN_WIDTH ? UI_SCREEN_WIDTH : x1;
  y1 = y1 < y0 ? y0 : y1 > UI_SCREEN_HEIGHT ? UI_SCREEN_HEIGHT : y1;

  batch->clip_x0 = (int16_t)x0;
  batch->clip_y0 = (int16_t)y0;
  batch->clip_x1 = (int16_t)x1;
  batch->clip_y1 = (int16_t)y1;
}

int ui_batch_add_texture(UiBatch *batch, const void *pixels,
 UiTextureFormat format, uint32_t width, uint32_t height, uint32_t stride)
{
  UiTexture *texture;

  if(batch->texture_count == UI_BATCH_TEXTURES || pixels == NULL ||
     (format != UI_TEXTURE_5551 && format != UI_TEXTURE_T4) ||
     width == 0 || width > UI_TEXTURE_MAX_SIDE || height == 0 ||
     height > UI_TEXTURE_MAX_SIDE || stride < width ||
     stride > UI_TEXTURE_MAX_SIDE)
    return -1;

  texture = &batch->textures[batch->texture_count];
  texture->pixels = pixels;
  texture->width = (uint16_t)width;
  texture->height = (uint16_t)height;
  texture->stride = (uint16_t)stride;
  texture->format = (uint8_t)format;
  return (int)batch->texture_count++;
}

void ui_batch_clear_textures(UiBatch *batch)
{
  /* Queued commands still name their slots. */
  ui_batch_flush(batch);
  batch->texture_count = 0;
}

int ui_batch_fill(UiBatch *batch, int x0, int y0, int x1, int y1,
 uint16_t color)
{
  return ui_batch_solid(batch, UI_COMMAND_FILL, x0, y0, x1, y1, color);
}

int ui_batch_fill_alpha(UiBatch *batch, int x0, int y0, int x1, int y1,
 uint32_t color)
{
  return ui_batch_solid(batch, UI_COMMAND_FILL_ALPHA, x0, y0, x1, y1, color);
}

/* The lines check what they add 1 to first, so the sum cannot overflow. */
int ui_batch_hline(UiBatch *batch, int sx, int ex, int y, uint16_t color)
{
  return ui_coord_ok(ex) && ui_coord_ok(y) &&
   ui_batch_solid(batch, UI_COMMAND_LINE, sx, y, ex + 1, y + 1, color);
}

int ui_batch_vline(UiBatch *batch, int x, int sy, int ey, uint16_t color)
{
  return ui_coord_ok(x) && ui_coord_ok(ey) &&
   ui_batch_solid(batch, UI_COMMAND_LINE, x, sy, x + 1, ey + 1, color);
}

int ui_batch_box_line(UiBatch *batch, int x1, int y1, int x2, int y2,
 uint16_t color)
{
  int queued = 0;

  if(x2 < x1 || y2 < y1 || !ui_coord_ok(y1) || !ui_coord_ok(y2))
    return 0;

  queued += ui_batch_hline(batch, x1, x2, y1, color);
  if(y2 != y1)
    queued += ui_batch_hline(batch, x1, x2, y2, color);
  /* The sides leave out the corners the top and bottom already drew. */
  queued += ui_batch_vline(batch, x1, y1 + 1, y2 - 1, color);
  if(x2 != x1)
    queued += ui_batch_vline(batch, x2, y1 + 1, y2 - 1, color);
  return queued;
}

int ui_batch_image(UiBatch *batch, int texture, int x, int y, int u, int v,
 int w, int h)
{
  return ui_batch_textured(batch, UI_COMMAND_IMAGE, UI_TEXTURE_5551, texture,
   x, y, u, v, w, h, 0);
}

int ui_batch_glyph(UiBatch *batch, int texture, int x, int y, int u, int v,
 int w, int h, uint16_t color)
{
  return ui_batch_textured(batch, UI_COMMAND_GLYPH, UI_TEXTURE_T4, texture,
   x, y, u, v, w, h, color);
}

void ui_batch_flush(UiBatch *batch)
{
  if(batch->count == 0)
    return;

  batch->flush(batch->context, batch->commands, batch->count,
   batch->textures);
  batch->count = 0;
}
