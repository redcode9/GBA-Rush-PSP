/*
 * Host test for the GBK menu font: the Italian table writes its accented
 * vowels as GBK characters, so each one must have a drawn glyph pair in
 * gbkfont.c at the index ch_print.c computes for it.
 */

#include <assert.h>
#include <stdio.h>

#include "gbkfont.c"

#define GLYPH_BYTES 9   /* 6x12 pixels, 1 bit each */
#define GLYPH_COLUMNS 6
#define FALLBACK_GLYPH (16 * 10)

/* The formula of idx_zenkaku() in ch_print.c: 160 single-byte glyphs, then
   192 two-glyph characters per lead byte; lead bytes 0x81-0xFE are rows
   0-125. Returns the glyph of the left half; the right half is the next. */
static long gbk_glyph_index(unsigned char hi, unsigned char lo)
{
  long row = (long)(hi & 0x7F) - 1;

  if(row < 0 || row > 125)
    return FALLBACK_GLYPH;

  return FALLBACK_GLYPH + (row * 2 * 192) + ((lo - 0x40) << 1);
}

/* Pixel (x, y) of a character's 12x12 glyph pair. */
static int glyph_pixel(long glyph, int x, int y)
{
  long bit;

  glyph += x / GLYPH_COLUMNS;
  bit = (long)y * GLYPH_COLUMNS + x % GLYPH_COLUMNS;

  return (((unsigned char)gbkfont[glyph * GLYPH_BYTES + bit / 8] << (bit % 8))
   & 0x80) != 0;
}

static int rows_have_pixels(long glyph, int first_row, int last_row)
{
  int x;
  int y;

  for(y = first_row; y <= last_row; y++)
    for(x = 0; x < 2 * GLYPH_COLUMNS; x++)
      if(glyph_pixel(glyph, x, y))
        return 1;

  return 0;
}

/* GB2312 row A8 holds the pinyin vowels; these are the Italian ones. */
static void test_italian_vowels_have_accented_glyphs(void)
{
  /* a grave, e grave, e acute, i grave, o grave, u grave */
  static const unsigned char vowels[][2] =
  {
    { 0xA8, 0xA4 }, { 0xA8, 0xA8 }, { 0xA8, 0xA6 },
    { 0xA8, 0xAC }, { 0xA8, 0xB0 }, { 0xA8, 0xB4 },
  };
  size_t i;

  for(i = 0; i < sizeof(vowels) / sizeof(vowels[0]); i++)
  {
    long glyph = gbk_glyph_index(vowels[i][0], vowels[i][1]);

    assert(glyph != FALLBACK_GLYPH);
    assert((size_t)(glyph + 2) * GLYPH_BYTES <= sizeof(gbkfont));
    /* An accent over a lowercase letter: blank top rows, the accent, then
       the letter's body. */
    assert(!rows_have_pixels(glyph, 0, 1));
    assert(rows_have_pixels(glyph, 2, 4));
    assert(rows_have_pixels(glyph, 5, 9));
  }
}

int main(void)
{
  test_italian_vowels_have_accented_glyphs();
  puts("GBK glyph tests passed");
  return 0;
}
