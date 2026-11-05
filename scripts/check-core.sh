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
#             (BIOS and MKSC alone run the translation bench on it)
#   ROM SAVE  FireRed or LeafGreen and a save whose last Pokemon Center
#             visit was in a town, tests/rom_link/firered_trade.txt
#   UNICORN_DIR  Unicorn 2.1, for the MIPS backend (arm_mips.c): its
#             fuzzer against the interpreter, the suite with the backend
#             checked against the interpreter on every test, and the
#             lockstep runs again with the backend. Either an installed one
#             (include/ and lib/, such as Homebrew's /opt/homebrew/opt/unicorn)
#             or a tree built by scripts/build-unicorn.sh (include/, build/)
# SST_DIR, MGBA_DIR and UNICORN_DIR default, when unset, to what
# scripts/setup-dev.sh put in ext/; set one empty to skip its part.
# With --quick (make check-core-quick, for every pull request) the fuzzer
# runs 5 cases, the suite the first 1000 tests of each file and lockstep
# the first 60 frames of each game.
# The runs stop at the first instruction where the core and mGBA disagree,
# and fail when a cycle count differs in code run off the GamePak (on it
# the two model the prefetch buffer differently); they leave their logs in
# OUT (build/core).

out=${OUT:-build/core}
mkdir -p "$out"
fuzz_cases=200
sst_limit=
frames=
if [ "${1:-}" = --quick ]; then
  # 60 frames is a first guess at a million instructions per game; tune it
  # from the "instructions compared" lines of a quick run.
  fuzz_cases=5
  sst_limit="-n 1000"
  frames="--frames 60"
fi
if [ -z "${SST_DIR+set}" ] && [ -d ext/ARM7TDMI/v1 ]; then
  SST_DIR=ext/ARM7TDMI/v1
fi
if [ -z "${MGBA_DIR+set}" ] && [ -f ext/mgba-0.10.3/build/libmgba.a ]; then
  MGBA_DIR=ext/mgba-0.10.3
fi
if [ -z "${UNICORN_DIR+set}" ] && [ -d ext/unicorn/include ]; then
  UNICORN_DIR=$PWD/ext/unicorn
fi
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
  if ! "$out/jit_fuzz" --cases "${FUZZ_CASES:-$fuzz_cases}" > "$out/jit_fuzz.log"
  then
    echo "check-core: the backend differs, see $out/jit_fuzz.log" >&2
    exit 1
  fi
  grep "cases agree" "$out/jit_fuzz.log"
else
  echo "check-core: no UNICORN_DIR, MIPS backend checks skipped"
fi

if [ -n "${SST_DIR:-}" ]; then
  # shellcheck disable=SC2086
  ${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -Isource/src \
    tests/core/single_step.c $core_sources $backend -o "$out/single_step"
  # shellcheck disable=SC2086
  if ! "$out/single_step" $sst_limit "$SST_DIR"/*.json.bin \
    > "$out/single_step.log"
  then
    echo "check-core: SingleStepTests failed, see $out/single_step.log" >&2
    exit 1
  fi
  tail -2 "$out/single_step.log"
else
  echo "check-core: no SST_DIR, SingleStepTests skipped"
fi

if [ -n "${BIOS:-}" ] && [ -n "${MKSC:-}" ]; then
  # What translating the game's code costs, and how much code it makes.
  ${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -Isource/src \
    tests/core/translate_bench.c $core_sources -o "$out/translate_bench"
  "$out/translate_bench" --repeat 5 "$MKSC" "$BIOS"
fi

if [ -z "${MGBA_DIR:-}" ] || [ -z "${BIOS:-}" ]; then
  echo "check-core: no MGBA_DIR or BIOS, lockstep runs skipped"
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
  # shellcheck disable=SC2086
  set -- $frames "$@"
  for mode in step block; do
    if ! "$out/lockstep" --mode "$mode" "$@" > "$out/$name-$mode.log"; then
      echo "check-core: $name ($mode) diverged, see $out/$name-$mode.log" >&2
      exit 1
    fi
    grep -m 2 -e compared -e "off the GamePak" "$out/$name-$mode.log"
  done
  if [ -n "$backend" ]; then
    # The backend in lockstep, then the whole game run by each core: the
    # two runs must agree at every stop.
    if ! "$out/lockstep" --jit --mode block "$@" > "$out/$name-jit.log"; then
      echo "check-core: $name (backend) diverged, see $out/$name-jit.log" >&2
      exit 1
    fi
    grep -m 2 -e compared -e "off the GamePak" "$out/$name-jit.log"
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
