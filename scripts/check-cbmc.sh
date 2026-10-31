#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cbmc_bin=${CBMC:-cbmc}

if ! command -v "$cbmc_bin" >/dev/null 2>&1; then
  echo "CBMC 6.11 or newer is required for the bounded proof gate" >&2
  exit 2
fi

common_flags="--c99 --object-bits 12 --bounds-check --pointer-check
--div-by-zero-check --signed-overflow-check --pointer-overflow-check
--conversion-check --undefined-shift-check --enum-range-check
--unwinding-assertions --trace --verbosity 3"

run_proof()
{
  name=$1
  unwind=$2
  shift 2
  echo "CBMC proof: $name"
  # shellcheck disable=SC2086
  "$cbmc_bin" $common_flags --unwind "$unwind" \
    -I "$repo_root/source/src" "$@"
}

run_proof link-net 14 -DLINK_NET_QUEUE=4u \
  "$repo_root/proofs/cbmc/prove_link_net.c" \
  "$repo_root/source/src/link_net.c"

echo "CBMC bounded proofs passed"
