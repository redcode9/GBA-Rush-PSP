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
           audio_sample_rate="48000", audio_samples="800", frame_p99=18000,
           frame_max=20000, work_p95=None, over_budget=None):
    """A parsed report as read_report() returns it.

    Passing None for an optional field leaves it out, as in reports from
    older builds; work_p95 and over_budget (format 7) are left out unless
    given.
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
        "frame-time-p99-us": str(frame_p99),
        "frame-time-max-us": str(frame_max),
        "ge-wait-p95-us": "500",
    }
    if work_p95 is not None:
        result["work-time-p95-us"] = str(work_p95)
    if over_budget is not None:
        result["frames-over-1-5x-budget"] = str(over_budget)
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


def v7(**changed):
    """A two-minute report with the format 7 work fields: 12 ms work p95
    and no frame over 1.5x the budget unless changed."""
    values = {"frames": 7200, "duration": 120_000_000, "work_p95": 12000,
              "over_budget": 0}
    values.update(changed)
    return report(**values)


def write_report(path, values):
    """Write a parsed report back as the key=value file read_report() reads."""
    path.write_text("".join(f"{key}={value}\n" for key, value in values.items()),
                    encoding="utf-8")


def write_group(directory, game_code, frames, version="1.4.3", revision="0"):
    """Write one run per frames value of game_code under directory as the
    archive lays it out: <game-code>-r<revision>/<version>/run-N.txt."""
    folder = pathlib.Path(directory) / f"{game_code}-r{revision}" / version
    folder.mkdir(parents=True, exist_ok=True)
    for index, count in enumerate(frames):
        values = report(frames=count)
        values["game-code"] = game_code
        values["rom-revision"] = revision
        values["version"] = version
        write_report(folder / f"run-{index}.txt", values)


def runs_at(*minutes):
    """One report per minute given, captured at 10:<minute> on one day."""
    return [dict(report(), **{"capture-local-time":
                              f"2026-11-09T10:{minute:02d}:00"})
            for minute in minutes]


def run_files(baseline, candidate, *flags):
    """main() on the reports written as files: (exit status, stdout)."""
    with tempfile.TemporaryDirectory() as directory:
        paths = []
        for index, values in enumerate(baseline + candidate):
            path = pathlib.Path(directory) / f"run-{index}.txt"
            write_report(path, values)
            paths.append(str(path))
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = MODULE.main(["--baseline", *paths[:len(baseline)],
                                  "--candidate", *paths[len(baseline):],
                                  *flags])
    return status, output.getvalue()


def run_groups(baseline_dirs, candidate_dir):
    """main() in directory mode: (exit status, stdout)."""
    if isinstance(baseline_dirs, str):
        baseline_dirs = [baseline_dirs]
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        status = MODULE.main(["--baseline-dir", *baseline_dirs,
                              "--candidate-dir", candidate_dir])
    return status, output.getvalue()


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

    def test_gate_boundaries(self):
        """Each threshold passes at its exact value and fails just past it."""
        baseline = [report(), report(), report()]

        def failures(**changed):
            return MODULE.compare(baseline, [report(**changed)
                                             for _ in range(3)])["failures"]

        def drop_failures(baseline_changes, **changed):
            return MODULE.compare(
                [report(**baseline_changes) for _ in range(3)],
                [report(**changed) for _ in range(3)])["failures"]

        # 35 to 34.3 fps in 10 s is exactly -2%, which float rates put past
        # the gate; one microsecond more is past it.
        self.assertEqual(drop_failures({"frames": 350}, frames=343), [])
        self.assertEqual(drop_failures({"frames": 350}, frames=343,
                                       duration=10_000_001),
                         ["throughput regression exceeds 2%"])
        # The same for 1350 to 1323 frames in 10 s of fast-forward.
        fast = {"fast_forward_time": 10_000_000, "fast_forward_frames": 1350}
        self.assertEqual(drop_failures(fast, fast_forward_time=10_000_000,
                                       fast_forward_frames=1323), [])
        self.assertEqual(drop_failures(fast, fast_forward_time=10_000_001,
                                       fast_forward_frames=1323),
                         ["fast-forward speed regression exceeds 2%"])
        self.assertEqual(failures(frame_p95=17850), [])
        self.assertEqual(failures(frame_p95=17851),
                         ["frame-time p95 regression exceeds 5%"])
        self.assertEqual(failures(frame_p99=18900), [])
        self.assertEqual(failures(frame_p99=18901),
                         ["frame-time p99 regression exceeds 5%"])
        # The max limit is 1.5x the baseline median of 20000 us.
        self.assertEqual(failures(frame_max=30000), [])
        self.assertEqual(failures(frame_max=30001),
                         ["frame-time max above 1.5x the baseline in 3 runs"])

        def spikes(*maxima):
            return MODULE.compare(baseline, [report(frame_max=value)
                                             for value in maxima])["failures"]

        self.assertEqual(spikes(30001, 20000, 20000), [])
        self.assertEqual(spikes(30001, 30001, 20000),
                         ["frame-time max above 1.5x the baseline in 2 runs"])

        # Two-minute runs: 2 frames over budget make 1 per minute.
        def work_failures(baseline_changes, **changed):
            return MODULE.compare(
                [v7(**baseline_changes) for _ in range(3)],
                [v7(**changed) for _ in range(3)])["failures"]

        self.assertEqual(work_failures({}, work_p95=12600), [])
        self.assertEqual(work_failures({}, work_p95=12601),
                         ["work-time p95 regression exceeds 5%"])
        over_budget = ["frames over 1.5x budget per minute increased by "
                       "more than max(1, 20%)"]
        # From 0 per minute the allowance is +1 per minute...
        self.assertEqual(work_failures({}, over_budget=2), [])
        self.assertEqual(work_failures({}, over_budget=3), over_budget)
        # ...and from 10 per minute it is +20%, 2 per minute.
        self.assertEqual(work_failures({"over_budget": 20}, over_budget=24),
                         [])
        self.assertEqual(work_failures({"over_budget": 20}, over_budget=25),
                         over_budget)
        # Exactly +20% also passes when the rate is not a round float.
        odd = 93_123_457
        self.assertEqual(work_failures({"over_budget": 15, "duration": odd},
                                       over_budget=18, duration=odd), [])
        self.assertEqual(work_failures({"over_budget": 15, "duration": odd},
                                       over_budget=19, duration=odd),
                         over_budget)

    def test_p99_regression_fails_on_v6(self):
        baseline = [report(), report(frame_p99=18100), report(frame_p99=17900)]
        candidate = [report(frame_p99=19000) for _ in range(3)]

        result = MODULE.compare(baseline, candidate)

        self.assertEqual(result["failures"],
                         ["frame-time p99 regression exceeds 5%"])
        output = MODULE.format_result(result)
        self.assertIn("\nframe-p99=19000 us (+5.56%)\n", output)
        self.assertIn("\nreason=frame-time p99 regression exceeds 5%", output)
        self.assertIn("\nframe-p99-runs=baseline 18000 [17900..18100], "
                      "candidate 19000 [19000..19000]; candidate higher",
                      output)

    def test_single_max_spike_ignored(self):
        baseline = [report(), report(), report()]
        candidate = [report(frame_max=60000), report(), report()]

        result = MODULE.compare(baseline, candidate)

        self.assertTrue(result["passed"])
        self.assertEqual(result["frame_max_spikes"], 1)
        self.assertIn("\nframe-max=20000 us (runs above 30000 us: 1)\n",
                      MODULE.format_result(result))

    def test_repeated_max_spike_fails(self):
        # A spike in one baseline run does not raise the median reference.
        baseline = [report(), report(frame_max=50000), report()]
        candidate = [report(frame_max=60000), report(frame_max=31000),
                     report()]

        result = MODULE.compare(baseline, candidate)

        self.assertEqual(result["failures"],
                         ["frame-time max above 1.5x the baseline in 2 runs"])
        self.assertEqual(result["frame_max_limit"], 30000)
        # A limit of 30001.5 us prints as its floor, which the 30002 us
        # runs are above.
        result = MODULE.compare([report(frame_max=20001) for _ in range(3)],
                                [report(frame_max=30002),
                                 report(frame_max=30002), report()])
        self.assertIn("\nframe-max=30002 us (runs above 30001 us: 2)\n",
                      MODULE.format_result(result))

    def test_work_p95_regression_fails(self):
        baseline = [v7(), v7(work_p95=12100), v7(work_p95=11900)]
        candidate = [v7(work_p95=13000) for _ in range(3)]

        result = MODULE.compare(baseline, candidate)

        self.assertEqual(result["failures"],
                         ["work-time p95 regression exceeds 5%"])
        output = MODULE.format_result(result)
        self.assertIn("\nwork-p95=13000 us (+8.33%)\n", output)
        self.assertNotIn("note=work-p95", output)
        self.assertIn("\nwork-p95-runs=baseline 12000 [11900..12100], "
                      "candidate 13000 [13000..13000]; candidate higher",
                      output)

    def test_over_budget_per_minute_increase_fails(self):
        baseline = [v7(over_budget=20) for _ in range(3)]
        candidate = [v7(over_budget=30) for _ in range(3)]

        result = MODULE.compare(baseline, candidate)

        self.assertEqual(result["failures"], [
            "frames over 1.5x budget per minute increased by more than "
            "max(1, 20%)"])
        output = MODULE.format_result(result)
        self.assertIn("\nover-budget=15.0/min (baseline 10.0/min)\n", output)
        self.assertIn("\nover-budget-runs=baseline 10.0/min [10.0/min..10.0/min]"
                      ", candidate 15.0/min [15.0/min..15.0/min]; candidate "
                      "higher", output)

    def test_extreme_gates_read_the_median_of_each_build(self):
        """One slow run of either build moves no gate; two of three do."""
        quiet = [v7() for _ in range(3)]
        for slow, over, failure in (
                # A p99 never exceeds the max of the same run.
                ({"frame_p99": 24000, "frame_max": 24000}, {"frame_p99": 19000},
                 "frame-time p99 regression exceeds 5%"),
                ({"work_p95": 20000}, {"work_p95": 13000},
                 "work-time p95 regression exceeds 5%"),
                # 40 and 4 frames in two minutes: 20 and 2 per minute.
                ({"over_budget": 40, "frame_max": 40000}, {"over_budget": 4},
                 "frames over 1.5x budget per minute increased by more than "
                 "max(1, 20%)")):
            with self.subTest(slow=slow):
                one_slow = [v7(**slow), v7(), v7()]
                two_slow = [v7(**slow), v7(**slow), v7()]
                self.assertTrue(MODULE.compare(quiet, one_slow)["passed"])
                self.assertIn(failure,
                              MODULE.compare(quiet, two_slow)["failures"])
                # The reference is neither raised by one slow baseline run
                # nor lowered by one quiet one.
                self.assertIn(failure, MODULE.compare(
                    one_slow, [v7(**over) for _ in range(3)])["failures"])
                self.assertTrue(MODULE.compare(
                    two_slow, [v7(**slow) for _ in range(3)])["passed"])

    def test_v6_reports_skip_v7_gates_with_note(self):
        note = ("\nnote=work-p95 and over-budget gates skipped: a run lacks "
                "the format 7 fields\n")
        worse = [v7(work_p95=20000, over_budget=200) for _ in range(3)]
        # An early format 7 report without the fields counts as format 6.
        early = report(frames=7200, duration=120_000_000)
        early["gba-rush-performance-report"] = "7"
        for baseline, candidate in (
                ([report() for _ in range(3)], [report() for _ in range(3)]),
                ([v7(), v7(), early], worse),
                ([v7(), v7(), v7(over_budget=None)], worse),
                ([v7(), v7(), v7()], [v7(work_p95=None)] + worse[1:])):
            with self.subTest(baseline=baseline, candidate=candidate):
                result = MODULE.compare(baseline, candidate)
                self.assertTrue(result["passed"])
                self.assertIsNone(result["work_p95"])
                output = MODULE.format_result(result)
                self.assertIn(note, output)
                self.assertNotIn("work-p95=", output)
                self.assertNotIn("work-p95-runs=", output)
                self.assertNotIn("over-budget-runs=", output)

    def test_synthetic_stutter_fails_with_p95_and_p99_unchanged(self):
        # One 40 ms frame every 3 s: 40 frames over 1.5x budget in each
        # two-minute run, far too few to move p95 or p99.
        baseline = [v7() for _ in range(3)]
        stutter = [v7(frame_max=40000, over_budget=40) for _ in range(3)]

        result = MODULE.compare(baseline, stutter)

        self.assertEqual(result["failures"], [
            "frame-time max above 1.5x the baseline in 3 runs",
            "frames over 1.5x budget per minute increased by more than "
            "max(1, 20%)"])
        self.assertEqual(result["frame_p95_change"], 0)
        self.assertEqual(result["frame_p99_change"], 0)
        self.assertEqual(result["over_budget"], 20)
        # Format 6 reports still catch it on the max alone.
        for run in baseline + stutter:
            for field in MODULE.WORK_FIELDS:
                del run[field]
        self.assertEqual(MODULE.compare(baseline, stutter)["failures"],
                         ["frame-time max above 1.5x the baseline in 3 runs"])
        # The same stutter in one run only passes.
        self.assertTrue(MODULE.compare(
            [v7() for _ in range(3)],
            [v7(frame_max=40000, over_budget=40), v7(), v7()])["passed"])

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

    def test_bench_vs_paced_rejected(self):
        def bench(**changed):
            """A report of the unbound benchmark."""
            return dict(report(), **{
                "bench-mode": "unbound", "bench-frames": "3600",
                "bench-render": "1", "bench-audio": "1",
                "bench-input": "none", **changed})

        paced = dict(report(), **{
            "bench-mode": "paced", "bench-frames": "0", "bench-render": "1",
            "bench-audio": "1", "bench-input": "pad"})
        unbound = [bench() for _ in range(3)]
        # A paced baseline, with the bench fields or from a build without
        # them, never compares with a benchmark, on either side.
        for name, runs in (("paced", [paced] * 3), ("no fields", [report()] * 3)):
            for baseline, candidate in ((runs, unbound), (unbound, runs)):
                with self.subTest(name, baseline=baseline[0].get("bench-mode")), \
                        self.assertRaisesRegex(ValueError, "bench-mode"):
                    MODULE.compare(baseline, candidate)
        errors = io.StringIO()
        with contextlib.redirect_stderr(errors):
            status, _ = run_files([report()] * 3, unbound)
        self.assertEqual((status, errors.getvalue()),
                         (2, "ERROR: benchmark field does not match: bench-mode\n"))

        self.assertTrue(MODULE.compare(unbound, [bench() for _ in range(3)])["passed"])
        for field, value in (("bench-frames", "1800"), ("bench-render", "0"),
                             ("bench-audio", "0"), ("bench-input", "pad")):
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, field):
                MODULE.compare(unbound, [bench(), bench(), bench(**{field: value})])

        # Paced captures compare whichever build's reports lack the fields.
        self.assertTrue(MODULE.compare([report()] * 3, [paced] * 3)["passed"])
        self.assertTrue(MODULE.compare([paced] * 3, [report()] * 3)["passed"])

    def read_back(self, directory, name, values):
        """values written as a report file and read with read_report()."""
        path = pathlib.Path(directory) / name
        path.write_text("".join(f"{key}={value}\n" for key, value in values.items()),
                        encoding="utf-8")
        return MODULE.read_report(path)

    def test_format_six_and_seven_reports_of_different_builds_compare(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = [
                self.read_back(directory, f"base-{run}.txt", {
                    "gba-rush-performance-report": "6", "version": "1.4.3",
                    **report()})
                for run in range(3)
            ]
            candidate = [
                self.read_back(directory, f"cand-{run}.txt", {
                    "gba-rush-performance-report": "7", "version": "1.5.0",
                    "build-id": f"0123456789a{run}-dirty", **report(frames=605)})
                for run in range(3)
            ]

        self.assertTrue(MODULE.compare(baseline, candidate)["passed"])

    def test_versions_before_1_4_3_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for version in ("1.4.2", "0.9.9", "1.3.10"):
                with self.subTest(version=version), self.assertRaisesRegex(
                        ValueError, "64-bit counters are not valid before 1.4.3"):
                    self.read_back(directory, "old.txt",
                                   {"version": version, **report()})
            for version in ("1.4.3", "1.4.3-rc.1", "1.10.0", "2.0.0"):
                with self.subTest(version=version):
                    self.assertEqual(self.read_back(
                        directory, "new.txt",
                        {"version": version, **report()})["version"], version)
            self.assertNotIn("version", self.read_back(directory, "bare.txt", report()))
            with self.assertRaisesRegex(ValueError, "invalid version"):
                self.read_back(directory, "bad.txt", {"version": "dev", **report()})

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
        self.assertAlmostEqual(p, 0.25)
        self.assertIn("undecided", text)
        # 2 * ((z(0.95) + z(0.80)) * 0.5 / 0.3) ** 2 = 34.3, rounded up.
        self.assertEqual(needed, 35)
        self.assertIn("about 35 runs per build needed", text)
        output = MODULE.format_result(result)
        self.assertNotIn("higher", output)
        self.assertNotIn("lower", output)
        # One swapped pair at 4+3 is still undecided (p = 2/35).
        p, text, _ = MODULE.verdict([1, 2, 3, 5], [4, 6, 7])
        self.assertAlmostEqual(p, 2 / 35)
        self.assertTrue(text.startswith("undecided"))
        # A wide gap with a tie: the estimate is at least one more run.
        _, _, needed = MODULE.verdict([61, 61, 61], [61, 70, 70])
        self.assertEqual(needed, 4)
        # Identical runs are a tie in every pick: nothing to decide.
        p, text, needed = MODULE.verdict([60.1] * 3, [60.1] * 3)
        self.assertEqual(p, 1.0)
        self.assertIn("no difference observed", text)
        self.assertIsNone(needed)

    def test_tied_medians_take_the_side_of_the_means(self):
        # Sparse underruns tie on a median of 0; the summed test statistic
        # follows the means, which put the candidate higher.
        p, text, needed = MODULE.verdict([0] * 10, [0] * 6 + [3, 4, 5, 6])
        self.assertAlmostEqual(p, 14 / 323)
        self.assertEqual(text, "candidate higher (p=0.043, 10+10 runs)")
        self.assertIsNone(needed)
        # Short of ALPHA, the runs estimate comes from the mean difference.
        _, text, needed = MODULE.verdict([0] * 10, [0] * 8 + [3, 4])
        self.assertEqual(text, "undecided (p=0.237, 10+10 runs), about 29 "
                         "runs per build needed")
        self.assertEqual(needed, 29)
        # Equal medians and means give nothing to estimate from, and no
        # claim even at a low p.
        p, text, needed = MODULE.verdict([1, 2, 3], [0, 2, 4])
        self.assertEqual(text, f"undecided (p={p:.3f}, 3+3 runs), medians "
                         "and means equal")
        self.assertIsNone(needed)
        with unittest.mock.patch.object(MODULE, "permutation_p",
                                        return_value=(0.01, True)):
            _, text, _ = MODULE.verdict([1, 2, 3], [0, 2, 4])
        self.assertTrue(text.startswith("undecided (p=0.010"))

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
        with self.assertRaisesRegex(ValueError, "at least 3"):
            MODULE.compare([report() for _ in range(3)], [report(), report()])

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
        # Seeded: the same overlapping 11+10 runs always give the same p.
        baseline = [60.0 + 0.1 * index for index in range(11)]
        candidate = [60.3 + 0.1 * index for index in range(10)]
        first = MODULE.verdict(baseline, candidate)
        self.assertIn("p~", first[1])
        self.assertGreater(first[0], 10 / (MODULE.SAMPLED_PICKS + 1))
        self.assertEqual(MODULE.verdict(baseline, candidate), first)

    def test_float_rounding_ties_count(self):
        # 60.3 + 60.8 + 60.3 ties the candidate's 181.4 but sums a little
        # lower in floats; ties count, so 9 of the 20 picks do, not 8.
        p, _, _ = MODULE.verdict([60.0, 60.3, 60.8], [60.3, 60.4, 60.7])
        self.assertAlmostEqual(p, 9 / 20)
        # Same frame totals give throughput means (5+5, odd counts) or
        # medians (4+4, averaged middle runs) a few ulps apart: a tie, not
        # an effect with an astronomic runs estimate.
        for frames in (((590, 595, 595, 595, 592), (590, 595, 595, 599, 588)),
                       ((587, 589, 593, 595), (587, 587, 595, 595))):
            baseline, candidate = ([report(frames=value) for value in runs]
                                   for runs in frames)
            *_, text, needed = MODULE.compare(
                baseline, candidate)["metrics"][0]
            self.assertTrue(text.endswith("runs), medians and means equal"),
                            text)
            self.assertIsNone(needed)

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

    def test_group_mode_pass(self):
        with tempfile.TemporaryDirectory() as baseline, \
                tempfile.TemporaryDirectory() as candidate:
            for game in ("BPRE", "BPEE"):
                write_group(baseline, game, (600, 601, 602))
                write_group(candidate, game, (605, 606, 604), "1.5.0")
            # A second ROM revision of a title is a group of its own.
            write_group(baseline, "BPRE", (600, 601, 602), revision="1")
            write_group(candidate, "BPRE", (605, 606, 604), "1.5.0", "1")
            # A title only the candidate has is listed but decides nothing.
            write_group(candidate, "AXVE", (600, 600, 600), "1.5.0")

            status, output = run_groups(baseline, candidate)

        self.assertEqual(status, 0)
        self.assertIn("group=1/BPEE-r0 PASS\nthroughput=60.50 fps", output)
        self.assertIn("group=1/BPRE-r0 PASS\n", output)
        self.assertIn("group=1/BPRE-r1 PASS\n", output)
        self.assertIn("group=1/AXVE-r0 not in baseline", output)
        self.assertTrue(output.endswith("PASS groups=3 failed=0 missing=0\n"))

    def test_group_mode_selects_one_release_of_the_archive(self):
        with tempfile.TemporaryDirectory() as archive, \
                tempfile.TemporaryDirectory() as candidate:
            model = pathlib.Path(archive) / "psp-1000"
            for game in ("BPRE", "BPEE"):
                # The candidate is 5% slower than 1.4.3 but not than 1.5.
                write_group(model, game, (640, 641, 642))
                write_group(model, game, (600, 601, 602), "1.5.0")
                write_group(candidate, game, (605, 606, 604), "1.6.0")

            # The whole model directory pools both releases.
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                self.assertEqual(run_groups(str(model), candidate)[0], 2)
            self.assertIn("group 1/BPEE-r0 mixes versions", errors.getvalue())

            releases = sorted(path.as_posix() for path in model.glob("*/1.5.0"))
            self.assertEqual(len(releases), 2)
            status, output = run_groups(releases, candidate)

        self.assertEqual(status, 0)
        self.assertIn("group=1/BPRE-r0 PASS\nthroughput=60.50 fps", output)
        self.assertTrue(output.endswith("PASS groups=2 failed=0 missing=0\n"))

    def test_group_mode_fails_on_any_group(self):
        with tempfile.TemporaryDirectory() as baseline, \
                tempfile.TemporaryDirectory() as candidate:
            for game in ("BPRE", "BPEE"):
                write_group(baseline, game, (600, 601, 602))
            write_group(candidate, "BPRE", (605, 606, 604))
            # BPEE loses 5% throughput.
            write_group(candidate, "BPEE", (570, 571, 572))

            status, output = run_groups(baseline, candidate)

        self.assertEqual(status, 1)
        self.assertIn("group=1/BPEE-r0 FAIL\n", output)
        self.assertIn("reason=throughput regression exceeds 2%", output)
        self.assertIn("group=1/BPRE-r0 PASS\n", output)
        self.assertTrue(output.endswith("FAIL groups=2 failed=1 missing=0\n"))

    def test_missing_group_fails(self):
        with tempfile.TemporaryDirectory() as baseline, \
                tempfile.TemporaryDirectory() as candidate:
            for game in ("BPRE", "BPEE"):
                write_group(baseline, game, (600, 601, 602))
            write_group(candidate, "BPRE", (605, 606, 604))

            status, output = run_groups(baseline, candidate)

        self.assertEqual(status, 1)
        self.assertIn("group=1/BPEE-r0 MISSING\n", output)
        self.assertIn("group=1/BPRE-r0 PASS\n", output)
        self.assertTrue(output.endswith("FAIL groups=2 failed=0 missing=1\n"))

    def test_group_mode_rejects_bad_input(self):
        with tempfile.TemporaryDirectory() as baseline, \
                tempfile.TemporaryDirectory() as candidate:
            write_group(baseline, "BPRE", (600, 601, 602))
            write_group(candidate, "BPRE", (605, 606))
            (pathlib.Path(baseline) / "notes.md").write_text("ok\n")

            # A group short of MIN_RUNS is invalid input, named by group.
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                self.assertEqual(run_groups(baseline, candidate)[0], 2)
            self.assertIn("group 1/BPRE-r0: at least 3 reports", errors.getvalue())
            # Overlapping directories count each report once, so they do
            # not get the short group past MIN_RUNS.
            errors = io.StringIO()
            with contextlib.redirect_stderr(errors):
                self.assertEqual(run_groups([candidate,
                                             f"{candidate}/BPRE-r0"],
                                            baseline)[0], 2)
            self.assertIn("group 1/BPRE-r0: at least 3 reports", errors.getvalue())

            # The core profile a DIAGNOSTIC_CORE_PROFILE build writes next
            # to each report is not a report.
            (pathlib.Path(baseline) / "BPRE-r0" / "1.4.3" /
             "run-0-core.txt").write_text("profile\n")
            self.assertEqual(len(MODULE.collect_groups(baseline)[
                ("1", "BPRE", "0")]), 3)

            write_group(candidate, "BPRE", (605, 606, 604, 607), "1.5.1")
            write_group(candidate, "BPRE", (605, 606, 604), "1.5.0")
            with self.assertRaisesRegex(ValueError, "1/BPRE-r0 mixes versions"):
                MODULE.collect_groups(candidate)

            values = report()
            del values["rom-revision"]
            write_report(pathlib.Path(candidate) / "BPRE-r0" / "1.5.1" /
                         "run-3.txt", values)
            with self.assertRaisesRegex(ValueError,
                                        "run-3.txt: missing field: rom-revision"):
                MODULE.collect_groups(candidate)

            (pathlib.Path(baseline) / "state.bin").write_bytes(b"\0")
            with self.assertRaisesRegex(ValueError, "state.bin: not a .txt"):
                MODULE.collect_groups(baseline)
            with self.assertRaisesRegex(ValueError, "no reports in"):
                MODULE.collect_groups(pathlib.Path(baseline) / "absent")
            # One empty directory among several is an error too.
            with self.assertRaisesRegex(ValueError, "no reports in .*absent"):
                MODULE.collect_groups(pathlib.Path(baseline) / "absent", candidate)

            # Files and directories do not mix, and each mode needs both sides.
            for arguments in (["--baseline", "a.txt", "--candidate-dir", candidate],
                              ["--baseline-dir", baseline],
                              ["--baseline", "a.txt", "--candidate", "b.txt",
                               "--baseline-dir", baseline,
                               "--candidate-dir", candidate]):
                with contextlib.redirect_stderr(io.StringIO()), \
                        self.assertRaises(SystemExit) as stop:
                    MODULE.main(arguments)
                self.assertEqual(stop.exception.code, 2, arguments)

            # A stray non-UTF-8 file, such as ._run-0.txt, is named.
            stray = pathlib.Path(baseline) / "._run-0.txt"
            stray.write_bytes(b"\xff\xfe\0")
            with self.assertRaisesRegex(ValueError,
                                        r"\._run-0\.txt: not a UTF-8 report"):
                MODULE.read_report(stray.as_posix())

    def test_link_capture_is_rejected(self):
        linked = report()
        linked["link-lockstep-frames"] = "1"
        unlinked = report()
        unlinked["link-lockstep-frames"] = "0"

        with self.assertRaisesRegex(
                ValueError, r"^captured during a lockstep link: forced "
                r"frameskip and priority 0x77$"):
            MODULE.compare([report(), report(), report()],
                           [unlinked, unlinked, linked])
        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for index, values in enumerate([report()] * 3 + [linked] * 3):
                path = pathlib.Path(directory) / f"run-{index}.txt"
                write_report(path, values)
                paths.append(str(path))
            error = io.StringIO()
            with contextlib.redirect_stderr(error):
                status = MODULE.main(["--baseline", *paths[:3],
                                      "--candidate", *paths[3:]])
        self.assertEqual(status, 2)
        self.assertIn("captured during a lockstep link", error.getvalue())

    def test_allow_link_needs_every_run_in_lockstep(self):
        def linked(frames):
            values = report(frames=frames)
            values["link-lockstep-frames"] = str(frames)
            return values

        baseline = [linked(600), linked(601), linked(602)]
        candidate = [linked(605), linked(606), linked(604)]
        self.assertTrue(MODULE.compare(baseline, candidate,
                                       allow_link=True)["passed"])
        # A run without the field, as format 6 writes it, is not in lockstep;
        # either arm out of lockstep fails.
        unlinked = [report(), report(), report()]
        for arms in ((baseline, [report(frames=605)] + candidate[1:]),
                     (baseline, unlinked), (unlinked, candidate)):
            with self.subTest(arms=arms), self.assertRaisesRegex(
                    ValueError, "lockstep link.*in only some runs"):
                MODULE.compare(*arms, allow_link=True)

        key = ("1", "BPRE", "0")
        outcome = MODULE.compare_groups({key: baseline}, {key: candidate},
                                        allow_link=True)
        self.assertTrue(outcome["passed"])
        with self.assertRaisesRegex(ValueError,
                                    "group 1/BPRE-r0: captured during"):
            MODULE.compare_groups({key: baseline}, {key: candidate})

        with tempfile.TemporaryDirectory() as directory:
            paths = []
            for index, values in enumerate(baseline + candidate):
                path = pathlib.Path(directory) / f"run-{index}.txt"
                write_report(path, values)
                paths.append(str(path))
            with contextlib.redirect_stdout(io.StringIO()):
                status = MODULE.main(["--baseline", *paths[:3],
                                      "--candidate", *paths[3:],
                                      "--allow-link"])
        self.assertEqual(status, 0)

    def test_format_six_report_without_link_field_is_accepted(self):
        unlinked = report(frames=605)
        unlinked["link-lockstep-frames"] = "0"
        self.assertNotIn("link-lockstep-frames", report())

        result = MODULE.compare([report(), report(), report()],
                                [unlinked, unlinked, unlinked])

        self.assertTrue(result["passed"])

    def test_order_check_blocked_arms_warns(self):
        # A is a baseline run and B a candidate run, in capture order.
        cases = {
            "blocked (AAABBB)": (runs_at(2, 0, 1), runs_at(5, 3, 4)),
            "blocked (BBBAAA)": (runs_at(3, 4, 5), runs_at(0, 1, 2)),
            "blocked (ABBBAA)": (runs_at(0, 4, 5), runs_at(1, 2, 3)),
            "alternated (ABABAB)": (runs_at(0, 2, 4), runs_at(1, 3, 5)),
            "alternated (ABBAAB)": (runs_at(0, 3, 4), runs_at(1, 2, 5)),
            "alternated (ABABABA)": (runs_at(0, 2, 4, 6), runs_at(1, 3, 5)),
        }
        for order, arms in cases.items():
            with self.subTest(order):
                result = MODULE.compare(*arms, order_check=True)
                # Without --strict the order is only reported.
                self.assertTrue(result["passed"])
                self.assertEqual(result["order"], order)
                self.assertIn(f"\norder={order}\n",
                              MODULE.format_result(result) + "\n")

        unchecked = MODULE.compare(*cases["blocked (AAABBB)"])
        self.assertIsNone(unchecked["order"])
        self.assertNotIn("order=", MODULE.format_result(unchecked))
        status, output = run_files(*cases["blocked (AAABBB)"], "--order-check")
        self.assertEqual(status, 0)
        self.assertTrue(output.startswith("PASS\n"))
        self.assertIn("\norder=blocked (AAABBB)\n", output)

    def test_order_check_strict_fails(self):
        baseline, candidate = runs_at(0, 1, 2), runs_at(3, 4, 5)

        result = MODULE.compare(baseline, candidate, order_check=True,
                                strict=True)

        self.assertFalse(result["passed"])
        self.assertEqual(result["failures"],
                         ["run order is blocked (AAABBB)"])
        self.assertTrue(MODULE.compare(runs_at(0, 2, 4), runs_at(1, 3, 5),
                                       order_check=True, strict=True)["passed"])
        status, output = run_files(baseline, candidate, "--order-check",
                                   "--strict")
        self.assertEqual(status, 1)
        self.assertTrue(output.startswith("FAIL\n"))
        self.assertIn("\nreason=run order is blocked (AAABBB)\n", output)
        # Directory mode checks the order of every group.
        key = ("1", "BPRE", "0")
        outcome = MODULE.compare_groups({key: baseline}, {key: candidate},
                                        order_check=True, strict=True)
        self.assertFalse(outcome["passed"])
        self.assertIn("group=1/BPRE-r0 FAIL\n", MODULE.format_groups(outcome))
        # --strict alone is a usage error.
        with contextlib.redirect_stderr(io.StringIO()), \
                self.assertRaises(SystemExit) as error:
            run_files(baseline, candidate, "--strict")
        self.assertEqual(error.exception.code, 2)

    def test_order_check_unknown_without_times(self):
        lacking = runs_at(0, 2, 4)
        del lacking[1]["capture-local-time"]
        # A capture whose RTC read failed writes the key with no value.
        empty = runs_at(0, 2, 4)
        empty[1]["capture-local-time"] = ""
        for baseline in (lacking, empty, [report(), report(), report()]):
            with self.subTest(baseline=baseline):
                result = MODULE.compare(baseline, runs_at(1, 3, 5),
                                        order_check=True, strict=True)
                self.assertEqual(result["failures"], [
                    "run order is unknown (a run lacks capture-local-time)"])

        tied = MODULE.compare(runs_at(0, 2, 4), runs_at(1, 2, 5),
                              order_check=True)
        self.assertTrue(tied["passed"])
        self.assertEqual(tied["order"], "unknown (two runs have the same "
                         "capture-local-time)")

    def test_claim_holds_only_when_significant(self):
        def claim(text, baseline, candidate):
            return MODULE.compare(baseline, candidate,
                                  claim=MODULE.parse_claim(text))["claim"]

        fps = [report(frames=frames) for frames in (600, 601, 602)]
        # 62.5-62.7 fps divided by 1.03 is still above all of 60.0-60.2 fps.
        faster = [report(frames=frames) for frames in (625, 626, 627)]
        self.assertEqual(claim("throughput>=+3", fps, faster),
                         "throughput>=+3% holds (p=0.050)")
        # The median is up 3%, but the shifted runs overlap the baseline.
        mixed = [report(frames=frames) for frames in (612, 619, 625)]
        self.assertEqual(claim("throughput>=3%", fps, mixed),
                         "throughput>=+3% not demonstrated (p=0.650)")
        # 2% up: a higher candidate, but not by the 3% claimed.
        closer = [report(frames=frames) for frames in (612, 613, 614)]
        result = MODULE.compare(fps, closer,
                                claim=MODULE.parse_claim("throughput>=+3"))
        self.assertTrue(result["metrics"][0][4].startswith("candidate higher"))
        self.assertEqual(result["claim"],
                         "throughput>=+3% not demonstrated (p=1.000)")

        # <= declares the lower side: 16.0-16.1 ms divided by 0.97 is below
        # 16.9-17.1 ms, but 1% lower is not 3% lower.
        p95 = [report(frame_p95=value) for value in (16900, 17000, 17100)]
        lower = [report(frame_p95=value) for value in (16000, 16050, 16100)]
        slightly = [report(frame_p95=value) for value in (16700, 16800, 16850)]
        self.assertEqual(claim("frame-p95<=-3", p95, lower),
                         "frame-p95<=-3% holds (p=0.050)")
        self.assertEqual(claim("frame-p95<=-3", p95, slightly),
                         "frame-p95<=-3% not demonstrated (p=1.000)")

        # The claim never changes the exit status.
        status, output = run_files(fps, mixed, "--claim", "throughput>=+3")
        self.assertEqual(status, 0)
        self.assertIn("\nclaim=throughput>=+3% not demonstrated (p=", output)
        underruns = [report(frames=frames, underruns=1)
                     for frames in (625, 626, 627)]
        status, output = run_files(fps, underruns, "--claim", "throughput>=+3")
        self.assertEqual(status, 1)
        self.assertIn("\nclaim=throughput>=+3% holds (p=0.050)\n", output)
        # A metric the runs lack (no work fields, no fast-forward time)
        # leaves PASS, FAIL and the exit status as they are.
        normal = [report(frames=frames, fast_forward_time=0)
                  for frames in (625, 626, 627)]
        for name, text in (("work-p95", "work-p95<=-5"),
                           ("fast-forward", "fast-forward>=+5")):
            with self.subTest(text):
                status, output = run_files(fps, normal, "--claim", text)
                self.assertEqual(status, 0)
                self.assertIn(f"\nclaim={text}% not measured (not every "
                              f"run has {name})\n", output)

        for text in ("fps>=+3", "throughput>+3", "throughput>=-100",
                     "throughput>=3x", "throughput>=+3 "):
            with self.subTest(text), self.assertRaises(
                    MODULE.argparse.ArgumentTypeError):
                MODULE.parse_claim(text)
        self.assertEqual(MODULE.parse_claim("frame-p99<=-5"),
                         ("frame-p99", "<=", -5.0))
        with contextlib.redirect_stderr(io.StringIO()), \
                self.assertRaises(SystemExit) as error:
            run_files(fps, faster, "--claim", "fps>=+3")
        self.assertEqual(error.exception.code, 2)

    def test_legacy_v6_fixtures_same_pass_fail(self):
        passing = MODULE.compare(
            [report(), report(frame_p95=17100), report(frame_p95=16900)],
            [report(frames=605), report(frames=606), report(frames=604)],
        )
        self.assertTrue(passing["passed"])
        self.assertEqual(passing["failures"], [])
        # The lines printed before the per-run lines: the format 6 ones plus
        # frame-p99, frame-max and the skipped-gates note.
        self.assertEqual(
            [line for line in MODULE.format_result(passing).splitlines()
             if "-runs=" not in line],
            ["PASS",
             "throughput=60.50 fps (+0.83%)",
             "frame-p95=17000 us (+0.00%)",
             "frame-p99=18000 us (+0.00%)",
             "frame-max=20000 us (runs above 30000 us: 0)",
             "note=work-p95 and over-budget gates skipped: a run lacks the "
             "format 7 fields",
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
