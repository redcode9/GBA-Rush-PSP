#!/usr/bin/env python3
"""Validate a paired real-PSP link benchmark from runtime logs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import decode_runtime_log


REQUIRED_FLAGS = set(range(14))
MINIMUM_WORDS_PER_SECOND = 600
MAXIMUM_BURST_P99_US = 20_000
MINIMUM_DURATION_MS = 600_000
MAXIMUM_STARVATION_US = 50_000

STATUS_NAMES = {
    0: "idle",
    1: "wait-ack",
    2: "running",
    3: "stopping",
    4: "pass",
    5: "fail",
    6: "abort",
}
ROLE_NAMES = {1: "host", 2: "joiner"}


def _checksum(values: list[int]) -> int:
    result = 2166136261
    for item in values:
        value = item & 0xFFFFFFFF
        for _ in range(4):
            result ^= value & 0xFF
            result = (result * 16777619) & 0xFFFFFFFF
            value >>= 8
    return result


def _group_terminal_records(
    snapshot: dict[str, Any],
) -> dict[int, dict[int, list[int]]]:
    grouped: dict[int, dict[int, list[int]]] = {}
    for record in snapshot.get("records", []):
        if (record.get("event") != "link-benchmark"
                or record.get("flags") not in REQUIRED_FLAGS):
            continue
        values = record.get("values", [])
        if len(values) != 3:
            continue
        grouped.setdefault(values[0], {})[record["flags"]] = values
    return grouped


def extract_runs(
    snapshot: dict[str, Any], selected_run_id: int | None = None,
) -> dict[int, dict[str, int]]:
    runs: dict[int, dict[str, int]] = {}
    for run_id, fields in _group_terminal_records(snapshot).items():
        if selected_run_id is not None and run_id != selected_run_id:
            continue
        missing = REQUIRED_FLAGS - set(fields)
        if missing:
            raise ValueError(
                f"run {run_id}: missing benchmark records {sorted(missing)}"
            )
        run = {
            "run_id": run_id,
            "status": fields[0][1],
            "failure": fields[0][2],
            "duration_ms": fields[1][1],
            "completed_words": fields[1][2],
            "words_per_second": fields[2][1],
            "digest": fields[2][2],
            "rtt_p50_us": fields[3][1],
            "rtt_p95_us": fields[3][2],
            "rtt_p99_us": fields[4][1],
            "burst_p99_us": fields[4][2],
            "burst_max_us": fields[5][1],
            "checksum": fields[5][2],
            "duplicates": fields[6][1],
            "replays": fields[6][2],
            "stale_generations": fields[7][1],
            "timeouts": fields[7][2],
            "retransmissions": fields[8][1],
            "pure_acks": fields[8][2],
            "queue_high_water": fields[9][1],
            "hot_wake_max_us": fields[9][2],
            "barrier_max_us": fields[10][1],
            "audio_underruns": fields[10][2],
            "full_recoveries": fields[11][1],
            "starvation_max_us": fields[11][2],
            "role": fields[12][1],
            "attempt": fields[12][2],
            "outstanding": fields[13][1],
            "queue_depth": fields[13][2],
        }
        checksum_values = [
            run["run_id"], run["status"], run["failure"], run["role"],
            run["duration_ms"], run["completed_words"],
            run["words_per_second"], run["digest"], run["rtt_p50_us"],
            run["rtt_p95_us"], run["rtt_p99_us"], run["burst_p99_us"],
            run["burst_max_us"], run["duplicates"], run["replays"],
            run["stale_generations"], run["timeouts"],
        ]
        run["checksum_valid"] = int(_checksum(checksum_values) == run["checksum"])
        runs[run_id] = run
    return runs


def analyze_pair(
    first: dict[str, int],
    second: dict[str, int],
    minimum_duration_ms: int = MINIMUM_DURATION_MS,
) -> dict[str, Any]:
    failures: list[str] = []
    peers = [first, second]

    if first["run_id"] != second["run_id"]:
        failures.append("run IDs differ")
    if {first["role"], second["role"]} != {1, 2}:
        failures.append("one host and one joiner are required")
    if first["completed_words"] != second["completed_words"]:
        failures.append("completed word counts differ")
    if first["digest"] != second["digest"]:
        failures.append("deterministic digests differ")

    for peer in peers:
        label = ROLE_NAMES.get(peer["role"], f"role-{peer['role']}")
        if not peer["checksum_valid"]:
            failures.append(f"{label}: invalid summary checksum")
        if peer["status"] != 4 or peer["failure"] != 0:
            failures.append(
                f"{label}: terminal status is "
                f"{STATUS_NAMES.get(peer['status'], peer['status'])}"
            )
        if peer["duration_ms"] < minimum_duration_ms:
            failures.append(f"{label}: duration below target")
        if peer["words_per_second"] < MINIMUM_WORDS_PER_SECOND:
            failures.append(f"{label}: throughput below 600 words/s")
        if peer["burst_p99_us"] > MAXIMUM_BURST_P99_US:
            failures.append(f"{label}: p99 nine-word burst exceeds 20 ms")
        if peer["full_recoveries"] != 0:
            failures.append(f"{label}: full recovery observed")
        if peer["outstanding"] != 0 or peer["queue_depth"] != 0:
            failures.append(f"{label}: persistent queue growth")
        if peer["audio_underruns"] != 0:
            failures.append(f"{label}: audio underrun observed")
        if peer["starvation_max_us"] > MAXIMUM_STARVATION_US:
            failures.append(f"{label}: emulation/main-thread starvation")
        if peer["timeouts"] != 0 or peer["stale_generations"] != 0:
            failures.append(f"{label}: stream timeout or stale generation")

    return {
        "passed": not failures,
        "run_id": first["run_id"] if first["run_id"] == second["run_id"] else None,
        "failures": failures,
        "peers": peers,
    }


def analyze_files(paths: list[Path], run_id: int | None = None) -> dict[str, Any]:
    if len(paths) != 2:
        raise ValueError("exactly two peer logs are required")
    decoded = [
        decode_runtime_log.parse_snapshot(path.read_bytes(), str(path))
        for path in paths
    ]
    groups = [_group_terminal_records(snapshot) for snapshot in decoded]
    common = set(groups[0]) & set(groups[1])
    if not common:
        raise ValueError("logs contain no common benchmark run ID")
    if run_id is None:
        if len(common) != 1:
            raise ValueError("multiple common benchmark runs; select --run-id")
        run_id = next(iter(common))
    if run_id not in common:
        raise ValueError(f"run {run_id} is not present in both logs")
    peers = [extract_runs(snapshot, run_id)[run_id] for snapshot in decoded]
    return analyze_pair(peers[0], peers[1])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("logs", nargs=2, type=Path)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--run-id", type=lambda value: int(value, 0))
    arguments = parser.parse_args()
    try:
        result = analyze_files(arguments.logs, arguments.run_id)
    except (OSError, ValueError) as error:
        result = {"passed": False, "run_id": arguments.run_id,
                  "failures": [str(error)], "peers": []}
    if arguments.json:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(f"benchmark run {result['run_id']}: "
              f"{'PASS' if result['passed'] else 'FAIL'}")
        for failure in result["failures"]:
            print(f"- {failure}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
