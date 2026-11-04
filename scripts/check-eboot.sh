#!/bin/sh
set -eu

# GBA Rush PSP, Copyright (C) 2026 Piero Carrieri, GPL-2.0-or-later.

# make check-eboot: runs the PSP build in PPSSPP's headless runner on your
# own games, once on each CPU core, and fails when a run ends before its
# time is up, PPSSPP reports a crash, or FireRed's save no longer holds the
# same party. Skipped without PPSSPP or without games:
#   PPSSPP_HEADLESS  PPSSPPHeadless (default: the one scripts/setup-dev.sh
#             built in ext/ppsspp/build)
#   MKSC      Mario Kart: Super Circuit (European release)
#   ROM SAVE  Pokemon FireRed and a save
#   LEAFGREEN Pokemon LeafGreen
#   BIOS      optional GBA BIOS; the built-in one otherwise
#   EBOOT     the EBOOT.PBP to run (source/EBOOT.PBP, from make psp-build),
#             such as one built to crash, which must fail this check
#   RUN_SECONDS  how long each run lasts (120)
# The games start from boot with no keys pressed. PPSSPP has no Media
# Engine, so the Exact renderer draws on the CPU there. Each run's files
# and PPSSPP's output stay in OUT (build/eboot/<game>-<core>).

headless=${PPSSPP_HEADLESS:-ext/ppsspp/build/PPSSPPHeadless}
if [ ! -x "$headless" ]; then
  echo "EBOOT check skipped: no PPSSPPHeadless (PPSSPP_HEADLESS or sh scripts/setup-dev.sh ppsspp)"
  exit 0
fi
if [ -z "${MKSC:-}${ROM:-}${LEAFGREEN:-}" ]; then
  echo "EBOOT check skipped: set MKSC, ROM and SAVE, or LEAFGREEN"
  exit 0
fi
eboot=${EBOOT:-source/EBOOT.PBP}
if [ ! -f "$eboot" ]; then
  echo "check-eboot: no $eboot; run make psp-build" >&2
  exit 1
fi
out=${OUT:-build/eboot}
mkdir -p "$out"
${CC:-cc} -std=c99 -O2 -Wall -Wextra -Werror -Isource/src \
  tests/eboot/global_config.c source/src/config_model.c -o "$out/global_config"
party="python3 tests/rom_link/firered_save.py party"

absolute()
{
  case "$1" in
    /*) echo "$1" ;;
    *) echo "$PWD/$1" ;;
  esac
}

status=0
run()
{
  name=$1-$2
  rom=$3
  save=${4:-}
  dir=$out/$name
  rm -rf "$dir"
  mkdir -p "$dir/roms" "$dir/save" "$dir/state" "$dir/cfg" "$dir/snap" \
    "$dir/cheat" "$dir/boxart"
  # Without exception.prx a crash stops PPSSPP instead of waiting on the
  # crash screen for a key.
  cp "$eboot" "$dir/EBOOT.PBP"
  if [ -f source/media_engine.prx ]; then
    cp source/media_engine.prx "$dir/"
  fi
  # Without dir.ini the emulator stops at an error message for a key.
  printf '%s = %s\n' rom_directory roms save_directory save \
    save_state_directory state game_config_directory cfg \
    snapshot_directory snap cheat_directory cheat boxart_directory boxart \
    folder_boxart_directory boxart > "$dir/dir.ini"
  # Links, not copies, of your BIOS and ROM; a lone game.gba starts at
  # once, without the ROM browser.
  if [ -n "${BIOS:-}" ]; then
    ln -s "$(absolute "$BIOS")" "$dir/gba_bios.bin"
  fi
  ln -s "$(absolute "$rom")" "$dir/roms/game.gba"
  if [ -n "$save" ]; then
    cp "$save" "$dir/save/game.sav"
  fi
  number=0
  if [ "$2" = new ]; then
    number=1
  fi
  "$out/global_config" "$number" "$dir/tempgba.cfg"

  "$headless" --timeout="${RUN_SECONDS:-120}" "$dir/EBOOT.PBP" \
    > "$dir/ppsspp.log" 2>&1 || true
  if ! grep -q TIMEOUT "$dir/ppsspp.log" ||
    grep -Eiq 'exception|crash' "$dir/ppsspp.log"; then
    echo "check-eboot: $name ended early or crashed, see $dir/ppsspp.log" >&2
    status=1
    return
  fi
  if [ -n "$save" ] && [ "$($party "$save")" != "$($party "$dir/save/game.sav")" ]
  then
    echo "check-eboot: $name changed the save's party, see $dir" >&2
    status=1
    return
  fi
  echo "check-eboot: $name ran ${RUN_SECONDS:-120} s"
}

for core in classic new; do
  if [ -n "${MKSC:-}" ]; then
    run mksc "$core" "$MKSC"
  fi
  if [ -n "${ROM:-}" ]; then
    : "${SAVE:?ROM needs SAVE}"
    run firered "$core" "$ROM" "$SAVE"
  fi
  if [ -n "${LEAFGREEN:-}" ]; then
    run leafgreen "$core" "$LEAFGREEN"
  fi
done
if [ "$status" -ne 0 ]; then
  echo "EBOOT check: FAILED, see $out" >&2
  exit 1
fi
echo "EBOOT checks passed"
