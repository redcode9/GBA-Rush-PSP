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
 * How a line is drawn: the sprites go first into a sprite line, which also
 * marks the OBJ window. Then every column learns which layers and effects
 * its window allows. The backgrounds and the sprites are then laid down from
 * the back (priority 3, BG3) to the front (priority 0, BG0, sprites in front
 * of backgrounds of the same priority), each column keeping only its two
 * front pixels. A last pass mixes those two as BLDCNT asks and only then
 * looks their colours up in palette RAM.
 */

#include "gba_ppu.h"

#include <string.h>

/* Register offsets from 0x04000000. */
#define REG_DISPCNT  0x00
#define REG_BG0CNT   0x08
#define REG_BG0HOFS  0x10
#define REG_BG2PA    0x20
#define REG_BG2PB    0x22
#define REG_BG2PC    0x24
#define REG_BG2PD    0x26
#define REG_BG2X     0x28
#define REG_WIN0H    0x40
#define REG_WIN0V    0x44
#define REG_WININ    0x48
#define REG_WINOUT   0x4A
#define REG_MOSAIC   0x4C
#define REG_BLDCNT   0x50
#define REG_BLDALPHA 0x52
#define REG_BLDY     0x54
/* BG3's affine registers follow BG2's 16 bytes later. */
#define AFFINE_STRIDE 0x10

#define IO(ppu, reg) ((ppu)->io[(reg) >> 1])

#define DISPCNT_FRAME       0x0010
#define DISPCNT_HBLANK_FREE 0x0020
#define DISPCNT_OBJ_1D      0x0040
#define DISPCNT_BLANK       0x0080
#define DISPCNT_OBJ         0x1000
#define DISPCNT_WINDOWS     0xE000
#define DISPCNT_OBJ_WINDOW  0x8000

#define WHITE 0x7FFF

/*
 * A pixel on its way to the screen. The low 16 bits hold a palette index
 * (0-511, sprites from 256 up) or, with PIXEL_DIRECT, a BGR555 colour from
 * a bitmap mode. Above them sits the bit of the layer it comes from, in the
 * order of BLDCNT's target bits, so a target test is a single AND.
 */
#define PIXEL_DIRECT   0x8000u
#define PIXEL_LAYER(n) (0x10000u << (n))  /* BG0-BG3, 4 = OBJ */
#define PIXEL_BACKDROP PIXEL_LAYER(5)
#define PIXEL_SEMI     0x400000u          /* semi-transparent sprite */
/* Sprite line only: the sprite's priority, 4 where no sprite is, and a
   flag for the columns an OBJ window sprite covers. */
#define OBJ_PRIO_SHIFT 28
#define OBJ_PRIO_MASK  (7u << OBJ_PRIO_SHIFT)
#define OBJ_EMPTY      (4u << OBJ_PRIO_SHIFT)
#define OBJ_WINDOW     0x08000000u
#define OBJ_PIXEL      0x00FFFFFFu
#define OBJ_COLOUR     0x0000FFFFu

/* Window bits per column: BG0-BG3 and OBJ shown, then special effects. */
#define WINDOW_OBJ     0x10
#define WINDOW_EFFECTS 0x20
#define WINDOW_ALL     0x3F

/* Backgrounds each mode has, by bit. */
static const uint8_t mode_backgrounds[8] =
{
  0x0F, 0x07, 0x0C, 0x04, 0x04, 0x04, 0x00, 0x00
};

/* Sprite width and height in pixels by shape * 4 + size. */
static const uint8_t obj_sizes[12][2] =
{
  { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 },
  { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 },
  { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 }
};

static void copy_into(void *memory, uint32_t memory_size, uint32_t offset,
 const void *data, uint32_t size)
{
  if(offset >= memory_size)
    return;
  if(size > memory_size - offset)
    size = memory_size - offset;
  memcpy((uint8_t *)memory + offset, data, size);
}

