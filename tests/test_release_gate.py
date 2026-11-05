"""Tests for scripts/release_gate.py on scratch Git repositories with dated
tags and synthetic test records."""

import contextlib
import hashlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import release_gate  # noqa: E402


HOUR = 3600
DAY = 24 * HOUR
START = 1_790_000_000
RC = START + DAY
DIGEST = "0123456789ab" + "cd" * 26
OTHER_DIGEST = "ba9876543210" + "dc" * 26

CATALOG = """\
# Hardware tests

### save-a

- Models: PSP-1000, second model
- Minutes: 15
- Pass: the save loads.
- Steps: save and reload.

### link-a

- Models: PSP-1000, second model
- Minutes: 40
- Pass: the trade completes.
- Steps: trade.

### perf-baseline-5

- Models: PSP-1000
- Minutes: 120
- Pass: compare-benchmarks passes.
- Steps: measure.

### other-a

- Models: PSP-1000
- Minutes: 5
- Pass: it works.
- Steps: try it.
"""

RESULTS_HEADER = (
 "| Date | Version | EBOOT sha256 | Model | Firmware/CFW | Storage | Test id"
 " | Result | Minutes | Evidence |\n|---|---|---|---|---|---|---|---|---|---|\n")


def row(test_id, model, digest=DIGEST, result="pass", evidence="log.txt"):
    return (f"| 2026-11-10 | 1.4.4 | {digest[:12]} | {model} | 6.61 PRO-C | "
            f"Memory Stick | {test_id} | {result} | 10 | {evidence} |\n")


class Repo:
    """A scratch repository with the docs and the link headers the gate
    reads, and a package folder next to it."""

    def __init__(self, directory):
        self.root = pathlib.Path(directory) / "repo"
        self.dist = pathlib.Path(directory) / "dist"
        self.root.mkdir()
        self.git("init", "-q")
        self.write("docs/hardware-tests.md", CATALOG)
        self.write("docs/releases/link-compat.txt", (
         ROOT / "docs" / "releases" / "link-compat.txt").read_text())
        self.results()
        self.link(1, 2)
        for name in ("main.c", "link_cable.c", "link_net.c", "psp_adhoc.c"):
            self.write(f"source/src/{name}", "/* first */\n")

    def git(self, *args, when=START):
        stamp = f"{when} +0000"
        return subprocess.run(
         ["git", "-c", "user.name=Test", "-c", "user.email=test@example.com",
          "-c", "commit.gpgsign=false", "-c", "core.autocrlf=false",
          "-C", self.root.as_posix(), *args],
         check=True, capture_output=True,
         env={**os.environ, "GIT_AUTHOR_DATE": stamp,
              "GIT_COMMITTER_DATE": stamp})

    def write(self, path, text):
        (self.root / path).parent.mkdir(parents=True, exist_ok=True)
        (self.root / path).write_bytes(text.encode())

    def touch(self, *paths):
        for path in paths:
            with open(self.root / path, "a", encoding="utf-8") as stream:
                stream.write("/* changed */\n")

    def commit(self, when, *tags):
        """Commit everything with this date, tagged with annotated tags of
        the same date."""
        self.git("add", "-A")
        self.git("commit", "-q", "--allow-empty", "-m", "change", when=when)
        for tag in tags:
            self.git("tag", "-a", "-m", tag, tag, when=when)

    def link(self, version, protocol):
        self.write("source/src/link_net.h",
                   f"#define LINK_NET_VERSION          {version}u\n")
        self.write("source/src/link_cable.h",
                   f"/* Sent in HELLO. */\n#define LINK_CABLE_PROTOCOL {protocol}u\n")

    def release(self, version="1.4.4", tests="save-a", extra=""):
        self.write(f"docs/releases/{version}.txt", f"tests: {tests}\n{extra}")

    def results(self, *rows):
        self.write("docs/HARDWARE-RESULTS.md",
                   "# Results\n\n" + RESULTS_HEADER + "".join(rows))

    def manifest(self, version="1.4.4", digest=DIGEST):
        self.dist.mkdir(exist_ok=True)
        (self.dist / f"GBA-Rush-PSP-{version}.manifest.json").write_text(
         json.dumps({"format": release_gate.release_manifest.MANIFEST_FORMAT,
                     "files": {"EBOOT.PBP": {"sha256": digest}}}))

    def gate(self, version="1.4.4", channel="stable", now=RC + 8 * DAY):
        return release_gate.gate(version, channel, self.root, self.dist, now)

    def notes(self, version="1.4.4"):
        return (self.dist / f"RELEASE-NOTES-{version}.md").read_text()


class ScratchRepoTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.repo = Repo(directory.name)


