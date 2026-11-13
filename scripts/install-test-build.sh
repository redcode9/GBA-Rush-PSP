#!/bin/sh

set -eu

# Installs a packaged build on the two test PSPs, on whichever is mounted
# over USB: the PSP-2000 as PSP_2004 (PSP/GAME/CAT_Emulators/gpSP) and the
# PSP-1000 as PSP_1004 (PSP/GAME/gpSP). For each card it first copies the
# runtime logs, Diagnostics reports, input traces, crash log, saves and the
# build it replaces into artifacts/hardware/<date>-<time>/<volume>/ of the
# main checkout (never of a linked worktree), then
# copies EBOOT.PBP, exception.prx and media_engine.prx from the zip, reads
# them back from the card to check their SHA-256, removes the logs and
# reports it archived (so the next session starts clean) and ejects the
# card. Saves, savestates, ROMs, cheats, BIOS and settings are never removed
# or changed.
#
# Usage: sh scripts/install-test-build.sh [--dry-run] [--no-media-engine]
#          [--folder NAME] [--rom FILE]... ZIP
#   ZIP                 a zip from make package, release or PROFILE=diagnostic
#                       (its GBARushPSP-DIAG/ goes into the same folder);
#                       ZIP.sha256 must match it
#   --dry-run           print what would change; write nothing
#   --no-media-engine   leave media_engine.prx off the card (it is archived
#                       first), so the Exact renderer draws on the main
#                       processor; run again without it to put it back
#   --folder NAME       install into PSP/GAME/NAME instead, next to the test
#                       build, whose folder is only read: everything above
#                       (archive into artifacts/.../<volume>/NAME/, check,
#                       clean-up) applies to NAME alone. media_engine.prx
#                       goes there only if the zip has one (1.4.2 and 1.4.3
#                       have none). gba_bios.bin and dir.ini, if NAME lacks
#                       them, are copied from the test build's folder on the
#                       card, and the folders dir.ini names are created.
#   --rom FILE          copy FILE into the ROM folder dir.ini names (the app
#                       folder without one), over a ROM of the same name;
#                       repeatable. ROMs are never removed.
# PSP_VOLUMES (default /Volumes) is where the cards are mounted, and
# PSP_MIN_FREE_KB (default 2048) the space each card keeps free afterwards
# for savestates and logs.

usage()
{
  echo "Usage: $0 [--dry-run] [--no-media-engine] [--folder NAME]" \
    "[--rom FILE]... dist/GBA-Rush-PSP-VERSION.zip" >&2
  exit 1
}

dry_run=0
media_engine=1
folder=
zip=
work=$(mktemp -d "${TMPDIR:-/tmp}/gba-rush-install.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# The ROMs wait here, so their names may hold spaces.
mkdir "$work/rom"
while [ $# -gt 0 ]
do
  case "$1" in
    --dry-run) dry_run=1 ;;
    --no-media-engine) media_engine=0 ;;
    --folder) [ -n "${2-}" ] || usage; folder=$2; shift ;;
    --rom)
      if [ ! -f "${2-}" ]; then
        echo "No ROM file: ${2-}" >&2
        exit 1
      fi
      cp "$2" "$work/rom/"
      shift ;;
    -*) echo "Unknown option: $1" >&2; exit 1 ;;
    *) zip=$1 ;;
  esac
  shift
done
if [ -z "$zip" ] || [ ! -f "$zip" ]; then
  usage
fi
# FAT ignores case: gpSP is the test build's folder on the PSP-1000.
case "$folder" in
  */*|.|..|[Gg][Pp][Ss][Pp])
    echo "--folder takes a new folder name in PSP/GAME, not $folder" >&2
    exit 1 ;;
esac

volumes=${PSP_VOLUMES:-/Volumes}
min_free_kb=${PSP_MIN_FREE_KB:-2048}
root=$(cd "$(dirname "$0")/.." && pwd)
# Once the logs leave the card the archive is their only copy, so it goes
# into the main checkout even when the script runs from a linked worktree:
# `git worktree remove` deletes the ignored artifacts/ folder without a
# warning.
common=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir \
  2>/dev/null || :)
if [ "${common##*/}" = .git ]; then
  root=${common%/.git}
