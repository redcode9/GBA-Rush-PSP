"""Tests for scripts/prove-regression.sh on a toy repository.

The toy module source/toy.c gets a bug at tag v1 (v0 predates it); HEAD fixes
it and adds its tests and their Makefile rules. A fake make stands in for make:
it builds build/host/NAME and build/sanitize/NAME from tests/NAME.c and
source/toy.c with HOST_CC (default cc) when the Makefile has a rule for that
target. Needs git and sh.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]

# A build/sanitize binary is built with -DSANITIZE and wrapped: exit status 99
# becomes a report on stderr and exit status 0, as UBSan does when it recovers.
FAKE_MAKE = """#!/bin/sh
# make [-C DIR] [VAR=VALUE]... TARGET... for the toy's targets.
build()
{
  mkdir -p "${1%/*}" || return
  case $1 in
    build/host/*) $HOST_CC -o "$1" "tests/${1##*/}.c" source/toy.c ;;
    *) $HOST_CC -DSANITIZE -o "$1-real" "tests/${1##*/}.c" source/toy.c &&
       printf '%s\\n' '#!/bin/sh' '"$0-real" && exit 0' \\
        '[ "$?" -eq 99 ] && echo "source/toy.c:1: runtime error: wrong sum" >&2' \\
        >"$1" && chmod +x "$1" ;;
  esac
}
while [ "$#" -gt 0 ]; do
  case $1 in
    -C) cd "$2" || exit 2; shift ;;
    *=*) ;;
    *) grep -q "^$1:" Makefile && build "$1" || exit 2 ;;
  esac
  shift
done
"""

# The fake make reads only the target names.
HEAD_MAKEFILE = """build/host/test_toy:
build/sanitize/test_toy:
build/host/test_toy_sanitizer:
build/sanitize/test_toy_sanitizer:
"""

TOY_TEST = """int toy_add(int a, int b);
int main(void) { return toy_add(2, 3) == 5 ? 0 : 1; }
"""

# Only its sanitizer build sees the bug, and it reports it with exit status 0.
TOY_SANITIZER_TEST = """int toy_add(int a, int b);
int main(void)
{
#ifdef SANITIZE
  return toy_add(2, 3) == 5 ? 0 : 99;
#else
  return 0;
#endif
}
"""


class ProveRegressionTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.repo = Path(directory.name)
        fake_make = self.repo / "fake-make"
        fake_make.write_text(FAKE_MAKE, newline="\n")
        fake_make.chmod(0o755)
        # GIT_DIR and friends would point git at the checkout running the tests.
        self.env = {key: value for key, value in os.environ.items()
                    if not key.startswith("GIT_")}
        self.env.update(MAKE=fake_make.as_posix(), HOST_CC=os.environ.get("HOST_CC", "cc"))
        self.git("init", "-q")
        for key, value in (("user.name", "Test"), ("user.email", "test@example.com"),
                           ("commit.gpgsign", "false"), ("tag.gpgsign", "false"),
                           ("core.autocrlf", "false")):
            self.git("config", key, value)
        self.commit("v0", {"Makefile": "all:\n"})
        self.commit("v1", {"source/toy.c": "int toy_add(int a, int b) { return a - b; }\n"})
        self.commit(None, {"source/toy.c": "int toy_add(int a, int b) { return a + b; }\n",
                           "Makefile": HEAD_MAKEFILE, "tests/test_toy.c": TOY_TEST,
                           "tests/test_toy_sanitizer.c": TOY_SANITIZER_TEST})
        # sh needs LF, whatever line endings this checkout has.
        script = self.repo / "scripts/prove-regression.sh"
        script.parent.mkdir()
        script.write_bytes((ROOT / "scripts/prove-regression.sh").read_bytes()
                           .replace(b"\r\n", b"\n"))

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.repo, env=self.env, check=True,
                              capture_output=True, text=True).stdout

    def commit(self, tag, files):
        for name, text in files.items():
            path = self.repo / name
            path.parent.mkdir(exist_ok=True)
            path.write_text(text, newline="\n")
        self.git("add", *files)
        self.git("commit", "-q", "-m", tag or "Fix")
        if tag:
            self.git("tag", tag)

    def prove(self, ref, test):
        """The exit status and the outcome word (or stderr); the worktree must be gone."""
        result = subprocess.run(["sh", "scripts/prove-regression.sh", ref, test],
                                cwd=self.repo, env=self.env, capture_output=True,
                                text=True)
        self.assertFalse((self.repo / "build/prove" / ref).exists(), result.stdout)
        self.assertNotIn("build/prove", self.git("worktree", "list", "--porcelain"))
        lines = result.stdout.splitlines()
        return result.returncode, lines[-1].split(":")[0] if lines else result.stderr

    def test_a_test_that_catches_the_bug_fails_as_expected(self):
        self.assertEqual(self.prove("v1", "test_toy"), (0, "FAIL-AS-EXPECTED"))

    def test_a_test_that_passes_on_ref_does_not_catch_the_bug(self):
        self.assertEqual(self.prove("HEAD", "test_toy"), (1, "PASSES-ON-REF"))

    def test_a_test_that_does_not_build_on_ref_is_not_applicable(self):
        self.assertEqual(self.prove("v0", "test_toy"), (2, "NOT-APPLICABLE"))

    def test_a_sanitizer_report_fails_even_when_the_test_exits_zero(self):
        self.assertEqual(self.prove("v1", "test_toy_sanitizer"), (0, "FAIL-AS-EXPECTED"))
        self.assertEqual(self.prove("HEAD", "test_toy_sanitizer"), (1, "PASSES-ON-REF"))

    def test_a_worktree_left_by_a_killed_run_is_replaced(self):
        stale = self.repo / "build/prove/v1"
        stale.mkdir(parents=True)
        (stale / "leftover").write_text("")
        self.assertEqual(self.prove("v1", "test_toy"), (0, "FAIL-AS-EXPECTED"))
        # Still registered, but its folder is gone. git checks this only while
        # build/prove holds something else, such as a run for another REF.
        self.git("worktree", "add", "-q", "--detach", "build/prove/v1", "v1")
        shutil.rmtree(stale)
        (self.repo / "build/prove/v0").mkdir()
        self.assertEqual(self.prove("v1", "test_toy"), (0, "FAIL-AS-EXPECTED"))

    def test_other_worktrees_with_a_missing_folder_stay_registered(self):
        self.git("worktree", "add", "-q", "--detach", "other", "v0")
        shutil.rmtree(self.repo / "other")
        self.assertEqual(self.prove("v1", "test_toy"), (0, "FAIL-AS-EXPECTED"))
        self.assertIn("/other\n", self.git("worktree", "list", "--porcelain"))

    def test_bad_arguments_are_errors(self):
        for ref, test in (("v9", "test_toy"), ("../v1", "test_toy"), ("v1", "test_none"),
                          ("v1", "../test_toy")):
            with self.subTest(ref=ref, test=test):
                self.assertEqual(self.prove(ref, test)[0], 3)


if __name__ == "__main__":
    unittest.main()
