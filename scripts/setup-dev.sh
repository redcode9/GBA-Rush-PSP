#!/bin/sh
set -eu

# GBA Rush PSP, Copyright (C) 2026 Piero Carrieri, GPL-2.0-or-later.

# Prepares a machine (macOS arm64 or Linux x86_64) for every check: the PSP
# toolchain, the references check-core and check-render compare against,
# the test ROM suites and PPSSPP's headless runner, all in ext/ (ignored
# by Git) at the versions pinned below.
#
#   sh scripts/setup-dev.sh [--dry-run] [STEP...]
#
# STEP is any of pspdev arm-gcc unicorn mgba sst test-roms ppsspp (all of
# them by default). --dry-run prints every command and runs none. Running
# the script again skips what is already in place, and a tool already on
# the machine is linked into ext/ instead of downloaded: the pspdev at
# PSPDEV or ~/Development/.tools/pspdev, the arm-none-eabi-gcc on PATH or
# Homebrew's arm-gcc-bin@15, the Unicorn at UNICORN_DIR or Homebrew's.
# Archives are checked against their sha256 and Git sources against their
# commit; a mismatch or a failed download stops the script. Delete a
# directory in ext/ to rebuild it.
#
# What comes from where, with its licence. Nothing of it enters the
# repository or the EBOOT; test data is only read by local checks.
#   pspdev v20260801        PSP toolchain and PSPSDK (GCC and binutils
#                           GPL-3.0, PSPSDK BSD), github.com/pspdev/pspdev
#   arm-none-eabi-gcc 15.2  Arm GNU Toolchain 15.2.Rel1 (GPL-3.0), for
#                           bare-metal GBA test ROMs
#   Unicorn 2.1.4           GPL-2.0, runs the MIPS backend's code in the
#                           host tests (scripts/build-unicorn.sh)
#   mGBA 0.10.3             MPL-2.0, reference GBA for check-core,
#                           check-render and check-rom-link
#   SingleStepTests ARM7TDMI   MIT, per-instruction CPU tests (v1, ~1 GB)
#   gba-tests (jsmolka)     MIT, ARM, Thumb, memory, save and PPU test ROMs
#   armwrestler-gba-fixed   no licence declared: run locally only, nothing
#                           derived from it is committed
#   FuzzARM                 GPL-3.0, random ARM and Thumb test ROMs
#   mGBA test suite         MIT, timer and timing tests; source only, its
#                           ROM needs devkitARM (see its docker-build.sh)
#   PPSSPP v1.20.4          GPL-2.0-or-later, headless runner for
#                           make check-eboot

ext=$PWD/ext
dry=
steps=
for arg in "$@"; do
  case "$arg" in
    --dry-run) dry=1 ;;
    pspdev|arm-gcc|unicorn|mgba|sst|test-roms|ppsspp) steps="$steps $arg" ;;
    *)
      echo "usage: sh scripts/setup-dev.sh [--dry-run] [STEP...]" >&2
      exit 2
      ;;
  esac
done
steps=${steps:-pspdev arm-gcc unicorn mgba sst test-roms ppsspp}
platform=$(uname -s)-$(uname -m)

say()
{
  echo "setup-dev: $*"
}

die()
{
  echo "setup-dev: $*" >&2
  exit 1
}

run()
{
  echo "+ $*"
  [ -n "$dry" ] || "$@"
}

need()
{
  for tool in "$@"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      [ -n "$dry" ] || die "$tool is needed; install it and run again"
      say "missing $tool (needed for the steps below)"
    fi
  done
}

