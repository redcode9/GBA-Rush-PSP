#!/usr/bin/env python3
"""Compare two builds from their PSP performance reports.

Each build is run three times on the same game and settings; the medians of
the candidate runs must not regress against the baseline runs. The reports
are the key=value files written by source/src/perf_report.c.
"""

import argparse
import statistics
import sys


# Settings that every run must share, or the timings are not comparable.
MATCH_FIELDS = (
    "psp-model",
    "clock-mhz",
    "renderer",
    "scale",
    "filter",
    "frameskip-type",
    "frameskip-value",
    "sound-volume",
    "game-code",
    "rom-revision",
)

# Fields that older reports lack: checked only when the first baseline
# report has them, and then every run must carry the same value.
OPTIONAL_MATCH_FIELDS = (
    "gamepak-file-size",
    "gamepak-ram-buffer-size",
    "gamepak-ram-pages",
    "psp-fw-version",
    "vsync",
    "audio-sample-rate",
    "audio-samples",
    "fast-forward-game-override",
    "fast-forward-profile",
    "fast-forward-activation",
    "fast-forward-speed",
    "fast-forward-presentation",
    "fast-forward-audio",
)


def read_report(path):
    """Parse a report into a dict; only finished (stopped) sessions count."""
    values = {}
    with open(path, "r", encoding="utf-8") as report:
        for line_number, line in enumerate(report, 1):
            line = line.rstrip("\n")
            if not line:
                continue
            if "=" not in line:
                raise ValueError(f"{path}:{line_number}: invalid report line")
            key, value = line.split("=", 1)
            if not key or key in values:
                raise ValueError(f"{path}:{line_number}: invalid report key")
            values[key] = value

    if values.get("state") != "stopped":
        raise ValueError(f"{path}: benchmark is not stopped")
    return values


def number(report, key):
    try:
        return int(report[key])
    except (KeyError, ValueError) as error:
        raise ValueError(f"missing or invalid field: {key}") from error


def median_metric(reports, metric):
    return float(statistics.median(metric(report) for report in reports))


def throughput(report):
    duration = number(report, "duration-us")
    if duration <= 0:
        raise ValueError("duration-us must be positive")
    return number(report, "emulated-frames") * 1_000_000.0 / duration


def skip_ratio(report):
    frames = number(report, "emulated-frames")
    if frames <= 0:
        raise ValueError("emulated-frames must be positive")
    return number(report, "skipped-frames") / frames


def fast_forward_multiplier(report):
    """Fast-forward speed as a multiple of a real GBA.

    Each frame is 280896 cycles at 16777216 Hz, so emulated time in us is
    frames * 280896e6 / 16777216, divided here by the wall time spent.
    """
    duration = number(report, "fast-forward-time-us")
    if duration <= 0:
        raise ValueError("fast-forward-time-us must be positive")
    frames = number(report, "fast-forward-frames")
    return frames * 280_896_000_000.0 / (duration * 16_777_216.0)


def percent_change(baseline, candidate):
    if baseline == 0:
        return 0.0 if candidate == 0 else float("inf")
    return ((candidate / baseline) - 1.0) * 100.0


def validate_matching_runs(baseline, candidate):
    reference = baseline[0]
    for report in baseline[1:] + candidate:
        for field in MATCH_FIELDS:
            if report.get(field) != reference.get(field):
                raise ValueError(f"benchmark field does not match: {field}")
        for field in OPTIONAL_MATCH_FIELDS:
            if field in reference and report.get(field) != reference[field]:
                raise ValueError(f"benchmark field does not match: {field}")


