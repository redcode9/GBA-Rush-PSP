#!/bin/sh
set -eu

# Trades between two real FireRed games over the emulated cable, using mGBA
# as the host emulator and GBA Rush PSP's link code in between. Needs:
#   MGBA_DIR  an mGBA 0.10 source tree with libmgba.a built in build/
#   ROM BIOS SAVE  your own FireRed ROM, GBA BIOS and a save whose last
#             Pokemon Center visit was in a town and whose player has not
#             been upstairs in a Pokemon Center yet
#   ROM2 SAVE2  optional second game (defaults to ROM and SAVE)
# The script trades the host's first Pokemon for the joiner's second one,
# then checks both saved parties, under several network conditions.

: "${MGBA_DIR:?Set MGBA_DIR to an mGBA 0.10 tree built in build/}"
: "${ROM:?Set ROM to a FireRed ROM}"
: "${BIOS:?Set BIOS to a GBA BIOS}"
: "${SAVE:?Set SAVE to a FireRed save}"
ROM2=${ROM2:-$ROM}
SAVE2=${SAVE2:-$SAVE}

absolute()
{
  case "$1" in
    /*) echo "$1" ;;
    *) echo "$PWD/$1" ;;
  esac
}

out=build/rom_link
mkdir -p "$out"
frameworks=
if [ "$(uname)" = Darwin ]; then
  frameworks="-framework CoreFoundation"
fi
# shellcheck disable=SC2086
${CC:-cc} -O2 -std=gnu99 -I"$MGBA_DIR/include" -I"$MGBA_DIR/build/include" \
  -Isource/src tests/rom_link/link_e2e.c source/src/link_cable.c \
  source/src/link_net.c source/src/gba_sio.c "$MGBA_DIR/build/libmgba.a" \
  -lm $frameworks -o "$out/link_e2e"

rom0=$(absolute "$ROM")
rom1=$(absolute "$ROM2")
bios=$(absolute "$BIOS")
save_tool="python3 tests/rom_link/firered_save.py"
$save_tool place-heal "$SAVE" "$out/start-0.sav"
$save_tool place-heal "$SAVE2" "$out/start-1.sav"

run_case()
{
  name=$1
  shift
  # Network and speed apply from the moment the cable starts; walking to
  # the Cable Club stays identical on both consoles.
  while IFS= read -r line; do
    if [ "$line" = link ]; then
      for condition in "$@"; do echo "$condition"; done
    fi
    echo "$line"
  done < tests/rom_link/firered_trade.txt > "$out/$name.txt"
  rm -f "$out/after-0.sav" "$out/after-1.sav"
  (cd "$out" && ./link_e2e "$rom0" start-0.sav "$rom1" start-1.sav "$bios" \
    "$name.txt" > "$name.log")
  if ! $save_tool check-trade "$out/start-0.sav" "$out/after-0.sav" \
    "$out/start-1.sav" "$out/after-1.sav"; then
    echo "ROM link $name: FAILED, see $out/$name.log" >&2
    exit 1
  fi
  echo "ROM link $name: trade saved on both consoles"
}

run_case adhoc "net 3000 4000 3 1"
run_case noisy "net 8000 15000 15 5"
run_case slow-joiner "net 3000 4000 3 1" "speed 1 0.8"
run_case slow-host-noisy "net 8000 15000 15 5" "speed 0 0.8"
echo "ROM link checks passed"
