# Performance and interface modernization implementation plan

Status: implemented for GBA Rush PSP 1.3.0. Remaining hardware coverage is
recorded in the release notes.

This plan implements `docs/design/performance-ui-modernization.md` on `main`
after release 1.2.0. It preserves the public history and does not publish or
install a hardware build until the automated gates pass.

## Working rules

- Establish the 1.2.0 host and hardware baseline before judging performance.
- Add measurements before changing each hot path.
- Keep PSP APIs out of portable policy, metrics, menu-model, and config code.
- Avoid allocation, file access, formatting, and locks in emulation hot paths.
- Keep ROMs, BIOS files, saves, savestates, traces, and reports untracked.
- Port upstream fixes individually with attribution and a local regression
  case; do not merge or rebase the core wholesale.
- Run `make check` after every portable stage, `make test-sanitize` after every
  completed stage, and `make psp-build` after PSP-facing changes.
- Use narrow commits that describe the completed behavior.

## Stage 1: portable performance metrics

### Files

- Add `source/src/perf_metrics.h` and `source/src/perf_metrics.c`.
- Add `tests/test_perf_metrics.c`.
- Update the root and PSP Makefiles.

### Work

1. Define a fixed metrics snapshot with session state, emulated, presented and
   skipped frame counts, fast-forward frames, audio recovery and underrun
   counts, dynarec compilation and flush counts, cache high-water mark, memory
   low-water mark, GE wait total and maximum, frame-time total and maximum.
2. Store frame durations and GE waits in fixed logarithmic histograms so the
   50th, 95th, and 99th percentiles require no allocation or sorting.
3. Provide reset, start, stop, active, record, and snapshot interfaces. Recording
   while inactive has no effect.
4. Use saturating 64-bit counters and deterministic percentile boundaries.
5. Keep time sources, PSP model discovery, and report formatting outside the
   module.

### Tests

- Initial, reset, start, repeated start, stop, and repeated stop states.
- Inactive recording, counter accumulation, saturation, and low/high-water
  marks.
- Exact percentile behavior at bucket boundaries and with empty samples.
- Snapshot stability after stop and reset behavior after a new session.
- Normal and ASan/UBSan builds with warnings treated as errors.

### Exit condition

The new portable test passes in `make check` and `make test-sanitize` without
changing emulator runtime behavior.

Suggested commit: `Add portable performance metrics`

## Stage 2: runtime instrumentation and reports

### Files

- Update `source/src/main.c`, `source/src/psp_video.c`, and their headers.
- Update `source/src/sound.c`, `source/src/cpu.c`, and `source/src/memory.c` at
  measured event boundaries.
- Add `source/src/perf_report.h` and `source/src/perf_report.c`.
- Add `tests/test_perf_report.c` and update both Makefiles.

### Work

1. Count emulated frames at GBA VBlank and presented or skipped frames at the
   existing presentation decision.
2. Feed the existing RTC frame duration and GE wait measurements into the
   portable histograms once per frame.
3. Count explicit audio recovery, queue-starvation, dynarec compile, dynarec
   flush, and cache-pressure events at their owning modules.
4. Read memory availability outside hot paths at benchmark start and stop.
5. Format a bounded text report from a snapshot and supplied platform/game
   metadata. Escape or replace control characters in ROM header fields.
6. Write reports only after collection stops, using one foreground operation
   and a predictable `benchmarks/` filename relative to the emulator folder.
7. Invalidate an active session on suspend and discard pre-suspend timing.

### Tests

- Golden report containing every field and percentile.
- Empty and maximum counters, bounded strings, and hostile ROM header bytes.
- No filesystem path, ROM content, save data, or cheat data in the report.
- Source check proving no report writer is called from audio, renderer, or
  generated-code helpers.

### Exit condition

Normal gameplay is unchanged with collection disabled, a host report is
deterministic, and a PSP build can start, stop, and save one benchmark report.

Suggested commit: `Instrument emulator performance`

## Stage 3: diagnostic replay

### Files

- Add `source/src/input_trace.h` and `source/src/input_trace.c`.
- Add `tests/test_input_trace.c`.
- Update `source/src/input.c` and both Makefiles behind a diagnostic build flag.

### Work

1. Record button-state changes against emulated frame numbers into a bounded,
   preallocated buffer.
2. Encode a versioned trace with game code, ROM revision, start frame, event
   count, and checksum; do not include savestate or ROM bytes.
3. Replay only when trace identity matches the loaded ROM header and report a
   clear mismatch without altering input.
4. Stop cleanly on buffer exhaustion, suspend, ROM change, reset, or manual
   cancellation.
5. Exclude recording, replay, and trace commands from normal release builds.

### Exit condition

Host tests reproduce an input sequence exactly, malformed traces are rejected,
and normal builds contain no trace menu or trace storage.

Suggested commit: `Add diagnostic input replay`

## Stage 4: menu model and contextual help

### Files

- Add `source/src/menu_model.h` and `source/src/menu_model.c`.
- Add `tests/test_menu_model.c`.
- Split configuration and menu declarations from `source/src/gui.c` into
  focused modules without changing the existing drawing primitives.
- Update `source/src/message.h` and all five translation tables.

### Work

1. Define a portable row model with label, value, action, availability,
   visibility, and help identifiers.
2. Move main-menu declarations into the approved category order and remove the
   duplicate savestate entry and manual blank-row spacing.
