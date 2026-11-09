"""Tests for scripts/hardware-session.py and the repository's test records."""

import importlib.util
import pathlib
import subprocess
import sys
import unittest


# The script name has a dash, so load it as a module by path.
SCRIPT = pathlib.Path(__file__).parents[1] / "scripts" / "hardware-session.py"
SPEC = importlib.util.spec_from_file_location("hardware_session", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


CATALOG = """\
# Hardware tests

- Models: prose before the first test is not a field.

## Saves

### save-a

- Models: PSP-1000, second model
- Minutes: 15
- Pass: the save
  loads.
- Steps: save, exit and reload.

### link-b

- Models: second model
- Minutes: 40
- Pass: the trade completes.
- Steps: trade once.
"""

HEADER = ("| Date | Version | EBOOT sha256 | Model | Firmware/CFW | Storage "
          "| Test id | Result | Minutes | Evidence |\n"
          "|---|---|---|---|---|---|---|---|---|---|\n")


def row(test_id="save-a", digest="0123456789ab", model="PSP-1000",
        result="pass", minutes="12"):
    return (f"| 2026-11-05 | 1.4.4-rc.1 | {digest} | {model} | 6.61 PRO-C "
            f"| MS Pro Duo 8 GB | {test_id} | {result} | {minutes} "
            f"| GBA-Rush-1.log |\n")


class CatalogTest(unittest.TestCase):
    def test_reads_every_field_of_every_test(self):
        catalog = MODULE.parse_catalog(CATALOG)
        self.assertEqual(sorted(catalog), ["link-b", "save-a"])
        test = catalog["save-a"]
        self.assertEqual(test["Models"], ["PSP-1000", "second model"])
        self.assertEqual(test["Minutes"], 15)
        self.assertEqual(test["Pass"], "the save loads.")
        self.assertEqual(test["Steps"], "save, exit and reload.")

    def test_a_missing_field_names_the_test_and_its_line(self):
        text = CATALOG.replace("- Steps: trade once.\n", "")
        with self.assertRaisesRegex(ValueError,
                                    r"line 15: test 'link-b' has no Steps"):
            MODULE.parse_catalog(text)

    def test_a_duplicate_id_is_rejected(self):
        with self.assertRaisesRegex(ValueError,
                                    "line 22: duplicate test id 'save-a'"):
            MODULE.parse_catalog(CATALOG + "\n### save-a\n")

    def test_an_unknown_model_class_is_rejected(self):
        text = CATALOG.replace("- Models: second model", "- Models: PSP-200")
        with self.assertRaisesRegex(ValueError, "unknown model class"):
            MODULE.parse_catalog(text)


class ResultsTest(unittest.TestCase):
    def test_reads_each_row_with_its_line_number(self):
        rows = MODULE.parse_results(
         "# Results\n\n" + HEADER + row()
         + row("link-b", "unknown", "PSP-2000 + PSP-1000", "partial",
               "unknown"))
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]["Test id"], "save-a")
        self.assertEqual(rows[0]["EBOOT sha256"], "0123456789ab")
        self.assertEqual(rows[0]["line"], 5)
        self.assertEqual(rows[1]["Model"], "PSP-2000 + PSP-1000")
        self.assertEqual(rows[1]["Result"], "partial")

    def assert_malformed(self, bad_row, reason):
        text = HEADER + row() + bad_row
        with self.assertRaisesRegex(ValueError, f"^line 4: .*{reason}"):
            MODULE.parse_results(text)

    def test_a_short_row_is_reported_with_its_line_number(self):
        self.assert_malformed("| 2026-11-05 | 1.4.4 | unknown |\n",
                              "3 columns, expected 10")

    def test_an_elf_length_or_upper_case_digest_is_rejected(self):
        self.assert_malformed(row(digest="0123456789abcdef"), "12 hex")
        self.assert_malformed(row(digest="0123456789AB"), "12 hex")

    def test_an_unknown_model_or_result_is_rejected(self):
        self.assert_malformed(row(model="PSP 1000"), "unknown model")
        self.assert_malformed(row(result="ok"), "result 'ok'")
        self.assert_malformed(row(minutes="ten"), "minutes 'ten'")

    def test_a_row_before_the_header_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "line 1: .*before the header"):
            MODULE.parse_results(row() + HEADER)


FULL = "0123456789ab" + "cd" * 26
BUILDS = ("| EBOOT.PBP | Full SHA-256 | Built from | Notes |\n"
          "|---|---|---|---|\n")


