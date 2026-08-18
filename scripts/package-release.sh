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

for required in source/EBOOT.PBP source/exception.prx LICENSE \
 docs/INSTALL.md CHANGELOG.md
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

rm -rf "$release_root"
rm -f "$archive" "$checksum"
mkdir -p "$install_root"

cp source/EBOOT.PBP "$install_root/EBOOT.PBP"
cp source/exception.prx "$install_root/exception.prx"
cp docs/INSTALL.md "$release_root/README.md"
cp CHANGELOG.md "$release_root/CHANGELOG.md"
cp LICENSE "$release_root/LICENSE"

(cd dist && zip -q -r "$release_name.zip" "$release_name")

if command -v shasum >/dev/null 2>&1; then
  (cd dist && shasum -a 256 "$release_name.zip") > "$checksum"
else
  (cd dist && sha256sum "$release_name.zip") > "$checksum"
fi

echo "Created $archive"
echo "Created $checksum"
