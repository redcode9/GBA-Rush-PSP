#!/bin/sh

set -eu

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

echo "Menu layout checks passed"