class StableGateTest(ScratchRepoTest):
    """1.4.3 is tagged; each test makes the 1.4.4 candidate."""

    def setUp(self):
        super().setUp()
        self.repo.commit(START, "1.4.3")
        self.repo.release()
        self.repo.manifest()
        self.repo.results(row("save-a", "PSP-1000"), row("save-a", "PSP-3000"))

    def rc(self, *paths, when=RC, tag="1.4.4-rc.1"):
        self.repo.touch(*(paths or ("source/src/main.c",)))
        self.repo.commit(when, tag)

    def test_a_validated_candidate_passes_and_gets_its_notes(self):
        self.rc()
        self.repo.release(tests="save-a, link-a, perf-baseline-5")
        self.repo.results(
         row("save-a", "PSP-1000"), row("save-a", "PSP-3000"),
         row("link-a", "PSP-2000 + PSP-1000"),
         row("perf-baseline-5", "PSP-1000",
             evidence="PASS, throughput=59.70 fps (+1.20%)"))
        self.assertEqual(self.repo.gate(), [])
        notes = self.repo.notes()
        self.assertIn(f"EBOOT.PBP sha256: {DIGEST}.", notes)
        self.assertIn("- save-a: PSP-1000 (6.61 PRO-C), PSP-3000 (6.61 PRO-C)",
                      notes)
        self.assertIn("- link-a: PSP-2000 + PSP-1000 (6.61 PRO-C)", notes)
        self.assertIn("- perf-baseline-5: PSP-1000 (6.61 PRO-C); "
                      "compare-benchmarks: PASS, throughput=59.70 fps "
                      "(+1.20%)", notes)
        self.assertIn("Not required for this release: other-a.", notes)

    def test_a_missing_row_names_the_test_and_the_model_class(self):
        self.rc()
        # A pass of another test on the second model does not count.
        self.repo.results(row("save-a", "PSP-1000"), row("other-a", "PSP-3000"))
        self.assertEqual(self.repo.gate(), [
         "save-a: no pass row for EBOOT 0123456789ab on second model"])
        self.assertFalse((self.repo.dist / "RELEASE-NOTES-1.4.4.md").exists())

    def test_rows_of_another_eboot_or_without_a_pass_do_not_count(self):
        self.rc()
        self.repo.results(row("save-a", "PSP-1000", digest=OTHER_DIGEST),
                          row("save-a", "PSP-3000", result="partial"))
        self.assertEqual(self.repo.gate(), [
         "save-a: no pass row for EBOOT 0123456789ab on PSP-1000, "
         "second model"])

    def test_the_command_fails_and_names_the_missing_tests(self):
        self.rc()
        self.repo.release(tests="save-a, link-a")
        self.repo.results()
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            status = release_gate.main(
             ["--version", "1.4.4", "--channel", "stable",
              "--repo", str(self.repo.root), "--dist", str(self.repo.dist),
              "--now", str(RC + 8 * DAY)])
        self.assertEqual(status, 1)
        self.assertIn("save-a: no pass row", stderr.getvalue())
        self.assertIn("link-a: no pass row", stderr.getvalue())

    def test_the_soak_counts_from_the_newest_rc_tag(self):
        self.rc()
        self.rc(when=RC + DAY, tag="1.4.4-rc.2")
        self.assertEqual(self.repo.gate(now=RC + 7 * DAY + 12 * HOUR), [
         "soak: 156 h since 1.4.4-rc.2, 168 h needed"])
        self.assertEqual(self.repo.gate(now=RC + 8 * DAY), [])

    def test_the_soak_counts_from_an_annotated_tag_not_its_commit(self):
        # The rc commit is a week older than its tag.
        self.repo.touch("source/src/main.c")
        self.repo.commit(RC - 7 * DAY)
        self.repo.git("tag", "1.4.4-rc.1")
        self.assertEqual(self.repo.gate(now=RC + HOUR), [
         "1.4.4-rc.1: a lightweight tag has only its commit's date; "
         "recreate it with git tag -a, whose date starts the soak"])
        self.repo.git("tag", "-f", "-a", "-m", "rc", "1.4.4-rc.1", when=RC)
        self.assertEqual(self.repo.gate(now=RC + HOUR), [
         "soak: 1 h since 1.4.4-rc.1, 168 h needed"])

    def test_a_data_hotfix_soaks_for_72_hours(self):
        self.rc()
        self.repo.release(extra="hotfix-data: yes\n")
        self.assertEqual(self.repo.gate(now=RC + 72 * HOUR), [])
        self.assertEqual(self.repo.gate(now=RC + 72 * HOUR - 1), [
         "soak: 71 h since 1.4.4-rc.1, 72 h needed for a data hotfix"])

    def test_a_source_change_after_the_rc_tag_fails(self):
        self.rc()
        self.repo.touch("docs/HARDWARE-RESULTS.md")
        self.repo.commit(RC + HOUR)
        self.assertEqual(self.repo.gate(), [])
        self.repo.touch("source/src/main.c")
        self.repo.commit(RC + 2 * HOUR)
        self.assertEqual(self.repo.gate(), [
         "source/ changed since 1.4.4-rc.1"])

    def test_a_missing_rc_tag_fails(self):
        self.repo.touch("source/src/main.c")
        self.repo.commit(RC, "1.4.4-rc.x")
        problems = self.repo.gate()
        self.assertEqual(len(problems), 1)
        self.assertTrue(problems[0].startswith("no 1.4.4-rc.N tag"), problems)

    def test_link_code_changed_without_a_declaration_fails(self):
        self.rc("source/src/link_cable.c")
        problems = self.repo.gate()
        self.assertEqual(len(problems), 1)
        self.assertIn("link code changed since 1.4.3 "
                      "(source/src/link_cable.c)", problems[0])

    def test_wire_unchanged_is_accepted(self):
        self.rc("source/src/link_cable.c")
        self.repo.release(extra="link: wire-unchanged\n")
        self.assertEqual(self.repo.gate(), [])


