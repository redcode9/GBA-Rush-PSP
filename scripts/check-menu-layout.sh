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

echo "Menu layout checks passed"
