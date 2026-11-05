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

/* Registers of the raster ROM that tests/gba-rom/gba.h lacks (GBATEK). */

#ifndef RASTER_H
#define RASTER_H

#include "result.h"

#define REG_BG2CNT    (*(vu16 *)0x0400000C)
/* PA | PB << 16, PC | PD << 16, X, Y */
#define REG_BG2AFFINE ((vu32 *)0x04000020)
#define REG_DMA0SAD   (*(vu32 *)0x040000B0)
#define REG_DMA0DAD   (*(vu32 *)0x040000B4)
#define REG_DMA0CNT   (*(vu32 *)0x040000B8)
#define REG_DMA1SAD   (*(vu32 *)0x040000BC)
#define REG_DMA1DAD   (*(vu32 *)0x040000C0)
#define REG_DMA1CNT   (*(vu32 *)0x040000C4)
#define PALETTE       ((vu16 *)0x05000000)

#define DISPCNT_MODE2       0x0002
#define DISPSTAT_HBLANK_IRQ 0x0010

/* DMAxCNT written as one word: the count in the low half. */
#define DMA_DEST_FIXED  0x00400000u
#define DMA_DEST_RELOAD 0x00600000u
#define DMA_SRC_FIXED   0x01000000u
#define DMA_REPEAT      0x02000000u
#define DMA_HBLANK      0x20000000u

#define VISIBLE_LINES SCREEN_HEIGHT
#define FRAME_LINES   228

#endif
