# Changelog

## 1.4.0 - 2026-10-31

### Added

- Modern, Classic and Custom fast-forward profiles, per-game overrides and
  optional accelerated audio at 2x/3x.
- Fast-forward measurements in performance reports and benchmark comparisons.

### Changed

- Audio mixing retains 16-bit precision with integer timing and rounded scaling.
- Dynarec branch resolution uses an iterative worklist to bound host stack use.
- Mosaic rendering avoids per-pixel division and fixes rotated bitmap sampling.
- Performance menus separate common settings from advanced core options.
- The link cable is rebuilt around two techniques chosen per pair of games.
  Pokemon Ruby, Sapphire, Emerald, FireRed and LeafGreen, in any combination,
  emulate the wire locally on each PSP and relay only the games' link frames,
  so trades and battles run at full speed. The host pairs the frames so both
  games see identical data. Every other game exchanges each serial word.
- A new reliable channel repeats unacknowledged messages in every datagram,
  and a console only waits when it runs more than four frames ahead of the
  other one.
- The ad-hoc worker only moves datagrams; the network stack and the worker
  run above the emulator thread, which stopped reception during gameplay.
- Input, the HOME menu and power events stay available while the link holds
  the game.
- The link menu shows the chosen technique and the round trip. The no-ROM
  benchmark is removed; a game must be loaded before connecting.
- Runtime logs move to format 5 with the new link events.

### Fixed

- ROMs loaded from the startup menu now run without resetting the state restored
  there. Direct launches offer the auto-savestate prompt; the browser restores
  HOME handling, and the prompt waits for the selecting button to be released.
- Aligned IWRAM stack stores invalidate translated-code tags for SMOL overlays,
  following the upstream fix while preserving stores below the stack pointer.
- Renderer wrappers rebuild when their included sources change.
- Translation headers remain UTF-8 and compile as GBK for the menu glyphs;
  filename clipping and scrolling preserve GBK pairs and Shift-JIS kana.
- Evicted ROM pages invalidate the cached read pointer. Failed and short reads
  clear unread bytes and remain uncached for retry; partial final pages are padded.
- Stopped performance reports retain the original ROM and settings. DMA counters
  exclude SRAM from ROM transfers, and clock wrap no longer drops frame samples.
- Fast-forward ends at menus, reset, ROM changes, link setup, suspend and exit;
  accelerated audio falls back to silence when output cannot stay stable.
- Version 1.3 settings migrate without discarding unrelated configuration.

### Removed

- The previous link implementation (session, journal, recovery coordinator,
  transport lanes) with its TLA+ models, proofs and paired benchmark.

### Known issues

- In Pokemon mode, frames that queue up while one console stalls are not
  caught up afterwards, so the Cable Club can lag by about half a second
  until the players leave the room.

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
