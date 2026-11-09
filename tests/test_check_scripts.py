"""Tests for scripts/check-scripts.sh and scripts/check-module-tests.sh on
scratch trees, and for the Makefile parser, scripts/makefile_tests.py."""

import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

try:
    import yaml
except ImportError:
    yaml = None

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import makefile_tests  # noqa: E402


SCRIPT = (ROOT / "scripts" / "check-scripts.sh").as_posix()
MODULE_SCRIPT = (ROOT / "scripts" / "check-module-tests.sh").as_posix()
PYTHON_ENV = {**os.environ, "PYTHON": pathlib.Path(sys.executable).as_posix()}

CLEAN = {
    "scripts/good.sh": "#!/bin/sh\nif true; then\n  echo ok\nfi\n",
    "scripts/good.py": "print('ok')\n",
    "tests/rom_link/good.py": "VALUE = 1\n",
    ".github/workflows/ci.yml":
     "on: push\njobs:\n  check:\n    runs-on: ubuntu-latest\n",
}

BROKEN = {
    "scripts/bad.sh": "#!/bin/sh\nif true\nfi\n",
    "scripts/bad.py": "def broken(:\n",
    "tests/rom_link/bad.py": "def broken(:\n",
    ".github/workflows/bad.yml": "jobs: [unclosed\n",
}


def check(files):
    """check-scripts.sh's result on a Git tree holding these files, and the
    __pycache__ folders the run left in it."""
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(text.encode())
        subprocess.run(["git", "init", "-q", root.as_posix()], check=True)
        subprocess.run(["git", "-c", "core.autocrlf=false", "-C", root.as_posix(),
                        "add", "-A"], check=True)
        result = subprocess.run(
         ["sh", SCRIPT, root.as_posix()], capture_output=True, text=True,
         env=PYTHON_ENV)
        return result, list(root.rglob("__pycache__"))


# A variable's name need not match its binary's, as in the real Makefile.
SYNTHETIC_MAKEFILE = """\
OUT ?= build/host
ALPHA ?= test_alpha
ALPHA ?= test_ignored
SAN = build/sanitize
HOST_TEST_ALPHA = $(OUT)/$(ALPHA)
HOST_TEST_BETA = ${OUT}/test_beta_state
ALL = $(HOST_TEST_ALPHA) \\
 $(HOST_TEST_BETA)
HOST_TESTS = $(ALL)
HOST_SANITIZE_TESTS = $(SAN)/test_alpha \\
\t$(SAN)/test_beta_state
HOST_SANITIZE_TESTS += $(SAN)/test_gamma

test: $(HOST_TESTS)
\t$(HOST_TEST_ALPHA)
\t@$(HOST_TEST_BETA) --quick \\
\t --verbose

# Blank and comment lines do not end a recipe.
\t$(OUT)/test_gamma

test-sanitize: $(HOST_SANITIZE_TESTS)
\t$(SAN)/test_alpha
\t$(SAN)/test_beta_state
$(HOST_TEST_ALPHA): alpha.c
\tcc alpha.c -o $@
"""


class MakefileTestsTest(unittest.TestCase):
    def test_lists_and_recipes_are_expanded_to_test_names(self):
        self.assertEqual(makefile_tests.entries(SYNTHETIC_MAKEFILE), {
            "HOST_TESTS": ["test_alpha", "test_beta_state"],
            "HOST_SANITIZE_TESTS":
             ["test_alpha", "test_beta_state", "test_gamma"],
            "test": ["test_alpha", "test_beta_state", "test_gamma"],
            "test-sanitize": ["test_alpha", "test_beta_state"],
        })

    def test_a_missing_place_or_a_loop_is_an_error(self):
        for text in (SYNTHETIC_MAKEFILE.replace("HOST_TESTS =", "OTHER ="),
                     SYNTHETIC_MAKEFILE.replace("test-sanitize:", "other:"),
                     SYNTHETIC_MAKEFILE.replace("ALL = $(HOST_TEST_ALPHA)",
                                                "ALL = $(ALL)")):
            with self.subTest(text=text):
                with self.assertRaises(ValueError):
                    makefile_tests.entries(text)


MODULE_PLACES = {
    "HOST_TESTS": "HOST_TESTS = build/host/test_good\n",
    "HOST_SANITIZE_TESTS": "HOST_SANITIZE_TESTS = build/sanitize/test_good\n",
    "test": "\tbuild/host/test_good\n",
    "test-sanitize": "\tbuild/sanitize/test_good\n",
}

MODULE_TREE = {
    "Makefile": (MODULE_PLACES["HOST_TESTS"]
                 + MODULE_PLACES["HOST_SANITIZE_TESTS"]
                 + "test: $(HOST_TESTS)\n" + MODULE_PLACES["test"]
                 + "test-sanitize: $(HOST_SANITIZE_TESTS)\n"
                 + MODULE_PLACES["test-sanitize"]),
    "source/src/good.c": '#include <string.h>\n#include "good.h"\n',
    "tests/test_good.c": "int main(void) { return 0; }\n",
    "source/src/glue.c": '#include "common.h"\n',
    "source/src/sdk.c": "#include <pspkernel.h>\n",
    "source/src/gbkfont.c": "const char gbkfont[] = { 0 };\n",
}


def check_modules(files):
    """check-module-tests.sh's result on a tree holding these files and the
    Makefile parser."""
    with tempfile.TemporaryDirectory() as directory:
        root = pathlib.Path(directory)
        for name, text in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(text.encode())
        (root / "scripts").mkdir()
        shutil.copy(ROOT / "scripts" / "makefile_tests.py", root / "scripts")
        return subprocess.run(
         ["sh", MODULE_SCRIPT, root.as_posix()], capture_output=True,
         text=True, env=PYTHON_ENV)


def missing(stderr, module):
    """What the failure line of this module says is missing."""
    for line in stderr.splitlines():
        if line.startswith(f"Pure module {module} "):
            return line.split("missing:")[1].split()
    return None


class CheckModuleTestsTest(unittest.TestCase):
    def test_tested_glue_and_vendored_modules_pass(self):
        result = check_modules(MODULE_TREE)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Module test checks passed", result.stdout)

    def test_pure_module_without_test_fails_naming_it(self):
        result = check_modules(
         {**MODULE_TREE, "source/src/foo.c": "#include <stdint.h>\n"})
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(missing(result.stderr, "foo"),
                         ["tests/test_foo.c", *MODULE_PLACES])
        self.assertIsNone(missing(result.stderr, "good"))

    def test_each_place_the_test_misses_is_named(self):
        for place, line in MODULE_PLACES.items():
            with self.subTest(place=place):
                makefile = MODULE_TREE["Makefile"].replace(
                 line, line.replace("test_good", "test_other"))
                result = check_modules({**MODULE_TREE, "Makefile": makefile})
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertEqual(missing(result.stderr, "good"), [place])


class CheckScriptsTest(unittest.TestCase):
    def test_clean_tree_passes_and_leaves_no_bytecode(self):
        result, caches = check(CLEAN)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Script syntax checks passed", result.stdout)
        self.assertEqual(caches, [])

    def test_each_broken_file_fails(self):
        for name, text in BROKEN.items():
            with self.subTest(name=name):
                if name.endswith(".yml") and yaml is None:
                    self.skipTest("PyYAML missing")
                result, _ = check({**CLEAN, name: text})
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(name, result.stderr)


if __name__ == "__main__":
    unittest.main()
