import importlib.util
import pathlib
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).parents[1] / "scripts" / "compare-benchmarks.py"
SPEC = importlib.util.spec_from_file_location("compare_benchmarks", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def report(frame_p95=17000, frames=600, duration=10_000_000,
           underruns=0, skipped=0, clock=333):
    return {
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


class BenchmarkComparisonTests(unittest.TestCase):
    def test_median_candidate_passes(self):
        baseline = [report(), report(frame_p95=17100), report(frame_p95=16900)]
        candidate = [report(frames=605), report(frames=606), report(frames=604)]

        result = MODULE.compare(baseline, candidate)

        self.assertTrue(result["passed"])
        self.assertGreater(result["throughput_change"], 0)

    def test_regressions_fail(self):
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

    def test_parser_rejects_unstopped_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "report.txt"
            path.write_text("state=active\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "not stopped"):
                MODULE.read_report(path)


if __name__ == "__main__":
    unittest.main()
