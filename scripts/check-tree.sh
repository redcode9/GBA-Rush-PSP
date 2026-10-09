#!/bin/sh

set -eu

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

for path in \
  scripts/check-cbmc.sh \
  proofs/cbmc/prove_queues.c \
  proofs/cbmc/prove_queue_payload_bounds.c \
  proofs/cbmc/prove_link_transport_scheduler.c \
  proofs/cbmc/prove_link_protocol.c \
  proofs/cbmc/prove_link_protocol_v3.c \
  proofs/cbmc/prove_link_stream.c \
  proofs/cbmc/prove_link_journal.c \
  proofs/cbmc/prove_serial_arithmetic.c \
  proofs/cbmc/prove_lifecycle_teardown.c \
  proofs/cbmc/zlib_stub.c
do
  if [ ! -f "$path" ]; then
    echo "Missing bounded-proof input: $path" >&2
    exit 1
  fi
done
sh -n scripts/check-cbmc.sh
if ! grep -q -- '--unwinding-assertions' scripts/check-cbmc.sh ||
   ! grep -q -- '--cover assume' scripts/check-cbmc.sh; then
  echo "CBMC gate must check complete unwinding and assumption reachability" >&2
  exit 1
fi

for path in \
  scripts/check-link-model.sh \
  models/link/LinkCable.tla \
  models/link/LinkCableSafety.cfg \
  models/link/LinkCableLiveness.cfg \
  models/link/LinkJournalTransitions.tla \
  models/link/link_journal_transitions.def \
  models/link/LinkStream.tla \
  models/link/LinkStreamSafety.cfg \
  models/link/LinkStreamLiveness.cfg
do
  if [ ! -f "$path" ]; then
    echo "Missing link-model input: $path" >&2
    exit 1
  fi
done
sh -n scripts/check-link-model.sh
if ! grep -q -- '-lncheck final' scripts/check-link-model.sh; then
  echo "TLC liveness gate must check the completed state graph" >&2
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
