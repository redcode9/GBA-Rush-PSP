#!/bin/sh

set -eu

# make test-roms: builds every GBA test ROM (tests/core-rom, tests/save-rom,
# tests/raster-rom) into build/test-roms, runs each on build/host/rom_run
# (tests/core/rom_run.c: the New core's interpreter with a small GBA around
# it) and checks the signature it leaves with tools/decode_test_rom.py;
# then runs them in mGBA, if there is one (below). Each save ROM boots
# twice on the same save file, so the second boot reads back what the
# first one wrote. Skipped when there is no ARM compiler:
# arm-none-eabi-gcc from PATH, $DEVKITARM/bin, or ARM_PREFIX as in
# tests/gba-rom.mk.
#
# BIOS=/path/to/gba_bios.bin runs the core ROM on your dump of the official
# BIOS (never commit it), where the BIOS open bus cases must pass as well;
# the built-in BIOS returns from other addresses, so with it they fail.
#
# rom_run has no HBlank DMA, HBlank IRQ or PPU, so the raster ROM's cases
# fail there: it only has to boot, run them and leave its signature.
#
# Cases rom_run fails for known reasons, which are not ROM bugs:
# - cpu_irq_latency: the New core takes an IRQ at the end of a block,
#   not within a few cycles of the line rising.
# - timer_start_delay: rom_run times an access after its instruction's
#   other cycles (a load's internal cycle comes first), so right after a
#   start it reads 3 where mGBA reads 0.
# - prefetch_*: the New core runs a store's new code at once instead of
#   the two instructions already fetched.
# - vram_*, stm_vram: the New core keeps running its translation of code
#   in VRAM after the code is rewritten.
# - the save ROMs' odd and misaligned backup loads: the New core reads
#   the aligned address on the 8-bit backup bus.
core_allow=cpu_irq_latency,timer_start_delay
core_allow=$core_allow,prefetch_arm_1_cold
core_allow=$core_allow,prefetch_arm_1_warm,prefetch_arm_2_cold
core_allow=$core_allow,prefetch_arm_2_warm,prefetch_thumb_1_cold
core_allow=$core_allow,prefetch_thumb_1_warm,prefetch_thumb_2_cold
core_allow=$core_allow,prefetch_thumb_2_warm,prefetch_stm,prefetch_push
core_allow=$core_allow,prefetch_swp,vram_arm_cpu,vram_arm_dma
core_allow=$core_allow,vram_thumb_cpu,vram_thumb_dma,stm_vram
bus_allow=sram_ldrh_odd,sram_ldrsh_odd,sram_ldr_1,sram_ldr_2,sram_ldr_3
bus_allow=$bus_allow,flash_ldrh_device

PYTHON=${PYTHON:-python3}
MAKE=${MAKE:-make}
OUT=build/test-roms
RUN=${RUN:-build/host/rom_run}

if [ -n "${ARM_PREFIX:-}" ]; then
  arm_cc=${ARM_PREFIX}gcc
elif [ -n "${DEVKITARM:-}" ]; then
  arm_cc=$DEVKITARM/bin/arm-none-eabi-gcc
else
  arm_cc=arm-none-eabi-gcc
fi
if ! command -v "$arm_cc" >/dev/null 2>&1; then
  echo "Test ROMs skipped: no $arm_cc (install the Arm GNU toolchain or set DEVKITARM)"
  exit 0
fi

