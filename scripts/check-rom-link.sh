#!/bin/sh
set -eu

# Trades between two real FireRed games over the emulated cable, using mGBA
# as the host emulator and GBA Rush PSP's link code in between. Needs:
#   MGBA_DIR  an mGBA 0.10 source tree with libmgba.a built in build/
#   ROM BIOS SAVE  your own FireRed ROM, GBA BIOS and a save whose last
#             Pokemon Center visit was in a town and whose player has not
#             been upstairs in a Pokemon Center yet
#   ROM2 SAVE2  optional second game (defaults to ROM and SAVE), such as
#             LeafGreen with a matching save
#   MKSC      optional Mario Kart: Super Circuit ROM (European release)
# MGBA_DIR defaults to what scripts/setup-dev.sh put in ext/.
# The script trades the host's first Pokemon for the joiner's second one,
# then checks both saved parties, under several network conditions and once
# in the lockstep mode used for every non-Pokemon game. With MKSC it also
# races two Mario Kart games in Multi-Pak mode, which use lockstep mode,
# and checks that both race clocks run in step and, under PSP conditions,
# that the race runs near full speed.

if [ -z "${MGBA_DIR+set}" ] && [ -f ext/mgba-0.10.3/build/libmgba.a ]; then
  MGBA_DIR=ext/mgba-0.10.3
fi
: "${MGBA_DIR:?Set MGBA_DIR, or run sh scripts/setup-dev.sh mgba}"
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

out=${OUT:-build/rom_link}
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
# Each save continues in front of its last Pokemon Center, where the
# script's walk to the Cable Club starts.
save_tool="python3 tests/rom_link/firered_save.py"
$save_tool place-heal "$SAVE" "$out/start-0.sav"
$save_tool place-heal "$SAVE2" "$out/start-1.sav"

run_case()
{
  name=$1
  shift
  # Network and speed apply from the moment the cable starts; walking to
  # the Cable Club stays identical on both consoles.
  # "lockstep" makes the cable use the mode it uses for non-Pokemon games.
  while IFS= read -r line; do
    if [ "$line" = link ]; then
      for condition in "$@"; do
        if [ "$condition" = lockstep ]; then
          line="link lockstep"
        else
          echo "$condition"
        fi
      done
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
# Conditions measured between a PSP-2000 and a PSP-1000: each console needs
# about 12 ms to emulate a FireRed frame, runs it in a burst and leaves the
# link alone while it waits for VBlank; its network stack only moves
# datagrams while the emulator sleeps and spends 0.9 ms of that per datagram
# either way; in lockstep mode the console sleeps after each datagram it
# sends while emulating, 1.12 ms on average for main.c's 1 ms
# MAIN_LINK_SEND_YIELD_US. The trade cases add a 70 ms round trip that loses
# a third of the datagrams, and the second one radio dropouts and a joiner
# that freezes now and then.
psp="psp 0 12
psp 1 12
stack 0 1120 900
stack 1 1120 900"
# Mario Kart on the same two PSPs: races there ran at 40 fps on the build
# before 1.4.3's lockstep changes and at 45.5 fps on the first build with
# them. With 14 ms of emulation per frame and 0.3 ms of stack time per
# datagram they run at 39.7 and 52.7 fps here, with the clocking console
# sending and waiting as much as it did there; the second one's joiner
# resent 1446 datagrams too early there, which this test does not
# reproduce and the build after it no longer does.
mksc_psp="psp 0 14
psp 1 14
stack 0 1120 300
stack 1 1120 300"
run_case psp "net 20000 30000 35 1" "$psp"
run_case psp-stalls "net 20000 30000 35 1" "$psp" "outage 7000 250" \
  "hiccup 1 9000 600"
# Other network seeds time the dropouts differently against the games'
# own steps; these three once ended with both games waiting for good.
for seed in 4 9 21; do
  run_case "psp-stalls-$seed" "net 20000 30000 35 1" "$psp" \
    "outage 7000 250" "hiccup 1 9000 600" "seed $seed"
done
run_case lockstep "net 3000 4000 3 1" lockstep

# Mario Kart starts from blank saves; its script checks the race itself.
race_case()
{
  name=$1
  shift
  for condition in "$@"; do
    echo "$condition"
  done > "$out/$name.txt"
  cat tests/rom_link/mksc_race.txt >> "$out/$name.txt"
  # 64 KB of 0xFF, as erased save memory reads.
  dd if=/dev/zero bs=1024 count=64 2>/dev/null | tr '\000' '\377' \
    > "$out/mksc-0.sav"
  cp "$out/mksc-0.sav" "$out/mksc-1.sav"
  if ! (cd "$out" && ./link_e2e "$mksc" mksc-0.sav "$mksc" mksc-1.sav \
    "$bios" "$name.txt" > "$name.log"); then
    echo "ROM link $name: FAILED, see $out/$name.log" >&2
    exit 1
  fi
  echo "ROM link $name: both race clocks in step"
}

if [ -n "${MKSC:-}" ]; then
  mksc=$(absolute "$MKSC")
  race_case mksc-adhoc "net 3000 4000 3 1"
  race_case mksc-noisy "net 8000 15000 15 5"
  # mksc-psp-slow runs on the psp trade cases' network (20 ms delay, up to
  # 30 ms jitter, 35% loss): "slow" names that network here, while
  # slow-joiner and slow-host-noisy have a slow console.
  race_case mksc-psp-slow "net 20000 30000 35 1" "$mksc_psp"
  # Two PSPs side by side, a 4 ms round trip at best: the race must run at
  # full speed.
  race_case mksc-psp "net 2000 1000 5 1" "$mksc_psp" "minfps 57"
  # The frame times both PSPs reported in that race match this test's at
  # about a tenth of the datagrams lost: most frames on time, one in twenty
  # held 30 ms or more by a lost START or announcement.
  race_case mksc-psp-lossy "net 2000 1000 10 1" "$mksc_psp" "minfps 53"
fi
echo "ROM link checks passed"
