import importlib.util
import io
import json
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock


TOOLS = Path(__file__).parents[1] / "tools"
MODULE_PATH = TOOLS / "analyze_link_benchmark.py"
SPEC = importlib.util.spec_from_file_location("analyze_link_benchmark", MODULE_PATH)
analyzer = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
import sys
sys.path.insert(0, str(TOOLS))
SPEC.loader.exec_module(analyzer)


def summary(role: int, **changes: int) -> dict[str, int]:
    result = {
        "run_id": 7,
        "status": 4,
        "failure": 0,
        "role": role,
        "duration_ms": 600_000,
        "completed_words": 600_000,
        "words_per_second": 1000,
        "digest": 0x12345678,
        "rtt_p50_us": 1000,
        "rtt_p95_us": 2000,
        "rtt_p99_us": 3000,
        "burst_p99_us": 20_000,
        "burst_max_us": 20_000,
        "duplicates": 1,
        "replays": 1,
        "stale_generations": 0,
        "timeouts": 0,
        "retransmissions": 1,
        "pure_acks": 600_000,
        "queue_high_water": 2,
        "hot_wake_max_us": 900,
        "barrier_max_us": 0,
        "audio_underruns": 0,
        "full_recoveries": 0,
        "starvation_max_us": 20_000,
        "attempt": 1,
        "outstanding": 0,
        "queue_depth": 0,
        "checksum_valid": 1,
    }
    result.update(changes)
    return result


def snapshot_from(run: dict[str, int]) -> dict:
    records = {
        0: [run["run_id"], run["status"], run["failure"]],
        1: [run["run_id"], run["duration_ms"], run["completed_words"]],
        2: [run["run_id"], run["words_per_second"], run["digest"]],
        3: [run["run_id"], run["rtt_p50_us"], run["rtt_p95_us"]],
        4: [run["run_id"], run["rtt_p99_us"], run["burst_p99_us"]],
        6: [run["run_id"], run["duplicates"], run["replays"]],
        7: [run["run_id"], run["stale_generations"], run["timeouts"]],
        8: [run["run_id"], run["retransmissions"], run["pure_acks"]],
        9: [run["run_id"], run["queue_high_water"], run["hot_wake_max_us"]],
        10: [run["run_id"], run["barrier_max_us"], run["audio_underruns"]],
        11: [run["run_id"], run["full_recoveries"], run["starvation_max_us"]],
        12: [run["run_id"], run["role"], run["attempt"]],
        13: [run["run_id"], run["outstanding"], run["queue_depth"]],
    }
    values = [
        run["run_id"], run["status"], run["failure"], run["role"],
        run["duration_ms"], run["completed_words"], run["words_per_second"],
        run["digest"], run["rtt_p50_us"], run["rtt_p95_us"],
        run["rtt_p99_us"], run["burst_p99_us"], run["burst_max_us"],
        run["duplicates"], run["replays"], run["stale_generations"],
        run["timeouts"],
    ]
    records[5] = [run["run_id"], run["burst_max_us"], analyzer._checksum(values)]
    return {
        "records": [
            {"event": "link-benchmark", "flags": flag, "values": fields}
            for flag, fields in sorted(records.items())
        ]
    }


class LinkBenchmarkAnalyzerTests(unittest.TestCase):
    def test_accepts_exact_thresholds(self):
        result = analyzer.analyze_pair(summary(1), summary(2))
        self.assertTrue(result["passed"])

    def test_rejects_cross_peer_mismatch(self):
        result = analyzer.analyze_pair(summary(1), summary(2, digest=9))
        self.assertFalse(result["passed"])
        self.assertIn("deterministic digests differ", result["failures"])

    def test_rejects_recovery_queue_audio_and_starvation(self):
        result = analyzer.analyze_pair(
            summary(1),
            summary(
                2, full_recoveries=1, queue_depth=1,
                audio_underruns=1, starvation_max_us=50_001,
            ),
        )
        self.assertFalse(result["passed"])
        self.assertGreaterEqual(len(result["failures"]), 4)

    def test_extracts_and_checks_checksum(self):
        expected = summary(1)
        runs = analyzer.extract_runs(snapshot_from(expected))
        self.assertEqual(runs[7]["completed_words"], 600_000)
        self.assertEqual(runs[7]["checksum_valid"], 1)

    def test_start_record_is_not_a_terminal_summary(self):
        snapshot = {"records": [{"event": "link-benchmark", "flags": 0x80,
                                 "values": [7, 600000, 1000]}]}
        self.assertEqual(analyzer.extract_runs(snapshot), {})
        snapshot["records"] += snapshot_from(summary(1))["records"]
        self.assertTrue(analyzer.extract_runs(snapshot)[7]["checksum_valid"])

    def test_missing_run_has_machine_readable_failure(self):
        with mock.patch.object(sys, "argv", [str(MODULE_PATH), "a", "b", "--json"]):
            with mock.patch.object(analyzer, "analyze_files", side_effect=ValueError(
                "logs contain no common benchmark run ID"
            )):
                output = io.StringIO()
                with redirect_stdout(output):
                    self.assertEqual(analyzer.main(), 1)
                result = json.loads(output.getvalue())
                self.assertFalse(result["passed"])
                self.assertIn("no common benchmark", result["failures"][0])

    def test_run_ids_are_not_timestamps(self):
        paths = [mock.Mock(), mock.Mock()]
        snapshots = []
        for role in (1, 2):
            snapshot = snapshot_from(summary(role, run_id=100))
            snapshot["records"] += snapshot_from(summary(role, run_id=2))["records"]
            snapshots.append(snapshot)
        with mock.patch.object(analyzer.decode_runtime_log, "parse_snapshot",
                               side_effect=snapshots):
            with self.assertRaisesRegex(ValueError, "select --run-id"):
                analyzer.analyze_files(paths)
        with mock.patch.object(analyzer.decode_runtime_log, "parse_snapshot",
                               side_effect=snapshots):
            self.assertEqual(analyzer.analyze_files(paths, run_id=2)["run_id"], 2)

    def test_selected_run_is_not_hidden_by_an_older_partial_run(self):
        paths = [mock.Mock(), mock.Mock()]
        snapshots = []
        for role in (1, 2):
            snapshot = snapshot_from(summary(role, run_id=100))
            snapshot["records"] = snapshot["records"][-1:]
            snapshot["records"] += snapshot_from(summary(role, run_id=2))["records"]
            snapshots.append(snapshot)
        with mock.patch.object(analyzer.decode_runtime_log, "parse_snapshot",
                               side_effect=snapshots):
            self.assertTrue(analyzer.analyze_files(paths, run_id=2)["passed"])
        with mock.patch.object(analyzer.decode_runtime_log, "parse_snapshot",
                               side_effect=snapshots):
            with self.assertRaisesRegex(ValueError, "missing benchmark records"):
                analyzer.analyze_files(paths, run_id=100)

    def test_missing_record_is_rejected(self):
        snapshot = snapshot_from(summary(1))
        snapshot["records"].pop()
        with self.assertRaisesRegex(ValueError, "missing benchmark records"):
            analyzer.extract_runs(snapshot)


if __name__ == "__main__":
    unittest.main()
