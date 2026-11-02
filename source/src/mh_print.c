/*****************************************************************************
 * Double-byte string drawing library     mediumgauge
 *****************************************************************************/

/* Draws Shift-JIS text (the Japanese menus) with the 6x12 Shinonome bitmap
 * font into a 16-bit pixel buffer. Reached through print_string() and
 * print_string_ext() in psp_video.c; ch_print.c is the GBK twin. */

#include "common.h"

#include "shinonomefont.c"

/*****************************************************************************
* prototypes
*****************************************************************************/
static void draw_char(s32 chr, u16 x, u16 y, u16 col, s16 bg_col, u16 *base_vram, u16 bufferwidth);

static s32 idx_hankaku(u8 ch);
static s32 idx_zenkaku(u8 hi, u8 lo);

static u8 iskanji_hi(u8 c);
static u8 iskanji_lo(u8 c);

/*****************************************************************************
* Copy one glyph to VRAM. Glyphs are 6x12 1-bit bitmaps packed MSB first,
* 9 bytes each; a negative bg_col leaves unset pixels alone (BG_NO_FILL).
*****************************************************************************/
static void draw_char(s32 chr, u16 x, u16 y, u16 col, s16 bg_col, u16 *base_vram, u16 bufferwidth)
{
  u32 dx, dy;
  u16 *vptr0, *vptr;
  u8 bit, bitcnt;

  bitcnt = 0;
  bit = 0;
  chr *= 9;

  vptr0 = base_vram + (x + (y * bufferwidth));

  for (dy = 0 ; dy < FONTHEIGHT ; dy++)
  {
    vptr = vptr0;

    for (dx = 0 ; dx < FONTWIDTH ; dx++)
    {
      bitcnt >>= 1;

      if (bitcnt == 0)
      {
        bit = shinonomefont[chr++];
        bitcnt = 0x80;
      }

      if ((bit & bitcnt) != 0)
      {
        *vptr = col;
      }
      else if (bg_col >= 0)
      {
        *vptr = bg_col;
      }

      vptr++;
    }

    vptr0 += bufferwidth;
  }
}

/*****************************************************************************
* Glyph index of a single-byte character (unknown ones map to glyph 0)
*****************************************************************************/
static s32 idx_hankaku(u8 ch)
{
  // 0x20 - 0x7e (ASCII characters)
  if ((ch >= 0x20) && (ch <= 0x7e))
  {
    return (ch - 0x20);
  }

  // 0xA1 - 0xDF (half-width katakana)
  if ((ch >= 0xA1) && (ch <= 0xDF))
  {
    return (ch - 0x40);
  }

  // 0xF0 - 0xFD (icons: battery 0xF0 - 0xF7, menu titles 0xF8 - 0xFA)
  if ((ch >= 0xF0) && (ch <= 0xFD))
  {
    return (ch - 0x50);
  }

  return 0;
}

/*****************************************************************************
* Glyph index of the left half of a double-byte character; the right half
* is the next glyph
*****************************************************************************/
static s32 idx_zenkaku(u8 hi, u8 lo)
{
  s32 idx;
  static const s32 codetbl[] =
  {
    -1,  0,  1,  2,  3, -1, -1,  4,  5,  6,  7,  8,  9, 10, 11, 12,  /* 0x80 */
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,  /* 0x90 */
    29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, -1, -1, 40, 41, -1   /* 0xe0 */
  };

  /* Table row from the lead byte */
  idx = codetbl[hi & 0x3F];

  /* Lead bytes not in the table draw as a full-width space */
  if (idx == -1)
  {
    return (16 * 11);
  }

  idx = (16 * 11)           /* skip the 176 single-byte glyphs */
      + (idx * 2 * 192)     /* 192 characters per codetbl row */
      + ((lo - 0x40) << 1);

  return idx;
}

/*****************************************************************************
* Draw a string at (x, y). '\n' starts a new line at x; drawing stops at
* the right edge of the 480-pixel screen
*****************************************************************************/
void mh_print(const char *str, u16 x, u16 y, u16 col, s16 bg_col, u16 *base_vram, u16 bufferwidth)
{
  u16 dx, dy;
  u8 ch1, ch2;
  u8 lead_byte = 0;   // lead byte waiting for its trail byte
  s32 idx;

  dx = x;
  dy = y;

  while (*str != 0)
  {
    ch1 = *str++;
    ch2 = *str;

    if (ch1 == '\n')
    {
      dx = x;
      dy += FONTHEIGHT;
      lead_byte = 0;
      continue;
    }

    if (lead_byte != 0)
    {
      if (dx > (PSP_SCREEN_WIDTH - (FONTWIDTH * 2)))
        goto draw_end;

      idx = idx_zenkaku(lead_byte, ch1);
      draw_char(idx,     dx,             dy, col, bg_col, base_vram, bufferwidth);
      draw_char(idx + 1, dx + FONTWIDTH, dy, col, bg_col, base_vram, bufferwidth);

      dx += (FONTWIDTH * 2);
      lead_byte = 0;
    }
    else
    {
      if ((iskanji_hi(ch1) != 0) && (iskanji_lo(ch2) != 0))
      {
        lead_byte = ch1;
      }
      else
      {
        if (dx > (PSP_SCREEN_WIDTH - FONTWIDTH))
          goto draw_end;

        idx = idx_hankaku(ch1);
        draw_char(idx, dx, dy, col, bg_col, base_vram, bufferwidth);

        dx += FONTWIDTH;
      }
    }
  } /* while */

  draw_end:;
}

/*****************************************************************************
* Is c a Shift-JIS lead byte?  0x81-0x9F, 0xE0-0xEF
*****************************************************************************/
static u8 iskanji_hi(u8 c)
{
  return (((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xe0) && (c <= 0xeF)));
}

/*****************************************************************************
* Is c a Shift-JIS trail byte?  0x40-0x7E, 0x80-0xFC
*****************************************************************************/
static u8 iskanji_lo(u8 c)
{
  return (((c >= 0x40) && (c <= 0x7e)) || ((c >= 0x80) && (c <= 0xFC)));
}

