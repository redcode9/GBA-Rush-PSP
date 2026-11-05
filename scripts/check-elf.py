#!/usr/bin/env python3
"""Check a release TempGBA.elf before it is packaged.

package-release.sh runs it on the release profile. It fails when the ELF
holds a diagnostic-only string or lacks the version or the build id, or
when its load image leaves less startup heap on the PSP-1000 than
docs/RAM-BUDGET.md allows. Until that document gives those numbers the
heap is only reported. If scripts/check-layout.py exists it runs too: a
failure there is a warning, or an error with --strict.
"""

import argparse
import pathlib
import re
import subprocess
import sys

import elf_info


ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_HEADER = ROOT / "source" / "src" / "main.h"
CHECK_LAYOUT = ROOT / "scripts" / "check-layout.py"
# Strings only the diagnostic profile may hold: the autorun control files
# (diag-autorun.txt and .result), input trace files and their magic, and
# the diagnostic EBOOT's title tag.
FORBIDDEN = (b"diag-autorun", b".gbit", b"GBIT", b"[DIAG]")


def source_version(path=MAIN_HEADER):
    """GBA_RUSH_VERSION as source/src/main.h defines it."""
    text = pathlib.Path(path).read_text(encoding="utf-8")
    match = re.search(r'^#define GBA_RUSH_VERSION "([^"]+)"', text, re.MULTILINE)
    if not match:
        raise ValueError(f"{path}: no GBA_RUSH_VERSION")
    return match.group(1)


def check(elf, build_id, strict=False):
    """The list of problems found in elf; empty when it can be packaged."""
    data = pathlib.Path(elf).read_bytes()
    problems = [f"forbidden string {text.decode()!r} found"
                for text in FORBIDDEN if text in data]
    for name, text in (("version", source_version()), ("build id", build_id)):
        if text.encode() not in data:
            problems.append(f"{name} {text!r} not found")

    image = elf_info.load_image_size(elf)
    budget = elf_info.ram_budget()
    if budget is None:
        print(f"{elf}: load image {image} bytes; PSP-1000 startup heap not"
              " measured (no numbers in docs/RAM-BUDGET.md)")
    else:
        memory, lowest = budget
        heap = memory - image
        print(f"{elf}: load image {image} bytes; estimated PSP-1000 startup"
              f" heap {heap} bytes, lowest allowed {lowest}")
        if heap < lowest:
            problems.append(f"estimated PSP-1000 startup heap {heap} bytes is"
                            f" below {lowest}")

    if CHECK_LAYOUT.exists():
        # shortcut: assumes check-layout.py takes the ELF and --strict, as
        # PRF-10.4 describes it; adjust when that script lands.
        command = [sys.executable, str(CHECK_LAYOUT), str(elf)]
        if strict:
            command.append("--strict")
        if subprocess.run(command, check=False).returncode != 0:
            if strict:
                problems.append("check-layout.py failed")
            else:
                print("check-elf.py: warning: check-layout.py failed",
                      file=sys.stderr)
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-id", required=True,
                        help="the id make -C source print-build-id prints")
    parser.add_argument("--strict", action="store_true",
                        help="fail when check-layout.py fails")
    parser.add_argument("elf")
    args = parser.parse_args(argv)
    try:
        problems = check(args.elf, args.build_id, args.strict)
    except (OSError, ValueError) as error:
        print(f"check-elf.py: {error}", file=sys.stderr)
        return 1
    for problem in problems:
        print(f"check-elf.py: {args.elf}: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