class RebuiltReleasesTest(ScratchRepoTest):
    """Two real link changes, rebuilt from what changed between the tags."""

    def candidate(self, version, extra, *rows):
        self.repo.release(version, extra=extra)
        self.repo.results(row("save-a", "PSP-1000"), row("save-a", "PSP-3000"),
                          *rows)
        self.repo.manifest(version)
        return self.repo.gate(version)

    def test_1_4_3_extended_the_wire_without_a_version_change(self):
        self.repo.commit(START, "1.4.2")
        self.repo.touch("source/src/link_cable.c", "source/src/link_net.c")
        self.repo.commit(RC, "1.4.3-rc.1")
        self.assertIn("link code changed since 1.4.2 (source/src/link_cable.c, "
                      "source/src/link_net.c)", self.candidate("1.4.3", "")[0])
        self.assertIn("needs a mixed-pair: line",
                      self.candidate("1.4.3", "link: wire-extended\n")[0])
        loses = ("A 1.4.3 host with a 1.4.2 joiner may give Mario Kart: Super "
                 "Circuit a wrong word when a race starts.")
        self.assertEqual(self.candidate(
         "1.4.3", f"link: wire-extended\nmixed-pair: {loses}\n"), [])
        self.assertIn(f"- With an earlier release: {loses}",
                      self.repo.notes("1.4.3"))

    def test_1_4_1_is_caught_by_the_cable_protocol(self):
        self.repo.link(1, 1)
        self.repo.commit(START, "1.4.0")
        self.repo.link(1, 2)
        self.repo.touch("source/src/link_cable.c", "source/src/psp_adhoc.c")
        self.repo.commit(RC, "1.4.1-rc.1")
        self.assertEqual(self.candidate("1.4.1", "link: bump\n"), [
         "LINK_CABLE_PROTOCOL changed since 1.4.0: needs a pass row of "
         "ppsspp-link-pair or of a link-* test for this EBOOT"])
        self.assertEqual(self.candidate(
         "1.4.1", "link: bump\n", row("link-a", "PSP-2000 + PSP-1000")), [])
        self.assertEqual(self.candidate(
         "1.4.1", "link: wire-unchanged\n",
         row("link-a", "PSP-2000 + PSP-1000")), [
         "LINK_CABLE_PROTOCOL changed since 1.4.0: docs/releases/1.4.1.txt "
         "must declare link: bump"])