sha256()
{
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

# fetch URL SHA256 FILE: downloads URL to FILE unless FILE already has that
# sha256, and stops on any other digest.
fetch()
{
  if [ -f "$3" ] && [ "$(sha256 "$3")" = "$2" ]; then
    return
  fi
  need curl
  run mkdir -p "$(dirname "$3")"
  run curl -fL --retry 3 -o "$3.part" "$1"
  if [ -z "$dry" ] && [ "$(sha256 "$3.part")" != "$2" ]; then
    die "$1: sha256 $(sha256 "$3.part"), expected $2"
  fi
  run mv "$3.part" "$3"
}

# git_at URL COMMIT DIR [--recursive]: DIR checked out at COMMIT, with its
# submodules if asked. Git checks every object against its hash, so the
# commit pins the whole tree.
git_at()
{
  if [ "$(git -C "$3" rev-parse HEAD 2>/dev/null)" = "$2" ]; then
    say "${3#"$ext"/} is at $2"
    return
  fi
  need git
  run git init -q "$3"
  run git -C "$3" fetch -q --depth 1 "$1" "$2"
  run git -C "$3" checkout -q --detach FETCH_HEAD
  if [ "${4:-}" = --recursive ]; then
    run git -C "$3" submodule update -q --init --recursive --depth 1
  fi
  if [ -z "$dry" ] && [ "$(git -C "$3" rev-parse HEAD)" != "$2" ]; then
    die "$3 is not at $2"
  fi
}

# link NAME TARGET: ext/NAME points at a tool already on the machine.
link()
{
  say "using $2 for ext/$1"
  run ln -sfn "$2" "$ext/$1"
}

step_pspdev()
{
  existing=${PSPDEV:-$HOME/Development/.tools/pspdev}
  if [ -x "$ext/pspdev/bin/psp-config" ]; then
    say "pspdev is in place"
    return
  fi
  if [ -x "$existing/bin/psp-config" ]; then
    link pspdev "$existing"
    return
  fi
  case "$platform" in
    Darwin-arm64)
      asset=pspdev-macos-latest-arm64.tar.gz
      sum=f4f603a8f1bfded1b026679aef658ff6b0a53ecd787f182cdeae91413b5f1719 ;;
    Darwin-x86_64)
      asset=pspdev-macos-15-intel-x86_64.tar.gz
      sum=449e8140cc6ad9eede576c462a119a1a011933181cc54f579bf62ac703e15514 ;;
    Linux-x86_64)
      asset=pspdev-ubuntu-latest-x86_64.tar.gz
      sum=7a6cb2de21bfbc1d4aa06d10d0a45c547dedc8f6e1c8fc7ff8c7ba6eee667ee6 ;;
    Linux-aarch64)
      asset=pspdev-ubuntu-24.04-arm-arm64.tar.gz
      sum=1b76c35dc61f9d3383f5d122c86cd12bf7f1c0c1cbe71392b87efbacce18d260 ;;
    *) die "no pinned pspdev for $platform; install pspdev v20260801 and set PSPDEV" ;;
  esac
  fetch "https://github.com/pspdev/pspdev/releases/download/v20260801/$asset" \
    "$sum" "$ext/downloads/$asset"
  # The archive holds a single pspdev/ directory.
  run tar -xzf "$ext/downloads/$asset" -C "$ext"
}

step_arm_gcc()
{
  if [ -x "$ext/arm-gcc/bin/arm-none-eabi-gcc" ]; then
    say "arm-none-eabi-gcc is in place"
    return
  fi
  if found=$(command -v arm-none-eabi-gcc 2>/dev/null); then
    link arm-gcc "$(dirname "$(dirname "$found")")"
    return
  fi
  if [ -x /opt/homebrew/opt/arm-gcc-bin@15/bin/arm-none-eabi-gcc ]; then
    link arm-gcc /opt/homebrew/opt/arm-gcc-bin@15
    return
  fi
  if [ "$platform" != Darwin-arm64 ]; then
    # Only the macOS archive's digest is pinned; add the Linux one when a
    # Linux machine needs to build test ROMs.
    say "install arm-none-eabi-gcc 15 (gcc-arm-none-eabi) to build test ROMs"
    return
  fi
  name=arm-gnu-toolchain-15.2.rel1-darwin-arm64-arm-none-eabi
  fetch "https://developer.arm.com/-/media/Files/downloads/gnu/15.2.rel1/binrel/$name.tar.xz" \
    1938a84b7105c192e3fb4fa5e893ba25f425f7ddab40515ae608cd40f68669a8 \
    "$ext/downloads/$name.tar.xz"
  run tar -xJf "$ext/downloads/$name.tar.xz" -C "$ext"
  run ln -sfn "$name" "$ext/arm-gcc"
}

step_unicorn()
{
  if [ -f "$ext/unicorn/include/unicorn/unicorn.h" ]; then
    say "Unicorn is in place"
    return
  fi
  if [ -n "${UNICORN_DIR:-}" ]; then
    link unicorn "$UNICORN_DIR"
    return
  fi
  if [ -f /opt/homebrew/opt/unicorn/include/unicorn/unicorn.h ]; then
    link unicorn /opt/homebrew/opt/unicorn
    return
  fi
  need curl tar cmake make cc python3
  run sh scripts/build-unicorn.sh "$ext"
  run ln -sfn unicorn-2.1.4 "$ext/unicorn"
}