void gba_ppu_write_palette(GbaPpu *ppu, uint32_t offset, const void *data,
 uint32_t size)
{
  copy_into(ppu->palette, sizeof(ppu->palette), offset, data, size);
}

void gba_ppu_write_oam(GbaPpu *ppu, uint32_t offset, const void *data,
 uint32_t size)
{
  copy_into(ppu->oam, sizeof(ppu->oam), offset, data, size);
}

void gba_ppu_write_vram(GbaPpu *ppu, uint32_t offset, const void *data,
 uint32_t size)
{
  copy_into(ppu->vram, sizeof(ppu->vram), offset, data, size);
}

/* The reference point registers hold 28-bit signed numbers. */
static int32_t reference_point(const GbaPpu *ppu, uint32_t offset)
{
  uint32_t value = ppu->io[offset >> 1] |
   (uint32_t)ppu->io[(offset >> 1) + 1] << 16;

  return (int32_t)(value << 4) >> 4;
}

void gba_ppu_write_io(GbaPpu *ppu, uint32_t offset, uint16_t value)
{
  offset &= ~1u;
  if(offset >= GBA_PPU_IO_BYTES)
    return;
  ppu->io[offset >> 1] = value;

  /* BG2X/BG2Y at 0x28-0x2F, BG3X/BG3Y at 0x38-0x3F: a write to either half
     moves that reference point at once. */
  if(offset >= REG_BG2X && offset < 0x40 && (offset & 8))
  {
    int bg = (offset >> 4) - 2;
    int32_t point = reference_point(ppu, offset & ~2u);

    if(offset & 4)
      ppu->affine_y[bg] = point;
    else
      ppu->affine_x[bg] = point;
  }
}

void gba_ppu_start_frame(GbaPpu *ppu)
{
  int bg;

  for(bg = 0; bg < 2; bg++)
  {
    ppu->affine_x[bg] = reference_point(ppu, REG_BG2X + bg * AFFINE_STRIDE);
    ppu->affine_y[bg] = reference_point(ppu,
     REG_BG2X + 4 + bg * AFFINE_STRIDE);
  }
}

static void advance_affine(GbaPpu *ppu)
{
  int bg;

  for(bg = 0; bg < 2; bg++)
  {
    ppu->affine_x[bg] += (int16_t)IO(ppu, REG_BG2PB + bg * AFFINE_STRIDE);
    ppu->affine_y[bg] += (int16_t)IO(ppu, REG_BG2PD + bg * AFFINE_STRIDE);
  }
}

/* Text background: whole tiles of 8 pixels from the one under the left
   edge of the screen; the returned line starts at the screen's left edge. */
static const uint16_t *draw_text_bg(GbaPpu *ppu, int bg, int y)
{
  const uint8_t *vram = (const uint8_t *)ppu->vram;
  uint16_t control = IO(ppu, REG_BG0CNT + bg * 2);
  unsigned scroll_x = IO(ppu, REG_BG0HOFS + bg * 4) & 0x1FF;
  unsigned scroll_y = IO(ppu, REG_BG0HOFS + bg * 4 + 2) & 0x1FF;
  unsigned chars = ((control >> 2) & 3) << 14;
  unsigned map = ((control >> 8) & 31) << 11;
  unsigned size = control >> 14;
  unsigned wide = size & 1;
  unsigned map_y, column, tile_y, i;
  uint16_t *out = ppu->layer;

  if(control & 0x40)
  {
    unsigned mosaic = ((IO(ppu, REG_MOSAIC) >> 4) & 15) + 1;
    y -= y % mosaic;
  }
  map_y = (y + scroll_y) & (size & 2 ? 511 : 255);
  /* Maps are 32x32 entries of 2 KB, a tall map's lower half follows its
     upper half, which is one or two maps wide. */
  if(map_y >= 256)
    map += wide ? 0x1000 : 0x800;
  map += (map_y >> 3 & 31) * 64;
  tile_y = map_y & 7;
  column = scroll_x >> 3;

  for(i = 0; i < 31; i++, column++, out += 8)
  {
    unsigned x = column & (wide ? 63 : 31);
    unsigned entry = ppu->vram[(map + (x & 31) * 2 +
     (x & 32 ? 0x800 : 0)) >> 1];
    unsigned row = entry & 0x800 ? 7 - tile_y : tile_y;
    int flip = entry & 0x400 ? 7 : 0;
    unsigned address;
    int n;

    if(control & 0x80)
    {
      address = chars + (entry & 0x3FF) * 64 + row * 8;
      /* Background tiles cannot reach the sprite tiles. */
      if(address >= 0x10000)
      {
        memset(out, 0, 8 * sizeof(*out));
        continue;
      }
      for(n = 0; n < 8; n++)
        out[n ^ flip] = vram[address + n];
    }
    else
    {
      uint32_t pixels;
      unsigned palette = (entry >> 12) << 4;

      address = chars + (entry & 0x3FF) * 32 + row * 4;
      if(address >= 0x10000)
      {
        memset(out, 0, 8 * sizeof(*out));
        continue;
      }
      pixels = vram[address] | vram[address + 1] << 8 |
       (uint32_t)vram[address + 2] << 16 | (uint32_t)vram[address + 3] << 24;
      for(n = 0; n < 8; n++, pixels >>= 4)
        out[n ^ flip] = pixels & 15 ? palette | (pixels & 15) : 0;
    }
  }
  return ppu->layer + (scroll_x & 7);
}

