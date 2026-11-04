#!/bin/sh
set -eu

# Draws real games with both mGBA's renderer and GBA Rush's gba_ppu and
# checks that every pixel of every frame matches, gba_ppu drawing also from
# the write log the Media Engine reads, on a second thread. Skipped unless MGBA_DIR
# and at least one game are set:
#   MGBA_DIR  an mGBA 0.10 source tree with libmgba.a built in build16/ or
#             build/. Build build16/ with -DCMAKE_C_FLAGS=-DCOLOR_16_BIT:
#             it mixes colours in the GBA's 5-bit steps, where the usual
#             8-bit build rounds colour effects differently from the GBA.
#   BIOS      optional GBA BIOS, used for every game
#   MKSC      Mario Kart: Super Circuit (European release): menus and a race
#   FIRERED and FIRERED_SAVE   Pokemon FireRed and a save: the walk of
#             tests/rom_link/firered_trade.txt from the title screen
#   LEAFGREEN Pokemon LeafGreen: opening and new game
#   RENDER_ROMS  more ROM:SCRIPT pairs, such as test ROMs
# MGBA_DIR defaults to what scripts/setup-dev.sh put in ext/, and with no
# game set RENDER_ROMS to the free test ROMs it fetched. With --quick (make
# check-render-quick, for every pull request) each game runs its first 300
# frames.
# The first frame that differs in each game is saved as
# build/render/<game>.bmp, mGBA's picture on the left.

if [ -z "${MGBA_DIR+set}" ] && [ -d ext/mgba-0.10.3 ]; then
  MGBA_DIR=ext/mgba-0.10.3
fi
if [ -z "${MGBA_DIR:-}" ]; then
  echo "Render check skipped: no mGBA (MGBA_DIR or sh scripts/setup-dev.sh mgba)"
  exit 0
fi
tests=ext/test-roms/gba-tests
if [ -z "${MKSC:-}${FIRERED:-}${LEAFGREEN:-}${RENDER_ROMS:-}" ] &&
  [ -d "$tests" ]; then
  RENDER_ROMS=
  for rom in arm/arm thumb/thumb memory/memory ppu/hello ppu/shades \
    ppu/stripes; do
    RENDER_ROMS="$RENDER_ROMS $tests/$rom.gba:tests/render/still.txt"
  done
fi
if [ -z "${MKSC:-}${FIRERED:-}${LEAFGREEN:-}${RENDER_ROMS:-}" ]; then
  echo "Render check skipped: set MKSC, FIRERED, LEAFGREEN or RENDER_ROMS"
  exit 0
fi

absolute()
{
  case "$1" in
    /*) echo "$1" ;;
    *) echo "$PWD/$1" ;;
  esac
}

build=$MGBA_DIR/build16
color=-DCOLOR_16_BIT
if [ ! -f "$build/libmgba.a" ]; then
  build=$MGBA_DIR/build
  color=
fi
out=${OUT:-build/render}
mkdir -p "$out"
frames=
if [ "${1:-}" = --quick ]; then
  frames=300
fi
frameworks=
if [ "$(uname)" = Darwin ]; then
  frameworks="-framework CoreFoundation"
fi
# shellcheck disable=SC2086
${CC:-cc} -O2 -std=gnu99 $color -I"$MGBA_DIR/include" -I"$build/include" \
  -Isource/src tests/render/render_check.c source/src/gba_ppu.c \
  source/src/gba_ppu_log.c "$build/libmgba.a" -lm -lpthread $frameworks \
  -o "$out/render_check"

status=0
check()
{
  name=$1
  rom=$(absolute "$2")
  script=$(absolute "$3")
  if [ -n "$frames" ]; then
    # Only the script's first frames.
    awk -v left="$frames" '$1 == "run" && left > 0 {
      if ($2 > left) $2 = left
      left -= $2
      print
    }' "$3" > "$out/$name-quick.txt"
    script=$(absolute "$out/$name-quick.txt")
  fi
  shift 3
  rm -f "$out/mismatch.bmp"
  if ! (cd "$out" && ./render_check "$rom" "$script" "$@" > "$name.log"); then
    status=1
  fi
  if [ -f "$out/mismatch.bmp" ]; then
    mv "$out/mismatch.bmp" "$out/$name.bmp"
  fi
  tail -n 3 "$out/$name.log" | sed "s|^[^ ]*:|$name:|"
}

bios=
if [ -n "${BIOS:-}" ]; then
  bios=$(absolute "$BIOS")
fi
if [ -n "${MKSC:-}" ]; then
  # shellcheck disable=SC2086
  check mksc "$MKSC" tests/render/mksc.txt $bios
fi
if [ -n "${FIRERED:-}" ]; then
  : "${BIOS:?FIRERED needs BIOS}"
  : "${FIRERED_SAVE:?FIRERED needs FIRERED_SAVE}"
  # The walk starts in front of the save's last Pokemon Center.
  python3 tests/rom_link/firered_save.py place-heal "$FIRERED_SAVE" \
    "$out/firered-start.sav"
  check firered "$FIRERED" tests/rom_link/firered_trade.txt "$bios" \
    "$(absolute "$out/firered-start.sav")"
fi
if [ -n "${LEAFGREEN:-}" ]; then
  # shellcheck disable=SC2086
  check leafgreen "$LEAFGREEN" tests/render/leafgreen.txt $bios
fi
for pair in ${RENDER_ROMS:-}; do
  rom=${pair%%:*}
  # shellcheck disable=SC2086
  check "$(basename "$rom" .gba)" "$rom" "${pair#*:}" $bios
done
if [ "$status" -ne 0 ]; then
  echo "Render check: FAILED, see $out" >&2
  exit 1
fi
echo "Render checks passed"
