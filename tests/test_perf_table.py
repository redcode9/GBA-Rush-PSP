"""Tests for scripts/perf-table.py on synthetic report archives."""

import contextlib
import importlib.util
import io
import pathlib
import tempfile
import unittest


# The script name has a dash, so load it as a module by path.
SCRIPT = pathlib.Path(__file__).parents[1] / "scripts" / "perf-table.py"
SPEC = importlib.util.spec_from_file_location("perf_table", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

FIRE_RED = "psp-{}/BPRE-r0/1.4.3/run-{}.txt"
RUBY = "psp-{}/AXVE-r1/1.4.3/run-{}.txt"

# The psp-model (kuKernelGetModel()) of the consoles behind the fixtures.
PSP_MODEL = {"psp-1000": "0", "psp-2000": "1"}


def report(frames=600, clock=333, game="BPRE", revision=0,
           title="POKEMON FIRE", version="1.4.3", extra=None):
    """A finished 10-second report with the fields the table reads; archive()
    adds the psp-model of its directory unless extra sets one."""
    values = {
        "gba-rush-performance-report": "6",
        "version": version,
        "clock-mhz": str(clock),
        "renderer": "1",
        "scale": "6",
        "filter": "0",
        "frameskip-type": "0",
        "frameskip-value": "9",
        "sound-volume": "10",
        "rom-title": title,
        "game-code": game,
        "rom-revision": str(revision),
        "state": "stopped",
        "duration-us": "10000000",
        "emulated-frames": str(frames),
        "audio-underruns": "0",
        "frame-time-p99-us": "17000",
    }
    values.update(extra or {})
    return values


def ruby(frames=600):
    return report(frames, game="AXVE", revision=1, title="POKEMON RUBY")


def archive(root, runs):
    """Write the (relative path, report) pairs under root, in order."""
    for name, values in runs:
        if "psp-model" not in values:
            values = {"psp-model": PSP_MODEL[name.split("/")[0]], **values}
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("".join(f"{key}={value}\n"
                                for key, value in values.items()),
                        encoding="utf-8")
    return root


def rows(markdown):
    """Data rows of the generated tables as {(title, model): cells}."""
    table = {}
    for line in markdown.splitlines():
        cells = [cell.strip() for cell in line.strip("|").split("|")]
        if line.startswith("| ") and cells[1].startswith("psp-"):
            table[cells[0], cells[1]] = cells[2:]
    return table


class PerfTableTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.directory = pathlib.Path(directory.name)

    def test_groups_by_model_and_game(self):
        # The psp-2000 runs carry the PRF-2.1 and later keys; the psp-1000
        # ones, like 1.4.3 reports, do not.
        later = [{"work-time-p50-us": str(p50), "work-time-p99-us": str(p99),
                  "frames-over-1-5x-budget": str(over), "bios": "builtin",
                  "capture-local-time": date}
                 for p50, p99, over, date in (
                     (8000, 12000, 3, "2026-11-01T10:00"),
                     (8500, 13000, 1, "2026-11-03T10:00"),
                     (8250, 12500, 2, "2026-11-02T10:00"))]
        root = archive(self.directory, [
            (FIRE_RED.format(1000, 1), report(600)),
            (FIRE_RED.format(1000, 2), report(590)),
            (FIRE_RED.format(1000, 3), report(610)),
            (FIRE_RED.format(2000, 1), report(580, extra=later[0])),
            (FIRE_RED.format(2000, 2), report(570, extra=later[1])),
            (FIRE_RED.format(2000, 3), report(575, extra=later[2])),
        ])

        groups = MODULE.collect(root)
        table = rows(MODULE.render(groups))

        self.assertEqual(len(groups), 2)
        self.assertEqual(table, {
            ("POKEMON FIRE (BPRE-r0)", "psp-1000"): [
                "60.00 (59.00-61.00)", "—", "17000", "—", "0", "1",
                "333", "—", "1.4.3", "3", "—"],
            ("POKEMON FIRE (BPRE-r0)", "psp-2000"): [
                "57.50 (57.00-58.00)", "8250/12500", "17000", "12.0", "0",
                "1", "333", "builtin", "1.4.3", "3", "2026-11-03T10:00"],
        })

    def test_not_measured_cell(self):
        root = archive(self.directory, [
            *((FIRE_RED.format(model, run), report())
              for model in (1000, 2000) for run in (1, 2, 3)),
            *((RUBY.format(1000, run), ruby()) for run in (1, 2, 3)),
        ])

        table = rows(MODULE.render(MODULE.collect(root)))

        self.assertEqual(len(table), 4)
        self.assertEqual(table["POKEMON RUBY (AXVE-r1)", "psp-2000"],
                         ["not measured"] * 11)
        self.assertEqual(table["POKEMON RUBY (AXVE-r1)", "psp-1000"][0],
                         "60.00 (60.00-60.00)")

    def test_mismatched_group_rejected(self):
        root = archive(self.directory / "clock", [
            (FIRE_RED.format(1000, 1), report()),
            (FIRE_RED.format(1000, 2), report(clock=222)),
            (FIRE_RED.format(1000, 3), report()),
        ])
        # The table does not show frameskip, so only MATCH_FIELDS catches it.
        hidden = archive(self.directory / "hidden", [
            (FIRE_RED.format(1000, 1), report()),
            (FIRE_RED.format(1000, 2), report(extra={"frameskip-value": "2"})),
        ])

        with self.assertRaisesRegex(ValueError, "psp-1000/BPRE-r0/1.4.3: "
                                    "benchmark field does not match: "
                                    "clock-mhz"):
            MODULE.collect(root)
        with self.assertRaisesRegex(ValueError, "frameskip-value"):
            MODULE.collect(hidden)
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            status = MODULE.main(["--root", str(root)])
        self.assertEqual(status, 2)
        self.assertIn("ERROR:", errors.getvalue())
        self.assertIn("clock-mhz", errors.getvalue())

    def test_misplaced_reports_rejected(self):
        cases = {
            "file that is not a report": [
                (FIRE_RED.format(1000, 1), report()),
                ("psp-1000/BPRE-r0/1.4.3/notes.json", report())],
            "report outside a version directory": [
                ("psp-1000/BPRE-r0/run-1.txt", report())],
            "game directory of another revision": [
                ("psp-1000/BPRE-r1/1.4.3/run-1.txt", report())],
            "version directory of another release": [
                (FIRE_RED.format(1000, 1), report(version="1.4.2"))],
            "PSP-2000 reports under psp-1000": [
                (FIRE_RED.format(1000, 1), report(extra={"psp-model": "1"}))],
            "model directory that is not a PSP model": [
                ("psp-e1000/BPRE-r0/1.4.3/run-1.txt",
                 report(extra={"psp-model": "10"}))],
        }
        for name, runs in cases.items():
            with self.subTest(name), tempfile.TemporaryDirectory() as root:
                with self.assertRaises(ValueError):
                    MODULE.collect(archive(pathlib.Path(root), runs))

    def test_empty_archive(self):
        # benchmarks/README.md allows .md notes anywhere in the archive.
        notes = self.directory / "psp-1000" / "BPRE-r0" / "1.4.3"
        notes.mkdir(parents=True)
        for path in (self.directory / "README.md", notes / "notes.md"):
            path.write_text("notes", encoding="utf-8")

        self.assertEqual(MODULE.render(MODULE.collect(self.directory)),
                         "No reports archived yet.")

    def test_markdown_is_stable(self):
        # 1.10.0 sorts before 1.9.0 as text but is the newer version.
        runs = [(name.replace("1.4.3", version), values)
                for version in ("1.9.0", "1.10.0")
                for name, values in (
                    *((FIRE_RED.format(model, run), report(600 + run))
                      for model in (1000, 2000) for run in (1, 2, 3)),
                    *((RUBY.format(model, run), ruby())
                      for model in (2000, 1000) for run in (1, 2, 3)))]
        for name, values in runs:
            values["version"] = name.split("/")[2]
        forward = archive(self.directory / "forward", runs)
        backward = archive(self.directory / "backward", runs[::-1])

        groups = MODULE.collect(forward)
        markdown = MODULE.render(groups)

        self.assertEqual(MODULE.render(MODULE.collect(backward)), markdown)
        self.assertEqual(MODULE.render(dict(reversed(groups.items()))),
                         markdown)
        self.assertLess(markdown.index("### 1.10.0"),
                        markdown.index("### 1.9.0"))
        self.assertEqual([key for key in rows(markdown)][:4], [
            ("POKEMON RUBY (AXVE-r1)", "psp-1000"),
            ("POKEMON RUBY (AXVE-r1)", "psp-2000"),
            ("POKEMON FIRE (BPRE-r0)", "psp-1000"),
            ("POKEMON FIRE (BPRE-r0)", "psp-2000"),
        ])

        document = self.directory / "PERFORMANCE.md"
        document.write_bytes(b"# Performance\r\n\r\nIntro.\r\n\r\n"
                             b"<!-- perf-table:begin -->\r\nold table\r\n"
                             b"<!-- perf-table:end -->\r\n\r\nAfter.\r\n")
        arguments = ["--root", str(forward), "--update", str(document)]
        self.assertEqual(MODULE.main(arguments), 0)
        first = document.read_bytes()
        self.assertEqual(MODULE.main(arguments), 0)
        self.assertEqual(document.read_bytes(), first)
        self.assertEqual(first.decode("utf-8"),
                         "# Performance\n\nIntro.\n\n"
                         "<!-- perf-table:begin -->\n" + markdown +
                         "\n<!-- perf-table:end -->\n\nAfter.\n")

        document.write_text("no markers\n", encoding="utf-8")
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            self.assertEqual(MODULE.main(arguments), 2)
        self.assertIn("ERROR:", errors.getvalue())
        self.assertEqual(document.read_text(encoding="utf-8"),
                         "no markers\n")


if __name__ == "__main__":
    unittest.main()
