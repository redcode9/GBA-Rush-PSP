# Suspend and resume implementation plan

Status: approved for implementation on 21 August 2026.

This plan implements `docs/design/suspend-resume.md` on the existing
`feature/link-cable` branch. It does not change the `1.0.0` tag or publish a
release.

## Stage 1: portable power lifecycle

### Files

- Add `source/src/power_lifecycle.h`.
- Add `source/src/power_lifecycle.c`.
- Add `tests/test_power_lifecycle.c`.
- Update the root and PSP Makefiles.

### Work

1. Define explicit running, suspended, and resume-pending states.
2. Keep automatic-save and resume-result delivery separate so neither event is
   lost when the PSP suspends before the emulation thread runs.
3. Use narrow transition and consume functions instead of exposing writable
   state to the rest of the emulator.
4. Apply a PSP memory barrier when publishing or consuming cross-thread state.
5. Keep the module free of PSP audio, file, menu, and network dependencies so
   its transitions can run in host tests.

### Tests

- Initial state and repeated initialization.
- Suspend followed by foreground handling.
- Suspend and resume before foreground handling.
- Exactly-once automatic-save delivery.
- Exactly-once resume delivery and audio result retention.
- Repeated suspend cycles and resume failure.

### Exit condition

The new host test passes with normal warnings-as-errors and with ASan/UBSan.

## Stage 2: non-blocking audio resume

### Files

- Update `source/src/main.c` and `source/src/main.h`.
- Update `source/src/sound.c` and `source/src/sound.h`.

### Work

1. Make power flags shared with the sound and callback threads explicitly
   volatile.
2. Replace the resume callback's call to `psp_sound_frequency()` with a
   dedicated resume helper that releases and reserves the SRC channel without
   draining queued samples or waiting on the sound semaphore.
3. Keep the worker parked until foreground resume cleanup has reset
   fast-forward, buffered samples, and GBA sound timing.
4. Clear the sleep flag after every resume attempt, including audio failure.
5. Move automatic savestate work out of the power callback and consume its
   pending request in the foreground power service.
6. Run the power service from gameplay and menu loops so a suspend/resume pair
   cannot be missed when both callbacks occur before the emulation thread is
   scheduled.
7. Treat a failed audio reservation as silent output and retain access to the
   menu and normal application exit.
8. Add finite deadlines to audio drain, semaphore acquisition, and sound-thread
   termination used outside the callback.

### Exit condition

No branch of the power callback waits for a semaphore, queued sample, worker,
network operation, file operation, or user input.

## Stage 3: bounded ad-hoc shutdown

### Files

- Update `source/src/psp_adhoc.c` and `source/src/psp_adhoc.h`.
- Update `source/src/main.c` where link shutdown is requested.

### Work

1. Publish stop requests with a memory barrier even when the command queue is
   full.
2. Replace the unlimited network-thread join with a finite first deadline.
3. On timeout, close the listener and connected socket to release pending PTP
   calls, then perform one final bounded join.
4. If the worker still cannot finish, terminate it, clean the tracked network
   resources in reverse initialization order, and report a shutdown error.
5. Reset all queues, parser state, identifiers, and module flags before a new
   host or join attempt.
6. Never reconnect a suspended session automatically.

### Exit condition

Suspend, disconnect, ROM change, and exit contain no unlimited wait on the
ad-hoc worker.

## Stage 4: lifecycle audit and guards

### Files

- Add `docs/review/runtime-lifecycle.md`.
- Add or update source-check scripts under `scripts/`.

### Work

1. Review every PSP callback, thread entry point, semaphore wait, thread join,
   blocking audio call, network loop, and power-managed file operation.
2. Classify findings by impact and identify whether each is fixed here,
   accepted by design, or deferred with a concrete reason.
3. Add source guards for the prohibited resume callback and unlimited ad-hoc
   join patterns.
4. Confirm that no generated binaries, ROMs, private saves, or runtime logs are
   added to Git.

### Exit condition

The audit has no unresolved issue capable of explaining the reported freeze,
and the source checks prevent the same blocking patterns from returning.

## Stage 5: verification and hardware candidate

1. Run `make check` and `make test-sanitize`.
2. Run source formatting, staged-diff, and repository-content checks.
3. Perform a clean PSP build with the pinned PSPDEV toolchain.
4. Inspect the linked ELF for the expected power, audio, and thread imports.
5. Build an EBOOT candidate without packaging, tagging, publishing, or pushing.
6. Install the candidate only after the PSP storage volume is connected.
7. Run the physical acceptance matrix in `docs/design/suspend-resume.md`, with
   ten consecutive Pokemon Red suspend cycles as the release gate.

The hardware result is recorded before the beta is packaged. Host tests and a
successful PSP build do not replace the real suspend/resume test.
