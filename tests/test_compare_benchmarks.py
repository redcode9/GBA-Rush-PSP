"""Tests for scripts/compare-benchmarks.py on synthetic benchmark reports."""

import importlib.util
import pathlib
import tempfile
import unittest


# The script name has a dash, so load it as a module by path.
SCRIPT = pathlib.Path(__file__).parents[1] / "scripts" / "compare-benchmarks.py"
SPEC = importlib.util.spec_from_file_location("compare_benchmarks", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def report(frame_p95=17000, frames=600, duration=10_000_000,
           underruns=0, skipped=0, clock=333, fast_forward_time=None,
           fast_forward_frames=0, psp_fw_version="600", vsync="0",
           audio_sample_rate="48000", audio_samples="800"):
    """A parsed report as read_report() returns it.

    Passing None for an optional field leaves it out, as in reports from
    older builds.
    """
    result = {
        "state": "stopped",
        "psp-model": "1",
        "clock-mhz": str(clock),
        "renderer": "1",
        "scale": "6",
        "filter": "0",
        "frameskip-type": "0",
        "frameskip-value": "9",
        "sound-volume": "10",
        "game-code": "BPRE",
        "rom-revision": "0",
        "duration-us": str(duration),
        "emulated-frames": str(frames),
        "skipped-frames": str(skipped),
        "audio-underruns": str(underruns),
        "frame-time-p95-us": str(frame_p95),
        "ge-wait-p95-us": "500",
    }
    if psp_fw_version is not None:
        result["psp-fw-version"] = psp_fw_version
    if vsync is not None:
        result["vsync"] = vsync
    if audio_sample_rate is not None:
        result["audio-sample-rate"] = audio_sample_rate
    if audio_samples is not None:
        result["audio-samples"] = audio_samples
    if fast_forward_time is not None:
        result["fast-forward-time-us"] = str(fast_forward_time)
        result["fast-forward-frames"] = str(fast_forward_frames)
    return result


class BenchmarkComparisonTests(unittest.TestCase):
    def test_median_candidate_passes(self):
        baseline = [report(), report(frame_p95=17100), report(frame_p95=16900)]
        candidate = [report(frames=605), report(frames=606), report(frames=604)]

        result = MODULE.compare(baseline, candidate)

        self.assertTrue(result["passed"])
        self.assertGreater(result["throughput_change"], 0)

    def test_regressions_fail(self):
        # Throughput -5%, p95 +6%, more underruns and skips: four failures.
        baseline = [report(), report(), report()]
        candidate = [
            report(frame_p95=18000, frames=570, underruns=2, skipped=12),
            report(frame_p95=18100, frames=570, underruns=2, skipped=12),
            report(frame_p95=17900, frames=570, underruns=2, skipped=12),
        ]

        result = MODULE.compare(baseline, candidate)

        self.assertFalse(result["passed"])
        self.assertEqual(len(result["failures"]), 4)

    def test_mismatched_configuration_is_rejected(self):
        baseline = [report(), report(), report()]
        candidate = [report(), report(), report(clock=222)]

        with self.assertRaisesRegex(ValueError, "clock-mhz"):
            MODULE.compare(baseline, candidate)

    def test_fast_forward_regression_fails(self):
        baseline = [
            report(fast_forward_time=1_000_000, fast_forward_frames=120)
            for _ in range(3)
        ]
        candidate = [
            report(fast_forward_time=1_100_000, fast_forward_frames=120)
            for _ in range(3)
        ]

        result = MODULE.compare(baseline, candidate)

        self.assertFalse(result["passed"])
        self.assertEqual(
            result["failures"],
            ["fast-forward speed regression exceeds 2%"],
        )
        self.assertLess(result["fast_forward_change"], -2.0)

    def test_new_identity_fields_must_match(self):
        baseline = [report(), report(), report()]
        candidate = [report(vsync="1") for _ in range(3)]

        with self.assertRaisesRegex(ValueError, "vsync"):
            MODULE.compare(baseline, candidate)

    def test_reports_without_new_identity_fields_compare(self):
        def legacy():
            """A report from a build older than the identity fields."""
            return report(psp_fw_version=None, vsync=None,
                          audio_sample_rate=None, audio_samples=None)

        baseline = [legacy() for _ in range(3)]
        candidate = [legacy() for _ in range(3)]

        result = MODULE.compare(baseline, candidate)

        self.assertTrue(result["passed"])

    def test_paging_identity_must_match_when_reported(self):
        baseline = [report() for _ in range(3)]
        candidate = [report() for _ in range(3)]
        for run in baseline + candidate:
            run.update({"gamepak-file-size": "33554432",
                        "gamepak-ram-buffer-size": "16777216",
                        "gamepak-ram-pages": "512"})
        for field, changed in (("gamepak-file-size", "16777216"),
                               ("gamepak-ram-buffer-size", "33554432"),
                               ("gamepak-ram-pages", "1024")):
            original = candidate[0][field]
            candidate[0][field] = changed
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, field):
                MODULE.compare(baseline, candidate)
            candidate[0][field] = original

        del candidate[0]["gamepak-ram-buffer-size"]
        with self.assertRaisesRegex(ValueError, "gamepak-ram-buffer-size"):
            MODULE.compare(baseline, candidate)
        candidate[0]["gamepak-ram-buffer-size"] = "16777216"
        self.assertTrue(MODULE.compare(baseline, candidate)["passed"])

    def test_parser_rejects_unstopped_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "report.txt"
            path.write_text("state=active\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "not stopped"):
                MODULE.read_report(path)


if __name__ == "__main__":
    unittest.main()
