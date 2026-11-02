/* Platform-independent menu layout maths used by gui.c: scrolling the
   option list so the selection stays visible, and clipping or scrolling
   double-byte text by screen columns. */

#include "menu_layout.h"

#include <limits.h>

/* Scroll only as far as needed to keep selected_line visible; once the list
   scrolls, the selection sits on the last row. */
MenuViewport menu_viewport_for_selection(uint32_t selected_line,
 uint32_t visible_lines)
{
  MenuViewport viewport;

  viewport.line_count = visible_lines;
  viewport.first_line = 0;
  if(visible_lines != 0 && selected_line >= visible_lines)
    viewport.first_line = selected_line - visible_lines + 1;

  return viewport;
}

/* Screen y of logical_line; returns 0 (and leaves *baseline alone) when the
   line is outside the viewport or the result would overflow. */
int menu_viewport_baseline(const MenuViewport *viewport,
 uint32_t logical_line, uint32_t first_baseline, uint32_t line_height,
 uint32_t *baseline)
{
  uint32_t visible_line;

  if(viewport == NULL || baseline == NULL || viewport->line_count == 0 ||
     logical_line < viewport->first_line)
    return 0;

  visible_line = logical_line - viewport->first_line;
  if(visible_line >= viewport->line_count ||
     (line_height != 0 && visible_line >
      (UINT32_MAX - first_baseline) / line_height))
    return 0;

  *baseline = first_baseline + visible_line * line_height;
  return 1;
}

size_t menu_layout_max_columns(uint32_t screen_width, uint32_t start_x,
 uint32_t glyph_width)
{
  if(glyph_width == 0 || start_x >= screen_width)
    return 0;

  return (screen_width - start_x) / glyph_width;
}

/* Length of the character at text: 2 for a valid lead/trail pair in the
   given encoding (Shift-JIS lead 0x81-0x9F or 0xE0-0xEF, GBK lead
   0x81-0xFE), otherwise 1. Every byte takes one font column, so a
   double-byte character is two columns wide. */
static size_t text_character_bytes(const char *text,
 MenuTextEncoding encoding)
{
  unsigned char lead = (unsigned char)text[0];
  unsigned char trail = (unsigned char)text[1];
  int double_byte = encoding == MENU_TEXT_SHIFT_JIS ?
   ((lead >= 0x81 && lead <= 0x9F) || (lead >= 0xE0 && lead <= 0xEF)) :
   (lead >= 0x81 && lead <= 0xFE);

  return double_byte && trail >= 0x40 && trail != 0x7F &&
   trail <= (encoding == MENU_TEXT_SHIFT_JIS ? 0xFC : 0xFE) ? 2 : 1;
}

/* Bytes of text that fit in max_columns without splitting a character. */
size_t menu_text_clip_length_for_encoding(const char *text,
 size_t max_columns, MenuTextEncoding encoding)
{
  size_t bytes = 0;
  size_t columns = 0;

  if(text == NULL)
    return 0;

  while(text[bytes] != '\0')
  {
    const size_t char_bytes = text_character_bytes(text + bytes, encoding);
    const size_t char_columns = char_bytes;

    if(char_columns > max_columns - columns)
      break;

    bytes += char_bytes;
    columns += char_columns;
  }

  return bytes;
}

size_t menu_text_clip_length(const char *text, size_t max_columns)
{
  return menu_text_clip_length_for_encoding(text, max_columns, MENU_TEXT_GBK);
}

/* Byte offset at which to start drawing text scrolled left by column_offset
   columns, never starting inside a double-byte character. */
size_t menu_text_scroll_start(const char *text, size_t column_offset,
 MenuTextEncoding encoding)
{
  size_t bytes = 0;

  if(text == NULL)
    return 0;

  while(text[bytes] != '\0' && column_offset != 0)
  {
    const size_t char_bytes = text_character_bytes(text + bytes, encoding);

    if(char_bytes > column_offset)
      break;
    bytes += char_bytes;
    column_offset -= char_bytes;
  }

  return bytes;
}
