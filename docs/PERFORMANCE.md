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
   both builds (`gba_bios.bin` or the built-in one); when the reports have
   no `bios=` line, write down which. Write down the battery level, or that
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
`not demonstrated`, or `not measured` when a run lacks the metric. The
claim never changes PASS or FAIL.

## Phase timers

During a capture one frame in 11 (the 11th, 22nd, ... of the capture) is
timed in phases; 11 is prime, so the sampled frames do not follow the 2- to
6-frame cycles of games. Divide each total by `phase-main-sampled-frames`
for the time per frame:

- `phase-main-update-gba-us`: update_gba's own time (scheduler, input,
  presenting the frame, DMA), without the two phases below and without the
  waits the work time leaves out (the VBlank waits and the GE wait). During
  a lockstep link it also holds the link waits.
- `phase-main-scanline-us`: update_scanline.
- `phase-main-gbc-sound-us`: the PSG channels (update_gbc_sound). Direct
  sound, hundreds of calls a frame, is not timed.
- `phase-main-work-us`: the work time of the same frames. The three phases
  add up to less; the rest is the CPU core.
- `probe-read-ns`: the cost of one clock read, from 64 reads in a row when
  the capture starts. `probe-reads`: the reads the timers made in the
  sampled frames. Their product is the time the timers added.

The sampled frames count as frames but stay out of the frame-time and
work-time percentiles and maximum. A sampled frame longer than 128 ms (a
menu, a save state) is not timed. `frames-work-over-budget`,
`frames-over-1-5x-budget` and `frames-late` still count the sampled frames
with the time the timers' reads add, so a frame just under budget can go
over on a sampled frame: allow for it when comparing with a report made
before the phase timers.

The New core's diagnostic profile, saved beside the report as
`-core.txt`, has its own `phase-<name>` keys (code, dispatch, memory,
translate, interpret, events): those split the CPU core's time and are a
different measure, hence the `phase-main-` prefix here.

## Preemptions

During a capture the emulation thread reads its preemption counts from the
PSP kernel twice a frame: before the frame pacing in `synchronize()`, where
the work ends, and after the flip, where the waits end. The first frame of
a capture is not counted.

- `preempt-work-thread`, `preempt-work-intr`: the times another thread or
  an interrupt took the CPU from the emulation thread during the work.
- `preempt-wait-thread`, `preempt-wait-intr`: the same during the frame
  pacing, the sound sync and the flip.
- `preempt-max-per-frame`: the most preemptions in one frame, all four
  together.
- `preempt-frames-0` to `preempt-frames-17-plus`: the frames with 0, 1, 2,
  3-4, 5-8, 9-16 and 17 or more preemptions.
- `preempt-call-ns`: the cost of one read, from 16 reads when the capture
  starts. Each frame makes two, which count as work.
- `preempt-available=0`: the kernel refused a read (some firmware, or
  PPSSPP), so the counts stopped there; the rest of the report is valid.

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
