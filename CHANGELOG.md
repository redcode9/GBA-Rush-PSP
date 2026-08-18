# Changelog

## Unreleased

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
