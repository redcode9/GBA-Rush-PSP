#ifndef MENU_LAYOUT_H
#define MENU_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
  uint32_t first_line;
  uint32_t line_count;
} MenuViewport;

MenuViewport menu_viewport_for_selection(uint32_t selected_line,
 uint32_t visible_lines);
int menu_viewport_baseline(const MenuViewport *viewport,
 uint32_t logical_line, uint32_t first_baseline, uint32_t line_height,
 uint32_t *baseline);
size_t menu_layout_max_columns(uint32_t screen_width, uint32_t start_x,
 uint32_t glyph_width);
size_t menu_text_clip_length(const char *text, size_t max_columns);

#endif
