#!/bin/sh

set -eu

# Diagnostic features are switched on only by source/src/build_profile.h
# (make PROFILE=diagnostic). No other build file or source defines an
# ENABLE_DIAGNOSTIC_* macro (host tests in tests/ may), and the release
# profile refuses every one the source tests.

# Without rg every search below would find nothing and pass.
command -v rg >/dev/null || { echo "check-profiles.sh needs rg" >&2; exit 1; }

if rg -n --glob '!build_profile.h' \
  '#[[:space:]]*define[[:space:]]+ENABLE_DIAGNOSTIC_|-D[[:space:]]*ENABLE_DIAGNOSTIC_' \
  source Makefile scripts
then
  echo "An ENABLE_DIAGNOSTIC_* macro is defined outside" \
    "source/src/build_profile.h" >&2
  exit 1
fi

for name in $(rg -o --no-filename 'ENABLE_DIAGNOSTIC_[A-Z0-9_]+' source/src | \
  sort -u)
do
  if ! rg -qF "defined($name)" source/src/build_profile.h; then
    echo "$name is not refused by the release profile in" \
      "source/src/build_profile.h" >&2
    exit 1
  fi
done

echo "Build profile checks passed"
