#!/bin/sh

set -eu

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Source tree checks require a Git checkout." >&2
  exit 1
fi

git diff --check
git diff --cached --check

git ls-files | while IFS= read -r path
do
  case "$path" in
    *.gba|*.agb|*.bin|*.sav|*.svs|*.cht|*.o|*.elf|*.prx|*.PBP|*.exe|*/PARAM.SFO)
      echo "Generated or private runtime file is tracked: $path" >&2
      exit 1
      ;;
  esac
done

echo "Source tree checks passed"