def compare(baseline, candidate):
    """Compare run medians and list every regression past its threshold.

    Fails on throughput down more than 2%, frame-time p95 up more than 5%,
    more audio underruns, a skipped-frame ratio up more than 0.5 points, or
    fast-forward speed down more than 2% (when all runs measured it).
    """
    validate_matching_runs(baseline, candidate)

    baseline_throughput = median_metric(baseline, throughput)
    candidate_throughput = median_metric(candidate, throughput)
    baseline_frame_p95 = median_metric(
        baseline, lambda report: number(report, "frame-time-p95-us")
    )
    candidate_frame_p95 = median_metric(
        candidate, lambda report: number(report, "frame-time-p95-us")
    )
    baseline_ge_p95 = median_metric(
        baseline, lambda report: number(report, "ge-wait-p95-us")
    )
    candidate_ge_p95 = median_metric(
        candidate, lambda report: number(report, "ge-wait-p95-us")
    )
    baseline_underruns = median_metric(
        baseline, lambda report: number(report, "audio-underruns")
    )
    candidate_underruns = median_metric(
        candidate, lambda report: number(report, "audio-underruns")
    )
    baseline_skips = median_metric(baseline, skip_ratio)
    candidate_skips = median_metric(candidate, skip_ratio)
    baseline_fast_forward = None
    candidate_fast_forward = None
    if all("fast-forward-time-us" in report and
           number(report, "fast-forward-time-us") > 0 for report in
           baseline + candidate):
        baseline_fast_forward = median_metric(
            baseline, fast_forward_multiplier
        )
        candidate_fast_forward = median_metric(
            candidate, fast_forward_multiplier
        )

    throughput_change = percent_change(baseline_throughput, candidate_throughput)
    frame_p95_change = percent_change(baseline_frame_p95, candidate_frame_p95)
    ge_p95_change = percent_change(baseline_ge_p95, candidate_ge_p95)

    failures = []
    if throughput_change < -2.0:
        failures.append("throughput regression exceeds 2%")
    if frame_p95_change > 5.0:
        failures.append("frame-time p95 regression exceeds 5%")
    if candidate_underruns > baseline_underruns:
        failures.append("audio underruns increased")
    if candidate_skips > baseline_skips + 0.005:
        failures.append("skipped-frame ratio increased by more than 0.5 points")
    fast_forward_change = None
    if baseline_fast_forward is not None:
        fast_forward_change = percent_change(
            baseline_fast_forward, candidate_fast_forward
        )
        if fast_forward_change < -2.0:
            failures.append("fast-forward speed regression exceeds 2%")

    return {
        "passed": not failures,
        "failures": failures,
        "throughput": candidate_throughput,
        "throughput_change": throughput_change,
        "frame_p95": candidate_frame_p95,
        "frame_p95_change": frame_p95_change,
        "ge_p95": candidate_ge_p95,
        "ge_p95_change": ge_p95_change,
        "audio_underruns": candidate_underruns,
        "skip_ratio": candidate_skips,
        "fast_forward": candidate_fast_forward,
        "fast_forward_change": fast_forward_change,
    }


def format_result(result):
    lines = ["PASS" if result["passed"] else "FAIL"]
    lines.append(
        f"throughput={result['throughput']:.2f} fps "
        f"({result['throughput_change']:+.2f}%)"
    )
    lines.append(
        f"frame-p95={result['frame_p95']:.0f} us "
        f"({result['frame_p95_change']:+.2f}%)"
    )
    lines.append(
        f"ge-p95={result['ge_p95']:.0f} us "
        f"({result['ge_p95_change']:+.2f}%)"
    )
    lines.append(f"audio-underruns={result['audio_underruns']:.0f}")
    lines.append(f"skipped-frames={result['skip_ratio'] * 100.0:.2f}%")
    if result["fast_forward"] is not None:
        lines.append(
            f"fast-forward={result['fast_forward']:.2f}x "
            f"({result['fast_forward_change']:+.2f}%)"
        )
    lines.extend(f"reason={failure}" for failure in result["failures"])
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Compare three PSP benchmark reports per build."
    )
    parser.add_argument("--baseline", nargs=3, required=True, metavar="REPORT")
    parser.add_argument("--candidate", nargs=3, required=True, metavar="REPORT")
    arguments = parser.parse_args(argv)

    try:
        baseline = [read_report(path) for path in arguments.baseline]
        candidate = [read_report(path) for path in arguments.candidate]
        result = compare(baseline, candidate)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    print(format_result(result))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