/* Where an affine line starts: the internal reference point, or with
   vertical mosaic the one of the first line of the mosaic block. */
static void affine_start(const GbaPpu *ppu, int bg, int y, int32_t *x,
 int32_t *y_out)
{
  int i = bg - 2;
  uint16_t control = IO(ppu, REG_BG0CNT + bg * 2);

  *x = ppu->affine_x[i];
  *y_out = ppu->affine_y[i];
  if(control & 0x40)
  {
    int back = y % (((IO(ppu, REG_MOSAIC) >> 4) & 15) + 1);

    *x -= back * (int16_t)IO(ppu, REG_BG2PB + i * AFFINE_STRIDE);
    *y_out -= back * (int16_t)IO(ppu, REG_BG2PD + i * AFFINE_STRIDE);
  }
}

/* Rotation/scaling background of 8-bit tiles with one-byte map entries. */
static const uint16_t *draw_affine_bg(GbaPpu *ppu, int bg, int y)
{
  const uint8_t *vram = (const uint8_t *)ppu->vram;
  uint16_t control = IO(ppu, REG_BG0CNT + bg * 2);
  int32_t dx = (int16_t)IO(ppu, REG_BG2PA + (bg - 2) * AFFINE_STRIDE);
  int32_t dy = (int16_t)IO(ppu, REG_BG2PC + (bg - 2) * AFFINE_STRIDE);
  const uint8_t *chars = vram + (((control >> 2) & 3) << 14);
  const uint8_t *map = vram + (((control >> 8) & 31) << 11);
  unsigned shift = 4 + (control >> 14);   /* map width in tiles, log 2 */
  uint32_t mask = (8u << shift) - 1;
  int wrap = control & 0x2000;
  int32_t x, yy;
  int n;

  affine_start(ppu, bg, y, &x, &yy);
  for(n = 0; n < GBA_PPU_WIDTH; n++, x += dx, yy += dy)
  {
    uint32_t px = (uint32_t)(x >> 8), py = (uint32_t)(yy >> 8);

    if(wrap)
    {
      px &= mask;
      py &= mask;
    }
    else if((px | py) > mask)
    {
      ppu->layer[n] = 0;
      continue;
    }
    ppu->layer[n] = chars[map[(py >> 3 << shift) + (px >> 3)] * 64 +
     (py & 7) * 8 + (px & 7)];
  }
  return ppu->layer;
}

