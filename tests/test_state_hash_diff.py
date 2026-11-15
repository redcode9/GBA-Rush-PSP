"""Tests for tools/state_hash_diff.py on .gbsh texts built here.

Every file is written at run time into a temporary directory, with LF line
ends as the PSP writes them; no fixture is committed.
"""

import contextlib
import hashlib
import importlib.util
import io
import re
import tempfile
import unittest
from pathlib import Path


# tools/ is not a package, so load the script as a module by path.
ROOT = Path(__file__).parents[1]
MODULE_PATH = ROOT / "tools" / "state_hash_diff.py"
SPEC = importlib.util.spec_from_file_location("state_hash_diff", MODULE_PATH)
diff = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(diff)

REGIONS = ("regs", "iwram", "ewram", "vram", "oam", "palette", "io", "backup",
           "audio")
BASIS = "811c9dc5"

# two_frame_text from tests/test_state_hash.c, the text the C exporter must
# write byte for byte; test_pinned_text_matches_the_c_test keeps them equal.
TWO_FRAME_TEXT = (
    "GBSH v1\n"
    "build=abc_def game=BPR_ rev=1 start=1000\n"
    "regions=regs,iwram,ewram,vram,oam,palette,io,backup,audio\n"
    "0 " + " ".join([BASIS] * 9) + "\n"
    "1 040c5b8c " + " ".join([BASIS] * 7) + " 00ad5cab\n"
    "counters late=3 skipped=0\n"
)


def gbsh(frames=200, changes=(), build="abc_def", game="BPR_", rev=1,
         start=1000, counters="counters late=0"):
    """Full .gbsh text; each (frame, region) in changes alters one hash."""
    lines = [
        "GBSH v1",
        f"build={build} game={game} rev={rev} start={start}",
        "regions=" + ",".join(REGIONS),
    ]
    for frame in range(frames):
        hashes = [
            f"{(frame * 2654435761 + region * 40503) & 0xFFFFFFFF:08x}"
            for region in range(len(REGIONS))
        ]
        for changed, name in changes:
            if changed == frame:
                hashes[REGIONS.index(name)] = "ffffffff"
        lines.append(f"{frame} " + " ".join(hashes))
    if counters:
        lines.append(counters)
    return "\n".join(lines) + "\n"


def golden(full):
    """Compact golden of a full text, computed independently of the tool."""
    lines = full.split("\n")
    body = [line for line in lines[2:] if line and not line.startswith("counters")]
    sha = hashlib.sha256(("\n".join(body) + "\n").encode("ascii")).hexdigest()
    samples = [
        line for line in lines[3:]
        if line[:1].isdigit() and int(line.split(" ")[0]) % 60 == 0
    ]
    return "\n".join(["GBSH-GOLDEN v1", f"sha256={sha}", *lines[1:3],
                      *samples]) + "\n"


