#include "menu_layout.h"

#include <limits.h>

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

size_t menu_text_clip_length(const char *text, size_t max_columns)
{
  size_t bytes = 0;
  size_t columns = 0;

  if(text == NULL)
    return 0;

  while(text[bytes] != '\0')
  {
    const unsigned char current = (unsigned char)text[bytes];
    const size_t char_bytes = current >= 0x80 && text[bytes + 1] != '\0' ?
     2 : 1;
    const size_t char_columns = char_bytes;

    if(char_columns > max_columns - columns)
      break;

    bytes += char_bytes;
    columns += char_columns;
  }

  return bytes;
}
