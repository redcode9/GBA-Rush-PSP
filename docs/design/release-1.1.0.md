# GBA Rush PSP 1.1.0 release criteria

## Scope

Version 1.1.0 adds two-player GBA link cable emulation over local PSP ad-hoc
Wi-Fi and repairs suspend and resume recovery. The release includes Normal
8-bit, Normal 32-bit, and Multiplayer 16-bit serial modes, session controls in
all five interface languages, diagnostic ROM sources, and regression coverage
for the protocol and emulated serial hardware.

Wireless Adapter, UART, General Purpose, JoyBus, GameCube link, multiboot,
three- or four-player sessions, and internet relay are outside this release.

## Review boundary

The release review covers the complete change from 1.0.0, including production
code, tests, translations, build rules, documentation, and packaging. The main
failure classes are malformed packet input, serial timing or interrupt errors,
session desynchronization, cross-thread publication, unbounded worker waits,
suspend ordering, incomplete cleanup, and accidental inclusion of private or
generated files.

Changes outside correctness, stability, user data, security, and release
quality are deferred to keep the reviewed delta focused.

## Required checks

The release commit must pass:

1. host regression tests with compiler warnings treated as errors;
2. AddressSanitizer and UndefinedBehaviorSanitizer tests;
3. translation-order, lifecycle, source-tree, and diff checks;
4. a clean emulator and exception-handler build with the PSP toolchain;
5. builds of all three diagnostic GBA link ROMs;
6. static analysis of the portable production modules;
7. extraction and exact-manifest verification of the release ZIP;
8. checksum verification before and after publication.

The archive is limited to `EBOOT.PBP`, `exception.prx`, installation notes,
changelog, and license. It must not contain a BIOS, ROM, save, savestate, cheat
list, configuration, log, or local build file.

## Accepted hardware limit

The release can be published without a completed broad two-PSP hardware
matrix. That limitation must remain visible in the changelog, link guide, and
release notes. It does not replace the automated protocol, timing, sanitizer,
diagnostic-ROM, or PSP build gates.
