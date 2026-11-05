#!/bin/sh

set -eu

# Checks the archive made by package-release.sh (make package, or make
# artifact-smoke on its own): exactly the approved files, no symbolic links,
# contents identical to the tree and to the build manifest's hashes, a
# source zip holding exactly the files of the tag named VERSION, and
# matching SHA-256 files. PROFILE=diagnostic checks the diagnostic
# package (package-release.sh).

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

profile=${PROFILE:-release}
case "$profile" in
  release) suffix='' install_folder=GBARushPSP ;;
  diagnostic) suffix=-diag install_folder=GBARushPSP-DIAG ;;
  *)
    echo "Invalid profile: $profile (release or diagnostic)" >&2
    exit 1
    ;;
esac

release_name="GBA-Rush-PSP-$version$suffix"
archive="dist/$release_name.zip"
checksum="$archive.sha256"
manifest="dist/$release_name.manifest.json"
source_name="GBA-Rush-PSP-$version-source"
source_archive="dist/$source_name.zip"
sums="dist/SHA256SUMS$suffix"

documentation_files='LICENSE NOTICE README.md CHANGELOG.md CONTRIBUTING.md
docs/link-cable.md docs/ROADMAP-1.4.0.md docs/images/link-trade-room.jpg
docs/images/link-cable-club.jpg docs/images/link-trainer-card.jpg
source/copyright source/bios/README.md source/res/PIC1.png'

for required in "$archive" "$checksum" "$manifest" "$source_archive" "$sums" \
  source/EBOOT.PBP \
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
  echo "$release_name/$install_folder/EBOOT.PBP"
  echo "$release_name/$install_folder/exception.prx"
  echo "$release_name/$install_folder/media_engine.prx"
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
  "$package_root/$install_folder/EBOOT.PBP" \
  "$package_root/$install_folder/exception.prx" \
  "$package_root/$install_folder/media_engine.prx"
cmp source/EBOOT.PBP "$package_root/$install_folder/EBOOT.PBP"
cmp source/exception.prx "$package_root/$install_folder/exception.prx"
cmp source/media_engine.prx "$package_root/$install_folder/media_engine.prx"
for document in $documentation_files
do
  cmp "$document" "$package_root/$document"
done

# The source zip holds exactly the tag's files (GPL-2.0 section 3).
source_prefix="$source_name/"
git -c core.quotePath=false ls-tree -r --name-only "refs/tags/$version" \
  > "$temporary_root/tag-files.txt"
sed "s|^|$source_prefix|" "$temporary_root/tag-files.txt" | LC_ALL=C sort \
  > "$expected_manifest"
unzip -Z1 "$source_archive" | sed '/\/$/d' | LC_ALL=C sort \
  > "$actual_manifest"
if ! diff -u "$expected_manifest" "$actual_manifest"
then
  echo "Source archive does not hold exactly the files of tag $version" >&2
  exit 1
fi
for required in LICENSE NOTICE source/Makefile source/bios/Makefile \
  source/bios/README.md
do
  if ! rg -qxF "$source_prefix$required" "$actual_manifest"; then
    echo "Source archive is missing $required" >&2
    exit 1
  fi
done

sha256_check() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 -c "$@"
  else
    sha256sum -c "$@"
  fi
}

(cd dist && sha256_check "$release_name.zip.sha256")

# SHA256SUMS must cover this release's zips and manifest, not another's.
listed_files=$(awk '{ sub(/^\*/, "", $2); print $2 }' "$sums")
if [ "$listed_files" != "$release_name.zip
$release_name.manifest.json
$source_name.zip" ]; then
  echo "$sums does not list $release_name.zip, its manifest and" \
    "$source_name.zip" >&2
  exit 1
fi
(cd dist && sha256_check "${sums#dist/}")

echo "Release package checks passed"
