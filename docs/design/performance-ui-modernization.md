# Performance and interface modernization

Status: approved design for the next GBA Rush PSP release.

## Goal

Improve performance, frame pacing, audio stability, diagnostics, and menu
clarity without weakening game compatibility or the suspend, cheat, save, and
two-PSP link behavior shipped in 1.2.0.

Work is measurement-led. A change to a hot path is retained only when the
PSP-2000 benchmark corpus shows a repeatable benefit and the automated checks
remain clean.

## Scope

The release covers four connected areas:

1. a low-overhead performance harness for host and PSP builds;
2. targeted corrections and optimizations in video, audio, dynarec, and frame
   scheduling;
3. a data-driven menu layout with contextual help;
4. configuration migration and regression coverage for existing users.

A core rebase, a new renderer imported wholesale from another emulator,
three- or four-player link, new cheat formats, and a public ROM compatibility
database are outside this release. Upstream fixes may be ported individually
when they have a clear local test and attribution.

## Performance harness

### Metrics

A portable `perf_metrics` module owns fixed-size counters and histograms. It
records:

- emulated and presented frames;
- skipped frames and fast-forward display ratio;
- frame duration percentiles and worst frame;
- graphics-engine wait time;
- audio queue depth, recoveries, and underruns;
- dynarec block compilations, cache flushes, and cache high-water mark;
- available memory low-water mark.

Collection is disabled by default. Disabled instrumentation is limited to one
predictable branch at each sampling point. Normal releases retain aggregate
counters and fixed histograms for the Diagnostics screen; per-frame traces and
input replay require a diagnostic build flag. Metrics use preallocated memory
and never allocate or write files from a video, audio, or dynarec hot path.

### PSP diagnostics

The Diagnostics menu can start and stop a benchmark session, reset counters,
show a compact live summary, and write one report after collection stops. A
report includes emulator version, PSP model, clock setting, renderer, scaling
mode, frameskip, audio settings, ROM header identity, duration, and metrics.
It contains no ROM content, save data, filesystem paths, or cheat contents.

Diagnostic builds can record and replay input. Events are keyed to emulated
frames and stored separately from private savestates. This makes the same scene
repeatable without placing commercial game data in the repository or release
archive.

### Baseline method

Release 1.2.0 is the comparison baseline. Each candidate is tested on the same
PSP-2000, memory card, clock, configuration, savestate, and input trace. Three
runs are collected after one warm-up run; the median result is compared.

Normal-speed work must not regress median emulation throughput by more than two
percent or 95th-percentile frame duration by more than five percent. It must
not introduce recurring audio underruns, new skipped-frame bursts, or visible
correctness faults. Optimizations that do not clear measurement noise are
removed. Fast-forward throughput must not fall below 1.2.0 and is improved
where profiling identifies a bounded bottleneck.

## Runtime architecture

The PSP platform boundary remains PSPSDK and the system audio, GU, display,
power, and cache APIs. The project does not introduce replacement audio or
video drivers. Performance work targets correct API use, buffering, cache
ownership, and scheduling inside the emulator.

### Video

Scanline rendering keeps a direct, stable renderer callback for the duration
of a frame. Renderer selection and shared-state synchronization move to frame
boundaries unless the emulated register change requires scanline precision.

Cache maintenance is narrowed only where ownership and written ranges are
provable. Range writeback and full-cache writeback are benchmarked on hardware;
the safer path remains when the gain is not repeatable. The existing libGU
presentation and adaptive Sharp/Crisp modes remain the supported PSP display
path.

### Audio

Audio reservation, output, drain, suspend, and release use one consistent
PSPSDK API family. Buffer depth and wake-up behavior are measured at normal
speed and during fast-forward. Fast-forward continues to discard stale audio
and restores a clean queue when released.

The first pass corrects lifecycle and accounting before changing mixing code.
VFPU or assembly work is considered only if profiles show that mixing or
conversion is a material cost and a portable reference implementation can be
tested bit-for-bit.

### CPU and dynarec

Dynarec work begins with counters for block compilation, invalidation, cache
pressure, and fallback execution. Changes are restricted to observed hot paths
or individually ported correctness fixes. Cache size, eviction, and branch
handling are not changed from intuition alone.