step_mgba()
{
  src=$ext/mgba-0.10.3
  git_at https://github.com/mgba-emu/mgba.git \
    1c61b54208ca6266129d0f2394c04bd8c44f98c5 "$src"
  # libmgba.a alone, with no outside libraries, as the harnesses link it.
  # -fwrapv: mGBA's flag code relies on signed overflow wrapping (see
  # check-core.sh). build16 mixes colours in the GBA's 5-bit steps for
  # check-render. CMAKE_POLICY_VERSION_MINIMUM lets CMake 4 configure it.
  for build in build build16; do
    if [ -f "$src/$build/libmgba.a" ]; then
      say "mgba-0.10.3/$build is built"
      continue
    fi
    flags=-fwrapv
    if [ "$build" = build16 ]; then
      flags="-fwrapv -DCOLOR_16_BIT"
    fi
    need cmake make cc
    run cmake -S "$src" -B "$src/$build" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 "-DCMAKE_C_FLAGS=$flags" \
      -DBUILD_STATIC=ON -DBUILD_SHARED=OFF -DBUILD_QT=OFF -DBUILD_SDL=OFF \
      -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
      -DUSE_FFMPEG=OFF -DUSE_ZLIB=OFF -DUSE_MINIZIP=OFF -DUSE_PNG=OFF \
      -DUSE_LIBZIP=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_LUA=OFF \
      -DUSE_JSON_C=OFF -DUSE_LZMA=OFF -DUSE_EDITLINE=OFF -DUSE_EPOXY=OFF \
      -DUSE_DISCORD_RPC=OFF -DUSE_GDB_STUB=OFF -DENABLE_SCRIPTING=OFF
    run cmake --build "$src/$build" --parallel
  done
}

step_sst()
{
  git_at https://github.com/SingleStepTests/ARM7TDMI.git \
    e3097d88d428752736b949d94c094d5da50f0db6 "$ext/ARM7TDMI"
}

step_test_roms()
{
  git_at https://github.com/jsmolka/gba-tests.git \
    a7113b67e63f83a9b321696ddd7042ccfad6c881 "$ext/test-roms/gba-tests"
  git_at https://github.com/destoer/armwrestler-gba-fixed.git \
    802e55ad61b421f4bbbc6f74f96aa4e5df4d630f "$ext/test-roms/armwrestler"
  git_at https://github.com/DenSinH/FuzzARM.git \
    a675329cd57da48e3e406216ba2d79dd7e09ee20 "$ext/test-roms/FuzzARM"
  git_at https://github.com/mgba-emu/suite.git \
    e6942030d25ffe3ba76c72b73a86da073ec857cc "$ext/test-roms/mgba-suite"
}

step_ppsspp()
{
  src=$ext/ppsspp
  git_at https://github.com/hrydgard/ppsspp.git \
    fa50bb1976065c4f8b1b47af227d367fe9771555 "$src" --recursive
  if [ -x "$src/build/PPSSPPHeadless" ]; then
    say "PPSSPPHeadless is built"
    return
  fi
  need cmake make cc c++
  run cmake -S "$src" -B "$src/build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DHEADLESS=ON
  run cmake --build "$src/build" --target PPSSPPHeadless --parallel
}

[ -f scripts/setup-dev.sh ] || die "run it from the repository root"
if [ -n "$dry" ]; then
  say "dry run: nothing is downloaded, built or linked"
fi
run mkdir -p "$ext"
for step in $steps; do
  say "== $step"
  case "$step" in
    arm-gcc) step_arm_gcc ;;
    test-roms) step_test_roms ;;
    *) "step_$step" ;;
  esac
done

cat <<EOF
setup-dev: done. For the PSP build and the test ROMs, add to your shell:
  export PSPDEV=$ext/pspdev
  export PATH=\$PSPDEV/bin:$ext/arm-gcc/bin:\$PATH
check-core, check-render, check-rom-link and check-eboot find their tools
in ext/ by themselves; BIOS, MKSC, ROM, SAVE, FIRERED, FIRERED_SAVE and
LEAFGREEN name your own files (see each script).
EOF