/* Modes 3-5: BG2 is a bitmap, transparent outside its edges. */
static const uint16_t *draw_bitmap_bg(GbaPpu *ppu, int mode, int y)
{
  const uint8_t *vram = (const uint8_t *)ppu->vram;
  int32_t dx = (int16_t)IO(ppu, REG_BG2PA);
  int32_t dy = (int16_t)IO(ppu, REG_BG2PC);
  uint32_t width = mode == 5 ? 160 : 240;
  uint32_t height = mode == 5 ? 128 : 160;
  uint32_t base = mode != 3 && (IO(ppu, REG_DISPCNT) & DISPCNT_FRAME) ?
   0xA000 : 0;
  int32_t x, yy;
  int n;

  affine_start(ppu, 2, y, &x, &yy);
  for(n = 0; n < GBA_PPU_WIDTH; n++, x += dx, yy += dy)
  {
    uint32_t px = (uint32_t)(x >> 8), py = (uint32_t)(yy >> 8);

    if(px >= width || py >= height)
      ppu->layer[n] = 0;
    else if(mode == 4)
      ppu->layer[n] = vram[base + py * width + px];
    else
      ppu->layer[n] = PIXEL_DIRECT |
       (ppu->vram[(base >> 1) + py * width + px] & 0x7FFF);
  }
  return ppu->layer;
}

/* Horizontal mosaic: each block of pixels repeats its leftmost one. */
static void mosaic_line(uint16_t *line, int size)
{
  int x, n;

  for(x = 0; x < GBA_PPU_WIDTH; x += size)
    for(n = 1; n < size && x + n < GBA_PPU_WIDTH; n++)
      line[x + n] = line[x];
}

/* One sprite's pixels on line y, into the sprite line where it wins over
   what is there: a lower priority number, or the same one from an earlier
   sprite. */
static void draw_sprite(GbaPpu *ppu, const uint16_t *attr, int y, int top)
{
  const uint8_t *tiles = (const uint8_t *)ppu->vram + 0x10000;
  uint16_t dispcnt = IO(ppu, REG_DISPCNT);
  unsigned a0 = attr[0], a1 = attr[1], a2 = attr[2];
  const uint8_t *size = obj_sizes[(a0 >> 14) * 4 + (a1 >> 14)];
  int width = size[0], height = size[1];
  int box_width = width, box_height = height;
  int left = a1 & 0x1FF;
  int deep = (a0 >> 13) & 1;    /* 8 bits per pixel */
  unsigned tile = a2 & 0x3FF;
  unsigned mode = (a0 >> 10) & 3;
  uint32_t priority = (uint32_t)((a2 >> 10) & 3) << OBJ_PRIO_SHIFT;
  uint32_t pixel = priority | PIXEL_LAYER(4) |
   (mode == 1 ? PIXEL_SEMI : 0) | (deep ? 256 : 256 + ((a2 >> 12) << 4));
  unsigned row_tiles;
  int mosaic_x = 1;
  int32_t u, v, du, dv;
  int x, end, row;

  if(a0 & 0x200 && a0 & 0x100)
  {
    box_width *= 2;
    box_height *= 2;
  }
  if(left >= 256)
    left -= 512;
  row = y - top;
  if(a0 & 0x1000)
  {
    unsigned mosaic = IO(ppu, REG_MOSAIC);

    row -= y % ((mosaic >> 12) + 1);
    if(row < 0)
      row = 0;
    mosaic_x = ((mosaic >> 8) & 15) + 1;
  }

  /* Texture position of the box's left column in 1/256 pixel, and its
     step per screen pixel. */
  if(a0 & 0x100)
  {
    const uint16_t *params = ppu->oam + ((a1 >> 9) & 31) * 16 + 3;
    int32_t pa = (int16_t)params[0], pb = (int16_t)params[4];
    int32_t pc = (int16_t)params[8], pd = (int16_t)params[12];
    int from_centre_y = row - box_height / 2;

    u = (width << 7) - box_width / 2 * pa + from_centre_y * pb;
    v = (height << 7) - box_width / 2 * pc + from_centre_y * pd;
    du = pa;
    dv = pc;
  }
  else
  {
    if(a1 & 0x2000)
      row = height - 1 - row;
    v = row << 8;
    dv = 0;
    u = a1 & 0x1000 ? (width - 1) << 8 : 0;
    du = a1 & 0x1000 ? -256 : 256;
  }

  if(dispcnt & DISPCNT_OBJ_1D)
    row_tiles = (width >> 3) << deep;
  else
  {
    row_tiles = 32;
    if(deep)
      tile &= ~1u;
  }

  x = left < 0 ? 0 : left;
  end = left + box_width > GBA_PPU_WIDTH ? GBA_PPU_WIDTH : left + box_width;
  u += du * (x - left);
  v += dv * (x - left);
  for(; x < end; x++, u += du, v += dv)
  {
    int32_t tu = u, tv = v;
    uint32_t old;
    unsigned address, index;

    if(mosaic_x > 1)
    {
      int back = x % mosaic_x;

      tu -= back * du;
      tv -= back * dv;
    }
    tu >>= 8;
    tv >>= 8;
    index = 0;
    if((uint32_t)tu < (uint32_t)width && (uint32_t)tv < (uint32_t)height)
    {
      address = (tile + (tv >> 3) * row_tiles + ((tu >> 3) << deep)) * 32;
      if(deep)
        index = tiles[(address + (tv & 7) * 8 + (tu & 7)) & 0x7FFF];
      else
        index = tiles[(address + (tv & 7) * 4 + ((tu & 7) >> 1)) & 0x7FFF] >>
         ((tu & 1) * 4) & 15;
    }
    old = ppu->obj[x];
    if(mode == 2)
    {
      if(index)
        ppu->obj[x] = old | OBJ_WINDOW;
    }
    else if(priority < (old & OBJ_PRIO_MASK))
    {
      if(index)
        ppu->obj[x] = (old & OBJ_WINDOW) | pixel | index;
      /* A transparent pixel still lends its priority to an earlier
         sprite's pixel under it (GBATEK warns of the "garbage" this
         makes when OBJ0 has a lower priority than OBJ1). */
      else if(old & OBJ_COLOUR)
        ppu->obj[x] = (old & ~OBJ_PRIO_MASK) | priority;
    }
  }
}

