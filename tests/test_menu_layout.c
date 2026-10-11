#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "menu_layout.h"

static void test_top_of_menu(void)
{
  MenuViewport viewport = menu_viewport_for_selection(0, 15);
  uint32_t baseline = 0;

  assert(viewport.first_line == 0);
  assert(menu_viewport_baseline(&viewport, 0, 28, 12, &baseline) == 1);
  assert(baseline == 28);
  assert(menu_viewport_baseline(&viewport, 14, 28, 12, &baseline) == 1);
  assert(baseline == 196);
  assert(menu_viewport_baseline(&viewport, 15, 28, 12, &baseline) == 0);
}

static void test_bottom_selection_scrolls(void)
{
  MenuViewport viewport = menu_viewport_for_selection(15, 15);
  uint32_t baseline = 0;

  assert(viewport.first_line == 1);
  assert(menu_viewport_baseline(&viewport, 0, 28, 12, &baseline) == 0);
  assert(menu_viewport_baseline(&viewport, 1, 28, 12, &baseline) == 1);
  assert(baseline == 28);
  assert(menu_viewport_baseline(&viewport, 15, 28, 12, &baseline) == 1);
  assert(baseline == 196);
}

static void test_sparse_and_invalid_rows(void)
{
  MenuViewport viewport = menu_viewport_for_selection(30, 15);
  MenuViewport empty = menu_viewport_for_selection(UINT32_MAX, 0);
  uint32_t baseline = 0;

  assert(viewport.first_line == 16);
  assert(menu_viewport_baseline(&viewport, 15, 28, 12, &baseline) == 0);
  assert(menu_viewport_baseline(&viewport, 18, 28, 12, &baseline) == 1);
  assert(baseline == 52);
  assert(menu_viewport_baseline(&empty, UINT32_MAX, 28, 12,
                                &baseline) == 0);
  assert(menu_viewport_baseline(NULL, 0, 28, 12, &baseline) == 0);
}

static void test_text_bounds(void)
{
  static const char double_byte[] = { (char)0xA1, (char)0xF0,
                                      'A', 'B', 'C', '\0' };

  assert(menu_layout_max_columns(480, 30, 6) == 75);
  assert(menu_layout_max_columns(480, 480, 6) == 0);
  assert(menu_layout_max_columns(480, 30, 0) == 0);
  assert(menu_text_clip_length("abcdef", 4) == 4);
  assert(menu_text_clip_length("abc", 10) == 3);
  assert(menu_text_clip_length(double_byte, 1) == 0);
  assert(menu_text_clip_length(double_byte, 2) == 2);
  assert(menu_text_clip_length(double_byte, 3) == 3);
  assert(menu_text_clip_length(NULL, 3) == 0);
  assert(menu_text_scroll_start(double_byte, 0, MENU_TEXT_GBK) == 0);
  assert(menu_text_scroll_start(double_byte, 1, MENU_TEXT_GBK) == 0);
  assert(menu_text_scroll_start(double_byte, 2, MENU_TEXT_GBK) == 2);
  assert(menu_text_scroll_start(double_byte, 3, MENU_TEXT_GBK) == 3);
  assert(menu_text_scroll_start(double_byte, 99, MENU_TEXT_GBK) == 5);
  assert(menu_text_scroll_start(NULL, 3, MENU_TEXT_GBK) == 0);
  {
    const char japanese[] = { (char)0xB1, (char)0x82, (char)0xA0, 'A', 0 };
    assert(menu_text_clip_length_for_encoding(japanese, 1,
     MENU_TEXT_SHIFT_JIS) == 1);
    assert(menu_text_clip_length_for_encoding(japanese, 2,
     MENU_TEXT_SHIFT_JIS) == 1);
    assert(menu_text_clip_length_for_encoding(japanese, 3,
     MENU_TEXT_SHIFT_JIS) == 3);
    assert(menu_text_scroll_start(japanese, 2, MENU_TEXT_SHIFT_JIS) == 1);
    assert(menu_text_scroll_start(japanese, 3, MENU_TEXT_SHIFT_JIS) == 3);
  }
}

int main(void)
{
  test_top_of_menu();
  test_bottom_selection_scrolls();
  test_sparse_and_invalid_rows();
  test_text_bounds();
  puts("menu layout tests passed");
  return 0;
}
