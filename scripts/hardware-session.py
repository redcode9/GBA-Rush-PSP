#!/usr/bin/env python3
"""Print the sheet of a hardware test session, or check the test records.

docs/hardware-tests.md is the catalog: one `### <id>` section per test with
the fields Models, Minutes, Pass and Steps. docs/HARDWARE-RESULTS.md holds
one table row per run, and every row must name a test of the catalog. Its
builds table gives the full SHA-256 of each test build's EBOOT.PBP.
"""

import argparse
import pathlib
import re
import sys
import textwrap


ROOT = pathlib.Path(__file__).resolve().parents[1]
CATALOG = ROOT / "docs" / "hardware-tests.md"
RESULTS = ROOT / "docs" / "HARDWARE-RESULTS.md"

FIELDS = ("Models", "Minutes", "Pass", "Steps")
MODEL_CLASSES = ("PSP-1000", "second model")
MODELS = ("PSP-1000", "PSP-2000", "PSP-3000", "PSP-N1000", "PSP-E1000")
RESULTS_HEADER = ("Date", "Version", "EBOOT sha256", "Model", "Firmware/CFW",
                  "Storage", "Test id", "Result", "Minutes", "Evidence")
BUILDS_HEADER = ("EBOOT.PBP", "Full SHA-256", "Built from", "Notes")
OUTCOMES = ("pass", "fail", "partial")

TEST_ID = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")
FIELD = re.compile(r"- (\w+): (.*)")


def parse_catalog(text):
    """{id: {"line", "Models" (list), "Minutes" (int), "Pass", "Steps"}}."""
    catalog = {}
    test = field = None
    for number, line in enumerate(text.splitlines(), 1):
        if line.startswith("#"):
            test = field = None
            if line.startswith("### "):
                test_id = line[4:].strip()
                if not TEST_ID.fullmatch(test_id):
                    raise ValueError(f"line {number}: bad test id '{test_id}'")
                if test_id in catalog:
                    raise ValueError(f"line {number}: duplicate test id "
                                     f"'{test_id}'")
                test = catalog[test_id] = {"line": number}
            continue
        if test is None:
            continue
        match = FIELD.fullmatch(line)
        if match and match.group(1) in FIELDS:
            field = match.group(1)
            test[field] = match.group(2).strip()
        elif field and line[:1].isspace() and line.strip():
            test[field] += " " + line.strip()
        else:
            field = None
    for test_id, test in catalog.items():
        where = f"line {test['line']}: test '{test_id}'"
        for name in FIELDS:
            if not test.get(name):
                raise ValueError(f"{where} has no {name}")
        test["Models"] = [m.strip() for m in test["Models"].split(",")]
        for model in test["Models"]:
            if model not in MODEL_CLASSES:
                raise ValueError(f"{where}: unknown model class '{model}'")
        if not test["Minutes"].isdigit():
            raise ValueError(f"{where}: Minutes is not a whole number")
        test["Minutes"] = int(test["Minutes"])
    return catalog


def cells_of(line):
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def check_row(row):
    """The reason a results row is malformed, or None."""
    if not re.fullmatch(r"\d{4}-\d{2}-\d{2}", row["Date"]):
        return f"date '{row['Date']}' is not YYYY-MM-DD"
    digest = row["EBOOT sha256"]
    if digest != "unknown" and not re.fullmatch(r"[0-9a-f]{12}", digest):
        return f"EBOOT sha256 '{digest}' is not 12 hex digits or 'unknown'"
    for model in row["Model"].split(" + "):
        if model not in MODELS:
            return f"unknown model '{model}'"
    if row["Result"] not in OUTCOMES:
        return f"result '{row['Result']}' is not one of {', '.join(OUTCOMES)}"
    minutes = row["Minutes"]
    if minutes != "unknown" and not re.fullmatch(r"\d+(\.\d+)?", minutes):
        return f"minutes '{minutes}' is not a number or 'unknown'"
    for name in RESULTS_HEADER:
        if not row[name]:
            return f"empty {name}"
    return None


