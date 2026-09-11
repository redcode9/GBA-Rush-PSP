# M8 No-ROM Session Startup Design

Date: 2026-09-11

Status: user approved the narrow correction and regression scope in chat;
this written specification is awaiting user review before implementation.

## Scope and evidence

Restore Create Session and Join Session from a fresh no-ROM menu without
resetting an already loaded game's SIO state. This is a prerequisite for the
existing benchmark, not a fix or acceptance claim for the FireRed stalls.

The RC3.1 PSP-2004 dump, photo, matching ELF and sanitizer reproducer agree:
`main_link_start -> link_session_host/join -> link_session_reset_runtime ->
gba_sio_disconnect -> gba_sio_refresh_control` reads SIOCNT through a null
register pointer. Only ROM reset currently initializes that pointer.
Immutable evidence is retained under
`build/hardware-logs/20260911-m8-v3-rc31-no-rom-crash/`; CONTRIBUTING.md remains
the canonical acquisition, hash and execution record.

## Approaches considered

1. **Selected: prepare at the common link-start boundary, plus validate the
   session dependency.** A small host-testable SIO operation initializes an
   unbound instance once and leaves a valid instance unchanged. Both menu
   roles share this boundary. This directly covers the missing precondition
   without changing unrelated emulator startup.
2. **Initialize SIO during application boot, plus session validation.** Also
   addresses the cold path, but depends on broader boot ordering and needs
   coverage for every alternate startup path. Not selected for this patch.
3. **Reject null register storage only.** Prevents a crash but still prevents
   the advertised no-ROM benchmark from starting. Insufficient by itself.

## Initialization contract

Add the preparation operation alongside the existing SIO initialization API
in `gba_sio.c/.h`, using the existing caller-owned `io_registers` storage.

- Reject a null SIO object or null supplied storage before dereferencing it.
- If the SIO register pointer is null, call the existing initializer with the
  supplied storage. Reuse its established SIOCNT/RCNT defaults; do not invent
  benchmark-specific register values or clear unrelated I/O registers.
- If the SIO already references that storage, return success without changing
  any SIO fields or register bytes. Repeated preparation is a strict no-op.
- If it references different storage, reject without rebinding or changing
  either memory region. The caller must resolve ownership explicitly.
- Allocate nothing and perform no PSP, networking or filesystem calls.

`main_link_start` invokes preparation on the emulation/UI thread before its
first session reset/disconnect or network-worker startup. Both host and join
use this same path. Preparation failure returns through the existing failed
start result without starting transport work or resetting game state.

Independently, session admission must require both a non-null SIO instance
and non-null register storage. `link_session_host` and `link_session_join`
reject an unprepared instance before runtime reset or any transport callback.
This protects callers that bypass application preparation; the session layer
does not allocate or choose a substitute GBA register bank.

Do not call `reset_gba`, `init_memory` or unconditional `gba_sio_init` on each
link attempt. A real ROM reset retains its existing initialization semantics.
Preparation must not alter the normal link-start disconnect/cancellation
semantics; those may legitimately update link status pins or an existing arm.

## Regression requirements

Add focused tests before the production changes and observe the expected
failure against the unchanged code. Keep the original diagnostic reproducer
and captured failing output as evidence rather than rewriting them.

- Session host/join reject null SIO or null register storage safely, with no
  transport calls or partial session mutation.
- Cold zero-initialized SIO is prepared with the real production helper;
  host and join reach their transport entry points without loading a ROM.
- Preparation rejects invalid/mismatched storage without mutation.
- Repeated preparation preserves every SIO byte and register byte, including
  a game-like configured multiplayer mode, data, cycle and pending state.
- Unrelated register sentinels survive cold preparation.
- No-ROM host/join cancellation and retry remain safe, including a role
  change after clean disconnect and preparation after a real SIO reset.
- Existing transfer, cancellation, exact-once and recovery tests still pass.

Exercise the production helper in native and ASan/UBSan tests. Add a focused
wiring check that the shared application start path calls it before its first
SIO-consuming transition/network startup. This source-order check supplements
the runtime tests; it does not simulate the PSP menu or prove hardware safety.

## Gates and delivery

Run the focused SIO/session tests, full `make check`, `make test-sanitize`,
and a clean PSP build with the pinned local toolchain. Recheck proof/model
inputs and run the applicable existing gates before any SD installation.
Preserve the RC3.1 ELF/PRX/EBOOT used for symbolication before a clean rebuild.
Verify the new EBOOT's embedded PRX, package and hashes; identify the candidate
as a new patch build rather than silently replacing the RC3.1 artifact.

Only install after software/build gates pass, with current device identity
checks, backups, byte comparison and the required external-volume permission.
Do not change ROMs, BIOS files, saves, settings, existing logs or the dump.

Hardware acceptance for this correction starts with fresh no-ROM host and
join launches on both PSP models, cancel/retry and normal exit, followed by a
loaded-ROM smoke check. The ten-minute benchmark in both role assignments
and the FireRed trade matrix remain separate, still-unpassed M8 gates.

## Explicit exclusions

No changes to wire protocol, heartbeat cadence, clock pacing, recovery policy,
thread priorities, WLAN power saving, audio policy or exception-log flushing.
Do not claim that stopping this crash resolves the earlier trade failures.
