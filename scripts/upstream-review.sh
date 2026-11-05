#!/bin/sh

set -eu

# Lists the libretro/gpsp commits after the last reviewed one that touch the
# code docs/UPSTREAM.md tracks (the core, serial, rfu.c, bios/, gba_over.h
# and tests/), one per line: short hash, date, subject.
#   sh scripts/upstream-review.sh CLONE [LAST]
# CLONE is the top directory of a local clone of libretro/gpsp with its
# history, updated first: the list ends at its HEAD. LAST defaults to
# the "Last reviewed upstream commit" line of docs/UPSTREAM.md.

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  echo "Usage: $0 CLONE [LAST_REVIEWED_COMMIT]" >&2
  exit 1
fi

clone=$1
register="$(dirname "$0")/../docs/UPSTREAM.md"

if [ "$#" -eq 2 ]; then
  last=$2
else
  if [ ! -f "$register" ]; then
    echo "Missing register: $register" >&2
    exit 1
  fi
  last=$(sed -n 's/^- Last reviewed upstream commit: `\([0-9a-f]*\)`.*/\1/p' \
    "$register")
  if [ -z "$last" ]; then
    echo "No 'Last reviewed upstream commit' line in $register" >&2
    exit 1
  fi
fi

# The paths below are relative to CLONE: a subdirectory would list nothing.
if [ ! -d "$clone" ] ||
  ! prefix=$(git -C "$clone" rev-parse --show-prefix 2>/dev/null) ||
  [ -n "$prefix" ]
then
  echo "Not the top directory of a git clone of libretro/gpsp: $clone" >&2
  exit 1
fi

if ! git -C "$clone" rev-parse --verify --quiet "$last^{commit}" >/dev/null
then
  echo "Commit $last is not in $clone (fetch the full history, or pass" \
    "the last reviewed commit)" >&2
  exit 1
fi

git -C "$clone" log --format='%h %ad %s' --date=short "$last..HEAD" -- \
  'cpu*' gba_memory.c video.cc sound.c 'serial*.c' rfu.c bios/ gba_over.h \
  tests/
