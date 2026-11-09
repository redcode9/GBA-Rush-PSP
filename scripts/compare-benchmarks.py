#!/usr/bin/env python3
"""Compare two builds from their PSP performance reports.

Each build is run at least three times on the same game and settings (the two
builds may have different run counts); the medians of the candidate runs must
not regress against the baseline runs. Each metric also gets the spread of its
runs and an exact one-sided permutation verdict. The reports are the key=value
files written by source/src/perf_report.c.

With --baseline-dir and --candidate-dir, every report under the directories
given on each side is grouped by PSP model, game and ROM revision, and every
baseline group is compared with the candidate group of the same title and
model; a baseline group without candidate reports fails the check.

--order-check prints whether the runs were captured alternating between the
builds (from each report's capture-local-time) and --claim whether a claim
written before the runs, such as 'throughput>=+3', holds significantly.
"""

import argparse
import fractions
import itertools
import math
import pathlib
import random
import re
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

# Reports of formats 6 and 7 compare with each other; build-id (format 7)
# is deliberately not a match field, since comparing two builds is the
# point. Reports written before 1.4.3 carry invalid 64-bit counters.
FIRST_VALID_VERSION = (1, 4, 3)

# Why a run with link-lockstep-frames above 0 is rejected: a lockstep link
# forces auto frameskip and runs the emulator thread at priority 0x77.
LINK_REASON = ("captured during a lockstep link: forced frameskip and "
               "priority 0x77")


def read_report(path):
    """Parse a report into a dict; only finished (stopped) sessions count."""
    values = {}
    try:
        with open(path, "r", encoding="utf-8") as report:
            lines = report.read().split("\n")
    except UnicodeDecodeError:
        raise ValueError(f"{path}: not a UTF-8 report") from None
    for line_number, line in enumerate(lines, 1):
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
    # Synthetic reports leave version out; those stay accepted.
    if "version" in values:
        match = re.match(r"(\d+)\.(\d+)\.(\d+)", values["version"])
        if match is None:
            raise ValueError(f"{path}: invalid version: {values['version']}")
        if tuple(map(int, match.groups())) < FIRST_VALID_VERSION:
            raise ValueError(
                f"{path}: 64-bit counters are not valid before 1.4.3 "
                f"(version {values['version']})")
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


def rise_exceeds(baseline, candidate, percent):
    """Whether candidate is more than percent above baseline.

    Exact on the integer and half-integer medians of the reports, where
    percent_change() puts a rise of exactly 5% at 5.000000000000004.
    """
    return candidate * 100 > baseline * (100 + percent)


def summary(values):
    """Median, minimum and maximum of the per-run values of one build."""
    return float(statistics.median(values)), min(values), max(values)


def rounding_slack(scale):
    """Gap below which two floats of about scale are equal up to rounding."""
    return 1e-9 * max(1.0, abs(scale))


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
    floor = observed - rounding_slack(observed)
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
    # Per-run values are rounded floats: a gap within rounding is a tie.
    slack = rounding_slack(max(map(abs, [*baseline_values,
                                         *candidate_values])))
    difference = (statistics.median(candidate_values) -
                  statistics.median(baseline_values))
    if abs(difference) <= slack:
        # Sparse metrics such as underruns often tie on the median; the
        # summed test statistic follows the means.
        difference = (statistics.mean(candidate_values) -
                      statistics.mean(baseline_values))
        if abs(difference) <= slack:
            difference = 0
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


def run_order(baseline, candidate):
    """The capture order of the runs, as order= prints it.

    A is a baseline run and B a candidate run, sorted by capture-local-time
    (zero-padded, so text order is time order): blocked when either build
    has 3 or more runs in a row (AAABBB), alternated otherwise (ABAB, ABBA),
    and unknown when a run lacks the time or two runs share it.
    """
    times = [report.get("capture-local-time") for report in
             baseline + candidate]
    if not all(times):
        return "unknown (a run lacks capture-local-time)"
    if len(set(times)) < len(times):
        return "unknown (two runs have the same capture-local-time)"
    labels = "A" * len(baseline) + "B" * len(candidate)
    sequence = "".join(label for _, label in sorted(zip(times, labels)))
    if "AAA" in sequence or "BBB" in sequence:
        return f"blocked ({sequence})"
    return f"alternated ({sequence})"


