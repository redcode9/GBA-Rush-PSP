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
