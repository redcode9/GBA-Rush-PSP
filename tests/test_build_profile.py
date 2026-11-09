"""source/src/build_profile.h, compiled with HOST_CC: exactly one profile, and
no diagnostic feature in a release build."""

import os
import pathlib
import re
import shlex
import subprocess
import tempfile
import unittest


SOURCE = pathlib.Path(__file__).resolve().parents[1] / "source" / "src"
PROGRAM = """#include "build_profile.h"
#if defined(GBA_RUSH_PROFILE_DIAGNOSTIC) != defined(ENABLE_DIAGNOSTIC_INPUT_TRACE)
#error "the input trace does not follow the profile"
#endif
#if defined(GBA_RUSH_PROFILE_DIAGNOSTIC) != defined(ENABLE_DIAGNOSTIC_AUTORUN)
#error "the autorun does not follow the profile"
#endif
const char *profile_name = GBA_RUSH_PROFILE_NAME;
"""


class BuildProfileTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = pathlib.Path(directory.name)
        (self.root / "profile.c").write_text(PROGRAM, encoding="ascii")

    def compile(self, *defines):
        """The compiler's exit status and messages for PROGRAM with -D defines."""
        result = subprocess.run(
         shlex.split(os.environ.get("HOST_CC", "cc"))
         + ["-std=c99", "-Wall", "-Wextra", "-Werror", f"-I{SOURCE}"]
         + [f"-D{name}" for name in defines]
         + ["-c", str(self.root / "profile.c"), "-o", str(self.root / "profile.o")],
         capture_output=True, text=True)
        return result.returncode, result.stdout + result.stderr

    def test_each_profile_alone_compiles(self):
        for profile in ("GBA_RUSH_PROFILE_RELEASE", "GBA_RUSH_PROFILE_DIAGNOSTIC"):
            with self.subTest(profile=profile):
                status, messages = self.compile(profile)
                self.assertEqual(status, 0, messages)

    def test_no_profile_or_both_profiles_stop_the_build(self):
        status, messages = self.compile()
        self.assertNotEqual(status, 0)
        self.assertIn("No build profile", messages)
        status, messages = self.compile("GBA_RUSH_PROFILE_RELEASE",
                                        "GBA_RUSH_PROFILE_DIAGNOSTIC")
        self.assertNotEqual(status, 0)
        self.assertIn("Both build profiles", messages)

    def test_release_refuses_every_diagnostic_feature(self):
        header = (SOURCE / "build_profile.h").read_text(encoding="ascii")
        features = set(re.findall(r"defined\((ENABLE_DIAGNOSTIC_\w+)\)", header))
        self.assertIn("ENABLE_DIAGNOSTIC_INPUT_TRACE", features)
        for feature in sorted(features):
            with self.subTest(feature=feature):
                status, messages = self.compile("GBA_RUSH_PROFILE_RELEASE", feature)
                self.assertNotEqual(status, 0)
                self.assertIn(feature, messages)
                self.assertIn("make PROFILE=diagnostic", messages)


if __name__ == "__main__":
    unittest.main()
