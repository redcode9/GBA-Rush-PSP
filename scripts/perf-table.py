#!/usr/bin/env python3
# GBA Rush PSP
#
# Copyright (C) 2026 Piero Carrieri
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
"""Generate the per-model baseline table of docs/PERFORMANCE.md.

The archive holds raw PSP performance reports as
benchmarks/<model>/<game-code>-r<revision>/<version>/<run>.txt. The runs of
each group must share the settings compare-benchmarks checks. The output is
one Markdown table per version, newest first, with every title of that
version on every model of the archive; "not measured" marks a model that has
no reports for a title.
"""

import argparse
import importlib.util
import pathlib
import statistics
import sys


# compare-benchmarks.py has a dash in its name, so load it by path.
_SCRIPT = pathlib.Path(__file__).with_name("compare-benchmarks.py")
_SPEC = importlib.util.spec_from_file_location("compare_benchmarks", _SCRIPT)
compare_benchmarks = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(compare_benchmarks)

DEFAULT_ROOT = pathlib.Path(__file__).resolve().parents[1] / "benchmarks"

BEGIN = "<!-- perf-table:begin -->"
END = "<!-- perf-table:end -->"

# A cell whose key the reports of that group lack (older report formats).
MISSING = "—"
# Every cell of a title that a model has no reports for.
NOT_MEASURED = "not measured"
EMPTY = "No reports archived yet."

# Report keys that later stories add. PRF-2.1 (report v7) must use these
# work-time and over-budget names; the table shows the over-budget count
# per minute of capture, count * 60e6 / duration-us.
KEYS = {
    "bios": "bios",                            # CMP-1.4
    "build": "build-id",                       # ENG-1.6; else "version"
    "date": "capture-local-time",              # PRF-1.3
    "work-p50": "work-time-p50-us",            # PRF-2.1
    "work-p99": "work-time-p99-us",            # PRF-2.1
    "over-budget": "frames-over-1-5x-budget",  # PRF-2.1
}

COLUMNS = (
    "Title", "Model", "fps median (min-max)", "Work p50/p99 (us)",
    "Frame p99 (us)", "Over 1.5x budget /min", "Underruns", "Renderer",
    "Clock (MHz)", "BIOS", "Build", "Runs", "Date",
)


def version_key(version):
    """Sort key of a version directory such as 1.4.3."""
    try:
        return tuple(int(part) for part in version.split("."))
    except ValueError:
        raise ValueError(f"version is not dotted numbers: {version}") from None


def shared(reports, key):
    """The value every run reports for key, or None when they lack it."""
    values = {report.get(key) for report in reports}
    if len(values) > 1:
        raise ValueError(f"runs disagree on {key}")
    return values.pop()


def median_cell(reports, key, display, scale=lambda report: 1.0):
    """Median over the runs of the integer key times scale(report), or
    MISSING when a run lacks the key."""
    if any(key not in report for report in reports):
        return MISSING
    return display.format(statistics.median(
        compare_benchmarks.number(report, key) * scale(report)
        for report in reports))


def summarize(reports):
    """The ROM title and the metric cells of one group of runs."""
    fps = compare_benchmarks.summary(
        [compare_benchmarks.throughput(report) for report in reports])
    work = [median_cell(reports, KEYS[key], "{:.0f}")
            for key in ("work-p50", "work-p99")]
    dates = [report.get(KEYS["date"]) for report in reports]
    return shared(reports, "rom-title") or "", (
        "{:.2f} ({:.2f}-{:.2f})".format(*fps),
        MISSING if MISSING in work else "/".join(work),
        median_cell(reports, "frame-time-p99-us", "{:.0f}"),
        # throughput() above already rejected a duration that is not > 0.
        median_cell(reports, KEYS["over-budget"], "{:.1f}",
                    lambda report: 60_000_000.0 /
                    compare_benchmarks.number(report, "duration-us")),
        median_cell(reports, "audio-underruns", "{:.0f}"),
        shared(reports, "renderer") or MISSING,
        shared(reports, "clock-mhz") or MISSING,
        shared(reports, KEYS["bios"]) or MISSING,
        shared(reports, KEYS["build"]) or shared(reports, "version"),
        str(len(reports)),
        MISSING if None in dates else max(dates),
    )


