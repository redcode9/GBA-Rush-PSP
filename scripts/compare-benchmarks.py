#!/usr/bin/env python3
"""Compare two builds from their PSP performance reports.

Each build is run at least three times on the same game and settings (the two
builds may have different run counts); the medians of the candidate runs must
not regress against the baseline runs. Each metric also gets the spread of its
runs and an exact one-sided permutation verdict. The reports are the key=value
files written by source/src/perf_report.c.
"""

import argparse
import itertools
import math
import random
import statistics
import sys


# Fewest reports per build; 3+3 runs is the smallest even split whose exact
# test can reach p = 0.05.
MIN_RUNS = 3

# A difference is claimed only when the exact one-sided p is at most this.
ALPHA = 0.05

# Above EXACT_PICKS ways of picking the candidate runs (more than 10+10
# runs), p is estimated from SAMPLED_PICKS random picks instead, so large
# run sets still finish in about a second.
EXACT_PICKS = 200_000
SAMPLED_PICKS = 20_000

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
    """Integer field of a report; a missing or bad value is an error."""
    try:
        return int(report[key])
    except (KeyError, ValueError) as error:
        raise ValueError(f"missing or invalid field: {key}") from error


def median_metric(reports, metric):
    """Median of metric(report) over the runs of one build."""
    return float(statistics.median(metric(report) for report in reports))


def throughput(report):
    """Emulated frames per second of wall time."""
    duration = number(report, "duration-us")
    if duration <= 0:
        raise ValueError("duration-us must be positive")
    return number(report, "emulated-frames") * 1_000_000.0 / duration


def skip_ratio(report):
    """Share of emulated frames that were never shown."""
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
    """Relative change in percent; infinite when the baseline is zero."""
    if baseline == 0:
        return 0.0 if candidate == 0 else float("inf")
    return ((candidate / baseline) - 1.0) * 100.0


def summary(values):
    """Median, minimum and maximum of the per-run values of one build."""
    return float(statistics.median(values)), min(values), max(values)


def permutation_p(baseline_values, candidate_values, higher):
    """One-sided permutation p of the candidate runs' sum: (p, exact).

    If both builds are the same, every way of picking the candidate's runs
    out of the pooled runs is equally likely; p is the share of those picks
    whose sum is at least as high (or as low, when higher is False) as the
    observed one. Up to EXACT_PICKS picks (10+10 runs) every pick is
    counted; above that, SAMPLED_PICKS seeded random picks estimate p and
    exact is False.
    """
    sign = 1.0 if higher else -1.0
    pooled = [sign * value for value in list(baseline_values) +
              list(candidate_values)]
    size = len(candidate_values)
    if min(pooled) == max(pooled):
        return 1.0, True
    observed = sum(pooled[len(baseline_values):])
    # Float sums that are equal up to rounding are ties, and ties count.
    floor = observed - 1e-9 * max(1.0, abs(observed))
    picks = math.comb(len(pooled), size)
    if picks <= EXACT_PICKS:
        extreme = sum(1 for pick in itertools.combinations(pooled, size)
                      if sum(pick) >= floor)
        return extreme / picks, True
    # Seeded, so the same reports always print the same verdict; the
    # observed pick counts once more, so the estimate is never 0.
    rng = random.Random(0)
    extreme = sum(1 for _ in range(SAMPLED_PICKS)
                  if sum(rng.sample(pooled, size)) >= floor)
    return (extreme + 1) / (SAMPLED_PICKS + 1), False


def runs_needed(baseline_values, candidate_values, difference):
    """Runs per build that would likely decide the observed difference.

    Normal approximation of a one-sided test at ALPHA with 80% power, with
    Cohen's d from difference and the pooled spread; None when the
    difference is 0 or the runs do not differ at all.
    """
    difference = abs(difference)
    spread = math.sqrt((statistics.variance(baseline_values) +
                        statistics.variance(candidate_values)) / 2.0)
    if difference == 0 or spread == 0:
        return None
    z = statistics.NormalDist().inv_cdf
    needed = math.ceil(2.0 * ((z(1.0 - ALPHA) + z(0.80)) * spread /
                              difference) ** 2)
    return max(needed, len(baseline_values) + 1, len(candidate_values) + 1)


