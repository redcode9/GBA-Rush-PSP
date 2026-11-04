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
#   UNICORN_DIR  Unicorn 2.1, for the MIPS backend (arm_mips.c): its
#             fuzzer against the interpreter, the suite with the backend
#             checked against the interpreter on every test, and the
#             lockstep runs again with the backend. Either an installed one
#             (include/ and lib/, such as Homebrew's /opt/homebrew/opt/unicorn)
#             or a tree built by scripts/build-unicorn.sh (include/, build/)
# The runs stop at the first instruction where the core and mGBA disagree
# and leave their logs in OUT (build/core).

out=${OUT:-build/core}
mkdir -p "$out"
core_sources="source/src/arm_decode.c source/src/arm_interp.c"
backend=
if [ -n "${UNICORN_DIR:-}" ]; then
  lib=$UNICORN_DIR/build
  [ -d "$lib" ] || lib=$UNICORN_DIR/lib
  backend="-DSINGLE_STEP_JIT -DLOCKSTEP_JIT -I$UNICORN_DIR/include
    tests/core/mips_unicorn.c source/src/arm_mips.c -L$lib -lunicorn
    -Wl,-rpath,$lib"
  # shellcheck disable=SC2086
  ${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -Isource/src \
    -I"$UNICORN_DIR/include" tests/core/jit_fuzz.c tests/core/mips_unicorn.c \
    source/src/arm_mips.c $core_sources -L"$lib" -lunicorn \
    -Wl,-rpath,"$lib" -o "$out/jit_fuzz"
  if ! "$out/jit_fuzz" --cases "${FUZZ_CASES:-200}" > "$out/jit_fuzz.log"
  then
    echo "check-core: the backend differs, see $out/jit_fuzz.log" >&2
    exit 1
  fi
  tail -1 "$out/jit_fuzz.log"
else
  echo "check-core: UNICORN_DIR not set, MIPS backend checks skipped"
fi

if [ -n "${SST_DIR:-}" ]; then
  # shellcheck disable=SC2086
  ${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -Isource/src \
    tests/core/single_step.c $core_sources $backend -o "$out/single_step"
  if ! "$out/single_step" "$SST_DIR"/*.json.bin > "$out/single_step.log"
  then
    echo "check-core: SingleStepTests failed, see $out/single_step.log" >&2
    exit 1
  fi
  tail -2 "$out/single_step.log"
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
  -Isource/src tests/core/lockstep.c $core_sources $backend \
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
  if [ -n "$backend" ]; then
    # The backend in lockstep, then the whole game run by each core: the
    # two runs must agree at every stop.
    if ! "$out/lockstep" --jit --mode block "$@" > "$out/$name-jit.log"; then
      echo "check-core: $name (backend) diverged, see $out/$name-jit.log" >&2
      exit 1
    fi
    grep -m 1 "compared" "$out/$name-jit.log"
    "$out/lockstep" --mode run --trace "$out/$name-run.trace" "$@" \
      > /dev/null
    "$out/lockstep" --jit --mode run --trace "$out/$name-jit.trace" "$@" \
      > /dev/null
    if ! cmp -s "$out/$name-run.trace" "$out/$name-jit.trace"; then
      echo "check-core: $name runs differ, see $out/$name-*.trace" >&2
      exit 1
    fi
    echo "$name: $(wc -l < "$out/$name-run.trace") runs alike with the backend"
  fi
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
