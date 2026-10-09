# Changelog

## 1.4.0 - Unreleased

### Added

- An original register-level cable workload using production transport lanes
  and scheduling, independent guest clocks, delayed serial handlers and
  loss/duplicate/backpressure injection. It does not execute a game ROM.
- Benchmark start/run-control records and explicit paired run selection in the
  log analyzer; missing evidence produces a readable or JSON failure.
- Modern, Classic and Custom fast-forward profiles with global defaults and
  optional per-game overrides.
- Hold or Toggle activation, fixed 2x/3x or maximum speed, three presentation
  rates and optional accelerated audio for Custom mode.
- Fast-forward speed, presentation, transition and audio-fallback measurements
  in performance reports and the benchmark comparison gate.
- Versioned runtime snapshots for link state, recovery, wait time and transport
  counters, with a macOS decoder and four-session retention.
- Bounded CBMC harnesses for queue structure and full payload limits, packet
  parsing, journal transitions, serial arithmetic and lifecycle teardown,
  including assumption-reachability checks.
- A no-ROM, ten-minute two-PSP link benchmark that uses the gameplay stream,
  records paired checksummed summaries, and enforces throughput, nine-word
  burst, recovery, queue, timeout, audio and starvation limits.
- Protocol-v3 stream safety/liveness models and bounded proofs for exact-once
  application, cached-reply recovery, stale-generation rejection, exclusive
  transport health and bounded hot scheduling.
- Structured link failure domains, timeout sources and stream metrics that
  preserve internal decisions separately from original signed/hex PSP API
  results.

### Changed

- Two-player Multiplayer 16-bit transfers now require protocol v3 and use one
  host `TRANSFER_START` plus one joiner `TRANSFER_REPLY` per causally available
  word. Duplicate replay is exact-once, only one word is active, and the former
  six-phase decision/release exchange is not used on this hot path.
- Clock anchors and cumulative reply acknowledgements are carried by stream
  and heartbeat records, replacing standalone high-frequency CLOCK traffic for
  negotiated multiplayer streams. Version 2 EBOOTs intentionally fail v3
  negotiation.
- Link data now uses an ordered PDP transport with cumulative and selective
  acknowledgements, adaptive retransmission and fixed send/receive windows.
  It does not allocate memory during play.
- The PSP network worker uses bounded latency-priority drains while stream work
  is hot and a low-CPU wait while cold; ready replies wake the worker without
  letting diagnostics, heartbeat or retained legacy clock traffic overtake
  critical data.
- Link liveness is bidirectional and tokenized: after one second without
  traffic either peer can send a request, either role replies once with the
  matching token, and replies are never echoed. Duplicate requests are
  suppressed, lost replies use a new token at the next wall-clock interval,
  and heartbeat ordering remains independent of serial transfer sequences.
- Redundant SIO state is collapsed while a peer remains in the menu; the newest
  state is published before serial transfers resume.
- Audio mixing now retains full 16-bit precision, rounds interpolation and
  volume scaling and uses deterministic integer timing.
- External dynarec branch targets are resolved through an iterative worklist,
  bounding host stack use on long first-visit branch chains.
- Mosaic rendering avoids per-pixel division and correctly advances sampling
  in rotated bitmap paths.
- Performance options keep common controls visible and move low-level dynarec,
  HBlank, stack and boot settings to an advanced screen.
- Contributor documentation separates the link specification from the 1.4.0
  release checklist; obsolete design drafts and local progress records are
  retired. Inherited text documentation is grouped under `docs/legacy/`.
- Repository ignores cover local workspaces, private ROMs, hardware logs and
  build output while retaining the four TLC model configurations required by
  the link verification gate.

### Fixed

- Early multiplayer STARTs now wait for the joiner's previous serial event
  and the mapped inter-word guest time before latching the next outgoing
  value. Tests cover unequal cycle bases, wrap and recovery before latching.
  Arbitrary-guest synchronization and real FireRed trades remain unverified.
- Multiplayer receive registers accept guest writes, including 32-bit clears;
  starting a transfer still initializes all receive slots to `FFFF`.
- Lost final SIO/menu presence can be repaired by periodic latest-state
  refresh without suppressing correlated liveness probes. Stale heartbeat
  samples no longer impose a 500 ms advisory stream pacing wait.
- Uncertain stream reply timeouts request coordinated recovery rather than
  cancelling a result already scheduled by the peer. Disconnect preserves a
  scheduled register result and its pending serial IRQ.
- Active benchmarks become terminal on disconnect, suspend, ROM change and
  transport failure. A silent joiner times out even before the first word;
  terminal summaries remain immutable and are recorded before teardown.
- Benchmark live duration no longer includes emulator uptime. Offered load
  remains 1,000 words/s while the release acceptance floor is 600; unbounded
  histogram tails no longer report misleading 32 ms percentiles.
