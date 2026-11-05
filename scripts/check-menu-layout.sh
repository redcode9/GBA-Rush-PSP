#!/bin/sh

set -eu

# Source-level checks of the main menu in gui.c, which host tests do not
# compile: the category order (the same order test_menu_model.c checks in
# menu_catalog.c), that menu text is drawn through the clipped,
# viewport-bounded helpers, and that the paused game is the background.

expected='MSG_CATEGORY_CONTINUE
MSG_CATEGORY_SAVE_LOAD
MSG_CATEGORY_GAME
MSG_CATEGORY_VIDEO
MSG_CATEGORY_AUDIO
MSG_CATEGORY_PERFORMANCE
MSG_CATEGORY_CONTROLS
MSG_CATEGORY_LINK_CABLE
MSG_CATEGORY_INTERFACE
MSG_CATEGORY_DIAGNOSTICS
MSG_CATEGORY_EXIT'

actual=$(awk '
  /MenuOptionType main_options\[\]/ { inside = 1; next }
  inside && match($0, /MSG\[MSG_CATEGORY_[A-Z_]+\]/) {
    item = substr($0, RSTART + 4, RLENGTH - 5)
    print item
  }
  inside && /^[[:space:]]*};/ { exit }
' source/src/gui.c)

if [ "$actual" != "$expected" ]; then
  echo "Main menu category order is invalid." >&2
  printf '%s\n' "$actual" >&2
  exit 1
fi

if [ "$(printf '%s\n' "$actual" | sort -u | wc -l | tr -d ' ')" -ne 11 ]; then
  echo "Main menu contains a duplicate category." >&2
  exit 1
fi

if ! grep -q 'menu_viewport_baseline(&options_viewport' source/src/gui.c; then
  echo "Menu rows are not constrained by the options viewport." >&2
  exit 1
fi

if grep -q '(display_option->line_number \* FONTHEIGHT) + 28' source/src/gui.c; then
  echo "Menu rows still use unchecked logical coordinates." >&2
  exit 1
fi

if ! grep -q 'print_swap_aware_clipped(MSG\[current_option->help_string\]' source/src/gui.c; then
  echo "Menu footer text is not width limited." >&2
  exit 1
fi

if grep -q 'print_string.*guide_line' source/src/gui.c ||
   grep -q 'print_string_gbk.*guide_line' source/src/gui.c; then
  echo "Contextual guide text bypasses bounded rendering." >&2
  exit 1
fi

# The menu shows the paused game behind its rows, from one top-level
# function that other full-screen cards can reuse.
if ! grep -q '^static void menu_draw_background(void)' source/src/gui.c ||
   ! grep -q '^    menu_draw_background();' source/src/gui.c ||
   ! grep -q 'video_present_paused_frame()' source/src/gui.c; then
  echo "The menu does not draw the paused game behind its rows." >&2
  exit 1
fi

# The main menu row status is drawn every frame: it reads RAM only (the
# state slots are counted when the main menu is entered) and is cut to the
# room left after the label.
status_body=$(awk '
  /^static void menu_draw_main_status\(/ { inside = 1 }
  inside { print }
  inside && /^}/ { exit }
' source/src/gui.c)
if [ -z "$status_body" ] ||
   printf '%s\n' "$status_body" |
    grep -qE 'sceIo|savestate_file_exists|get_savestate_info' ||
   ! printf '%s\n' "$status_body" | grep -q 'menu_layout_status_columns' ||
   ! grep -q 'menu_draw_main_status(display_option->display_msg' \
    source/src/gui.c; then
  echo "The main menu row status is missing, unbounded or reads files." >&2
  exit 1
fi

echo "Menu layout checks passed"
