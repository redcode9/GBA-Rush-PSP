/*
 * Host tests for gba_ppu.c, the scanline renderer: a few hand-built
 * scenes checked against GBATEK's rules. make check-render compares it with
 * mGBA on whole games.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "gba_ppu.h"

static GbaPpu ppu;
static GbaPpuMemory copy;
static uint16_t row[GBA_PPU_WIDTH];

static void reset(void)
{
  memset(&copy, 0, sizeof(copy));
  gba_ppu_init(&ppu, copy.palette, copy.oam, copy.vram, copy.io);
}

static void io(uint32_t offset, uint16_t value)
{
  gba_ppu_write_io(&ppu, offset, value);
}

static void palette(unsigned index, uint16_t colour)
{
  gba_ppu_write_palette(&ppu, index * 2, &colour, 2);
}

/* BG0 in mode 0: a 32x32 map at 0xF800 full of tile 1, whose pixels are
   colour 1 of palette 0, except column 0 of each row, colour 2. */
static void text_scene(void)
{
  uint16_t entry = 1;
  uint8_t pixels[4] = { 0x12, 0x11, 0x11, 0x11 };
  unsigned i;

  reset();
  for(i = 0; i < 1024; i++)
    gba_ppu_write_vram(&ppu, 0xF800 + i * 2, &entry, 2);
  for(i = 0; i < 8; i++)
    gba_ppu_write_vram(&ppu, 32 + i * 4, pixels, 4);
  palette(0, 0x7FFF);
  palette(1, 0x001F);
  palette(2, 0x03E0);
  io(0x08, 0x1F00);
  io(0x00, 0x0100);
  gba_ppu_start_frame(&ppu);
}

static void test_forced_blank_is_white(void)
{
  int x;

  text_scene();
  io(0x00, 0x0180);
  gba_ppu_draw_line(&ppu, 0, row);
  for(x = 0; x < GBA_PPU_WIDTH; x++)
    assert(row[x] == 0x7FFF);
}

static void test_text_background_scrolls_and_flips(void)
{
  uint16_t flipped = 0x0401;

  text_scene();
  gba_ppu_draw_line(&ppu, 0, row);
  assert(row[0] == 0x03E0 && row[1] == 0x001F && row[8] == 0x03E0);
  io(0x10, 3);
  gba_ppu_draw_line(&ppu, 1, row);
  assert(row[4] == 0x001F && row[5] == 0x03E0);
  /* Horizontal flip moves column 0 to the tile's right edge. */
  io(0x10, 0);
  gba_ppu_write_vram(&ppu, 0xF800, &flipped, 2);
  gba_ppu_draw_line(&ppu, 2, row);
  assert(row[6] == 0x001F && row[7] == 0x03E0 && row[8] == 0x03E0);
}

/* GBATEK: I = min(31, (I1 * EVA + I2 * EVB) / 16), I + (31 - I) * EVY / 16
   and I - I * EVY / 16, coefficients above 16 counting as 16. */
static void test_colour_effects_follow_gbatek(void)
{
  unsigned c, k;

  for(c = 0; c < 32; c++)
    for(k = 0; k < 20; k++)
    {
      unsigned w = k > 16 ? 16 : k;
      unsigned grey = c | c << 5 | c << 10;
      unsigned expected;

      text_scene();
      palette(1, (uint16_t)grey);
      palette(0, 0x2108);                 /* backdrop, 8 in every channel */
      io(0x50, 0x2041);                   /* BG0 over backdrop, alpha */
      io(0x52, (uint16_t)(k | (16 - w) << 8));
      gba_ppu_draw_line(&ppu, 0, row);
      expected = (c * w + 8 * (16 - w)) / 16;
      expected = expected > 31 ? 31 : expected;
      assert(row[1] == (expected | expected << 5 | expected << 10));

      io(0x50, 0x0081);
      io(0x54, (uint16_t)k);
      gba_ppu_draw_line(&ppu, 1, row);
      expected = c + (31 - c) * w / 16;
      assert(row[1] == (expected | expected << 5 | expected << 10));

      io(0x50, 0x00C1);
      gba_ppu_draw_line(&ppu, 2, row);
      expected = c - c * w / 16;
      assert(row[1] == (expected | expected << 5 | expected << 10));
    }
}

/* Mode 3 shows the source line the internal reference point is at: it is
   reloaded at V-Blank and when BG2Y is written, moves on by PD after each
   line that draws BG2, and stands still while BG2 is off or still hidden
   after being switched back on (two lines in bitmap modes). */
