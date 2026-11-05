#!/usr/bin/env python3
"""Compare two replay state-hash files and name the first divergence.

A and B are each a full .gbsh file (the "GBSH v1" text documented in
source/src/state_hash.h) or a compact golden:

  GBSH-GOLDEN v1
  sha256=<64 lowercase hex digits>
  build=<id> game=<code> rev=<n> start=<frame>
  regions=<name>,<name>,...
  <frame> <hash> ...   (only the records whose frame is a multiple of 60)
  counters ...         (optional, compared like a full file's)

The golden's sha256 is that of the full .gbsh from its regions line through
its last record: the magic, identity and counters lines are left out, so
neither the build id, the start frame nor a counter changes it. A golden
checked out with CRLF line ends is read as if it had LF ones.

A different game code or revision, or a different region list, is refused.
A different build id is printed and the files are still compared; start= is
never compared. Two full files are compared frame by frame. Against a
golden, an equal sha256 means equal records; otherwise the tool names the
60-frame window of the first sampled divergence. A difference that heals
before the next sample shows only in the full files: regenerate the full
.gbsh with the reference build to find the first diverging frame.
Counters are compared by the names both counters lines hold: a different
value is a divergence, a name that one side lacks is printed (a newer build
may add a counter) and does not fail.

Exit status: 0 when the files match, 1 at the first divergence, 2 on a
usage, format, identity or region-list error.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path
from typing import Any

MAGIC = "GBSH v1"
GOLDEN_MAGIC = "GBSH-GOLDEN v1"
GOLDEN_STEP = 60
NAME = "[A-Za-z0-9._-]"
SHA256 = re.compile(r"sha256=([0-9a-f]{64})")
IDENTITY = re.compile(
    rf"build=({NAME}{{0,40}}) game=({NAME}{{4}}) rev=([0-9]+) start=[0-9]+"
)
REGIONS = re.compile(rf"regions=({NAME}+(?:,{NAME}+)*)")
COUNTERS = re.compile(rf"counters(?: {NAME}+=[0-9]+)+")
FRAME = re.compile(r"[0-9]+")
HASH = re.compile(r"[0-9a-f]{8}")
UNSAMPLED = ("an unsampled frame may differ first: compare the full .gbsh "
             "files, regenerated with the reference build, to find it")


class FormatError(Exception):
    """The file is not a valid .gbsh or golden."""


def header_line(lines: list[str], index: int, pattern: re.Pattern[str],
                path: str) -> re.Match[str]:
    """Match header line `index` (0-based) or raise FormatError."""
    match = pattern.fullmatch(lines[index]) if index < len(lines) else None
    if match is None:
        raise FormatError(f"{path}:{index + 1}: malformed header line")
    return match


def parse(data: bytes, path: str) -> dict[str, Any]:
    """Parse the bytes of a full .gbsh or of a golden."""
    if data.startswith(GOLDEN_MAGIC.encode() + b"\r\n"):
        data = data.replace(b"\r\n", b"\n")  # a golden's own bytes are never hashed
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError:
        raise FormatError(f"{path}: not ASCII text") from None
    if not text.endswith("\n"):
        raise FormatError(f"{path}: the last line has no LF (truncated?)")
    lines = text[:-1].split("\n")
    golden = lines[0] == GOLDEN_MAGIC
    if not golden and lines[0] != MAGIC:
        raise FormatError(f"{path}:1: neither {MAGIC!r} nor {GOLDEN_MAGIC!r}")
    first = 2 if golden else 1  # the identity line
    identity = header_line(lines, first, IDENTITY, path)
    regions = header_line(lines, first + 1, REGIONS, path).group(1).split(",")
    if golden:
        sha256 = header_line(lines, 1, SHA256, path).group(1)
    records: dict[int, tuple[str, ...]] = {}
    counters = ""
    last = -1
    for number in range(first + 3, len(lines) + 1):
        line = lines[number - 1]
        if line.startswith("counters"):
            if number != len(lines) or COUNTERS.fullmatch(line) is None:
                raise FormatError(f"{path}:{number}: bad or misplaced counters line")
            counters = line
            break
        fields = line.split(" ")
        if (len(fields) != len(regions) + 1 or FRAME.fullmatch(fields[0]) is None
                or not all(HASH.fullmatch(field) for field in fields[1:])):
            raise FormatError(f"{path}:{number}: malformed record")
        frame = int(fields[0])
        if frame <= last:
            raise FormatError(f"{path}:{number}: frame {frame} is out of order")
        if golden and frame % GOLDEN_STEP:
            raise FormatError(f"{path}:{number}: frame {frame} is not a golden sample")
        records[frame] = tuple(fields[1:])
        last = frame
    if not golden:
        body = data.split(b"\n", 2)[2]
        if counters:
            body = body[:-len(counters) - 1]
        sha256 = hashlib.sha256(body).hexdigest()
    return {
        "path": path,
        "golden": golden,
        "sha256": sha256,
        "build": identity.group(1),
        "rom": (identity.group(2), int(identity.group(3))),
        "regions": regions,
        "records": records,
        "counters": counters,
    }


def compare(a: dict[str, Any], b: dict[str, Any]) -> int:
    """Print how a and b differ and return the exit status."""
    if a["rom"] != b["rom"]:
        print(f"different ROMs: {a['path']} is {a['rom'][0]} rev {a['rom'][1]}, "
              f"{b['path']} is {b['rom'][0]} rev {b['rom'][1]}", file=sys.stderr)
        return 2
    if a["regions"] != b["regions"]:
        print("different region lists: " + ",".join(a["regions"]) + " / "
              + ",".join(b["regions"]), file=sys.stderr)
        return 2
    if a["build"] != b["build"]:
        print(f"different builds: {a['path']} build={a['build']}, "
              f"{b['path']} build={b['build']}")
    sampled = a["golden"] or b["golden"]
    if sampled and a["sha256"] == b["sha256"]:
        result = "match: same sha256"
    else:
        frames = sorted({
            frame for side in (a, b) for frame in side["records"]
            if not sampled or frame % GOLDEN_STEP == 0
        })
        previous = -1
        for frame in frames:
            hashes_a = a["records"].get(frame)
            hashes_b = b["records"].get(frame)
            if hashes_a != hashes_b:
                if hashes_a is None or hashes_b is None:
                    missing = a["path"] if hashes_a is None else b["path"]
                    where = f"frame {frame} missing in {missing}"
                else:
                    where = f"frame {frame} region " + ",".join(
                        name for name, x, y in zip(a["regions"], hashes_a, hashes_b)
                        if x != y
                    )
                if sampled:
                    # The previous sample matched, but an unsampled frame
                    # before it may have differed and healed.
                    print(f"first sampled divergence in frames "
                          f"{previous + 1}..{frame}: {where}")
                    print(UNSAMPLED)
                else:
                    print(where)
                return 1
            previous = frame
        if sampled:
            print("the sampled frames match but the sha256 differs")
            print(UNSAMPLED)
            return 1
        result = f"match: {len(frames)} frames"
    counters_a, counters_b = (
        dict(field.split("=") for field in side["counters"].split(" ")[1:])
        for side in (a, b)
    )
    for name in sorted(counters_a.keys() ^ counters_b.keys()):
        side = a if name in counters_a else b
        print(f"counter {name} is only in {side['path']}, not compared")
    differ = [name for name in counters_a
              if counters_b.get(name, counters_a[name]) != counters_a[name]]
    if differ:
        print("counters differ: " + ", ".join(
            f"{name}={counters_a[name]} / {name}={counters_b[name]}"
            for name in differ))
        return 1
    print(result)
    return 0


def main(argv: list[str] | None = None) -> int:
    """Compare A with B as the module docstring describes."""
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("a", metavar="A", help="full .gbsh or compact golden")
    parser.add_argument("b", metavar="B", help="full .gbsh or compact golden")
    args = parser.parse_args(argv)
    try:
        files = [parse(Path(path).read_bytes(), path) for path in (args.a, args.b)]
    except (OSError, FormatError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return compare(*files)


if __name__ == "__main__":
    raise SystemExit(main())