# --claim METRIC>=PERCENT or METRIC<=PERCENT, such as throughput>=+3.
CLAIM = re.compile(r"([a-z0-9-]+)(>=|<=)([+-]?\d+(?:\.\d+)?)%?")


def parse_claim(text):
    """A --claim argument as (metric, operator, percent)."""
    names = [name for name, _, _ in RUN_METRICS]
    match = CLAIM.fullmatch(text)
    if match is None or match[1] not in names or float(match[3]) <= -100:
        raise argparse.ArgumentTypeError(
            f"invalid claim {text!r}: give METRIC>=PERCENT or "
            f"METRIC<=PERCENT, PERCENT above -100, METRIC one of "
            f"{', '.join(names)}")
    return match[1], match[2], float(match[3])


def claim_verdict(baseline, candidate, claim):
    """Whether a claim written before the runs holds, as claim= prints it.

    The candidate runs are divided by 1 + percent / 100, and the exact
    one-sided test on the side the claim declares (higher for >=, lower
    for <=) must then give p <= ALPHA.
    """
    name, operator, percent = claim
    metric = next(metric for metric_name, metric, _ in RUN_METRICS
                  if metric_name == name)
    factor = 1.0 + percent / 100.0
    p, exact = permutation_p([metric(report) for report in baseline],
                             [metric(report) / factor for report in candidate],
                             operator == ">=")
    state = "holds" if p <= ALPHA else "not demonstrated"
    return (f"{name}{operator}{percent:+g}% {state} "
            f"(p{'=' if exact else '~'}{p:.3f})")


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


def validate_link_runs(baseline, candidate, allow_link):
    """Reject runs captured during a lockstep link (LINK_REASON), unless
    allow_link and every run of both builds was; reports without
    link-lockstep-frames (format 6) count as captured without a link."""
    linked = ["link-lockstep-frames" in report and
              number(report, "link-lockstep-frames") > 0
              for report in baseline + candidate]
    if any(linked) and not (allow_link and all(linked)):
        if allow_link:
            raise ValueError(f"{LINK_REASON} in only some runs")
        raise ValueError(LINK_REASON)


def over_budget_per_minute(report):
    """Frames over 1.5x the frame budget per minute of wall time, exact, so
    a rise of exactly 20% passes whatever the run's duration."""
    # compare() runs throughput() first, which rejects a duration <= 0.
    return fractions.Fraction(
        number(report, "frames-over-1-5x-budget") * 60_000_000,
        number(report, "duration-us"))


# The format 7 fields of the work-time and over-budget gates. Early format 7
# reports lack them, so their presence decides, not the format number.
WORK_FIELDS = ("work-time-p95-us", "frames-over-1-5x-budget")


