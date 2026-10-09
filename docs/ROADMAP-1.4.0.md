# 1.4.0 release roadmap

Version 1.4.0 ships only after two physical PSP systems can complete reliable
GBA cable transfers. The release covers Normal 8-bit, Normal 32-bit and
two-player Multiplayer 16-bit communication, with interchangeable host and
joiner roles on PSP-1000 and PSP-2000.

This is the maintained list of remaining work. An item closes when its stated
result is verified; a successful host test, socket connection or package build
does not substitute for the required hardware result. Protocol requirements
are maintained in the [link specification](link-cable.md).

## Verified starting point

The last documented game trial is the September 16, 2026 RC3.3 FireRed session
on PSP-1000/PSP-2000. Neither log records a START, REPLY, applied word or serial
IRQ. Receive-silence recovery held gameplay for about 20 seconds on the joiner
and 44.6 seconds on the host. Neither log contains a terminal benchmark
summary, and no completed trade has been verified.

The first cause remains unresolved. The retained queue high-water marks are
small; neither a full diagnostic ring nor a decoder label establishes radio
failure, queue exhaustion or suspend. The current stream clock hints are
advisory and do not prove safe timing for arbitrary guest programs.

Historical host, sanitizer, model and bounded-proof results cover the RC3.3
implementation. The production-scheduler regression exercises 720 synthetic
words; it does not execute a guest ROM or PSP network threads. All applicable
checks must run again on the final release candidate.

Local copies of the paired baseline logs and their acquisition manifest are
kept in `artifacts/link-baseline/2026-09-16/`, excluded from Git. The baseline
raw-log SHA-256 values are:

| Role | SHA-256 |
|---|---|
| Host | `df6a70ba5f5ddc2745714fb2974e3c160f53333b80d06014976b9edbae77d011` |
| Joiner | `017643b1f76fe955c12dd1009423d46cf5d46ad501ab150f4bb25b16877ca534` |

## 1. Diagnose and repair the pre-transfer failure

- [ ] **Reproduce the baseline from a known source build.** Record commit,
  toolchain, flags and EBOOT hashes for both roles. Preserve the first failing
  paired logs and a deterministic reproduction before selecting a repair.
- [ ] **Align log decoding with production emitters.** Correct packed
  drain/high-water fields and transport-health enum labels; separate the link
  nonce from runtime-log IDs, and prevent packed failure detail from overwriting
  the adjacent reason. Test legacy and current records. Distinguish recovery
  attempts from completions and clarify what the PSP receive-buffer field
  measures.
- [ ] **Exercise the production worker boundary in host tests.** Include queue
  publication, worker wakes, PDP dispatch and one-sided delivery/recovery.
  Reproduce the no-transfer receive-silence failure through this boundary.
- [ ] **Repair liveness and presence propagation.** Verify correlated heartbeat
  replies, menu/gameplay transitions, lost final presence updates and pending
  recovery requests. Both roles must converge or close within fixed deadlines,
  including when only one traffic direction is making progress.
- [ ] **Make serial waits and recovery responsive.** Replace long synchronous
  recovery work with bounded service steps; keep input, menu, power handling
  and audio policy serviceable while the GBA core waits. Verify queue ownership
  and publication ordering across the emulator and network threads.
- [ ] **Verify the repaired connection on both PSPs.** Demonstrate stable
  no-ROM connection and game/menu transitions in both role assignments before
  proceeding to throughput and trade acceptance.

## 2. Complete guest timing and serial correctness

- [ ] **Choose and document a feasible shared serial horizon.** Measure actual
  PSP latency and distinguish transport reliability from guest timing. Define
  how hot-mode entry, first START, cycle mapping and late events work without
  advancing a guest past an unreceived serial event.
- [ ] **Extend models and production timing together.** Cover unequal cycle
  bases, wrap, delayed guest interrupt handlers, mode changes, one-sided
  recovery and immutable scheduled results. Map model transitions to the
  production implementation and retain exact-once register/IRQ assertions.
- [ ] **Run guest-executed differential traces.** Use original diagnostic ROMs
  and a documented reference to compare register writes, latched data,
  completion cycles and serial IRQs. Cover Normal 8-bit, Normal 32-bit and
  Multiplayer 16-bit; precomputed register workloads alone cannot close this
  item.