/* The sprite line for line y. Sprites are fetched in OAM order and each
   costs drawing time: its width in cycles, or 10 plus twice the width of
   its box with rotation/scaling. Those that start after the line's budget
   ran out are not shown. */
static void draw_sprites(GbaPpu *ppu, int y)
{
  uint16_t dispcnt = IO(ppu, REG_DISPCNT);
  int budget = dispcnt & DISPCNT_HBLANK_FREE ? 954 : 1210;
  int bitmap = (dispcnt & 7) >= 3;
  int i;

  for(i = 0; i < 128; i++)
  {
    const uint16_t *attr = ppu->oam + i * 4;
    unsigned a0 = attr[0];
    int affine = a0 & 0x100;
    const uint8_t *size;
    int box_width, box_height, top, left;

    /* Without rotation/scaling, bit 9 hides the sprite. */
    if((a0 & 0x300) == 0x200 || (a0 >> 14) == 3)
      continue;
    size = obj_sizes[(a0 >> 14) * 4 + (attr[1] >> 14)];
    box_width = size[0];
    box_height = size[1];
    if(affine && (a0 & 0x200))
    {
      box_width *= 2;
      box_height *= 2;
    }
    /* A sprite reaching below line 255 comes in from the top. */
    top = a0 & 0xFF;
    if(top + box_height > 256)
      top -= 256;
    if(y < top || y >= top + box_height)
      continue;
    /* Sprites wholly past the right edge cost nothing; any other starts
       while time is left and is drawn whole. */
    left = attr[1] & 0x1FF;
    if(left >= GBA_PPU_WIDTH && left + box_width < 512)
      continue;
    if(budget <= 0)
      break;
    budget -= affine ? 10 + box_width * 2 : box_width;
    /* In bitmap modes the lower half of sprite VRAM holds the bitmap. */
    if(bitmap && (attr[2] & 0x3FF) < 512)
      continue;
    draw_sprite(ppu, attr, y, top);
  }
}