static void test_affine_reference_points(void)
{
  uint16_t y;

  reset();
  for(y = 0; y < 160; y++)
    gba_ppu_write_vram(&ppu, (uint32_t)y * 480, &y, 2);
  io(0x20, 0x100);
  io(0x26, 0x200);                        /* two source lines per line */
  io(0x2C, 0x0500);                       /* start at source line 5 */
  io(0x00, 0x0403);
  gba_ppu_start_frame(&ppu);
  gba_ppu_draw_line(&ppu, 0, row);
  assert(row[0] == 5);
  gba_ppu_draw_line(&ppu, 1, row);
  assert(row[0] == 7);
  io(0x00, 0x0003);
  gba_ppu_draw_line(&ppu, 2, row);
  gba_ppu_draw_line(&ppu, 3, row);
  io(0x00, 0x0403);
  gba_ppu_draw_line(&ppu, 4, row);
  gba_ppu_draw_line(&ppu, 5, row);
  assert(row[0] == 0);
  gba_ppu_draw_line(&ppu, 6, row);
  assert(row[0] == 9);
  io(0x2C, 0x1400);
  gba_ppu_draw_line(&ppu, 7, row);
  assert(row[0] == 20);
  gba_ppu_start_frame(&ppu);
  gba_ppu_draw_line(&ppu, 0, row);
  assert(row[0] == 20);
}

/* A background switched on mid-frame appears three lines later in tile
   modes; switched on during V-Blank it is there at once. */
static void test_background_enable_delay(void)
{
  text_scene();
  io(0x00, 0x0000);
  gba_ppu_draw_line(&ppu, 0, row);
  io(0x00, 0x0100);
  gba_ppu_draw_line(&ppu, 1, row);
  assert(row[1] == 0x7FFF);
  gba_ppu_draw_line(&ppu, 2, row);
  gba_ppu_draw_line(&ppu, 3, row);
  assert(row[1] == 0x7FFF);
  gba_ppu_draw_line(&ppu, 4, row);
  assert(row[1] == 0x001F);
  io(0x00, 0x0000);
  gba_ppu_start_frame(&ppu);
  io(0x00, 0x0100);
  gba_ppu_draw_line(&ppu, 0, row);
  assert(row[1] == 0x001F);
}

/* A semi-transparent sprite blends with a 2nd target behind it even
   without an effect selected; an opaque one in front of it wins. */
static void test_semi_transparent_sprite(void)
{
  static const uint16_t sprite[3] = { 0x0400, 0x0000 | 16, 0x0000 };
  uint8_t solid[32];

  text_scene();
  memset(solid, 0x33, sizeof(solid));
  gba_ppu_write_vram(&ppu, 0x10000, solid, sizeof(solid));
  gba_ppu_write_oam(&ppu, 0, sprite, sizeof(sprite));
  palette(256 + 3, 0x7C00);
  io(0x00, 0x1140);
  io(0x50, 0x0100);
  io(0x52, 0x0808);
  gba_ppu_draw_line(&ppu, 0, row);
  assert(row[17] == (0x7C00 / 2 & 0x7C00) + 0x001F / 2);
  assert(row[30] == 0x001F);
}

/* The emulator's way: the renderer reads the emulator's arrays, which the
   game's writes reach directly, and hears only of writes to DISPCNT and the
   reference points, after they were stored. */
static void test_emulator_memory(void)
{
  static uint16_t palette_ram[0x200], oam_ram[0x200], io_registers[0x400];
  static uint16_t vram[0x18000 / 2];
  uint16_t y;

  gba_ppu_init(&ppu, palette_ram, oam_ram, vram, io_registers);
  for(y = 0; y < 160; y++)
    vram[y * 240] = y;
  io_registers[0x20 >> 1] = 0x100;
  io_registers[0x26 >> 1] = 0x100;
  io_registers[0x2C >> 1] = 0x0300;       /* start at source line 3 */
  gba_ppu_write_io(&ppu, 0x2C, io_registers[0x2C >> 1]);
  io_registers[0] = 0x0403;
  gba_ppu_write_io(&ppu, 0x00, io_registers[0]);
  gba_ppu_draw_line(&ppu, 0, row);
  assert(row[0] == 3);
  gba_ppu_draw_line(&ppu, 1, row);
  assert(row[0] == 4);
  vram[10 * 240] = 0x7C1F;
  io_registers[0x2C >> 1] = 0x0A00;
  gba_ppu_write_io(&ppu, 0x2C, io_registers[0x2C >> 1]);
  gba_ppu_draw_line(&ppu, 2, row);
  assert(row[0] == 0x7C1F);
  palette_ram[0] = 0x8123;                /* backdrop; bit 15 is ignored */
  io_registers[0] = 0x0003;
  gba_ppu_write_io(&ppu, 0x00, io_registers[0]);
  gba_ppu_draw_line(&ppu, 3, row);
  assert(row[0] == 0x0123);
}

int main(void)
{
  test_forced_blank_is_white();
  test_text_background_scrolls_and_flips();
  test_colour_effects_follow_gbatek();
  test_affine_reference_points();
  test_background_enable_delay();
  test_semi_transparent_sprite();
  test_emulator_memory();
  return 0;
}
