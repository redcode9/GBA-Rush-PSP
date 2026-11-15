#!/bin/sh

set -eu

# Builds dist/GBA-Rush-PSP-VERSION.zip from the built EBOOT.PBP,
# exception.prx and media_engine.prx plus the documentation (make package). The archive is
# reproducible: every file gets the last commit's time (or
# SOURCE_DATE_EPOCH) and zip -X leaves out extra file attributes.
# Its root is PSP/GAME/GBARushPSP/, with the documentation under docs/, so
# extracting it at the root of ms0: or ef0: installs or upgrades in place.
# Next to it go the build manifest (scripts/release_manifest.py: commit,
# build id, compiler, flags and the SHA-256 of every built file), the
# corresponding source (dist/GBA-Rush-PSP-VERSION-source.zip, git archive
# of the tag named VERSION) and dist/SHA256SUMS.
# PROFILE=diagnostic (make package PROFILE=diagnostic) packages the
# diagnostic EBOOT instead: GBA-Rush-PSP-VERSION-diag.zip, its manifest and
# dist/SHA256SUMS-diag, installed in PSP/GAME/GBARushPSP-DIAG so that it
# keeps its own settings, saves and logs.

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

# The source zip must be the source of the binary being packaged.
if ! git rev-parse -q --verify "refs/tags/$version" >/dev/null; then
  echo "Missing tag $version: tag the release before packaging it" >&2
  exit 1
fi
if [ -n "$(git status --porcelain -- source/)" ]; then
  echo "source/ has uncommitted changes: commit them before packaging" >&2
  exit 1
fi
if [ "$(git rev-parse "refs/tags/$version:source")" != \
  "$(git rev-parse HEAD:source)" ]; then
  echo "Tag $version has a different source/ tree than HEAD" >&2
  exit 1
fi

documentation_files='LICENSE NOTICE README.md CHANGELOG.md CONTRIBUTING.md
docs/link-cable.md docs/ROADMAP-1.4.0.md docs/images/link-trade-room.jpg
docs/images/link-cable-club.jpg docs/images/link-trainer-card.jpg
source/copyright source/bios/README.md source/res/PIC1.png'

for required in source/EBOOT.PBP source/exception.prx \
  source/media_engine.prx source/TempGBA.elf $documentation_files
do
  if [ ! -f "$required" ]; then
    echo "Missing required file: $required" >&2
    exit 1
  fi
done

# The built EBOOT must be this tree's, in this profile, with the same core
# profile setting as the flags below: source/Makefile records them in
# build-config.stamp when it builds the objects.
build_id=$(${MAKE:-make} -s --no-print-directory -C source print-build-id)
build_config=$(${MAKE:-make} -s --no-print-directory -C source \
  PROFILE="$profile" print-config)
if [ "$(cat source/build-config.stamp 2>/dev/null)" != "$build_config" ]
then
  echo "source/EBOOT.PBP was not built as $build_config:" \
    "run make package VERSION=$version PROFILE=$profile" >&2
  exit 1
fi

# No diagnostic code in a release, and the version and build id it must show.
if [ "$profile" = release ]; then
  ${PYTHON:-python3} scripts/check-elf.py --build-id "$build_id" \
    source/TempGBA.elf
fi

release_name="GBA-Rush-PSP-$version$suffix"
release_root="dist/$release_name"
install_root="$release_root/PSP/GAME/$install_folder"
archive="dist/$release_name.zip"
checksum="$archive.sha256"
manifest="dist/$release_name.manifest.json"
# One source zip per version: both profiles are built from the same tag.
source_name="GBA-Rush-PSP-$version-source"
source_archive="dist/$source_name.zip"
sums="dist/SHA256SUMS$suffix"
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
rm -f "$archive" "$checksum" "$manifest" "$source_archive" "$sums"
mkdir -p "$install_root"

cp source/EBOOT.PBP "$install_root/EBOOT.PBP"
cp source/exception.prx "$install_root/exception.prx"
cp source/media_engine.prx "$install_root/media_engine.prx"
for document in $documentation_files
do
  mkdir -p "$install_root/docs/$(dirname "$document")"
  cp "$document" "$install_root/docs/$document"
done

find "$release_root" -exec env TZ=UTC touch -t "$archive_timestamp" {} +
(cd "$release_root" && TZ=UTC zip -q -X -D -r "../$release_name.zip" PSP)

sha256() {
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$@"
  else
    sha256sum "$@"
  fi
}

# The single-file checksum stays for one more release, for older tools.
(cd dist && sha256 "$release_name.zip") > "$checksum"

commit=$(git rev-parse HEAD)
tree_clean=true
if [ -n "$(git status --porcelain)" ]; then
  tree_clean=false
fi
compiler_version=$(psp-gcc --version)
cflags=$(${MAKE:-make} -s --no-print-directory -C source PROFILE="$profile" \
  print-flags)
pspsdk=$(psp-config --pspsdk-path)
pspdev=$(psp-config --pspdev-path)
${PYTHON:-python3} scripts/release_manifest.py write --output "$manifest" \
  --version "$version" --profile "$profile" --commit "$commit" \
  --build-id "$build_id" --tree-clean "$tree_clean" \
  --source-date-epoch "$release_epoch" --compiler "$compiler_version" \
  --cflags="$cflags" --pspsdk "$pspsdk" --pspdev "$pspdev" \
  --source-dir source
# autocrlf off: the zip holds the repository's bytes on every host.
git -c core.autocrlf=false archive --format=zip \
  --prefix="$source_name/" -o "$source_archive" "refs/tags/$version"
(cd dist && sha256 "$release_name.zip" "$release_name.manifest.json" \
  "$source_name.zip") > "$sums"

echo "Created $archive"
echo "Created $checksum"
echo "Created $manifest"
echo "Created $source_archive"
echo "Created $sums"
