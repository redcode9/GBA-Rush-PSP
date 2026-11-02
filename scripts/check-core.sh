#!/bin/sh
set -eu

# make check-core: the ARM7TDMI core (source/src/arm_*.c) against references
# outside the repository. Each part runs only when its inputs are set:
#   SST_DIR   the v1 directory of the SingleStepTests ARM7TDMI suite
#             (github.com/SingleStepTests/ARM7TDMI, the .json.bin files)
#   MGBA_DIR  an mGBA 0.10 source tree with libmgba.a built in build/
#             (with CMAKE_C_FLAGS=-fwrapv: mGBA's flag code relies on
#             signed overflow wrapping, and clang otherwise gets V wrong
#             in places, such as gba-tests' thumb test 102), and
#   BIOS      a GBA BIOS, for lockstep runs (instruction by instruction,
#             then block by block) of
#   MKSC      Mario Kart: Super Circuit (Europe), tests/core/mksc_race.txt
#   ROM SAVE  FireRed or LeafGreen and a save whose last Pokemon Center
#             visit was in a town, tests/rom_link/firered_trade.txt
# The runs stop at the first instruction where the core and mGBA disagree
# and leave their logs in OUT (build/core).

out=${OUT:-build/core}
mkdir -p "$out"
core_sources="source/src/arm_decode.c source/src/arm_interp.c"

if [ -n "${SST_DIR:-}" ]; then
  # shellcheck disable=SC2086
  ${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -Isource/src \
    tests/core/single_step.c $core_sources -o "$out/single_step"
  "$out/single_step" "$SST_DIR"/*.json.bin | tail -1
else
  echo "check-core: SST_DIR not set, SingleStepTests skipped"
fi

if [ -z "${MGBA_DIR:-}" ] || [ -z "${BIOS:-}" ]; then
  echo "check-core: MGBA_DIR or BIOS not set, lockstep runs skipped"
  exit 0
fi
frameworks=
if [ "$(uname)" = Darwin ]; then
  frameworks="-framework CoreFoundation"
fi
# shellcheck disable=SC2086
${CC:-cc} -std=gnu99 -O2 -I"$MGBA_DIR/include" -I"$MGBA_DIR/build/include" \
  -Isource/src tests/core/lockstep.c $core_sources \
  "$MGBA_DIR/build/libmgba.a" -lm $frameworks -o "$out/lockstep"

lockstep()
{
  name=$1
  shift
  for mode in step block; do
    if ! "$out/lockstep" --mode "$mode" "$@" > "$out/$name-$mode.log"; then
      echo "check-core: $name ($mode) diverged, see $out/$name-$mode.log" >&2
      exit 1
    fi
    grep -m 1 "compared" "$out/$name-$mode.log"
  done
}

if [ -n "${MKSC:-}" ]; then
  lockstep mksc --script tests/core/mksc_race.txt "$MKSC" "$BIOS"
fi
if [ -n "${ROM:-}" ] && [ -n "${SAVE:-}" ]; then
  # The script starts in front of the last Pokemon Center, as in
  # check-rom-link.sh.
  python3 tests/rom_link/firered_save.py place-heal "$SAVE" "$out/start.sav"
  lockstep pokemon --script tests/rom_link/firered_trade.txt "$ROM" "$BIOS" \
    "$out/start.sav"
fi
echo "Core checks passed"
