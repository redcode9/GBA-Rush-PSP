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

check_assumptions()
{
  name=$1
  unwind=$2
  shift 2
  echo "CBMC assumption cover: $name"
  coverage_output=$("$cbmc_bin" --c99 --object-bits 12 \
    --no-standard-checks --no-assertions --cover assume \
    --unwind "$unwind" --verbosity 4 -I "$repo_root/source/src" \
    "$@" 2>&1)
  if printf '%s\n' "$coverage_output" |
     grep -q 'after assume(.*): FAILED'; then
    printf '%s\n' "$coverage_output" >&2
    echo "CBMC proof '$name' has a vacuous assumption" >&2
    exit 1
  fi
  if ! printf '%s\n' "$coverage_output" |
       grep -q 'after assume(.*): SATISFIED'; then
    printf '%s\n' "$coverage_output" >&2
    echo "CBMC proof '$name' exposed no reachable assumption" >&2
    exit 1
  fi
}

run_proof queues 34 \
  "$repo_root/proofs/cbmc/prove_queues.c" \
  "$repo_root/source/src/link_spsc_ring.c" \
  "$repo_root/source/src/link_tx_queue.c"
check_assumptions queues 34 \
  "$repo_root/proofs/cbmc/prove_queues.c" \
  "$repo_root/source/src/link_spsc_ring.c" \
  "$repo_root/source/src/link_tx_queue.c"

run_proof queue-payload-bounds 154 \
  "$repo_root/proofs/cbmc/prove_queue_payload_bounds.c" \
  "$repo_root/source/src/link_spsc_ring.c" \
  "$repo_root/source/src/link_tx_queue.c"
check_assumptions queue-payload-bounds 154 \
  "$repo_root/proofs/cbmc/prove_queue_payload_bounds.c" \
  "$repo_root/source/src/link_spsc_ring.c" \
  "$repo_root/source/src/link_tx_queue.c"

run_proof transport-scheduler 32 \
  "$repo_root/proofs/cbmc/prove_link_transport_scheduler.c" \
  "$repo_root/proofs/cbmc/zlib_stub.c" \
  "$repo_root/source/src/link_transport_scheduler.c" \
  "$repo_root/source/src/link_transport_lanes.c" \
  "$repo_root/source/src/link_spsc_ring.c"

run_proof protocol 6 \
  "$repo_root/proofs/cbmc/prove_link_protocol.c" \
  "$repo_root/proofs/cbmc/zlib_stub.c" \
  "$repo_root/source/src/link_protocol.c"

run_proof protocol-v3 6 \
  "$repo_root/proofs/cbmc/prove_link_protocol_v3.c" \
  "$repo_root/proofs/cbmc/zlib_stub.c" \
  "$repo_root/source/src/link_protocol.c"

run_proof link-stream 3 \
  "$repo_root/proofs/cbmc/prove_link_stream.c" \
  "$repo_root/source/src/link_stream.c"
check_assumptions protocol 6 \
  "$repo_root/proofs/cbmc/prove_link_protocol.c" \
  "$repo_root/proofs/cbmc/zlib_stub.c" \
  "$repo_root/source/src/link_protocol.c"

run_proof journal 2 \
  "$repo_root/proofs/cbmc/prove_link_journal.c" \
  "$repo_root/source/src/link_journal.c"
check_assumptions journal 2 \
  "$repo_root/proofs/cbmc/prove_link_journal.c" \
  "$repo_root/source/src/link_journal.c"

run_proof serial 2 \
  "$repo_root/proofs/cbmc/prove_serial_arithmetic.c" \
  "$repo_root/proofs/cbmc/zlib_stub.c" \
  "$repo_root/source/src/adhoc_datagram.c" \
  "$repo_root/source/src/link_journal.c" \
  "$repo_root/source/src/link_protocol.c" \
  "$repo_root/source/src/link_spsc_ring.c" \
  "$repo_root/source/src/link_transport_lanes.c"

run_proof lifecycle 3 \
  "$repo_root/proofs/cbmc/prove_lifecycle_teardown.c" \
  "$repo_root/source/src/lifecycle_teardown.c"
check_assumptions lifecycle 3 \
  "$repo_root/proofs/cbmc/prove_lifecycle_teardown.c" \
  "$repo_root/source/src/lifecycle_teardown.c"

run_proof link-net 14 -DLINK_NET_QUEUE=4u \
  "$repo_root/proofs/cbmc/prove_link_net.c" \
  "$repo_root/source/src/link_net.c"

echo "CBMC bounded proofs passed"