def compare(baseline, candidate, allow_link=False, order_check=False,
            strict=False, claim=None):
    """Compare run medians and list every regression past its threshold.

    Fails on throughput down more than 2%, frame-time p95 or p99 up more
    than 5%, a frame-time max above 1.5x the baseline median in at least
    2 candidate runs (one spike passes), more audio underruns, a
    skipped-frame ratio up more than 0.5 points, or fast-forward speed
    down more than 2% (when all runs measured it). When every run has the
    WORK_FIELDS, it also fails on work-time p95 up more than 5% or on
    frames over 1.5x budget per minute up more than max(1, 20%).
    order_check adds the run_order(), which with strict fails unless it is
    alternated; claim (from parse_claim) adds its claim_verdict() and never
    fails.
    """
    if min(len(baseline), len(candidate)) < MIN_RUNS:
        raise ValueError(f"at least {MIN_RUNS} reports per build are needed")
    validate_matching_runs(baseline, candidate)
    validate_link_runs(baseline, candidate, allow_link)

    baseline_throughput = median_metric(baseline, throughput)
    candidate_throughput = median_metric(candidate, throughput)
    baseline_frame_p95 = median_metric(
        baseline, lambda report: number(report, "frame-time-p95-us")
    )
    candidate_frame_p95 = median_metric(
        candidate, lambda report: number(report, "frame-time-p95-us")
    )
    baseline_frame_p99 = median_metric(
        baseline, lambda report: number(report, "frame-time-p99-us")
    )
    candidate_frame_p99 = median_metric(
        candidate, lambda report: number(report, "frame-time-p99-us")
    )
    frame_max_limit = 1.5 * median_metric(
        baseline, lambda report: number(report, "frame-time-max-us")
    )
    candidate_frame_max = median_metric(
        candidate, lambda report: number(report, "frame-time-max-us")
    )
    frame_max_spikes = sum(number(report, "frame-time-max-us") >
                           frame_max_limit for report in candidate)
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
    if rise_exceeds(baseline_frame_p95, candidate_frame_p95, 5):
        failures.append("frame-time p95 regression exceeds 5%")
    if rise_exceeds(baseline_frame_p99, candidate_frame_p99, 5):
        failures.append("frame-time p99 regression exceeds 5%")
    if frame_max_spikes >= 2:
        failures.append("frame-time max above 1.5x the baseline in "
                        f"{frame_max_spikes} runs")
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
    candidate_work_p95 = work_p95_change = None
    baseline_over_budget = candidate_over_budget = None
    if all(field in report for field in WORK_FIELDS
           for report in baseline + candidate):
        baseline_work_p95 = median_metric(
            baseline, lambda report: number(report, "work-time-p95-us")
        )
        candidate_work_p95 = median_metric(
            candidate, lambda report: number(report, "work-time-p95-us")
        )
        work_p95_change = percent_change(baseline_work_p95,
                                         candidate_work_p95)
        if rise_exceeds(baseline_work_p95, candidate_work_p95, 5):
            failures.append("work-time p95 regression exceeds 5%")
        baseline_over_budget = statistics.median(
            map(over_budget_per_minute, baseline))
        candidate_over_budget = statistics.median(
            map(over_budget_per_minute, candidate))
        if candidate_over_budget > baseline_over_budget + max(
                1, baseline_over_budget / 5):
            failures.append("frames over 1.5x budget per minute increased "
                            "by more than max(1, 20%)")
        baseline_over_budget = float(baseline_over_budget)
        candidate_over_budget = float(candidate_over_budget)
    order = run_order(baseline, candidate) if order_check else None
    if strict and order is not None and not order.startswith("alternated"):
        failures.append(f"run order is {order}")

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
        "frame_p99": candidate_frame_p99,
        "frame_p99_change": percent_change(baseline_frame_p99,
                                           candidate_frame_p99),
        "frame_max": candidate_frame_max,
        "frame_max_limit": frame_max_limit,
        "frame_max_spikes": frame_max_spikes,
        "work_p95": candidate_work_p95,
        "work_p95_change": work_p95_change,
        "over_budget": candidate_over_budget,
        "over_budget_baseline": baseline_over_budget,
        "ge_p95": candidate_ge_p95,
        "ge_p95_change": ge_p95_change,
        "audio_underruns": candidate_underruns,
        "skip_ratio": candidate_skips,
        "fast_forward": candidate_fast_forward,
        "fast_forward_change": fast_forward_change,
        "metrics": metrics,
        "order": order,
        "claim": (claim_verdict(baseline, candidate, claim)
                  if claim is not None else None),
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
        f"frame-p99={result['frame_p99']:.0f} us "
        f"({result['frame_p99_change']:+.2f}%)"
    )
    lines.append(
        f"frame-max={result['frame_max']:.0f} us "
        # Floored, not rounded: a whole-us max is above the limit exactly
        # when it is above the limit's floor.
        f"(runs above {math.floor(result['frame_max_limit'])} us: "
        f"{result['frame_max_spikes']})"
    )
    if result["work_p95"] is None:
        lines.append("note=work-p95 and over-budget gates skipped: a run "
                     "lacks the format 7 fields")
    else:
        lines.append(
            f"work-p95={result['work_p95']:.0f} us "
            f"({result['work_p95_change']:+.2f}%)"
        )
        lines.append(
            f"over-budget={result['over_budget']:.1f}/min "
            f"(baseline {result['over_budget_baseline']:.1f}/min)"
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
    lines.extend(f"{key}={result[key]}" for key in ("order", "claim")
                 if result[key] is not None)
    lines.extend(f"reason={failure}" for failure in result["failures"])
    return "\n".join(lines)


# The report fields that make a group (title x model) in directory mode.
GROUP_FIELDS = ("psp-model", "game-code", "rom-revision")


def group_name(key):
    """A group's name as the output prints it, such as 0/BPRE-r0."""
    return "{}/{}-r{}".format(*key)


def collect_groups(*directories):
    """The reports anywhere under the directories, keyed by GROUP_FIELDS.

    Every file but the .md notes and the -core.txt profiles of a
    DIAGNOSTIC_CORE_PROFILE build must be a stopped .txt report, every
    directory must hold at least one, and the runs of one group must all
    come from the same version.
    """
    groups = {}
    seen = set()
    for directory in map(pathlib.Path, directories):
        count = 0
        for path in sorted(directory.rglob("*")):
            if not path.is_file() or path.name.endswith((".md", "-core.txt")):
                continue
            name = path.as_posix()
            if path.suffix != ".txt":
                raise ValueError(f"{name}: not a .txt report")
            count += 1
            # Overlapping directories must not count a run twice.
            if path.resolve() in seen:
                continue
            seen.add(path.resolve())
            report = read_report(name)
            for field in GROUP_FIELDS:
                if field not in report:
                    raise ValueError(f"{name}: missing field: {field}")
            groups.setdefault(tuple(report[field] for field in GROUP_FIELDS),
                              []).append(report)
        # A mistyped directory must not drop its groups from the check.
        if not count:
            raise ValueError(f"no reports in {directory.as_posix()}")
    for key, reports in groups.items():
        # A whole model directory of the archive would mix releases: give
        # its <game-code>-r<revision>/<version> directories instead.
        if len({report.get("version") for report in reports}) > 1:
            raise ValueError(f"group {group_name(key)} mixes versions")
    return groups


def compare_groups(baseline_groups, candidate_groups, **options):
    """Compare every baseline group with the candidate group of its key.

    Every group is compared before the verdict, so the output lists them
    all. A baseline group the candidate lacks is MISSING (result None) and
    fails; candidate groups the baseline lacks are only listed. options
    are those of compare(), applied to every group.
    """
    groups = []
    for key in sorted(baseline_groups):
        result = None
        if key in candidate_groups:
            try:
                result = compare(baseline_groups[key], candidate_groups[key],
                                 **options)
            except ValueError as error:
                raise ValueError(f"group {group_name(key)}: {error}") from None
        groups.append((group_name(key), result))
    return {
        "passed": all(result is not None and result["passed"]
                      for _, result in groups),
        "groups": groups,
        "extra": [group_name(key) for key in sorted(candidate_groups)
                  if key not in baseline_groups],
    }


def format_groups(outcome):
    """The per-group blocks and the overall line main() prints."""
    blocks = []
    for name, result in outcome["groups"]:
        if result is None:
            blocks.append(f"group={name} MISSING\n"
                          "reason=no candidate reports for this group")
        else:
            blocks.append(f"group={name} {format_result(result)}")
    blocks.extend(f"group={name} not in baseline"
                  for name in outcome["extra"])
    missing = sum(result is None for _, result in outcome["groups"])
    failed = sum(result is not None and not result["passed"]
                 for _, result in outcome["groups"])
    blocks.append(f"{'PASS' if outcome['passed'] else 'FAIL'} "
                  f"groups={len(outcome['groups'])} failed={failed} "
                  f"missing={missing}")
    return "\n\n".join(blocks)


def main(argv=None):
    """Exit status: 0 on PASS, 1 on a regression, a missing group or, with
    --strict, a run order not shown to alternate, 2 on unreadable input or
    fewer than MIN_RUNS reports per build."""
    parser = argparse.ArgumentParser(
        description=f"Compare PSP benchmark reports, at least {MIN_RUNS} "
        "per build (the counts may differ), on medians, on frame-time max "
        "spikes repeated in 2 or more runs and on an exact one-sided "
        "permutation test. The work-time p95 and over-budget-per-minute "
        "gates need the format 7 fields in every run."
    )
    parser.add_argument("--baseline", nargs="+", metavar="REPORT")
    parser.add_argument("--candidate", nargs="+", metavar="REPORT")
    parser.add_argument("--baseline-dir", nargs="+", metavar="DIR",
                        help="compare every title x model group of the "
                        "reports under the DIRs (with --candidate-dir), "
                        "such as benchmarks/psp-1000/*/1.4.3 for one "
                        "release of the archive")
    parser.add_argument("--candidate-dir", nargs="+", metavar="DIR")
    parser.add_argument("--allow-link", action="store_true",
                        help="accept runs captured during a lockstep link "
                        "when every run of both builds was")
    parser.add_argument("--order-check", action="store_true",
                        help="sort the runs of both builds (A baseline, B "
                        "candidate) by capture-local-time and print "
                        "order=blocked when one build has 3 or more runs "
                        "in a row (AAABBB), order=alternated for ABAB or "
                        "ABBA, order=unknown when a run lacks the time or "
                        "two runs share it; per group in directory mode")
    parser.add_argument("--strict", action="store_true",
                        help="with --order-check, FAIL unless the order is "
                        "alternated")
    parser.add_argument("--claim", type=parse_claim, metavar="CLAIM",
                        help="a claim written before the runs, such as "
                        "'throughput>=+3' (candidate at least 3%% higher) "
                        "or 'frame-p95<=-2', on a metric of the -runs= "
                        "lines; it holds when the candidate runs divided by "
                        "1 + PERCENT/100 are higher (>=) or lower (<=) at "
                        f"p <= {ALPHA}, otherwise it is not demonstrated. "
                        "The claim never changes the exit status")
    arguments = parser.parse_args(argv)
    files = (arguments.baseline, arguments.candidate)
    directories = (arguments.baseline_dir, arguments.candidate_dir)
    if not (all(files) and not any(directories) or
            all(directories) and not any(files)):
        parser.error("give --baseline and --candidate, or --baseline-dir "
                     "and --candidate-dir")
    if arguments.strict and not arguments.order_check:
        parser.error("--strict needs --order-check")
    options = {"allow_link": arguments.allow_link,
               "order_check": arguments.order_check,
               "strict": arguments.strict, "claim": arguments.claim}

    try:
        if all(directories):
            outcome = compare_groups(collect_groups(*arguments.baseline_dir),
                                     collect_groups(*arguments.candidate_dir),
                                     **options)
            print(format_groups(outcome))
            return 0 if outcome["passed"] else 1
        baseline = [read_report(path) for path in arguments.baseline]
        candidate = [read_report(path) for path in arguments.candidate]
        result = compare(baseline, candidate, **options)
    except (OSError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2

    print(format_result(result))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
