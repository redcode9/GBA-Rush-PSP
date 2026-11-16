"""make test-sanitize fails on a UBSan report: a program that HOST_CC builds
with the Makefile's HOST_SANITIZE_FLAGS stops at its first undefined
behaviour instead of reporting it and exiting with 0. Skipped where HOST_CC
cannot build with the sanitizers."""

import os
import pathlib
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import makefile_tests  # noqa: E402


# argc is 1, so the sum overflows where the compiler cannot fold it away.
PROGRAM = """#include <limits.h>
int main(int argc, char **argv)
{
  int value = INT_MAX;
  (void)argv;
  value += argc;
  return value == 0;
}
"""


class SanitizeFlagsTest(unittest.TestCase):
    def test_undefined_behaviour_fails_the_run(self):
        variables, _ = makefile_tests.parse(
         (ROOT / "Makefile").read_text(encoding="utf-8"))
        flags = makefile_tests.expand(variables["HOST_SANITIZE_FLAGS"], variables)
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "overflow.c"
            binary = pathlib.Path(directory) / "overflow"
            source.write_text(PROGRAM, encoding="ascii")
            build = subprocess.run(
             shlex.split(os.environ.get("HOST_CC", "cc")) + ["-std=c99"]
             + shlex.split(flags) + [str(source), "-o", str(binary)],
             capture_output=True, text=True)
            if build.returncode != 0:
                self.skipTest("HOST_CC cannot build with the sanitizers: "
                              + build.stderr.strip()[-300:])
            run = subprocess.run([str(binary)], capture_output=True, text=True)
        self.assertIn("runtime error:", run.stderr)
        self.assertNotEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