def collect(root):
    """The summarized groups of the archive, keyed (model, game, version).

    Every file but root/README.md must be a .txt report three directories
    down, and a group's reports must match the game and version directories
    they are in.
    """
    root = pathlib.Path(root)
    if not root.is_dir():
        raise ValueError(f"not a directory: {root.as_posix()}")
    files = sorted((path.relative_to(root).as_posix(), path)
                   for path in root.rglob("*") if path.is_file())
    groups = {}
    for name, path in files:
        if name == "README.md":
            continue
        parts = name.split("/")
        if len(parts) != 4 or not name.endswith(".txt"):
            raise ValueError("not a <model>/<game-code>-r<revision>/"
                             f"<version>/<run>.txt report: {name}")
        groups.setdefault(tuple(parts[:3]), []).append(
            compare_benchmarks.read_report(path.as_posix()))

    rows = {}
    for (model, game, version), reports in groups.items():
        try:
            compare_benchmarks.validate_matching_runs(reports, [])
            version_key(version)
            reported = "{}-r{}".format(reports[0].get("game-code"),
                                       reports[0].get("rom-revision"))
            if game != reported:
                raise ValueError(f"reports are for {reported}")
            if shared(reports, "version") != version:
                raise ValueError("reports are from version "
                                 f"{shared(reports, 'version')}")
            rows[model, game, version] = summarize(reports)
        except ValueError as error:
            raise ValueError(f"{model}/{game}/{version}: {error}") from None
    return rows


def render(rows):
    """Markdown tables of the summarized groups, one per version."""
    if not rows:
        return EMPTY
    models = sorted({model for model, _, _ in rows})
    versions = sorted({version for _, _, version in rows}, key=version_key,
                      reverse=True)
    blocks = []
    for version in versions:
        lines = [f"### {version}", "",
                 "| " + " | ".join(COLUMNS) + " |",
                 "|" + "---|" * len(COLUMNS)]
        for game in sorted({game for _, game, seen in rows
                            if seen == version}):
            measured = [rows[model, game, version] for model in models
                        if (model, game, version) in rows]
            title = measured[0][0]
            title = f"{title} ({game})" if title else game
            for model in models:
                _, cells = rows.get((model, game, version), (
                    None, (NOT_MEASURED,) * (len(COLUMNS) - 2)))
                lines.append("| " + " | ".join((title, model) + cells) +
                             " |")
        blocks.append("\n".join(lines))
    return "\n\n".join(blocks)


def main(argv=None):
    """Exit status: 0 on success, 2 on an invalid archive or a document
    without the perf-table markers."""
    parser = argparse.ArgumentParser(
        description="Generate the per-model baseline table from the "
        "archived PSP performance reports."
    )
    parser.add_argument("--root", type=pathlib.Path, default=DEFAULT_ROOT,
                        help="report archive (default: benchmarks/)")
    parser.add_argument("--update", type=pathlib.Path, metavar="FILE",
                        help="replace the table between the perf-table "
                        "markers of FILE instead of printing it")
    arguments = parser.parse_args(argv)

    try:
        table = render(collect(arguments.root))
        if arguments.update is None:
            print(table)
            return 0
        text = arguments.update.read_text(encoding="utf-8")
        head, begin, rest = text.partition(BEGIN)
        _, end, tail = rest.partition(END)
        if not begin or not end:
            raise ValueError(f"{arguments.update.as_posix()}: missing "
                             f"{BEGIN} and {END}")
        with open(arguments.update, "w", encoding="utf-8",
                  newline="\n") as document:
            document.write(f"{head}{BEGIN}\n{table}\n{END}{tail}")
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