- Benchmark menu service no longer depends on rendering/vblank cadence, and
  no-ROM status no longer hides transport failures behind the no-game label.
- Final benchmark queue measurements allow a bounded STOP/ACK drain and avoid
  counting the critical handoff twice. Transport ACK progress reports a real
  advancement count instead of a boolean.
- Cold no-ROM link startup now prepares SIO register storage before creating
  or joining a session. Repeated preparation preserves a loaded game's state,
  and session admission rejects missing storage instead of dereferencing it.
  RC3.2 targets the confirmed RC3.1 startup crash; PSP benchmark and FireRed
  trade acceptance remain outstanding.
- Protocol-v3 liveness no longer depends on independently timed, unacknowledged
  token-zero anchors. A peer that receives reverse traffic now answers its
  correlated heartbeat request, providing a return path when independent
  anchors are lost. Physical radio delivery still requires validation. Directional
  request/reply, duplicate, unmatched and outstanding-token diagnostics are
  retained in failure logs.
- Recovery clears heartbeat duplicate-token and outstanding-request state
  without erasing cumulative diagnostics. A reused token in the new stream
  generation receives a reply; delayed old-generation payloads remain ignored.
- Protocol-v3 clock publication now schedules at most one heartbeat request
  per second; immediate correlated replies also defer the next scheduled request.
  The former cycle-rate check accidentally converted the removed
  near-1 kHz CLOCK stream into hundreds of heartbeat datagrams per second,
  exhausting PSP PDP send space before FireRed could start its first SIO word.
- Transport health is one generation-tagged exclusive state, so a delayed
  degraded observation cannot overlap recovery and reproduce invariant
  `0x200`. Ordinary stream loss retransmits an immutable start or cached reply
  before escalating to full recovery.
- Session recovery retains a pending start or cached reply as semantic state,
  re-encodes it for the new generation, and rejects old-generation datagrams
  before they can mutate the session.
- Internal session error `-7` can no longer be presented as a PDP receive
  result; the UI and runtime logs report its domain independently from the
  originating PSP API phase and value.
- Link recovery now reconciles one-sided arms, issued decisions, released
  records and completed checkpoints through one epoch coordinator. Conflicting
  hashes, checkpoints or unsupported decision claims close the session instead
  of guessing, while nested transport rebuilds reuse the active recovery round.
- PSP-1000 link startup preserves memory for delayed network modules, retries
  transient control states and resolves the remote peer before opening PDP.
- Link setup continues in the background after leaving its menu; explicit
  disconnect and lifecycle shutdown remain orderly and do not report stale sends.
- Link peers advertise menu/gameplay presence, so serial work waits for both
  games without dropping the underlying ad-hoc session.
- Link liveness treats menu presence as an intentional pause. Network failures
  remain recoverable while gameplay presence and queued traffic are retained.
- Menu exit publishes gameplay presence only after ROM and UI teardown has
  completed, preventing staggered returns from closing the peer session.
- Link setup pins the configured gameplay clock before opening PDP and avoids
  PLL, CPU or bus changes until the session has closed.
- First-failure diagnostics retain ad-hoc control state, PDP status, transport
  epoch, sequence window, retransmissions, recovery tier and traffic counters.
- Lost, duplicated and reordered datagrams have bounded deterministic recovery
  coverage. Recovery advances through probe, transport resync, group rebuild
  and network-stack restart without a separate stream socket; this is not a
  claim of completed physical-game acceptance.
- Transport recovery now completes a bilateral barrier before resuming the GBA
  session. A second recovery supersedes an unfinished resume instead of racing
  its timeout, and duplicate acknowledgements replay the final confirmation.
- Late resync requests cannot reopen a completed transport epoch, and nested
  recovery cannot extend the session deadline indefinitely.
- Starting Wi-Fi during an in-game cable wait now aborts the stale serial
  operation and delivers its interrupt so the game can retry after connection.
- Link liveness has deterministic virtual-time coverage for asymmetric
  menu/gameplay operation, packet loss, latency, duplication and delayed
  replies across a serial-sequence transition.
- Sustained PDP backpressure before a first successful critical send now
  degrades transport health independently of ACK progress and requests the
  existing recovery coordinator after a bounded interval.
- Retained non-stream CLOCK synchronization backs off adaptively under measured
  socket pressure; SIO state, heartbeat and critical traffic remain eligible
  while CLOCK is deferred. The v3 multiplayer stream uses piggybacked anchors
  instead of standalone hot CLOCK traffic.
- Gameplay receive silence now requests coordinated transport recovery before
  terminal timeout, so a peer with no outstanding critical packet cannot close
  independently while the other peer is recovering an armed operation.
- Backpressure episodes, blocked lane/class, CLOCK throttling and session-origin
  recovery requests survive epoch resets in the retained runtime diagnostics.