def verdict(baseline_values, candidate_values):
    """Exact verdict on the per-run values: (p, text, runs needed).

    The side tested is the one the medians point to, or the means when the
    medians tie. A difference is claimed only at p <= ALPHA; otherwise the
    text says the result is undecided and about how many runs per build
    would decide it.
    """
    difference = (statistics.median(candidate_values) -
                  statistics.median(baseline_values))
    if difference == 0:
        # Sparse metrics such as underruns often tie on the median; the
        # summed test statistic follows the means.
        difference = (statistics.mean(candidate_values) -
                      statistics.mean(baseline_values))
    higher = difference >= 0
    p, exact = permutation_p(baseline_values, candidate_values, higher)
    # "p~" marks a p estimated from sampled picks.
    runs = (f"p{'=' if exact else '~'}{p:.3f}, "
            f"{len(baseline_values)}+{len(candidate_values)} runs")
    if difference != 0 and p <= ALPHA:
        side = "higher" if higher else "lower"
        return p, f"candidate {side} ({runs})", None
    needed = runs_needed(baseline_values, candidate_values, difference)
    if needed is not None:
        return p, (f"undecided ({runs}), about {needed} runs per build "
                   "needed"), needed
    if len(set(baseline_values) | set(candidate_values)) == 1:
        return p, f"undecided ({runs}), no difference observed", None
    return p, f"undecided ({runs}), medians and means equal", None


# The per-run metrics whose spread and verdict the report prints, with
# their display format; fast-forward only when every run measured it.
RUN_METRICS = (
    ("throughput", throughput, "{:.2f}"),
    ("frame-p95", lambda report: number(report, "frame-time-p95-us"),
     "{:.0f}"),
    ("ge-p95", lambda report: number(report, "ge-wait-p95-us"), "{:.0f}"),
    ("audio-underruns", lambda report: number(report, "audio-underruns"),
     "{:.0f}"),
    ("skipped-frames", lambda report: skip_ratio(report) * 100.0,
     "{:.2f}%"),
    ("fast-forward", fast_forward_multiplier, "{:.2f}x"),
)


def validate_matching_runs(baseline, candidate):
    """All runs must share the settings in MATCH_FIELDS, and the
    OPTIONAL_MATCH_FIELDS the first run reports, or the numbers are not
    comparable."""
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
    if min(len(baseline), len(candidate)) < MIN_RUNS:
        raise ValueError(f"at least {MIN_RUNS} reports per build are needed")
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

    metrics = []
    for name, metric, _ in RUN_METRICS:
        if name == "fast-forward" and baseline_fast_forward is None:
            continue
        baseline_values = [metric(report) for report in baseline]
        candidate_values = [metric(report) for report in candidate]
        p, text, needed = verdict(baseline_values, candidate_values)
        metrics.append((name, summary(baseline_values),
                        summary(candidate_values), p, text, needed))

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
        "metrics": metrics,
    }


def format_result(result):
    """The PASS/FAIL report main() prints, one key=value per line."""
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
    formats = {name: display for name, _, display in RUN_METRICS}
    for name, baseline_runs, candidate_runs, _, text, _ in result["metrics"]:
        show = formats[name].format
        baseline_text = "{} [{}..{}]".format(*map(show, baseline_runs))
        candidate_text = "{} [{}..{}]".format(*map(show, candidate_runs))
        lines.append(f"{name}-runs=baseline {baseline_text}, "
                     f"candidate {candidate_text}; {text}")
    lines.extend(f"reason={failure}" for failure in result["failures"])
    return "\n".join(lines)


def main(argv=None):
    """Exit status: 0 on PASS, 1 on a regression, 2 on unreadable input or
    fewer than MIN_RUNS reports per build."""
    parser = argparse.ArgumentParser(
        description=f"Compare PSP benchmark reports, at least {MIN_RUNS} "
        "per build (the counts may differ), on medians and on an exact "
        "one-sided permutation test."
    )
    parser.add_argument("--baseline", nargs="+", required=True, metavar="REPORT")
    parser.add_argument("--candidate", nargs="+", required=True, metavar="REPORT")
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
