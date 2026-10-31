#!/bin/sh

set -eu

# Builds dist/GBA-Rush-PSP-VERSION.zip from the built EBOOT.PBP and
# exception.prx plus the documentation (make package). The archive is
# reproducible: every file gets the last commit's time (or
# SOURCE_DATE_EPOCH) and zip -X leaves out extra file attributes.

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

documentation_files='LICENSE NOTICE README.md CHANGELOG.md CONTRIBUTING.md
docs/link-cable.md docs/ROADMAP-1.4.0.md docs/images/link-trade-room.jpg
docs/images/link-cable-club.jpg docs/images/link-trainer-card.jpg
source/copyright source/res/PIC1.png'

for required in source/EBOOT.PBP source/exception.prx $documentation_files
do
  if [ ! -f "$required" ]; then
    echo "Missing required file: $required" >&2
    exit 1
  fi
done

release_name="GBA-Rush-PSP-$version"
release_root="dist/$release_name"
install_root="$release_root/GBARushPSP"
archive="dist/$release_name.zip"
checksum="$archive.sha256"
release_epoch=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%at)}

case "$release_epoch" in
  *[!0-9]*|'')
    echo "Invalid release timestamp: $release_epoch" >&2
    exit 1
    ;;
esac

# BSD date takes -r SECONDS, GNU date -d @SECONDS.
if archive_timestamp=$(date -u -r "$release_epoch" +%Y%m%d%H%M.%S \
  2>/dev/null)
then
  :
elif archive_timestamp=$(date -u -d "@$release_epoch" +%Y%m%d%H%M.%S \
  2>/dev/null)
then
  :
else
  echo "Cannot convert release timestamp: $release_epoch" >&2
  exit 1
fi

rm -rf "$release_root"
rm -f "$archive" "$checksum"
mkdir -p "$install_root"

cp source/EBOOT.PBP "$install_root/EBOOT.PBP"
cp source/exception.prx "$install_root/exception.prx"
for document in $documentation_files
do
  mkdir -p "$release_root/$(dirname "$document")"
  cp "$document" "$release_root/$document"
done

find "$release_root" -exec env TZ=UTC touch -t "$archive_timestamp" {} +
(cd dist && TZ=UTC zip -q -X -D -r "$release_name.zip" "$release_name")

if command -v shasum >/dev/null 2>&1; then
  (cd dist && shasum -a 256 "$release_name.zip") > "$checksum"
else
  (cd dist && sha256sum "$release_name.zip") > "$checksum"
fi

echo "Created $archive"
echo "Created $checksum"
