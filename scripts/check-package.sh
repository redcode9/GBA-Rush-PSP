#!/bin/sh

set -eu

# Checks the archive made by package-release.sh (make package, or make
# artifact-smoke on its own): no user files, ROMs or BIOS dumps, exactly the
# approved files under PSP/GAME/GBARushPSP/, no symbolic links, contents
# identical to the tree, and a matching SHA-256 file.

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
source/copyright source/bios/README.md source/res/PIC1.png'

for required in "$archive" "$checksum" source/EBOOT.PBP \
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

install_dir="PSP/GAME/GBARushPSP"

{
  echo "$install_dir/EBOOT.PBP"
  echo "$install_dir/exception.prx"
  echo "$install_dir/media_engine.prx"
  for document in $documentation_files
  do
    echo "$install_dir/docs/$document"
  done
} | LC_ALL=C sort > "$expected_manifest"

# List the archive's files, without directory entries.
unzip -Z1 "$archive" | sed '/\/$/d' | LC_ALL=C sort > "$actual_manifest"

# Extracting the archive over an install would overwrite these.
if user_files=$(grep -i -E \
  '(^|/)dir\.ini$|\.(cfg|sav|svs|gba|agb|gbz|bin)$' "$actual_manifest")
then
  echo "Release archive contains user files, ROMs or BIOS dumps:" >&2
  echo "$user_files" >&2
  exit 1
fi

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

package_root="$temporary_root/extracted/$install_dir"
cmp source/EBOOT.PBP "$package_root/EBOOT.PBP"
cmp source/exception.prx "$package_root/exception.prx"
cmp source/media_engine.prx "$package_root/media_engine.prx"
for document in $documentation_files
do
  cmp "$document" "$package_root/docs/$document"
done

if command -v shasum >/dev/null 2>&1; then
  (cd dist && shasum -a 256 -c "$release_name.zip.sha256")
else
  (cd dist && sha256sum -c "$release_name.zip.sha256")
fi

echo "Release package checks passed"