fi
artifacts="$root/artifacts/hardware/$(date +%Y%m%d-%H%M%S)"

sha256()
{
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$@"
  else
    sha256sum "$@"
  fi
}

run()
{
  if [ "$dry_run" = 1 ]; then
    echo "  would run: $*"
  else
    "$@"
  fi
}

# The value of the dir.ini key (a pattern) in FILE, one per line.
ini_value()
{
  tr -d '\r' < "$1" | sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*//p" |
    sed 's/[[:space:]]*$//'
}

(cd "$(dirname "$zip")" && sha256 -c "$(basename "$zip").sha256")
unzip -q "$zip" -d "$work/zip"
build=
for candidate in "$work"/zip/*/GBARushPSP "$work"/zip/*/GBARushPSP-DIAG
do
  if [ -d "$candidate" ]; then
    build=$candidate
    break
  fi
done
if [ -z "$build" ]; then
  echo "The zip has no GBARushPSP/ or GBARushPSP-DIAG/ folder" >&2
  exit 1
fi
if [ -n "$folder" ] && [ ! -f "$build/media_engine.prx" ]; then
  media_engine=0
fi
files="EBOOT.PBP exception.prx"
if [ "$media_engine" = 1 ]; then
  files="$files media_engine.prx"
fi
for file in $files
do
  if [ ! -f "$build/$file" ]; then
    echo "The zip has no $(basename "$build")/$file" >&2
    exit 1
  fi
done

install_on()
{
  volume=$1
  default=$2
  card=$3
  archive="$artifacts/$volume${folder:+/$folder}"
  echo "$volume: $card"

  # A new folder takes the test build's BIOS and folder layout, copied on
  # the card.
  extras=
  if [ -n "$folder" ]; then
    for file in gba_bios.bin dir.ini
    do
      if [ -f "$default/$file" ] && [ ! -f "$card/$file" ]; then
        extras="$extras $file"
      fi
    done
  fi
  ini="$card/dir.ini"
  if [ ! -f "$ini" ]; then
    ini="$default/dir.ini"
  fi
  dirs=
  rom_dir=
  if [ -f "$ini" ]; then
    dirs=$(ini_value "$ini" '[A-Za-z_]*')
    rom_dir=$(ini_value "$ini" rom_directory)
  fi
  rom_dir=${rom_dir:-.}
  case "$rom_dir" in
    *:*)
      echo "$volume: dir.ini puts the ROMs outside the app folder" \
        "($rom_dir); nothing installed" >&2
      exit 1 ;;
  esac

  # Everything is copied off the card, and compared, before anything on it
  # changes.
  run mkdir -p "$archive/previous-build"
  for directory in logs benchmarks traces GBA/SAV
  do
    if [ -d "$card/$directory" ]; then
      run mkdir -p "$archive/$directory"
      run cp -R "$card/$directory/." "$archive/$directory/"
      run diff -r "$card/$directory" "$archive/$directory"
    fi
  done
  if [ -f "$card/tempgba_crash.log" ]; then
    run cp "$card/tempgba_crash.log" "$archive/"
    run cmp "$card/tempgba_crash.log" "$archive/tempgba_crash.log"
  fi
  for file in EBOOT.PBP exception.prx media_engine.prx
  do
    if [ -f "$card/$file" ]; then
      run cp "$card/$file" "$archive/previous-build/"
    fi
  done

  # The new files replace the old ones in place, so only the difference
  # has to fit.
  needed=0
  for file in $files
  do
    needed=$((needed + $(wc -c < "$build/$file")))
    if [ -f "$card/$file" ]; then
      needed=$((needed - $(wc -c < "$card/$file")))
    fi
  done
  for file in $extras
  do
    needed=$((needed + $(wc -c < "$default/$file")))
  done
  for rom in "$work"/rom/*
  do
    if [ -f "$rom" ]; then
      needed=$((needed + $(wc -c < "$rom")))
      if [ -f "$card/$rom_dir/${rom##*/}" ]; then
        needed=$((needed - $(wc -c < "$card/$rom_dir/${rom##*/}")))
      fi
    fi
  done
  # The test build's folder: NAME may not exist yet.
  free_kb=$(df -Pk "$default" | awk 'NR == 2 { print $4 }')
  if [ $((needed / 1024 + min_free_kb)) -gt "$free_kb" ]; then
    echo "$volume: $free_kb KB free, $((needed / 1024)) KB more needed" \
      "plus $min_free_kb KB to keep free; nothing installed" >&2
    exit 1
  fi

  if [ ! -d "$card" ]; then
    run mkdir -p "$card"
  fi
  for file in $files
  do
    # -X: no extended attributes, so no ._ files on the card.
    run cp -X "$build/$file" "$card/$file"
  done
  for file in $extras
  do
    run cp -X "$default/$file" "$card/$file"
  done
  # Without them the emulator stops on a message at start. Paths with a
  # drive (ms0:/...) are the test build's folders already.
  if [ -n "$folder" ]; then
    while IFS= read -r directory
    do
      case "$directory" in
        ''|*:*) ;;
        *) if [ ! -d "$card/$directory" ]; then
             run mkdir -p "$card/$directory"
           fi ;;
      esac
    done <<EOF
