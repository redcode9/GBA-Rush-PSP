#!/bin/sh

set -eu

# Shows that a regression test catches the bug it was written for
# (make prove-regression REF=<tag> TEST=<name>). REF, an older tag or commit
# that has the bug, is checked out in build/prove/REF; the tests/ folder and
# the top-level Makefile of this checkout replace REF's own, while REF's
# source/ stays. TEST names a C test of the Makefile (test_link_net): its
# build/host/TEST and its ASan and UBSan build, build/sanitize/TEST, are
# built and run there. The last line is the outcome:
#   FAIL-AS-EXPECTED  exit 0: a run fails on REF, or a sanitizer reports an
#                     error there, so the test catches the bug
#   PASSES-ON-REF     exit 1: both runs pass on REF, so it does not
#   NOT-APPLICABLE    exit 2: the test does not build on REF
# Any other error exits with 3, a sanitizer build that fails where the plain
# build works included. The worktree is removed in every case.

if [ "$#" -ne 2 ]; then
  echo "Usage: $0 REF TEST" >&2
  exit 3
fi

ref=$1
name=$2
# REF becomes a folder under build/prove, so it must stay inside it.
case "$ref" in
  ''|[-./]*|*/|*..*|*//*|*[!0-9A-Za-z._/~^-]*)
    echo "Invalid REF: $ref" >&2
    exit 3
    ;;
esac
case "$name" in
  ''|[-.]*|*[!0-9A-Za-z._-]*)
    echo "Invalid TEST: $name" >&2
    exit 3
    ;;
esac

cd "$(dirname "$0")/.."

if [ ! -f "tests/$name.c" ]; then
  echo "No such C test: tests/$name.c" >&2
  exit 3
fi

if ! commit=$(git rev-parse --verify --quiet --short "$ref^{commit}"); then
  echo "Unknown REF: $ref" >&2
  exit 3
fi

worktree=build/prove/$ref
log=$worktree/prove-regression.log
result=3

remove_worktree()
{
  if ! git worktree remove --force "$worktree" 2>/dev/null; then
    rm -rf "$worktree"
    git worktree prune
  fi
  rmdir -p "${worktree%/*}" 2>/dev/null || true
}

# A run killed before its cleanup leaves its worktree behind.
remove_worktree
trap 'remove_worktree; exit "$result"' EXIT
trap 'exit' HUP INT TERM

git worktree add --quiet --detach "$worktree" "$commit"
rm -rf "$worktree/tests"
cp -R tests "$worktree/tests"
cp Makefile "$worktree/Makefile"

if ! ${MAKE:-make} -C "$worktree" "build/host/$name"; then
  echo "NOT-APPLICABLE: $name does not build on $ref ($commit)"
  result=2
  exit
fi
if ! ${MAKE:-make} -C "$worktree" "build/sanitize/$name"; then
  echo "build/sanitize/$name does not build where build/host/$name does" >&2
  exit
fi

status=0
for binary in "build/host/$name" "build/sanitize/$name"; do
  echo "./$binary" >>"$log"
  (cd "$worktree" && "./$binary") >>"$log" 2>&1 || status=$?
done
cat "$log"

# UBSan reports and carries on unless built with -fno-sanitize-recover.
if [ "$status" -ne 0 ] ||
   grep -q -e 'runtime error:' -e 'ERROR: [A-Za-z]*Sanitizer' "$log"; then
  echo "FAIL-AS-EXPECTED: $name fails on $ref ($commit)"
  result=0
else
  echo "PASSES-ON-REF: $name passes on $ref ($commit), so it does not catch the bug"
  result=1
fi
