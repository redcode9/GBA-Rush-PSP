#!/bin/sh

set -eu

# Diagnostic features are switched on only by source/src/build_profile.h
# (make PROFILE=diagnostic). No other build file or source defines an
# ENABLE_DIAGNOSTIC_* macro (host tests in tests/ may), and the release
# profile refuses every one the source tests. The two exceptions, both
# needing PROFILE=diagnostic, are in source/Makefile:
# ENABLE_DIAGNOSTIC_CORE_PROFILE, which arm_mips.c reads without
# build_profile.h, and ENABLE_DIAGNOSTIC_HW_PROBE, which a diagnostic build
# only gets on request ([E] keeps the pattern below from matching its own
# line).

# Outside a Git checkout every search below would find nothing and pass.
git rev-parse --is-inside-work-tree >/dev/null 2>&1 ||
  { echo "check-profiles.sh needs a Git checkout" >&2; exit 1; }

# git grep --untracked leaves out ignored build output, as check-lifecycle.sh.
if git grep -nIE --untracked \
  '#[[:space:]]*define[[:space:]]+ENABLE_DIAGNOSTIC_|-D[[:space:]]*ENABLE_DIAGNOSTIC_' \
  -- source Makefile scripts ':(exclude)source/src/build_profile.h' |
  grep -vE '^source/Makefile:[0-9]+:CFLAGS [+]= -D[E]NABLE_DIAGNOSTIC_(CORE_PROFILE|HW_PROBE)[[:space:]]*$'
then
  echo "An ENABLE_DIAGNOSTIC_* macro is defined outside" \
    "source/src/build_profile.h" >&2
  exit 1
fi

for name in $(git grep -hoIE --untracked 'ENABLE_DIAGNOSTIC_[A-Z0-9_]+' \
  -- source/src | sort -u)
do
  if ! grep -qF "defined($name)" source/src/build_profile.h; then
    echo "$name is not refused by the release profile in" \
      "source/src/build_profile.h" >&2
    exit 1
  fi
done

echo "Build profile checks passed"
