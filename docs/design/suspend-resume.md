# PSP suspend and resume hardening

Status: approved for implementation on 21 August 2026.

## Objective

Restore reliable suspend and resume during normal gameplay without changing
emulation state, saves, controls, or single-player performance. The emulator
must always return control to the user after resume, even when an optional
subsystem cannot be restored.

## Evidence

The freeze is reproducible during Pokemon Red gameplay without fast-forward
and without starting a link session. This excludes the active link transport
and fast-forward hold path as direct causes.

The current resume callback calls `psp_sound_frequency()`. That function waits
without a timeout for the audio semaphore and then waits for the hardware
output queue to become empty before releasing the channel. A suspended audio
channel may retain queued samples without consuming them, so the callback can
remain inside that wait and never clear `sleep_flag`.

Earlier gpSP PSP code releases and reserves the SRC channel directly during
resume. The initial fix restores that non-draining behavior while preserving
the newer sound worker and its semaphore during ordinary runtime.

The review also found an independent dormant risk: `psp_adhoc_stop()` waits
without a timeout for the network thread. It cannot explain a freeze when the
link feature has never been started, but it must not remain an unlimited wait
in a power-management path.

## Power callback contract

The power callback may update small shared flags and call only the audio
operations required by the PSP resume event. It must not wait for queued audio,
wait indefinitely on a semaphore, stop a worker synchronously, display a
dialog, or perform network teardown.

On suspend it marks the emulator asleep and records one pending automatic
save. The existing foreground sleep path performs file handling and the save
at most once. If the PSP suspends before that path runs, the pending request
survives resume and is serviced before emulation continues.

On resume it:

1. keeps the sound worker parked;
2. releases the stale SRC channel without draining it;
3. attempts to reserve a fresh channel;
4. records the result for the foreground thread;
5. clears the sleep state regardless of the audio result.

An audio error must degrade to silent gameplay rather than leave the emulator
or callback thread blocked.

## Shared state and audio recovery

Power and audio flags accessed by multiple PSP threads use explicit volatile
storage, PSP-safe memory barriers, and narrow helper functions. The callback
owns the transition out of sleep; the emulation thread owns post-resume
cleanup.

Post-resume cleanup disables fast-forward, discards stale buffered samples,
realigns sound timing with the current GBA tick count, and wakes the sound
worker only after the channel reservation attempt has finished. Normal audio
buffer synchronization remains unchanged outside suspend and resume.

The automatic sleep savestate remains compatible with existing files. The
resume fix does not add power state to native savestates.

## Link transport shutdown

Suspend closes the logical GBA link session immediately and requests transport
shutdown without waiting inside the power callback. Network operations keep
their existing finite socket and control timeouts.

The foreground shutdown path gains a bounded thread join. If the network
worker misses the first deadline, the caller closes its listener and connected
socket to unblock pending calls and waits once more. If it still cannot exit,
the worker is terminated, the transport is marked unusable until its PSP
network modules have been cleaned up, and the emulator continues its resume or
exit path instead of waiting forever. A new link attempt starts from a freshly
initialized transport context; an old session is never resumed automatically.

## Review scope

The accompanying code review covers the complete PSP-facing codebase for:

- waits, polling loops, and blocking calls without a deadline;
- callback functions that perform file, audio, network, or UI work;
- flags shared between the emulator, sound, callback, and network threads;
- semaphore ownership and worker termination;
- suspend, resume, ROM replacement, reset, menu sleep, and application exit;
- failure paths that could make the HOME menu or normal exit unavailable.

Findings unrelated to suspend and resume are documented separately and are not
mixed into this fix unless they can cause a deadlock, data loss, or forced
power-off.

## Verification

Host tests cover the power-state transitions and prove that audio failure still
releases the emulation thread. Existing CodeBreaker, GBA SIO, protocol, and
link-session suites must continue to pass under their normal and sanitizer
builds.

The PSP target must build cleanly with the pinned toolchain. Source checks must
reject any newly introduced unbounded wait in the power and ad-hoc lifecycle
helpers.

Real-hardware acceptance uses these cases:

1. suspend and resume Pokemon Red ten consecutive times during gameplay;
2. suspend from the emulator menu with a ROM loaded;
3. suspend from the ROM browser without a loaded game;
4. use fast-forward, release it, then suspend and resume;
5. suspend while a link session is hosting, joining, idle, and connected;
6. resume with the WLAN switch both enabled and disabled;
7. enter the emulator menu and exit normally after every recovery case.

## Acceptance criteria

The fix is complete when the automated suites and PSP build pass, Pokemon Red
survives ten consecutive hardware suspend cycles without a freeze, audio
returns or fails silently without blocking, and every test case retains access
to the emulator menu and normal exit.