def check_build(cells):
    """The reason a builds table row is malformed, or None."""
    short, full = (cell.strip("`") for cell in cells[:2])
    if not re.fullmatch(r"[0-9a-f]{64}", full):
        return f"full SHA-256 '{full}' is not 64 lower-case hex digits"
    if short != full[:12]:
        return f"EBOOT.PBP '{short}' is not the start of its full SHA-256"
    if not cells[2]:
        return "empty Built from"
    return None


def parse_results(text):
    """The rows of every results table, as dicts with their line number.

    Builds tables are checked too, but their rows are not results."""
    rows = []
    header = None
    for number, line in enumerate(text.splitlines(), 1):
        if not line.startswith("|"):
            continue
        cells = cells_of(line)
        if tuple(cells) in (RESULTS_HEADER, BUILDS_HEADER):
            header = tuple(cells)
            continue
        if all(re.fullmatch(r":?-+:?", cell) for cell in cells):
            continue
        if header is None:
            raise ValueError(f"line {number}: table row before the header")
        if len(cells) != len(header):
            raise ValueError(f"line {number}: {len(cells)} columns, expected "
                             f"{len(header)}")
        if header == BUILDS_HEADER:
            problem = check_build(cells)
            if problem:
                raise ValueError(f"line {number}: {problem}")
            continue
        row = dict(zip(RESULTS_HEADER, cells), line=number)
        problem = check_row(row)
        if problem:
            raise ValueError(f"line {number}: {problem}")
        rows.append(row)
    return rows


def validate(rows, catalog):
    """Reject a row whose test id is not in the catalog."""
    for row in rows:
        if row["Test id"] not in catalog:
            raise ValueError(f"line {row['line']}: unknown test id "
                             f"'{row['Test id']}'")


def release_tests(text):
    """The ids on the `tests:` lines of docs/releases/<version>.txt."""
    ids = []
    for line in text.splitlines():
        key, _, value = line.partition(":")
        if key.strip() == "tests":
            ids += [i.strip() for i in value.split(",") if i.strip()]
    return ids


def session_sheet(ids, catalog):
    """The text to follow at the console for the tests in ids."""
    for test_id in ids:
        if test_id not in catalog:
            raise ValueError(f"unknown test id '{test_id}'")
    total = sum(catalog[i]["Minutes"] for i in ids)
    lines = [f"Hardware session: {len(ids)} tests, about {total} minutes", ""]
    for test_id in ids:
        test = catalog[test_id]
        lines.append(f"[ ] {test_id}: {', '.join(test['Models'])}, "
                     f"{test['Minutes']} min")
        for name in ("Steps", "Pass"):
            lines.append(textwrap.fill(
             f"{name}: {test[name]}", 78, initial_indent="    ",
             subsequent_indent="      "))
        lines.append("")
    lines.append("Record each run in docs/HARDWARE-RESULTS.md: "
                 + " | ".join(RESULTS_HEADER))
    return "\n".join(lines)


def check(catalog_path=CATALOG, results_path=RESULTS):
    """Parse and cross-check the catalog and the results table."""
    try:
        catalog = parse_catalog(catalog_path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise ValueError(f"{catalog_path.name}: {error}") from None
    try:
        rows = parse_results(results_path.read_text(encoding="utf-8"))
        validate(rows, catalog)
    except ValueError as error:
        raise ValueError(f"{results_path.name}: {error}") from None
    return catalog, rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--tests", help="comma-separated test ids")
    group.add_argument("--release", help="read docs/releases/<RELEASE>.txt")
    group.add_argument("--check", action="store_true",
                       help="check the catalog and the results table")
    args = parser.parse_args(argv)
    try:
        catalog, rows = check()
        if args.check:
            print(f"Hardware tests: {len(catalog)} in the catalog, "
                  f"{len(rows)} result rows valid")
            return 0
        if args.release:
            name = f"docs/releases/{args.release}.txt"
            if not (ROOT / name).is_file():
                raise ValueError(f"{name} not found; list the tests with "
                                 "--tests")
            ids = release_tests((ROOT / name).read_text(encoding="utf-8"))
            if not ids:
                raise ValueError(f"{name} has no tests line")
        else:
            ids = [i.strip() for i in args.tests.split(",") if i.strip()]
        print(session_sheet(ids, catalog))
    except ValueError as error:
        print(f"hardware-session: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