PSP clock selection stays explicit and visible. The emulator may recommend a
setting, but it does not silently overclock or change a user's saved value.

### Scheduling and power

Frame pacing owns the normal-speed and fast-forward policies through one state
machine. Audio, video, link, and input see explicit transitions rather than
inferring mode from unrelated globals.

Suspend remains foreground-coordinated. A benchmark session is invalidated on
suspend, timing history is cleared on resume, and no diagnostic write delays
the power callback. The recovery behavior shipped in 1.1.0 remains a release
gate.

## Interface

The main menu is reorganized into:

1. Continue;
2. Save and Load;
3. Game;
4. Video;
5. Audio;
6. Performance;
7. Controls;
8. Link Cable;
9. Interface;
10. Diagnostics;
11. Exit.

All supported settings remain visible in their relevant category. There is no
separate Advanced menu. Each row presents one setting and its current value.
A stable help area explains what the selected setting changes, the recommended
value, its performance or compatibility impact, and whether it requires a
restart.

Unavailable settings remain visible but dimmed, with the reason shown in the
help area. Returning from a child screen preserves the previous selection.
Confirm, back, left, and right behavior is consistent across menus. Duplicate
entries and spacing controlled by manually inserted blank rows are removed.

Menu declarations use a small data model containing label, value provider,
action, availability predicate, and help message. Rendering and navigation do
not own emulator configuration logic. The existing five interface languages
retain identical message ordering, enforced by the translation check.

## Configuration

Configuration loading moves behind a versioned interface with explicit
defaults, validation, and migration. Existing numeric values keep their
meaning. Unknown or malformed values fall back at field level instead of
discarding the entire configuration.

Saving writes and validates a complete temporary file before promotion. The
previous file is retained as a recovery copy until promotion succeeds. If
promotion fails, the loader restores the recovery copy and the interface
reports the error. ROMs, saves, savestates, cheat files, recent-game history,
and user button mappings are never rewritten as part of migration.

## Test corpus

Automated checks use portable unit tests, sanitizer builds, the open mGBA test
suite where applicable, and redistributable diagnostic or homebrew ROMs.
Commercial ROMs, BIOS files, savestates, input traces derived from private
play, and benchmark reports remain local and untracked.

The initial private PSP-2000 corpus is:

- Pokemon FireRed, Italian release: long play, save, cheats, and link;
- Golden Sun, Italian release: sprite ordering, blending, and battle effects;
- Metroid: Zero Mission, European release in Italian: scrolling and layered
  effects;
- Kirby & The Amazing Mirror, European release in Italian: dense sprites and
  multiplayer link;
- Mario & Luigi: Superstar Saga, European release in Italian: audio, timing,
  and scripted transitions;
- V-Rally 3, European release in Italian: pseudo-3D load and frame pacing.

The Legend of Zelda: The Minish Cap, European release in Italian, is an
optional long-play and mixed-workload addition.

## Release gates

Required gates are:

- host tests with warnings treated as errors;
- AddressSanitizer and UndefinedBehaviorSanitizer runs;
- clean PSP and exception-handler builds;
- source-tree, translations, packaging, and artifact checks;
- no regression in CodeBreaker, save handling, link protocol, or suspend and
  resume tests;
- successful cold start, game load, save, fast-forward, repeated suspend, and
  clean exit on a PSP-2000;
- three-run benchmark comparison against 1.2.0 for each available corpus game;
- a release archive containing no ROM, BIOS, save, trace, report, cheat list,
  or user configuration.

Testing on PSP-1000, PSP-3000, PSP Go, and PSP Street is valuable but optional
for this release. Missing coverage on those models is recorded in the release
notes and does not block publication.

## References

- [PSPSDK audio API](https://pspdev.github.io/pspsdk/pspaudio_8h.html)
- [PSPSDK Graphics Utility](https://pspdev.github.io/pspsdk/group__GU.html)
- [PSPSDK cache utilities](https://pspdev.github.io/pspsdk/group__Utils.html)
- [libretro gpSP](https://github.com/libretro/gpsp)
- [mGBA test suite](https://github.com/mgba-emu/suite)