$MAKE -C tests/core-rom
$MAKE -C tests/save-rom
$MAKE -C tests/raster-rom
rm -f build/test-roms/*.sav

if [ -n "${BIOS:-}" ]; then
  $RUN --bios "$BIOS" --frames 600 --save $OUT/core-rom.sav $OUT/core-rom.gba
  $PYTHON tools/decode_test_rom.py --allow-fail $core_allow $OUT/core-rom.sav
else
  $RUN --frames 600 --save $OUT/core-rom.sav $OUT/core-rom.gba
  $PYTHON tools/decode_test_rom.py \
    --allow-fail bios_openbus_swi,bios_openbus_irq,$core_allow \
    $OUT/core-rom.sav
fi

for chip in sram32 sram64 flash64 flash128 eeprom512 eeprom8k notag
do
  rom=$OUT/save-$chip.gba
  save=$OUT/save-$chip.sav
  $RUN --frames 400 --save "$save" "$rom"
  $PYTHON tools/decode_test_rom.py --allow-fail $bus_allow "$save"
  $RUN --frames 400 --save "$save" "$rom"
  # Into a file rather than through a pipe, which would hide a failing exit.
  $PYTHON tools/decode_test_rom.py --allow-fail $bus_allow "$save" \
    > "$save.txt" || { cat "$save.txt"; exit 1; }
  cat "$save.txt"
  if ! grep -q 'read_back  *PASS' "$save.txt"; then
    echo "save-$chip did not read back its first boot's generation" >&2
    exit 1
  fi
done

$RUN --frames 300 --save $OUT/raster-rom.sav $OUT/raster-rom.gba
$PYTHON tools/decode_test_rom.py \
  --allow-fail hblank_dma_vcount,hblank_dma_reload,hblank_irq_vcount \
  $OUT/raster-rom.sav

# mGBA 0.10 as the reference: MGBA_DIR, a source tree with libmgba.a
# built in build/, by default ext/mgba-0.10.3 once scripts/setup-dev.sh
# has built it. Every ROM runs again there through tests/core/mgba_run.c,
# and only these cases may fail, each for a reason in mGBA's source:
# - pc_high hangs: mGBA runs open bus above 0FFFFFFFh
#   (docs/DESIGN-DECISIONS.md).
# - bios_openbus_boot: mgba_run starts the ROM without running the BIOS
#   (skipBios), which leaves the BIOS latch at 0.
# - cpu_irq_unmask: mGBA looks for a pending IRQ 7 cycles after the
#   write that unmasks it, not at the end of that instruction.
# - timer_overflow_reload: mGBA reads a timer 2 cycles in the past from
#   the reloaded count, so right after an overflow it reads up to 2 below
#   the reload.
# - without BIOS, the RL cases: mGBA's own BIOS decompresses RL data
#   differently; with your official BIOS they pass.
# mgba_run is told the save chips mGBA cannot guess (it ignores the ID
# string) and the save ROMs get 900 frames, as filling the 8 KiB EEPROM
# takes about 400. The raster ROM runs twice and must draw the same
# frame.
if [ -z "${MGBA_DIR+set}" ] && [ -f ext/mgba-0.10.3/build/libmgba.a ]; then
  MGBA_DIR=ext/mgba-0.10.3
fi
if [ -z "${MGBA_DIR:-}" ]; then
  echo "Test ROMs in mGBA skipped: no mGBA (MGBA_DIR or sh scripts/setup-dev.sh mgba)"
  echo "Test ROMs passed"
  exit 0
fi
frameworks=
if [ "$(uname)" = Darwin ]; then
  frameworks="-framework CoreFoundation"
fi
# shellcheck disable=SC2086
${CC:-cc} -std=gnu99 -O2 -Wall -Wextra -I"$MGBA_DIR/include" \
  -I"$MGBA_DIR/build/include" tests/core/mgba_run.c \
  "$MGBA_DIR/build/libmgba.a" -lm $frameworks -o $OUT/mgba_run
MGBA=$OUT/mgba_run
mkdir -p $OUT/mgba
rm -f $OUT/mgba/*.sav

mgba_allow=pc_high,bios_openbus_boot,cpu_irq_unmask,timer_overflow_reload
if [ -n "${BIOS:-}" ]; then
  $MGBA --bios "$BIOS" --save $OUT/mgba/core-rom.sav $OUT/core-rom.gba
else
  mgba_allow=$mgba_allow,rl_wram_arm,rl_wram_thumb,rl_vram_arm,rl_vram_thumb
  $MGBA --save $OUT/mgba/core-rom.sav $OUT/core-rom.gba
fi
$PYTHON tools/decode_test_rom.py --allow-fail $mgba_allow \
  $OUT/mgba/core-rom.sav

for chip in sram32 sram64 flash64 flash128 eeprom512 eeprom8k notag
do
  rom=$OUT/save-$chip.gba
  save=$OUT/mgba/save-$chip.sav
  case $chip in
    sram64) type=SRAM512 ;;
    flash64) type=FLASH512 ;;
    flash128) type=FLASH1M ;;
    *) type= ;;
  esac
  $MGBA ${type:+--savetype $type} --frames 900 --save "$save" "$rom"
  $PYTHON tools/decode_test_rom.py "$save"
  $MGBA ${type:+--savetype $type} --frames 900 --save "$save" "$rom"
  $PYTHON tools/decode_test_rom.py "$save" > "$save.txt" ||
    { cat "$save.txt"; exit 1; }
  cat "$save.txt"
  if ! grep -q 'read_back  *PASS' "$save.txt"; then
    echo "save-$chip did not read back its first boot's generation in mGBA" >&2
    exit 1
  fi
done

for run in 1 2; do
  rm -f $OUT/mgba/raster-rom.sav
  $MGBA --frames 300 --save $OUT/mgba/raster-rom.sav \
    --frame $OUT/mgba/raster-rom-$run.ppm $OUT/raster-rom.gba
  $PYTHON tools/decode_test_rom.py $OUT/mgba/raster-rom.sav
done
if ! cmp -s $OUT/mgba/raster-rom-1.ppm $OUT/mgba/raster-rom-2.ppm; then
  echo "raster-rom drew different frames in two mGBA runs" >&2
  exit 1
fi

echo "Test ROMs passed"
