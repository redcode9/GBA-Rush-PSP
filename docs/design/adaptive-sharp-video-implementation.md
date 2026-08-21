# Adaptive sharp video implementation plan

Status: approved for implementation on 21 August 2026.

This plan implements `docs/design/adaptive-sharp-video.md` for the feature
release following 1.1.0. Each stage retains a buildable libGU renderer and can
be validated independently on physical hardware.

## Stage 1: measurement and policy

### Files

- Add `source/src/video_adaptive.h` and `source/src/video_adaptive.c`.
- Add `tests/test_video_adaptive.c`.
- Update the root and PSP Makefiles.

### Work

1. Define `Sharp`, `Crisp`, and `Smooth` quality levels and explicit reasons
   for every automatic transition.
2. Implement fixed-size rolling timing windows and hysteresis without dynamic
   allocation in the frame loop.
3. Keep hardware observations separate from configuration so automatic mode
   cannot overwrite the user's manual settings.
4. Reset observations on ROM, clock, video mode, suspend, and resume changes.
5. Add an advisory per-ROM profile format with version and hardware identity.
6. Make missing, old, truncated, or invalid profile data fall back to a fresh
   measurement session.

### Tests

- Stable full-speed input remains at `Sharp`.
- Short spikes do not lower quality.
- Sustained misses select `Crisp` exactly once.
- Recovery requires the longer clean interval.
- Alternating boundary values do not cause oscillation.
- Reset events discard stale timing.
- Invalid cached profiles are ignored.

### Exit condition

The policy has deterministic host coverage and no PSP SDK dependency except a
narrow hardware-information adapter.

## Stage 2: safe present scheduling

### Files

- Update `source/src/psp_video.c` and `source/src/psp_video.h`.
- Update `source/src/main.c`.
- Extend video-related source checks.

### Work

1. Represent pending GE presentation explicitly.
2. Submit the current libGU list at GBA VBlank without waiting immediately.
3. Synchronize before buffer exchange and before any menu, copy, mode change,
   shutdown, or power operation that reuses video resources.
4. Keep a conservative synchronous path for initialization, error recovery,
   the first post-resume frame, and developer comparison.
5. Remove the per-frame clear only for viewports that cover all 480 by 272
   pixels.
6. Clear both buffers when switching to a bordered viewport.
7. Preserve current menu drawing order and volume overlay behaviour.

### Exit condition

The existing visual output is unchanged, host checks pass, the PSP build is
clean, and physical hardware survives ROM load, menu entry, ten suspend
cycles, and normal exit before a new filter is introduced.

## Stage 3: sharp two-pass GE renderer

### Files

- Update `source/src/psp_video.c`, `source/src/psp_video.h`, and
  `source/src/main.h`.
- Update graphics menu definitions and all five message tables.

### Work

1. Reserve and validate a 512 by 512 GU_PSM_5551 intermediate surface in
   EDRAM without overlapping framebuffers, the source texture, or icon data.
2. Build a first-pass list that scales 240 by 160 to 480 by 320 with
   `GU_NEAREST`.
3. Insert the required texture synchronization and cache flush before the
   intermediate surface is sampled.
4. Build a second-pass list that scales 480 by 320 to 480 by 272 with
   `GU_LINEAR` and verified texel-centre alignment.
5. Restore the display draw buffer and normal GU state before overlays or
   buffer exchange.
6. Keep single-pass nearest and bilinear implementations available.
7. Treat allocation or list construction failure as a recoverable transition
   to `Crisp`.

### Exit condition

Static patterns and scrolling scenes show no slice seams, missing border
pixels, half-pixel shift, stale texture data, or complete-frame blur.

## Stage 4: cache, upload, and slice benchmarks

### Files

- Update `source/src/psp_video.c`.
- Add bounded developer video statistics to the existing FPS diagnostics.

### Work

1. Replace global cache writeback in gameplay presentation with an aligned
   active-range writeback and test it without other cache changes.
2. Compare current direct EDRAM rendering against cached user-RAM rendering
   followed by `sceGuCopyImage` upload.
3. Compare safe texture slice widths and, if required, a two-dimensional slice
   grid on each available physical PSP model.
4. Measure swizzled sampling only with the conversion cost included.
5. Select a path from measured total frame cost, not isolated GE throughput.
6. Keep the old direct EDRAM path as a no-allocation fallback.

### Exit condition

Every retained optimization has an isolated real-hardware result and can be
disabled without changing image geometry or configuration compatibility.

## Stage 5: automatic integration

### Files

- Update `source/src/main.c`, `source/src/gui.c`, configuration loading, and
  the five language tables.
- Update user documentation.

### Work

1. Make `Automatic - Sharp fullscreen` the default for new configurations.
2. Migrate existing configuration files without reinterpreting their saved
   numeric mode values.
3. Feed frame, frameskip, GE-wait, audio, clock, and lifecycle observations to
   the policy through narrow functions.
4. Apply quality transitions only at a safe frame boundary.
5. Persist advisory ROM results without writing continuously during gameplay.
6. Expose clear manual choices and the active automatic level in diagnostics.
7. Ensure link and cheat activity affect only measured headroom, never image
   rules or game compatibility settings.

### Exit condition

Automatic mode chooses the highest stable sharp path, remains steady under
short spikes, and never changes unrelated emulator settings.

## Stage 6: power and regression hardening

### Files

- Update `source/src/main.c`, `source/src/psp_video.c`, and lifecycle checks.
- Extend host tests where state can be isolated from the PSP SDK.

### Work

1. Stop new submissions when suspend is observed.
2. Reconcile pending GE work in the foreground power service.
3. Rebuild all transient video state on resume and force one synchronized
   present.
4. Reset adaptive timing without deleting the per-ROM advisory profile.
5. Exercise menus, savestates, screenshots, volume overlays, fast-forward,
   link shutdown, ROM replacement, and exit around pending frames.
6. Repeat the complete 1.1.0 suspend acceptance matrix.

### Exit condition

Pokemon Red survives ten consecutive suspend cycles with automatic sharp video
enabled, and every lifecycle path retains menu and normal-exit access.

## Stage 7: release verification

1. Run normal and sanitizer host suites.
2. Run repository, documentation, translation, and staged-diff checks.
3. Produce a clean PSP build with the pinned toolchain.
4. Install only the EBOOT over an existing emulator directory, retaining ROMs,
   saves, cheats, and user configuration.
5. Record output comparisons and timing results for each tested PSP model.
6. Complete the hardware matrix in the design document.
7. Update changelog, release notes, checksums, and version metadata for 1.2.0
   only after the hardware candidate passes.

Host tests and a successful PSP build do not replace image-quality,
performance, and suspend testing on a physical PSP.
