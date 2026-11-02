#!/bin/sh

set -eu

# Checks the archive made by package-release.sh (make package, or make
# artifact-smoke on its own): exactly the approved files, no symbolic links,
# contents identical to the tree, and a matching SHA-256 file.

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 VERSION" >&2
  exit 1
fi

version=$1
case "$version" in
  *[!0-9A-Za-z.-]*|'')
    echo "Invalid version: $version" >&2
    exit 1
    ;;
esac

release_name="GBA-Rush-PSP-$version"
archive="dist/$release_name.zip"
checksum="$archive.sha256"

documentation_files='LICENSE NOTICE README.md CHANGELOG.md CONTRIBUTING.md
docs/link-cable.md docs/ROADMAP-1.4.0.md docs/images/link-trade-room.jpg
docs/images/link-cable-club.jpg docs/images/link-trainer-card.jpg
source/copyright source/res/PIC1.png'

for required in "$archive" "$checksum" source/EBOOT.PBP \
  source/exception.prx $documentation_files
do
  if [ ! -f "$required" ]; then
    echo "Missing package input: $required" >&2
    exit 1
  fi
done

temporary_root=$(mktemp -d "${TMPDIR:-/tmp}/gba-rush-package.XXXXXX")
trap 'rm -rf "$temporary_root"' EXIT HUP INT TERM

expected_manifest="$temporary_root/expected.txt"
actual_manifest="$temporary_root/actual.txt"

{
  echo "$release_name/GBARushPSP/EBOOT.PBP"
  echo "$release_name/GBARushPSP/exception.prx"
  for document in $documentation_files
  do
    echo "$release_name/$document"
  done
} | LC_ALL=C sort > "$expected_manifest"

# List the archive's files, without directory entries.
unzip -Z1 "$archive" | sed '/\/$/d' | LC_ALL=C sort > "$actual_manifest"
if ! diff -u "$expected_manifest" "$actual_manifest"
then
  echo "Release archive does not match the approved manifest" >&2
  exit 1
fi

unzip -q "$archive" -d "$temporary_root/extracted"
if find "$temporary_root/extracted" -type l | rg -q .
then
  echo "Release archive contains a symbolic link" >&2
  exit 1
fi

package_root="$temporary_root/extracted/$release_name"
cmp source/EBOOT.PBP "$package_root/GBARushPSP/EBOOT.PBP"
cmp source/exception.prx "$package_root/GBARushPSP/exception.prx"
for document in $documentation_files
do
  cmp "$document" "$package_root/$document"
done

if command -v shasum >/dev/null 2>&1; then
  (cd dist && shasum -a 256 -c "$release_name.zip.sha256")
else
  (cd dist && sha256sum -c "$release_name.zip.sha256")
fi

echo "Release package checks passed"