class StateHashDiffTests(unittest.TestCase):
    def run_diff(self, a, b):
        """Write a and b as a.gbsh and b.gbsh; return the status and output."""
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for name, text in (("a.gbsh", a), ("b.gbsh", b)):
                path = Path(directory) / name
                path.write_bytes(text.encode("latin-1"))
                paths.append(str(path))
            output = io.StringIO()
            with contextlib.redirect_stdout(output), \
                    contextlib.redirect_stderr(output):
                status = diff.main(paths)
        return status, output.getvalue()

    def test_equal_files(self):
        status, output = self.run_diff(gbsh(), gbsh())
        self.assertEqual(status, 0, output)
        self.assertIn("match: 200 frames", output)

    def test_vram_divergence_at_frame_120(self):
        changes = {(120, "vram"), (121, "vram"), (150, "audio")}
        status, output = self.run_diff(gbsh(), gbsh(changes=changes))
        self.assertEqual(status, 1, output)
        self.assertIn("frame 120 region vram\n", output)

    def test_audio_divergence(self):
        status, output = self.run_diff(gbsh(changes={(5, "audio")}), gbsh())
        self.assertEqual(status, 1, output)
        self.assertIn("frame 5 region audio\n", output)

    def test_every_differing_region_is_named(self):
        changes = {(7, "oam"), (7, "io")}
        status, output = self.run_diff(gbsh(), gbsh(changes=changes))
        self.assertEqual(status, 1, output)
        self.assertIn("frame 7 region oam,io\n", output)

    def test_different_region_lists_are_refused(self):
        renamed = gbsh().replace(",audio\n", ",sound\n", 1)
        for a, b in ((gbsh(), renamed), (golden(gbsh()), renamed)):
            status, output = self.run_diff(a, b)
            self.assertEqual(status, 2, output)
            self.assertIn("different region lists", output)

    def test_different_rom_is_refused(self):
        for other in (gbsh(game="BPE_"), gbsh(rev=2), golden(gbsh(rev=2))):
            with self.subTest(other=other.split("\n")[1]):
                status, output = self.run_diff(gbsh(), other)
                self.assertEqual(status, 2, output)
                self.assertIn("different ROMs", output)

    def test_different_build_and_start_still_compare(self):
        base = gbsh(build="base-1", start=1000)
        status, output = self.run_diff(base, gbsh(build="cand-2", start=37))
        self.assertEqual(status, 0, output)
        self.assertIn("build=base-1", output)
        self.assertIn("build=cand-2", output)
        status, output = self.run_diff(golden(base), gbsh(build="cand-2", start=37))
        self.assertEqual(status, 0, output)
        status, output = self.run_diff(
            base, gbsh(build="cand-2", changes={(3, "oam")})
        )
        self.assertEqual(status, 1, output)
        self.assertIn("build=cand-2", output)
        self.assertIn("frame 3 region oam\n", output)

    def test_truncated_file(self):
        status, output = self.run_diff(gbsh(), gbsh(frames=150))
        self.assertEqual(status, 1, output)
        self.assertRegex(output, r"frame 150 missing in .*b\.gbsh")
        status, output = self.run_diff(gbsh(frames=150), gbsh())
        self.assertEqual(status, 1, output)
        self.assertRegex(output, r"frame 150 missing in .*a\.gbsh")
        text = gbsh()
        status, output = self.run_diff(gbsh(), text[:text.index("\n100 ") + 8])
        self.assertEqual(status, 2, output)
        self.assertIn("no LF", output)

    def test_malformed_files(self):
        lines = gbsh(frames=4).split("\n")
        record = lines[4]

        def edit(index, line):
            return "\n".join(lines[:index] + [line] + lines[index + 1:])

        cases = {
            "short hash": edit(4, record[:-1]),
            "uppercase hash": edit(4, record.upper()),
            "extra field": edit(4, record + " 00000000"),
            "missing field": edit(4, record[:-9]),
            "CRLF": "\r\n".join(lines),
            "magic": edit(0, "GBSH v2"),
            "identity": edit(1, "build=abc game=BPR rev=1 start=0"),
            "regions": edit(2, "regions=regs,,iwram"),
            "counters not last": "\n".join(lines[:4] + [lines[7]] + lines[4:7]
                                           + [""]),
            "frames out of order": "\n".join(lines[:3] + [lines[4], lines[3]]
                                             + lines[5:]),
            "repeated frame": "\n".join(lines[:5] + [lines[4]] + lines[5:]),
            "not ASCII": edit(4, record[:-1] + "\xe9"),
            "golden off the grid": golden(gbsh()).replace(
                "\n60 ", "\n61 "),
            "golden sha": golden(gbsh()).replace("sha256=", "sha256=0"),
        }
        for name, text in cases.items():
            with self.subTest(name):
                status, output = self.run_diff(gbsh(frames=4), text)
                self.assertEqual(status, 2, output)
                self.assertIn("error: ", output)
        status, output = self.run_diff(gbsh(frames=4), cases["not ASCII"])
        self.assertIn("not ASCII text", output)
        status, output = self.run_diff(gbsh(frames=4), cases["repeated frame"])
        self.assertIn("frame 1 is out of order", output)

    def test_usage_errors(self):
        with tempfile.TemporaryDirectory() as directory, \
                contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as raised:
                diff.main([directory])
            self.assertEqual(raised.exception.code, 2)
            missing = str(Path(directory) / "missing.gbsh")
            self.assertEqual(diff.main([missing, missing]), 2)

    def test_golden_match(self):
        full = gbsh()
        for a, b in ((full, golden(full)), (golden(full), full),
                     (golden(full), golden(full)),
                     (full, golden(full).replace("\n", "\r\n"))):
            status, output = self.run_diff(a, b)
            self.assertEqual(status, 0, output)
            self.assertIn("same sha256", output)

    def test_golden_window(self):
        changed = gbsh(changes={(frame, "iwram") for frame in range(130, 200)})
        for a, b in ((golden(gbsh()), changed), (changed, golden(gbsh()))):
            status, output = self.run_diff(a, b)
            self.assertEqual(status, 1, output)
            self.assertIn("first sampled divergence in frames 121..180: "
                          "frame 180 region iwram\n", output)
            self.assertIn("unsampled frame may differ first", output)
        status, output = self.run_diff(golden(gbsh()), gbsh(changes={(0, "regs")}))
        self.assertEqual(status, 1, output)
        self.assertIn("frames 0..0: frame 0 region regs", output)
        status, output = self.run_diff(golden(gbsh()), gbsh(frames=150))
        self.assertEqual(status, 1, output)
        self.assertRegex(output, r"frames 121\.\.180: frame 180 missing in .*b")

    def test_golden_sha_mismatch_with_equal_samples(self):
        status, output = self.run_diff(golden(gbsh()), gbsh(changes={(61, "ewram")}))
        self.assertEqual(status, 1, output)
        self.assertIn("the sampled frames match but the sha256 differs", output)

    def test_counters_compare_by_shared_names(self):
        late = "counters late=1"
        for a, b in ((gbsh(), gbsh(counters=late)),
                     (golden(gbsh()) + late + "\n", gbsh())):
            status, output = self.run_diff(a, b)
            self.assertEqual(status, 1, output)
            self.assertIn("counters differ: late=", output)
        # A build that adds a counter still matches older files and goldens.
        newer = gbsh(counters="counters late=0 dside=0")
        for a in (gbsh(counters=None), golden(gbsh(counters=None)), gbsh()):
            status, output = self.run_diff(a, newer)
            self.assertEqual(status, 0, output)
            self.assertRegex(output, r"counter dside is only in .*b\.gbsh")
        status, output = self.run_diff(golden(gbsh()), gbsh(counters=late))
        self.assertEqual(status, 0, output)

    def test_pinned_text_matches_the_c_test(self):
        source = (ROOT / "tests" / "test_state_hash.c").read_text()
        basis = re.search(r'#define BASIS "([0-9a-f]{8})"', source).group(1)
        block = re.search(r"two_frame_text\[\] =(.*?);", source, re.S).group(1)
        c_text = "".join(
            basis if match.group(2) else match.group(1)
            for match in re.finditer(r'"([^"]*)"|\b(BASIS)\b', block)
        ).replace("\\n", "\n")
        self.assertEqual(c_text, TWO_FRAME_TEXT)
        parsed = diff.parse(TWO_FRAME_TEXT.encode("ascii"), "pinned")
        self.assertEqual(parsed["records"][1][0], "040c5b8c")
        self.assertEqual(parsed["counters"], "counters late=3 skipped=0")
        status, output = self.run_diff(TWO_FRAME_TEXT, TWO_FRAME_TEXT)
        self.assertEqual(status, 0, output)


if __name__ == "__main__":
    unittest.main()