def build(short="`0123456789ab`", full=f"`{FULL}`", source="main 1234567"):
    return f"| {short} | {full} | {source} | Test build. |\n"


class BuildsTest(unittest.TestCase):
    def test_a_builds_table_before_the_results_is_not_a_result(self):
        rows = MODULE.parse_results(BUILDS + build() + "\n" + HEADER + row())
        self.assertEqual([r["line"] for r in rows], [7])

    def assert_malformed(self, bad_row, reason):
        with self.assertRaisesRegex(ValueError, f"^line 4: .*{reason}"):
            MODULE.parse_results(BUILDS + build() + bad_row)

    def test_a_short_digest_must_start_the_full_one(self):
        self.assert_malformed(build(short="`0123456789ac`"), "not the start")

    def test_the_full_digest_has_64_lower_case_hex_digits(self):
        self.assert_malformed(build(full=f"`{FULL[:-1]}`"), "64 lower-case")
        self.assert_malformed(build(full=f"`{FULL.upper()}`"), "64 lower-case")

    def test_a_build_row_needs_its_source_and_four_columns(self):
        self.assert_malformed(build(source=""), "empty Built from")
        self.assert_malformed("| `0123456789ab` | x |\n", "2 columns, expected 4")


class ValidateTest(unittest.TestCase):
    def test_rows_with_catalog_ids_pass(self):
        catalog = MODULE.parse_catalog(CATALOG)
        MODULE.validate(MODULE.parse_results(HEADER + row()), catalog)

    def test_an_unknown_test_id_is_rejected_with_its_line(self):
        catalog = MODULE.parse_catalog(CATALOG)
        rows = MODULE.parse_results(HEADER + row() + row("save-z"))
        with self.assertRaisesRegex(ValueError,
                                    "line 4: unknown test id 'save-z'"):
            MODULE.validate(rows, catalog)


class SessionSheetTest(unittest.TestCase):
    def test_lists_steps_models_and_total_minutes(self):
        catalog = MODULE.parse_catalog(CATALOG)
        sheet = MODULE.session_sheet(["save-a", "link-b"], catalog)
        self.assertIn("2 tests, about 55 minutes", sheet)
        self.assertIn("save-a: PSP-1000, second model, 15 min", sheet)
        self.assertIn("Steps: trade once.", sheet)
        self.assertIn("Pass: the save loads.", sheet)

    def test_a_path_is_not_split_at_its_hyphens(self):
        text = CATALOG.replace(
            "- Steps: trade once.",
            "- Steps: trade once and keep the save "
            "build/test-roms/save-eeprom8k_wfirst.gba.")
        sheet = MODULE.session_sheet(["link-b"], MODULE.parse_catalog(text))
        self.assertIn("build/test-roms/save-eeprom8k_wfirst.gba.", sheet)

    def test_an_unknown_test_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "unknown test id 'nope'"):
            MODULE.session_sheet(["nope"], MODULE.parse_catalog(CATALOG))

    def test_reads_the_tests_of_a_release_file(self):
        text = "hotfix-data: no\ntests: save-a, link-b\ntests: core-rom\n"
        self.assertEqual(MODULE.release_tests(text),
                         ["save-a", "link-b", "core-rom"])


class RepositoryTest(unittest.TestCase):
    """The real catalog and results table, so make check fails on them."""

    def test_every_result_row_uses_a_catalog_id(self):
        catalog, rows = MODULE.check()
        for test_id in ("save-sram-roundtrip", "save-powerloss-10x",
                        "link-frlg-both-roles", "link-ruby-fr",
                        "sleep-auto-20x", "boot-open-bios-10",
                        "perf-baseline-5", "link-mksc-race", "link-mixed-pair",
                        "core-rom", "suite-terzi"):
            self.assertIn(test_id, catalog)
        self.assertTrue(rows)

    def test_prints_a_session_sheet(self):
        result = subprocess.run(
         [sys.executable, str(SCRIPT), "--tests",
          "save-sram-roundtrip,link-mksc-race"],
         capture_output=True, text=True, check=True)
        self.assertIn("Hardware session: 2 tests", result.stdout)
        self.assertIn("link-mksc-race: PSP-1000, second model", result.stdout)

    def test_an_unknown_test_fails_the_command(self):
        result = subprocess.run(
         [sys.executable, str(SCRIPT), "--tests", "no-such-test"],
         capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unknown test id 'no-such-test'", result.stderr)


if __name__ == "__main__":
    unittest.main()