- [ ] **Validate the complete fault matrix.** Inject loss, duplication,
  reordering, backpressure, stale generations, mode changes, peer departure,
  suspend and shutdown through the integrated worker/session/SIO path.
  Recovery must preserve known results or close cleanly, with bounded memory
  and no duplicated data or IRQ.

## 3. Qualify runtime performance and diagnostics

Diagnostics must provide trustworthy measurements before runtime optimization.
Qualify instrumentation separately from cable compatibility.

- [ ] **Review and integrate performance instrumentation.** Verify DMA
  counts/classes, frame-time timestamp initialization, firmware labels, stable
  replay identity and comparable ROM-paging metadata. Check both normal and
  diagnostic builds without claiming an unmeasured speed improvement.

- [ ] **Preserve the identity of a stopped measurement.** Reproduce Start
  measurement → Stop → change ROM → Save report through the production path.
  Store the original ROM identity or reject the stale report; verify reset,
  savestate and configuration transitions as well.
- [ ] **Collect comparable hardware baselines.** On PSP-1000 first, then
  PSP-2000, compare three runs per build with fixed ROM/state hashes, renderer,
  clocks, storage and actual ROM-cache capacity. Measure instrumentation
  overhead with the same binary and estimate normal run-to-run variation.
- [ ] **Resolve reproduced ROM-paging correctness defects.** Check stale cached
  pointers after slot reuse and short/failed page reads. Publish a page only
  after valid data is available and preserve executable-memory invalidation.
  Keep fixes separate from eviction-policy or prefetch experiments.
- [ ] **Profile link waits, frame pacing and audio.** Adopt only changes that
  remove a reproduced defect or improve measured latency without changing
  guest timing, raster behavior or sound correctness. Record memory headroom
  on PSP-1000 and benchmark both link roles.

## 4. Pass release acceptance on physical hardware

Both PSPs must run the same candidate. Record model, firmware, clocks, storage,
EBOOT hash, ROM revision/hash and role for every run. Back up saves first;
keep private ROMs, BIOS files and saves out of the repository.

- [ ] **Two paired ten-minute no-ROM benchmarks, with roles reversed.** Each
  must sustain at least 600 completed words/s at the offered 1,000 words/s,
  with nine-word burst p99 ≤ 20 ms, zero full recoveries, no persistent queue
  growth, stream timeout, stale-generation event or audio underrun, and no
  main-thread starvation over 50 ms. Preserve both terminal summaries and
  require the paired analyzer to pass:

  ```sh
  python3 tools/analyze_link_benchmark.py host.gbrlog joiner.gbrlog --json
  ```

- [ ] **Synthetic-mode and idle acceptance.** Complete 10,000 consecutive
  transfers per diagnostic mode and a 60-minute idle hot session without
  recovery churn. Cover both host/join role assignments.
- [ ] **At least 20 complete FireRed trades across repeated sessions.** Cover
  both role assignments, repeated room entry/trade/exit without restarting the
  link, in-game saves and reload verification of both received Pokémon. Run
  this only after both no-ROM benchmark gates pass.
- [ ] **Physical lifecycle and failure acceptance.** Test menu transitions,
  user disconnect, WLAN loss, suspend/resume, peer exit and local exit from
  every exposed session phase. Require bounded recovery or close, no crash or
  permanent freeze, no divergent result and a stable audio pause/resume.
- [ ] **Final software, proof and package gates.** Run `make check`,
  `make test-sanitize`, `make check-cbmc`, `make check-link-model`, a clean PSP
  build, `make package VERSION=1.4.0` and `make artifact-smoke VERSION=1.4.0`.
  Preserve commands, exits, source/toolchain identity and package hashes.
- [ ] **Publish verified release documentation.** Update README compatibility
  and known limits from actual hardware results, finalize the changelog and
  archive the accepted evidence. Tag and publish 1.4.0 only when every required
  item above has passed.

## After 1.4.0

Three/four-player sessions, Wireless Adapter, internet relay, JoyBus, UART,
multiboot, GPIO and a broader game catalog are separate compatibility work.
ROM prefetch, alternative eviction policies, bulk DMA, asynchronous GE,
renderer caching and direct JIT emission remain optional experiments after
correctness and measured baselines; they do not block this release unless a
reproduced defect requires a focused fix.
