#!/bin/sh
set -eu

# Bounded model checks with CBMC (make check-cbmc) of link_net.c's datagram
# handling and of the savestate trailer decoder. Not part of make check,
# since it needs CBMC; tree-check only makes sure this script and the
# link_net proof exist and keep unwinding assertions on.

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cbmc_bin=${CBMC:-cbmc}

if ! command -v "$cbmc_bin" >/dev/null 2>&1; then
  echo "CBMC 6.11 or newer is required for the bounded proof gate" >&2
  exit 2
fi

# --unwinding-assertions fails the proof when a loop needs more iterations
# than --unwind allows, so the bound is checked rather than assumed.
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

# The proof assumes queues of 4 messages, so link_net.c is built with them.
# Unwind 7 is one more than the most rounds any loop can make: the proof's
# 52-byte datagram holds at most 6 messages after its 40-byte header, which
# bounds both loops of link_net_receive, and every other loop walks at most
# a queue of 4. CBMC unrolls each loop to the full bound however few rounds
# it can make, so a larger bound proves nothing more and only costs memory.
#
# The proof defines memcpy itself. CBMC's own copies through an array of the
# symbolic size n, which its solver encodes with array theory: the proof ran
# out of CI's 16 GB with it, and out of 1.5 GB even with every loop cut to
# one round, while with the byte loop the whole proof peaks near 1.1 GB.
# It proves the same:
# - the preconditions are those of CBMC 6.11's memcpy, so every call is
#   still checked for overlap and for n readable and n writable bytes;
# - the loop stores src[i] in dst[i] for each i below n, the bytes and
#   values CBMC's copy writes, and --pointer-check and --bounds-check also
#   check each access;
# - its unwind bound 11 is one more than the largest copy: 10 bytes, what a
#   52-byte datagram leaves for a record's data after the header and the
#   record's 2-byte prefix, while the proof queues at most 9 bytes per
#   message. The unwinding assertion fails the proof if a copy is larger,
#   so no copy is cut short.
# Every other property of the proof is unchanged. The only checks dropped
# are two conversions of n to signed and back inside CBMC's memcpy, for its
# temporary array's size; w_ok(dst, n) already rules out such an n.
run_proof link-net 7 --unwindset memcpy.0:11 -DLINK_NET_QUEUE=4u \
  "$repo_root/proofs/cbmc/prove_link_net.c" \
  "$repo_root/source/src/link_net.c"

# The proof supplies its own crc32, so zlib's crc32.c is not linked in.
run_proof savestate-trailer 70 -DZLIB_CONST \
  "$repo_root/proofs/cbmc/prove_savestate_trailer.c" \
  "$repo_root/source/src/savestate_trailer.c"

echo "CBMC bounded proofs passed"
