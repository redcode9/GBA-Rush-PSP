# Adaptive sharp video implementation

Status: completed for GBA Rush PSP 1.2.0.

## Included

- `video_adaptive.c` owns the fixed-window policy and hysteresis.
- `psp_video.c` owns GE timing, asynchronous presentation, EDRAM allocation,
  two-pass rendering, fallback, and power-state recovery.
- `16:9 Sharp` and `16:9 Auto` use new scale values; the existing values keep
  their configuration meaning.
- Fullscreen lists omit the per-frame clear. Bordered modes retain it.
- The first frame after resume is synchronized before normal overlap resumes.
- Translation tables expose both modes in all five interfaces.

The intermediate surface occupies a 512 by 512 RGB5551 allocation after both
display buffers and the 256 by 256 source texture. A compile-time bound rejects
layouts beyond the PSP's 2 MiB EDRAM.

If display-list vertex allocation fails, `Sharp` completes the partial list,
waits for the GE, and changes to `Crisp` instead of terminating the emulator.

## Checks

- Host tests cover thresholds, hysteresis, short spikes, skipped frames,
  resets, manual mode, and cached-profile validation.
- Sanitizer tests run the same policy cases.
- Source checks prohibit raw GE queue submission and video waits in the power
  callback.
- The PSP build uses PSPDEV v20260801.
- Physical PSP testing confirmed that the candidate starts and renders a game.

## Deferred

- Per-ROM profile persistence remains advisory code only.
- Audio recovery events are not yet connected to the policy input.
- Cache-range and texture-upload changes require separate hardware profiling;
  1.2.0 keeps the established cache writeback path.
