# Runtime lifecycle review

Reviewed on 21 August 2026 for GBA Rush PSP 1.1.0.

## Scope

The review covered PSP callbacks, foreground polling loops, worker threads,
semaphore waits, audio output, ad-hoc networking, power-managed file access,
ROM replacement, reset, menu sleep, and application exit. Vendored zlib and
emulation loops whose termination is controlled by emulated hardware were not
treated as PSP lifecycle waits.

## Closed findings

### Blocking audio work in the power callback

The resume callback called `psp_sound_frequency()`, which could wait forever
for the sound semaphore or for queued samples to drain from a suspended SRC
channel. Resume now uses a dedicated release-and-reserve path with no drain or
semaphore wait. The foreground thread resets audio timing and buffers before it
wakes the sound worker. Reservation failure leaves gameplay running silently.

### Lost foreground recovery event

The old sleep flags could complete an entire suspend and resume cycle before
the foreground thread observed either state. A sequenced lifecycle state now
delivers the pending automatic save and resume cleanup exactly once, including
when both callbacks run before the emulator is scheduled again.

### File and network work during suspend

Automatic savestates, ROM-handle replacement, and logical link teardown now
run in the foreground power service. The callback only publishes state and
performs the minimum SRC reset required by PSP resume. Gameplay, menus, and
dialogs all reach the same service path.

### Unlimited sound waits

Sound semaphore acquisition, queued-sample drain, sound-thread termination,
and the foreground audio backlog loop now have deadlines. A stalled worker or
audio channel drops pending samples instead of taking away menu and exit
control.

### Unlimited ad-hoc shutdown

Network-thread shutdown now has a first deadline, closes claimed socket and
listener identifiers to release pending calls, and has a final deadline. A
worker that still does not finish is terminated before tracked PSP network
resources are released. Connection identifiers are claimed before close so
foreground recovery and worker cleanup cannot close the same identifier.

### Callback initialization race

Power callback registration now occurs after sound initialization. A power
event therefore cannot reach an uninitialized sound semaphore or SRC channel.

## Accepted waits

- `sceAudioSRCOutputBlocking()` remains in the dedicated sound worker. This is
  the normal PSP audio pacing primitive; resume releases its stale channel and
  sound termination has a finite fallback.
- The sound worker may sleep while paused, under fast-forward, or while waiting
  for samples. Resume and termination explicitly wake it.
- The callback thread sleeps with callback processing enabled. This is the
  standard dispatcher lifetime and is not joined by the foreground thread.
- Ad-hoc scan, connect, send, receive, and control polling are non-blocking or
  deadline-controlled and also observe the stop request.
- The exit callback retains synchronous save and configuration writes so HOME
  exit does not discard user data. Every emulator-owned worker wait reached by
  that path is now bounded.

## Guards

`scripts/check-lifecycle.sh` rejects blocking kernel, file, network, and UI
work in the power callback, unlimited sound semaphore waits, unlimited sound or
network thread joins, and removal of the audio backlog recovery deadline.

## Hardware validation

The source review, host regression suite, sanitizer suite, and PSP build cannot
reproduce every power-controller timing case. The hardware matrix in
`docs/design/suspend-resume.md`, led by ten consecutive suspend and resume
cycles during Pokemon Red gameplay, remains the recommended physical test for
each supported PSP model.