$dirs
EOF
  fi
  for rom in "$work"/rom/*
  do
    if [ -f "$rom" ]; then
      if [ ! -d "$card/$rom_dir" ]; then
        run mkdir -p "$card/$rom_dir"
      fi
      run cp -X "$rom" "$card/$rom_dir/"
    fi
  done
  if [ "$media_engine" = 0 ] && [ -f "$card/media_engine.prx" ]; then
    run rm -f "$card/media_engine.prx"
  fi
  run sync

  # Remounting drops the Mac's cache, so the check reads the card itself.
  device=$(df "$default" | awk 'NR == 2 { print $1 }')
  run diskutil unmount "$volumes/$volume"
  run diskutil mount "$device"
  if [ "$dry_run" = 1 ]; then
    echo "  would check the SHA-256 of $files$extras and the ROMs on the card"
  else
    for file in $files
    do
      expected=$(sha256 "$build/$file" | cut -d ' ' -f 1)
      actual=$(sha256 "$card/$file" | cut -d ' ' -f 1)
      if [ "$expected" != "$actual" ]; then
        echo "$volume: $file on the card differs from the zip" >&2
        exit 1
      fi
    done
    (cd "$card" && sha256 $files) | tee "$archive/installed.sha256"
    for file in $extras
    do
      cmp "$default/$file" "$card/$file"
    done
    for rom in "$work"/rom/*
    do
      if [ -f "$rom" ]; then
        cmp "$rom" "$card/$rom_dir/${rom##*/}"
        (cd "$card" && sha256 "$rom_dir/${rom##*/}") |
          tee -a "$archive/installed.sha256"
      fi
    done
  fi

  # Only what was archived and compared above.
  for file in "$card"/logs/session-*.gbrlog "$card"/logs/session.tmp \
    "$card"/logs/session.bak "$card"/benchmarks/perf-*.txt \
    "$card"/tempgba_crash.log
  do
    if [ -f "$file" ]; then
      run rm -f "$file"
    fi
  done
  run sync
  run diskutil eject "$volumes/$volume"
  echo "$volume: done"
}

found=0
for entry in PSP_2004:PSP/GAME/CAT_Emulators/gpSP PSP_1004:PSP/GAME/gpSP
do
  volume=${entry%%:*}
  default="$volumes/$volume/${entry#*:}"
  card=$default
  if [ -n "$folder" ]; then
    card="$volumes/$volume/PSP/GAME/$folder"
  fi
  if [ -d "$default" ]; then
    install_on "$volume" "$default" "$card"
    found=1
  else
    echo "$volume: not mounted, skipped"
  fi
done
if [ "$found" = 0 ]; then
  echo "No test PSP is mounted under $volumes" >&2
  exit 1
fi
if [ "$dry_run" = 0 ]; then
  echo "Logs and the replaced build are in $artifacts"
fi