- A delayed heartbeat reply no longer causes a protocol failure when a serial
  transfer advances while that reply is in flight.
- Temporary serial-mode mismatches abort only the affected transfer; the Link
  screen falls back to ROM identity and retains the first failure with context.
- Fast-forward state can no longer survive menus, reset, ROM changes, link
  setup, suspend or exit.
- Version 1.3 settings migrate to the Modern fast-forward profile without
  discarding unrelated configuration.
- Accelerated audio falls back to silence when its output buffer cannot remain
  stable.
- A completed network teardown can no longer be reported as deadline-expired
  when a later lifecycle poll observes its elapsed deadline.

## 1.3.0 - 2026-08-22

### Added

- A low-overhead performance monitor with live diagnostics and bounded reports.
- Diagnostic input recording and replay for development builds.
- Contextual help for settings and a benchmark comparison gate.

### Changed

- Menus are grouped by task and retain their selection when returning from a
  child screen.
- Renderer dispatch is selected once per frame instead of once per scanline.
- Audio output, suspend, resume, and shutdown use a consistent PSP SRC-channel
  lifecycle.
- Global and per-game configurations use versioned validation and recoverable
  writes while preserving existing settings.

### Fixed

- Long menus scroll within a fixed viewport instead of overlapping the guide
  and button footer.
- Invalid configuration fields no longer discard unrelated valid settings.

## 1.2.0 - 2026-08-21

### Added

- `16:9 Auto`, which starts with a sharp two-pass fullscreen renderer and
  falls back to single-pass nearest scaling after sustained timing misses.
- A manual `16:9 Sharp` mode and `SHARP`/`CRISP` status in the FPS overlay.

### Changed

- Fullscreen presentation now overlaps GE work with the remaining GBA VBlank.
- Fullscreen paths no longer clear pixels that the next frame fully replaces.
- New configurations default to `16:9 Auto`; existing scale values are kept.

### Fixed

- Video draw-buffer, viewport, texture, and filter state is rebuilt after PSP
  resume before presentation restarts.

## 1.1.0 - 2026-08-21

### Added

- Two-player GBA link cable emulation over PSP ad-hoc Wi-Fi.
- Normal 8-bit, Normal 32-bit, and Multiplayer 16-bit serial modes with
  hardware-derived completion timing and serial interrupts.
- Create, join, status, peer information, and disconnect controls in all five
  interface languages.
- A framed, versioned link protocol with ROM metadata, transfer barriers,
  heartbeats, timeouts, and clean disconnect reasons.
- Redistributable diagnostic ROM sources and deterministic two-core stress
  coverage for variable network delay and malformed traffic.

### Changed

- Savestates and fast-forward are disabled while a link session is open.
- Reset, ROM replacement, suspend, sleep, and exit now close an active link
  session before changing emulator state.
- ROM loading records exact size and CRC32 for peer diagnostics without
  requiring identical ROM files.

### Fixed

- PSP resume no longer waits for suspended audio samples to drain.
- Pending audio, save, ROM, and link recovery is completed in the foreground
  after every suspend cycle.
- Sound and ad-hoc worker shutdown paths no longer wait without a deadline.
- Ad-hoc command and event queues now enforce publication order between the
  emulator and network threads.
- Oversized transport events are rejected instead of being emitted without a
  payload.

### Limitations

- This release supports two PSP systems and GBA cable modes only.
- GBA Wireless Adapter, UART, General Purpose, JoyBus, GameCube link,
  multiboot, and internet relay modes are not supported.
- Broad compatibility and performance testing across two physical PSP systems
  and the commercial game catalog has not yet been completed.

## 1.0.0 - 2026-08-18

### Added

- Plaintext and encrypted CodeBreaker cheat support.
- Timed CodeBreaker master hooks in the dynamic recompiler.
- Configurable hold-to-fast-forward action with paced display updates.
- Host regression tests for CodeBreaker decoding and every supported operation
  family.
- Repeatable source checks and release packaging commands.

### Changed

- Rebranded the independent fork as GBA Rush PSP with a new PSP-native visual
  identity and clearer upstream attribution.
- Cheat loading is manual, transactional, and reports precise errors.
- Cheat pages show stable entry numbers, names, states, filename, and count.
- Runtime cheat work scales with active entries and configured hooks.
- Fast-forward drops queued audio and restores normal pacing immediately when
  released.
- PSP build flags now distinguish C, C++, assembler, and vendored zlib sources.
- The EBOOT uses the stable `GBA Rush PSP` title.

### Fixed

- Multiline CodeBreaker payloads no longer look like hook or seed operations.
- Conflicting timed masters no longer dispatch ordinary codes more than once.
- Signed 32-bit CodeBreaker addition and conditional skip lengths.
- Audio suspension when entering the menu during fast-forward.
- Several compiler warnings and two malformed translated strings.
