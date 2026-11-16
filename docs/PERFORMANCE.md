# Performance

How GBA Rush PSP speed is measured, and the baselines measured so far.

## Comparing two builds

On the PSP, use Diagnostics > Start recording, play a repeatable section,
then Stop recording and Save report. Capture at least three reports per
build with the same ROM, save or save state, renderer, clock and settings,
then compare:

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
  waits the work time leaves out (the VBlank waits, the sound sync's wait
  for the audio thread and the GE wait). During a lockstep link it also
  holds the link waits.
- `phase-main-scanline-us`: update_scanline.
- `phase-main-gbc-sound-us`: the PSG channels (update_gbc_sound). Direct
  sound, hundreds of calls a frame, is not timed.
- `phase-main-work-us`: the work time of the same frames. The three phases
  add up to less; the rest is the CPU core.
- `probe-read-ns`: the cost of one clock read, from 64 reads in a row when
  the capture starts. `probe-reads`: the reads the timers made in the
  sampled frames. Their product is the time the timers added. This
  calibration runs when the menu closes on the game, at the game clock,
  which the menu can still change.

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
  together. A menu visit or a save state during the capture falls in one
  frame's work and can set it, and the 17-plus class, as it sets the
  frame-time maximum.
- `preempt-frames-0` to `preempt-frames-17-plus`: the frames with 0, 1, 2,
  3-4, 5-8, 9-16 and 17 or more preemptions.
- `preempt-call-ns`: the cost of one read, from 16 reads when the capture
  starts, also at the game clock. Each frame makes two, which count as
  work.
- `preempt-available=0`: the kernel refused a read (the call may be
  missing on some firmware or in PPSSPP), so the counts stopped there; the
  rest of the report is valid.

## Unbound benchmark

Diagnostics > Unbound benchmark loads the current save state slot as the
slot menu does (a numbered slot backs up the progress it replaces to the
auto slot first, and the load asks before replacing the game), then runs
3600 frames as fast as the PSP can: every frame is drawn, nothing waits
for the VBlank, VSync is off, and the sound is mixed but dropped, so the
emulation never waits for audio. The game gets no input. The capture then
stops by itself and its report is saved to `benchmarks/`; the Diagnostics
status row shows its name, and the game returns to its normal speed. A
link refuses the benchmark. Opening the menu, a load or save state hotkey,
a reset, a ROM change or a suspend before the end mark the report
interrupted.

- `bench-mode`: `unbound` for the benchmark, `paced` for a capture the
  player runs.
- `bench-frames`: the benchmark's frame count, 0 when paced.
- `bench-render`, `bench-audio`: 1 when the frames are drawn and the audio
  is mixed.
- `bench-input`: `none` for the benchmark, `pad` when paced, `trace` when
  the benchmark replays an input trace.
- `bench-trace-checksum`: with `bench-input=trace`, the checksum field of
  the trace's file as `0x` and eight hex digits; empty otherwise.

Diagnostic builds add two arms of the same run below it. Unbound
benchmark, rendering off skips every frame as frameskip does: no line is
drawn and nothing is presented or flipped, with the Exact renderer on the
Media Engine too, so the screen keeps its last picture and the frames
count as skipped. Unbound benchmark, audio off writes no sound samples,
while the Direct Sound FIFO, its DMA and the PSG channels advance as
always. Their reports say `bench-render=0` or `bench-audio=0`, and the
throughput they gain over the plain benchmark of the same build and slot
is what drawing or the audio costs. compare-benchmarks refuses that pair,
because its `bench-render` or `bench-audio` differ, so read the gain from
each report's `emulated-frames` and `duration-us`.

In diagnostic builds the benchmark also replays the input trace the
Diagnostics page holds: one just recorded and saved, one loaded with its
replay row, or one whose replay finished, as long as its game code and
revision match the loaded ROM. The trace starts again from its first
frame with the benchmark's, the game gets its keys instead of none, and the
run ends with the trace's last frame instead of after 3600 frames.
`bench-frames` is then the trace's length in frames, which bounds the run,
and the report says `bench-input=trace` with the trace's checksum. With no
trace held, or one that does not match the ROM, the benchmark runs
without input as above. Do not press the FPS display hotkey during a trace
benchmark: that frame skips the trace, so its keys reach the game one frame
late and the run no longer ends in the same state.

compare-benchmarks requires the same `bench-*` values in every run, and
reads a report without them (from an older build) as paced, so a benchmark
never compares with a paced capture. It does not read
`bench-trace-checksum`: check by hand that two trace benchmarks replayed
the same trace.

A benchmark's `duration-us` is the sum of its frame times, which run back
to back, so the return from the menu before its first frame is left out.
Its `vsync=0`, `frameskip-type=2` (none) and `frameskip-value=0` are what
it runs under, whatever the options say, and every one of its frames counts
in `frames-late`, since none waits.
Its `heap-free-low`, `heap-largest-low` and `kernel-free-low` come only
from the memory measured when it starts: the largest-block probe
allocates, so it never runs at the end of the benchmark's last frame.

## Padding experiment

Moving code and data by a few kilobytes, as any unrelated change does, can
change the emulator's speed through the PSP's caches. This experiment
measures that effect with two diagnostic builds of the same tree that differ
only by about 3000 bytes of code and 3000 bytes of BSS that nothing uses
(`source/src/padding_probe.c`):

```sh
make -j4 psp-build PROFILE=diagnostic PADDING_PROBE_BYTES=0
make -j4 psp-build PROFILE=diagnostic PADDING_PROBE_BYTES=3000
```

Copy `EBOOT.PBP`, the two PRXs and `TempGBA.elf` out of `source/` after
each build. Install the builds in two folders, such as
`PSP/GAME/GBARushPSP-PAD0/` and `PSP/GAME/GBARushPSP-PAD3000/`, with the
same settings, BIOS and save state in both. Their reports carry the same
`build-id`, so only the folder a report was saved in tells the builds
apart.

Before any run, list both ELFs with `psp-nm -n`. The build links the probe
first and without `-flto`, so in the 3000-byte build `padding_probe_code`
must sit below `update_gba` and `padding_probe_data` below the translation
caches (`readonly_code_cache`, `writable_code_cache`), and those three must
sit at different addresses in the two builds. If they do not, the probe is
not ahead of the core: that is a build defect to fix in `source/Makefile`
before any run, not a result.

Then, on a PSP-1000 and on one slim, with the same two titles on both:

1. Follow steps 2 to 5 of the [measurement protocol](#measurement-protocol):
   333 MHz, link off, the same BIOS and save state slot, one warm-up run
   discarded, the builds alternated.
2. Each run is the plain [Unbound benchmark](#unbound-benchmark), without
   an input trace recorded or loaded first, so every report says
   `bench-input=none`.
3. Make 3 runs per build and title: 12 runs per model.

Put each build's reports in its own folder and compare them, the 0-byte
build as baseline; compare-benchmarks makes one group per title and model:

```sh
python3 scripts/compare-benchmarks.py --order-check --strict \
  --baseline-dir pad-0 --candidate-dir pad-3000
```

The decision comes from each group's exact test, not from PASS or FAIL. The
padding changes the speed when, on at least one model, a title's
`throughput=` changes by more than 2% either way and its `throughput-runs=`
verdict states the difference (`candidate higher` or `candidate lower`).
The test is weaker than a single 5% test: compare-benchmarks picks the side
from the medians after the runs, so a stated verdict means p <= 0.10
two-sided for its group, and the rule reads four groups (two titles on two
models). Every group must say `order=alternated`. The result and the
decision go here, with the addresses `psp-nm` showed, after the hardware
session.

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