class PrereleaseAndLinksTest(ScratchRepoTest):
    def history(self, constants_1_4_0):
        """1.3.0 without a link cable, then 1.4.0 to 1.4.3 and a candidate."""
        for header in ("link_net.h", "link_cable.h"):
            (self.repo.root / "source" / "src" / header).unlink()
        self.repo.commit(START, "1.3.0")
        self.repo.link(*constants_1_4_0)
        self.repo.commit(START + HOUR, "1.4.0")
        self.repo.link(1, 2)
        for hour, tag in ((2, "1.4.1"), (3, "1.4.2"), (4, "1.4.3"), (5, None)):
            self.repo.touch("source/src/main.c")
            self.repo.commit(START + hour * HOUR, *([tag] if tag else []))

    def test_a_prerelease_passes_with_every_test_not_validated(self):
        self.history((1, 1))
        self.repo.release(tests="save-a, link-a")
        self.assertEqual(self.repo.gate(channel="prerelease"), [])
        notes = self.repo.notes()
        self.assertIn("## Validated on\n\nNothing yet", notes)
        self.assertIn("The stable release needs:\n\n"
                      "- save-a on PSP-1000, second model\n"
                      "- link-a on PSP-1000, second model\n", notes)
        self.assertIn("Not required for this release: perf-baseline-5, "
                      "other-a.", notes)
        self.assertNotIn("EBOOT.PBP sha256", notes)

    def test_a_prerelease_lists_what_its_eboot_already_passed(self):
        self.history((1, 1))
        self.repo.release(tests="save-a, link-a")
        self.repo.manifest()
        self.repo.results(row("save-a", "PSP-1000"), row("save-a", "PSP-3000"),
                          row("link-a", "PSP-1000"))
        self.assertEqual(self.repo.gate(channel="prerelease"), [])
        self.assertIn(
         "## Validated on\n\n"
         "- save-a: PSP-1000 (6.61 PRO-C), PSP-3000 (6.61 PRO-C)\n"
         "- link-a: PSP-1000 (6.61 PRO-C)\n\n"
         "## Not validated\n\n"
         "The stable release needs:\n\n"
         "- link-a on second model\n", self.repo.notes())

    def test_a_forbidden_word_since_the_previous_release_fails(self):
        self.history((1, 1))
        self.repo.release()
        self.repo.git("commit", "-q", "--allow-empty", "-m",
                      "Port the zorblequint idea")
        listed = self.repo.root.parent / "words.sha256"
        listed.write_text(hashlib.sha256(b"zorblequint").hexdigest())
        with mock.patch.object(release_gate.check_names, "LIST", listed):
            problems = self.repo.gate(channel="prerelease")
        self.assertEqual(len(problems), 1, problems)
        self.assertRegex(problems[0], r"^forbidden word in commit [0-9a-f]+: "
                         r"message line 1$")

    def test_a_new_cable_protocol_links_with_no_earlier_release(self):
        self.history((1, 1))
        self.repo.link(1, 3)
        self.repo.commit(START + 6 * HOUR)
        self.repo.release()
        self.assertEqual(self.repo.gate(channel="prerelease"), [])
        links = self.repo.notes().partition("## Links with\n\n")[2]
        self.assertEqual(links.splitlines(), [
         "- Links with no earlier release (link version 1, cable protocol 3).",
         "- Does not link with 1.4.0, 1.4.1, 1.4.2, 1.4.3."])

    def test_the_notes_never_say_1_4_0_links_with_later_releases(self):
        # The real 1.4.0, and one whose constants match that only
        # link-compat.txt excludes.
        for constants in ((1, 1), (1, 2)):
            with self.subTest(constants=constants):
                self.setUp()
                self.history(constants)
                self.repo.release()
                self.assertEqual(self.repo.gate(channel="prerelease"), [])
                links = self.repo.notes().partition("## Links with\n\n")[2]
                self.assertEqual(links.splitlines(), [
                 "- Links with 1.4.1, 1.4.2, 1.4.3 (link version 1, "
                 "cable protocol 2).",
                 "- Does not link with 1.4.0.",
                 "- 1.4.0 with 1.4.1+: no link; 1.4.0 has link version 1 "
                 "like them, but an older cable protocol",
                 "- 1.4.3 with 1.4.1-1.4.2: links; the 1.4.3 trade fix "
                 "applies only if both PSPs run 1.4.3 or later"])


class ReleaseFilesTest(unittest.TestCase):
    def test_the_repository_release_files_parse(self):
        text = (ROOT / "docs" / "releases" / "1.4.4.txt").read_text()
        release = release_gate.parse_release(text)
        self.assertEqual(release["hotfix-data"], ["yes"])
        catalog = release_gate.hardware_session.parse_catalog(
         (ROOT / "docs" / "hardware-tests.md").read_text(encoding="utf-8"))
        tests = release_gate.hardware_session.release_tests(text)
        self.assertTrue(tests)
        for test_id in tests:
            self.assertIn(test_id, catalog)
        release_gate.parse_compat(
         (ROOT / "docs" / "releases" / "link-compat.txt").read_text())

    def test_an_unknown_key_or_value_is_rejected(self):
        for text in ("hotfix_data: yes\n", "hotfix-data: maybe\n",
                     "link: wire-same\n", "link: bump\nlink: bump\n"):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    release_gate.parse_release(text)

    def test_a_malformed_link_fact_is_rejected_with_its_line(self):
        for line in ("1.4.0 and 1.4.1: no link", "1.4 with 1.4.1: links",
                     "1.4.0 with 1.4.1-: links"):
            with self.subTest(line=line):
                with self.assertRaisesRegex(ValueError, "line 2"):
                    release_gate.parse_compat(f"# facts\n{line}\n")


if __name__ == "__main__":
    unittest.main()
