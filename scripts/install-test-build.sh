#!/bin/sh

set -eu

# Installs a packaged build on the two test PSPs, on whichever is mounted
# over USB: the PSP-2000 as PSP_2004 (PSP/GAME/CAT_Emulators/gpSP) and the
# PSP-1000 as PSP_1004 (PSP/GAME/gpSP). For each card it first copies the
# runtime logs, Diagnostics reports, input traces, crash log, saves and the
# build it replaces into artifacts/hardware/<date>-<time>/<volume>/, then
# copies EBOOT.PBP, exception.prx and media_engine.prx from the zip, reads
# them back from the card to check their SHA-256, removes the logs and
# reports it archived (so the next session starts clean) and ejects the
# card. Saves, savestates, ROMs, cheats, BIOS and settings are never removed
# or changed.
#
# Usage: sh scripts/install-test-build.sh [--dry-run] [--no-media-engine] ZIP
#   ZIP                 a zip from make package; ZIP.sha256 must match it
#   --dry-run           print what would change; write nothing
#   --no-media-engine   leave media_engine.prx off the card (it is archived
#                       first), so the Exact renderer draws on the main
#                       processor; run again without it to put it back
# PSP_VOLUMES (default /Volumes) is where the cards are mounted, and
# PSP_MIN_FREE_KB (default 2048) the space each card keeps free afterwards
# for savestates and logs.

dry_run=0
media_engine=1
zip=
for argument
do
  case "$argument" in
    --dry-run) dry_run=1 ;;
    --no-media-engine) media_engine=0 ;;
    -*) echo "Unknown option: $argument" >&2; exit 1 ;;
    *) zip=$argument ;;
  esac
done
if [ -z "$zip" ] || [ ! -f "$zip" ]; then
  echo "Usage: $0 [--dry-run] [--no-media-engine] dist/GBA-Rush-PSP-VERSION.zip" >&2
  exit 1
fi

volumes=${PSP_VOLUMES:-/Volumes}
min_free_kb=${PSP_MIN_FREE_KB:-2048}
root=$(cd "$(dirname "$0")/.." && pwd)
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

(cd "$(dirname "$zip")" && sha256 -c "$(basename "$zip").sha256")
work=$(mktemp -d "${TMPDIR:-/tmp}/gba-rush-install.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
unzip -q "$zip" -d "$work"
set -- "$work"/*/GBARushPSP
build=$1
files="EBOOT.PBP exception.prx"
if [ "$media_engine" = 1 ]; then
  files="$files media_engine.prx"
fi
for file in $files
do
  if [ ! -f "$build/$file" ]; then
    echo "The zip has no GBARushPSP/$file" >&2
    exit 1
  fi
done

install_on()
{
  volume=$1
  card=$2
  archive="$artifacts/$volume"
  echo "$volume: $card"

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
  free_kb=$(df -Pk "$card" | awk 'NR == 2 { print $4 }')
  if [ $((needed / 1024 + min_free_kb)) -gt "$free_kb" ]; then
    echo "$volume: $free_kb KB free, $((needed / 1024)) KB more needed" \
      "plus $min_free_kb KB to keep free; nothing installed" >&2
    exit 1
  fi

  for file in $files
  do
    # -X: no extended attributes, so no ._ files on the card.
    run cp -X "$build/$file" "$card/$file"
  done
  if [ "$media_engine" = 0 ] && [ -f "$card/media_engine.prx" ]; then
    run rm -f "$card/media_engine.prx"
  fi
  run sync

  # Remounting drops the Mac's cache, so the check reads the card itself.
  device=$(df "$card" | awk 'NR == 2 { print $1 }')
  run diskutil unmount "$volumes/$volume"
  run diskutil mount "$device"
  if [ "$dry_run" = 1 ]; then
    echo "  would check the SHA-256 of $files on the card"
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
  card="$volumes/$volume/${entry#*:}"
  if [ -d "$card" ]; then
    install_on "$volume" "$card"
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
