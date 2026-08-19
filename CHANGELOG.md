# Changelog

## 1.1.0-beta.1 - Unreleased

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

### Limitations

- This beta supports two PSP systems and GBA cable modes only.
- GBA Wireless Adapter, UART, General Purpose, JoyBus, GameCube link,
  multiboot, and internet relay modes are not supported.
- Physical two-console acceptance and performance testing remain required
  before the beta package is published.

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
