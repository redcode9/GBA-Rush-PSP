#!/usr/bin/env python3
"""Write or verify the build manifest of a release package.

write: package-release.sh records what was built (version, build profile,
channel, commit, build id, compiler and flags) and the SHA-256 of every built
file in dist/GBA-Rush-PSP-<version>[-diag].manifest.json.
verify: check-package.sh checks the files extracted from the zip against it.
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import sys

import elf_info


MANIFEST_FORMAT = 1
# Built files hashed from source/; all but the ELF ship in the zip.
BUILT_FILES = ("EBOOT.PBP", "exception.prx", "media_engine.prx", "TempGBA.elf")
CHANNELS = ("stable", "prerelease", "dev")
PROFILES = ("release", "diagnostic")
# A path from the root or a drive letter: alone, after -I, -L or -isystem,
# or after "=".
ABSOLUTE_PATH = re.compile(r"(?:^(?:-I|-L|-isystem)?|=)(?:/|[A-Za-z]:[\\/])")


def channel_for(version, override=None):
    """stable for X.Y.Z, prerelease for X.Y.Z-rc.N, dev otherwise."""
    if override:
        if override not in CHANNELS:
            raise ValueError(f"unknown channel: {override}")
        return override
    if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        return "stable"
    if re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+-rc\.[0-9]+", version):
        return "prerelease"
    return "dev"


def portable_cflags(cflags, sdk_paths):
    """CFLAGS with each SDK path written as its variable, as in $PSPSDK.

    sdk_paths maps a variable name to the path psp-config gives for it.
    Any other absolute path is an error: the manifest must not record the
    build machine's layout.
    """
    # The longest path first: PSPSDK lies inside PSPDEV.
    for name, path in sorted(sdk_paths.items(), key=lambda item: -len(item[1])):
        path = path.rstrip("/\\")
        if path:
            cflags = cflags.replace(path, "$" + name)
    for token in cflags.split():
        if ABSOLUTE_PATH.search(token):
            raise ValueError(f"absolute path left in CFLAGS: {token}")
    return cflags


def file_entry(path):
    data = pathlib.Path(path).read_bytes()
    return {"sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


def read_manifest(path):
    """The manifest as a dict; fails on a format this script does not know."""
    with open(path, encoding="utf-8") as stream:
        manifest = json.load(stream)
    if not isinstance(manifest, dict) or manifest.get("format") != MANIFEST_FORMAT:
        raise ValueError(f"{path}: not a format {MANIFEST_FORMAT} manifest")
    return manifest


def write(args):
    source = pathlib.Path(args.source_dir)
    manifest = {
        "format": MANIFEST_FORMAT,
        "version": args.version,
        # make PROFILE=..., as source/Makefile names it.
        "profile": args.profile,
        "channel": channel_for(args.version, os.environ.get("CHANNEL")),
        "commit": args.commit,
        # The id the EBOOT shows and writes (make -C source print-build-id).
        "build_id": args.build_id,
        "tree_clean": args.tree_clean == "true",
        "source_date_epoch": args.source_date_epoch,
        "compiler": args.compiler.partition("\n")[0].rstrip(),
        "pspdev_image": os.environ.get("PSPDEV_IMAGE") or None,
        "cflags": portable_cflags(
         args.cflags, {"PSPSDK": args.pspsdk, "PSPDEV": args.pspdev}),
        "files": {name: file_entry(source / name) for name in BUILT_FILES},
        "elf_load_image_bytes": elf_info.load_image_size(source / "TempGBA.elf"),
    }
    with open(args.output, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(manifest, indent=2) + "\n")
    return 0


def verify(args):
    files = read_manifest(args.manifest)["files"]
    mismatches = 0
    for path in map(pathlib.Path, args.files):
        if file_entry(path)["sha256"] != files.get(path.name, {}).get("sha256"):
            print(f"{path.name} is not the file {args.manifest} lists",
                  file=sys.stderr)
            mismatches += 1
    return 1 if mismatches else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    writer = commands.add_parser("write", help="write the manifest")
    for option in ("--output", "--version", "--commit", "--build-id",
                   "--compiler", "--cflags", "--pspsdk", "--pspdev",
                   "--source-dir"):
        writer.add_argument(option, required=True)
    writer.add_argument("--profile", required=True, choices=PROFILES)
    writer.add_argument("--tree-clean", required=True, choices=("true", "false"))
    writer.add_argument("--source-date-epoch", required=True, type=int)
    verifier = commands.add_parser(
     "verify", help="check files against the hashes in a manifest")
    verifier.add_argument("--manifest", required=True)
    verifier.add_argument("files", nargs="+")
    args = parser.parse_args(argv)
    try:
        return write(args) if args.command == "write" else verify(args)
    except (OSError, ValueError) as error:
        print(f"release_manifest.py: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
