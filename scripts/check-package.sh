#!/bin/sh

set -eu

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

for required in "$archive" "$checksum" source/EBOOT.PBP \
  source/exception.prx README.md CHANGELOG.md LICENSE
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
  echo "$release_name/CHANGELOG.md"
  echo "$release_name/GBARushPSP/EBOOT.PBP"
  echo "$release_name/GBARushPSP/exception.prx"
  echo "$release_name/LICENSE"
  echo "$release_name/README.md"
} | LC_ALL=C sort > "$expected_manifest"

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
cmp README.md "$package_root/README.md"
cmp CHANGELOG.md "$package_root/CHANGELOG.md"
cmp LICENSE "$package_root/LICENSE"

if command -v shasum >/dev/null 2>&1; then
  (cd dist && shasum -a 256 -c "$release_name.zip.sha256")
else
  (cd dist && sha256sum -c "$release_name.zip.sha256")
fi

echo "Release package checks passed"
