#!/bin/sh

set -eu

# Every translation table in source/src/text/ must list its strings in the
# order of enum MESSAGE_ITEM in message.h, or the menus show the wrong text.
# Each table entry carries a "// MSG_..." tag; the tags are compared to the
# enum names in order.

temporary_root=${TMPDIR:-/tmp}
expected=$(mktemp "$temporary_root/gbarush-messages-expected.XXXXXX")
actual=$(mktemp "$temporary_root/gbarush-messages-actual.XXXXXX")
trap 'rm -f "$expected" "$actual"' EXIT HUP INT TERM

awk '
  /enum MESSAGE_ITEM/ { inside = 1; next }
  inside && match($0, /(MSG|CH|MH)_[A-Z0-9_]+/) {
    name = substr($0, RSTART, RLENGTH)
    if(name != "MSG_END")
      print name
  }
  inside && /};/ { exit }
' source/src/message.h > "$expected"

for file in source/src/text/message_jpn.h source/src/text/message_eng.h \
 source/src/text/message_chs.h source/src/text/message_cht.h \
 source/src/text/message_ita.h
do
  awk '
    match($0, /\/\/[[:space:]]*(MSG|CH|MH)_[A-Z0-9_]+/) {
      name = substr($0, RSTART, RLENGTH)
      sub(/^\/\/[[:space:]]*/, "", name)
      print name
    }
  ' "$file" > "$actual"

  if ! cmp -s "$expected" "$actual"
  then
    echo "Message order differs in $file" >&2
    diff -u "$expected" "$actual" >&2 || true
    exit 1
  fi
done

echo "Message table checks passed"