/* WINxH and WINxV hold the far edge plus one in the low byte and the near
   edge in the high byte; a near edge past the far one wraps around. */
static int window_covers(uint16_t edges, int position, int limit)
{
  int from = edges >> 8, to = edges & 0xFF;

  if(to > limit)
    to = limit;
  if(from <= to)
    return position >= from && position < to;
  return position >= from || position < to;
}

static void build_window(GbaPpu *ppu, int y, uint16_t dispcnt)
{
  uint8_t *window = ppu->window;
  int i, x;

  if(!(dispcnt & DISPCNT_WINDOWS))
  {
    memset(window, WINDOW_ALL, GBA_PPU_WIDTH);
    return;
  }
  memset(window, IO(ppu, REG_WINOUT) & WINDOW_ALL, GBA_PPU_WIDTH);
  if((dispcnt & (DISPCNT_OBJ_WINDOW | DISPCNT_OBJ)) ==
   (DISPCNT_OBJ_WINDOW | DISPCNT_OBJ))
  {
    uint8_t inside = (IO(ppu, REG_WINOUT) >> 8) & WINDOW_ALL;

    for(x = 0; x < GBA_PPU_WIDTH; x++)
      if(ppu->obj[x] & OBJ_WINDOW)
        window[x] = inside;
  }
  /* Window 1 first, so window 0 ends up in front. */
  for(i = 1; i >= 0; i--)
  {
    uint16_t across = IO(ppu, REG_WIN0H + i * 2);
    uint8_t inside = (IO(ppu, REG_WININ) >> (i * 8)) & WINDOW_ALL;

    if(!(dispcnt & (0x2000 << i)) ||
     !window_covers(IO(ppu, REG_WIN0V + i * 2), y, GBA_PPU_HEIGHT))
      continue;
    for(x = 0; x < GBA_PPU_WIDTH; x++)
      if(window_covers(across, x, GBA_PPU_WIDTH))
        window[x] = inside;
  }
}

/* Lays a background line over what is already there, where its window
   shows it. */
static void lay_background(GbaPpu *ppu, const uint16_t *line, int bg)
{
  uint32_t layer = PIXEL_LAYER(bg);
  unsigned shown = 1u << bg;
  int x;

  for(x = 0; x < GBA_PPU_WIDTH; x++)
    if(line[x] && (ppu->window[x] & shown))
    {
      ppu->below[x] = ppu->top[x];
      ppu->top[x] = line[x] | layer;
    }
}

static void lay_sprites(GbaPpu *ppu, unsigned priority)
{
  int x;

  for(x = 0; x < GBA_PPU_WIDTH; x++)
  {
    uint32_t pixel = ppu->obj[x];

    if(pixel >> OBJ_PRIO_SHIFT == priority &&
     (ppu->window[x] & WINDOW_OBJ))
    {
      ppu->below[x] = ppu->top[x];
      ppu->top[x] = pixel & OBJ_PIXEL;
    }
  }
}

static unsigned colour_of(const GbaPpu *ppu, uint32_t pixel)
{
  if(pixel & PIXEL_DIRECT)
    return pixel & 0x7FFF;
  return ppu->palette[pixel & 0x1FF] & 0x7FFF;
}

/* min(31, (first * eva + second * evb) / 16) for each of red, green and
   blue, weights 0-16. */
static unsigned blend(unsigned first, unsigned second, unsigned eva,
 unsigned evb)
{
  unsigned result = 0, shift;

  for(shift = 0; shift < 15; shift += 5)
  {
    unsigned c = (((first >> shift) & 31) * eva +
     ((second >> shift) & 31) * evb) >> 4;

    result |= (c > 31 ? 31 : c) << shift;
  }
  return result;
}

/* Brightness: each of red, green and blue moves evy/16 of the way to
   white (to = 31) or to black (to = 0). */
