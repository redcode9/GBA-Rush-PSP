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
 * A frame's interface drawing as a list of commands (ui_batch.c).
 *
 * The menu queues filled rectangles, lines, images and text glyphs into a
 * fixed array; the owner's flush callback receives them in the order they
 * were queued and draws them (with the GE on the console, with
 * tests/ui_raster.c on the host). When the array is full the queued
 * commands are flushed before the new one goes in, so no command is ever
 * lost and the order never changes.
 *
 * Coordinates are screen pixels in the signed 16-bit range of the GE's
 * 16-bit vertices (a command reaching outside it is refused), and every
 * rectangle is half-open: [x0, x1) x [y0, y1) covers x1 - x0 columns.
 * Today's drawing calls take inclusive corners; they map as
 *   draw_box_fill(x1, y1, x2, y2, c)  -> ui_batch_fill(b, x1, y1, x2 + 1, y2 + 1, c)
 *   draw_box_alpha(x1, y1, x2, y2, c) -> ui_batch_fill_alpha(b, x1, y1, x2 + 1, y2 + 1, c)
 *   draw_box_line(x1, y1, x2, y2, c)  -> ui_batch_box_line(b, x1, y1, x2, y2, c)
 *   draw_hline(sx, ex, y, c)          -> ui_batch_hline(b, sx, ex, y, c)
 *   draw_vline(x, sy, ey, c)          -> ui_batch_vline(b, x, sy, ey, c)
 *   blit_to_screen(src, w, h, x, y)   -> ui_batch_image(b, t, x, y, 0, 0, w, h)
 *                                        with t from ui_batch_add_texture(b, src, ...)
 * The batch reads a texture's pixels only when it flushes, so they must stay
 * unchanged until the next ui_batch_flush or ui_batch_clear_textures. A
 * caller that refills a buffer it already queued (boxart_carousel_draw
 * scales every carousel slot into one scratch buffer), or gets -1 from
 * ui_batch_add_texture, calls ui_batch_clear_textures (which flushes) first.
 *
 * Commands are cut to the clip rectangle when they are queued, so a stored
 * command never reaches outside the screen and a fully hidden one is not
 * stored at all. Textures are drawn 1:1, one texel per pixel, so a cut
 * moves the texels exactly with the edges. Colours are RGBA5551 (red in
 * the low bits), except for ui_batch_fill_alpha, which takes the GU 8888
 * value 0xAABBGGRR.
 */

#ifndef UI_BATCH_H
#define UI_BATCH_H

#include <stdint.h>

#define UI_SCREEN_WIDTH     480
#define UI_SCREEN_HEIGHT    272
#define UI_BATCH_CAPACITY   256
#define UI_BATCH_TEXTURES   8
/* The GE's largest texture side. */
#define UI_TEXTURE_MAX_SIDE 512

typedef enum
{
  UI_COMMAND_FILL,       /* replaces pixels with a 5551 colour */
  UI_COMMAND_FILL_ALPHA, /* blends an 8888 colour over the pixels */
  UI_COMMAND_LINE,       /* a one-pixel horizontal or vertical 5551 line */
  UI_COMMAND_IMAGE,      /* a 5551 texture rectangle, replaced */
  UI_COMMAND_GLYPH       /* a T4 coverage rectangle tinted with a 5551 colour */
} UiCommandKind;

typedef enum
{
  UI_TEXTURE_5551, /* one uint16_t per texel */
  UI_TEXTURE_T4    /* 4-bit coverage 0..15, two texels per byte, low nibble first */
} UiTextureFormat;

typedef struct
{
  const void *pixels;
  uint16_t width;
  uint16_t height;
  uint16_t stride; /* texels from one row to the next */
  uint8_t format;  /* UiTextureFormat */
} UiTexture;

typedef struct
{
  uint8_t kind;    /* UiCommandKind */
  uint8_t texture; /* IMAGE and GLYPH: the registry slot */
  int16_t x0, y0, x1, y1;
  /* IMAGE and GLYPH: the texels [u0, u1) x [v0, v1), drawn 1:1 onto the
     rectangle (u1 - u0 == x1 - x0, v1 - v0 == y1 - y0). */
  uint16_t u0, v0, u1, v1;
  uint32_t color;
} UiCommand;

/* Draws count commands in order; textures is the batch's registry. It must
   not queue into the batch that calls it. */
typedef void (*UiBatchFlush)(void *context, const UiCommand *commands,
 uint32_t count, const UiTexture *textures);

typedef struct
{
  UiCommand commands[UI_BATCH_CAPACITY];
  uint32_t count;
  UiTexture textures[UI_BATCH_TEXTURES];
  uint32_t texture_count;
  int16_t clip_x0, clip_y0, clip_x1, clip_y1;
  UiBatchFlush flush;
  void *context;
} UiBatch;

/* Empties the batch and the registry and clips to the whole screen; flush
   must not be NULL. A UiBatch is large: keep it static, not on the stack. */
void ui_batch_init(UiBatch *batch, UiBatchFlush flush, void *context);
/* Commands queued from now on are cut to this rectangle and the screen. */
void ui_batch_set_clip(UiBatch *batch, int x0, int y0, int x1, int y1);
/* Registers a texture and returns its slot, or -1 when the registry is
   full or the texture is not valid (sides 1..UI_TEXTURE_MAX_SIDE). */
int ui_batch_add_texture(UiBatch *batch, const void *pixels,
 UiTextureFormat format, uint32_t width, uint32_t height, uint32_t stride);
/* Flushes, then empties the registry so its slots can be reused. */
void ui_batch_clear_textures(UiBatch *batch);

/* Each of these returns 1 when it queued a command and 0 when nothing of it
   is visible or its arguments are not valid. */
int ui_batch_fill(UiBatch *batch, int x0, int y0, int x1, int y1,
 uint16_t color);
int ui_batch_fill_alpha(UiBatch *batch, int x0, int y0, int x1, int y1,
 uint32_t color);
/* Inclusive ends, as draw_hline and draw_vline take them. */
int ui_batch_hline(UiBatch *batch, int sx, int ex, int y, uint16_t color);
int ui_batch_vline(UiBatch *batch, int x, int sy, int ey, uint16_t color);
/* The one-pixel outline of the inclusive box, as up to four lines that do
   not overlap; returns how many it queued. */
int ui_batch_box_line(UiBatch *batch, int x1, int y1, int x2, int y2,
 uint16_t color);
/* A w x h rectangle at texels (u, v) of a 5551 texture, drawn 1:1 at
   (x, y). */
int ui_batch_image(UiBatch *batch, int texture, int x, int y, int u, int v,
 int w, int h);
/* A w x h glyph at texels (u, v) of a T4 page, drawn 1:1 at (x, y). */
int ui_batch_glyph(UiBatch *batch, int texture, int x, int y, int u, int v,
 int w, int h, uint16_t color);

/* Sends every queued command to the callback; does nothing when the batch
   is empty. */
void ui_batch_flush(UiBatch *batch);

#endif
