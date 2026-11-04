#!/bin/sh

set -eu

# Checks the archive made by package-release.sh (make package, or make
# artifact-smoke on its own): exactly the approved files, no symbolic links,
# contents identical to the tree and to the build manifest's hashes, and
# matching SHA-256 files.

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
manifest="dist/$release_name.manifest.json"
sums=dist/SHA256SUMS

documentation_files='LICENSE NOTICE README.md CHANGELOG.md CONTRIBUTING.md
docs/link-cable.md docs/ROADMAP-1.4.0.md docs/images/link-trade-room.jpg
docs/images/link-cable-club.jpg docs/images/link-trainer-card.jpg
source/copyright source/bios/README.md source/res/PIC1.png'

for required in "$archive" "$checksum" "$manifest" "$sums" source/EBOOT.PBP \
  source/exception.prx source/media_engine.prx $documentation_files
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
  echo "$release_name/GBARushPSP/media_engine.prx"
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
${PYTHON:-python3} scripts/release_manifest.py verify --manifest "$manifest" \
  "$package_root/GBARushPSP/EBOOT.PBP" \
  "$package_root/GBARushPSP/exception.prx" \
  "$package_root/GBARushPSP/media_engine.prx"
cmp source/EBOOT.PBP "$package_root/GBARushPSP/EBOOT.PBP"
cmp source/exception.prx "$package_root/GBARushPSP/exception.prx"
cmp source/media_engine.prx "$package_root/GBARushPSP/media_engine.prx"
for document in $documentation_files
do
  cmp "$document" "$package_root/$document"
done

sha256_check() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 -c "$@"
  else
    sha256sum -c "$@"
  fi
}

(cd dist && sha256_check "$release_name.zip.sha256")

# SHA256SUMS must cover this release's zip and manifest, not another's.
listed_files=$(awk '{ sub(/^\*/, "", $2); print $2 }' "$sums")
if [ "$listed_files" != "$release_name.zip
$release_name.manifest.json" ]; then
  echo "$sums does not list $release_name.zip and its manifest" >&2
  exit 1
fi
(cd dist && sha256_check SHA256SUMS)

echo "Release package checks passed"