3. Keep every supported setting visible in its relevant category. Disabled
   rows remain navigable enough to show the reason they are unavailable.
4. Add a stable lower help panel with summary, recommendation, impact, and
   restart requirement.
5. Preserve selection independently for each menu and use consistent confirm,
   back, left, and right actions.
6. Add Diagnostics controls for aggregate metrics and report writing. Add
   trace controls only in diagnostic builds.
7. Keep translation identifiers in identical order across all languages.

### Tests

- Row traversal with visible, hidden, enabled, and disabled combinations.
- Value adjustment bounds, action dispatch, and selection restoration.
- Exact approved main-menu category order and no duplicate action.
- Translation ordering and presence of every help message.

### Exit condition

All settings are reachable, every selectable row has contextual help, and menu
navigation passes host tests and PSP smoke testing.

Suggested commit: `Reorganize emulator settings`

## Stage 5: versioned configuration

### Files

- Add `source/src/config_model.h` and `source/src/config_model.c`.
- Add `tests/test_config_model.c`.
- Update the PSP file adapter currently embedded in `source/src/gui.c`.

### Work

1. Describe every persisted field with type, bounds, default, and migration
   behavior while preserving existing numeric meanings.
2. Parse fields independently; retain valid values and default only malformed
   or unsupported fields.
3. Promote a fully written and reread temporary file while retaining the last
   valid configuration as a recovery copy until success.
4. Restore the recovery copy after a failed promotion and expose one stable
   error to the interface.
5. Preserve button mappings, recent history, paths, video mode, cheat behavior,
   and all user data across migration.

### Tests

- Current and legacy fixtures, missing fields, unknown fields, invalid numbers,
  truncated files, duplicate keys, and maximum-length paths.
- Simulated write, flush, validation, rename, and restore failures through a
  fake file adapter.
- Exact round-trip preservation of current configuration values.

### Exit condition

Existing PSP configuration loads unchanged and every simulated interrupted
save leaves a recoverable valid file.

Suggested commit: `Harden configuration storage`

## Stage 6: audio lifecycle and pacing

### Files

- Update `source/src/sound.c`, `source/src/sound.h`, and lifecycle tests.
- Add a portable audio queue policy module only if required for deterministic
  testing.

### Work

1. Replace the `sceAudioOutput2GetRestSample` poll in the SRC-channel release
   path with lifecycle logic belonging to the SRC API family.
2. Measure release, reserve, output, queue starvation, and recovery events with
   the Stage 2 counters.
3. Benchmark supported sample counts and wake-up thresholds at normal speed and
   fast-forward on PSP-2000.
4. Centralize normal, fast-forward, link-wait, suspend, resume, ROM-change, and
   shutdown transitions so each discards or drains audio deliberately.
5. Retain bounded waits and silent-output fallback on reservation failure.

### Exit condition

There are no mixed audio API families, no unbounded wait, no post-fast-forward
backlog, and the corpus has no recurring underrun or suspend regression.

Suggested commit: `Correct PSP audio lifecycle`

## Stage 7: video dispatch and cache ownership

### Files

- Update `source/src/video_dispatch.c`, renderer wrappers, and
  `source/src/psp_video.c`.
- Add portable selection tests where state can be isolated.

### Work

1. Select a stable renderer callback at frame boundaries and avoid repeated
   renderer-selection work on every scanline.
2. Preserve scanline-accurate register state and savestate dispatch.
3. Identify the exact CPU-written source, display-list, and intermediate
   surface ranges before replacing any full-cache writeback.
4. Benchmark full and range writeback variants in identical PSP-2000 runs.
5. Retain libGU presentation, Sharp/Crisp fallback, suspend synchronization,
   and safe vertex-allocation failure behavior.

### Exit condition

Renderer regression tests and the visual corpus remain correct, and each kept
cache or dispatch change clears the measurement thresholds in the design.

Suggested commit: `Reduce video dispatch overhead`

## Stage 8: CPU and dynarec profiling

### Files

- Update `source/src/cpu.c`, `source/src/memory.c`, `source/src/mips_stub.S`,
  and related portable tests only at profiled boundaries.

### Work

1. Use Stage 2 counters to identify compilation, invalidation, or cache-pressure
   costs during normal play and fast-forward.
2. Compare current behavior against current libretro gpSP fixes and port only
   independently justified corrections.
3. Add a regression vector before each correctness change and a hardware
   benchmark before each performance change.
4. Reject speculative cache-size, eviction, assembly, or VFPU work without a
   stable measured benefit.

### Exit condition

All retained changes have a test or benchmark record, fast-forward does not
regress, and no title in the available corpus gains a correctness fault.

Suggested commits depend on the specific measured correction.

## Stage 9: integration and release candidate

1. Run `make check`, `make test-sanitize`, and a clean `make psp-build`.
2. Run source-tree, translation, lifecycle, package, and artifact checks.
3. Capture three candidate runs after a warm-up against the 1.2.0 PSP-2000
   baseline for each available corpus game.
4. Test cold start, save/load, cheats, fast-forward, ten suspend cycles, link
   connect/disconnect, menu exit, and clean XMB return.
5. Record optional PSP-model coverage without blocking the release.
6. Update the concise changelog, user documentation, release notes, version,
   and package manifest.
7. Inspect the archive and checksum before publishing or installing it.

### Exit condition

Every required gate in the approved design passes, measured results justify
the retained hot-path changes, and the archive contains no private runtime
material.
