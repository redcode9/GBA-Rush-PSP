#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tla_jar=${TLA2TOOLS_JAR:-}
tlc_workers=${TLC_WORKERS:-auto}
tlc_state_root=${TLC_STATE_DIR:-$repo_root/build/tlc}

if [ -x /opt/homebrew/opt/openjdk@21/bin/java ]; then
  tla_java=/opt/homebrew/opt/openjdk@21/bin/java
elif command -v java >/dev/null 2>&1; then
  tla_java=$(command -v java)
else
  echo "Java 11 or newer is required for the TLA+ model checks" >&2
  exit 2
fi

if [ -z "$tla_jar" ] || [ ! -f "$tla_jar" ]; then
  echo "Set TLA2TOOLS_JAR to a local tla2tools.jar (TLA+ 1.7.2 tested)" >&2
  exit 2
fi

python3 "$repo_root/tools/generate_link_transitions.py" --check
mkdir -p "$tlc_state_root"

cd "$repo_root/models/link"
"$tla_java" -XX:+UseParallelGC -jar "$tla_jar" \
  -cleanup -difftrace -workers "$tlc_workers" \
  -metadir "$tlc_state_root/safety" \
  -config LinkCableSafety.cfg LinkCable.tla
"$tla_java" -XX:+UseParallelGC -jar "$tla_jar" \
  -cleanup -difftrace -lncheck final -workers "$tlc_workers" \
  -metadir "$tlc_state_root/liveness" \
  -config LinkCableLiveness.cfg LinkCable.tla
"$tla_java" -XX:+UseParallelGC -jar "$tla_jar" \
  -cleanup -difftrace -workers "$tlc_workers" \
  -metadir "$tlc_state_root/stream-safety" \
  -config LinkStreamSafety.cfg LinkStream.tla
"$tla_java" -XX:+UseParallelGC -jar "$tla_jar" \
  -cleanup -difftrace -lncheck final -workers "$tlc_workers" \
  -metadir "$tlc_state_root/stream-liveness" \
  -config LinkStreamLiveness.cfg LinkStream.tla
