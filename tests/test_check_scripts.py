"""Tests for scripts/check-scripts.sh on scratch Git trees."""

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

try:
    import yaml
except ImportError:
    yaml = None


SCRIPT = (pathlib.Path(__file__).resolve().parents[1]
          / "scripts" / "check-scripts.sh").as_posix()

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
         env={**os.environ, "PYTHON": pathlib.Path(sys.executable).as_posix()})
        return result, list(root.rglob("__pycache__"))


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