static unsigned fade(unsigned colour, unsigned evy, unsigned to)
{
  unsigned result = 0, shift;

  for(shift = 0; shift < 15; shift += 5)
  {
    unsigned c = (colour >> shift) & 31;

    if(to)
      c += ((31 - c) * evy) >> 4;
    else
      c -= (c * evy) >> 4;
    result |= c << shift;
  }
  return result;
}

static unsigned weight(uint16_t value)
{
  value &= 31;
  return value > 16 ? 16 : value;
}

/* The last pass: colour special effects between each column's two front
   pixels, then the palette. */
static void compose(GbaPpu *ppu, uint16_t *row)
{
  uint16_t control = IO(ppu, REG_BLDCNT);
  uint16_t alpha = IO(ppu, REG_BLDALPHA);
  unsigned effect = (control >> 6) & 3;
  uint32_t first = (uint32_t)(control & 0x3F) << 16;
  uint32_t second = (uint32_t)(control & 0x3F00) << 8;
  unsigned eva = weight(alpha), evb = weight(alpha >> 8);
  unsigned evy = weight(IO(ppu, REG_BLDY));
  int x;

  for(x = 0; x < GBA_PPU_WIDTH; x++)
  {
    uint32_t top = ppu->top[x], below = ppu->below[x];
    unsigned colour = colour_of(ppu, top);

    if(ppu->window[x] & WINDOW_EFFECTS)
    {
      /* A semi-transparent sprite blends with any 2nd target behind it,
         whatever the effect; without one it is an ordinary sprite. */
      if(top & PIXEL_SEMI && below & second)
        colour = blend(colour, colour_of(ppu, below), eva, evb);
      else if(top & first)
      {
        if(effect == 1)
        {
          if(below & second)
            colour = blend(colour, colour_of(ppu, below), eva, evb);
        }
        else if(effect >= 2)
          colour = fade(colour, evy, effect == 2);
      }
    }
    row[x] = (uint16_t)colour;
  }
}

void gba_ppu_draw_line(GbaPpu *ppu, int y, uint16_t *row)
{
  uint16_t dispcnt = IO(ppu, REG_DISPCNT);
  int mode = dispcnt & 7;
  unsigned backgrounds = (dispcnt >> 8) & mode_backgrounds[mode];
  unsigned mosaic = (IO(ppu, REG_MOSAIC) & 15) + 1;
  int x, bg, priority;

  if(dispcnt & DISPCNT_BLANK)
  {
    for(x = 0; x < GBA_PPU_WIDTH; x++)
      row[x] = WHITE;
    advance_affine(ppu);
    return;
  }

  for(x = 0; x < GBA_PPU_WIDTH; x++)
    ppu->obj[x] = OBJ_EMPTY;
  if(dispcnt & DISPCNT_OBJ)
    draw_sprites(ppu, y);
  build_window(ppu, y, dispcnt);
  for(x = 0; x < GBA_PPU_WIDTH; x++)
  {
    ppu->top[x] = PIXEL_BACKDROP;
    ppu->below[x] = 0;
  }

  for(priority = 3; priority >= 0; priority--)
  {
    for(bg = 3; bg >= 0; bg--)
    {
      uint16_t control = IO(ppu, REG_BG0CNT + bg * 2);
      const uint16_t *line;

      if(!(backgrounds & (1u << bg)) || (control & 3) != (unsigned)priority)
        continue;
      if(mode >= 3)
        line = draw_bitmap_bg(ppu, mode, y);
      else if(mode == 0 || (mode == 1 && bg < 2))
        line = draw_text_bg(ppu, bg, y);
      else
        line = draw_affine_bg(ppu, bg, y);
      if(control & 0x40 && mosaic > 1)
        mosaic_line((uint16_t *)line, mosaic);
      lay_background(ppu, line, bg);
    }
    if(dispcnt & DISPCNT_OBJ)
      lay_sprites(ppu, priority);
  }
  compose(ppu, row);
  advance_affine(ppu);
}
