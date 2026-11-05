#!/bin/sh

set -eu

# Lists the libretro/gpsp commits after the last reviewed one that touch the
# code docs/UPSTREAM.md tracks (the core, serial, rfu.c, bios/, gba_over.h
# and tests/), one per line: short hash, date, subject. Then it prints on
# stderr the full hash of the clone's HEAD, where the list ends.
#   sh scripts/upstream-review.sh CLONE [LAST]
# CLONE is the top directory of a local clone of libretro/gpsp with its
# history, updated first. LAST defaults to the "Last reviewed upstream
# commit" line of docs/UPSTREAM.md.
#   sh scripts/upstream-review.sh --check-age [--today YYYY-MM-DD]
# warns, and still exits 0, when the "Last review" date of docs/UPSTREAM.md
# is more than 45 days before today: the review is monthly (CONTRIBUTING.md).

usage() {
  echo "Usage: $0 CLONE [LAST_REVIEWED_COMMIT]" >&2
  echo "       $0 --check-age [--today YYYY-MM-DD]" >&2
  exit 1
}

register="$(dirname "$0")/../docs/UPSTREAM.md"

# Prints the day number of a YYYY-MM-DD date, or fails on any other text.
day_number() {
  case $1 in
    [12][0-9][0-9][0-9]-[01][0-9]-[0-3][0-9]) ;;
    *) return 1 ;;
  esac
  y=${1%%-*} m=${1#*-} d=${1##*-}
  m=${m%-*}
  m=${m#0} d=${d#0}
  # shortcut: a day past the month's end (02-30) counts into the next
  # month; check month lengths if a register ever needs that.
  [ "$m" -ge 1 ] && [ "$m" -le 12 ] && [ "$d" -ge 1 ] && [ "$d" -le 31 ] ||
    return 1
  # Days from a fixed epoch, counting March as the first month of the year.
  a=$(( (14 - m) / 12 ))
  y=$(( y + 4800 - a )) m=$(( m + 12 * a - 3 ))
  echo $(( d + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 ))
}

if [ "${1-}" = --check-age ]; then
  today=$(date +%Y-%m-%d)
  if [ "$#" -eq 3 ] && [ "$2" = --today ]; then
    today=$3
  elif [ "$#" -ne 1 ]; then
    usage
  fi
  if [ ! -f "$register" ]; then
    echo "Missing register: $register" >&2
    exit 1
  fi
  reviewed=$(sed -n 's/^- Last review: //p' "$register" | tr -d '\r')
  if ! from=$(day_number "$reviewed"); then
    echo "No 'Last review: YYYY-MM-DD' line in $register: '$reviewed'" >&2
    exit 1
  fi
  if ! to=$(day_number "$today"); then
    echo "Not a YYYY-MM-DD date: '$today'" >&2
    exit 1
  fi
  age=$(( to - from ))
  if [ "$age" -gt 45 ]; then
    echo "Warning: the last upstream review, on $reviewed, is $age days" \
      "old (more than 45). Run the monthly review (CONTRIBUTING.md)." >&2
  fi
  exit 0
fi

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
  usage
fi

clone=$1

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

head=$(git -C "$clone" rev-parse --verify HEAD)
git -C "$clone" log --format='%h %ad %s' --date=short "$last..$head" -- \
  'cpu*' gba_memory.c video.cc sound.c 'serial*.c' rfu.c bios/ gba_over.h \
  tests/
echo "Reviewed up to $head" >&2
