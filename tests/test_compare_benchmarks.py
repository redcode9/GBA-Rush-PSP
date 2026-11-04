"""Tests for scripts/compare-benchmarks.py on synthetic benchmark reports."""

import contextlib
import importlib.util
import io
import pathlib
import tempfile
import time
import unittest
import unittest.mock


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


def write_report(path, values):
    """Write a parsed report back as the key=value file read_report() reads."""
    path.write_text("".join(f"{key}={value}\n" for key, value in values.items()),
                    encoding="utf-8")


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

    def test_exact_permutation_complete_separation_3v3(self):
        baseline = [report(frames=frames) for frames in (600, 601, 602)]
        candidate = [report(frames=frames) for frames in (610, 611, 612)]

        result = MODULE.compare(baseline, candidate)

        name, _, _, p, text, needed = result["metrics"][0]
        self.assertEqual(name, "throughput")
        self.assertAlmostEqual(p, 0.05)
        self.assertIn("candidate higher (p=0.050, 3+3 runs)", text)
        self.assertIsNone(needed)
        self.assertIn(
            "throughput-runs=baseline 60.10 [60.00..60.20], "
            "candidate 61.10 [61.00..61.20]; candidate higher",
            MODULE.format_result(result),
        )
        # The same separation the other way round is a lower candidate.
        p, text, _ = MODULE.verdict([61.0, 61.1, 61.2], [60.0, 60.1, 60.2])
        self.assertAlmostEqual(p, 0.05)
        self.assertTrue(text.startswith("candidate lower"))

    def test_overlap_is_undecided_with_runs_needed(self):
        baseline = [report(frames=frames) for frames in (600, 605, 610)]
        candidate = [report(frames=frames) for frames in (603, 608, 613)]

        result = MODULE.compare(baseline, candidate)

        _, _, _, p, text, needed = result["metrics"][0]
        self.assertGreater(p, 0.05)
        self.assertIn("undecided", text)
        self.assertIsInstance(needed, int)
        self.assertGreater(needed, 3)
        self.assertIn(f"about {needed} runs per build needed", text)
        output = MODULE.format_result(result)
        self.assertNotIn("higher", output)
        self.assertNotIn("lower", output)
        # Identical runs are a tie in every pick: nothing to decide.
        p, text, needed = MODULE.verdict([60.1] * 3, [60.1] * 3)
        self.assertEqual(p, 1.0)
        self.assertIn("no difference observed", text)
        self.assertIsNone(needed)

    def test_unequal_run_counts_4v3(self):
        baseline = [report(frames=frames) for frames in (600, 601, 602, 603)]
        candidate = [report(frames=frames) for frames in (610, 611, 612)]

        result = MODULE.compare(baseline, candidate)

        _, baseline_runs, _, p, text, _ = result["metrics"][0]
        self.assertAlmostEqual(p, 1 / 35)
        self.assertIn("4+3 runs", text)
        for value, expected in zip(baseline_runs, (60.15, 60.0, 60.3)):
            self.assertAlmostEqual(value, expected)
        # PASS/FAIL still comes from the medians: 60.15 against 61.10 fps.
        self.assertTrue(result["passed"])
        self.assertAlmostEqual(result["throughput_change"],
                               (61.1 / 60.15 - 1.0) * 100.0)

    def test_fewer_than_three_runs_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for index in range(5):
                path = pathlib.Path(directory) / f"run-{index}.txt"
                write_report(path, report())
                paths.append(str(path))

            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                status = MODULE.main(["--baseline", *paths[:2],
                                      "--candidate", *paths[2:]])

        self.assertEqual(status, 2)
        self.assertIn("at least 3 reports per build", errors.getvalue())
        with self.assertRaisesRegex(ValueError, "at least 3"):
            MODULE.compare([report(), report()], [report() for _ in range(3)])

    def test_many_runs_use_sampled_picks(self):
        baseline = [report(frames=600 + index) for index in range(15)]
        candidate = [report(frames=620 + index) for index in range(15)]

        start = time.perf_counter()
        result = MODULE.compare(baseline, candidate)
        # Counting all C(30, 15) picks of every metric would take minutes.
        self.assertLess(time.perf_counter() - start, 5.0)

        _, _, _, p, text, _ = result["metrics"][0]
        self.assertAlmostEqual(p, 1 / (MODULE.SAMPLED_PICKS + 1))
        self.assertIn("candidate higher (p~0.000, 15+15 runs)", text)
        # Constant metrics are a tie in every pick and are not sampled.
        self.assertIn("ge-p95-runs=baseline 500 [500..500], candidate 500 "
                      "[500..500]; undecided (p=1.000, 15+15 runs)",
                      MODULE.format_result(result))
        # The sampled estimate agrees with the exact count on 3+3 runs.
        values = ([60.0, 60.5, 61.0], [60.3, 60.8, 61.3])
        with unittest.mock.patch.object(MODULE, "EXACT_PICKS", 0):
            p, text, _ = MODULE.verdict(*values)
        self.assertAlmostEqual(p, 0.25, delta=0.01)
        self.assertIn("undecided (p~0.2", text)

    def test_cli_compares_three_runs_per_build(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for index, frames in enumerate((600, 601, 602, 610, 611, 612)):
                path = pathlib.Path(directory) / f"run-{index}.txt"
                write_report(path, report(frames=frames))
                paths.append(str(path))

            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                status = MODULE.main(["--baseline", *paths[:3],
                                      "--candidate", *paths[3:]])

        self.assertEqual(status, 0)
        self.assertTrue(output.getvalue().startswith("PASS\n"))

    def test_legacy_v6_fixtures_same_pass_fail(self):
        passing = MODULE.compare(
            [report(), report(frame_p95=17100), report(frame_p95=16900)],
            [report(frames=605), report(frames=606), report(frames=604)],
        )
        self.assertTrue(passing["passed"])
        self.assertEqual(passing["failures"], [])
        # The lines printed before the per-run lines are unchanged.
        self.assertEqual(
            [line for line in MODULE.format_result(passing).splitlines()
             if "-runs=" not in line],
            ["PASS",
             "throughput=60.50 fps (+0.83%)",
             "frame-p95=17000 us (+0.00%)",
             "ge-p95=500 us (+0.00%)",
             "audio-underruns=0",
             "skipped-frames=0.00%"],
        )

        regressed = MODULE.compare(
            [report(), report(), report()],
            [report(frame_p95=p95, frames=570, underruns=2, skipped=12)
             for p95 in (18000, 18100, 17900)],
        )
        self.assertFalse(regressed["passed"])
        self.assertEqual(regressed["failures"], [
            "throughput regression exceeds 2%",
            "frame-time p95 regression exceeds 5%",
            "audio underruns increased",
            "skipped-frame ratio increased by more than 0.5 points",
        ])

        fast_forward = MODULE.compare(
            [report(fast_forward_time=1_000_000, fast_forward_frames=120)
             for _ in range(3)],
            [report(fast_forward_time=1_100_000, fast_forward_frames=120)
             for _ in range(3)],
        )
        self.assertFalse(fast_forward["passed"])
        self.assertEqual(fast_forward["failures"],
                         ["fast-forward speed regression exceeds 2%"])
        self.assertEqual(fast_forward["metrics"][-1][0], "fast-forward")


if __name__ == "__main__":
    unittest.main()
