#!/bin/sh

set -eu

# make tree-check, part of make check: whitespace errors, the source-level
# guard scripts, the CBMC gate's inputs (CBMC itself runs only in make
# check-cbmc), maintained docs, and no ROMs, saves or build output in Git.

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Source tree checks require a Git checkout." >&2
  exit 1
fi

git diff --check
git diff --cached --check
sh scripts/check-messages.sh
sh scripts/check-menu-layout.sh
sh scripts/check-lifecycle.sh
sh scripts/check-video.sh
sh scripts/check-performance.sh

for path in scripts/check-cbmc.sh proofs/cbmc/prove_link_net.c
do
  if [ ! -f "$path" ]; then
    echo "Missing bounded-proof input: $path" >&2
    exit 1
  fi
done
sh -n scripts/check-cbmc.sh
if ! grep -q -- '--unwinding-assertions' scripts/check-cbmc.sh; then
  echo "CBMC gate must check complete unwinding" >&2
  exit 1
fi

# Validate documentation entry points, not a historical list of draft plans.
# Adding or retiring a design note must not break every unrelated host test.
for path in README.md CHANGELOG.md CONTRIBUTING.md \
  docs/link-cable.md docs/ROADMAP-1.4.0.md \
  source/single-game_launcher/tool/README.md
do
  if [ ! -s "$path" ]; then
    echo "Missing maintained documentation: $path" >&2
    exit 1
  fi
done

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
