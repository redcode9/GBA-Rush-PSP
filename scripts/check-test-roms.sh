#!/bin/sh

set -eu

# make test-roms: builds every GBA test ROM (tests/core-rom, tests/save-rom)
# into build/test-roms, runs each on build/host/rom_run (tests/core/rom_run.c:
# the New core's interpreter with a small GBA around it) and checks the
# signature it leaves with tools/decode_test_rom.py. Each save ROM boots
# twice on the same save file, so the second boot reads back what the first
# one wrote. Skipped when there is no ARM compiler: arm-none-eabi-gcc from
# PATH, $DEVKITARM/bin, or ARM_PREFIX as in tests/gba-rom.mk.
#
# BIOS=/path/to/gba_bios.bin runs the core ROM on your dump of the official
# BIOS (never commit it), where the BIOS open bus cases must pass as well;
# the built-in BIOS returns from other addresses, so with it they fail.
#
# Cases rom_run fails for known reasons, which are not ROM bugs:
# - timer_start_delay, timer_stop_hold: rom_run times an access after its
#   instruction's other cycles (a load's internal cycle comes first), so
#   it reads 2 more right after a start and 1 more at a stop.
# - prefetch_*: the New core runs a store's new code at once instead of
#   the two instructions already fetched.
# - vram_*, stm_vram: the New core keeps running its translation of code
#   in VRAM after the code is rewritten.
core_allow=timer_start_delay,timer_stop_hold,prefetch_arm_1_cold
core_allow=$core_allow,prefetch_arm_1_warm,prefetch_arm_2_cold
core_allow=$core_allow,prefetch_arm_2_warm,prefetch_thumb_1_cold
core_allow=$core_allow,prefetch_thumb_1_warm,prefetch_thumb_2_cold
core_allow=$core_allow,prefetch_thumb_2_warm,prefetch_stm,prefetch_push
core_allow=$core_allow,prefetch_swp,vram_arm_cpu,vram_arm_dma
core_allow=$core_allow,vram_thumb_cpu,vram_thumb_dma,stm_vram

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
  $PYTHON tools/decode_test_rom.py "$save"
  $RUN --frames 400 --save "$save" "$rom"
  $PYTHON tools/decode_test_rom.py "$save" | tee "$save.txt"
  if ! grep -q 'read_back  *PASS' "$save.txt"; then
    echo "save-$chip did not read back its first boot's generation" >&2
    exit 1
  fi
done

echo "Test ROMs passed"
