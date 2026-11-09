# Performance

How GBA Rush PSP speed is measured, and the baselines measured so far.

## Comparing two builds

On the PSP, use Diagnostics > Start measurement, play a repeatable section,
stop and save the report. Capture at least three reports per build with the
same ROM, save or save state, renderer, clock and settings, then compare:

```sh
python3 scripts/compare-benchmarks.py \
  --baseline old-1.txt old-2.txt old-3.txt \
  --candidate new-1.txt new-2.txt new-3.txt
```

[CONTRIBUTING.md](../CONTRIBUTING.md#testing-changes) explains the PASS and
FAIL thresholds and the verdict printed for each metric. Never claim a
difference that the verdict does not state.

## Measurement protocol

A speed claim needs runs captured this way:

1. Write the claim down before the first run, in the form `--claim` takes:
   for example `throughput>=+3` (the candidate at least 3% faster) or
   `frame-p95<=-2`.
2. Set the CPU clock to 333 MHz and keep the link off. Use the same BIOS for
   both builds (`gba_bios.bin` or the built-in one) and write down which;
   the reports do not record it yet. Write down the battery level, or that
   the PSP ran on its charger.
3. Start every run from the same save state slot.
4. Make one warm-up run first and discard its report.
5. Alternate the builds (ABAB or ABBA), never all the runs of one build and
   then the other's. Each report records the PSP's local time when its
   capture started as `capture-local-time`; do not change the PSP's clock
   during a session, since that time only orders the runs.

Then compare with the order check and the claim:

```sh
python3 scripts/compare-benchmarks.py --order-check --strict \
  --claim 'throughput>=+3' \
  --baseline old-1.txt old-2.txt old-3.txt \
  --candidate new-1.txt new-2.txt new-3.txt
```

`order=` reports `alternated`, `blocked` (one build has three or more runs
in a row) or `unknown` (a report lacks `capture-local-time` or two share
it); with `--strict` anything but `alternated` fails. `claim=` says
`holds` only when the candidate runs, divided by 1 + percent/100, still
differ on the claimed side at p <= 0.05; otherwise it says
`not demonstrated`. The claim never changes PASS or FAIL.

## Baselines per model

The reports of each published release are archived under
[benchmarks/](../benchmarks/README.md), and the table below is generated
from them; do not edit it by hand. After changing the archive, run:

```sh
python3 scripts/perf-table.py --update docs/PERFORMANCE.md
```

Each row is one title on one console model, with the median of its runs:

- fps: emulated frames per second of wall time, with the slowest and fastest
  run.
- Work p50/p99: emulation time per frame without the waits.
- Frame p99: 99th percentile of the time between frames.
- Over 1.5x budget /min: frames that took more than 1.5 frame times, per
  minute of capture.
- Underruns: audio buffer underruns per run.

"not measured" means that model has no reports for the title; "—" means
the reports of that release do not have the field.

<!-- perf-table:begin -->
No reports archived yet.
<!-- perf-table:end -->
