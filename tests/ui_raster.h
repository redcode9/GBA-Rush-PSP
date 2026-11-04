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
 * Host-only reference rasterizer for ui_batch commands (ui_raster.c): it
 * draws them into a 480x272 RGBA5551 image for the tests' hashes and
 * goldens.
 */

#ifndef UI_RASTER_H
#define UI_RASTER_H

#include <stdint.h>

#include "ui_batch.h"

typedef struct
{
  uint16_t pixels[UI_SCREEN_HEIGHT][UI_SCREEN_WIDTH];
} UiRaster;

/* Draws commands as a flush callback receives them; textures is the
   batch's registry. */
void ui_raster_execute(UiRaster *raster, const UiCommand *commands,
 uint32_t count, const UiTexture *textures);
/* FNV-1a 32 over the pixels, row by row, each one little-endian. */
uint32_t ui_raster_hash(const UiRaster *raster);

#endif
